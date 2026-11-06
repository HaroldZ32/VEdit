# A3 review — VHDL / Markdown / Tcl syntax highlighting (UX batch 2)

Reviewer lane: independent verifier for A3. Findings only; no source, test or gate was edited.
Frozen contract: `.mpd/plans/ux-batch-2/00-plan.md` §3 A3 + §1 + §2, including the two appended
captain rulings (rev3 licence wording; rev4 additions).
Deliverable: `evidence/ux-batch-2/a3/` and the files listed in the review task.

**Risk level: HIGH.**
**Verdict: FAIL.**

## Assessment

The three definitions are genuinely self-authored, not a transcription of the upstream KDE files
(no shared comments, no shared regex `String=` literals, three contexts versus the upstream VHDL's
~60, and the shipped VHDL keyword list even contains three words absent from the whole upstream
token union), and the table-first mapping, including `.sdc/.xdc → Tcl`, passes an independent rerun
of the AppDir selftest (86/0 against the AppDir's own F2 `libKF6SyntaxHighlighting.so.6`). The
deliverable is nevertheless not acceptable: the A3 whitelist growth breaks the packaged AppRun's
`ksh_guard` on the primary `zh_CN.UTF-8` locale because the whitelist is Python-codepoint sorted
while the guard compares it with a host-locale `sort`; the reworked F2 "non-shadowing" check cannot
fail because KSyntaxHighlighting 6.24 keeps one definition per name; and the delivered 86-check
evidence predates the final selftest source and contains a check line the current code cannot print.
The cross-lane SEGFAULT attribution is plausible for the app-shell assertions but unproven for the
crash itself; I cannot decide that question from the evidence supplied.

## Findings (ranked)

### Blocker 1 — the packaged AppRun refuses to start on the primary `zh_CN.UTF-8` locale

- **Where:** `packaging/runtime/AppRun`, shell function `ksh_guard`; whitelist producer in
  `packaging/runtime/assemble-appdir.sh` (the inline `python3` generator that writes
  `usr/share/vedit/ksh-whitelist.txt`); masked by `packaging/runtime/selfcheck.sh`, which invokes
  AppRun with `LANG=C.UTF-8`.
- **What:** `assemble-appdir.sh` writes the whitelist with Python `sorted()`, producing
  `Markdown,SystemVerilog,Tcl,VHDL,Verilog`. `ksh_guard` reads that string verbatim as `wl`, then
  builds `seen` as `printf … | sort | paste -sd, -`. `sort` honours the caller's `LC_COLLATE`
  (`LANG=zh_CN.UTF-8` on this host), which orders `Verilog` before `VHDL`; the comparison
  `[ "$seen" != "$wl" ]` is then true and the guard prints `KSH-GUARD FAIL` and `exit 2` before the
  product is executed.
- **Evidence:** on this review host (`LANG=zh_CN.UTF-8`, `LC_ALL` unset),
  `printf 'Markdown,SystemVerilog,Tcl,VHDL,Verilog' | tr ',' '\n' | sort | paste -sd, -` →
  `Markdown,SystemVerilog,Tcl,Verilog,VHDL`, while the packaged
  `target/a3-appdir/AppDir/usr/share/vedit/ksh-whitelist.txt` is
  `Markdown,SystemVerilog,Tcl,VHDL,Verilog`; the exact comparison therefore fails. The same
  collation mismatch is already recorded in `evidence/ux-batch-2/a3/logs/05-f2-isolation-before.log`
  (`names [...] != [...]`), where the author fixed `verify-f2-isolation.sh` by sorting both sides but
  did not apply the same fix to the product-side guard. Before A3 the whitelist was
  `SystemVerilog,Verilog`, whose order is the same in both collations, so A3 introduces the failure.
  `selfcheck.sh` runs AppRun with `LANG=C.UTF-8`, which is why the packaging CI did not see it.
- **Why it matters:** AppRun is the shipped launcher. On a Chinese-locale desktop the package exits
  2 before the application starts; this is a user-visible launch regression, not a cosmetic gate
  issue. The A3 evidence never executes AppRun (the 86-check run invokes the selftest binary
  directly and sets XDG itself), so no A3 artefact covers this path.
- **Direction of a fix:** make the guard's comparison locale-independent — sort `wl` with the same
  `LC_ALL=C sort` used for `seen` (or sort both with the caller's `sort`), or normalise the whole
  guard to `LC_ALL=C`. Add a `selfcheck.sh` case with a non-C locale (`zh_CN.UTF-8` or
  `en_US.UTF-8`) so the packaging gate exercises the real launcher path. Do not weaken the whitelist
  or remove the guard.

### Blocker 2 — the reworked "mutually non-shadowing" check cannot detect a shadow

- **Where:** `packaging/runtime/verify-f2-isolation.sh`, function `check_positive` (the new
  `duplicates` check and its comment) and `run_probe` (the `UserLeakProbe` fixture);
  `cmake/smoke/ksh_env_probe.cpp`, function `sortedValidNames` (prints names only); repeated in
  `packaging/runtime/README.md` and `docs/handover/03-architecture.md`.
- **What:** `check_positive` asserts that the visible names contain no duplicates. In the pinned
  KSyntaxHighlighting 6.24.0 source, `RepositoryPrivate::addDefinition` stores definitions in
  `m_defs` keyed by `def.name()` (`try_emplace`; a later file replaces only if its `version()` is
  higher), and `Repository::definitions()` returns `m_sortedDefs` built from that map. A same-named
  host or user definition can therefore never appear twice: it either replaces ours or is discarded,
  but the name list is unchanged. The probe's `--list` output prints only `definition.name()`, not
  the winning `definition.filePath()`, so the script cannot tell which file won. The author's own
  reverse run proves the point: `defs=406` equals 402 host names + 5 whitelist names − 1 shared
  `SystemVerilog`, not 407; and the positive run reports no duplicate by construction. The
  `UserLeakProbe` fixture is inert as well: `run_probe` always sets `XDG_DATA_HOME="/app/usr/share"`,
  so the fixture mounted at `/root/.local/share` is never in the search path, and `check_reverse`
  varies only `XDG_DATA_DIRS`. This also contradicts `packaging/runtime/README.md` and
  `docs/handover/03-architecture.md`, which describe 406 as "402 host + 1 user".
- **Why it matters:** the rev3 ruling explicitly required the script to preserve the meaning
  "bundled definitions and system definitions are mutually non-shadowing — not merely counting
  entries". The realistic same-name shadow vector is a user-level `SystemVerilog.xml` (or any other
  whitelist name) overriding ours; that is exactly the vector the script claims to test and cannot.
  A check that can never fail is worse than no check: it certifies a property that is not observed.
- **Direction of a fix:** have the probe print `Definition::filePath()` alongside each loaded name
  (and make the whitelist check assert that every whitelist name resolves to a file under
  `$APPDIR/usr/share/org.kde.syntax-highlighting/syntax/`). Add a negative control with a
  same-named, higher-version host definition and require the check to fail when the host wins; add a
  reverse run with `XDG_DATA_HOME="$HOME/.local/share"` (and narrowed `XDG_DATA_DIRS`) and require
  `UserLeakProbe` to reappear. Because `cmake/smoke/ksh_env_probe.cpp` is outside A3's write scope,
  this needs a captain-approved scope expansion rather than another script-only check; report that
  to the captain instead of claiming the property is proven.

### High 3 — the delivered AppDir evidence is not from the final selftest source

- **Where:** `evidence/ux-batch-2/a3/logs/09-appdir-checks-full.log`,
  `evidence/ux-batch-2/a3/logs/10-final-green.log`, `src/editor/kernel/highlight_mode_selftest.cpp`
  (`runChecks`, the `definition-parsed.*` check), and the `README.md` claim of "86 checks / 0
  failures".
- **What:** log 09 prints `CHECK definition-parsed.top.v PASS  no definition named 'Verilog' parsed
  from …` for all twelve mappings. In the current source that check is
  `check(name, !shipped.isEmpty(), shipped.isEmpty() ? "no definition named …" : shipped)`: the
  "no definition …" detail is reachable only when `shipped` is empty, and in that case the result is
  `FAIL`, not `PASS`. The line the log contains is therefore impossible for the code in the tree.
  The log's mtime (01:55:04) precedes the selftest source's last edit (01:55:31); `logs/10-final-green.log`
  (01:56:22) contains zero `definition-parsed.*` lines and starts in the middle of the check list
  (at `construct.vhdl-bit-literal`, after 58 preceding checks), i.e. it is an excerpt of the earlier
  run, not a fresh run. `logs/10` also has an empty `=== build ===` section and a ctest line "75% tests passed,
  1 tests failed" under a "final green" name.
- **Why it matters:** the evidence is the acceptance basis; a log that cannot have been produced by
  the reviewed source does not certify the reviewed source. I independently reran the binary built
  at 02:17 against the same AppDir (`--appdir target/a3-appdir/AppDir`, `LD_LIBRARY_PATH` pointing
  at a directory containing only the AppDir's `libKF6SyntaxHighlighting.so.6`): it prints the real
  `definition-parsed.*` paths and `HIGHLIGHT-MODE-SELFTEST mode=appdir checks=86 failures=0`. The
  behaviour now holds, but the delivered logs do not show it and the stale `definition-parsed` check
  was vacuous in the run they do show.
- **Direction of a fix:** rebuild the final source, rerun the AppDir assertion unedited, and replace
  logs 09/10 with the complete command output (including `MODE-FACT` and every check). Do not
  concatenate or truncate a run and call it final; record the command, binary sha256 and source
  revision in the log header.

### Medium 4 — the F2 user-level isolation has no negative control

- **Where:** `packaging/runtime/verify-f2-isolation.sh`, `run_probe` and `check_reverse`; the
  "402 host + 1 user" wording in `packaging/runtime/README.md` and
  `docs/handover/03-architecture.md`.
- **What:** as above, `XDG_DATA_HOME` is pinned to `/app/usr/share` in both the positive and reverse
  probes, so `UserLeakProbe` is invisible by construction. The reverse control only adds `/usr/share`
  to `XDG_DATA_DIRS`; it never restores `$HOME/.local/share`, so a broken `XDG_DATA_HOME` narrowing
  (the user-override path) is not exercised. The 406 count is 402 host + 5 whitelist − 1 shared name,
  not "402 host + 1 user".
- **Why it matters:** the product's own handover records that setting only `XDG_DATA_DIRS` leaves
  the user directory visible (`defs=2` including `UserLeakProbe`); this is a real isolation dimension
  and the script claims to test it.
- **Direction of a fix:** add a reverse probe with `XDG_DATA_HOME=/root/.local/share` (and narrowed
  `XDG_DATA_DIRS`) and assert `UserLeakProbe` becomes visible; correct the 406 explanation in both
  documents.

### Medium 5 — the `vedit-app-syntax-startup` SEGFAULT attribution is not proven (and the test is weaker than claimed)

- **Where:** `src/syntax/product/CMakeLists.txt`, function `vedit_syntax_link_product`, property
  `VEDIT_SYNTAX_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../../resources`; generated
  `target/qt6-build/CTestTestfile.cmake`; `src/syntax/product/syntax_product.cpp`,
  `privateSyntaxDir()`; `evidence/ux-batch-2/a3/README.md` §4.0 and
  `evidence/ux-batch-2/a3/logs/14-app-syntax-startup-rerun.log`.
- **What:** the deferred `cmake_language(DEFER … CALL vedit_syntax_link_product)` evaluates
  `CMAKE_CURRENT_SOURCE_DIR` at the top-level call site, so the generated test environment contains
  `VEDIT_SYNTAX_DIR=/vedit-build/../../../resources` = `/resources`, not the workspace's
  `resources/`. `syntax_product.cpp` uses the non-empty environment value without a fallback;
  `KsyntaxData` reports the private directory missing and the test's `PASS_REGULAR_EXPRESSION`
  only requires `syntax-product: grammar=verilog`, not `syntax_data_ok` or the definition set.
  The app test therefore does not load A3's XMLs, which makes it unlikely that the new definitions
  caused the SEGFAULT. But the author's exclusion is still too strong: the README cites a
  `--selfcheck` output that is not in the evidence, the app binary's mtime at the time of the
  passing run is not recorded, `highlight_mode.cpp`/`ksyntax_data.cpp` are compiled into the app
  when it is rebuilt, and the SEGFAULT has no backtrace or core. The `APP-SHELL-SELFTEST ok=0
  failures=2` message does point at app-shell code from another lane, but it does not locate the
  crash.
- **Why it matters:** the task asks whether A3 gave enough evidence to exclude its own involvement.
  It did not: the attribution is plausible (and the XMLs are very likely uninvolved because the test
  never loads them), but "not this lane" is not established for the crash. My own attempt to run
  the current `vedit-app --selfcheck` on this host timed out (exit 124 after 180 s, only the
  `propagateSizeHints`/canberra warnings), so I could not obtain a backtrace either.
- **Direction of a fix:** capture a backtrace/core of the failing run, record the app binary's
  build time/sha256 at the passing and failing runs, and run the test on a tree with A3's changes
  reverted (or on a clean worktree at the A3-only revision). Fix the `VEDIT_SYNTAX_DIR` property to
  `${CMAKE_SOURCE_DIR}/resources` and assert `syntax_data_ok` and the five definition names, so the
  test actually covers the delivered definitions. Until then, report the failure as unattributed.

### Nit 6 — `README.md` §0 says "ctest 全绿", which the delivered logs contradict

- **Where:** `evidence/ux-batch-2/a3/README.md` §0; `logs/10-final-green.log` (75%, one failure,
  `vedit-edit-core-selftest` BAD_COMMAND) and `logs/13-ctest-final-clean.log` (5/6, the SEGFAULT).
- **Why it matters:** the details are disclosed in §4.0/§2c, but the one-line conclusion is what a
  reader (and a future verifier) will quote. "ctest 全绿" is false for both delivered runs.
- **Direction:** say "the highlight-mode and syntax ctests pass; the app-syntax-startup test is red
  in the shared tree and is reported as outside this lane".

### Nit 7 — wrong cross-reference for the upstream sha256s

- **Where:** `resources/syntax/README.md` (Provenance paragraph) points to
  `THIRD_PARTY_NOTICES.md` §9.4; the A3 table is in §9.1.1, while §9.4 is "范围与复核".
- **Direction:** point to §9.1.1 (or move the table).

### Nit 8 — the VHDL upstream-licence premise is contradicted by the repository's own evidence

- **Where:** `THIRD_PARTY_NOTICES.md` §9.1.1 and `resources/syntax/README.md` say KDE `vhdl.xml` has
  no licence statement and is therefore "unusable"; `evidence/ux-batch-2/a3/raw/debian-kf6-copyright.txt`
  records the same file in the MIT block, and the upstream README says contributions are MIT.
- **Why it matters:** the shipped files are self-authored and their LGPL-2.1-or-later declaration is
  correct, so this is not a misdeclared licence. But the legal rationale is inaccurate: it treats a
  file as unlicensed when the distribution's own copyright file attributes it to MIT. A future
  maintainer could repeat the reasoning on a file where the conclusion matters.
- **Direction:** cite the Debian MIT attribution (or state that the file-level header is missing but
  the distribution records MIT) and explain why self-authoring was still chosen; do not call the file
  unlicensed.

### Nit 9 — two extension tables can drift

- **Where:** `src/editor/kernel/highlight_mode.cpp` (`kExtensionTable`) and
  `src/syntax/kde/ksyntax_data.cpp` (`KsyntaxData::languageNameForExtension`).
- **Why it matters:** both encode the same 15 suffixes and language names; no test compares them
  across all suffixes (the syntax-data validator checks only `.v`). A future alias added to one
  path silently diverges between the editor and the breadcrumb/syntax-data path.
- **Direction:** make one table the source of truth or add a cross-check that iterates both and
  asserts equality for the contract's extensions.

### Nit 10 — the VHDL folding rule is unbalanced for labelled processes

- **Where:** `resources/syntax/vedit-vhdl.xml`, the `beginRegion`/`endRegion` `RegExpr` rules in the
  `Normal` context; `docs/USER_GUIDE.md` says `entity`/`architecture`/`process` fold.
- **Why it matters:** the opener is `^\s*(entity|architecture|…|process|…)`, so a legal
  `p1: process (clk) is` line does not open a region, while `^\s*end\b` still closes one on
  `end process;`. The README's claim "不折叠而不是错折叠" is therefore not guaranteed. The folding
  assertion only reports `folds=1/0` for a one-line sample and does not cover a labelled process.
- **Direction:** allow an optional label before the opener, or make the `end` rule consume the
  construct word; add a labelled-process folding case.

### Nit 11 — the new `embargo_misfire` branch is not exercised

- **Where:** `src/syntax/kde/ksyntax_data.cpp`, the `embargo_misfire` flag; the author admits in
  `evidence/ux-batch-2/a3/README.md` §4.3 that the branch cannot be triggered without keeping a
  banned file.
- **Why it matters:** the contract required an assertion that the three new files are not
  false-rejected; the positive all-files scan (`data.no-forbidden-kde-definitions`, five private
  files, `forbidden-none`) does cover them, but the new branch itself has no negative control.
- **Direction:** if a negative control is impossible under the licence rules, record that in the
  gate documentation and keep the positive all-files predicate; otherwise inject the condition at
  the unit-test seam.

## Declined to judge (with reason on the same line)

- **Licence honesty of the three shipped files** — holds: no verbatim shared comments, no shared
  regex `String=` literals, structurally different context graphs, and the VHDL keyword list
  contains words absent from the upstream union, so it is not a subset transcription; the
  "adapted → upstream licence" clause is stated in both the README and §9.1.1; the upstream
  originals are evidence-only in `raw/` and are not installed by `assemble-appdir.sh`.
- **Keyword-inventory overlap** — the VHDL set shares 97.5% of its tokens with the upstream token
  union and the Tcl set 69.9%; these are language vocabularies (facts), and the shipped-only words
  show independent assembly; not treated as copying.
- **"Raw upstream files are redistributed"** — they are untracked under `evidence/…/raw/`, not in the
  AppDir; if the evidence directory is committed, record the upstream licences alongside them.
- **Construct assertions on the "language-name-only" probe copies** — equivalent for the rules:
  `writeRenamedLanguage` changes only the `<language name>` value, the rules are byte-identical, and
  the AppDir run uses the real names; I verified the latter independently (86/0).
- **Table-first and repository fallback** — holds: the table returns `Verilog`/`SystemVerilog`/
  `VHDL`/`Markdown`/`Tcl` before `definitionForFileName`, so `.v/.vh/.vlg/.sv/.svh` cannot reach the
  host's `V`, and `.sdc/.xdc → Tcl` is genuine (decoy test plus AppDir run); the ctest decoy claims
  `*.sdc` and the product still returns `Tcl`.
- **NOT WEAKENED** — the `ksyntax_data.cpp` embargo is still the two upstream hashes and the five
  shipped files pass `isForbiddenDefinitionFile()==false`; the F2 script's hard-coded `defs=2` was
  replaced by a whitelist-derived count (per contract), not by a weaker assertion; no test or gate
  was deleted. The `embargo_misfire` gap and the vacuous duplicate check are reported above.
- **The `vedit-app-syntax-startup` SEGFAULT root cause** — cannot decide; missing a backtrace/core,
  the app binary build time at the passing/failing runs, and a run with A3 reverted. The test's
  wrong `VEDIT_SYNTAX_DIR` makes it unlikely the XMLs are involved; the crash remains unattributed.
- **Real GUI appearance / colours / folding on a real screen** — not judged; the author discloses
  no Xvfb/xcb screenshot, and offscreen format ranges do not prove visual correctness.
- **Upstream provenance beyond the recorded hashes** — I did not fetch the KDE v6.24.0 files; the
  `raw/` hashes match the README and the in-file licence attributes match the provenance record.
- **The exact upstream file being MIT** — noted as Nit 8; it does not change the shipped licence.
- **The Rust ABI / other lanes' dirty tree** — not reviewed; the worktree contains many unrelated
  modifications (`src/app/**`, `src/workspace/settings/**`, Rust crates), and the shared tree is
  still moving.
- **`docs/handover/04-hard-facts.md` still says `defs=2`** — outside A3's write scope and already
  reported by the author; not counted against A3.
- **`.sdc`/`.xdc` absent from the Tcl `extensions=` attribute** — deliberate and required to make
  the table-first behaviour observable; not a defect.

## Bounds of the review

- **Read:** the frozen contract (A3, §1, §2), the A3 README/provenance/logs 01–14, all three shipped
  XMLs and the three `raw/` upstream originals, `highlight_mode.cpp`, `highlight_mode_selftest.cpp`,
  `ksyntax_data.cpp` and `src/syntax/kde/tests/validate_syntax_data.cpp`, `verify-f2-isolation.sh`,
  `assemble-appdir.sh` (syntax/whitelist sections), `packaging/runtime/AppRun` (`ksh_guard`),
  `selfcheck.sh`, `build.sh`, the A3 diffs in `src/editor/CMakeLists.txt`, `THIRD_PARTY_NOTICES.md`,
  `resources/syntax/README.md`, `docs/USER_GUIDE.md`, `docs/handover/03-architecture.md`, the pinned
  KSyntaxHighlighting 6.24.0 `src/lib/repository.cpp`, and the generated ctest properties.
- **Ran:** the current `vedit-highlight-mode-selftest --appdir target/a3-appdir/AppDir` with
  `LD_LIBRARY_PATH` containing only the AppDir `libKF6SyntaxHighlighting.so.6` (86 checks, 0
  failures, `definition-parsed` paths printed); the host-locale `sort` comparison against the
  packaged whitelist (fails under `zh_CN.UTF-8`, passes under `LC_ALL=C`); an attempted
  `AppRun --selfcheck` (the guard's probe hung on this host, so the guard line was not reached;
  the comparison is nevertheless deterministic and corroborated by the author's log 05); an
  attempted `vedit-app --selfcheck` (timed out after 180 s, exit 124, no crash/backtrace).
- **Did not run:** the full ctest suite — the ctest-mode selftest binary was built with
  `VEDIT_SYNTAX_RESOURCES_DIR=/vedit-build/…`, which does not exist on this host, so it refuses to
  build its probe share; I did not rebuild in the Docker image. Did not run `verify-f2-isolation.sh`
  end to end (Docker) — the 406 count and the script's logic were checked statically together with
  the author's logs. Did not fetch the upstream KDE files over the network, did not run a GUI, and
  did not review the Rust ABI or the other lanes' changes.

## Verdict

**FAIL** — the definitions and the table-first mapping are sound and pass an independent AppDir
rerun, but the packaged AppRun's whitelist guard fails on the primary `zh_CN.UTF-8` locale because
A3 introduced the `VHDL`/`Verilog` collation pair, and the reworked F2 "non-shadowing" gate cannot
detect the same-name shadow it claims to prove; the delivered AppDir evidence also predates the
final selftest source.
