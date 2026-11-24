//! Engineering-wide rename: the atomic applier (batch 20261110, `rename-core`).
//!
//! Write every planned file, or put every byte back. The flow is deliberately
//! boring:
//!
//! 1. **Read first.** Every planned file is read before the first write, so an
//!    unreadable file fails before anything changed.
//! 2. **Splice in reverse.** Each hit is applied from the LAST byte offset to the
//!    first, so earlier offsets stay valid while the file's length changes. Each
//!    hit's bytes are checked against the planned identifier first — a file that
//!    changed under the plan is refused, never silently patched.
//! 3. **Temp + rename per file.** The new content goes to a sibling temporary
//!    file and `rename(2)` replaces the original, so a reader never sees a
//!    half-written file.
//! 4. **Rollback on any failure.** Every replaced file keeps its original bytes;
//!    a failure (or a cancel) restores them all and names the file that failed.
//! 5. **Verify.** After the writes, every file is re-read and the planned
//!    replacement is looked for at the shifted offsets; `verified` reports what
//!    was actually observed.
//!
//! Multi-file atomicity here is "rollback", not a filesystem transaction: a crash
//! between two renames can leave the earlier files renamed (their bytes are
//! recorded only in memory). That bound is stated in the evidence, not hidden:
//! the alternative — one shared directory journal — would be a different product.

use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::rename_index::{RenamePlan, MAX_APPLY_BYTES};

/// How an apply ended.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ApplyStatus {
    /// Every file was rewritten and verified.
    Applied,
    /// A failure after the first write: every file was restored.
    RolledBack,
    /// The caller cancelled: every file was restored.
    Cancelled,
    /// `dry_run`: every target was checked writable and nothing was written.
    DryRunOk,
    /// `dry_run`: a target is not writable; nothing was written.
    DryRunUnwritable,
}

impl ApplyStatus {
    /// Stable label for logs.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Applied => "applied",
            Self::RolledBack => "rolled_back",
            Self::Cancelled => "cancelled",
            Self::DryRunOk => "dry_run_ok",
            Self::DryRunUnwritable => "dry_run_unwritable",
        }
    }
}

/// What one apply did (or refused to do).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ApplyOutcome {
    /// Files written (0 for a dry run, a rollback or a cancel).
    pub applied_files: usize,
    /// Replacements written (0 for a dry run, a rollback or a cancel).
    pub applied_occurrences: usize,
    /// The file that could not be written, when there was one.
    pub failed_file: Option<PathBuf>,
    /// True when every previously written file was restored byte for byte.
    pub rolled_back: bool,
    /// True when every written file was re-read and the replacement was found.
    pub verified: bool,
    /// The terminal state.
    pub status: ApplyStatus,
    /// One line for the user when the outcome is not `Applied`/`DryRunOk`.
    pub message: Option<String>,
}

impl ApplyOutcome {
    fn failure(status: ApplyStatus, failed_file: Option<PathBuf>, message: String) -> Self {
        Self {
            applied_files: 0,
            applied_occurrences: 0,
            failed_file,
            rolled_back: status == ApplyStatus::RolledBack,
            verified: false,
            status,
            message: Some(message),
        }
    }
}

