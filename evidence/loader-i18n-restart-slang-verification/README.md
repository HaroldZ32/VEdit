# Independent verification — loader / i18n / restart / slang

Date: 2026-11-04 (Asia/Shanghai). Tree: HEAD `4e2d0a5`, dirty (pre-existing WIP preserved).
Verified against the repaired tree, NOT against the writer's narrative. The writer's report is
`evidence/loader-i18n-restart-slang/README.md` (read, not modified).

**Verdict: PASS.** All critical claims were reproduced independently: the CentOS 7 glibc-2.17
A/B, the module-tree selftest through the real AppDir loader with `LD_LIBRARY_PATH` exported
(58/58), the negative control (49 checks / 2 failures, both with the loader error now visible),
the i18n resolvers and real translation load, the restart resolver, the new ctest, and the
`build-qt` gate (EXIT=0, 38/38). **The tree is ready for a packaging rebuild**, subject to
refreshing the AppDir from the fresh build and re-running the package smoke; one non-blocking
fallback/diagnostic edge is reported below.

This directory is new verification evidence. No product code, test, or writer evidence was edited.
Re-running `scripts/build-qt.sh` refreshes the script's own `evidence/m0-build/` and
`evidence/ffi-abi/` logs, as every gate run does.

## Environment

- `vedit-build:local` — CentOS Linux 7.9.2009, glibc 2.17 (delivery target class).
- `vedit-qt6-build:local` — Ubuntu 26.04.1, glibc 2.43 (build image used by `build-qt`).
- AppDir: `packaging/runtime/out/AppDir`; AppRun launches through the bundled loader.
- AppDir product binary (writer's final refresh): sha256 `7c3a04a1…` (full hash in
  `repair-window-files.txt`). A fresh build produced `8672310b…`; the two files differ in only
  24 bytes (build metadata), and both contain the new `VEDIT-RESTART` and translation strings.
- The module-tree test binary is not shipped; it was run with `libQt6Test.so.6` from the host
  `/usr/lib/x86_64-linux-gnu` added to `--library-path` because the AppDir intentionally does not
  ship Qt6Test. The product paths themselves used only AppDir libraries.

## Evidence (commands and observed results)

All logs are in this directory.

