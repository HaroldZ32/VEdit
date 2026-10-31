# Project panel (`src/workspace/project/`)

Slice 1 / task T6. The Qt side of the project model: it shows the **expanded**
project — file count, member list, include dirs, defines and the diagnostics of
every bad `.f` entry — and keeps that project and its members across a restart.

The filelist semantics are **not** implemented here. `crates/vd-config` resolves
the project root, the `.f` files (`+incdir+`, `+define+`, nested `-f`, `//` and
`#` comments, duplicate/unknown-token diagnostics), and
`crates/vd-ffi/src/project.rs` exports the result over the C ABI:

| C entry point | used for |
|---|---|
| `vd_service_create` / `vd_service_destroy` | one service handle per model |
| `vd_project_parse(service, root)` | starts the expansion, returns a job id immediately (C2) |
| `vd_project_poll(service, job, &info)` | PENDING / READY / FAILED |
| `vd_project_info_free(&info)` | releases the four string lists |
| `vd_service_error(service, &error)` | reason of a failed job |

## Files

| file | role |
|---|---|
| `project_state.{h,cpp}` | the resolved project + atomic JSON persistence (`project-model.json`) |
| `project_service.{h,cpp}` | the only file that includes `vd_ffi.h` |
| `project_model.{h,cpp}` | async `QTimer` polling, summary line, restore/refresh, save on success |
| `project_panel.{h,cpp}` | the dock's widget: summary, file list, diagnostics, Refresh |
| `project_integration.{h,cpp}` | `installProject(...)`: root resolution + dock insertion |
| `project_selftest.cpp` | headless checks (`--selfcheck`, `--seed`, `--restore`) |

## Restart persistence (AC-S1.3)

`<state dir>/project-model.json` records the root, the expanded files, the
include dirs, the defines, the diagnostics and the capture time. On start the
panel restores that state first (so the project and its members are visible even
before the service answers, and stay visible if it fails), then re-expands the
root and replaces the members with the live result. The state directory is the
shell's (`--state-dir` or the session store's directory), so a run with
`--state-dir X` keeps everything in `X`.

Root resolution order in `installProject`:
1. `--project <dir>` (command line),
2. this lane's persisted state,
3. the shell session's `project` field (first start after an upgrade).

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
