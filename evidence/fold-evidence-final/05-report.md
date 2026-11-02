# T100 — three T30 follow-ups: block comments, A4 ldd fact, T45 upgrade audit

C9 header

- Each log in this directory carries its own `collected_at` / `git_head` header.
- Headless only; no GUI claim.

## A. F-4: `/* ... */` handling (implemented, red-first)

Choice: **(a) implement block-comment skipping** (cheaper than declaring the
subset, and it keeps the "no silent gap" rule).

- Pre-fix red evidence (`01-blockcomment-red.log`): the new
  `testBlockCommentsDoNotCreateFolds` produced **ranges=2** (`(2,3)` + `(0,5)`)
  and `blockcomment.inline.unbalanced FAIL actual=1 expected=0`
  (`FOLDING-TESTS ok=0 checks=80 failures=2`).
- Fix: `KsyntaxFoldSource::ranges()` keeps an `inBlockComment` state across
  lines, consumes `*/`, and never tokenises a word inside the block; `//` and
  string handling are unchanged.
- Post-fix green (`02-blockcomment-green.log`, run with F2):
  `A4 BLOCKCOMMENT ranges=1 unbalanced=0 (0,5)`, all six block-comment checks
  pass, `FOLDING-TESTS ok=1 checks=82 failures=0`, `TEST-EXIT=0`.
- `src/syntax/folding/README.md` now states the rule and the regression test.

## B. F-1: the A4 lane log now has a link fact

- New companion `evidence/a4-fold/01b-lane-tests-f2-ldd.log`:
  `libKF6SyntaxHighlighting.so.6 => /…/target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6`,
  then the same test run under the same `LD_LIBRARY_PATH`:
  `FOLDING-TESTS ok=1 checks=82 failures=0`, `TEST-EXIT=0`.
- The old `01-lane-tests.log` gained a top banner pointing to the companion, and
  `REPORT.md` explains why the F2 sha256 + "0 embedded definitions" marker are
  **properties of the file, not of the loader's resolution**: the binary can
  load a different library through another search path while both remain true.
  `ldd` with the exact environment is the link fact.

## C. T45 conditional PASS

See `03-T45-upgrade-audit.md`: old 10/11 are explicitly INVALID; the `-F2` pair
exists with `ldd` on every result; the negative control rejects a DISTRO link;
one stale citation was fixed; **T45 can be an unconditional PASS** with X/Y/Z as
listed there. Verdict files themselves were not edited.

## Verification (this run)

- standalone folding lane ctest: **4/4 passed**, `FOLD-CTEST-EXIT=0`
- root ctest: **22/22 passed**, `ROOT-CTEST-EXIT=0`
- `bash build/gates/typecheck.sh`: **PASS**, `TYPECHECK-EXIT=0`
  (`04-gates.log`; the tree was green at this HEAD)

## Scope

Changed: `src/syntax/folding/{ksyntax_fold_source.cpp,tests/folding_tests.cpp,README.md}`,
`evidence/a4-fold/**`, `evidence/syn-kde-data/FOLDING-fix-T45.md` (one stale
citation), and `evidence/fold-evidence-final/**`. No `src/editor/**`,
`src/app/**`, `src/workspace/**`, other lanes, tests/gates beyond the lane test
added here. No commit/push.
