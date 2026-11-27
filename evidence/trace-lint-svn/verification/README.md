# Independent native verification

I built and tested code commit `bb7d1458eb993b7c2c706f7b15aa5f2bf2e684bc` in a clean integration checkout. The comparison baseline was `752c8ab8e2e22528f4e863da8318d81e177203e8`. Any follow-up commit that contains only these artifacts refers back to the tested code commit. The acceptance criteria are in `.mpd/plans/trace-lint-svn-20261127.md`. [User guide](../../../docs/USER_GUIDE.md) · [Artifact hashes](artifacts.sha256)

| Acceptance | Observed result |
| --- | --- |
| 1. Oversized trace | The original probe reproduced the `100663296` byte limit. The integrated Qt Xvfb probe imported a real filelist, generated a **138,188,283-byte** slang AST, jumped to the driver in `leaf.sv` and the load in `top.sv`, and reused one elaboration. Cold action **2,679 ms**, 257 GUI timer events, largest observed event gap **21 ms**. Warm action **0 ms** at timer precision. Editing invalidated the cache in **27 ms**, replacement waited for the old compiler to exit, and shell destruction with a live large cache took **25 ms**. Normal trace also passed with repeated instances, chains and a valid empty result. |
| 2 and 6. Editing and lint | In a 45,003-line Xvfb fixture with the real slang-server start delayed by two seconds, open took **26 ms**, the slowest key took **2 ms**, and the largest event gap was **64 ms**. One coalesced analysis yielded the latest unsaved diagnostic **2,337 ms** after the burst. The stale rows cleared, no intermediate result appeared, a different file received its own diagnostic after switch and close, and disk bytes stayed unchanged. The 46,126-line editor benchmark recorded **32 ms** open, **21 ms** input and **20 ms** maximum event gap. The focused diagnostic suite covered one-level scope, file switches, restarts and a manual-only whole-project policy. |
| 3. SVN working tree | With genuine SVN 1.7.14, a single click selected a row. Double-click used the editor comparison page for modified, added, deleted and unversioned files. The read-only left side showed `BASE` and the right side used the working buffer. SVN had no inline diff. The Git workspace VCS and existing file-comparison tests passed. [Working tree](screenshots/working-tree-and-gutter.png) · [BASE comparison](screenshots/working-base-to-copy.png) |
| 4. Legacy file history | Signed SVN and `svnadmin` **1.7.14** ran in the pinned Qt image using a private CentOS loader. The actual CLI refused `--show-item`. Real `file://` checkouts passed history, snapshot and comparison checks. The file-scoped view kept its own scope and cleared its stale Chinese startup prompt when rows arrived. [Chinese file history](screenshots/editor-file-history-zh.png) |
| 5. Project history | Working tree, History and Project History appeared as sibling tabs. Selecting `r5` displayed the nested project's changed paths, including a Chinese filename containing a space and `@`. Double-click opened read-only revision pairs for modifications, empty sides for additions and deletions, and copy-source ancestry for a copy. The delayed callback controls covered a newer selection, switching away and back, revision replacement, hiding the dock and changing the working copy. [Changed paths](screenshots/project-history-changed-files.png) · [Modified](screenshots/project-history-modified.png) · [Added](screenshots/project-history-added.png) · [Deleted](screenshots/project-history-deleted.png) · [Copied](screenshots/project-history-copied.png) |

The selected run reported **15 of 15 CTests passed** and **20 of 20 product C ABI checks passed**. The older diagnostics-only `vedit-project-lint-e2e` has an internal skip when its positive fixture has no filelist. The separate `vedit-module-closure-e2e` passed with 81 checks, zero failures and one internal skip. Its imported filelist triggered zero automatic project passes. The real closure included the direct child and excluded the grandchild. The explicit lint action submitted the expected five effective compiler inputs from six project files. The exact-commit Rust LSP monotonic-version test passed. The supplementary T13 run of the same LSP scope using pinned Rust recorded **65 passed, 0 failed** across eight test binaries. The exact-commit Rust `svn_compare_e2e` passed on genuine 1.7.14, including the r4 working copy whose original node was edited at r5 and deleted at r6 before an unrelated r7 addition reused the path. [Command output excerpts](checks.log)

