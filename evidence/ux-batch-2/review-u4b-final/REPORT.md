# U4b review (independent) — resolved definition names + exit-code anchor

Reviewer: independent verifier. READ-ONLY for the reviewed work: no product source, test,
CMake file or shared generated file was edited by this review. The negative controls used
**copies** of the generated test definition under `target/review-u4b/` (build area), changing
only `VEDIT_SYNTAX_DIR`; the shared `target/qt6-build/CTestTestfile.cmake` was never touched
(sha256 `4ae24e52…86aff0`, mtime from the lane's configure).

Contract: close the two findings of `evidence/ux-batch-2/review-u4-final/REPORT.md`
(U4-F1: file names instead of resolved definition names; U4-F2: CTest ignores the exit code
when `PASS_REGULAR_EXPRESSION` is set), per the Lead's T32 brief.

Evidence reviewed: `evidence/ux-batch-2/u4b-final/README.md` and its logs; the diffs of
`src/syntax/kde/ksyntax_data.{h,cpp}`, `src/syntax/product/syntax_product.{h,cpp}`,
`src/syntax/product/CMakeLists.txt`; the generated test definition.

New evidence produced by this review (under `evidence/ux-batch-2/review-u4b-final/`):

| File | Content |
|---|---|
| `01-real-run.log` | my own `ctest -V` of both syntax tests on the real tree: 2/2 passed; the new startup line with `definitions-ok=[Markdown,SystemVerilog,Tcl,VHDL,Verilog]` and `APP-SHELL-SELFTEST ok=1` |
| `02-corrupt-def-ctest-new.log` | my corrupt-`vedit-tcl.xml` control (`<syntax></syntax>`, temp resources dir) against a COPY of the shipped test definition: `***Failed`, exit 8, `definitions-ok=[Markdown,SystemVerilog,VHDL,Verilog]` (Tcl gone) |
| `02-corrupt-def-ctest-old.log` | the SAME corrupt tree against the old U4 predicate: `Passed`, exit 0 — the finding's original false green reproduced, so the failure above is caused by the new assertion |
| `03-exitcode-e-a.log` | probe printing the exact startup line then `exit 1`, under the shipped predicate: `***Failed` — the old "syntax line + nonzero exit" hole is closed |
| `03-exitcode-e-b.log` | probe printing the startup line **and** `APP-SHELL-SELFTEST ok=1` then `exit 1`: `Passed` — residual CTest exit-code blindness (Nit) |
| `05-corrupt-no-contexts.log` | extra control: well-formed `<language name="Tcl"><contexts></contexts></language>` (parseable root, no contexts) → Tcl drops out, test fails |
| `06-generated-property.txt` | the generated `add_test`/`set_tests_properties` lines, hashes, and the CMakeLists derivation they match |
| `04-scope-check.txt` | the five changed files (all inside `src/syntax/product/` + `src/syntax/kde/`), resources untouched, related hashes |
| `07-related-ctests.log` | loader-related ctests (`vedit-syntax-kde-data`, `vedit-syntax-directory-equivalence`, `vedit-highlight-mode-selftest`): 3/3 passed |

## Assessment

**Risk level: LOW. Verdict: PASS.** Both U4 findings are closed. The startup line now reports
the five **resolved definition names** (`definitions-ok=[Markdown,SystemVerilog,Tcl,VHDL,Verilog]`)
and the test requires that exact list plus the five file names; my own corrupt-definition control
— the load-bearing check — now fails, and the same corrupt tree still passes under the old U4
predicate, which proves the new assertion is what catches it. The exit-code hole is closed for
the failure mode it was opened for: the predicate now also requires the selfcheck's own terminal
`APP-SHELL-SELFTEST ok=1` line, and a probe that prints the old syntax line and exits 1 is
`***Failed`. The normal run and both syntax tests pass, the change is confined to the two
claimed directories, and the related loader ctests still pass. One residual CTest semantics nit
below (a process that prints the success line and then exits nonzero is still green), which is
inherent to `PASS_REGULAR_EXPRESSION` and much narrower than the original hole.

## Findings

### U4b-F1 — Nit: CTest still ignores the exit code once the full expected output is printed

- What: the PASS predicate now requires `APP-SHELL-SELFTEST ok=1`, so a failing `--selfcheck`
  (no success line, or `ok=0`) can no longer be green — my probe `e-a` (exact startup line,
  `exit 1`) is `***Failed`. But CTest's `PASS_REGULAR_EXPRESSION` still overrides the return
  code: my probe `e-b` prints the same startup line **plus** `APP-SHELL-SELFTEST ok=1` and
  exits 1, and CTest reports **Passed** (`03-exitcode-e-b.log`). A crash *after* the success
  line is therefore still green.
- Where: the test definition in `src/syntax/product/CMakeLists.txt`
  (`PASS_REGULAR_EXPRESSION` of `vedit-app-syntax-startup`); the product side is
  `vedit::syntax::product::describe` (`syntax_product.cpp`) and the app's selfcheck terminal
  line.
- Why it matters: it is the same CTest semantics that made the original hole; the anchor makes
  the common failure (selfcheck reports a failure) visible, but the exit code itself is still
  not asserted. In practice a failure after the terminal line would be in teardown; the product
  prints the selfcheck verdict as its last check.
- Direction (described, never applied): make the test command a small wrapper (`cmake -P` or a
  script) that runs `vedit-app --selfcheck`, asserts `rc == 0` **and** the output anchors, and
  exits nonzero itself; or have the app expose a dedicated `--assert-syntax-startup` mode that
  returns nonzero on any of the five checks, removing the regex proxy. Neither is needed to
  close the review finding, which was about a *failing selfcheck being masked*.

## What I verified myself (with observed output)

1. **Resolved names are real.** The real run's startup line is
   `... startup-hook=1 definitions-ok=[Markdown,SystemVerilog,Tcl,VHDL,Verilog]`, sorted, from
   `KsyntaxData::load`'s `resolved_private_definition_names` (each private file must parse as a
   usable definition and be resolved by the repository). The same line is consumed by the test's
   `definitions-ok=\[...\]` anchor (`06-generated-property.txt`; the generated line matches the
   CMakeLists variables `_u4_resolved_definitions` / `_u4_own_xml`).
