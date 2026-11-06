# loader / i18n / restart / slang — three packaged-AppDir defects

Date: 2026-11-04. Tree: HEAD `4e2d0a5`, dirty with unrelated work in progress
(not reverted). Product: Qt6/KF6 `vedit-app`, packaged AppDir at
`packaging/runtime/out/AppDir` (AppRun → bundled loader, so Qt's
`applicationDirPath()` is the AppDir ROOT and `applicationFilePath()` is the
loader).

## Defects and fixes

### A. Manual language switch stayed English

* `src/app/i18n.cpp` — `translationCandidates()` now probes
  `<appDir>/bin/translations` (packaged AppDir) → `<appDir>/translations`
  (build tree/tarball) → `<appDir>/../share/vedit/translations` (prefix), in that
  order. `translationsDirectoryFor()` returns the first existing candidate; the
  missing-`.qm` diagnostic lists every candidate that was actually searched
  instead of naming only the `../share` fallback.
* `packaging/runtime/AppRun` — guarded `VEDIT_TRANSLATIONS_DIR="$APPDIR/bin/translations"`
  export (only when unset **and** the directory exists), mirroring the
  `VEDIT_SLANG` guard.
* `docs/USER_GUIDE.md` §6 + §8 — Auto follows the system locale; the package
  ships only `C.UTF-8`, so packaged Auto resolves to English. Choosing
  “简体中文” explicitly and restarting works.

### B. Restart spawned the loader and died

* `src/app/i18n.cpp` — `restartCommand()` resolves
  `$APPIMAGE` → `<applicationDirPath>/AppRun` (must exist and be executable) →
  `applicationFilePath()`, always forwards `arguments().mid(1)`.
  `restartApplication()` uses it, prints one `VEDIT-RESTART program=… args=…`
  line to stderr, then `startDetached` as before. The loader command is never
  hand-assembled; the `setRestartApplicationHandler()` testing seam is kept.
* `src/app/i18n_selftest.cpp` + `src/app/CMakeLists.txt` — headless regression
  test (ctest `vedit-app-i18n-selftest`) covering: packaged AppDir → AppRun,
  arguments forwarded, `$APPIMAGE` wins, non-executable AppRun falls back,
  plain build-tree → `applicationFilePath()`, and the three translation
  candidates (plus a real-AppDir argv probe).

### C. Module tree: “slang failed … produced no diagnostics”

* `crates/vd-deeplint/src/binary.rs` — one shared helper
  `sanitize_tool_child_env()` removes `LD_LIBRARY_PATH` and `LOCPATH` from every
  tool child. It is applied to both spawns:
  `hierarchy.rs::elaborate()` and `service.rs::run_engine()` (slang and verible).
  The packaged `bin/slang` is a glibc-2.17 build executed by the **host** loader;
  inheriting `LD_LIBRARY_PATH=<AppDir>/lib` made that loader load the packaged
  glibc 2.43 and abort before `main`.
* `crates/vd-deeplint/src/lib.rs` — `LintFailure::MissingOutput::user_message()`
  appends the first non-empty stderr line, so the loader reason is visible.
* `crates/vd-deeplint/src/hierarchy.rs` — a nonzero exit **with** parseable AST
  JSON is a normal result now (the crate rule from `lib.rs:33-40`); failures are
  `MissingOutput` only when no usable JSON was produced.
