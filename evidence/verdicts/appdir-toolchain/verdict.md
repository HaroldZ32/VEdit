# Verdict — T87: review of T84 (VEDIT_SYNTAX_DIR) + T85 (toolchain UX/discovery)

captured: 2026-11-01T02:37Z
git_head: 9a6b828f468d85a13a035fe839aa59c4b2a77f44
branch: feat/qt6-ktexteditor-migration
reviewer: Reviewer (instance 2), session b0af789c-16ef-42ef-ad05-9e137a41d604
review contract: **none frozen** — T87 was hand-created; no `contracts/` entry and no
`.mpd/verify/loops/` loop exists for it (nor for T84/T85). So no `mpd_verify_seat` /
`mpd_verify_record` were possible; this verdict is **ADVISORY**, written to
`evidence/verdicts/**` as the dispatch body requires.
Sources of truth: T84/T85 task records (`.mpd/team/contracts/T84.json`,
`T85.json`), `AGENT.md` §Runtime toolchain discovery, `docs/BUILD.md` §10.1,
the T84/T85 evidence packages, and the commits `0ed4915`, `2e12136`, `fd78844`.

## Verdict: PASS

Both deliverables hold: the syntax data root is sealed on both sides (packaging
value + product-side normalisation) without touching the whitelist/embargo
semantics, and the toolchain-missing UX is one concise line with a working
four-layer discovery chain. Two non-blocking evidence/doc inaccuracies are
listed as findings; the compliance red line was **not** touched.

## Object A — T84 (VEDIT_SYNTAX_DIR)

### A1. Root cause: reproduced with both values on the rebuilt tarball

Extracted the delivered tarball (`build/centos7-verify/dist/vedit-centos7-verify-c682694.tar.gz`,
sha256 `acc58ee03206526342ef59edc1b4b23470c068bb6dbdd1aec634629ce85d9a0e`,
sidecar `.sha256` agrees) and ran the AppDir binary via its bundled loader inside
the CentOS 7 image with only `VEDIT_SYNTAX_DIR` differing:

| value | product line |
| --- | --- |
| `<AppDir>/usr/share/org.kde.syntax-highlighting` (data root) | `private-dir='…/org.kde.syntax-highlighting' definitions=3 own-xml=[vedit-systemverilog.xml,vedit-verilog.xml] verilog=1 systemverilog=1 embargoed=[]` |
| `<AppDir>/usr/share/org.kde.syntax-highlighting/syntax` (subdir) | `private-dir='…/org.kde.syntax-highlighting'` (normalised), `definitions=3 own-xml=[…both…] verilog=1 systemverilog=1 embargoed=[]` |

The pre-fix behaviour (subdir → `own-xml=[] verilog=0`) is recorded in
`evidence/appdir-syntax-dir/01-two-values.log` and matches the product contract
(`addSearchPath` requires `<root>/syntax`). **Conclusion: the data root is the
correct value, and the rebuilt product tolerates both values** — both sides are
sealed (packaging value in the repo, tolerance in the product).

### A2. Both sides changed

- Repo `packaging/runtime/AppRun:33` exports the data root; commit `2e12136`
  changes exactly that one line (`+1/-1`).
- `src/syntax/kde/ksyntax_data.cpp` (`addSearchPath`) normalises a path whose
  basename is `syntax` and whose `<path>/syntax` is missing to its parent;
  `src/syntax/kde/ksyntax_data.h` documents it (commit `0ed4915`, 2 files only).
- The delivered tarball really carries the corrected values: extracted
  `AppDir/AppRun` line 33 and `verify-centos7.sh` lines 80/311 all export the
  data root; `MANIFEST.sha256`'s AppRun entry matches the extracted file
  (`070dbfe0…`).

### A3. Compliance red line — NOT touched (line-by-line)

`src/syntax/kde/ksyntax_data.cpp:104-150` after the fix:

1. `dataRoot` starts as the configured path; the only rewrite is
   `basename(path) == "syntax"` **and** `<path>/syntax` missing → `dirname(path)`.
   No other directory is introduced.
2. The function returns `QString()` unless `<dataRoot>/syntax` exists, and only
   then calls `repository_->addCustomSearchPath(dataRoot)` — the repository sees
   exactly `<effective root>/syntax`.
3. `report_.private_dir` is set to that same effective root, and the whitelist /
   embargo scan iterates `report_.private_dir + "/syntax"` — i.e. **the same
   effective root the repository uses**, never a broader or different tree.
   On the failure path `report_.private_dir` keeps its initial value
   (`= config_.private_dir`, line 87), so the scan scope is unchanged there too.
