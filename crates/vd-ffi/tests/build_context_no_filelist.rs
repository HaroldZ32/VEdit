//! G1-d/G1-h regression lock (batch-20261119): the generated slang build context
//! never nests the project's imported `.f` list.
//!
//! The C ABI keeps `vd_lsp_session_set_build_context`'s `build_files` parameter
//! for compatibility and IGNORES it: the context is
//! `+incdir+… / -D… / <the session's own files>`, and a line `-f <project.f>`
//! would hand the server the whole project again - slang's own last-processed
//! rule then picks a definition of a same-named module that disagrees with the
//! module tree (the realtime "module has no such port" false positives). This
//! test exists so that defect CANNOT come back silently:
//!
//! 1. the `.f` list is passed exactly as the old C++ client passed it (a
//!    `VdStringList` with the file's absolute path), through the public C ABI;
//! 2. the generated context file is read back from the directory the session
//!    worker writes it into, and it must contain the include dir, the define and
//!    the SESSION's file - and neither `-f` nor the `.f` path nor the extra
//!    source file only the `.f` names.
//!
//! Everything is black box: a minimal LSP peer (a `sh` script) answers the
//! handshake, records what it receives, and answers every request with `null`.
//! No `slang-server` is needed, so the lock also runs on a checkout that ships
//! no server.
#![cfg(unix)]

use std::ffi::{c_char, CStr, CString};
use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::ptr;
use std::time::{Duration, Instant};

use vd_ffi::capi::{VdError, VdStringList, VD_STATUS_READY};
use vd_ffi::lsp::{
    vd_lsp_session_free, vd_lsp_session_set_build_context, vd_lsp_session_start_v2,
    vd_lsp_session_status, vd_lsp_session_stop, vd_lsp_status_free, VdLspStatus,
};
use vd_ffi::service::{vd_service_create, vd_service_destroy, vd_service_error, VdService};

/// A scratch directory that removes itself.
struct Scratch {
    path: PathBuf,
}

impl Scratch {
    fn new(tag: &str) -> Self {
        let path = std::env::temp_dir().join(format!(
            "vedit-ctx-probe-{tag}-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|value| value.as_nanos())
                .unwrap_or_default()
        ));
        fs::create_dir_all(&path).unwrap_or_else(|error| {
            panic!(
                "creating the scratch directory {} failed: {error}",
                path.display()
            )
        });
        Self { path }
    }

    fn join(&self, name: &str) -> PathBuf {
        self.path.join(name)
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.path);
    }
}

/// One `VdStringList` over owned C strings (kept alive for its lifetime).
struct List {
    _owned: Vec<CString>,
    _raw: Vec<*mut c_char>,
    list: VdStringList,
}

impl List {
    fn new(values: &[&str]) -> Self {
        let owned: Vec<CString> = values
            .iter()
            .map(|value| {
                CString::new(*value)
                    .unwrap_or_else(|error| panic!("a fixture string must have no NUL: {error}"))
            })
            .collect();
        let mut raw: Vec<*mut c_char> = owned
            .iter()
            .map(|value| value.as_ptr().cast_mut())
            .collect();
        let list = VdStringList {
            count: raw.len(),
            items: if raw.is_empty() {
                ptr::null_mut()
            } else {
                raw.as_mut_ptr()
            },
        };
        Self {
            _owned: owned,
            _raw: raw,
            list,
        }
    }

    fn as_ptr(&self) -> *const VdStringList {
        &raw const self.list
    }
}

/// The fake LSP peer: `initialize` -> empty capabilities, every other REQUEST ->
/// `null`, and every received body appended to `capture` (one JSON per line).
///
/// The framing is JSON-RPC over stdio (`Content-Length: N\r\n\r\n<body>`), which
/// is what the client speaks; notifications (no `id`) are consumed silently.
fn fake_lsp_script(capture: &Path) -> String {
    format!(
        r#"#!/bin/sh
capture='{capture}'
reply() {{
  payload=$(printf '{{"jsonrpc":"2.0","id":%s,"result":%s}}' "$1" "$2")
  printf 'Content-Length: %s\r\n\r\n%s' "${{#payload}}" "$payload"
}}
while :; do
  len=""
  while IFS= read -r line; do
    line=$(printf '%s' "$line" | tr -d '\r')
    [ -z "$line" ] && break
    case "$line" in
      Content-Length:*) len=$(printf '%s' "${{line#Content-Length:}}" | tr -d ' ') ;;
    esac
  done
  [ -z "$len" ] && exit 0
  body=$(dd bs=1 count="$len" 2>/dev/null)
  printf '%s\n' "$body" >> "$capture"
  id=$(printf '%s' "$body" | sed -n 's/.*"id"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -n 1)
  case "$body" in
    *'"method":"initialize"'*) reply "$id" '{{"capabilities":{{}}}}' ;;
    *'"method":"workspace/executeCommand"'*) reply "$id" 'null' ;;
    *'"id":'*) reply "$id" 'null' ;;
  esac
