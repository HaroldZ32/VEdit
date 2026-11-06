# A3 repair — evidence for the re-review

Lane: A3 (VHDL / Markdown / Tcl highlighting) — repair of `evidence/ux-batch-2/review-a3/REPORT.md`.
Contract: `.mpd/plans/ux-batch-2/00-plan.md` §3 A3 (+ rev3/rev4 rulings).
Baseline: `git HEAD 4e2d0a57ca50db9568db5a5b040cc66f04c9a05f`, shared dirty tree (other lanes' work
included); nothing outside the repair's allowed write scope was edited.

Every item below has its own before/after evidence in this directory. The one-line summary:

| Finding | Verdict | Evidence |
|---|---|---|
| R1 Blocker 1 — AppRun guard locale-dependent | **fixed**: both sides normalised with `LC_ALL=C`; selfcheck runs the guard under C and zh_CN.UTF-8; a wrong whitelist still fails closed (exit 2) | `01-r1-before.txt`, `02-r1-after.txt`, `03-r1-guard-fails-closed.txt`, `10-selfcheck-appdir.log` |
| R2 Blocker 2 — non-shadowing check could not fail | **fixed**: probe prints the winning `Definition::filePath()`; F2 gate asserts winner paths, adds a same-name high-version shadow control and a real user-level reverse probe | `11-f2-isolation.log`, `12-r2-gate-fails-on-shadow.log` |
| R2 Medium 4 — user-level isolation had no negative control | **fixed** (fixture was mounted one level too high *and* `XDG_DATA_HOME` was pinned) | `11-f2-isolation.log` (`appdir-reverse-user`) |
| R2 arithmetic 406 | corrected to `402 host names + 5 whitelist names − 1 shared SystemVerilog` | `11-f2-isolation.log`, `packaging/runtime/README.md`, `docs/handover/03-architecture.md` |
| R3 High — stale AppDir logs | **regenerated** from the rebuilt tree; old 09/10 replaced; everything else marked superseded | `04-r3-before-stale-logs.txt`, `08-assemble-appdir.log`, `../a3/logs/09-appdir-checks-full.log`, `../a3/logs/10-final-green.log`, `../a3/logs/00-LOG-INDEX.md` |
| R4(a) README §0 "ctest 全绿" | corrected to the real per-run numbers + current 6/6 | `../a3/README.md` §0/§3 |
| R4(b) §9.4 → §9.1.1 | fixed | `resources/syntax/README.md` |
| R4(c) VHDL licence rationale | corrected: the file is **not** unlicensed — Debian records it as MIT; self-authored LGPL kept; "adapted → upstream licence" clause kept | `THIRD_PARTY_NOTICES.md` §9.1.1, `resources/syntax/README.md`, `../a3/README.md` §2.2 |
| R4(d) two extension tables could drift | cross-check added for all 15 contract suffixes; proven it can fail | `06-r4d-extension-drift.log` |
| R4(e) labelled VHDL process did not fold | opener regex handles an optional `label :`; case added; proven it can fail | `05-r4e-vhdl-folding.log` |
| R4(f) `embargo_misfire` unexercised | disclosure kept and made explicit next to the claim; no negative control (licence rules forbid keeping the file) | `../a3/README.md` §4 item 3 |
| Medium 5 SEGFAULT attribution | closed on the rebuilt tree: 312 PASS / 0 FAIL, ctest green | `00-crosslane.txt`, `10-selfcheck-appdir.log` |

## R1 — packaged launcher must not depend on the host collation

**Change.** `packaging/runtime/AppRun`, new shell function `canonical_names()` + `ksh_guard()`:
the whitelist file (written by `assemble-appdir.sh` with Python `sorted()` = code-point order) and the
probe's visible names are **both** reduced with `LC_ALL=C sort` before the set comparison. The guard's
`sed` extraction was widened from `name="…"$` to `name="…".*$` because the probe now prints a trailing
`file="…"` (R2). `packaging/runtime/assemble-appdir.sh` records the collation contract next to the
whitelist generator.

**Before** (`01-r1-before.txt`, old AppDir):
```
LC_ALL=zh_CN.UTF-8  KSH-GUARD FAIL: visible definitions [Markdown,SystemVerilog,Tcl,Verilog,VHDL] != whitelist [Markdown,SystemVerilog,Tcl,VHDL,Verilog]
                    exit=2
LC_ALL=en_US.UTF-8  same failure, exit=2
LC_ALL=C            KSH-GUARD PASS, exit=0
```
**After** (`02-r1-after.txt`, rebuilt AppDir): `KSH-GUARD PASS` and `exit=0` under C, zh_CN.UTF-8 and
en_US.UTF-8. `selfcheck.sh` now runs the guard path under a C locale **and** a non-C locale
(zh_CN.UTF-8 when installed, else en_US.UTF-8; explicit `CHECK SKIP` if neither is installed), so the
old `LANG=C.UTF-8` mask can no longer hide this: `10-selfcheck-appdir.log` has
`CHECK PASS ksh_guard under LC_ALL=C` and `CHECK PASS ksh_guard under LC_ALL=zh_CN.UTF-8`.