The Xvfb trace run's largest single-process RSS, including subprocesses, was **522,588 KiB**. The disposable test container peaked at **1,374,244,864 bytes**, including filesystem cache. The same test measured an offscreen cold-action gap of **127 ms** in a separate run. The standalone same-fixture Rust comparison recorded **517,396 KiB** for the old whole-document parser versus **242,460 KiB** for streaming navigation. The standalone comparison used a larger cap only to let the original parser finish the measurement. The compiler's own memory and timing are reported separately. [Trace memory control](../trace/README.md)

## Reproduction

The build used `vedit-build:local` image `0798e93a6d30` with Rust 1.99.0 and `vedit-qt6-build:local` image `809c8072ac65` with Qt 6.10.2, KF6 6.24.0 and tree-sitter 0.25.9. Both helpers mount the checkout at `/vedit-build`. Prepare the pinned slang and Verible artifacts using the build guide. Use a fresh state/config/cache/data/runtime directory for each run. The fixture repos and HDL are synthetic. [Build guide](../../../docs/BUILD.md)

From the checkout root, with the pinned images and Cargo cache available:

```bash
source scripts/lib-qt.sh
source scripts/lib.sh
load_versions
load_qt_versions
export VEDIT_QT6_BUILD_JOBS=3
export VEDIT_CI_LOG_DIR=target/trace-lint-svn-verification-logs
export SOURCE_DATE_EPOCH=$(git show -s --format=%ct HEAD)
run_in_build_container bash -lc '
  set -euo pipefail
  source scripts/versions.env
  export RUSTFLAGS="${RUSTFLAGS:-} ${RELEASE_RUSTFLAGS}" CARGO_INCREMENTAL=0
  cargo build --locked --offline -p vd-ffi --release -j 3
  cargo test --locked --offline -p vd-lsp --lib --release -j 2 late_older_version_does_not_replace_current_diagnostics
'
git diff --exit-code HEAD -- crates/vd-ffi/include/vd_ffi.h
run_in_qt6_container cmake --build target/qt6-build --parallel 3 --target \
  vedit-app vedit-signal-trace-selftest vedit-editor-open-bench \
  vedit-signature-help-selftest vedit-realtime-context-e2e \
  vedit-realtime-depth-switch-e2e vedit-active-watcher-cap-selftest \
  vedit-app-batch-layout-selftest vedit-project-lint-e2e \
  vedit-workspace-diagnostics-selftest vedit-file-compare-selftest \
  vedit-svn-workbench-selftest vedit-workspace-vcs-selftest vedit-module-closure-e2e
bun run verify:docs
```

The existing CMake cache used Ninja, `RelWithDebInfo`, the newly built `target/release/libvd_ffi.a` and the generated `crates/vd-ffi/include/vd_ffi.h`. For a fresh cache, configure it as shown in the build guide or in `scripts/build-qt-in-container.sh`.

Inside the Qt image, run the following from `/vedit-build`. Set `VEDIT_SLANG_SERVER` to the real pinned slang-server 0.3.1+ path. For the selective CTest group I supplied the SVN 1.14.5 compatibility wrapper. The stand-alone SVN workbench checks below used the actual 1.7.14 clients.

```bash
export VEDIT_NO_ENV_AUTOCAPTURE=1
export VEDIT_SLANG=/vedit-build/build/slang/out/slang
export VEDIT_VERIBLE_LS=/vedit-build/.mpd/recon/verible/bin/verible-verilog-ls
export QT_QPA_PLATFORM=offscreen
build=target/qt6-build
ctest --test-dir "$build" -V -R '^(vedit-signal-trace(-oversize)?-selftest|vedit-editor-open-bench|vedit-signature-help-selftest|vedit-realtime-context-e2e|vedit-realtime-depth-switch-e2e|vedit-active-watcher-cap-selftest|vedit-app-batch-layout-selftest|vedit-project-lint-e2e|vedit-workspace-diagnostics-selftest|vedit-workspace-vcs-selftest|vedit-file-compare-.+)$'
ctest --test-dir "$build" -V -R '^vedit-module-closure-e2e$'
"$build/src/app/vedit-app" --selfcheck-ffi crates/vd-ffi/tests/fixtures/project
```

