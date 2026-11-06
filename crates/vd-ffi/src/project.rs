//! Project model + `.f` filelist expansion (service 1 of 7).
//!
//! The seam owns the **shape** of the C entry point; the expansion itself is
//! `vd-config`'s public `Loader`/`ProjectContext` (C4: the project model is a
//! Rust service, and `vd-config` is the crate that resolves the root, the
//! configured filelists, `+incdir+`, `+define+` and nested `-f`).
//!
//! What this module adds on top of `vd-config` is flattening: the C side gets
//! one ordered file list with duplicates removed, plus the include dirs, the
//! defines and the warnings collected while parsing. The Qt side never parses a
//! `.f` file itself.

use crate::capi::{
    c_str, c_string, c_string_list, free_string, free_string_list, VdError, VdProjectInfo,
    VdStringList, VD_ERR_INVALID_ARGUMENT, VD_ERR_NOT_FOUND, VD_ERR_SERVICE, VD_STATUS_FAILED,
    VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::collections::BTreeSet;
use std::ffi::c_char;
use std::path::{Path, PathBuf};
use std::ptr;

/// Everything one project expansion produces.
pub(crate) struct ProjectData {
    /// Expanded file list (absolute), filelist order, duplicates removed.
    pub(crate) files: Vec<String>,
    /// The same file list relative to the project root — what a generated `.f`
    /// must carry (see [`crate::capi::VdProjectInfo::relative_files`]).
    pub(crate) relative_files: Vec<String>,
    /// Include directories from `+incdir+` entries.
    pub(crate) include_dirs: Vec<String>,
    /// Defines rendered as `NAME` or `NAME=VALUE`.
    pub(crate) defines: Vec<String>,
    /// Non-fatal diagnostics, already phrased for the UI.
    pub(crate) warnings: Vec<String>,
    /// Unresolved filelist variables (ADDENDUM A-9), unique and sorted.
    pub(crate) missing_variables: Vec<String>,
    /// Lines that produced no entry because of a dropped/invalid entry.
    pub(crate) skipped: u32,
    /// Absolute path of the `.rules.verible_lint` the loader discovered, if any.
    pub(crate) rules_path: Option<String>,
}

impl ProjectData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned lists must be released with `vd_project_info_free`.
    pub(crate) unsafe fn into_c(self) -> VdProjectInfo {
        VdProjectInfo {
            files: c_string_list(&self.files),
            include_dirs: c_string_list(&self.include_dirs),
            defines: c_string_list(&self.defines),
            warnings: c_string_list(&self.warnings),
            relative_files: c_string_list(&self.relative_files),
            missing_variables: c_string_list(&self.missing_variables),
            skipped_lines: self.skipped,
            rules_path: self.rules_path.as_deref().map_or(ptr::null_mut(), c_string),
        }
    }
}

/// The project-relative spelling of a member: the path inside the root, or the
/// absolute path for a member that lives outside it (an imported `.f` may name
/// `../lib/x.sv`; that case cannot be expressed relative to the root safely).
///
/// Portability aid for a generated `.f`, not the correctness rule: the server's
/// cwd (and `rootUri`) must be the project root this projection is based on.
fn relative_to(root: &Path, file: &Path) -> String {
    file.strip_prefix(root)
        .map(|rest| rest.display().to_string())
        .unwrap_or_else(|_| file.display().to_string())
}

/// Expand the project rooted at (or above) `start_dir`.
pub(crate) fn parse(start_dir: &Path) -> Result<ProjectData, VdError> {
    let loaded = vd_config::Loader::new(start_dir).load().map_err(|error| {
        VdError::new(
            VD_ERR_SERVICE,
            &format!(
                "cannot load the project at {}: {error}",
                start_dir.display()
            ),
        )
    })?;

    let mut files = Vec::new();
    let mut seen: BTreeSet<PathBuf> = BTreeSet::new();
    for filelist in &loaded.project.filelists {
        for entry in &filelist.entries {
            if seen.insert(entry.clone()) {
                files.push(entry.display().to_string());
            }
        }
    }
    let relative_files = files
        .iter()
        .map(|file| relative_to(&loaded.project.root, Path::new(file)))
        .collect();
    let include_dirs = loaded
        .project
        .include_dirs
        .iter()
        .map(|dir| dir.display().to_string())
        .collect();
    let defines = loaded
        .project
        .defines
        .iter()
        .map(|define| match &define.value {
            Some(value) => format!("{}={value}", define.name),
            None => define.name.clone(),
        })
        .collect();
    let warnings = format_warnings(&loaded.warnings);

    Ok(ProjectData {
        files,
        relative_files,
        include_dirs,
        defines,
        warnings,
        // The legacy entry does not read a project variable table; the A-9
        // collection is only available through `vd_project_expand_v2`.
        missing_variables: Vec::new(),
        skipped: 0,
        rules_path: loaded.project.rules_path.as_deref().map(absolute),
    })
}

/// The absolute spelling of a discovered path.
///
/// The loader's root is absolute for a normal product call, but a relative
/// start directory would leave the rules path relative; the ABI promises an
/// absolute path, so it is joined with the current directory in that case.
fn absolute(path: &Path) -> String {
    if path.is_absolute() {
        return path.display().to_string();
    }
    std::env::current_dir()
        .map_or_else(|_| path.to_path_buf(), |cwd| cwd.join(path))
        .display()
        .to_string()
}

