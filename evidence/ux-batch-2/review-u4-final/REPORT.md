# U4 review (independent) — syntax startup test integrity

Reviewer: independent verifier. READ-ONLY for the reviewed work: no product source, test,
CMake file or gate was edited by this review. Temporary experiments lived in
`target/review-u4/` (a gitignored build area) and the shared generated
`target/qt6-build/CTestTestfile.cmake` was restored byte-identically
(sha256 `f961d35371…dd3af`, the same bytes a fresh configure of the fixed tree writes);
see the note at the end of this report.

Contract: `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U4 — fix the deferred
`VEDIT_SYNTAX_DIR` (it resolved to `/resources`) and strengthen the PASS condition from
`"syntax-product: grammar=verilog"` to "syntax data loaded + all FIVE definition names
(Verilog, SystemVerilog, VHDL, Markdown, Tcl)", with a reproduced negative control.

Evidence reviewed: `evidence/ux-batch-2/u4-final/` — `README.md`, the before-fix baseline
(`00-baseline-before-fix*.log`), the configure and property snapshots (`01-configure.log`,
`10-generated-property-line-after.txt`), the after-fix runs (`02…05`), the build/ctest
(`07-affected-targets-build.log`, `08-both-tests.log`, `09-selfcheck-full.log`), plus
`src/syntax/product/CMakeLists.txt` (the only changed source file).

New evidence produced by this review (all under `evidence/ux-batch-2/review-u4-final/`):

| File | Content |
|---|---|
| `01-property-and-hashes.txt` | source + generated-file sha256 and the generated `add_test`/`set_tests_properties` lines I read |
| `02-positive-ctest.log` | my own `ctest -R vedit-app-syntax-startup -V` run in the pinned image: PASSED |
| `03-negative-empty-dir.log` | negative control: `VEDIT_SYNTAX_DIR` = an existing empty dir → `***Failed` (ctest exit 8) |
| `04-negative-four-defs.log` | negative control: four of the five shipped XMLs (proper `syntax/` layout) → `***Failed` (ctest exit 8) |
| `05-negative-missing-path.log` | negative control: nonexistent path → `***Failed` (ctest exit 8) |
| `06-restored-positive.log`, `08-both-tests.log` | restored tree: startup test + `vedit-syntax-product-tests` both pass (2/2) |
| `07-scope-check.txt` | git status/diff scope and the only `src/syntax/**` file modified today |
| `09-broken-tcl-definition-output.log` | five files present but `vedit-tcl.xml` replaced by `<syntax></syntax>`: the startup line still lists five files and the shipped PASS regex STILL MATCHES |
| `10-ctest-exit-code-semantics.log` | CTest probe: with `PASS_REGULAR_EXPRESSION` set, a command that prints the line and exits 1 is reported **Passed** |

## Assessment

**Risk level: LOW. Verdict: PASS** — the two defects U4 was opened for are genuinely fixed
and the widened check demonstrably fails in three independent negative controls, including
"four of five definitions". One criterion substitution needs to be recorded rather than
presented as equivalent: the startup PASS regex asserts the five shipped **XML file names**,
not the five resolved **definition names**, and my corrupt-`vedit-tcl.xml` experiment shows
the test still passes in that state. Because the original defect class (nothing loaded while
green) is now caught, because a missing file is caught, and because the definitions' actual
resolution is exercised by the sibling `vedit-highlight-mode-selftest` construction checks,
I rank this **Medium**, not Blocker/High; the lane's own README justification for the
substitution is factually wrong and must be corrected either way.

## Findings

### U4-F1 — Medium: "all five definition names" is implemented as "five file names present", and a corrupt definition still passes

- **Explicit judgement on the writer's question:** a file name is **NOT** an acceptable equivalent of
  a resolved definition name *on the writer's stated reason* ("a file is only listed after
  resolution"), because that reason is false in the code. The loader appends every non-embargoed
  `*.xml` it finds in the private `syntax/` directory to `private_definition_files` before/without
  resolving it as a definition, so a present-but-invalid `vedit-tcl.xml` is still listed and the
  test still passes. The substitution therefore leaves a real false-green window; it is a Medium
  finding rather than a Blocker only because the original defect class (nothing loaded) and a
  *missing* file are both caught, and the definitions' actual resolution is exercised by
  `vedit-highlight-mode-selftest`'s construction checks. If the captain accepts the file-presence
  proxy, the README and the acceptance matrix must say "five files present", not "five definition
  names".
- What: The strengthened `PASS_REGULAR_EXPRESSION` in `vedit_syntax_link_product`
  (`src/syntax/product/CMakeLists.txt`) requires `own-xml=[` five entries drawn from the five
  shipped file names plus `verilog=1 systemverilog=1`. `own-xml` is
  `KsyntaxData::load`'s `report_.private_definition_files`
  (`src/syntax/kde/ksyntax_data.cpp`), and that list is appended for **every `*.xml` found in
  the private `syntax/` directory** that is not sha256-embargoed — *before* any parse/resolve
  step. Only the two Verilog files get an extra `languageNameInFile` parse. The lane's README
  says "the loader only reports a file after it resolved it as a definition"; that is false.
- Where: `KsyntaxData::load` (`src/syntax/kde/ksyntax_data.cpp`), `describe`
  (`src/syntax/product/syntax_product.cpp`, `own-xml=`), and the PASS regex in
  `vedit_syntax_link_product` (`src/syntax/product/CMakeLists.txt`).
- Why it matters: a shipped `vedit-vhdl.xml` / `vedit-markdown.xml` / `vedit-tcl.xml` that is
  present but malformed (or simply not a definition) satisfies the test. I reproduced exactly
  that: with the five files present and `vedit-tcl.xml` replaced by `<syntax></syntax>`, the
  startup line reads `own-xml=[vedit-vhdl.xml,vedit-tcl.xml,vedit-systemverilog.xml,
  vedit-markdown.xml,vedit-verilog.xml] … verilog=1 systemverilog=1` and the regex matches
  (`09-broken-tcl-definition-output.log`) — i.e. the check is green while one of the five
  definitions did not load. That is the same *shape* of false green U4 was meant to remove,
  narrowed to one definition.
- Mitigation that keeps this Medium: a missing file fails (`04-negative-four-defs.log`), a
  wrong/empty path fails (`03`, `05`), and the definitions' actual resolution is covered by
  `vedit-highlight-mode-selftest`'s construction checks for VHDL/Markdown/Tcl
  (`src/editor/kernel/highlight_mode_selftest.cpp`, `runConstructionChecks`) and by
  `vedit-syntax-product-tests` for Verilog/SystemVerilog. The gap is in *this* test's own
  assertion strength, not in the product.
- Direction of a fix (described, not applied): have the product report the resolved
  **definition names** it actually owns (the data already exists: `LoadReport::definition_names`
  and the per-file parse; a minimal `definitions-ok=[…]` field in
  `describe()`/`LoadReport`), then assert those five names in the PASS regex. That is a C++
  change outside U4's write scope (`src/syntax/kde/**` or `src/syntax/product/**`), so it needs
  a captain scope decision. If the captain instead accepts "five files present" as the
  criterion, the U4 README's justification must be corrected to say **file presence, not
  resolution**, and the acceptance matrix must record the deviation; the phrase "all five
  definition names" must not be claimed as met.

### U4-F2 — Nit: the startup test still cannot fail on a failed `--selfcheck` (exit code ignored)

- What: `PASS_REGULAR_EXPRESSION` is the test's only oracle, and CTest ignores the process
  exit code once it is set. My probe (`10-ctest-exit-code-semantics.log`) prints the match and
  exits 1: CTest reports **Passed**. So a `vedit-app --selfcheck` that prints the syntax line
  and then fails (or crashes) is still green.
- Where: `vedit_syntax_link_product` test `vedit-app-syntax-startup`
  (`src/syntax/product/CMakeLists.txt`); pre-existing property of the test, not introduced by
  U4, and the lane correctly asserts `startup-hook=1`.
- Why it matters: the test is named "syntax startup" and its product run is a 312-check
  selfcheck; a selfcheck regression after the syntax line is invisible to it.
- Direction: append `APP-SHELL-SELFTEST ok=1` (already printed by the same run, at the end)
  to the PASS regex, or add a `FAIL_REGULAR_EXPRESSION` for `APP-SHELL-SELFTEST ok=0` /
  `failures=[1-9]`.

## What I verified myself (and its observed result)

1. **The path is correct in the generated property.** I read
   `target/qt6-build/CTestTestfile.cmake` (`01-property-and-hashes.txt`):
   `VEDIT_SYNTAX_DIR=/vedit-build/resources` and the five-entry
   `PASS_REGULAR_EXPRESSION`. The generated `_BACKTRACE_TRIPLES` points at
   `src/syntax/product/CMakeLists.txt` (the deferred function), so the value now comes from
   the file-scope `SYNTAX_PRODUCT_RESOURCES_DIR` + GLOBAL property, not the deferred scope.
2. **The test passes on the real tree** (`02-positive-ctest.log`, exit 0): the startup line
   carries `private-dir='/vedit-build/resources' definitions=452
   own-xml=[five files] … verilog=1 systemverilog=1 … startup-hook=1`.
3. **It still fails when the path is broken** — three independent negatives, all
   `***Failed` with CTest exit 8 and "Required regular expression not found":
   an existing empty dir (`03`), four-of-five definitions (`04`), a nonexistent path (`05`).
   The four-of-five case is the direct proof that the five-entry requirement is not vacuous:
   `own-xml` had exactly four entries and `verilog=1 systemverilog=1`, and the test failed.
4. **Restored tree passes again** (`06-restored-positive.log`, exit 0), and both syntax tests
   pass together (`08-both-tests.log`: 2/2).
5. **Scope is confined** (`07-scope-check.txt`): `git status` shows only
   `src/syntax/product/CMakeLists.txt` modified under `src/syntax/`; the diff is
   +49/−4 and touches only the resources-path capture, the two test ENVIRONMENT lines and the
   PASS regex; `vedit-syntax-product-tests` still passes with the same captured path, so the
   change weakens no other test.
6. **The lane's own three-point verification is reproducible**: its property line, its
   five-definition output and its broken-path failure match mine.

## Declined to judge

- Whether the `vedit-syntax-product-tests` unit test should also assert VHDL/Markdown/Tcl by
  name — outside U4's write scope and outside the startup-test criterion; noted only as the
  reason the Medium above is not a Blocker.
- Whether the pinned image's embedded KDE definitions shadow any of the five — that is an
  A3/AGENT §9 fact, not U4; the startup line's `verilog=1 systemverilog=1` is the loader's own
  availability flag and is not re-judged here.
- The exact AppDir/packaged behaviour of the same definitions — U5's review owns the packaged
  selfcheck; this review covers the build-tree test only.
- Cosmetic field-order coupling of the PASS regex (a reordered `describe()` line would fail the
  test) — accepted as a test-of-output trade-off, not a defect.

## Bounds of this review

- I ran only the two syntax ctests in the pinned image (`vedit-qt6-build:local`); I did **not**
  run the full `build-qt.sh`, the full 50-test suite, packaging, or the AppImage.
- I did not read the full `ksyntax_data.cpp` or the whole syntax stack; the claim about
  `private_definition_files` is based on the `KsyntaxData::load` loop that populates it, and
  it is confirmed empirically by the corrupt-`vedit-tcl.xml` run.
- The first three negative controls patched the **generated**
  `target/qt6-build/CTestTestfile.cmake` in the shared build tree (before the "use a copy"
  instruction existed); after each run it was restored and verified by sha256
  (`f961d35371…dd3af`), and the later corrupt-definition check ran the product binary
  directly with an environment override, patching nothing. The tree today is byte-identical
  to the fixed configure's output; a fresh configure regenerates the same file.
- Review is against the tree as of 2026-11-06 10:10 (+08:00); later lane changes (U5,
  U6, U1) are outside this review.
