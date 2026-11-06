# Independent verification — ux repair F1–F4 (ux-repair-verification)

- Repository: `/home/haroldzhao/MyProj/DshProj/VEdit`
- HEAD: `4e2d0a57ca50db9568db5a5b040cc66f04c9a05f` (worktree dirty; all changes
  are uncommitted, as in the two writers' evidence)
- Date: 2026-11-04 (Asia/Shanghai)
- Inputs: `evidence/ux-fixes-verification/REPORT.md` (predecessor FAIL),
  `evidence/ux-repair-2/REPORT.md` (repair) and their logs; the current source,
  tests, docs and packaging scripts. Neither writer's evidence was modified.

## Verdict

**PASS — risk Low.** All four predecessor findings are repaired and independently
reproduced: the packaged AppRun preserves a caller's `VEDIT_SLANG` (and verible
variables) and only names a tool that exists and is executable; the palette file
index follows `ApplicationShell::projectRootChanged`; the path-display guide is
scoped to persistent views with the one-shot status exception; and a no-op
`.f` re-import reports “Already imported: nothing changed”. The full gate is
green (EXIT=0, ctest 37/37, xcb smoke ok=1), every focused suite passes, and the
check labels only grew (project 150→153, palette integration 28→31, all other
fixed sets identical). No regression was observed. The remaining items are NITs,
not blockers; the source tree is ready for a **Qt6 AppDir/AppImage packaging
rebuild** (the existing `packaging/runtime/out/` artifacts predate the repair
and must be regenerated), after the untracked probe file is included in the
commit.

## Finding closure

| Finding | Repair | Independent evidence | Status |
|---|---|---|---|
| F1 — AppRun overwrites caller `VEDIT_SLANG` | guarded export + 9-check probe + selfcheck wiring | probe 9/0; RED control 4 failures; manual `/caller/slang` preserved; default bundled path still exported | **closed** |
| F2 — palette index ignores runtime root changes | `projectRootChanged` subscription with the integration as context | root follows signal, rescan of the new root, old files gone (31/0 integration suite) | **closed** |
| F3 — guide overclaimed the display rule | guide scoped to persistent views + documented one-shot status exception; header comment aligned | guide and `path_display.h` match the `displayPath()` call sites; status messages named by the exception; docs gate PASS | **closed** |
| F4 — no-op re-import still said “Imported” | defaulted `addedFilelists` out-param + honest panel status | 153/0 project suite; `Imported 1 filelist` on the real import, `Already imported: nothing changed` on the no-op; guide corrected | **closed** |

## What I ran, and what I observed

All product commands ran in the pinned image `vedit-qt6-build:local` (the build
also runs its Rust stage in `vedit-build:local`). Exact output is in the logs
named below.