/// The `.rules.verible_lint` the loader discovers for `start_dir`, absolute.
///
/// T114 semantics stay the loader's: the search stops at `$HOME` (or the
/// filesystem root), a missing file is the normal case (not a warning), and a
/// file that exists but cannot be read is reported through `expand_with_inputs`'
/// warnings. This function never turns "not found" into an error.
fn discovered_rules_path(start_dir: &Path) -> Option<String> {
    let loaded = vd_config::Loader::new(start_dir).load().ok()?;
    loaded.project.rules_path.as_deref().map(absolute)
}

/// Render project warnings for the UI: `code: file:line: message` where the
/// file/line exist, and `code: file: message` / `code: message` otherwise.
///
/// T114 defence in depth: `rules.search_stopped` was the note "no optional
/// `.rules.verible_lint` anywhere" (which also named the absolute stop path,
/// i.e. the user's home directory). vd-config no longer emits it, and this
/// projection drops it if an older/pinned service ever does.
fn format_warnings(warnings: &[vd_config::Warning]) -> Vec<String> {
    warnings
        .iter()
        .filter(|warning| warning.code.as_str() != "rules.search_stopped")
        .map(|warning| match (&warning.file, warning.line) {
            (Some(file), Some(line)) => format!(
                "{}: {}:{}: {}",
                warning.code,
                file.display(),
                line,
                warning.message
            ),
            (Some(file), None) => {
                format!("{}: {}: {}", warning.code, file.display(), warning.message)
            }
            _ => format!("{}: {}", warning.code, warning.message),
        })
        .collect()
}

/// Read the tools-page values (`tools.include_dirs`, `tools.defines`) from the
/// settings store (T10 keys; AC-2.5).
///
/// The store is read here, in the worker job, so the GUI thread never parses
/// it (C4). A broken store degrades to "no extra values" and its own warnings
/// are surfaced in the project diagnostics instead of being swallowed.
fn tool_settings(
    settings_path: &Path,
    diagnostics: &mut Vec<String>,
) -> vd_settings::schema::ToolSettings {
    let loaded = vd_settings::load(settings_path);
    for warning in loaded.warnings {
        diagnostics.push(format!("settings: {warning}"));
    }
    vd_settings::schema::ToolSettings::from_entries(&loaded.entries)
}

/// The full project model of T6 (AC-2.1–2.5): the opened folder as the root,
/// the imported `.f` lists, the file-set union, and the include dirs / defines
/// the backends receive.
///
/// The tools-page values are APPENDED to what the project declares and
/// de-duplicated (documented in `vd_config::project::expand`).
pub(crate) fn expand_with_inputs(
    start_dir: &Path,
    imported_filelists: &[PathBuf],
    settings_path: Option<&Path>,
    variables: &[(String, String)],
) -> Result<ProjectData, VdError> {
    // The server runs with cwd = root and receives rootUri = root; a root that
    // does not exist makes `initialize` fail. Surface that as a visible error
    // instead of an empty project (measured by the slang-server spike).
    if !start_dir.is_dir() {
        return Err(VdError::new(
            VD_ERR_NOT_FOUND,
            &format!(
                "the project root {} does not exist or is not a directory",
                start_dir.display()
            ),
        ));
    }
    let mut settings_diagnostics: Vec<String> = Vec::new();
    let tools = settings_path.map_or_else(vd_settings::schema::ToolSettings::default, |path| {
        tool_settings(path, &mut settings_diagnostics)
    });
    let extra_include_dirs: Vec<PathBuf> = tools.include_dirs.iter().map(PathBuf::from).collect();
    let table = vd_config::project::Variables::from_pairs(variables);
    let expansion = vd_config::project::expand_with_variables(
        start_dir,
        imported_filelists,
        &extra_include_dirs,
        &tools.defines,
        &table,
    )
    .map_err(|error| {
        VdError::new(
            VD_ERR_SERVICE,
            &format!(
                "cannot expand the project at {}: {error}",
                start_dir.display()
            ),
        )
    })?;

    let mut warnings = format_warnings(&expansion.warnings);
    warnings.append(&mut settings_diagnostics);

    Ok(ProjectData {
        files: expansion
            .files
            .iter()
            .map(|path| path.display().to_string())
            .collect(),
        relative_files: expansion
            .files
            .iter()
            .map(|path| relative_to(&expansion.root, path))
            .collect(),
        include_dirs: expansion
            .include_dirs
            .iter()
            .map(|path| path.display().to_string())
            .collect(),
        defines: expansion
            .defines
            .iter()
            .map(|define| match &define.value {
                Some(value) => format!("{}={value}", define.name),
                None => define.name.clone(),
            })
            .collect(),
        warnings,
        missing_variables: expansion.missing_variables,
        skipped: u32::try_from(expansion.skipped_lines).unwrap_or(u32::MAX),
        // The expansion does not carry the discovery result (it re-runs the
        // discovery with the project variable table), so ask the loader once
        // more. It is deterministic, and this runs on the worker thread.
        rules_path: discovered_rules_path(start_dir),
    })
}