/// Apply a plan, atomically with rollback.
///
/// `cancelled` is polled between files (and between the read phase and the write
/// phase); a `true` aborts with [`ApplyStatus::Cancelled`] and restores every
/// file already written. `dry_run` checks writability and writes nothing.
pub fn apply(plan: &RenamePlan, dry_run: bool, cancelled: &dyn Fn() -> bool) -> ApplyOutcome {
    // 1. Read + splice everything BEFORE the first write.
    let mut prepared: Vec<Prepared> = Vec::new();
    for file_plan in &plan.plans {
        if cancelled() {
            return ApplyOutcome::failure(
                ApplyStatus::Cancelled,
                None,
                "cancelled before any write".to_string(),
            );
        }
        let original = match fs::read(&file_plan.file) {
            Ok(bytes) => bytes,
            Err(error) => {
                return ApplyOutcome::failure(
                    ApplyStatus::RolledBack,
                    Some(file_plan.file.clone()),
                    format!("reading {} failed: {error}", file_plan.file.display()),
                );
            }
        };
        if original.len() > MAX_APPLY_BYTES {
            return ApplyOutcome::failure(
                ApplyStatus::RolledBack,
                Some(file_plan.file.clone()),
                format!(
                    "{} is larger than the apply bound ({} bytes)",
                    file_plan.file.display(),
                    MAX_APPLY_BYTES
                ),
            );
        }
        let updated = match splice(&original, file_plan, &plan.from, &plan.to) {
            Ok(updated) => updated,
            Err(message) => {
                return ApplyOutcome::failure(
                    ApplyStatus::RolledBack,
                    Some(file_plan.file.clone()),
                    format!("{}: {message}", file_plan.file.display()),
                );
            }
        };
        prepared.push(Prepared {
            file: file_plan.file.clone(),
            original,
            updated,
            replaced: false,
            hits: file_plan.hits.len(),
        });
    }

    // 2. Dry run: only writability.
    if dry_run {
        for item in &prepared {
            if cancelled() {
                return ApplyOutcome::failure(
                    ApplyStatus::Cancelled,
                    None,
                    "cancelled during the dry run".to_string(),
                );
            }
            if let Err(error) = check_writable(&item.file) {
                return ApplyOutcome::failure(
                    ApplyStatus::DryRunUnwritable,
                    Some(item.file.clone()),
                    format!("{} would not be writable: {error}", item.file.display()),
                );
            }
        }
        return ApplyOutcome {
            applied_files: 0,
            applied_occurrences: 0,
            failed_file: None,
            rolled_back: false,
            verified: false,
            status: ApplyStatus::DryRunOk,
            message: None,
        };
    }

    // 3. Write, remembering the original bytes.
    for index in 0..prepared.len() {
        if cancelled() {
            let restored = restore(&mut prepared, index);
            return ApplyOutcome {
                applied_files: 0,
                applied_occurrences: 0,
                failed_file: None,
                rolled_back: restored,
                verified: false,
                status: ApplyStatus::Cancelled,
                message: Some("cancelled: every written file was restored".to_string()),
            };
        }
        let file = prepared[index].file.clone();
        match replace_file(&file, &prepared[index].updated) {
            Ok(()) => prepared[index].replaced = true,
            Err(error) => {
                let restored = restore(&mut prepared, index);
                return ApplyOutcome {
                    applied_files: 0,
                    applied_occurrences: 0,
                    failed_file: Some(file.clone()),
                    rolled_back: restored,
                    verified: false,
                    status: ApplyStatus::RolledBack,
                    message: Some(format!("writing {} failed: {error}", file.display())),
                };
            }
        }
    }

    // 4. Verify by re-reading.
    let mut verified = true;
    let mut applied_occurrences = 0usize;
    for item in &prepared {
        applied_occurrences += item.hits;
        match verify(&item.file, &plan.plans, &plan.to) {
            Ok(true) => {}
            Ok(false) => verified = false,
            Err(_) => verified = false,
        }
    }
    ApplyOutcome {
        applied_files: prepared.len(),
        applied_occurrences,
        failed_file: None,
        rolled_back: false,
        verified,
        status: ApplyStatus::Applied,
        message: None,
    }
}

struct Prepared {
    file: PathBuf,
    original: Vec<u8>,
    updated: Vec<u8>,
    replaced: bool,
    hits: usize,
}