| # | Check | Observed | Log |
|---|---|---|---|
| 1 | `bash scripts/build-qt.sh` | `BUILD_QT_EXIT=0`; `100% tests passed, 0 tests failed out of 38`; xcb/software-rendering smoke `M0-SMOKE ok=1` | `build-qt.log`, `ctest-N-raw.txt` |
| 2 | `ctest -N` inside `vedit-qt6-build:local` | 38 tests; `Test #14: vedit-app-i18n-selftest`; names identical to the writer's `45-ctest-list.log`; test #14 passes verbosely | `ctest-N-raw.txt`, `ctest-current-names.txt`, `ctest-i18n-verbose.log` |
| 3 | Rust tests, pinned CentOS 7 image | `cargo test -p vd-deeplint`: 43+4+15+12+6+2 = **82 passed, 0 failed**; `cargo test -p vd-config`: 56+14+2+4+1 = **77 passed, 0 failed**; `cargo fmt --check` and `cargo clippy -D warnings` exit 0 | `rust-tests.log` |
| 4 | CentOS 7 slang A/B | `LD_LIBRARY_PATH=$APPDIR/lib $APPDIR/bin/slang --version` → `version 'GLIBC_2.35' not found`, `A_EXIT=1`; `env -u LD_LIBRARY_PATH` → `slang version 12.1.0+0`, `B_EXIT=0` | `centos7-slang-ab.log` |
| 5 | Module-tree selftest through the real AppDir loader, `LD_LIBRARY_PATH=$APPDIR/lib` and `LOCPATH` exported, 2-file fixture | **58 checks, 0 failure(s)**, `SELFTEST_EXIT=0`, including `ok real.service-answered`, `ok packaged.answered`, `real.instances — instances=3`, `packaged.instances — instances=3` | `module-tree-after-real-appdir.log` |
| 6 | Positive child-env probe (scratch AppDir; wrapper dumps child env, then execs the real slang) | **58 checks, 0 failure(s)**; child dump `LD_LIBRARY_PATH=<unset>`, `LOCPATH=<unset>` | `scratch-env-positive.log`, `scratch-env-positive.out`, `child-env-positive.txt` |
| 7 | Negative control (wrapper re-adds `LD_LIBRARY_PATH` before exec; simulates the pre-fix child env) | **49 checks, 2 failures** — exactly `real.service-answered` and `packaged.answered`; the message now contains the loader line: `… exited with code 1 and produced no diagnostics: … version 'GLIBC_2.35' not found …` | `scratch-both-negative.log`, `scratch-both-negative.out` |
| 8 | i18n/restart selftest through the real AppDir loader, real AppDir as `argv[1]` | **20 checks, 0 failure(s)**, including `probe.translations`, `probe.restart-selects-apprun`, `probe.restart-forwards-args` | `i18n-selftest-real-appdir.log` |
| 9 | AppRun `--selfcheck` under `sh -x` | `APPRUN_EXIT=0`; trace exports `VEDIT_TRANSLATIONS_DIR=/…/AppDir/bin/translations`; product `APP-SHELL-SELFTEST ok=1 checks=312 failures=0`; with an explicit value the export count is `0` | `apprun-run.log`, `apprun-selfcheck.trace`, `apprun-selfcheck.stdout`, `apprun-selfcheck-explicit.trace` |
| 10 | Real AppRun with `ui.language = "zh_CN"` | `APPRUN_EXIT=0`; translations export present; **no** `interface language` diagnostic → the packaged `vedit_zh_CN.qm` loaded | `apprun-zh.log`, `apprun-zh.trace`, `apprun-zh.stdout` |
| 11 | Product with a packaged `bin/translations` containing the `.qm` | no translation diagnostic (translator loaded); `TOOLCHAIN source=verible:bundled slang:bundled missing=[]` | `zh-product.log`, `zh-product.out` |
| 12 | Product with `zh_CN` and no translations anywhere | diagnostic lists the three real candidate paths | `notrans-product.log`, `notrans-product.out` |
| 13 | AppDir loader semantics | `/proc/self/exe` under the AppDir loader is `AppDir/ld-linux-x86-64.so.2`; `applicationDirPath()` is therefore the AppDir root (the premise of both the i18n candidate and the AppRun restart selection) | `loader-appdirpath.log` |
| 14 | Changed-set / tree state | `git status` / `git diff --stat`; repair-window source edits are exactly the claimed paths plus AppDir build artifacts; version/Cargo/package pins untouched | `git-status.txt`, `git-diff-stat.txt`, `repair-window-files.txt` |
| 15 | Test-name baseline | Writer's prior 37-test evidence and the 38-test list: the captured pre-existing names (`#10–#37`) all survive; the only new name is `vedit-app-i18n-selftest`; `src/app/CMakeLists.txt` adds only that test registration | `ctest-baseline-names.txt`, `ctest-current-names.txt`, `git-diff-stat.txt` |

### What the source shows

- `src/app/i18n.cpp` `translationCandidates()` returns `<app>/bin/translations`,
  `<app>/translations`, `<app>/../share/vedit/translations`; `translationsDirectoryFor()` returns
  the first existing one; `translationsDirectory()` honours `VEDIT_TRANSLATIONS_DIR` first;
  `installTranslations()` reports the candidate list when the `.qm` is missing.
- `packaging/runtime/AppRun` exports `VEDIT_TRANSLATIONS_DIR` only when unset and the directory
  exists; explicit values win.
- `src/app/i18n.cpp` `restartCommand()` chooses `$APPIMAGE`, then an executable
  `<applicationDirPath>/AppRun`, then `applicationFilePath()`; it always forwards
  `arguments().mid(1)`. `restartApplication()` prints the `VEDIT-RESTART` line and exits on the
  success path. `src/app/i18n_selftest.cpp` covers all three selections, arg forwarding, the
  non-executable fallback and the three translation layouts.
- `crates/vd-deeplint/src/binary.rs` `sanitize_tool_child_env()` removes `LD_LIBRARY_PATH` and
  `LOCPATH`; it is called in `hierarchy::elaborate()` and `service::run_engine()` (covers slang
  and verible lint). `LintFailure::MissingOutput::user_message()` appends the first non-empty
  stderr line. `hierarchy::elaborate()` accepts a nonzero exit when the AST JSON exists and
  parses; `tests/hierarchy.rs` covers exit 1 with valid JSON and the stderr-bearing failure.