/// Read a borrowed [`VdStringList`] into owned strings, with the same defensive
/// limits as the other entry points (`count` is never trusted blindly).
///
/// # Safety
///
/// When `list` is non-null it must point to a readable [`VdStringList`] whose
/// `items` satisfy the C string-array contract.
unsafe fn read_owned_list(list: *const VdStringList) -> Result<Vec<String>, String> {
    if list.is_null() {
        return Ok(Vec::new());
    }
    // SAFETY: the caller contract guarantees a readable VdStringList.
    let list = unsafe { *list };
    if list.count == 0 {
        return Ok(Vec::new());
    }
    if list.items.is_null() {
        return Err("items must not be null when count > 0".to_string());
    }
    if list.count > 100_000 {
        return Err("the list is implausibly large".to_string());
    }
    // SAFETY: the caller contract guarantees `items` points to `count` valid
    // NUL-terminated strings.
    let items = unsafe { std::slice::from_raw_parts(list.items, list.count) };
    let mut values = Vec::with_capacity(list.count);
    for item in items {
        // SAFETY: each `*item` is one of the validated NUL-terminated strings.
        let Some(text) = (unsafe { c_str(*item) }) else {
            return Err("every entry must be valid UTF-8".to_string());
        };
        values.push(text.to_string());
    }
    Ok(values)
}

/// Start a project/.f expansion job.
///
/// Returns the job id, or `0` when the arguments were rejected (the reason is
/// readable with `vd_service_error`).
///
/// # Safety
///
/// `service` must be null or a live handle; `start_dir` must be null or a valid
/// NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn vd_project_parse(
    service: *mut VdService,
    start_dir: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(start_dir) = crate::capi::c_str(start_dir) else {
        state.record_error(VdError::new(
            crate::capi::VD_ERR_INVALID_ARGUMENT,
            "start_dir must be a valid UTF-8 path",
        ));
        return 0;
    };
    let start = PathBuf::from(start_dir);
    state.start(move || parse(&start).map(JobValue::Project))
}

/// Start a full project-model expansion job (T6, AC-2.1–2.5).
///
/// Unlike [`vd_project_parse`] — kept unchanged for the existing callers — this
/// entry point implements the project model of C7:
///
/// * `start_dir` is the folder the user opened and becomes the ROOT; the upward
///   discovery never moves it above that folder;
/// * `filelists` (null or empty when none) are the imported `.f` files;
/// * `settings_path` (null for "no tools page yet") is the `settings.toml` whose
///   `tools.include_dirs` / `tools.defines` are appended to the project's own;
/// * the file set is the union of the `.f` expansion and a recursive scan of
///   the root (`.v`/`.sv`/`.vh`/`.svh`), de-duplicated.
///
/// Returns the job id, or `0` when the arguments were rejected (the reason is
/// readable with `vd_service_error`). Poll it with [`vd_project_poll`] and free
/// the result with [`vd_project_info_free`].
///
/// # Safety
///
/// `service` must be null or a live handle; `start_dir`/`settings_path` must be
/// null or valid NUL-terminated strings; `filelists` must be null or satisfy the
/// [`VdStringList`] contract for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_project_expand(
    service: *mut VdService,
    start_dir: *const c_char,
    filelists: *const VdStringList,
    settings_path: *const c_char,
) -> u64 {
    unsafe { start_expand_impl(service, start_dir, filelists, settings_path, &[]) }
}

/// Start a full project-model expansion with the project's filelist VARIABLE
/// TABLE (ADDENDUM A-9).
///
/// Same contract as [`vd_project_expand`], plus `variables`: a list of
/// `NAME=VALUE` entries (a bare `NAME` is a defined-but-empty variable). The
/// table wins over the process environment; `$VAR` / `${VAR}` / `$(VAR)` / `~`
/// inside the `.f` files expand from it. Unresolved names come back in
/// `VdProjectInfo.missing_variables` and each affected line is a warning with
/// its `.f` file and 1-based line.
///
/// Returns the job id, or `0` when the arguments were rejected (the reason is
/// readable with `vd_service_error`). Poll with [`vd_project_poll`], free with
/// [`vd_project_info_free`].
///
/// # Safety
///
/// Same pointer contract as [`vd_project_expand`]; `variables` must be null or
/// satisfy the [`VdStringList`] contract for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_project_expand_v2(
    service: *mut VdService,
    start_dir: *const c_char,
    filelists: *const VdStringList,
    settings_path: *const c_char,
    variables: *const VdStringList,
) -> u64 {
    let parsed = match unsafe { read_owned_list(variables) } {
        Ok(values) => {
            let mut parsed: Vec<(String, String)> = Vec::with_capacity(values.len());
            for value in values {
                let value = value.trim().to_string();
                if value.is_empty() {
                    continue;
                }
                match value.split_once('=') {
                    Some((name, value)) => {
                        parsed.push((name.trim().to_string(), value.to_string()));
                    }
                    None => parsed.push((value, String::new())),
                }
            }
            parsed
        }
        Err(message) => {
            if let Some(state) = service_state(service) {
                state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            }
            return 0;
        }
    };
    unsafe { start_expand_impl(service, start_dir, filelists, settings_path, &parsed) }
}