done
"#,
        capture = capture.to_string_lossy()
    )
}

/// Every generated context file this process left in the temp tree.
fn context_files() -> Vec<PathBuf> {
    let prefix = format!("vedit-slang-context-{}-", std::process::id());
    let mut out = Vec::new();
    let Ok(entries) = fs::read_dir(std::env::temp_dir()) else {
        return out;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if name.starts_with(&prefix) {
            let file = entry.path().join("context.f");
            if file.is_file() {
                out.push(file);
            }
        }
    }
    out
}

#[test]
fn the_generated_build_context_never_nests_the_project_filelist() {
    let scratch = Scratch::new("g1h");
    let workspace = scratch.join("design");
    create_dir(&workspace);
    let source = workspace.join("top.sv");
    write_file(&source, "module top;\nendmodule\n");
    // The file ONLY the project `.f` names: it must never reach the context.
    let filelist_only = workspace.join("only-in-filelist.sv");
    write_file(&filelist_only, "module only_in_filelist;\nendmodule\n");
    // The project's imported `.f` list, exactly what the C++ panel hands over.
    let project_list = workspace.join("project-list.f");
    write_file(&project_list, &format!("{}\n", filelist_only.display()));
    let capture = scratch.join("lsp-received.log");
    let server = scratch.join("fake-slang-server.sh");
    write_file(&server, &fake_lsp_script(&capture));
    make_executable(&server);
    let include_dir = workspace.join("inc");
    create_dir(&include_dir);

    let before = context_files();

    let include_text = include_dir.to_string_lossy().into_owned();
    let source_text = source.to_string_lossy().into_owned();
    let project_list_text = project_list.to_string_lossy().into_owned();
    let filelist_only_text = filelist_only.to_string_lossy().into_owned();

    let service = vd_service_create();
    assert!(!service.is_null(), "the service handle must be created");
    let ls_path = c_string(&server.to_string_lossy(), "the server path");
    let root = c_string(&workspace.to_string_lossy(), "the workspace root");
    let source_c = c_string(&source_text, "the session file path");

    let mut file_items: [*mut c_char; 1] = [source_c.as_ptr().cast_mut()];
    let files = VdStringList {
        count: file_items.len(),
        items: file_items.as_mut_ptr(),
    };
    let include_dirs = List::new(&[include_text.as_str()]);
    let defines = List::new(&["WIDTH=8"]);

    let session = unsafe {
        vd_lsp_session_start_v2(
            service,
            ls_path.as_ptr(),
            root.as_ptr(),
            ptr::null(),
            &files,
            /*backend=*/ 1, // slang: the only backend with a build-context channel
            include_dirs.as_ptr(),
            defines.as_ptr(),
        )
    };
    assert_ne!(
        session,
        0,
        "the session must be created: {}",
        last_error(service)
    );

    assert!(
        wait_for_ready(service, session, Duration::from_secs(20)),
        "the fake server never reached Ready: {}",
        session_message(service, session)
    );

    // The G1-h call: the `.f` list is passed exactly as the old client passed it.
    let build_files = List::new(&[project_list_text.as_str()]);
    let status = unsafe {
        vd_lsp_session_set_build_context(
            service,
            session,
            include_dirs.as_ptr(),
            defines.as_ptr(),
            build_files.as_ptr(),
        )
    };
    assert_eq!(
        status,
        VD_STATUS_READY,
        "the build-context command must be queued: {}",
        last_error(service)
    );

    let applied = wait_for(
        || session_message(service, session).contains("build context:"),
        Duration::from_secs(20),
    );
    let message = session_message(service, session);
    assert!(
        applied,
        "the context was never delivered to the server: {message}"
    );

    // The worker writes ONE generated context file per push into a private
    // directory under the temp tree; read the one this push created.
    let after = context_files();
    let generated: Vec<PathBuf> = after
        .into_iter()
        .filter(|path| !before.contains(path))
        .collect();
    let Some(context_path) = generated.first() else {
        panic!("no generated build context file was written (session: {message})");
    };
    let context_text = fs::read_to_string(context_path)
        .unwrap_or_else(|error| panic!("reading the generated context failed: {error}"));

    assert!(
        context_text.contains(&format!("+incdir+\"{include_text}\"")),
        "the include directory must be in the context: {context_text}"
    );
    assert!(
        context_text.contains("-D\"WIDTH=8\""),
        "the define must be in the context: {context_text}"
    );
    assert!(
        context_text.contains(&source_text),
        "the SESSION's own file must be what the context compiles: {context_text}"
    );
    assert!(
        !context_text.contains("-f"),
        "the context must never nest a file list with `-f` (G1-d): {context_text}"
    );
    assert!(
        !context_text.contains(&project_list_text),
        "the project's imported `.f` must never reach the context: {context_text}"
    );
    assert!(
        !context_text.contains(&filelist_only_text),
        "a file only the project `.f` names must never reach the context: {context_text}"
    );

    // The push really reached the server (the fake peer recorded it).
    let received = fs::read_to_string(&capture).unwrap_or_default();
    assert!(
        received.contains("slang.setBuildFile"),
        "the server never received the build file: {received}"
    );
    assert!(
        !received.contains(&project_list_text),
        "the project's `.f` path must never be sent to the server: {received}"
    );

    unsafe {
        vd_lsp_session_stop(service, session);
        vd_lsp_session_free(service, session);
        vd_service_destroy(service);
    }
    for path in &generated {
        if let Some(directory) = path.parent() {
            let _ = fs::remove_dir_all(directory);
        }
    }
}

