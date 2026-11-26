# Qt6 build and verification baseline

The baseline is `78093bf6d584533a17e38258b2139fa3bffe38fb` from the frozen `editor-compare-svn-tabs-20261126.md` plan. The base source was exported with `git archive` into an ignored directory in the feature worktree. All builds, fixtures and captures ran under `target/editor-compare-pr`. The feature build directory is `target/qt6-build` within that worktree.

## Acceptance cases

| Area | Cases to verify against the feature build |
| --- | --- |
| Compare | Open from the File menu, file tree and SVN into the active editor split’s tab row. Verify two native KTextEditor views, HDL highlighting with diff backgrounds, and no ordinary diagnostic dispatch for temporary snapshots. Verify two-way hunk copy, local editing, undo, independent save, read-only history sides, and save/discard/cancel on tab or window close. Exercise navigation, synchronized scrolling, insert/delete, identical files, final newline, encoding and stale asynchronous results. |
| Editor tabs | Compare tab widths with one file and enough files to overflow, including a long filename with a full-name tooltip. Drag the horizontal scrollbar thumb and use the wheel without changing the active file. Check that opening or selecting a file reveals the active tab. Exercise close, split, drag, file paths and session restore. |
| SVN | Create real local repositories and checkouts. Use current-file History and Compare with BASE from a normal editor, including a dirty buffer. Compare historical revisions and verify read-only snapshot sides. Switch files, switch working copies, and open a non-SVN file to exercise stale-path guards. |

## Runner and artifacts

| Input | Observed identity |
| --- | --- |
| Qt runner | `sha256:809c8072ac65be85a15bfc010400f1a0b2cffa10966cfebd4e7dcd0b418360c1`, Ubuntu 26.04.1 / glibc 2.43, Qt 6.10.2, KF6/KTextEditor/KSyntaxHighlighting 6.24.0, tree-sitter 0.25.9, CMake 4.2.3, Ninja 1.13.2, GCC 15.2.0 |
| Rust runner | `sha256:0798e93a6d3026424dfcda486014462298474a3e773e448e31856028f921a0e0`, Rust and Cargo 1.99.0, glibc 2.17 |
| slang | Cached binary checked against `SLANG_BIN_SHA256=63cb60a05f6ca57f648b8e2ef6de61ee0a3132c1c9d481f831dba52388832252` in the archived source and staged in both trees |
| Baseline `vedit-app` | SHA-256 `6ee3e1e8acd10aa7eef2985e50cd01ca86422b9b077fff161acf9aa425c27cfe` |
| Baseline `libvd_ffi.a` | SHA-256 `505a5945bf24e8ad01a37b81f62bd822ec700309735324951fdf33742c8f6934` |
| QA tools | SVN and svnadmin 1.14.5, xdotool 3.20160805.1, xwd, host FFmpeg 8.0.1. Ubuntu `.deb` packages were extracted into the ignored `target/editor-compare-qa/native-tools/` directory. Their SHA-256 receipts are in the download and capture setup logs. |

The isolated baseline compiled 565 of 565 Ninja steps after the Rust stage and exact-version CMake configuration passed. The baseline CTest registry contained 99 cases. The initial full run exited 8 with two failures and eight skips. The archive omitted Git metadata for the ABI fixture. The realtime-depth case reported zero LSP requests, diagnostics and sessions with the HDL server unavailable. The later focused run supplied Git metadata and real SVN and passed seven cases. The shared feature build cache was seeded from the isolated output, and both binary hashes were checked after copying. Its test results only represent the base until the feature source is rebuilt.

## Commands and observations

The command logs listed here are under the feature worktree’s ignored `target/editor-compare-qa/` directory. They contain expanded command lines and exit receipts.