/// Shared body of [`vd_project_expand`] and [`vd_project_expand_v2`].
///
/// # Safety
///
/// Same contract as the public entry points.
unsafe fn start_expand_impl(
    service: *mut VdService,
    start_dir: *const c_char,
    filelists: *const VdStringList,
    settings_path: *const c_char,
    variables: &[(String, String)],
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(start_dir) = (unsafe { c_str(start_dir) }) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "start_dir must be a valid UTF-8 path",
        ));
        return 0;
    };
    let imported: Vec<PathBuf> = match unsafe { read_owned_list(filelists) } {
        Ok(values) => values
            .into_iter()
            .filter(|value| !value.trim().is_empty())
            .map(PathBuf::from)
            .collect(),
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    let settings = if settings_path.is_null() {
        None
    } else {
        match unsafe { c_str(settings_path) } {
            Some(text) if !text.is_empty() => Some(PathBuf::from(text)),
            Some(_) => None,
            None => {
                state.record_error(VdError::new(
                    VD_ERR_INVALID_ARGUMENT,
                    "settings_path must be a valid UTF-8 path",
                ));
                return 0;
            }
        }
    };
    let start = PathBuf::from(start_dir);
    let variables = variables.to_vec();
    state.start(move || {
        expand_with_inputs(&start, &imported, settings.as_deref(), &variables)
            .map(JobValue::Project)
    })
}

/// Poll a project job.
///
/// `VD_STATUS_READY` fills `out`; `VD_STATUS_FAILED` leaves it untouched and the
/// reason is readable with `vd_service_error`.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdProjectInfo`].
#[no_mangle]
pub unsafe extern "C" fn vd_project_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdProjectInfo,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    match state.poll(job) {
        Polled::Unknown => unknown_job(state),
        Polled::Pending => VD_STATUS_PENDING,
        Polled::Failed => VD_STATUS_FAILED,
        Polled::Ready(JobValue::Project(data)) => {
            *out = data.into_c();
            VD_STATUS_READY
        }
        Polled::Ready(_) => {
            state.record_error(VdError::new(
                crate::capi::VD_ERR_WRONG_JOB_KIND,
                "this job id belongs to a different service",
            ));
            POLL_MISUSE
        }
    }
}

/// Release a filled [`VdProjectInfo`]; null-safe and idempotent.
///
/// # Safety
///
/// `info` must be null or point to a struct filled by [`vd_project_poll`] and not
/// freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_project_info_free(info: *mut VdProjectInfo) {
    if info.is_null() {
        return;
    }
    let info = &mut *info;
    free_string_list(&mut info.files);
    free_string_list(&mut info.include_dirs);
    free_string_list(&mut info.defines);
    free_string_list(&mut info.warnings);
    free_string_list(&mut info.relative_files);
    free_string_list(&mut info.missing_variables);
    // SAFETY: rules_path was produced by c_string in into_c.
    unsafe { free_string(info.rules_path) };
    info.skipped_lines = 0;
    info.rules_path = ptr::null_mut();
    *info = VdProjectInfo {
        files: VdStringList::empty(),
        include_dirs: VdStringList::empty(),
        defines: VdStringList::empty(),
        warnings: VdStringList::empty(),
        relative_files: VdStringList::empty(),
        missing_variables: VdStringList::empty(),
        skipped_lines: 0,
        rules_path: ptr::null_mut(),
    };
}

#[cfg(test)]
mod tests {
    use super::{
        expand_with_inputs, parse, vd_project_expand, vd_project_expand_v2, vd_project_info_free,
        vd_project_poll,
    };
    use crate::capi::{
        c_string, c_string_list, free_string_list, VdProjectInfo, VdStringList, VD_ERR_NOT_FOUND,
        VD_STATUS_PENDING, VD_STATUS_READY,
    };
    use std::path::{Path, PathBuf};
    use std::ptr;

    fn fixture_root() -> PathBuf {
        let path =
            Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/verilog-project");
        match std::fs::canonicalize(&path) {
            Ok(path) => path,
            Err(error) => panic!("cannot canonicalize {}: {error}", path.display()),
        }
    }

    fn write(path: &Path, text: &str) {
        if let Some(parent) = path.parent() {
            if let Err(error) = std::fs::create_dir_all(parent) {
                panic!("cannot create {}: {error}", parent.display());
            }
        }
        if let Err(error) = std::fs::write(path, text) {
            panic!("cannot write {}: {error}", path.display());
        }
    }

    fn require<T>(value: Option<T>, what: &str) -> T {
        match value {
            Some(value) => value,
            None => panic!("missing {what}"),
        }
    }

    fn relative(path: &str, root: &Path) -> String {
        Path::new(path)
            .strip_prefix(root)
            .unwrap_or_else(|_| Path::new(path))
            .to_string_lossy()
            .replace('\\', "/")
    }

    fn expected_lines(root: &Path, name: &str) -> Vec<String> {
        let path = root.join("expected").join(name);
        let text = match std::fs::read_to_string(&path) {
            Ok(text) => text,
            Err(error) => panic!("cannot read {}: {error}", path.display()),
        };
        text.lines()
            .map(str::trim)
            .filter(|line| !line.is_empty())
            .map(str::to_string)
            .collect()
    }