2. **My corrupt-definition control now fails.** Five files present, `vedit-tcl.xml` replaced by
   `<syntax></syntax>` in `target/review-u4b/probe/broken/resources/syntax/`, run through a
   COPY of the shipped test definition with only `VEDIT_SYNTAX_DIR` changed: `definitions-ok=`
   lost `Tcl`, the test is `***Failed` (exit 8). The same corrupt tree under the old U4
   predicate is `Passed` (exit 0), reproducing the original finding. An extra control with a
   well-formed but context-less `<language name="Tcl">` also drops Tcl and fails.
3. **Exit-code anchor.** The generated predicate requires
   `.*APP-SHELL-SELFTEST ok=1` after the startup line; the real run prints
   `APP-SHELL-SELFTEST ok=1 checks=312 failures=0` and passes. Probe `e-a` (startup line, exit
   1, no selfcheck line) fails; probe `e-b` (startup line + success line, exit 1) passes
   (U4b-F1). So the *masked-failing-selfcheck* hole is closed; the raw exit code is still not
   the oracle.
4. **Normal path and neighbours.** `ctest -R "^(vedit-app-syntax-startup|vedit-syntax-product-tests)$"`
   → 2/2 passed; `vedit-syntax-kde-data`, `vedit-syntax-directory-equivalence`,
   `vedit-highlight-mode-selftest` → 3/3 passed (`07-related-ctests.log`).
5. **Scope.** The five files changed since 10:20 are exactly
   `src/syntax/product/{CMakeLists.txt,syntax_product.cpp,syntax_product.h}` and
   `src/syntax/kde/{ksyntax_data.cpp,ksyntax_data.h}` (10:28–10:57); `resources/syntax/*.xml`
   hashes are unchanged from my U5 review (the corrupt file lives only in `target/`); no file
   under `resources/`, `packaging/` or `build/` was changed by this round. (`build/gates/lrelease.sh`
   changed at 10:28 by another lane, not U4b.) The generated test definition was produced by
   the lane's configure and matches the CMakeLists derivation; I did not hand-edit it and my
   controls only used copies under `target/review-u4b/`.

## Declined to judge

- Whether a *parseable but semantically broken* definition should fail: `definitionNameInFile`
  accepts a well-formed `<language name=...>` with contexts, and the name still counts if the
  repository resolves it (possibly from the distro library's embedded copy — the loader records
  a diagnostic for that shadowing). The required control (a non-definition file) fails; full
  semantic validation of a parseable definition is beyond this test, and the delivered AppDir
  library embeds no definitions.
- The pre-existing `build/gates/lrelease.sh` change at 10:28 — another lane's, outside U4b.
- The full configure/build reproducibility of the generated file — the captain's full gate
  re-runs configure; I verified the generated lines against the CMakeLists derivation and the
  test outcome with two predicates.
- The packaged selfcheck (U5) against the new `definitions-ok` field — the field is appended at
  the end of the line and U5's greps (`own-xml=`, `verilog=1`) are unaffected; not re-run here.

## Bounds of this review

- I ran only the syntax/loader tests in the pinned image; I did **not** run the full build, the
  full ctest suite, the packaging selfcheck, the AppImage, or any gate (per the build-tree
  notice).
- I did not rebuild the product; I used the lane's binary (2026-11-06 10:59) and the shared
  build tree read-only, with my own test-definition copies under `target/review-u4b/`.
- The corrupt-definition controls modify only `target/` copies; `resources/` was verified
  untouched by sha256.
- Review is against the tree as of 2026-11-06 11:08 (+08:00); later lane changes (U1/U6/docs)
  are outside this review.
