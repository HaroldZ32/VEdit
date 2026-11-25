//! C ABI for the host shell selector and the shared host launch environment.

use std::ffi::{c_char, CStr, CString, OsStr};
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};
use std::ptr;
use std::sync::RwLock;

use vd_vcs::host::{bind_shell_preference, HostEnvironment};
use vd_vcs::toolchain::{resolve_in, ToolSource};

use crate::capi::{VdEnvSnapshot, VdEnvSnapshotEntry};

static SETTINGS_STORE: RwLock<Option<PathBuf>> = RwLock::new(None);

fn shell_preference() -> Option<PathBuf> {
    let store = SETTINGS_STORE
        .read()
        .unwrap_or_else(|error| error.into_inner());
    let loaded = vd_settings::load(store.as_deref()?);
    loaded.entries.into_iter().find_map(|entry| {
        (entry.key == vd_settings::schema::keys::HOST_SHELL && !entry.value.is_empty())
            .then(|| PathBuf::from(entry.value))
    })
}

// The foreign caller owns the original buffer. This copy keeps raw Unix path bytes.
unsafe fn path_argument(path: *const c_char) -> Option<PathBuf> {
    if path.is_null() {
        return None;
    }
    // SAFETY: each caller forwards the C ABI's readable, NUL-terminated argument.
    let bytes = unsafe { CStr::from_ptr(path) }.to_bytes();
    (!bytes.is_empty()).then(|| PathBuf::from(OsStr::from_bytes(bytes)))
}

fn owned_os_string(value: &OsStr) -> *mut c_char {
    CString::new(value.as_bytes()).map_or(ptr::null_mut(), CString::into_raw)
}

fn owned_snapshot(rows: Vec<VdEnvSnapshotEntry>) -> VdEnvSnapshot {
    let count = rows.len();
    let items = if count == 0 {
        ptr::null_mut()
    } else {
        Box::into_raw(rows.into_boxed_slice()).cast::<VdEnvSnapshotEntry>()
    };
    VdEnvSnapshot { count, items }
}

fn environment_snapshot(host: &HostEnvironment) -> VdEnvSnapshot {
    owned_snapshot(
        host.variables()
            .iter()
            .map(|(name, value)| VdEnvSnapshotEntry {
                name: owned_os_string(name),
                value: owned_os_string(value),
            })
            .collect(),
    )
}

/// Bind the settings store used by subsequent PTY and VCS launches.
/// Null or empty clears the binding and selects Auto.
///
/// # Safety
/// `path` must be null or a readable, NUL-terminated settings file path.
#[no_mangle]
pub unsafe extern "C" fn vd_host_use_settings_store(path: *const c_char) {
    // SAFETY: `path` satisfies path_argument's readable string contract.
    let path = unsafe { path_argument(path) };
    *SETTINGS_STORE
        .write()
        .unwrap_or_else(|error| error.into_inner()) = path;
    bind_shell_preference(shell_preference);
}

/// Fill a snapshot with installed shell paths in `name`; `value` is null.
/// Release the rows with `vd_env_snapshot_free`.
///
/// # Safety
/// `out` must be null or writable for one unfilled snapshot.
#[no_mangle]
pub unsafe extern "C" fn vd_host_shell_choices(out: *mut VdEnvSnapshot) {
    if out.is_null() {
        return;
    }
    let rows = HostEnvironment::capture(Path::new("."))
        .shells()
        .iter()
        .map(|path| VdEnvSnapshotEntry {
            name: owned_os_string(path.as_os_str()),
            value: ptr::null_mut(),
        })
        .collect();
    // SAFETY: `out` is writable and was checked for null.
    unsafe { *out = owned_snapshot(rows) };
}

/// Fill the PTY shell path and environment. Returns 1 on success, or 0 with
/// a diagnostic in `shell_out` and the snapshot untouched. A null or empty
/// explicit shell uses the persisted preference. Null or empty cwd uses the
/// current directory. Release the string and snapshot with the usual ABI frees.
///
/// # Safety
/// The two inputs must be null or readable, NUL-terminated paths. Both outputs
/// must be null or writable for one unfilled value, and must not overlap.
#[no_mangle]
pub unsafe extern "C" fn vd_host_terminal_plan(
    explicit_shell: *const c_char,
    cwd: *const c_char,
    shell_out: *mut *mut c_char,
    environment_out: *mut VdEnvSnapshot,
) -> i32 {
    if shell_out.is_null() || environment_out.is_null() {
        return 0;
    }
    // SAFETY: both inputs satisfy path_argument's readable string contract.
    let (explicit, cwd) = unsafe { (path_argument(explicit_shell), path_argument(cwd)) };
    let host = HostEnvironment::capture(cwd.as_deref().unwrap_or_else(|| Path::new(".")));
    match host.selected_shell(explicit.as_deref()) {
        Ok(shell) => {
            // SAFETY: both output slots are writable and were checked for null.
            unsafe {
                *shell_out = owned_os_string(shell.as_os_str());
                *environment_out = environment_snapshot(&host);
            }
            1
        }
        Err(error) => {
            // SAFETY: the shell output slot is writable and was checked for null.
            unsafe { *shell_out = crate::capi::c_string(&error) };
            0
        }
    }
}

/// Resolve a version probe's host tool, environment and launch argv. Returns
/// 1 for PATH, 2 for the environment fallback, or 0 when the tool is missing.
/// Launch rows carry the shell path then its arguments in `name`. A missing
/// shell leaves the launch rows empty. Release successful outputs with the
/// usual ABI frees.
///
/// # Safety
/// `tool` must be null or a readable NUL-terminated string. Each output must
/// be null or writable for one unfilled value, and must not overlap.
#[no_mangle]
pub unsafe extern "C" fn vd_host_vcs_probe(
    tool: *const c_char,
    path_out: *mut *mut c_char,
    environment_out: *mut VdEnvSnapshot,
    launch_out: *mut VdEnvSnapshot,
) -> i32 {
    if tool.is_null() || path_out.is_null() || environment_out.is_null() || launch_out.is_null() {
        return 0;
    }
    // SAFETY: `tool` is NUL-terminated and was checked for null.
    let Ok(tool) = (unsafe { CStr::from_ptr(tool) }).to_str() else {
        return 0;
    };
    let host = HostEnvironment::capture(Path::new("."));
    let Some(resolved) = resolve_in(tool, &host) else {
        return 0;
    };
    let mut args = vec!["--version".to_string()];
    if tool == "svn" {
        args.push("--quiet".to_string());
    }
    let launch = host.selected_shell(None).map_or_else(
        |_| Vec::new(),
        |shell| {
            let command = host.command(&shell, &resolved.path, &args);
            std::iter::once(command.get_program())
                .chain(command.get_args())
                .map(|argument| VdEnvSnapshotEntry {
                    name: owned_os_string(argument),
                    value: ptr::null_mut(),
                })
                .collect()
        },
    );
    // SAFETY: the output slots are writable and were checked for null.
    unsafe {
        *path_out = owned_os_string(resolved.path.as_os_str());
        *environment_out = environment_snapshot(&host);
        *launch_out = owned_snapshot(launch);
    }
    match resolved.source {
        ToolSource::Path => 1,
        ToolSource::Env => 2,
    }
}
