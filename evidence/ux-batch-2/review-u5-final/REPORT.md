# U5 review (independent) — packaging selfcheck covers all five syntax definitions

Reviewer: independent verifier. READ-ONLY for the reviewed work: no product source, test,
packaging script or gate was edited by this review. Temporary experiments lived under
`target/review-u5/` (gitignored build output); the only file in the source tree with today's
mtime is the lane's own `packaging/runtime/selfcheck.sh`.

Contract: `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U5 — the runtime selfcheck
greps only two of the five shipped syntax XMLs, so a package that lost VHDL/Markdown/Tcl still
passed; extend the coverage to all five and keep it fail-closed, with a negative control on a
copy of the AppDir. `packaging/runtime/AppRun` must not be touched.

Evidence reviewed: `evidence/ux-batch-2/u5-final/README.md`, `00-appdir-provenance.log`, the
lane's three runs (`01-all-five-present-passes.log`, `02-missing-vhdl-fails.log`,
`03-old-two-xml-check-would-pass.log`), the diff of `packaging/runtime/selfcheck.sh`, and
`packaging/runtime/assemble-appdir.sh` (`REQUIRED_SYNTAX`).

New evidence produced by this review (all under `evidence/ux-batch-2/review-u5-final/`):

| File | Content |
|---|---|
| `01-positive-copy.log` | my own run on a COPY of the lane's AppDir: `SELFCHECK PASS`, exit 0 (all checks PASS, incl. five-XML check, both ksh_guard locales, bundled toolchain) |
| `02-negative-missing-tcl.log` | I removed **vedit-tcl.xml** (a newly covered definition; the lane removed VHDL) from my own copy and aligned the whitelist: product really starts (`app_exit=0`, `APP-SHELL-SELFTEST ok=1`), `CHECK FAIL own XML definitions missing: vedit-tcl.xml …`, exit 1 |
| `03-positive-after-negative.log` | the unmodified copy passes again after the negative: exit 0 |
| `04-absent-appdir.log` | `--appdir` pointing at a nonexistent dir: `FAIL: no AppRun …`, exit 1 (no skip) |
| `05-empty-syntax-dir.log` | copy with every XML removed and the whitelist removed (guard intentionally skipped): product starts with `own-xml=[]`, the check names all five as missing, exit 1 (no skip) |
| `06-scope-and-apprun.txt` | five definitions vs `resources/syntax` vs packaged whitelist, per-file sha256, AppRun mtime/sha256/git status, the only file modified today, `bash -n` |
| `07-old-check-on-negative.log` | the SAME negative AppDir run with a copy of the script whose syntax block is reverted to the old two-XML form: `CHECK PASS … SELFCHECK PASS`, exit 0 — the defect the lane reported is reproduced |

## Assessment

**Risk level: LOW. Verdict: PASS.** The widening is genuine and fail-closed: the check now
requires all five shipped file names in the loader's `own-xml=` field, a missing definition is
named in the failure line, and the failure is attributable only to that check (my negative run
had the product fully starting and `APP-SHELL-SELFTEST ok=1`; every other check passed). The
old script on the same damaged AppDir is green, so the new check has teeth. `AppRun` is
untouched (mtime 2026-11-05 02:43:48, sha256 `925ab1500e…406c3`; the only file modified under
`packaging/runtime/` today is `selfcheck.sh`). Two Nits below; neither blocks the verdict.

## Findings

### U5-F1 — Nit: the `verilog=1` term is satisfied by the `systemverilog=1` substring

- What: after the five-file loop, the branch is `elif printf '%s' "$SYNTAX_LINE" | grep -q
  'verilog=1' && … grep -q 'systemverilog=1'`. The first pattern has no token boundary, so it
  matches inside `systemverilog=1`; a package where only SystemVerilog's name lookup worked
  would still satisfy "verilog=1". (The five-file requirement independently requires
  `vedit-verilog.xml` to be listed, so this is not a coverage hole — only the name-lookup half
  of the old assertion is not independent. The old block had the same pattern.)
- Where: `packaging/runtime/selfcheck.sh`, the syntax block (`SYNTAX_DEFINITIONS` / `OWN_XML` /
  the `elif` branch).
- Why it matters: the message "all five XMLs loaded but the Verilog/SystemVerilog name lookup
  failed" cannot be reached for a Verilog-only failure; a real `verilog=0 systemverilog=1`
  package would still take the PASS branch.
- Direction: use a token-anchored test, e.g. `grep -qE '(^|[[:space:]])verilog=1([[:space:]]|$)'`
  (or require the exact adjacent pair `verilog=1 systemverilog=1`), as the U4 regex effectively
  does.

### U5-F2 — Nit: "definitions loaded" is "five XML files listed by the loader"

- What: `own-xml=` comes from `KsyntaxData::load`'s `private_definition_files`, which is
  appended for every non-embargoed `*.xml` in the private `syntax/` directory *before* it is
  resolved as a definition. So a present-but-corrupt packaged XML still satisfies this
  selfcheck even though the definition would not load. The check's PASS message says "our
  syntax definitions loaded (all five XMLs)".
- Where: `packaging/runtime/selfcheck.sh` (syntax block) and the product's
  `KsyntaxData::load` / `describe` `own-xml=` field.
- Why it matters: the U5 criterion is *coverage* of the five shipped files, and that is met;
  this is the same root cause reported as U4-F1 (the startup test's "five definition names"
  substitution). Recording it here so the two reviews agree and the wording is not read as a
  stronger guarantee than it is.
- Direction: either state "five shipped XML files present" in the message/README, or (root
  fix, outside U5's scope) have the loader report resolved definition names and assert those.

## What I verified myself (and its observed result)

1. **Coverage matches what ships.** `resources/syntax/` has the five XMLs; the AppDir
   `target/u5-appdir/usr/share/org.kde.syntax-highlighting/syntax/` has the same five, and all
   five files are byte-identical to `resources/syntax/` (sha256 per file in
   `06-scope-and-apprun.txt`); the packaged `usr/share/vedit/ksh-whitelist.txt` is
   `Markdown,SystemVerilog,Tcl,VHDL,Verilog` (five names, code-point order).
2. **Positive:** my copy passes (`01-positive-copy.log`, exit 0) with `CHECK PASS our syntax
   definitions loaded (all five XMLs, order-insensitive)`.
3. **Fail-closed, independently:** removing `vedit-tcl.xml` from my own copy (whitelist aligned
   to the four remaining names so `AppRun`'s guard passes and the product really runs,
   `app_exit=0`) gives `CHECK FAIL own XML definitions missing: vedit-tcl.xml (own-xml=[four
   files])` and exit 1 (`02-negative-missing-tcl.log`). The failure is the new check's, not the
   guard's.
4. **Restored:** the untouched copy passes again (`03-positive-after-negative.log`, exit 0).
5. **No silent skip:** a nonexistent AppDir exits 1 with `FAIL: no AppRun …`
   (`04-absent-appdir.log`); an AppDir whose syntax dir is empty and whose whitelist was
   removed starts the product with `own-xml=[]`, names all five files as missing and exits 1
   (`05-empty-syntax-dir.log`).
6. **AppRun untouched:** mtime 2026-11-05 02:43:48 (before this session), sha256
   `925ab1500e1504412a2cfa8a492d8140453f803dc3081702fa4412edb62406c3`; the git `M` status is
   the previous batch's uncommitted `ksh_guard` repair, and `find packaging/runtime -newermt
   2026-11-06` lists only `selfcheck.sh` (10:15). `bash -n` is clean.
7. **Defence in depth confirmed:** `assemble-appdir.sh` independently refuses to build without
   the same five (`REQUIRED_SYNTAX`), so the runtime check guards the damaged/hand-edited/
   re-assembled AppDir case.
8. **The old check would have passed the damaged AppDir:** a copy of the script with only the
   syntax block reverted to the old two-XML form, run on the same four-definition AppDir,
   reports `CHECK PASS … SELFCHECK PASS`, exit 0 (`07-old-check-on-negative.log`) — the lane's
   defect claim is reproduced, not taken on trust.

## Declined to judge

- The whitelist-vs-visible-set comparison itself — that is `AppRun`'s `ksh_guard` (the previous
  batch's F6 repair); this review only confirms U5 did not disturb it, not that the guard is
  correct.
- The `--appimage` extraction path — the delivery AppImage is not repacked in this batch (U8);
  I exercised `--appdir` only.
- Whether the AppDir assembled by the lane equals the final delivery AppImage — outside this
  review; the AppDir was assembled from the current working tree at 10:13–10:16 and its XMLs
  are hash-identical to `resources/syntax/`.
- A sixth definition added later would not be demanded by this check (it requires exactly the
  five named); that matches the frozen scope ("five"), not a defect.

## Bounds of this review

- I did not rebuild the AppDir myself (I used the lane's `target/u5-appdir`, after verifying its
  five XML hashes against `resources/syntax/` and its whitelist); I did not run the full
  `build-qt.sh`, the full ctest suite, the AppImage packaging, or the other gates.
- I did not run the selfcheck against the *delivery* AppImage (`packaging/runtime/out/AppImage`
  is the previous batch's, with two syntax defs and no repack in this batch — U8 owns that).
- The negative/old-script controls live under `target/review-u5/` and `target/u5-appdir*`
  (gitignored build output); no source file was modified, and the reviewed
  `packaging/runtime/selfcheck.sh` was executed unmodified for the positive/negative controls.
- Review is against the tree as of 2026-11-06 10:23 (+08:00); later changes (U6, U1, the
  packing step) are outside this review.