    /// AC-S1.3 through the exported service: the fixture expands to the checked-in
    /// expectations, and the diagnostics the UI receives are phrased strings that
    /// name the offending filelist and line.
    #[test]
    fn fixture_expansion_matches_the_service_contract() {
        let root = fixture_root();
        let data = match parse(&root) {
            Ok(data) => data,
            Err(error) => panic!("project parse failed with code {}", error.code),
        };

        let files: Vec<String> = data
            .files
            .iter()
            .map(|file| relative(file, &root))
            .collect();
        assert_eq!(files, expected_lines(&root, "files.txt"), "expanded files");
        assert_eq!(data.files.len(), 7, "fixture file count");

        let incdirs: Vec<String> = data
            .include_dirs
            .iter()
            .map(|dir| relative(dir, &root))
            .collect();
        assert_eq!(
            incdirs,
            expected_lines(&root, "incdirs.txt"),
            "include dirs"
        );

        assert_eq!(
            data.defines,
            expected_lines(&root, "defines.txt"),
            "defines"
        );

        for file in &data.files {
            assert!(
                Path::new(file).is_absolute(),
                "the C side receives absolute paths, got {file}"
            );
        }

        let unknown = data
            .warnings
            .iter()
            .find(|warning| warning.starts_with("filelist.unknown_token: "))
            .unwrap_or_else(|| panic!("no unknown-token diagnostic in {:#?}", data.warnings));
        assert!(
            unknown.contains("project.f:"),
            "the diagnostic must name project.f: {unknown}"
        );
        let line = unknown
            .split("project.f:")
            .nth(1)
            .and_then(|rest| rest.split(':').next());
        match line {
            Some(digits) if !digits.is_empty() && digits.chars().all(|c| c.is_ascii_digit()) => {}
            _ => panic!("the diagnostic must carry the 1-based line: {unknown}"),
        }

        let duplicate = data
            .warnings
            .iter()
            .find(|warning| warning.starts_with("filelist.duplicate_entry: "))
            .unwrap_or_else(|| panic!("no duplicate diagnostic in {:#?}", data.warnings));
        assert!(
            duplicate.contains("extra.f:"),
            "the duplicate diagnostic must name the nested filelist: {duplicate}"
        );
    }

    /// A project whose filelist contains broken entries still expands (the editor
    /// must open it) and every broken entry produces one diagnostic string with
    /// the file and line the user has to fix.
    #[test]
    fn bad_entries_render_actionable_diagnostics() {
        let temp = match tempfile::tempdir() {
            Ok(temp) => temp,
            Err(error) => panic!("temporary directory: {error}"),
        };
        let root = temp.path().join("bad-project");
        let filelist = root.join("project.f");
        write(
            &filelist,
            "+define+1BAD=1\n+libext+.v\n-f missing.f\nrtl/a.sv\n",
        );
        write(&root.join("rtl/a.sv"), "module a; endmodule\n");

        let data = match parse(&root) {
            Ok(data) => data,
            Err(error) => panic!("broken entries must not fail the expansion: {}", error.code),
        };
        let absolute = require(std::path::absolute(&root).ok(), "absolute project root");
        assert_eq!(
            data.files,
            vec![absolute.join("rtl/a.sv").display().to_string()]
        );
        assert_eq!(data.include_dirs.len(), 0);
        assert_eq!(data.defines.len(), 0);

        let prefix = |code: &str, line: u32| format!("{code}: {}:{line}:", filelist.display());
        for (code, line, needle) in [
            ("filelist.define_invalid", 1, "malformed"),
            ("filelist.unknown_token", 2, "+libext+.v"),
            ("filelist.missing", 3, "missing.f"),
        ] {
            let expected = prefix(code, line);
            let found = data
                .warnings
                .iter()
                .find(|warning| warning.starts_with(&expected));
            let found = require(found, "diagnostic");
            assert!(
                found.contains(needle),
                "diagnostic {code} must explain the bad entry, got {found}"
            );
        }
    }

    /// Two loads of the same project produce byte-identical payloads; the Qt panel
    /// re-expands on every start and the list must not reshuffle.
    #[test]
    fn expansion_is_deterministic() {
        let root = fixture_root();
        let first = match parse(&root) {
            Ok(data) => data,
            Err(error) => panic!("first parse failed with code {}", error.code),
        };
        let second = match parse(&root) {
            Ok(data) => data,
            Err(error) => panic!("second parse failed with code {}", error.code),
        };
        assert_eq!(first.files, second.files);
        assert_eq!(first.include_dirs, second.include_dirs);
        assert_eq!(first.defines, second.defines);
        assert_eq!(first.warnings, second.warnings);
    }

    // --- T114: optional resources are not GUI diagnostics --------------------

    /// NEGATIVE CONTROL: a project without `.rules.verible_lint` (the normal
    /// case) produces NO `rules.*` diagnostic, so the panel's "Diagnostics (N)"
    /// list stays empty and no home-directory path is shown.
    #[test]
    fn missing_optional_rules_file_is_not_a_panel_diagnostic(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        std::fs::write(root.join("top.f"), "rtl/a.sv\n")?;
        let data = match parse(root) {
            Ok(data) => data,
            Err(error) => panic!("parse failed with code {}", error.code),
        };
        println!(
            "NEGATIVE-CONTROL project_without_rules warnings={:?}",
            data.warnings
        );
        let rules: Vec<&String> = data
            .warnings
            .iter()
            .filter(|warning| warning.starts_with("rules."))
            .collect();
        assert!(
            rules.is_empty(),
            "a missing optional rules file must not reach the panel: {rules:?}"
        );
        assert!(
            !data
                .warnings
                .iter()
                .any(|warning| warning.contains("/home/")),
            "no diagnostic may leak a home directory: {:#?}",
            data.warnings
        );
        Ok(())
    }