- The bundled `verible-verilog-*` tools are statically linked; `vd-lsp` spawning
  `verible-verilog-ls` therefore does not inherit the loader failure. `vcs_toolchain.cpp`
  already removes `LD_LIBRARY_PATH` from its git/svn version probes; the Rust VCS runner does the
  same.

## Findings (none blocks the packaged happy path)

### 1. Medium — a first-existing translations directory masks a valid later candidate, and the diagnostic overstates the search

- **What:** `translationsDirectoryFor()` in `src/app/i18n.cpp` selects the first **existing
  directory**, not the first directory that contains the requested `vedit_zh_CN.qm`. If
  `<app>/bin/translations` exists but is empty/stale while `<app>/translations/vedit_zh_CN.qm`
  exists, `installTranslations()` checks only the first directory, reports
  `no translations directory contains it (searched: <bin>, <translations>, <../share>)` and stays
  English. The message is false for the second candidate. `packaging/runtime/AppRun` compounds it
  by exporting the first existing directory when present.
- **Where:** `src/app/i18n.cpp` `translationsDirectoryFor()` / `installTranslations()`;
  `packaging/runtime/AppRun` translations export block.
- **Why it matters:** the user explicitly selects 简体中文, the `.qm` is present in a documented
  fallback location, and the UI remains English with an error that claims every candidate was
  checked. The normal packaged layout (`bin/translations` populated) works; this is a mixed/stale
  layout edge.
- **Evidence:** `mask-layout.txt` (candidate 1 is an empty `bin/translations`; candidate 2 is a
  symlink to the real `vedit_zh_CN.qm`), and `mask-product.log` / `mask-product.out` (the product
  still prints the "no translations directory contains it" diagnostic).
- **Direction of fix:** resolve by the required file, not by directory existence (continue to the
  next candidate when the file is absent), and list only the directories actually probed; or
  make the AppRun export conditional on the `.qm` being present. Not applied.

### 2. Nit — the formatter child is outside the sanitizer

- **What:** `formatVerilog()` in `src/workspace/diagnostics/toolchain.cpp` starts
  `verible-verilog-format` with a default `QProcess` environment, so the child inherits
  `LD_LIBRARY_PATH`/`LOCPATH`. The bundled formatter is statically linked, so no loader failure
  was observed; it is the same environment contract the deeplint fix enforces for slang/verible.
  `crates/vd-lsp` similarly spawns the static `verible-verilog-ls` without sanitizing.
- **Why it matters:** a future dynamic or user-supplied formatter/LSP would hit the same
  `GLIBC_2.35` abort on the packaged CentOS 7 target; a single sanitizer would prevent a
  regression. No current user-visible failure was reproduced.
- **Evidence:** `file packaging/runtime/out/AppDir/bin/verible-verilog-format` is statically
  linked; `scratch-env-positive` shows the deeplint path sanitized. No failing formatter run.
- **Direction of fix:** apply the same `QProcessEnvironment::remove("LD_LIBRARY_PATH"/"LOCPATH")`
  helper (already used by `src/workspace/vcs/vcs_toolchain.cpp`) to the formatter/LSP children,
  or document the static-tool exemption. Not applied.

### 3. Nit — the new ctest does not cover `installTranslations()` or the restart stderr line

- **What:** `src/app/i18n_selftest.cpp` exercises the pure resolvers (candidates, directory
  selection, restart selection, arg forwarding, `$APPIMAGE` precedence, non-executable fallback)
  but not `installTranslations()` with a real settings file/`.qm`, the missing-`.qm` diagnostic
  text, or the `VEDIT-RESTART` line from `restartApplication()`. The report's claims for those
  rest on code reading and manual probes (this verification exercised them manually).
- **Why it matters:** the mask finding above and the stderr line are exactly the behaviours a
  regression test would lock; the claim "ctest covers the three translation layouts" is true for
  the resolver, not the installer.
- **Evidence:** `ctest-i18n-verbose.log` (18 checks, no install/diagnostic/restart-line check);
  `mask-product.log`; `notrans-product.log`.
