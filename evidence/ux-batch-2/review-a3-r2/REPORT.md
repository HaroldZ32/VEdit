# A3 re-review after repair — VHDL / Markdown / Tcl syntax highlighting (UX batch 2)

Reviewer lane: independent verifier for A3. Findings only; no source, test or gate was edited.
Frozen contract: `.mpd/plans/ux-batch-2/00-plan.md` §3 A3 (including the rev3/rev4 rulings) and §1/§2.
Previous verdict: `evidence/ux-batch-2/review-a3/REPORT.md`.
Repair evidence: `evidence/ux-batch-2/a3-repair/` and the regenerated
`evidence/ux-batch-2/a3/logs/09-appdir-checks-full.log`,
`evidence/ux-batch-2/a3/logs/10-final-green.log`, `00-LOG-INDEX.md`.

**Risk level: LOW.**
**Verdict: PASS.**

## Assessment

The repair fixes the two blockers and the High finding at the level the contract requires, and the
fixes survive independent reproduction rather than resting on the repairer's narrative. The AppRun
guard now canonicalises both the whitelist and the probe's visible names with `LC_ALL=C sort`, and I
reproduced `KSH-GUARD PASS` under `LC_ALL=C`, `zh_CN.UTF-8` and `en_US.UTF-8`; a deliberately wrong
whitelist (one extra name, and one missing name) still produced `KSH-GUARD FAIL` and exit 2 on
`zh_CN.UTF-8`. The F2 gate now prints and compares each winning `Definition::filePath()`: I reran the
current script against `target/a3-appdir-repair/AppDir` (11 pass / 0 fail) and reran a one-line
mutation that exposes the shadow fixture to the positive probe (9 pass / 2 fail, exit 1, with
`Tcl=/shadow/...`). The regenerated AppDir evidence is from the final source: the header sha256s and
source mtimes match the current binaries and files, and an independent run of the AppDir assertion
printed `checks=104 failures=0` with real `definition-parsed.*` paths and no `PASS no definition named`
line. The remaining items are nits in the evidence documents, the bounded Medium 5 attribution proof (the
crash is gone and the other lane's fix is documented, but no backtrace or A3-reverted run was
produced), and two explicitly bounded follow-ups (the pre-existing
`src/syntax/product/CMakeLists.txt` test weakness and the unexercised `embargo_misfire` branch);
none of them contradicts an A3 acceptance item.

## Original findings — disposition

