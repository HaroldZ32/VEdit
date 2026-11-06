# Repair report — Reviewer findings F1–F4 (ux-repair-2)

- Repository: `/home/haroldzhao/MyProj/DshProj/VEdit`
- HEAD: `4e2d0a57ca50db9568db5a5b040cc66f04c9a05f` (worktree was already dirty; no
  other writer's change was reverted)
- Date: 2026-11-04 (Asia/Shanghai)
- Input: `evidence/ux-fixes-verification/REPORT.md` (FAIL, request changes),
  logs `41-apprun-override-probe.log` and `42-display-and-docs-audit.txt`.
- Scope: repair exactly F1–F4. No frozen file was modified (`CMakeLists.txt`,
  `Cargo.*`, `rust-toolchain.toml`, `package.json`, `scripts/build-qt.sh`,
  `build/gates/**` all keep their pre-task mtimes — `logs/00-inventory.txt`).
  No test was deleted or weakened; `displayPath` tests, the `fileOpened` signal
  checks and the slang-resolver checks all still pass (`logs/07-preserved-checks.txt`).

## Verdict

All four findings are repaired in the current worktree. The gate is green
(`EXIT=0`, ctest **37/37**, xcb smoke `ok=1`), every touched focused suite passes,
the new AppRun probe passes against the fixed script and fails against a
reconstructed pre-repair AppRun, and the new F2/F4 checks pass with their labels
present.

---

## 1. What changed

### F1 (High) — AppRun no longer replaces a caller's tool environment

`packaging/runtime/AppRun:35-40` — the unconditional `export VEDIT_SLANG` is now
the same guard the Verible block already used:

```sh
if [ -z "${VEDIT_SLANG:-}" ] && [ -x "$APPDIR/bin/slang" ]; then
    export VEDIT_SLANG="$APPDIR/bin/slang"
fi
```

The rationale comment was kept and extended: the Qt module-tree lane reads
`VEDIT_SLANG` first (`moduleTreeSlangBinary()`), so an unconditional or dead
value would mask the diagnostics-resolved toolchain and the documented env
override. Net effect: caller value wins; no export when the bundled file is
missing or non-executable; the bundled path is still named when it exists.

New regression test `packaging/runtime/probe-apprun-env.sh` (no product build
needed): it builds fake AppDirs whose "loader" prints the environment AppRun
would hand the product, runs the **real** AppRun from the fake AppDir root, and
asserts 9 checks:

| check | expectation |
|---|---|
| `apprun.syntax` | `sh -n AppRun` |
| `default.slang-bundled` | unset caller + executable file → bundled path |
| `default.verible-bundled` | same for `VEDIT_VERIBLE_LS` |
| `override.slang-preserved` | caller `VEDIT_SLANG=/caller/slang` survives |
| `override.verible-preserved` | caller `VEDIT_VERIBLE_LS` survives |
| `absent.slang-not-exported` | no file → variable stays **unset** |
| `absent.verible-not-exported` | same for Verible |
| `absent.slang-caller-preserved` | no file + caller value → caller value survives |
| `non-executable.slang-not-exported` | present but non-executable → no export |

It is wired into the runtime selftest at `packaging/runtime/selfcheck.sh:58-66`
(`CHECK PASS/FAIL  AppRun preserves caller tool overrides`), so a future runtime
self-check runs it; it can also be run directly.

### F2 (Medium) — the palette file index follows `projectRootChanged`

`src/workspace/palette/palette_integration.cpp:39-45` — `PaletteIntegration::install()`
now binds `ApplicationShell::projectRootChanged` with the integration as the
connection context (same pattern as `search_integration.cpp`; leak-safe: the
integration is a child of the shell and the context drops the connection with
it), calling `setProjectRoot()` → `WorkspaceFileIndex::setRoot()`. `setRoot()`
clears the old snapshot and resets the scan counter, so the next `Ctrl+P` walks
the new root. `File → Open Folder` and the `.f` import both end in
`ApplicationShell::setProjectRoot()`, so this covers both.

New checks in `src/workspace/palette/palette_integration_selftest.cpp:278-298`:

- `integration.root-follows-projectRootChanged` — index root == new shell root;
- `integration.root-rescan-new-root` — next palette open indexes only the new root;
- `integration.root-old-files-gone` — the old root's file is no longer listed.

### F3 (Nit) — chose "narrow the guide", so code and doc now agree

The task allowed either applying `displayPath()` to the transient messages or
narrowing the guide. I chose the guide route deliberately: the status message is
asserted by an existing test (`search_selftest.cpp:739-740`, "the pre-repair
evidence asserted this status-bar text") and the task requires existing tests to
stay intact. The code behaviour is unchanged; the documentation now states the
exception explicitly.

- `docs/USER_GUIDE.md:129-141` — the rule is now scoped to the enumerated
  **persistent** path views (tab tooltips/title/tree tooltips/project
  lists/palette/search rows), and a new paragraph names the exception: one-shot
  status-bar / panel confirmations and error notes (`Opened …` after palette /
  Files tree / search jump, the Project panel's `Removed …` and error notes)
  intentionally keep the absolute path (≈5 s, not a path view). Everything
  outside the enumerated views and those one-shot notes follows the rule.
- `src/common/path_display.h:9-14` — the header comment now says "every
  persistent surface" and names the status-bar / panel exception, so the shared
  rule's own documentation matches the guide and the code.

No runtime behaviour changed for F3; the remaining absolute-path status messages
are listed in `logs/06-doc-code-agreement.txt` and are exactly the documented
exception.

### F4 (Nit) — the "Imported" message is honest on a no-op re-import

- `src/workspace/project/project_model.h:50-57` /
  `project_model.cpp:128-162` — `importFilelists()` gained an optional
  `int *addedFilelists` out-parameter (defaulted, so every existing call site is
  unchanged). It reports how many entries were **newly** added; `0` is the
  already-imported no-op. The return value and the early return are unchanged.
- `src/workspace/project/project_panel.cpp:215-231` — on `added == 0` the panel
  now says `Already imported: nothing changed (use Refresh to re-read an edited
  filelist)`; otherwise it reports `Imported N filelist(s)` using the **newly
  added** count (honest for a mixed selection too).
- `docs/USER_GUIDE.md:145-149` — the `.f` section now says: edit the filelist,
  then press the Project panel's **Refresh** to re-expand; re-opening the same
  filelist is idempotent and the Project panel status line says
  `Already imported: nothing changed` (the exact UI prefix).
- New checks in `src/workspace/project/project_selftest.cpp:472-477`:
  `filelist.import-status`, `filelist.reimport-panel-noop`,
  `filelist.reimport-status-honest`.

---

## 2. Verification (commands and observed results)

All product commands ran in the pinned image (`vedit-qt6-build:local`); the full
gate also ran the Rust stage in `vedit-build:local`.

| # | Command | Observed | Log |
|---|---|---|---|
| 1 | `bash scripts/build-qt.sh` | **EXIT=0**; ctest `100% tests passed, 0 tests failed out of 37`; `M0-SMOKE ok=1 platform=xcb …` | `logs/10-build-qt.log` |
| 2 | focused ctest (`vedit-app-shell`, both palette suites, project, search, module-tree) | `100% tests passed, 0 tests failed out of 6`; `ctest EXIT=0` | `logs/20-focused-ctest.log` |
| 3 | `vedit-app --selfcheck --state-dir …` | `APP-SHELL-SELFTEST ok=1 checks=312 failures=0` (same 312 as the Reviewer's log); all `path.display.*` pass | `logs/21-app-selfcheck.log` |
| 4 | `vedit-workspace-project-selftest --selfcheck tests/fixtures/verilog-project` | `PROJECT-MODEL-SELFTEST ok=1 checks=153 failures=0`; new F4 checks PASS; all `display.*` checks PASS | `logs/22-project-selftest.log` |
| 5 | `vedit-workspace-palette-selftest` | `PALETTE-SELFTEST ok=1 checks=46 failures=0`; `index.outside-root-absolute` / `index.inside-root-relative` PASS | `logs/23-palette-selftest.log` |
| 6 | `vedit-workspace-palette-integration-selftest` | `INTEGRATION-SELFTEST ok=1 checks=31 failures=0`; the 3 new `integration.root-*` checks PASS | `logs/24-palette-integration-selftest.log` |
| 7 | `vedit-workspace-search-selftest --selfcheck` | `WORKSPACE-SEARCH-SELFTEST ok=1 checks=106 failures=0 skipped=2` (identical to the Reviewer's count) | `logs/25-search-selftest.log` |
| 8 | `vedit-module-tree-selftest probes/slang-spike/fixture build/slang/out/slang …` | `61 checks, 0 failure(s)`; `toolchain.env-override-wins`, `packaged.appdir-slang`, `packaged.answered` PASS | `logs/26-module-tree-selftest.log` |
| 9 | `sh packaging/runtime/probe-apprun-env.sh` | `APPRUN-ENV-PROBE ok=1 checks=9 failures=0`, EXIT=0; `sh -n` on AppRun/probe and `bash -n` on selfcheck.sh all EXIT=0 | `logs/30-apprun-probe-green.log` |
| 10 | RED control: same probe against a reconstructed pre-repair AppRun (unconditional export) | `ok=0 checks=9 failures=4`; fails exactly `override.slang-preserved`, `absent.slang-not-exported`, `absent.slang-caller-preserved`, `non-executable.slang-not-exported` | `logs/31-apprun-probe-red.log` |
| 11 | `bash build/gates/docs.sh` | `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`, EXIT=0 | `logs/32-docs-gate.log` |

Check-name preservation: app 312 (same), palette 46 (same), search 106/2 skipped
(same), module-tree 61 (same). Project grew 150→153 and palette integration 28→31 (the new
F2/F4 checks); no existing label disappeared.
`logs/05-check-labels.txt` and `logs/07-preserved-checks.txt` record the new and
preserved assertions.

Every acceptance claim in this report is re-checked mechanically against the
logs and the final source by `scratch/check-claims.sh`: all 27 claims PASS,
`CLAIM-CHECK fail=0` (`logs/08-claim-check.txt`).

Constraint spot-checks from the same logs: `open.signal.opens`,
`open.signal.one-per-open`, `open.signal.failed-open-silent` PASS;
`toolchain.env-override-wins`, `toolchain.override-executable`,
`packaged.appdir-slang` PASS; `path.display.open-key-still-absolute`,
`display.files.open-key-absolute` PASS.

---

## 3. What I could not prove / declared limits

- **No real AppDir/AppImage run.** The F1 probe uses a fake AppDir and the real
  `AppRun`; `packaging/runtime/selfcheck.sh` now calls it, but that selftest was
  not exercised against a freshly built prefix/AppImage in this repair (a
  packaging build is outside the requested `build-qt` scope). The probe is
  independent of the product binary, so the shell contract itself is proven.
- **F2 is tested through `ApplicationShell::setProjectRoot()`,** the same signal
  the `.f` flow emits; I did not drive a literal `.f` open through a GUI in this
  repair (the project selftest already proves that path sets the root). A palette
  that is *already open* during a root switch was not re-queried (the index root
  and re-scan are asserted; the open-overlay refresh is not).
- **F3 is a documentation fix by choice.** The displayPath alternative was
  rejected because it would require changing the existing
  `jump.product.first-status` assertion, which the task says must stay intact.
  The guide now documents the status-bar exception, and the header comment was
  aligned; no runtime behaviour changed.
- **F4 is asserted at the panel/status level, not through a real dialog.** The
  "Refresh re-reads an edited filelist" behaviour is the existing
  `ProjectModel::refresh()`/`startExpansion()` path; this repair asserts the
  honest message, not a full GUI edit-then-refresh cycle.
- **RED was rebuilt only for F1** (the probe control in log 31). The F2/F4 new
  checks pass on the fixed tree; their pre-fix failure is inferred from the
  reviewer's code audit and the old code path, not from a rebuilt pre-fix binary.
- **Out-of-scope observation (not changed):** the legacy tarball launcher
  `packaging/tarball/launcher/vedit.sh:70` still does
  `export VEDIT_SLANG="$HERE/bin/slang"` unconditionally. That is the same
  masking class as F1, but F1's stated scope is `packaging/runtime/AppRun`; it
  should be a follow-up finding, not absorbed into this repair.

---

## 4. Evidence index

- `logs/00-inventory.txt` — HEAD, frozen-file mtimes, repair-touched mtimes,
  worktree status count, `git diff --check` (EXIT=0).
- `logs/05-check-labels.txt` — new checks + preserved regression labels.
- `logs/06-doc-code-agreement.txt` — guide sections, header comment, the
  documented absolute status messages, the F4 UI string.
- `logs/07-preserved-checks.txt` — `fileOpened`, slang-resolver and display checks.
- `logs/10-build-qt.log` — full gate (`EXIT=0`, ctest 37/37, xcb smoke).
- `logs/20-focused-ctest.log` … `logs/26-module-tree-selftest.log` — focused
  suites, command + observed summary.
- `logs/30-apprun-probe-green.log` — GREEN probe + shell syntax checks.
- `logs/31-apprun-probe-red.log` — RED control (pre-repair AppRun).
- `logs/32-docs-gate.log` — docs gate.
- `logs/08-claim-check.txt` — 27 report claims re-checked against logs + source.
- `scratch/red-apprun-probe.sh` — reproducible RED-control harness.
- `scratch/check-claims.sh` — reproducible claim checker.

Reproduce the key checks:

```sh
sh packaging/runtime/probe-apprun-env.sh
sh evidence/ux-repair-2/scratch/red-apprun-probe.sh
sh evidence/ux-repair-2/scratch/check-claims.sh
bash scripts/build-qt.sh
docker run --rm -v "$PWD":/vedit-build -w /vedit-build \
  -e HOME=/tmp -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 \
  vedit-qt6-build:local bash -lc \
  'export QT_QPA_PLATFORM=offscreen; ctest --test-dir target/qt6-build \
   -R "vedit-app-shell-selftest|vedit-workspace-palette|vedit-workspace-project-selftest|vedit-workspace-search-selftest|vedit-module-tree-selftest" -V'
```