- **Direction of fix:** add a temp-app-dir test that writes `settings.toml`, copies a `.qm`, and
  asserts `translated`/`qmPath`/diagnostic; add a handler-seam or stderr-capture check for the
  restart line. Not applied.

### 4. Nit / clarification — ctest numbers are positional

- Adding `vedit-app-i18n-selftest` as `#14` renumbers the former `#14` (`vedit-native-fold-tests`)
  through `#37` by one. The captured pre-existing names (`#10–#37`) are all present; the nine
  earlier lanes (syntax/terminal) have no repair-window edit. No script in the tree pins a
  numeric ctest id. Task item "the 37 pre-existing ids still exist" is true by name, not by
  number. Direction: keep referring to test names, or append the new test at the end if numeric
  stability matters.

## Declined to judge

- Real AppImage relaunch, `$APPIMAGE` FUSE mount lifetime, and a real GUI Preferences click: not
  run; the writer's report explicitly leaves these unverified. The resolver and packaged
  translation load were tested instead.
- On-screen Chinese strings / layout: not inspected visually; the real AppRun + `zh_CN` run
  loaded the `.qm` without a diagnostic, which is the code-level contract.
- `zh_TW`/other Chinese locales mapping to the `zh_CN` catalogue: pre-existing language policy,
  outside these three fixes.
- Accepting a nonzero exit with parseable AST JSON, including a possible partial AST from a hard
  elaboration error: matches the documented crate rule and the frozen requirement; no
  user-visible partial-tree case was observed, so I did not call it a defect.
- `LOCPATH` inherited by the static verible formatter/LSP and by the VCS tool probes (the latter
  already removes `LD_LIBRARY_PATH`): no observed failure; separate from the slang loader defect.
- AppRun's `LIBPATH` appending a caller `LD_LIBRARY_PATH` (e.g. `/opt/gsv4/...` from the build
  image): the product itself is meant to use the AppDir libraries; the tool children are
  sanitized. Out of scope.
- Build reproducibility / the 24-byte difference between the writer's AppDir binary and a fresh
  build: looks like build metadata; behaviour and embedded fix strings match. A packaging rebuild
  will produce a new hash.
- Host glibc ≥ 2.43 cannot reproduce the GLIBC abort: expected; the delivery target is CentOS 7,
  where the A/B reproduced exactly.

## Bounds of this review

- The tree is dirty with ~199 changed files of pre-existing WIP. I read the claimed paths, their
  tests, the AppRun/packaging path and the call sites; I did not review the unrelated WIP.
- Only `vd-deeplint` and `vd-config` Rust tests were run directly (plus the crates `build-qt`
  already runs: vd-ffi, vd-search, vd-settings, vd-vcs). Not a full `cargo test --workspace`.
- No pre-fix product binary was available: the negative control re-adds `LD_LIBRARY_PATH` in a
  wrapper before exec'ing the real packaged slang. It reproduces the pre-fix failure counts
  (49/2) and names exactly, but it does not execute the pre-fix Rust code. The writer's before
  log was not trusted for the verdict.
- The module-tree selftest needed `libQt6Test.so.6`, which the AppDir does not ship; the CentOS 7
  container mounted `/usr/lib/x86_64-linux-gnu` read-only as `/hostlib` and added it to
  `--library-path`. This does not change the child-slang environment under test.
- A real GUI session, a real AppImage mount, and an on-screen Chinese screenshot were not run.
- The writer's `evidence/loader-i18n-restart-slang/` was read only. The build gate refreshed its
  own `evidence/m0-build/` and `evidence/ffi-abi/` logs, as the script does on every run.

## Readiness for a packaging rebuild

**Yes.** The source builds (`BUILD_QT_EXIT=0`), the full ctest suite is 38/38, the Rust tests and
lints are green, and the packaged-AppDir paths were exercised through the real loader on CentOS 7:
the slang child no longer inherits the packaged loader environment, the module-tree test is
58/58, and the real AppRun loads the packaged `zh_CN` translation. Reassemble the AppDir from the
fresh `target/qt6-build` so the package carries the new binary, and retain the manual AppImage /
GUI language-switch check that the writer already lists as unverified. The Medium finding above
does not affect the correctly populated packaged layout; it should be fixed before relying on a
fallback translations directory.