| Check | Command and observation |
| --- | --- |
| Runner identities | `docker image inspect … vedit-build:local vedit-qt6-build:local`, then independent container runs of `dpkg-query`, `pkg-config`, compiler version commands, `rustc --version`, `cargo --version` and `getconf GNU_LIBC_VERSION`. Exit 0. `runner-facts.log` |
| Baseline build | `git archive 78093bf6d584533a17e38258b2139fa3bffe38fb` then `VEDIT_CI_LOG_DIR=target/qa-logs VEDIT_SKIP_IMAGE_BUILD=1 VEDIT_QT6_BUILD_JOBS=6 bash scripts/build-qt.sh` in the exported source with the base commit time as `SOURCE_DATE_EPOCH`. The first CTest run lacked Git metadata and native SVN. The ABI test requires its fixture to be inside Git and SVN tests require native tooling. `baseline-build.log` |
| Cache preparation | `rsync -a` of baseline build/cache outputs, `sha256sum` of binaries before and after, and `cmp` of the generated FFI header. Exit 0. `cache-seed.log` |
| Real SVN | `svnadmin create`, `svn import`, `svn checkout`, `svn log -l 1` and `svn cat -r BASE` in the Qt runner with `--network none`. Exit 0, revision 1 and the exact HDL fixture text observed. `svn-runner-smoke.log` |
| SVN after environment filtering | The first product-run SVN attempt reached the staged executable but failed loading `libsvn_client-1.so.1` after the product removed the inherited library path. The disposable QA runner adds the staged directory to its own linker cache with `ldconfig -X`. `env -i PATH=/usr/bin:/bin /native-tools/usr/bin/{svn,svnadmin} --version --quiet` each returned 1.14.5 with exit 0. `focused-baseline.log`, `native-loader.log` |
| Git worktree read | The runner binds the worktree’s common Git metadata read-only at its original absolute path. `git rev-parse --is-inside-work-tree`, `git rev-parse HEAD` and the fixture’s `git rev-parse --show-toplevel` exited 0. `git-mount.log` |
| Focused baseline with Git and SVN | `bash target/editor-compare-qa/run-qt.sh env QT_QPA_PLATFORM=offscreen ctest --test-dir target/qt6-build --output-on-failure -R '^vedit-(file-compare(-selftest\|-close-active-diff\|-exit-active-diff)\|editor-area-selftest\|svn-workbench-selftest\|svn-workspaces-selftest\|app-ffi-abi-selftest)$'`. CTest reported 7 passed, 0 failed, exit 0. This includes the ABI fixture and both SVN tests with the environment fixes applied. [Command and test output](baseline-focused.log) |
| Real X11 capture | `timeout 20 xvfb-run -a -s '-screen 0 1280x800x24 -nolisten tcp' …` as a child process in the pinned container, normal `vedit-app --no-session-restore --state-dir /capture/state --hold 10000 /capture/baseline_capture.sv` launch with `QT_QPA_PLATFORM=xcb`, `xdotool`, and `xwd -root`. FFmpeg generated PNG and H.264 from the X11 frames. Exit 0. The PNG was visually inspected and showed the opened SystemVerilog file with syntax colors. The 1280×820 window was 20 pixels taller than the capture, so final capture uses a larger viewport. `baseline-capture.log`, `editor-capture-1pMDLAda/baseline.png`, `editor-capture-1pMDLAda/baseline.mp4` |

The host-to-container X11 probe exited 1. The follow-up observed that the mounted host X11 socket was absent in the container even though the Xauthority record was readable. Running Xvfb and the window/capture tools in the same isolated runner succeeded.

The cached Verible binaries matched the three repository SHA-256 pins. The cached slang-server archive matched `SLANG_SERVER_SHA256` before extraction. Verible `v0.0-4298-g085eef05` and slang-server `0.3.1+` each reported their version with exit 0. The isolated baseline then passed `ctest --test-dir target/qt6-build --output-on-failure -R '^vedit-realtime-depth-switch-e2e$'` with both servers available, 1 passed, exit 0. [Exact command and output](baseline-realtime.log)

## Feature build coordination

QA owns configure and compile operations in the shared feature build directory. Writer tests may run against an explicitly reported fresh build, with isolated fixture/settings paths. `bash target/editor-compare-qa/run-qt.sh <command> [args...]` runs a command in the pinned Qt image with network disabled, worktree Git metadata read-only, real SVN tools, and a separate CTest log directory. It leaves the pinned image unchanged.
