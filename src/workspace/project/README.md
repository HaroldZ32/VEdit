# Project panel (`src/workspace/project/`)

Task T6 (project model). The Qt side of the project model: opening a folder makes
it the **project root**, importing `.f` filelists adds them to the project, the
file set is the union of the `.f` expansion and a directory scan, and the panel
shows the file count, the member list, the imported filelists, the include dirs,
the defines and every bad `.f` entry with `file:line + reason`. Everything
persists across a restart (`<state dir>/project-model.json`).

The filelist semantics are **not** implemented here. `crates/vd-config` resolves
the project root, the `.f` files (`+incdir+`, `+define+`, nested `-f`, `//` and
`#` comments, duplicate/unknown-token/missing-entry diagnostics), and
`crates/vd-ffi/src/project.rs` exports the result over the C ABI:

| C entry point | used for |
|---|---|
| `vd_service_create` / `vd_service_destroy` | one service handle per model |
| `vd_project_expand(service, root, filelists, settings)` | starts the T6 expansion (imported `.f` + `-I`/`-D`), returns a job id immediately (C2) |
| `vd_project_poll(service, job, &info)` | PENDING / READY / FAILED |
| `vd_project_info_free(&info)` | releases the string lists (files, relative files, include dirs, defines, warnings) |
| `vd_service_error(service, &error)` | reason of a failed job (a missing root is `VD_ERR_NOT_FOUND`) |

`vd_project_parse` (the older entry) is kept unchanged for `--selfcheck-ffi` and
the T14 fixture contract.

## Files

| file | role |
|---|---|
| `project_state.{h,cpp}` | the resolved project (root, imported filelists, files, `-I`, `-D`, diagnostics) + atomic JSON persistence |
| `project_service.{h,cpp}` | the only file that includes `vd_ffi.h` |
| `project_model.{h,cpp}` | async `QTimer` polling, import/remove filelist, settings watch + re-expand, summary line, restore/refresh, save on success |
| `project_panel.{h,cpp}` | the dock's widget: summary, filelists (remove), file list (double-click jump), include dirs, defines, diagnostics |
| `project_subscription.{h,cpp}` | binds any object exposing `directoryOpened(QString)` to `ProjectModel::openRoot` (AC-2.1; name-based so the lane stays standalone) |
| `project_integration.{h,cpp}` | `installProject(...)`: root resolution + dock insertion |
| `project_selftest.cpp` | headless checks (`--selfcheck`, `--seed`, `--restore`) |

## Acceptance mapping

* **AC-2.1** — the folder opened in the File Tree emits `directoryOpened`; the
  project model is subscribed to every such emitter under the shell, so the
  folder IS the root (the Rust expansion pins it; it never walks up to a parent
  `.git`). The dock is created hidden and becomes visible with the first project.
* **AC-2.2** — “Import Filelist…” adds one or more `.f` files (removable);
  the file set is `scan(root)` (`.v/.sv/.vh/.svh`, hidden directories skipped)
  ∪ the `.f` expansion, de-duplicated by canonical path. The panel shows the
  file count, the list, the filelists, the include dirs, the defines and the
  `.f` diagnostics.
* **AC-2.3** — every bad `.f` line is reported as `code: <file>:<line>: <reason>`
  by `crates/vd-config`; the three required cases are covered by Rust tests
  (missing `+incdir+` directory, unknown directive, missing nested `-f`) and by
  the self-check through the panel.
* **AC-2.4** — the state file records root + imported filelists + members +
  `-I`/`-D` + diagnostics; `restoreFromDisk()` lists the same project before the
  service answers. `--seed` / `--restore` prove it across two real processes
  (`evidence/w3-project/`).
* **AC-2.5** — the tools page stores `tools.include_dirs` / `tools.defines` in
  `settings.toml`; the Rust worker reads them (never the GUI thread) and
  **appends** them to the project's own values, de-duplicated. The values reach
  the model (`includeDirs()`, `defines()`), the `-I`/`-D` argument dump
  (`backendArguments()`), and the backend ABI (`vd_lsp_session_start_v2` /
  `vd_deeplint_*` take the same two lists). A settings save is observed by a
  directory watch (with a 1 s stamp poll fallback for filesystems without a
  working watch) and re-expands without a restart.