| # | Original finding | Disposition | Evidence checked / observed |
|---|---|---|---|
| Blocker 1 | AppRun `ksh_guard` was host-locale dependent | **FIXED** | `packaging/runtime/AppRun` `canonical_names()` and `ksh_guard()`; `packaging/runtime/selfcheck.sh` `ksh_guard_case()`; `a3-repair/02-r1-after.txt`, `03-r1-guard-fails-closed.txt`. Independently reproduced PASS under C/zh/en and FAIL exit 2 for an extra-name and a missing-name whitelist under zh. |
| Blocker 2 | The reworked non-shadowing check could never fail | **FIXED** | `packaging/runtime/verify-f2-isolation.sh` `check_positive()`, `winner_offenders()`, `check_shadow_control()`; `cmake/smoke/ksh_env_probe.cpp` `sortedValidDefinitions()` / `printLoaded()`; `a3-repair/11-f2-isolation.log`, `12-r2-gate-fails-on-shadow.log`. Independently reran the gate (11/0) and the mutated copy (9/2, exit 1). |
| High 3 | Delivered AppDir evidence predates the final selftest source | **FIXED** | `a3/logs/09-appdir-checks-full.log` and `10-final-green.log` headers; `a3-repair/04-r3-before-stale-logs.txt`, `08-assemble-appdir.log`; `a3/logs/00-LOG-INDEX.md`. Header binary sha256s/mtimes match the current files; independent AppDir run 104/0; no impossible `PASS no definition named ...` line. |
| Medium 4 | F2 user-level isolation had no negative control | **FIXED** | `verify-f2-isolation.sh` `run_probe()` (mounts the parent of `$USER`) and `check_reverse_user()`; `11-f2-isolation.log` `appdir-reverse-user`: `defs=6`, `UserLeakProbe=/root/.local/share/...`. Reproduced in my independent F2 run. |
| Medium 5 | `vedit-app-syntax-startup` SEGFAULT attribution unproven (and the test weaker than claimed) | **PARTIALLY FIXED** | The crash no longer reproduces: `10-final-green.log` ctest 6/6 includes `vedit-app-syntax-startup`; `10-selfcheck-appdir.log` has `APP-SHELL-SELFTEST ok=1 checks=312 failures=0`; `evidence/ux-batch-2/a2/README.md` §6 documents the dangling `editor_` pointer fix. The original attribution proof (backtrace/core, or a run with A3 reverted) was not produced, and the app test still receives `VEDIT_SYNTAX_DIR=/resources` from the deferred property, so its green does not load A3's XMLs. That residual is captain decision (a), not an A3 acceptance item. |
| Nit 6 | `a3/README.md` §0 said "ctest 全绿" | **FIXED** | `evidence/ux-batch-2/a3/README.md` §0 now gives 75% / 5-of-6 as historical and 6/6 as current; the false one-line conclusion is gone. |
| Nit 7 | Wrong cross-reference for upstream sha256s | **FIXED** | `resources/syntax/README.md` now points to `THIRD_PARTY_NOTICES.md` §9.1.1; §9.4 is correctly left as scope/review. |
| Nit 8 | VHDL upstream-licence premise said "unlicensed" | **FIXED** | `THIRD_PARTY_NOTICES.md` §9.1.1, `resources/syntax/README.md`, `a3/README.md` §2.2 now state no in-file licence statement but Debian records `data/syntax/vhdl.xml` in the MIT block, keep the shipped files self-authored LGPL-2.1-or-later, and keep the "adapted → upstream licence" clause (`tcl` BSD, `markdown` GPL/BSD, `vhdl` MIT). `01-provenance-license.txt` confirms the Debian MIT block and upstream README MIT. |
| Nit 9 | Two extension tables could drift | **FIXED** | `src/editor/kernel/highlight_mode_selftest.cpp` `runExtensionDriftChecks()` iterates the 15 contract suffixes and compares `highlight_mode.cpp`'s table with `KsyntaxData::languageNameForExtension`; `a3-repair/06-r4d-extension-drift.log` mutates `ksyntax_data.cpp` (`mdown` removed), fails `drift.extension.mdown`, then restores and passes. The mutation targeted product code, not a test list. |
| Nit 10 | VHDL labelled process did not fold | **FIXED** | `resources/syntax/vedit-vhdl.xml` now has a labelled opener rule; `a3-repair/05-r4e-vhdl-folding.log` shows the pre-repair regex failing `begins=0 ends=1` and the repaired regex passing both labelled and unlabelled cases; the independent 104/0 AppDir run includes `construct.vhdl-fold-labelled-process PASS begins=1 ends=1`. |
| Nit 11 | New `embargo_misfire` branch not exercised | **NOT FIXED (accepted bound)** | `a3/README.md` §4 item 3 and `a3-repair/README.md` §R4(f) now state the conclusion rests only on the positive all-files predicate and that a negative control would require keeping a banned KDE file. The branch is still unexercised; the contract's positive requirement ("the three new files are not falsely rejected") is met by `data.no-forbidden-kde-definitions` / `forbidden-none` and the five-file scan. Legitimate bound, not a blocker. |

**Blocker 2 detail, independently checked.** `cmake/smoke/ksh_env_probe.cpp` `sortedValidDefinitions()`
now carries `Definition::filePath()` and `printLoaded()` emits `KSH-ENV def name="..." file="..."`;
`verify-f2-isolation.sh` `probe_pairs()` parses that line and `winner_offenders()` compares every
winning path with `APPDIR_SYNTAX=/app/usr/share/org.kde.syntax-highlighting/syntax` (inside the
container). On the repaired AppDir I observed all five winners under that directory; when I exposed
`/shadow` to the positive probe, the two winner-path checks failed with
`Tcl=/shadow/org.kde.syntax-highlighting/syntax/Tcl.xml` while the name/count checks still passed —
exactly the old check's blind spot, now observable.