/// Apply every hit from the last offset to the first, checking the old bytes.
fn splice(
    original: &[u8],
    file_plan: &crate::rename_index::FilePlan,
    from: &str,
    to: &str,
) -> Result<Vec<u8>, String> {
    if file_plan.truncated {
        return Err("the preview is truncated: refusing to apply a partial plan".to_string());
    }
    let mut hits = file_plan.hits.clone();
    hits.sort_by_key(|hit| hit.byte_offset);
    let mut output = original.to_vec();
    for hit in hits.iter().rev() {
        let start =
            usize::try_from(hit.byte_offset).map_err(|_| "hit offset overflows".to_string())?;
        let end = start
            .checked_add(
                usize::try_from(hit.byte_len).map_err(|_| "hit length overflows".to_string())?,
            )
            .ok_or_else(|| "hit range overflows".to_string())?;
        let slice = output
            .get(start..end)
            .ok_or_else(|| format!("hit at byte {start}..{end} is outside the file"))?;
        if slice != from.as_bytes() {
            return Err(format!(
                "the file changed since the plan: expected `{from}` at byte {start}, found `{}`",
                String::from_utf8_lossy(slice)
            ));
        }
        let replacement = to.as_bytes();
        output.splice(start..end, replacement.iter().copied());
    }
    Ok(output)
}

fn check_writable(path: &Path) -> std::io::Result<()> {
    let parent = path
        .parent()
        .ok_or_else(|| std::io::Error::other("the file has no parent directory"))?;
    let metadata = fs::metadata(parent)?;
    if metadata.permissions().readonly() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::PermissionDenied,
            "the parent directory is read-only",
        ));
    }
    let file = fs::OpenOptions::new().write(true).open(path)?;
    drop(file);
    Ok(())
}

/// Write `content` through a sibling temporary file and `rename` it over `path`.
fn replace_file(path: &Path, content: &[u8]) -> std::io::Result<()> {
    let parent = path
        .parent()
        .ok_or_else(|| std::io::Error::other("the file has no parent directory"))?;
    let temporary = parent.join(format!(
        ".{}.vdrn.tmp",
        path.file_name()
            .map(|name| name.to_string_lossy().into_owned())
            .unwrap_or_else(|| "file".to_string())
    ));
    {
        let mut handle = fs::File::create(&temporary)?;
        handle.write_all(content)?;
        handle.sync_all()?;
    }
    fs::rename(&temporary, path).inspect_err(|_| {
        let _ = fs::remove_file(&temporary);
    })
}

/// Restore every file replaced so far, newest first. Returns true when all were
/// restored byte for byte.
fn restore(prepared: &mut [Prepared], upto: usize) -> bool {
    let mut restored = true;
    for index in (0..upto.min(prepared.len())).rev() {
        if !prepared[index].replaced {
            continue;
        }
        if replace_file(&prepared[index].file, &prepared[index].original).is_err() {
            restored = false;
        }
        prepared[index].replaced = false;
    }
    restored
}