| # | Command | Observed | Log |
|---|---|---|---|
| 1 | `git rev-parse HEAD`; `git status --porcelain=v1 -uall`; mtime audit; `git diff --check` scoped/unscoped | HEAD `4e2d0a5`; product-source files newer than 17:30 are exactly the 11 files the repair claims; frozen-file mtimes are all pre-task; repair-scoped `git diff --check` EXIT=0 (whole-tree EXIT=2 on six pre-existing unrelated files, NIT-1) | `logs/00-inventory.txt` |
| 2 | `bash scripts/build-qt.sh` | **EXIT=0**; Rust stage + Qt build + ctest `100% tests passed, 0 tests failed out of 37`; `M0-SMOKE ok=1 platform=xcb …` | `logs/10-build-qt.log` |
| 3 | focused `ctest -R` for app-shell, palette, palette-integration, project, search, module-tree (`-V`) | `100% tests passed, 0 tests failed out of 6`; `APP-SHELL-SELFTEST ok=1 checks=312 failures=0`; `PALETTE-SELFTEST 46/0`; `INTEGRATION-SELFTEST 31/0`; `PROJECT-MODEL-SELFTEST 153/0`; `WORKSPACE-SEARCH-SELFTEST 106/0 skipped=2`; module-tree `61 checks, 0 failure(s)`; no `CHECK FAIL` | `logs/20-focused-ctest.log` |
| 4 | `sh packaging/runtime/probe-apprun-env.sh` | `APPRUN-ENV-PROBE ok=1 checks=9 failures=0`; all nine checks PASS, including `override.slang-preserved` | `logs/30-apprun-probe.log` |
| 5 | RED control `sh evidence/ux-repair-2/scratch/red-apprun-probe.sh` (pre-repair AppRun reconstructed) | `ok=0 checks=9 failures=4`, failing exactly `override.slang-preserved`, `absent.slang-not-exported`, `absent.slang-caller-preserved`, `non-executable.slang-not-exported`; RED script EXIT=0 | `logs/30-apprun-probe.log` |
| 6 | independent fake-AppDir probe (my own, not the repair's script) | caller `VEDIT_SLANG=/caller/slang` → child keeps `/caller/slang`; caller unset + executable bundled file → child gets `<tmp>/bin/slang` | `logs/31-apprun-manual-probe.txt` |
| 7 | `sh -n` AppRun/probe; `bash -n` selfcheck | all EXIT=0 | `logs/30-apprun-probe.log` |
| 8 | `bash build/gates/docs.sh` | `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`; EXIT=0 | `logs/32-docs-and-whitespace.log` |
| 9 | independent label-set comparison (predecessor logs → this run; repair logs → this run) | app 312, palette 46, search 112 distinct (106/2 skipped), module-tree 61: no old label lost. Project 150→153 with only the 3 new F4 labels; palette integration 28→31 with only the 3 new F2 labels. Every label in the repair logs appears in this run. | `logs/05-label-compare.txt`, `logs/07-preservation-audit.txt` |
| 10 | packaging-prerequisite/readiness audit | all checked preflight artifacts present; `selfcheck.sh` invokes the probe and exits nonzero on failure; `build.sh` fails on selfcheck rc; `assemble-appdir.sh` installs the source AppRun verbatim; existing `out/` artifacts are stale and must be rebuilt | `logs/40-packaging-and-overclaim-audit.txt` |

## Findings

### NIT-1 — the repair's “`git diff --check` clean” claim is only true scoped

- **What.** `evidence/ux-repair-2/logs/00-inventory.txt` records `git diff
  --check EXIT=0` under the heading “whitespace check on repair files”, and the
  report reads as an unqualified clean-tree claim. Whole-tree
  `git diff --check` currently exits 2 on six unrelated files:
  `THIRD_PARTY_NOTICES.md`, `evidence/slice3-pack/10-assemble.log`,
  `src/editor/kernel/fim_wiring.cpp`, `src/editor/kernel/find_replace.cpp`,
  `src/editor/kernel/multi_cursor.cpp` and
  `src/workspace/vcs/vcs_panel.cpp`. All six predate the repair (mtimes
  2026-11-02..11-04 14:03; none is in the repair-touched set).
- **Why it matters.** It is an evidence-scope overclaim, not a product defect;
  nobody should read “clean” as covering the pre-existing dirty tree.
- **Direction.** Say “repair-scoped `git diff --check`” in the report (or show
  the six pre-existing hits and explicitly exempt them). Do not touch the
  unrelated files as part of this repair.

### NIT-2 — the legacy tarball launcher still has the same override class (out of scope)

- **What.** `packaging/tarball/launcher/vedit.sh` still does
  `export VEDIT_SLANG="$HERE/bin/slang"` unconditionally. The tarball is the
  legacy Rust/GTK3 product (`packaging/tarball/stage.sh` ships
  `bin/vd-app`), and `packaging/runtime/build.sh` does not build it. F1's stated
  scope was `packaging/runtime/AppRun`.
- **Why it matters.** A user of the legacy `.run`/tar.gz can still have a caller
  `VEDIT_SLANG` overwritten and a dead bundled path exported — the same masking
  class F1 fixed for the AppImage. It is **not** a blocker for the Qt6
  AppImage/AppDir rebuild; it **is** user-visible if the legacy tarball is part
  of the same release.
- **Direction.** Keep it as a separate follow-up. If the release still ships
  the tarball, apply the same `[ -z "${VEDIT_SLANG:-}" ] && [ -x ... ]` guard
  (and test it) before shipping; otherwise record it as a known limitation of
  the legacy artifact.

### NIT-3 — release-process notes for the rebuild (not code defects)

- The existing `packaging/runtime/out/AppDir` and
  `packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage` are from 14:22, before the
  repair; the AppRun there has no guarded toolchain block. They must not be
  shipped; a rebuild is required.
- `packaging/runtime/probe-apprun-env.sh` is a new untracked file (not ignored).
  The current worktree rebuild can see it, but the eventual commit must include
  it or `selfcheck.sh` fails on a clean checkout. This is a staging note, not a
  reason to change the repair.

## Declined to judge (weighed and set aside)

- **Empty `VEDIT_SLANG=`:** treated as unset by `[ -z "${VEDIT_SLANG:-}" ]`, so
  the bundled path is exported. The existing verible guard uses the same
  convention; an empty value is not a documented path override. Not counted.
- **Palette already open during a root switch:** the index root and next-scan
  behavior are asserted; the open overlay is not re-queried. Declared limit in
  the repair report; no user-visible failure demonstrated. Not counted.
- **F4 mixed new/old selection:** the panel reports the newly-added count and
  the model re-expands the whole list; no mixed-selection test exists, but the
  logic is a simple size difference and no user-visible lie was found. Not
  counted.
- **Other path-like tooltips (diagnostics `entry.file`, VCS repo-relative
  change paths):** outside the guide's enumerated path views and outside F3's
  status-message trigger; no persistent absolute-path repro was run. Not
  counted as a repair regression.
- **The 25 `check(` lines removed in the HEAD diff of
  `project_selftest.cpp`:** these removals are outside the repair boundary (the
  predecessor's 150-label project set does not contain those labels), and no old
  label disappeared between the predecessor's set and this run; the three new F4
  labels are the only additions. Not counted against the repair.
- **Pre-existing dirty-tree facts** (`git status` has 185 modified and 13
  deleted paths; the deletions are the pre-existing T29 folding set): not
  caused by this repair; frozen-file mtimes confirm the repair did not touch
  them.
- **Repair inventory timestamp of `docs/USER_GUIDE.md`:** the inventory
  records the guide's earlier mtime (17:48:43), while the final edit is
  17:52:38; I re-read the final guide and re-ran the docs gate, and both agree
  with the claims. Not counted.

## Ready for a packaging rebuild?

**Yes for the Qt6 AppDir/AppImage source tree, subject to NIT-3.** The full
product gate is green, the packaging preflight inputs I checked are present, the
source AppRun is what `assemble-appdir.sh` installs, and `selfcheck.sh` now runs
the 9-check AppRun contract and is gated by `build.sh`. Rebuild the `out/`
artifacts before shipping them, include the new probe file in the commit, and
treat the legacy tarball launcher (NIT-2) as a separate artifact/follow-up. No
full AppDir/AppImage packaging run, CentOS 7 probe or GUI `.f` open was run in
this verification.

**Declared limits.** None of the repair's declared limits is a user-visible
blocker for F1–F4: the palette root/rescan asserts the user-visible symptom and
only the already-open overlay is uncovered; the F4 dialog is only the trigger,
and the panel/model status path is asserted; the F1 probe exercises the real
source AppRun, which the packaging script installs verbatim; and no frozen file
or old check label was lost.

## Bounds of the review

- **Read:** the predecessor and repair reports and their logs, the four repaired
  source/test/doc areas and their callers (`src/app`, palette, project, search,
  filetree, `src/common/path_display.h`, `docs/USER_GUIDE.md`,
  `packaging/runtime/{AppRun,probe-apprun-env.sh,selfcheck.sh,assemble-appdir.sh,build.sh}`,
  `packaging/tarball/stage.sh` and launcher), plus the check-label logs used for
  comparison. Did not read the entire ~40k-line pre-existing dirty diff, every
  earlier evidence batch, or the full Rust workspace.
- **Ran:** the full `build-qt` gate, six focused ctest suites, the repair's
  AppRun probe and RED control, an independent fake-AppDir caller-override
  probe, shell syntax checks, the docs gate, label-set comparison, mtime/frozen
  audits and scoped/whole-tree whitespace checks. Running the gate refreshed its
  own `build/evidence/**`, `evidence/ffi-abi/logs/**` and
  `evidence/m0-build/build-logs/**` logs (gate behavior); no
  `evidence/ux-repair-2/**` or `evidence/ux-fixes-verification/**` file was
  written, and the regenerated `crates/vd-ffi/include/vd_ffi.h` is byte-identical
  to the recorded copy. Did not run: a full AppDir/AppImage packaging build, the
  CentOS 7 clean-room probe, a literal GUI `.f` open, a GUI F4 import dialog, or
  a dynamic leak/sanitizer run (the F2 connection is parented to the
  integration; leak safety is by inspection).

## Evidence index

- `logs/00-inventory.txt` — HEAD, status counts, frozen/repair-scope mtimes,
  changed-file set, deleted paths, whole/scoped `git diff --check`, untracked
  probe check, cbindgen comparison.
- `logs/05-label-compare.txt` — predecessor/repair/this-run label-set
  comparison; `scratch/label-compare.sh` reproduces it.
- `logs/06-source-and-docs-audit.txt` — F2 connection and index reset, F4
  model/panel/refresh path, F3 guide/header and `displayPath()` call sites.
- `logs/07-preservation-audit.txt` — test-label preservation, old checks still
  present, no repair-touched test deletion.
- `logs/10-build-qt.log` — full gate (Rust + Qt + ctest 37/37 + xcb smoke).
- `logs/20-focused-ctest.log` — six focused suites with check-level output.
- `logs/30-apprun-probe.log` — GREEN 9/0, RED 4 failures, shell syntax.
- `logs/31-apprun-manual-probe.txt` — independent caller-override probe.
- `logs/32-docs-and-whitespace.log` — docs gate and diff-check scope.
- `logs/40-packaging-and-overclaim-audit.txt` — preflight inputs, selfcheck
  wiring, stale `out/` artifacts, legacy tarball launcher, declared limits.