**Test/gate integrity.** I compared the repaired tests and gates with the previous verdict's
descriptions and the contract. The F2 gate's old vacuous duplicate-name check was replaced by the
stronger winner-path assertions, not merely deleted; `selfcheck.sh` gained the C and non-C locale
guard cases and reads the product output from a file instead of a `pipefail`-sensitive `grep -q`
pipeline; `assemble-appdir.sh` gained the five-file `REQUIRED_SYNTAX` gate; `AppRun` still fails closed
on a name-set mismatch; `ksyntax_data.cpp` still bans the same two KDE hashes and now also fails the
report on an embargo misfire; the highlight-mode selftest grew from 86 to 104 checks and no existing
check was removed or converted into a `fact`. The drift-guard mutation in `06-r4d-extension-drift.log`
edits `src/syntax/kde/ksyntax_data.cpp` (product code) and the folding mutation in
`05-r4e-vhdl-folding.log` edits `resources/syntax/vedit-vhdl.xml` (product data), then restores; both
are real mutations, not test-list edits. No A3 test or gate was deleted or weakened by this repair.

## New findings from the repair

### Nit 12 — `01-provenance-license.txt` still lists the pre-repair VHDL sha256 in its body

- **Where:** `evidence/ux-batch-2/a3/01-provenance-license.txt` (the shipped-definitions block) versus
  the repair note appended to the same file and `THIRD_PARTY_NOTICES.md` §9.1.1.
- **What:** the body records `2664feaf...` for `resources/syntax/vedit-vhdl.xml`; the current file is
  `47a4539e...`. The repair note explains the change and gives the current hash, and the licence table
  in `THIRD_PARTY_NOTICES.md` is correct; but a reader who stops at the body sees a hash that no
  longer matches the shipped file.
- **Why it matters:** this is an acceptance provenance artefact; a stale hash in the body weakens
  reproducibility even though the correction is present.
- **Direction:** regenerate the provenance output from the final file, or put a "pre-repair history;
  current hash below" banner at the top of the body rather than only at the end.

### Nit 13 — `a3/README.md` §4.0 still cites `logs/09` as the old app-test run

- **Where:** `evidence/ux-batch-2/a3/README.md` §4.0 versus `evidence/ux-batch-2/a3/logs/09-appdir-checks-full.log`
  and `00-LOG-INDEX.md`.
- **What:** §4.0 says the same app test "在 logs/09（17:48）还是 Passed", but `logs/09` has been
  replaced by the current AppDir assertion (03:00), and the old log is only described in
  `a3-repair/04-r3-before-stale-logs.txt` / the log index.
- **Why it matters:** a historical claim now points at a different artefact; the acceptance README
  should not send a reader to the wrong log.
- **Direction:** cite `logs/00-LOG-INDEX.md` / `a3-repair/04-r3-before-stale-logs.txt` for the
  historical run, or say "the superseded pre-repair log".

### Nit 14 — `a3/README.md` §5 reproduce command has a path typo

- **Where:** `evidence/ux-batch-2/a3/README.md` §5, the AppDir reproduce block.
- **What:** it creates `target/a3-appdir/libonly` but symlinks into `target/a3-appdir-repair/libonly`;
  a fresh reader reproduces a different directory and the `ln -sf` can fail. The actual log 09 uses
  the correct `target/a3-appdir-repair/libonly`.
- **Why it matters:** the reproduce recipe is part of the acceptance evidence; a typo makes the
  advertised re-run unreliable.
- **Direction:** make the `mkdir` and the symlink path identical (`target/a3-appdir-repair/libonly`).

### Nit 15 — `src/syntax/kde/README.md` still describes only the two T16 definitions

- **Where:** `src/syntax/kde/README.md` ("ships our own LGPL Verilog / SystemVerilog definitions";
  the data table lists two files).
- **What:** A3 added VHDL / Markdown / Tcl; the loader README is stale. The file is outside A3's
  declared write scope, so this is a follow-up, not a repair defect.
- **Why it matters:** the lane documentation no longer matches `KsyntaxData`'s five-file private
  directory and the AppDir payload.
- **Direction:** assign a small docs follow-up to extend the table and the `VEDIT_SYNTAX_DIR` wording.

### Nit 16 — `selfcheck.sh` still asserts only two of the five shipped XMLs

- **Where:** `packaging/runtime/selfcheck.sh`, the `SYNTAX_LINE` check and its "both XMLs" message.
- **What:** it greps for `vedit-verilog.xml` and `vedit-systemverilog.xml` in `own-xml`; the three A3
  XMLs are covered by `assemble-appdir.sh` `REQUIRED_SYNTAX`, the AppDir 104-check run and the F2
  gate, but not by this selfcheck line.