**Not proven:** the selfcheck cases assume `sort` honours `LC_ALL` (POSIX; observed on this host and in
the CentOS 7 image), and they exercise `--hold 10`, not a full interactive launch.

## R2 — the non-shadowing check must be able to fail

**Probe change.** `cmake/smoke/ksh_env_probe.cpp`: `sortedValidDefinitions()` returns
`{name, Definition::filePath()}` sorted by (name, file); `printLoaded()` prints
`KSH-ENV def name="…" file="…"` (and the same for `editor_def`); the `defs=` line gains
`hash_files=` over the `name<TAB>file` pairs. `hash=` over names is unchanged for compatibility — and
deliberately stays identical under a shadow, because names are exactly what a shadow does **not** change.

**Gate change.** `packaging/runtime/verify-f2-isolation.sh`:
- positive: every whitelist name must win from `/app/usr/share/org.kde.syntax-highlighting/syntax/`,
  and **all** loaded definitions must win from there (`winner_offenders()`), not merely have a unique name;
- host reverse: unchanged semantics, now `defs=406` (= 402 host + 5 − 1 shared `SystemVerilog`);
- user reverse: `XDG_DATA_HOME=/root/.local/share`; the fixture had also been **mounted one directory too
  high** (`$USER` ends in `/syntax` but was mounted at the `org.kde.syntax-highlighting` level), which made
  it invisible even when the search path was right — fixed by mounting its parent;
- shadow control: `/shadow/Tcl.xml`, same name, `version="999"`, exposed via `XDG_DATA_DIRS=/app/usr/share:/shadow`;
  it must win, the winner-path assertion must report it, and `defs`/names must stay unchanged.

**Observed** (`11-f2-isolation.log`): `summary: pass=11 fail=0`, exit 0; `Tcl=/shadow/…` when the shadow
is visible; `defs=6` with `UserLeakProbe` at `/root/.local/share/…` for the user reverse.

**Can it fail?** `12-r2-gate-fails-on-shadow.log` runs a byte-identical copy of the script with one
temporary line changed (positive probe also sees `/shadow`): the name/count checks still PASS, the two
winner-path checks **FAIL** (`Tcl=/shadow/…`), `summary pass=9 fail=2`, exit 1. This is the proof that
the old name-only check could never provide.

**Not proven:** the KSyntaxHighlighting version pin is 6.24 (host) / the AppDir's self-built F2 library;
the tie-break on equal `version()` is not asserted (the control uses 999, so it wins deterministically).

## R3 — logs from the final source

(There is no `09`/`10` in this directory on purpose: the replacement acceptance logs live at
`../a3/logs/09-appdir-checks-full.log` and `../a3/logs/10-final-green.log`, which are the files the
review named.)

`bash packaging/runtime/assemble-appdir.sh --out target/a3-appdir-repair/AppDir` was run from the
rebuilt tree (`08-assemble-appdir.log`: `ASSEMBLE_EXIT=0`, whitelist 5, 5 syntax XMLs). The AppDir
assertion was run **unedited** with the AppDir's own F2 `libKF6SyntaxHighlighting`:

- `../a3/logs/09-appdir-checks-full.log` — complete output, header with command, binary sha256, source
  revision/mtimes; **104 checks / 0 failures**, every `definition-parsed.*` line carries a real
  AppDir path (the old log's `PASS no definition named …` is unreachable in the current code);
- `../a3/logs/10-final-green.log` — complete ctest 6/6 + `vedit_syntax_kde_selftest` + the same AppDir
  assertion;
- `../a3/logs/00-LOG-INDEX.md` — marks 01–08 and 11–14 as superseded with the reason each cannot be
  acceptance evidence (the impossible `PASS no definition named` class, the pre-repair 86-check runs,
  the SEGFAULT-era runs, the vacuous F2 check);
- `13-verify-not-shipped.log` — the delivery licence gate on the same AppDir: `pass=8 fail=0`,
  `DELIVERY_BLOCKED=no`, exit 0 (5 active definitions == whitelist, no banned KDE files, no
  `syntax-bundled/`).

## R4 — nits

- **(a)** `../a3/README.md` §0/§3 now states the real numbers: the delivered runs were 75% (one
  concurrent `vedit-edit-core-selftest` BAD_COMMAND) and 5/6 (SEGFAULT), and the current rebuilt run is
  6/6 plus 104/0 plus F2 11/0.
- **(b)** `resources/syntax/README.md` points at `THIRD_PARTY_NOTICES.md` §9.1.1.
- **(c)** VHDL rationale corrected in `THIRD_PARTY_NOTICES.md` §9.1.1, `resources/syntax/README.md`
  and `../a3/README.md` §2.2: the file has no **in-file** licence statement but the Debian copyright
  file records it under MIT, so it is not called "unlicensed"; self-authoring is justified by the
  absence of a licence text to carry, the naming rule, and the repository's evidence discipline. The
  three files stay self-authored **LGPL-2.1-or-later**, and the "adapted → upstream licence" clause is
  kept (vhdl MIT / markdown GPL-BSD / tcl BSD).
- **(d)** `src/editor/kernel/highlight_mode_selftest.cpp` + `src/editor/CMakeLists.txt`: the selftest
  links `vedit_syntax_kde` and runs `drift.extension.<suffix>` for all 15 contract suffixes, comparing
  `highlight_mode.cpp`'s table with `KsyntaxData::languageNameForExtension`. `06-r4d-extension-drift.log`
  removes `mdown` from one table → `drift.extension.mdown FAIL highlight_mode='Markdown' ksyntax_data=''`,
  81 checks / 1 failure; restored → PASS, 81/0. (A suffix added to *both* tables still needs the test
  list updated — the guard covers the contract's suffixes, which is what the finding asked for.)
- **(e)** `resources/syntax/vedit-vhdl.xml`: two opener rules — declarations
  (`entity|architecture|package|configuration|component`) and labelable constructs
  (`([A-Za-z_][A-Za-z0-9_]*\s*:\s*)?(process|block|function|procedure)`); `entity` is deliberately not
  in the labelled rule so `u1 : entity work.foo` does not open a region. Two new checks
  (`construct.vhdl-fold-labelled-process`, `construct.vhdl-fold-unlabelled-process`). Before/after in
  `05-r4e-vhdl-folding.log`: old regex `FAIL begins=0 ends=1`, fixed `PASS begins=1 ends=1`;
  `resources/syntax/README.md` also lost its wrong `generate`/`record` claim.
- **(f)** The `embargo_misfire` branch is still unexercised; `../a3/README.md` §4 item 3 now says so
  explicitly next to the claim and states that the "new files are not false-rejected" conclusion rests
  only on the positive all-files predicate (`forbidden-none`). No fixture was added because it would
  require keeping a banned KDE file in the tree.

## What this repair could NOT prove

- Real GUI appearance (no xcb screenshot in this lane); folding is asserted through the hidden
  highlighter, not by clicking the gutter.
- The shadow control uses the **same-name higher-version** vector; other shadow mechanisms
  (equal-version tie-break, `syntax-bundled/`) are not separately exercised.
- `embargo_misfire` has no negative control (see R4(f)).
- The drift guard covers the 15 contract suffixes; it cannot detect a suffix added to only one table
  *and* absent from the test list.
- The F2 gate runs in the pinned CentOS 7 container; the host `sort`/`locale` behaviour was verified
  separately, and the packaged launcher was exercised with `--hold 10`, not an interactive session.
- The F2 script's **AppImage branch** was not exercised (no AppImage was built in this repair); the new
  user-reverse and shadow checks are written for it but only the AppDir path has observed output.
- Other lanes' uncommitted work is part of the rebuilt tree; the repair did not audit it.

## Out-of-scope findings (reported, not absorbed)

These are real findings from the review that the repair's allowed write scope (`src/syntax/product/**`
and `crates/**` are forbidden) does not permit fixing. They are listed so the captain can assign them:

1. `src/syntax/product/CMakeLists.txt` — the deferred
   `cmake_language(DEFER … CALL vedit_syntax_link_product)` evaluates `CMAKE_CURRENT_SOURCE_DIR` at the
   top-level call site, so the generated ctest environment contains
   `VEDIT_SYNTAX_DIR=/vedit-build/../../../resources` = `/resources` instead of the workspace
   `resources/`. `src/syntax/product/syntax_product.cpp` (`privateSyntaxDir()`) uses a non-empty
   environment value without a fallback, and the test's `PASS_REGULAR_EXPRESSION` only requires
   `syntax-product: grammar=verilog` (not `syntax_data_ok` / the five definition names), so the app test
   does not actually load A3's XMLs. (Review Medium 5, second half.)
2. The A3 `--selfcheck` evidence referenced by the old README was not in the evidence directory; this
   repair added real output (`10-selfcheck-appdir.log`), so that part is closed.
3. `evidence/ux-batch-2/a3/raw/` is untracked upstream material; if the evidence directory is committed,
   record the upstream licences next to it (review "Declined to judge" list).