* **AC-7.4** — a project file row jumps through the one process-wide
  `jump::IJumpTarget` registry with the absolute path and 1-based position.

### Generated `.f` and server launch (measured rule)

A generated `.f` is best written with the project-relative entries
(`relativeFiles()` / `vd_project_info.relative_files`), so it stays portable
across a moved checkout. That is a **portability aid, not the correctness
rule**. The correctness rule was measured in a controlled 4-way experiment
(2026-11-02, `evidence/w2-spike/07-buildfile-rule.txt`):

* the server's **cwd must be the project root**, and `rootUri` must point at the
  same root and exist in that process's filesystem — otherwise `initialize`
  fails;
* with the correct cwd, even **absolute** entries resolve correctly; with the
  wrong cwd even relative entries land on a sibling path while the tree still
  looks normal (the silent failure this guards against);
* the `.f` file itself may live anywhere (the state dir, a temp area).

`projectRoot()` is the ONE source of truth for that cwd/`rootUri`; do not
re-derive it in the launcher, or the two values disagree after a move. A root
that does not exist is reported as `VD_ERR_NOT_FOUND` and shows up as a visible
service diagnostic, never as an empty project.

## Restart persistence (AC-2.4)

`<state dir>/project-model.json` records `projectRoot`, `filelists`, `files`,
`includeDirs`, `defines`, `diagnostics` and the capture time. On start the panel
restores that state first (so the project and its members are visible even before
the service answers, and stay visible if it fails), then re-expands the root and
replaces the members with the live result. The state directory is the shell's
(`--state-dir` or the session store's directory), so a run with `--state-dir X`
keeps everything in `X` — including the `settings.toml` the lane watches.

Root resolution order in `installProject`:
1. `--project <dir>` (command line),
2. this lane's persisted state,
3. the shell session's `project` field (first start after an upgrade),
4. otherwise the dock stays hidden until the File Tree opens a folder.

## Build and test

```sh
# standalone (how the lane's own evidence is produced)
cmake -S src/workspace/project -B target/project-build -G Ninja \
  -DVEDIT_RUST_FFI_LIB="$PWD/target/release/libvd_ffi.a" \
  -DVEDIT_RUST_FFI_INCLUDE="$PWD/crates/vd-ffi/include"
cmake --build target/project-build
ctest --test-dir target/project-build --output-on-failure

# the real restart proof: two processes sharing a state directory
QT_QPA_PLATFORM=offscreen target/project-build/vedit-workspace-project-selftest \
  --seed tests/fixtures/verilog-project /tmp/project-state
QT_QPA_PLATFORM=offscreen target/project-build/vedit-workspace-project-selftest \
  --restore /tmp/project-state 7
```

Inside the product build the lane is added by `src/app/CMakeLists.txt`
(`VEDIT_WORKSPACE_LANES`) and called from `src/workspace/integration.cpp`.

## Known limitation (declared, not hidden)

The panel is inserted as its own dock (`dock-project`) with
`QMainWindow::addDockWidget`; it is **not** registered in the shell's
`PanelRegistry`, because that registry lives in `src/app/app_shell.cpp` (shared
by every lane). Consequence: there is **no View-menu toggle / shortcut for the
project panel** yet. The dock appears whenever a project root is known and is
restored with the rest of the dock layout by `QMainWindow::saveState`. If a
toggle is required later, it is a one-line panel spec in `src/app/app_shell.cpp`
plus a command entry — a separate, reviewed change.

## Headless only

This machine has no display server. Every check in `project_selftest.cpp` runs
with `QT_QPA_PLATFORM=offscreen` and asserts on data (file count, list,
diagnostics, persisted JSON); nothing here claims anything about how the dock
looks on screen.
