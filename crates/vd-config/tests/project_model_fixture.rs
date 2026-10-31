//! The repository-owned Verilog fixture, expanded through the real project
//! model (`vd_config::Loader`), must match the checked-in expectations.
//!
//! `tests/fixtures/verilog-project` (task T14) documents itself as the project
//! root and pins the `.f` semantics in `expected/`. This test is the Rust-service
//! half of acceptance AC-S1.3: the same expectations the acceptance script
//! compares by hand are compared here on the code path the C ABI exports
//! (`vd-config` -> `crates/vd-ffi/src/project.rs`).
//!
//! `scripts/verify-fixture.sh` expands the filelists in bash; this test proves
//! the Rust project model returns the same lists, and — importantly — that the
//! fixture folder resolves to itself as the project root even though it lives
//! inside the VEdit git checkout (`RootSource::Filelist`).

use std::path::{Path, PathBuf};

use anyhow::{bail, Result};
use vd_config::{Env, Loader, RootSource, WarningCode};

fn write(path: &Path, text: &str) -> Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(path, text)?;
    Ok(())
}

fn fixture_root() -> Result<PathBuf> {
    let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/verilog-project");
    Ok(std::fs::canonicalize(&path)?)
}

fn expected_lines(root: &Path, name: &str) -> Result<Vec<String>> {
    let text = std::fs::read_to_string(root.join("expected").join(name))?;
    Ok(text
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .map(str::to_string)
        .collect())
}

fn relative(path: &Path, root: &Path) -> String {
    path.strip_prefix(root)
        .unwrap_or(path)
        .to_string_lossy()
        .replace('\\', "/")
}

fn rendered_defines(defines: &[vd_config::Define]) -> Vec<String> {
    defines
        .iter()
        .map(|define| match &define.value {
            Some(value) => format!("{}={value}", define.name),
            None => define.name.clone(),
        })
        .collect()
}

fn load(root: &Path) -> Result<vd_config::Loaded> {
    let temp = tempfile::tempdir()?;
    let home = temp.path().join("home");
    let exe_dir = temp.path().join("payload/usr/bin");
    std::fs::create_dir_all(&home)?;
    std::fs::create_dir_all(&exe_dir)?;
    let env = Env::from_pairs([
        ("HOME".to_string(), home.display().to_string()),
        (
            "XDG_CONFIG_HOME".to_string(),
            home.join(".config").display().to_string(),
        ),
    ]);
    Loader::new(root).with_env(env).with_exe_dir(exe_dir).load()
}

#[test]
fn the_repository_fixture_expands_to_the_checked_in_expectations() -> Result<()> {
    let root = fixture_root()?;
    let loaded = load(&root)?;

    assert_eq!(
        loaded.project.root, root,
        "the fixture folder is its own project root (it is nested inside the VEdit git checkout)"
    );
    assert_eq!(loaded.project.root_source, RootSource::Filelist);

    let mut files = Vec::new();
    for filelist in &loaded.project.filelists {
        for entry in &filelist.entries {
            files.push(relative(entry, &root));
        }
    }
    assert_eq!(
        files,
        expected_lines(&root, "files.txt")?,
        "expanded file list"
    );

    let incdirs: Vec<String> = loaded
        .project
        .include_dirs
        .iter()
        .map(|dir| relative(dir, &root))
        .collect();
    assert_eq!(
        incdirs,
        expected_lines(&root, "incdirs.txt")?,
        "include dirs"
    );

    assert_eq!(
        rendered_defines(&loaded.project.defines),
        expected_lines(&root, "defines.txt")?,
        "defines"
    );

    // Diagnostics are actionable: a line-scoped warning names the .f file and a
    // real line inside it, so the UI can point at the bad entry.
    for warning in &loaded.warnings {
        let Some(file) = &warning.file else {
            continue;
        };
        if !file.exists() {
            bail!(
                "warning {:?} names a path that does not exist: {}",
                warning.code,
                file.display()
            );
        }
        if let Some(line) = warning.line {
            assert!(line >= 1, "line numbers are 1-based: {warning:?}");
            let text = std::fs::read_to_string(file)?;
            assert!(
                (line as usize) <= text.lines().count(),
                "warning {:?} points at line {line} of {} but that file has {} lines",
                warning.code,
                file.display(),
                text.lines().count()
            );
        }
    }

    let duplicate = loaded
        .warnings
        .iter()
        .find(|warning| warning.code == WarningCode::FilelistDuplicateEntry);
    let Some(duplicate) = duplicate else {
        bail!("the fixture's duplicate entry must be reported");
    };
    assert!(
        duplicate
            .file
            .as_deref()
            .is_some_and(|file| file.ends_with("filelists/extra.f")),
        "the duplicate warning must name the filelist it came from, got {:?}",
        duplicate.file
    );
    assert!(
        duplicate.line.is_some(),
        "the duplicate warning must name a line"
    );

    let unknown: Vec<_> = loaded
        .warnings
        .iter()
        .filter(|warning| warning.code == WarningCode::FilelistUnknownToken)
        .collect();
    assert_eq!(
        unknown.len(),
        2,
        "the fixture has two simulator-only tokens"
    );
    for token in ["+libext+.v+.sv", "-y"] {
        let found = unknown
            .iter()
            .find(|warning| warning.message.contains(token));
        let Some(found) = found else {
            bail!("no warning quotes the skipped token `{token}`");
        };
        assert!(
            found
                .file
                .as_deref()
                .is_some_and(|file| file.ends_with("project.f")),
            "the unknown-token warning must name project.f, got {:?}",
            found.file
        );
        assert!(
            found.line.is_some(),
            "the unknown-token warning must name a line"
        );
    }

    Ok(())
}

#[test]
fn bad_entries_name_the_filelist_file_and_line() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let root = temp.path().join("bad-project");
    write(
        &root.join("project.f"),
        "+define+1BAD=1\n+libext+.v\n-f missing.f\nrtl/a.sv\n",
    )?;
    write(&root.join("rtl/a.sv"), "module a; endmodule\n")?;

    let loaded = load(&root)?;
    let root_absolute = std::path::absolute(&root)?;
    assert_eq!(loaded.project.root, root_absolute);
    assert_eq!(loaded.project.root_source, RootSource::Filelist);
    assert_eq!(
        loaded.project.filelists[0].entries,
        vec![root_absolute.join("rtl/a.sv")],
        "the well-formed entry survives its broken siblings"
    );

    let filelist = root_absolute.join("project.f");
    let expect = |code: WarningCode, line: u32, needle: &str| -> Result<()> {
        let found = loaded.warnings.iter().find(|warning| {
            warning.code == code
                && warning.file.as_deref() == Some(filelist.as_path())
                && warning.line == Some(line)
        });
        let Some(found) = found else {
            bail!(
                "missing diagnostic {code:?} for {}:{line}; got {:#?}",
                filelist.display(),
                loaded.warnings
            );
        };
        assert!(
            found.message.contains(needle),
            "warning {code:?} does not explain the bad entry: {}",
            found.message
        );
        Ok(())
    };

    expect(WarningCode::FilelistDefineInvalid, 1, "define")?;
    expect(WarningCode::FilelistUnknownToken, 2, "+libext+.v")?;
    expect(WarningCode::FilelistMissing, 3, "missing.f")?;
    Ok(())
}