- **Why it matters:** a regression that removed one of the three A3 XMLs would be caught elsewhere,
  but the packaging selfcheck's own "our syntax definitions loaded" line would still pass.
- **Direction:** extend the `SYNTAX_LINE` assertion to all five `REQUIRED_SYNTAX` names (or derive it
  from the same list).

## Captain decision (a) — `src/syntax/product/CMakeLists.txt`

**Separate follow-up, not a blocker for A3 acceptance.**

The defect is real and I reproduced it: `src/syntax/product/CMakeLists.txt` is unmodified from
`HEAD` (`git diff HEAD -- src/syntax/product/CMakeLists.txt` is empty; it was introduced by commit
`9ea326c`), and the deferred `vedit_syntax_link_product()` evaluates `CMAKE_CURRENT_SOURCE_DIR` at
the top-level call site, so `target/qt6-build/CTestTestfile.cmake` contains
`VEDIT_SYNTAX_DIR=/vedit-build/../../../resources` (= `/resources`). The `vedit-app-syntax-startup`
test's `PASS_REGULAR_EXPRESSION` only requires `syntax-product: grammar=verilog`, so the test passes
without loading the AppDir/workspace definitions.

It is not an A3 acceptance blocker because:
- the A3 acceptance is the `vedit-highlight-mode-selftest` ctest plus the packaged AppDir + F2-library
  loader run, both of which independently pass (104/0, real `definition-parsed.*` paths);
- the defective property is in `src/syntax/product/**`, outside A3's declared write scope, and it
  predates A3 (it was already in `HEAD`);
- the app test is green for the wrong reason, but that is a test-quality defect in a pre-existing
  test, not a regression introduced by A3.

Follow-up direction: set the property from a directory-scope value (e.g. `${CMAKE_SOURCE_DIR}/resources`
or capture `${CMAKE_CURRENT_SOURCE_DIR}` before the `DEFER`) and strengthen the regex to require
`syntax_data_ok` and the five definition names, with a negative control that fails when the directory
is wrong.

## Captain decision (b) — the repairer's "not proven" list

**None of the listed items contradicts an acceptance item in §3 A3; all are legitimate bounds.**

| Repairer's "not proven" item | Does it contradict §3 A3? | Why |
|---|---|---|
| No GUI/xcb check; folding asserted through the hidden highlighter | No | §3 A3 requires the AppDir offscreen assertion; §2 explicitly allows offscreen in a GUI-less CI and leaves real-screen appearance to user acceptance. A2, not A3, is the lane with the xcb screenshot item. |
| Shadow control only via higher-version same-name; equal-version tie-break and `syntax-bundled/` not separately exercised | No | The gate's positive assertion is mechanism-independent: it prints the winning `Definition::filePath()` for every loaded definition and requires it under `/app/usr/share/org.kde.syntax-highlighting/syntax`. The `12` control proves the assertion can fail on a real shadow; `verify-not-shipped.sh` separately bans `syntax-bundled/`. Additional shadow vectors are extra test coverage, not an acceptance gap. |
| `embargo_misfire` has no negative control | No | The contract requires the three new files not to be falsely rejected; the positive all-files predicate (`isForbiddenDefinitionFile()==false`, `forbidden-none`) covers that. A negative control would require keeping a banned KDE file in the tree, which the licence rules forbid; the limitation is now disclosed next to the claim. |
| Drift guard covers only the 15 contract suffixes | No | The guard compares both tables for all 15 suffixes the contract names; a future suffix added to both tables and not to the test list is a maintenance risk, not a contradiction of the A3 acceptance. |
| F2 AppImage branch unexercised | No | A3's acceptance is the AppDir; the AppImage is the final §5.1 packaging step, owned by the captain. The AppDir path is exercised end to end (11/0) and the AppImage branch is a final-packaging follow-up. |
| Other lanes' uncommitted work part of the rebuilt tree | No | Correct disclosure; the rebuilt-tree green runs and the separate A2 dangling-pointer evidence support the cross-lane attribution, but I did not audit the other lanes. |

## Declined to judge (reason on the same line)

- **Real GUI appearance / colours / real gutter clicks** — not judged; no Xvfb/xcb session was run and
  A3's acceptance is offscreen.