/// Re-read a file and confirm the replacement is where the shifted offsets say.
fn verify(file: &Path, plans: &[crate::rename_index::FilePlan], to: &str) -> std::io::Result<bool> {
    let Some(file_plan) = plans.iter().find(|plan| plan.file == file) else {
        return Ok(false);
    };
    let text = fs::read(file)?;
    let mut delta: i64 = 0;
    for hit in &file_plan.hits {
        let start = hit.byte_offset as i64 + delta;
        let start = usize::try_from(start).unwrap_or(usize::MAX);
        let want = to.as_bytes();
        let got = text.get(start..start + want.len());
        if got != Some(want) {
            return Ok(false);
        }
        delta += want.len() as i64 - hit.byte_len as i64;
    }
    Ok(true)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rename_index::{
        index, plan, resolve, FilePlan, Occurrence, PlanOutcome, Project, RenamePlan,
    };
    use std::path::{Path, PathBuf};

    type TestResult = Result<(), Box<dyn std::error::Error>>;

    fn fixture_root() -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/rename")
    }

    /// Copy the fixture into a scratch directory so a test never edits the repo.
    fn fixture_copy() -> Result<tempfile::TempDir, String> {
        let source = fixture_root();
        let temp = tempfile::tempdir().map_err(|error| error.to_string())?;
        for relative in [
            "rtl/pkg.sv",
            "rtl/alpha.sv",
            "rtl/beta.sv",
            "rtl/gamma.sv",
            "inc/defs.vh",
        ] {
            let target = temp.path().join(relative);
            std::fs::create_dir_all(target.parent().ok_or("parent")?)
                .map_err(|error| error.to_string())?;
            std::fs::copy(source.join(relative), &target).map_err(|error| error.to_string())?;
        }
        Ok(temp)
    }

    /// The engine's project for one scratch copy (pinned slang when present).
    fn fixture_project(root: &Path) -> Project {
        let mut project = Project::new(
            root,
            vec![
                root.join("rtl/pkg.sv"),
                root.join("rtl/alpha.sv"),
                root.join("rtl/beta.sv"),
                root.join("rtl/gamma.sv"),
            ],
        );
        project.include_dirs = vec![root.join("inc")];
        let repo = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
        let candidate = repo.join("build/slang/out/slang");
        project.slang_binary = candidate.is_file().then_some(candidate);
        project
    }

    fn scratch_project() -> Result<(tempfile::TempDir, RenamePlan), String> {
        let temp = fixture_copy()?;
        let project = fixture_project(temp.path());
        let index = index(&project).map_err(|error| error.message())?;
        let outcome =
            plan(&project, &index, "alpha_cnt", "alpha_count", 200).map_err(|e| e.message())?;
        let PlanOutcome::Plan(plan) = outcome else {
            return Err("expected a plan".into());
        };
        Ok((temp, plan))
    }

    /// A hand-built two-file plan: the applier is tested independently of the
    /// planner (a parameter rename is refused, so the fixture cannot produce a
    /// cross-file plan).
    fn manual_two_file_plan(root: &Path) -> Result<RenamePlan, String> {
        let mut plans = Vec::new();
        let mut occurrences = 0usize;
        for name in ["alpha.sv", "beta.sv"] {
            let path = root.join(name);
            let text = format!("// {name}\nlogic MARKER;\nassign MARKER = 1'b0;\n");
            std::fs::write(&path, &text).map_err(|error| error.to_string())?;
            let hits: Vec<Occurrence> = text
                .match_indices("MARKER")
                .map(|(offset, _)| Occurrence {
                    file: path.clone(),
                    line: 1,
                    column: 0,
                    end_column: 0,
                    byte_offset: offset as u64,
                    byte_len: "MARKER".len() as u64,
                    before: String::new(),
                    after: String::new(),
                })
                .collect();
            occurrences += hits.len();
            plans.push(FilePlan {
                file: path,
                hits,
                truncated: false,
            });
        }
        Ok(RenamePlan {
            root: root.to_path_buf(),
            from: "MARKER".to_string(),
            to: "RENAMED".to_string(),
            files: plans.len(),
            occurrences,
            plans,
            warnings: Vec::new(),
        })
    }

    fn contents(root: &Path, relative: &str) -> Result<String, String> {
        std::fs::read_to_string(root.join(relative)).map_err(|error| error.to_string())
    }

    fn never() -> bool {
        false
    }

    #[test]
    fn apply_rewrites_every_occurrence_and_verifies() -> TestResult {
        let (temp, plan) = scratch_project()?;
        let outcome = apply(&plan, false, &never);
        assert_eq!(
            outcome.status,
            ApplyStatus::Applied,
            "{:?}",
            outcome.message
        );
        assert!(outcome.verified);
        assert_eq!(outcome.applied_files, plan.plans.len());
        let alpha = contents(temp.path(), "rtl/alpha.sv")?;
        assert!(alpha.contains("alpha_count"), "{alpha}");
        assert!(!alpha.contains("alpha_cnt "), "{alpha}");
        assert!(contents(temp.path(), "rtl/gamma.sv")?.contains("cnt"));
        Ok(())
    }

    #[test]
    fn a_dry_run_writes_nothing() -> TestResult {
        let (temp, plan) = scratch_project()?;
        let before = contents(temp.path(), "rtl/alpha.sv")?;
        let outcome = apply(&plan, true, &never);
        assert_eq!(outcome.status, ApplyStatus::DryRunOk);
        assert_eq!(outcome.applied_files, 0);
        assert_eq!(contents(temp.path(), "rtl/alpha.sv")?, before);
        Ok(())
    }

    #[test]
    fn a_cancel_before_the_first_write_changes_nothing() -> TestResult {
        let (temp, plan) = scratch_project()?;
        let before = contents(temp.path(), "rtl/alpha.sv")?;
        let outcome = apply(&plan, false, &|| true);
        assert_eq!(outcome.status, ApplyStatus::Cancelled);
        assert_eq!(contents(temp.path(), "rtl/alpha.sv")?, before);
        Ok(())
    }

    #[test]
    fn a_cancel_after_the_first_write_restores_it() -> TestResult {
        use std::sync::atomic::{AtomicUsize, Ordering};
        let temp = tempfile::tempdir()?;
        let plan = manual_two_file_plan(temp.path())?;
        let first_before = std::fs::read(&plan.plans[0].file)?;
        // Calls: two in the read phase, then one per write. `> 3` cancels at the
        // SECOND write, i.e. after the first file was already replaced.
        let calls = AtomicUsize::new(0);
        let outcome = apply(&plan, false, &|| {
            let seen = calls.fetch_add(1, Ordering::SeqCst) + 1;
            seen > 3
        });
        assert_eq!(
            outcome.status,
            ApplyStatus::Cancelled,
            "{:?}",
            outcome.message
        );
        assert!(outcome.rolled_back);
        assert_eq!(std::fs::read(&plan.plans[0].file)?, first_before);
        Ok(())
    }

    #[test]
    fn an_unwritable_file_rolls_back_everything() -> TestResult {
        let temp = tempfile::tempdir()?;
        let plan = manual_two_file_plan(temp.path())?;
        let second = plan.plans[1].file.clone();
        let before_first = std::fs::read(&plan.plans[0].file)?;
        let before_second = std::fs::read(&second)?;
        // A directory where the applier's sibling temp file goes makes the
        // SECOND write fail even for root (EISDIR), after the first succeeded.
        let collision = second.parent().ok_or("parent")?.join(format!(
            ".{}.vdrn.tmp",
            second.file_name().map_or_else(
                || "file".to_string(),
                |name| name.to_string_lossy().into_owned(),
            )
        ));
        std::fs::create_dir_all(&collision)?;
        let outcome = apply(&plan, false, &never);
        assert_eq!(
            outcome.status,
            ApplyStatus::RolledBack,
            "{:?}",
            outcome.message
        );
        assert!(outcome.rolled_back);
        assert_eq!(std::fs::read(&plan.plans[0].file)?, before_first);
        assert_eq!(std::fs::read(&second)?, before_second);
        Ok(())
    }

    #[test]
    fn a_file_that_changed_under_the_plan_is_refused() -> TestResult {
        // The temp directory must stay alive for the whole test: `_temp` is a
        // real binding, not a discarded value (the plan's paths live inside it).
        let (_temp, plan) = scratch_project()?;
        let path = &plan.plans[0].file;
        let text = std::fs::read_to_string(path)?;
        let text = text.replace("alpha_cnt", "someone_else");
        std::fs::write(path, &text)?;
        let outcome = apply(&plan, false, &never);
        assert_eq!(outcome.status, ApplyStatus::RolledBack);
        assert!(outcome
            .message
            .as_deref()
            .unwrap_or("")
            .contains("changed since the plan"));
        Ok(())
    }

    /// sha256 of one file through the host's `sha256sum` (the acceptance asks for
    /// byte-identity, and the repo's other evidence tests measure it the same way).
    fn sha256(path: &Path) -> String {
        let output = std::process::Command::new("sha256sum")
            .arg(path)
            .output()
            .unwrap_or_else(|error| panic!("sha256sum could not run: {error}"));
        assert!(
            output.status.success(),
            "sha256sum failed on {}",
            path.display()
        );
        String::from_utf8_lossy(&output.stdout)
            .split_whitespace()
            .next()
            .unwrap_or_else(|| panic!("sha256sum printed nothing for {}", path.display()))
            .to_string()
    }

    const FIXTURE_FILES: [&str; 4] = ["rtl/pkg.sv", "rtl/alpha.sv", "rtl/beta.sv", "rtl/gamma.sv"];

    /// AC-2.3 preview: every entry carries an ABSOLUTE path, line, column and the
    /// before/after line; entries are grouped per file and the total equals the
    /// number of replacements.
    #[test]
    fn a_preview_is_grouped_per_absolute_file_with_before_and_after() -> TestResult {
        let (temp, plan) = scratch_project()?;
        let total: usize = plan.plans.iter().map(|file| file.hits.len()).sum();
        assert_eq!(plan.occurrences, total);
        assert_eq!(total, 3, "declaration + two references");
        for file in &plan.plans {
            assert!(file.file.is_absolute(), "{:?}", file.file);
            assert!(file.file.starts_with(temp.path()), "{:?}", file.file);
            for hit in &file.hits {
                assert!(hit.before.contains("alpha_cnt"), "{}", hit.before);
                assert!(hit.after.contains("alpha_count"), "{}", hit.after);
                assert!(hit.end_column > hit.column, "{hit:?}");
            }
        }
        Ok(())
    }

    /// AC-2.3 + AC-2.11 cancel path: cancelling leaves every file's sha256
    /// unchanged — the plan itself writes nothing.
    #[test]
    fn a_cancel_leaves_every_file_sha256_unchanged() -> TestResult {
        let (temp, plan) = scratch_project()?;
        let before: Vec<String> = FIXTURE_FILES
            .iter()
            .map(|relative| sha256(&temp.path().join(relative)))
            .collect();
        let outcome = apply(&plan, false, &|| true);
        assert_eq!(outcome.status, ApplyStatus::Cancelled);
        let after: Vec<String> = FIXTURE_FILES
            .iter()
            .map(|relative| sha256(&temp.path().join(relative)))
            .collect();
        for (relative, (before, after)) in FIXTURE_FILES.iter().zip(before.iter().zip(after.iter()))
        {
            assert_eq!(before, after, "{relative} changed on a cancelled apply");
        }
        Ok(())
    }

    /// AC-2.4 atomic apply: a failing target rolls every already-written file back
    /// to its ORIGINAL sha256 and names the failing file — no partial success.
    #[test]
    fn a_failed_write_restores_every_sha256_and_names_the_file() -> TestResult {
        let temp = tempfile::tempdir()?;
        let plan = manual_two_file_plan(temp.path())?;
        let before: Vec<String> = plan
            .plans
            .iter()
            .map(|file_plan| sha256(&file_plan.file))
            .collect();
        let second = plan
            .plans
            .get(1)
            .ok_or("the manual plan has two files")?
            .file
            .clone();
        // A directory where the applier's sibling temp file goes makes the
        // SECOND write fail even for root (EISDIR), after the first succeeded.
        let collision = second.parent().ok_or("parent")?.join(format!(
            ".{}.vdrn.tmp",
            second.file_name().map_or_else(
                || "file".to_string(),
                |name| name.to_string_lossy().into_owned(),
            )
        ));
        std::fs::create_dir_all(&collision)?;
        let outcome = apply(&plan, false, &never);
        assert_eq!(
            outcome.status,
            ApplyStatus::RolledBack,
            "{:?}",
            outcome.message
        );
        assert_eq!(outcome.failed_file.as_deref(), Some(second.as_path()));
        let after: Vec<String> = plan
            .plans
            .iter()
            .map(|file_plan| sha256(&file_plan.file))
            .collect();
        assert_eq!(before, after, "a rolled-back apply changed bytes");
        Ok(())
    }

    /// AC-2.5 negative controls with sha256: same-name locals in another module,
    /// the comment mention, and the string in the OTHER project keep their bytes.
    #[test]
    fn negative_controls_keep_their_sha256_after_apply() -> TestResult {
        let (temp, plan) = scratch_project()?;
        let gamma_before = sha256(&temp.path().join("rtl/gamma.sv"));
        let beta_before = sha256(&temp.path().join("rtl/beta.sv"));
        let other_before = sha256(&fixture_root().join("other/alpha_other.sv"));
        let outcome = apply(&plan, false, &never);
        assert_eq!(
            outcome.status,
            ApplyStatus::Applied,
            "{:?}",
            outcome.message
        );
        assert_eq!(
            sha256(&temp.path().join("rtl/gamma.sv")),
            gamma_before,
            "gamma.sv's same-name local `cnt` must not change"
        );
        assert_eq!(
            sha256(&temp.path().join("rtl/beta.sv")),
            beta_before,
            "the comment mention in beta.sv must not change"
        );
        assert_eq!(
            sha256(&fixture_root().join("other/alpha_other.sv")),
            other_before,
            "the string in the other project must not change"
        );
        let alpha = contents(temp.path(), "rtl/alpha.sv")?;
        assert_eq!(alpha.matches("alpha_count").count(), 3, "{alpha}");
        assert!(!alpha.contains("alpha_cnt"), "{alpha}");
        Ok(())
    }

    /// Run the project through the pinned slang WITHOUT `--ast-json` (the shape a
    /// post-apply self-check uses) and return its report.
    fn slang_build(root: &Path) -> Result<String, String> {
        let repo = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
        let binary = repo.join("build/slang/out/slang");
        if !binary.is_file() {
            return Err(format!(
                "the pinned slang is missing at {}",
                binary.display()
            ));
        }
        let output = std::process::Command::new(&binary)
            .current_dir(root)
            .args([
                "-I",
                "inc",
                "rtl/pkg.sv",
                "rtl/alpha.sv",
                "rtl/beta.sv",
                "rtl/gamma.sv",
            ])
            .output()
            .map_err(|error| error.to_string())?;
        let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
        let stderr = String::from_utf8_lossy(&output.stderr).into_owned();
        if !output.status.success() {
            return Err(format!(
                "the renamed project does not elaborate: {stdout}{stderr}"
            ));
        }
        Ok(format!("{stdout}{stderr}"))
    }

    /// AC-2.12 post-apply self-check: the RENAMED project re-parses with zero
    /// errors, and a fresh index resolves the new name at the same 3 sites (the
    /// old name is gone). This is a real second elaboration, not a string check.
    #[test]
    fn the_renamed_project_still_parses_after_apply() -> TestResult {
        let (temp, plan) = scratch_project()?;
        let outcome = apply(&plan, false, &never);
        assert_eq!(
            outcome.status,
            ApplyStatus::Applied,
            "{:?}",
            outcome.message
        );
        let report = slang_build(temp.path())?;
        assert!(report.contains("0 errors"), "{report}");
        let project = fixture_project(temp.path());
        let index = index(&project).map_err(|error| error.message())?;
        let identity = resolve(&index, "alpha_count").map_err(|error| error.message())?;
        let declarations = index
            .declarations
            .iter()
            .filter(|declaration| identity.addrs.contains(&declaration.addr))
            .count();
        let references = index
            .references
            .iter()
            .filter(|reference| identity.addrs.contains(&reference.addr))
            .count();
        assert_eq!(declarations + references, 3, "declaration + two references");
        assert!(
            resolve(&index, "alpha_cnt").is_err(),
            "the old name must be gone after the apply"
        );
        Ok(())
    }
}