4. The forbidden-KDE sha256 list, `isForbiddenDefinitionFile`, the diagnostic
   text, and `report_.ok = report_.verilog_available && forbidden_definitions.isEmpty()`
   are unchanged; the tolerance cannot pull a KDE GPLv3+ file into scope because
   it never adds a directory beyond the configured root and only scans the
   root's own `syntax/` (exactly what the repository loaded).
5. Runtime: both values end with `embargoed=[]` and `own-xml=[…]`.

Result: **whitelist/embargo semantics are neither widened nor narrowed.**

### A4. Tarball self-consistency and on-machine verification

Ran the delivered script from the extracted tarball inside **stock
`docker.m.daocloud.io/library/centos:7`**:

```
VERIFY-EXIT=1
RESULT FAIL kernel.is-3.10 | … | actual=7.0.0-38-generic | note=…container/VM shares a newer host kernel…
RESULT SKIP highlight.mode | …headless…
RESULT SKIP lint.product | exit 1 | VER-DIAGNOSTICS-FFI-SELFTEST checks=1 failures=1 | …needs FFI fixture dir…
RESULT PASS highlight.stack | … | definitions=3 own-xml=[vedit-systemverilog.xml,vedit-verilog.xml]
RESULT PASS highlight.verilog-definition | verilog=1 and own-xml lists the shipped definition | actual=verilog=1 own-xml=[…both…] definitions=3
SUMMARY: PASS=15 FAIL=1 SKIP=2
```

Only the expected `kernel.is-3.10` FAIL remains; the two SKIPs and their reasons
match the report. `sha256` of the tarball and the internal manifest agree.

## Object B — T85 (toolchain missing UX + discovery)

### B5. Missing state: one concise line, no probe wall

Product run (binary copied outside the checkout so no local artifacts, PATH
without the tools, `--project` + `--hold`, offscreen):

```
EXIT=0
"Diagnostics unavailable" occurrences: 1          (single line, 459 bytes incl. newline)
probe-wall ("probed") occurrences, default run: 0
longest line bytes: 458
TOOLCHAIN source=verible:missing slang:missing missing=[verible, slang] local=[]
```

with `VEDIT_TOOLCHAIN_TRACE=1`: probe-wall occurrences = **1** (the candidate
list is opt-in, as required). Panel-level counts are asserted headlessly by
`vedit-toolchain-ux-selftest` (42 CHECK lines, 0 FAIL): `panel.status-single-line`,
`panel.outline-zero-rows 0`, `panel.empty-state-visible/single-line`,
`panel.no-fabricated-diagnostics 0`, `panel.no-error-entries`,
`repeat.still-single-line`, `repeat.no-extra-entries 0`,
`repeat.outline-still-empty`, `shell.diagnostics-single-entry 1`,
`shell.diagnostics-concise 446`, `shell.diagnostics-no-probe-wall`.
So diagnostics = 1 status line, outline = 0 symbol rows + 1 empty-state line,
single line, not repeated.

### B6. Discovery order `bundled > local > PATH > env` — real, in that order

Code (`toolchain.cpp:21-100`) probes 1) bundled, 2) local checkout, 3) PATH,
4) env (`VEDIT_*` / `_DIR`), in that order. Runtime on the delivered binary:

| scenario | observed TOOLCHAIN line |
| --- | --- |
| packaged AppDir (bundled tools) | `source=verible:bundled slang:bundled missing=[]` |
| bare build in the checkout, cwd `/tmp` (binary inside repo) | `source=verible:local slang:local missing=[] local=[/vedit-build/build/slang/out,/.mpd/recon/verible/bin]` |
| binary copied outside the checkout, no tools | `source=verible:missing slang:missing missing=[verible, slang] local=[]` |
| outside + dummy tools on PATH | `source=verible:PATH slang:PATH missing=[]` |
| outside + PATH dummies + `VEDIT_*` env | `source=verible:PATH slang:PATH` (PATH wins — env is last) |
| outside + env only | `source=verible:env:VEDIT_VERIBLE_LS slang:env:VEDIT_SLANG` |

The local marker is stable from a different cwd: the walk starts at the product
binary's directory and looks for `scripts/fetch-verible.sh` (up to 10 levels);
with the binary inside the checkout but cwd `/tmp` the local artifacts are still
found; with the binary copied outside they are not (correct boundary).

### B7. No fabrication; real symbols when tools exist

