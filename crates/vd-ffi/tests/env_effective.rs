//! batch-20261115 AC-1.3: the imported environment selection reaches the C++ side
//! (the effective snapshot the terminal and the toolchain resolver read) and the
//! language-server CHILD.
//!
//! Both tests drive REAL code paths: the snapshot is the verb
//! `src/terminal/pty_session.cpp` calls before `fork()`, and the language-server
//! test spawns a real executable (a shell script standing in for
//! `verible-verilog-ls`) whose own environment dump is the evidence.
//!
//! The imported layer is a PROCESS-WIDE static (`vd_deeplint::binary`), so this
//! binary takes [`SERIAL`] first; the file is its own test target, so the static
//! cannot leak into another binary's tests.

use std::collections::BTreeMap;
use std::ffi::{CStr, CString};
use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::ptr;
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::time::{Duration, Instant};

use vd_ffi::capi::VdEnvSnapshot;
use vd_ffi::env::{vd_env_apply_selection, vd_env_effective_snapshot, vd_env_snapshot_free};
use vd_ffi::service::{vd_service_create, vd_service_destroy, VdService};

static SERIAL: Mutex<()> = Mutex::new(());

fn serial() -> MutexGuard<'static, ()> {
    SERIAL.lock().unwrap_or_else(PoisonError::into_inner)
}

struct TempDir(PathBuf);