/// Create `path` and every missing parent, failing with the path in the message.
fn create_dir(path: &Path) {
    fs::create_dir_all(path)
        .unwrap_or_else(|error| panic!("creating {} failed: {error}", path.display()));
}

/// Write `text` to `path`, failing with the path in the message.
fn write_file(path: &Path, text: &str) {
    fs::write(path, text)
        .unwrap_or_else(|error| panic!("writing {} failed: {error}", path.display()));
}

/// `chmod 0755`, so the session can spawn the fake server.
fn make_executable(path: &Path) {
    fs::set_permissions(path, fs::Permissions::from_mode(0o755))
        .unwrap_or_else(|error| panic!("making {} executable failed: {error}", path.display()));
}

/// One NUL-free C string, with `what` naming the value in the failure.
fn c_string(value: &str, what: &str) -> CString {
    CString::new(value)
        .unwrap_or_else(|error| panic!("{what} must not contain a NUL byte: {error}"))
}

fn wait_for(mut predicate: impl FnMut() -> bool, budget: Duration) -> bool {
    let deadline = Instant::now() + budget;
    loop {
        if predicate() {
            return true;
        }
        if Instant::now() >= deadline {
            return false;
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

fn wait_for_ready(service: *mut VdService, session: u64, budget: Duration) -> bool {
    wait_for(
        || {
            let mut status = zeroed_status();
            let read = unsafe { vd_lsp_session_status(service, session, &mut status) };
            if read != VD_STATUS_READY {
                return false;
            }
            let phase = status.phase;
            unsafe { vd_lsp_status_free(&mut status) };
            phase == 1
        },
        budget,
    )
}

fn session_message(service: *mut VdService, session: u64) -> String {
    let mut status = zeroed_status();
    let read = unsafe { vd_lsp_session_status(service, session, &mut status) };
    if read != VD_STATUS_READY {
        return String::new();
    }
    let message = status_string(status.message);
    unsafe { vd_lsp_status_free(&mut status) };
    message
}

fn zeroed_status() -> VdLspStatus {
    // SAFETY: the C struct is plain data; `vd_lsp_session_status` fills it and
    // `vd_lsp_status_free` releases the owned pointers.
    unsafe { std::mem::zeroed() }
}

fn status_string(pointer: *mut c_char) -> String {
    if pointer.is_null() {
        return String::new();
    }
    // SAFETY: the pointer is a NUL-terminated string owned by the caller side.
    unsafe { CStr::from_ptr(pointer) }
        .to_string_lossy()
        .into_owned()
}

fn last_error(service: *mut VdService) -> String {
    let mut error = unsafe { std::mem::zeroed::<VdError>() };
    let read = unsafe { vd_service_error(service, &mut error) };
    if read != 1 {
        return String::new();
    }
    // `VdError.message` is an inline NUL-terminated buffer, not an owned pointer.
    // SAFETY: `vd_service_error` filled it, so it is NUL-terminated.
    unsafe { CStr::from_ptr(error.message.as_ptr()) }
        .to_string_lossy()
        .into_owned()
}