    /// POSITIVE CONTROL: a real rules file is adopted and produces no noise
    /// (not even a "found it" note).
    #[test]
    fn a_present_rules_file_produces_no_panel_noise() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        std::fs::write(root.join(".rules.verible_lint"), "line-length: 120\n")?;
        std::fs::write(root.join("top.f"), "rtl/a.sv\n")?;
        let data = match parse(root) {
            Ok(data) => data,
            Err(error) => panic!("parse failed with code {}", error.code),
        };
        println!(
            "POSITIVE-CONTROL project_with_rules warnings={:?}",
            data.warnings
        );
        assert!(
            data.warnings.is_empty(),
            "a found rules file must be silent: {:#?}",
            data.warnings
        );
        Ok(())
    }

    /// REGRESSION CONTROL: genuinely actionable filelist problems still reach
    /// the panel (the filter above must not become a blanket mute).
    #[test]
    fn actionable_filelist_warnings_still_reach_the_panel() -> Result<(), Box<dyn std::error::Error>>
    {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        std::fs::write(root.join("top.f"), "+broken+token\nrtl/a.sv\n")?;
        let data = match parse(root) {
            Ok(data) => data,
            Err(error) => panic!("parse failed with code {}", error.code),
        };
        println!("REGRESSION-CONTROL warnings={:?}", data.warnings);
        assert!(
            data.warnings
                .iter()
                .any(|warning| warning.starts_with("filelist.unknown_token: ")),
            "a malformed filelist entry must still be reported: {:#?}",
            data.warnings
        );
        Ok(())
    }

    /// The "exists but unusable" branch still warns, and the message stays
    /// path-free (a directory with the rules file name is deterministic even in
    /// a root container).
    #[test]
    fn unreadable_rules_file_still_warns_without_an_absolute_path() {
        let temp = match tempfile::tempdir() {
            Ok(temp) => temp,
            Err(error) => panic!("cannot create a temp dir: {error}"),
        };
        let root = temp.path();
        if let Err(error) = std::fs::create_dir_all(root.join(".rules.verible_lint")) {
            panic!("cannot create the rules directory: {error}");
        }
        let data = match parse(root) {
            Ok(data) => data,
            Err(error) => panic!("parse failed with code {}", error.code),
        };
        println!("UNREADABLE-CONTROL warnings={:?}", data.warnings);
        let unreadable: Vec<&String> = data
            .warnings
            .iter()
            .filter(|warning| warning.starts_with("rules.unreadable: "))
            .collect();
        assert_eq!(
            unreadable.len(),
            1,
            "an unusable rules file must still warn: {:#?}",
            data.warnings
        );
        let message = unreadable[0];
        assert!(
            message.contains("falling back to built-in rules"),
            "the message must say the feature still works: {message}"
        );
        assert!(
            !message.contains(&root.display().to_string()) && !message.contains("/home/"),
            "the warning must not leak an absolute path: {message}"
        );
    }

    /// One flat `settings.toml` as the T10 settings store writes it.
    fn settings_store(root: &Path, include_dirs: &str, defines: &str) -> PathBuf {
        let path = root.join("settings.toml");
        let escaped_defines = defines.replace('\n', "\\n");
        write(
            &path,
            &format!(
                "\"tools.include_dirs\" = \"{include_dirs}\"\n\
                 \"tools.defines\" = \"{escaped_defines}\"\n"
            ),
        );
        path
    }

    fn list_strings(list: &VdStringList) -> Vec<String> {
        let mut out = Vec::new();
        for index in 0..list.count {
            // SAFETY: `items` is a valid array of `count` NUL-terminated strings.
            let slot = unsafe { *list.items.add(index) };
            // SAFETY: each slot is one of the validated NUL-terminated strings.
            if let Some(text) = unsafe { crate::capi::c_str(slot) } {
                out.push(text.to_string());
            }
        }
        out
    }

    /// AC-2.2/AC-2.5 at the service boundary: imported `.f` files and the
    /// tools-page `-I`/`-D` are merged into the union, de-duplicated, and the
    /// include dirs resolve relative to the project root.
    #[test]
    fn expand_with_inputs_merges_the_imported_filelists_and_the_tools_page() {
        let temp = match tempfile::tempdir() {
            Ok(temp) => temp,
            Err(error) => panic!("cannot create a temp dir: {error}"),
        };
        let root = temp.path();
        write(&root.join("rtl/a.sv"), "module a; endmodule\n");
        write(&root.join("rtl/extra.sv"), "module extra; endmodule\n");
        write(&root.join("inc/shared.vh"), "`define SHARED 1\n");
        let imported = root.join("imported.f");
        write(&imported, "+incdir+inc\n+define+IMPORTED\nrtl/extra.sv\n");
        let settings = settings_store(root, "inc", "BOARD=7");

        let data =
            match expand_with_inputs(root, std::slice::from_ref(&imported), Some(&settings), &[]) {
                Ok(data) => data,
                Err(error) => panic!("expand failed with code {}", error.code),
            };
        println!(
            "EXPAND files={:?} incdirs={:?} defines={:?}",
            data.files, data.include_dirs, data.defines
        );
        assert_eq!(
            data.files,
            vec![
                root.join("rtl/extra.sv").display().to_string(),
                root.join("inc/shared.vh").display().to_string(),
                root.join("rtl/a.sv").display().to_string(),
            ],
            "imported filelist order first, then the sorted scan"
        );
        assert_eq!(
            data.include_dirs,
            vec![root.join("inc").display().to_string()],
            "the filelist's include dir and the tools-page one are one entry"
        );
        assert_eq!(
            data.defines,
            vec!["IMPORTED".to_string(), "BOARD=7".to_string()]
        );
        assert!(
            data.warnings.is_empty(),
            "a clean project must not warn: {:?}",
            data.warnings
        );
    }

    /// AC-2.3: every bad `.f` entry names the file and the 1-based line; the
    /// three specified cases are covered.
    #[test]
    fn expand_reports_the_three_bad_filelist_cases_with_file_and_line() {
        let temp = match tempfile::tempdir() {
            Ok(temp) => temp,
            Err(error) => panic!("cannot create a temp dir: {error}"),
        };
        let root = temp.path();
        write(&root.join("good.sv"), "module good; endmodule\n");
        let bad = root.join("bad.f");
        write(
            &bad,
            "good.sv\n+incdir+no_such_dir\n+frobnicate now\n-f missing_nested.f\n",
        );

        let data = match expand_with_inputs(root, &[], None, &[]) {
            Ok(data) => data,
            Err(error) => panic!("expand failed with code {}", error.code),
        };
        println!("BAD-FILELIST warnings={:#?}", data.warnings);
        let prefix = bad.display().to_string();
        let at = |line: u32| {
            data.warnings.iter().any(|warning| {
                warning.contains(&format!("{prefix}:{line}:")) && warning.contains(": ")
            })
        };
        assert!(
            at(2),
            "missing +incdir+ must name bad.f:2: {:?}",
            data.warnings
        );
        assert!(
            at(3),
            "unknown directive must name bad.f:3: {:?}",
            data.warnings
        );
        assert!(
            at(4),
            "missing nested -f must name bad.f:4: {:?}",
            data.warnings
        );
    }

    /// AC-2.5 evidence: the include dirs and defines of the expansion are
    /// exactly what the slang engine puts on its command line (the same
    /// projection the settings page echoes).
    #[test]
    fn the_engine_command_line_carries_the_expansion_include_dirs_and_defines() {
        let temp = match tempfile::tempdir() {
            Ok(temp) => temp,
            Err(error) => panic!("cannot create a temp dir: {error}"),
        };
        let root = temp.path();
        if let Err(error) = std::fs::create_dir_all(root.join("inc")) {
            panic!("cannot create the include dir: {error}");
        }
        write(&root.join("rtl/a.sv"), "module a; endmodule\n");
        let settings = settings_store(root, "inc", "BOARD=7\nSIM");
        let data = match expand_with_inputs(root, &[], Some(&settings), &[]) {
            Ok(data) => data,
            Err(error) => panic!("expand failed with code {}", error.code),
        };
        let include_dirs: Vec<PathBuf> = data.include_dirs.iter().map(PathBuf::from).collect();
        let line = vd_deeplint::command_line(
            vd_deeplint::LintEngine::Slang,
            Path::new("/opt/slang"),
            &root.join("rtl/a.sv"),
            &include_dirs,
            &data.defines,
            None,
        );
        println!("BACKEND-ARGV {line}");
        assert!(
            line.contains(&format!("-I{}", root.join("inc").display())),
            "the include dir must reach the argv: {line}"
        );
        assert!(line.contains("-DBOARD=7"), "{line}");
        assert!(line.contains("-DSIM"), "{line}");
    }

    /// The exported entry point (job + poll) carries the same model the Qt
    /// project panel consumes.
    #[test]
    fn the_expand_abi_delivers_the_union_through_a_job() {
        let temp = match tempfile::tempdir() {
            Ok(temp) => temp,
            Err(error) => panic!("cannot create a temp dir: {error}"),
        };
        let root = temp.path();
        write(&root.join("rtl/a.sv"), "module a; endmodule\n");
        write(&root.join("rtl/extra.sv"), "module extra; endmodule\n");
        let imported = root.join("imported.f");
        write(&imported, "rtl/extra.sv\n");
        let settings = settings_store(root, "", "BOARD=7");

        let mut filelists = unsafe { c_string_list(&[imported.display().to_string()]) };
        let start = c_string(&root.display().to_string());
        let settings_path = c_string(&settings.display().to_string());
        let service = crate::service::vd_service_create();
        assert!(!start.is_null() && !settings_path.is_null());

        let job = unsafe { vd_project_expand(service, start, &filelists, settings_path) };
        assert!(job != 0, "the expand call must be accepted");
        let mut info = VdProjectInfo {
            files: VdStringList::empty(),
            include_dirs: VdStringList::empty(),
            defines: VdStringList::empty(),
            warnings: VdStringList::empty(),
            relative_files: VdStringList::empty(),
            missing_variables: VdStringList::empty(),
            skipped_lines: 0,
            rules_path: ptr::null_mut(),
        };
        let mut status = VD_STATUS_PENDING;
        for _ in 0..4000 {
            status = unsafe { vd_project_poll(service, job, &mut info) };
            if status != VD_STATUS_PENDING {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(1));
        }
        assert_eq!(status, VD_STATUS_READY, "the job must finish");
        let files = list_strings(&info.files);
        let defines = list_strings(&info.defines);
        let relative = list_strings(&info.relative_files);
        println!("ABI files={files:?} relative={relative:?} defines={defines:?}");
        assert_eq!(files.len(), 2, "union through the ABI: {files:?}");
        assert!(files.contains(&root.join("rtl/a.sv").display().to_string()));
        assert!(files.contains(&root.join("rtl/extra.sv").display().to_string()));
        assert_eq!(defines, vec!["BOARD=7".to_string()]);
        // The generated `.f` must use the relative form (slang-server location
        // URIs stay correct); the absolute form is for opening files in the UI.
        assert!(
            relative.iter().all(|file| !Path::new(file).is_absolute()),
            "relative_files must be relative to the root: {relative:?}"
        );
        assert!(relative.contains(&"rtl/a.sv".to_string()));
        assert!(relative.contains(&"rtl/extra.sv".to_string()));

        unsafe {
            vd_project_info_free(&mut info);
            free_string_list(&mut filelists);
            crate::service::vd_service_destroy(service);
            drop(std::ffi::CString::from_raw(start));
            drop(std::ffi::CString::from_raw(settings_path));
        }
    }

    /// ADDENDUM A-9 at the service boundary: the project variable table wins
    /// over the environment, unresolved names come back for the one dialog, and
    /// the skipped count is non-zero whenever a line was dropped.
    #[test]
    fn the_expand_abi_collects_unresolved_variables_and_skipped_lines() {
        let temp = match tempfile::tempdir() {
            Ok(temp) => temp,
            Err(error) => panic!("cannot create a temp dir: {error}"),
        };
        let root = temp.path();
        write(&root.join("rtl/a.sv"), "module a; endmodule\n");
        let filelist = root.join("top.f");
        write(&filelist, "$SUB/a.sv\nrtl/$NOPE.sv\n+define+ARCH=$ARCH\n");
        // `ARCH` is in the project table; `SUB` too. `NOPE` is nowhere.
        let mut filelists = unsafe { c_string_list(&[filelist.display().to_string()]) };
        let mut variables =
            unsafe { c_string_list(&["SUB=rtl".to_string(), "ARCH=x86".to_string()]) };
        let start = c_string(&root.display().to_string());
        let service = crate::service::vd_service_create();
        assert!(!start.is_null());

        let job = unsafe {
            vd_project_expand_v2(service, start, &filelists, std::ptr::null(), &variables)
        };
        assert!(job != 0, "the expand call must be accepted");
        let mut info = VdProjectInfo {
            files: VdStringList::empty(),
            include_dirs: VdStringList::empty(),
            defines: VdStringList::empty(),
            warnings: VdStringList::empty(),
            relative_files: VdStringList::empty(),
            missing_variables: VdStringList::empty(),
            skipped_lines: 0,
            rules_path: ptr::null_mut(),
        };
        let mut status = VD_STATUS_PENDING;
        for _ in 0..4000 {
            status = unsafe { vd_project_poll(service, job, &mut info) };
            if status != VD_STATUS_PENDING {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(1));
        }
        assert_eq!(status, VD_STATUS_READY, "the job must finish");
        let files = list_strings(&info.files);
        let missing = list_strings(&info.missing_variables);
        let defines = list_strings(&info.defines);
        let warnings = list_strings(&info.warnings);
        println!(
            "A9-ABI files={files:?} missing={missing:?} skipped={} defines={defines:?} warnings={warnings:?}",
            info.skipped_lines
        );
        assert_eq!(files, vec![root.join("rtl/a.sv").display().to_string()]);
        assert_eq!(missing, vec!["NOPE".to_string()]);
        assert_eq!(info.skipped_lines, 1);
        assert_eq!(defines, vec!["ARCH=x86".to_string()]);
        unsafe {
            vd_project_info_free(&mut info);
            free_string_list(&mut filelists);
            free_string_list(&mut variables);
            crate::service::vd_service_destroy(service);
            drop(std::ffi::CString::from_raw(start));
        }
    }

    /// The slang-server spike measured that a rootUri which does not exist makes
    /// `initialize` fail. The model must say so, not hand out an empty project.
    #[test]
    fn a_missing_project_root_is_a_typed_error() {
        let missing = Path::new("/nonexistent/vedit-project-root");
        match expand_with_inputs(missing, &[], None, &[]) {
            Ok(_) => panic!("a missing project root must not expand"),
            Err(error) => {
                assert_eq!(error.code, VD_ERR_NOT_FOUND);
                let message: String = error
                    .message
                    .iter()
                    .take_while(|slot| **slot != 0)
                    .map(|slot| *slot as u8 as char)
                    .collect();
                assert!(
                    message.contains("does not exist"),
                    "the failure must be visible: {message}"
                );
            }
        }
    }
}