impl TempDir {
    fn new(label: &str) -> Self {
        let serial = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|elapsed| elapsed.subsec_nanos())
            .unwrap_or(0);
        let path = std::env::temp_dir().join(format!(
            "vd-env-effective-{}-{serial}-{label}",
            std::process::id()
        ));
        fs::create_dir_all(&path)
            .unwrap_or_else(|error| panic!("creating {} failed: {error}", path.display()));
        Self(path)
    }

    fn path(&self) -> &Path {
        &self.0
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

/// One selection file holding exactly `entries` in the global layer.
fn write_selection(path: &Path, entries: &[(&str, &str)]) {
    let global: Vec<serde_json::Value> = entries
        .iter()
        .map(|(name, value)| serde_json::json!({"name": name, "value": value}))
        .collect();
    let body = serde_json::json!({"version": 1, "global": global, "projects": []});
    fs::write(path, body.to_string())
        .unwrap_or_else(|error| panic!("writing {} failed: {error}", path.display()));
}

/// Apply one selection file to the imported layer (the startup verb).
fn apply(service: *mut VdService, selection: &Path) {
    let text = selection
        .to_str()
        .unwrap_or_else(|| panic!("{} is not UTF-8", selection.display()));
    let path = CString::new(text).unwrap_or_else(|error| panic!("{error}"));
    let applied =
        unsafe { vd_env_apply_selection(service, path.as_ptr(), ptr::null(), ptr::null_mut()) };
    assert_eq!(applied, 1, "the selection must be applied");
}

/// Read a filled snapshot as `(name, value)` pairs.
fn snapshot_rows(snapshot: &VdEnvSnapshot) -> Vec<(String, String)> {
    if snapshot.items.is_null() || snapshot.count == 0 {
        return Vec::new();
    }
    // SAFETY: `vd_env_effective_snapshot` filled `items` with `count` owned rows.
    let rows = unsafe { std::slice::from_raw_parts(snapshot.items, snapshot.count) };
    rows.iter()
        .map(|row| {
            // SAFETY: every row carries two owned NUL-terminated strings.
            let name = unsafe { CStr::from_ptr(row.name) }
                .to_string_lossy()
                .into_owned();
            let value = unsafe { CStr::from_ptr(row.value) }
                .to_string_lossy()
                .into_owned();
            (name, value)
        })
        .collect()
}

/// AC-1.3/AC-1.4: the snapshot is `process < imported`, and clearing the
/// selection takes the imported entry away again (the negative control).
#[test]
fn effective_snapshot_carries_process_and_imported_entries() {
    let _serial = serial();
    let scratch = TempDir::new("snapshot");
    let selection = scratch.path().join("environment.json");
    let service = vd_service_create();
    assert!(!service.is_null(), "the service handle must exist");

    // No entry yet: the snapshot is the process environment (PATH is always set
    // in a POSIX test process), and the probe name is absent.
    write_selection(&selection, &[]);
    apply(service, &selection);
    let mut snapshot = unsafe { std::mem::zeroed::<VdEnvSnapshot>() };
    assert_eq!(
        unsafe { vd_env_effective_snapshot(service, &mut snapshot) },
        1
    );
    let rows = snapshot_rows(&snapshot);
    unsafe { vd_env_snapshot_free(&mut snapshot) };
    assert!(
        rows.iter().any(|(name, _)| name == "PATH"),
        "the process environment is part of the snapshot"
    );
    assert!(
        !rows.iter().any(|(name, _)| name == "VEDIT_SNAPSHOT_PROBE"),
        "an unselected name is not invented"
    );

    // With one imported entry: it is present with its real value.
    write_selection(&selection, &[("VEDIT_SNAPSHOT_PROBE", "snapshot-ok")]);
    apply(service, &selection);
    let mut snapshot = unsafe { std::mem::zeroed::<VdEnvSnapshot>() };
    assert_eq!(
        unsafe { vd_env_effective_snapshot(service, &mut snapshot) },
        1
    );
    let rows = snapshot_rows(&snapshot);
    unsafe { vd_env_snapshot_free(&mut snapshot) };
    assert!(
        rows.iter()
            .any(|(name, value)| name == "VEDIT_SNAPSHOT_PROBE" && value == "snapshot-ok"),
        "the imported value must be visible to the C++ consumers: {rows:?}"
    );

    // Negative control: an empty selection removes it again.
    write_selection(&selection, &[]);
    apply(service, &selection);
    let mut snapshot = unsafe { std::mem::zeroed::<VdEnvSnapshot>() };
    assert_eq!(
        unsafe { vd_env_effective_snapshot(service, &mut snapshot) },
        1
    );
    let rows = snapshot_rows(&snapshot);
    unsafe { vd_env_snapshot_free(&mut snapshot) };
    assert!(
        !rows.iter().any(|(name, _)| name == "VEDIT_SNAPSHOT_PROBE"),
        "an empty selection must not leave the old value behind"
    );

    // A null output slot is refused instead of crashing.
    assert_eq!(
        unsafe { vd_env_effective_snapshot(service, ptr::null_mut()) },
        0
    );
    unsafe { vd_service_destroy(service) };
}

/// AC-1.3: the language server is spawned with the imported layer, and the
/// packaged loader variables are stripped after it (the tool-child rule).
///
/// The "server" is a real executable: `/bin/sh` running a script that writes its
/// OWN environment to a file and exits. The child's dump is the evidence — this is
/// a real `execve`, not an inspection of a `Command` object.
#[test]
fn language_server_child_receives_the_imported_selection() {
    let _serial = serial();
    let scratch = TempDir::new("lsp-child");
    let workspace = scratch.path();
    let source = workspace.join("probe.sv");
    fs::write(&source, "module probe;\nendmodule\n")
        .unwrap_or_else(|error| panic!("writing the source file failed: {error}"));
    let dump = workspace.join("child-env.dump");
    let fake_server = workspace.join("fake-ls.sh");
    fs::write(
        &fake_server,
        format!("#!/bin/sh\nenv > '{}'\nexit 0\n", dump.to_string_lossy()),
    )
    .unwrap_or_else(|error| panic!("writing the fake server failed: {error}"));
    fs::set_permissions(&fake_server, fs::Permissions::from_mode(0o755))
        .unwrap_or_else(|error| panic!("making the fake server executable failed: {error}"));

    let selection = workspace.join("environment.json");
    write_selection(
        &selection,
        &[
            ("VEDIT_LSP_PROBE", "lsp-ok"),
            // The sanitize rule is applied AFTER the imported merge: a loader
            // path in the selection must not reach a tool child either.
            ("LD_LIBRARY_PATH", "/must-not-reach"),
        ],
    );
    let service = vd_service_create();
    assert!(!service.is_null());
    apply(service, &selection);

    let environment: BTreeMap<String, String> =
        vd_ffi::env::effective_environment().into_iter().collect();
    assert_eq!(
        environment.get("VEDIT_LSP_PROBE").map(String::as_str),
        Some("lsp-ok"),
        "the source of the injected layer is the applied store"
    );
    let config = vd_lsp::LspConfig::new(&fake_server, workspace).with_environment(environment);
    let handle = vd_lsp::SessionHandle::start(config, vec![source.clone()])
        .unwrap_or_else(|error| panic!("the session worker must start: {error}"));

    let deadline = Instant::now() + Duration::from_secs(20);
    while !dump.exists() && Instant::now() < deadline {
        std::thread::sleep(Duration::from_millis(20));
    }
    handle.stop();
    let text = fs::read_to_string(&dump).unwrap_or_else(|error| {
        panic!(
            "the language server was never spawned ({}): {error}",
            dump.display()
        )
    });
    assert!(
        text.lines().any(|line| line == "VEDIT_LSP_PROBE=lsp-ok"),
        "the server child must see the imported value: {text}"
    );
    assert!(
        !text.contains("/must-not-reach"),
        "an imported loader path must never reach a tool child: {text}"
    );
    unsafe { vd_service_destroy(service) };
}

/// AC-1.3, the PRODUCT wiring: `vd_lsp_session_start_v2` is the verb the C++ client
/// calls, and it must put the imported layer into the session configuration the
/// client spawns with. The test above drives `SessionHandle` directly, so it would
/// still pass if the product entry forgot to fill the layer — this one would not.
#[test]
fn product_lsp_session_start_injects_the_imported_selection() {
    let _serial = serial();
    let scratch = TempDir::new("lsp-product");
    let workspace = scratch.path();
    let source = workspace.join("probe.sv");
    fs::write(&source, "module probe;\nendmodule\n")
        .unwrap_or_else(|error| panic!("writing the source file failed: {error}"));
    let dump = workspace.join("child-env.dump");
    let fake_server = workspace.join("fake-ls.sh");
    fs::write(
        &fake_server,
        format!("#!/bin/sh\nenv > '{}'\nexit 0\n", dump.to_string_lossy()),
    )
    .unwrap_or_else(|error| panic!("writing the fake server failed: {error}"));
    fs::set_permissions(&fake_server, fs::Permissions::from_mode(0o755))
        .unwrap_or_else(|error| panic!("making the fake server executable failed: {error}"));

    let selection = workspace.join("environment.json");
    write_selection(&selection, &[("VEDIT_LSP_PRODUCT_PROBE", "product-lsp-ok")]);
    let service = vd_service_create();
    assert!(!service.is_null());
    apply(service, &selection);

    let ls_path = CString::new(fake_server.to_string_lossy().as_bytes())
        .unwrap_or_else(|error| panic!("{error}"));
    let root = CString::new(workspace.to_string_lossy().as_bytes())
        .unwrap_or_else(|error| panic!("{error}"));
    let file =
        CString::new(source.to_string_lossy().as_bytes()).unwrap_or_else(|error| panic!("{error}"));
    let mut items: [*mut std::ffi::c_char; 1] = [file.as_ptr() as *mut std::ffi::c_char];
    let files = vd_ffi::capi::VdStringList {
        count: items.len(),
        items: items.as_mut_ptr(),
    };
    let session = unsafe {
        vd_ffi::lsp::vd_lsp_session_start_v2(
            service,
            ls_path.as_ptr(),
            root.as_ptr(),
            ptr::null(),
            &files,
            0,
            ptr::null(),
            ptr::null(),
        )
    };
    assert_ne!(session, 0, "the session must be created");

    let deadline = Instant::now() + Duration::from_secs(20);
    while !dump.exists() && Instant::now() < deadline {
        std::thread::sleep(Duration::from_millis(20));
    }
    unsafe {
        vd_ffi::lsp::vd_lsp_session_stop(service, session);
        vd_ffi::lsp::vd_lsp_session_free(service, session);
        vd_service_destroy(service);
    }
    let text = fs::read_to_string(&dump).unwrap_or_else(|error| {
        panic!(
            "the product session never spawned the server ({}): {error}",
            dump.display()
        )
    });
    assert!(
        text.lines()
            .any(|line| line == "VEDIT_LSP_PRODUCT_PROBE=product-lsp-ok"),
        "the product session start must inject the imported layer: {text}"
    );
}