* Tests: `tests/fake_binary.rs` (child-env regression via a re-executed test
  binary, so no other test's environment is mutated), `src/binary.rs` unit test
  for the helper, `src/lib.rs` message tests, and `tests/hierarchy.rs` for
  exit-1-with-JSON and the stderr-bearing missing-output message.

## Verification (observed)

All commands were run from the repository root. Raw logs are in `logs/`.

| Check | Command | Observed | Log |
|---|---|---|---|
| Qt6 build + ctest | `bash scripts/build-qt.sh` | `BUILD_QT_EXIT=0`; `100% tests passed, 0 tests failed out of 38` (37 pre-existing + 1 new regression; none removed/weakened) | `logs/44-build-qt-final2.log` |
| Rust tests (pinned CentOS 7 Rust image `vedit-build:local`) | `cargo test -p vd-deeplint` | 82 passed, 0 failed (43 unit + 4 + 15 + 12 + 6 + 2) | `logs/30-rust-test.log` |
| Rust fmt/clippy | `cargo fmt -p vd-deeplint -- --check`; `cargo clippy -p vd-deeplint --all-targets -- -D warnings` | `FMT_OK`, `CLIPPY_OK`, no warnings | `logs/31-rust-clippy-config.log` |
| Neighbour crate tests | `cargo test -p vd-config` | 77 passed, 0 failed | `logs/31-rust-clippy-config.log` |

### C — headless product-shaped A/B on glibc 2.17 (real failure class)

Run inside `vedit-build:local` (CentOS Linux 7.9.2009, glibc 2.17), with the
packaged AppDir:

* CLI, `LD_LIBRARY_PATH=$APPDIR/lib` exported:
  `/…/AppDir/bin/slang: /lib64/ld-linux-x86-64.so.2: version 'GLIBC_2.35' not found (required by /…/AppDir/lib/libc.so.6)`, `EXIT=1`.
* CLI, `env -u LD_LIBRARY_PATH`: `slang version 12.1.0+0`, `EXIT=0`.
  → `logs/11-cli-ab-centos7.log`
* Module-tree selftest through `$APPDIR/ld-linux-x86-64.so.2 --library-path …`
  with `LD_LIBRARY_PATH=$APPDIR/lib` exported:
  * **before** (pre-fix binary): `49 checks, 2 failure(s)`, `SELFTEST_EXIT=1`,
    exactly `FAIL real.service-answered — elaboration failed: slang failed: the
    lint tool exited with code 1 and produced no diagnostics` (and the packaged
    variant) — `logs/20-module-tree-before.log`;
  * **after** (final binary): `58 checks, 0 failure(s)`, `SELFTEST_EXIT=0`,
    including `ok real.service-answered`, `ok packaged.answered`,
    `real.instances — instances=3` — `logs/22-module-tree-and-i18n-final.log`.

### A/B — packaged layout and launcher

* ctest `vedit-app-i18n-selftest`: passed (in the 38/38 run) — covers the pure
  resolvers: `<dir>/bin/translations` found first, build-tree layout, prefix
  fallback, AppRun selected, args forwarded, `$APPIMAGE` wins, non-executable
  AppRun falls back, plain build tree uses `applicationFilePath()`.
* Same test binary run against the **real** AppDir through the AppDir loader:
  `0 failure(s)`, `PROBE_EXIT=0`, including
  `ok probe.translations` (real `<AppDir>/bin/translations`),
  `ok probe.restart-selects-apprun` (real `<AppDir>/AppRun`),
  `ok probe.restart-forwards-args` — `logs/51-i18n-appdir-probe.log`,
  repeated on the final binary in `logs/22-module-tree-and-i18n-final.log`.
* `packaging/runtime/AppRun` under `sh -x --selfcheck`:
  `+ export VEDIT_TRANSLATIONS_DIR=/…/AppDir/bin/translations`; with an explicit
  `VEDIT_TRANSLATIONS_DIR=/custom/translations` the export count is `0` (explicit
  value wins); product selfcheck `ok=1 checks=312 failures=0`,
  `APPRUN_EXIT=0` — `logs/52-apprun-translations-guard.log`.
* The AppDir used for the A/B was refreshed with the final product binary
  (`sha256 7c3a04a1…`) and the amended `AppRun` — `logs/50-appdir-refresh.log`.

## Not verified (honest limits)

* **GUI language switch**: no GUI session was used. The resolver, the packaged
  probe and AppRun's export are verified; the actual Preferences click, translator
  installation and on-screen Chinese strings were not observed.
* **Real AppImage relaunch**: `$APPIMAGE` selection is asserted by the resolver
  test only. No real AppImage was executed, so the runtime relaunch (mount a new
  image, old process exits) is not demonstrated.
* **AppImage mount lifetime**: the old process `_Exit`s immediately after
  `startDetached`; whether the FUSE mount outlives it for the successor was not
  tested. Under a real AppImage the selected program is the image, so the runtime
  re-mounts; under an extracted AppDir AppRun is selected.
* **Host with glibc ≥ 2.43 cannot reproduce C**: this workshop host runs glibc
  2.43, the same version as the packaged one, so `LD_LIBRARY_PATH=$APPDIR/lib
  slang --version` succeeds here. The failure was reproduced on the pinned
  CentOS 7 image (glibc 2.17), which is the delivery target and the real-machine
  class. The A/B “before” run is from the pre-fix binary in that image.
* **ctest count**: the baseline was 37; the new regression test makes it 38.
  No test was deleted, skipped or relaxed (`ctest -N` lists all ids, including
  the pre-existing ones and `Test #14: vedit-app-i18n-selftest`) —
  `logs/45-ctest-list.log`.

## Changed paths

* `src/app/i18n.cpp`, `src/app/i18n.h` (packaged candidates + restart resolver)
* `src/app/i18n_selftest.cpp` (new), `src/app/CMakeLists.txt` (register it)
* `packaging/runtime/AppRun`, `packaging/runtime/assemble-appdir.sh` (comment)
* `docs/USER_GUIDE.md`
* `crates/vd-deeplint/src/{binary,hierarchy,lib,service}.rs`
* `crates/vd-deeplint/tests/{fake_binary,hierarchy}.rs`