- **Upstream KDE files over the network** — not fetched; the `raw/` originals' sha256s and in-file
  licence attributes match the records and the Debian copyright excerpt.
- **Full final 7-gate / AppImage rebuild** — not run; that is the captain's §5.1 integration step, not
  A3 lane acceptance.
- **`vedit-app-syntax-startup` test weakness** — confirmed real but assigned to captain decision (a);
  not an A3 acceptance item.
- **`embargo_misfire` branch unexercised** — accepted bound; the positive predicate is present and the
  contract does not require a negative control.
- **`src/syntax/kde/README.md` stale two-definition description** — outside A3's write scope; docs
  follow-up, not a repair defect.
- **`01-provenance-license.txt` body hash** — noted as Nit 12; the current hash is in the repair note
  and the licence table, so the claim is not unsupported.
- **`a3/README.md` 0.96 s vs `10-final-green.log` 0.95 s** — rounding-level difference in a
  documentation sentence; not a result claim.
- **Scope authorisation for `cmake/smoke/ksh_env_probe.cpp` and `docs/handover/04-hard-facts.md`** —
  the A3 plan's write-scope list does not name them; the repair README says it stayed within its
  allowed scope and the previous review had asked for a captain-approved expansion for the probe.
  I could not find the approval record on disk, so this is a process note for the captain, not a
  functional finding: the probe change is necessary for Blocker 2's fix and is exercised by the F2
  gate.
- **Other lanes' dirty tree** — not reviewed; the tree is still moving and unrelated modifications are
  attributed to those lanes.

## Bounds of the review

- **Read:** the frozen contract (§3 A3, §1, §2), the previous A3 verdict, the repair README and all
  files in `evidence/ux-batch-2/a3-repair/`, the regenerated A3 logs and log index, the A3 README
  and provenance file, `packaging/runtime/AppRun`, `assemble-appdir.sh`, `selfcheck.sh`,
  `verify-f2-isolation.sh`, `verify-not-shipped.sh` outputs, `cmake/smoke/ksh_env_probe.cpp`,
  `src/editor/kernel/highlight_mode.cpp`, `highlight_mode_selftest.cpp`, `src/editor/CMakeLists.txt`,
  `src/syntax/kde/ksyntax_data.cpp`, `src/syntax/product/CMakeLists.txt`, the generated
  `target/qt6-build/CTestTestfile.cmake`, `THIRD_PARTY_NOTICES.md`, `resources/syntax/README.md`,
  the three shipped XMLs and the three upstream `raw/` originals.
- **Ran:** an independent AppDir assertion with the current
  `vedit-highlight-mode-selftest` and the AppDir's own `libKF6SyntaxHighlighting.so.6`
  (`checks=104 failures=0`, real paths); the six-test ctest set in the pinned
  `vedit-qt6-build:local` image (`6/6`, `CTEST_EXIT=0`); the F2 gate against the repaired AppDir
  (`pass=11 fail=0`); a one-line mutated copy of that gate exposing `/shadow` to the positive probe
  (`pass=9 fail=2`, exit 1); the AppRun guard under `LC_ALL=C`, `zh_CN.UTF-8`, `en_US.UTF-8` and with
  an extra-name and a missing-name whitelist (PASS / PASS / PASS; FAIL exit 2; FAIL exit 2);
  `sha256sum`/`stat` comparisons of the logs' header values against the current source and binaries.
- **Did not run:** a GUI/xcb session; the full seven-gate release suite; a fresh source rebuild (I
  used the rebuilt tree and existing binaries whose hashes match the log headers); the AppImage
  branch of the F2 gate; no upstream network fetch; I did not edit source, tests or gates.

## Verdict

**PASS** — both blockers and the High finding are genuinely fixed and independently reproducible
(AppRun passes under C/zh/en and still fails closed on a wrong whitelist; the F2 gate passes 11/0 and
its shadow mutation fails 9/2; the AppDir/F2 logs reproduce from the final source at 104/0 and ctest
6/6), while the remaining items are nits in evidence documents and two explicitly bounded follow-ups
(the pre-existing `src/syntax/product/CMakeLists.txt` app-test weakness and the unexercised
`embargo_misfire` branch), neither of which contradicts an A3 acceptance item.