Missing: `panel.no-fabricated-diagnostics PASS 0`, `panel.no-error-entries PASS`,
0 outline rows; no per-file diagnostics were invented.
Available (local verible + slang found): `available.real-session-ready-not-failed`,
`available.real-symbols-non-empty PASS 5`, `available.real-panel-has-symbols PASS 5`
— real outline symbols from a real Verible session through the C ABI, not
fabricated. Real non-zero diagnostics on a deliberately broken file were not
demonstrated by this lane's selftest (clean fixture → `diagnostics 0`); see F3.

## Regression

- Root ctest in the pinned image: **`100% tests passed, 0 tests failed out of 18`**,
  `CTEST-EXIT=0` (baseline 18/18 met; includes the new
  `vedit-toolchain-ux-selftest`).
- `bash build/gates/typecheck.sh`: `TYPECHECK-EXIT=0`,
  `[typecheck] PASS (pinned image vedit-build:local)` (Rust clippy + Qt6
  `-Werror` halves).
- No test/gate weakening: commits `2e12136` (AppRun ±1 line), `0ed4915`
  (ksyntax_data), `fd78844` (diagnostics lane files only; the new ctest is
  additional coverage) contain **no deletion of an existing assertion**; the
  `verify-centos7.sh` criterion in the before/after reports is the same
  `verilog=1 and own-xml lists the shipped definition` (before FAIL → after
  PASS), i.e. the check was not relaxed. The gate log writes of my typecheck run
  (`build/evidence/typecheck/*`, operational regenerated logs) were restored to
  HEAD afterwards.

## Findings

| id | severity | symptom | expected | source |
| --- | --- | --- | --- | --- |
| **F1** | LOW (evidence accuracy) | T84's report/evidence table says the product-side tolerance was verified "on the real AppDir … `01-two-values.log` second half; the rebuilt binary was used", but that log's header says the AppDir was the pre-fix T83 tree and its second half shows the subdir value producing `own-xml=[] verilog=0` — it records the before-failure, not the tolerance. | The tolerance claim is nevertheless true (independently verified on the rebuilt tarball: both values → `own-xml=[…] verilog=1`); correct the citation or add a two-value log produced with the rebuilt binary. | `evidence/appdir-syntax-dir/01-two-values.log:4-5,19-20`; `08-report.md` §2/§6; my run in this verdict |
| **F2** | LOW (stale comment) | `toolchain.cpp:19` still says "delivery order bundled > PATH > env" (local missing), while the header (`toolchain.h:5-14`), AGENT.md §Runtime toolchain discovery and BUILD.md §10.1 all say `bundled > local > PATH > env` and the code does that. | Update the one-line comment to the four-layer order. | `src/workspace/diagnostics/toolchain.cpp:19` |
| **F3** | INFO (bound, not a defect) | The available-scenario real session constructs `createFfiSlangBackend()` **without** the resolved local slang path, so its `lastError()` detail reads "slang: slang binary not found (tried 4 location(s); set VEDIT_SLANG)" while the toolchain report says `slang:local`; the product integration does pass `toolchain.slang.path`. Real **symbols** are proven (5); real non-zero **diagnostics** on a broken file are not demonstrated by this selftest. | No action for T84/T85 acceptance; if desired, add a product-level selftest with a deliberately broken file and pass the resolved slang path to the session. | `toolchain_ux_selftest.cpp:287-321`; `diagnostics_integration.cpp` (passes `toolchain.slang.path`) |
| **F4** | INFO (process) | The required typecheck self-run rewrote the gate's own operational logs under `build/evidence/typecheck/`; I restored them to HEAD after recording the result. | None. | — |

## Not verified / bounds (UNVERIFIABLE-IN-CONTAINER where applicable)

- **GUI appearance** (how the single line wraps/renders, the empty outline table
  visually): no display server — the text/count assertions are the headless
  equivalent; visual confirmation belongs to the user's on-display acceptance.
- **Exact stock CentOS 7 kernel check** cannot pass inside a container
  (`kernel.is-3.10` FAIL is by design and accepted); I did run the tarball's
  `verify-centos7.sh` in the stock centos:7 image and reproduced `PASS=15 FAIL=1 SKIP=2`.
- The writer's "before" **binary** was not re-obtained (the old tarball with
  sha256 `6ecf90a2…` is not in `dist/`); the before-state is verified from the
  recorded before-run/report and the commit diffs, while the after-state was
  independently reproduced by me.
- Multi-platform / real user toolchain (user-installed verible on a real PATH)
  is covered by the PATH/env probes above, not by a host matrix run.