For the Xvfb runs, launch `Xvfb -displayfd 3 -screen 0 1440x960x24 -nolisten tcp`, writing FD 3 to a new scratch file. Wait for the file, read the display number and set `DISPLAY`, `QT_QPA_PLATFORM=xcb`, `QT_OPENGL=software` and `LIBGL_ALWAYS_SOFTWARE=1`. Capture paths must exist before starting the workbench tests. Use bundled CJK fonts when running the Chinese catalog.

```bash
"$build/src/app/vedit-signal-trace-selftest" "$PWD/tests/fixtures/signal_trace" "$VEDIT_SLANG" --oversize
# Point VEDIT_SLANG_SERVER at a wrapper that sleeps 2 s and execs the real server with unchanged argv.
"$build/src/app/workspace-lane-diagnostics/vedit-realtime-context-e2e" --stress
# Set VEDIT_SVN_BIN and VEDIT_SVNADMIN to the genuine 1.7.14 tools or their loaders.
export VEDIT_SVN_BIN VEDIT_SVNADMIN
export PATH="$(dirname "$VEDIT_SVN_BIN"):$PATH" VEDIT_SVN="$VEDIT_SVN_BIN"
"$VEDIT_SVN_BIN" info --show-item url . # expected exit 1 for 1.7.14
VEDIT_SVN_UI_CAPTURE="$capture_en" "$build/src/app/vedit-svn-workbench-selftest"
VEDIT_SVN_UI_CAPTURE="$capture_zh" VEDIT_SVN_ZH_CATALOGS="$build/src/app/translations" \
  "$build/src/app/vedit-svn-workbench-selftest"
```

The legacy RPMs came from the CentOS 7 Vault and `rpm --checksig` passed with the CentOS 7 key. To run the same binaries in Qt's Ubuntu image, copy dependencies identified by `ldd` in CentOS 7 and include its `ld-linux-x86-64.so.2` and `libnss_files.so.2`. Invoke the old loader with `--inhibit-cache --library-path <private legacy library folder>` and point `LOCPATH` to a UTF-8 locale generated in that old userspace. Only the `svn` and `svnadmin` subprocesses use that loader. Inside the pinned CentOS image, point `VEDIT_SVN_BIN` and `VEDIT_SVNADMIN` to the same 1.7.14 clients and run the backend fixture. [Legacy packages and test fixture](../svn-data/README.md)

```bash
VEDIT_TEST_LEGACY_SVN_DIRECT=1 cargo test --locked --offline -p vd-vcs --release -j 2 \
  --test svn_compare_e2e -- --nocapture --test-threads=1
```

## Verification records

The registered `docs` gate exited 0 in each bound loop. The native and Rust results above establish the behavior and timing coverage.

| Task | Gate evidence | Recorded verdict |
| --- | --- | --- |
| T2, trace | `ev-20261127T085134-4e8683` | `rec-20261127T090349-e0a9a7` PASS |
| T3, edit and lint | `ev-20261127T085154-254750` | `rec-20261127T090402-349350` PASS |
| T4, SVN interface | `ev-20261127T085205-c6f87f` | `rec-20261127T090415-d01e98` PASS |
| T5, SVN compatibility and ABI | `ev-20261127T085215-a2f327` | `rec-20261127T090427-43f0cb` PASS |

## Review bounds

Coverage is limited to the Linux pinned toolchain, synthetic HDL, local `file://` repositories and the selected gates. Qt probes generated widget and action events under offscreen or Xvfb/xcb. The timing results apply to those fixtures and platforms. Customer projects, remote authentication, release packaging, hardware input and other desktop platforms fall outside these runs. The comparison headers clip on some 512-pixel panes when the filename and side annotation are both long. The empty and occupied sides and revision contents remain visible in the examined screenshots.
