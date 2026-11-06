# U4b — assert the RESOLVED definition names, and stop masking a failing selfcheck

Task: close the two findings of the independent U4 review
(`evidence/ux-batch-2/review-u4-final/REPORT.md`, U4-F1 Medium + U4-F2 Nit).

- **U4-F1**: the shipped syntax-startup test asserted the five shipped XML **file
  names**; a present-but-corrupt file (e.g. `vedit-tcl.xml` = `<syntax></syntax>`)
  stayed green because `KsyntaxData::load()` appends every non-embargoed `*.xml`
  to `private_definition_files` **before/without** resolving it.
- **U4-F2**: `PASS_REGULAR_EXPRESSION` makes CTest ignore the process exit code,
  so a `--selfcheck` that printed the syntax line and then failed was green.

Result: **done**. The corrupt-definition control now FAILS the shipped test
(the load-bearing verification), the normal run passes and prints the five
resolved definition names, and the exit-code hole is closed by requiring the
selfcheck's own `APP-SHELL-SELFTEST ok=1` terminal line in the same regex.

Write scope used: `src/syntax/product/`, `src/syntax/kde/`,
`evidence/ux-batch-2/u4b-final/`. **No** change to `resources/`,
`packaging/`, `build/`, gates, or any generated file in the shared build tree.

## 1. Files changed

| File | Change |
|---|---|
| `src/syntax/kde/ksyntax_data.h` | new `LoadReport::resolved_private_definition_names` |
| `src/syntax/kde/ksyntax_data.cpp` | real XML parse of each private file (`definitionNameInFile`) + repository resolution into the new list; diagnostics for a non-definition file and for a definition the repository takes from a different (embedded) file |
| `src/syntax/product/syntax_product.h` | new `Report::syntax_resolved_definitions` |
| `src/syntax/product/syntax_product.cpp` | `describe()` appends `definitions-ok=[...]` (last field on the line) |
| `src/syntax/product/CMakeLists.txt` | PASS regex requires `definitions-ok=[Markdown,SystemVerilog,Tcl,VHDL,Verilog]` **and** `APP-SHELL-SELFTEST ok=1`; comments corrected (U4's `own-xml=` is file presence, not resolution) |

The `own-xml=` five-file assertion is kept unchanged, as required.

### FIX 1 — resolved definition names

`KsyntaxData::load()` previously identified a private file only by name. It now
(1) parses every private `*.xml` as a definition — well-formed XML, `<language>`
root, non-empty `name`, at least one `<context>` inside `<contexts>` — and
(2) requires the repository to resolve that definition name. Only then does the
name enter `resolved_private_definition_names` (sorted). A file that is present
but corrupt yields no name, so it disappears from `definitions-ok=`.
`private_verilog_shipped` / `private_systemverilog_shipped` now use the same real
parse instead of the old regex.

`describe()` appends the field **last on the line**, after `startup-hook=1`, so
the pre-U4b predicate (which ended at `startup-hook=1`) still matches this line —
that is what makes the negative control below honest: the *old* predicate can be
shown green on the corrupt tree, while the *new* one fails.

### FIX 2 — exit-code anchor

The PASS regex is still one expression (CTest treats a `;`-separated list as OR,
so both anchors must live in ONE regex). CTest's regex engine lets `.` match
newlines — the shipped run itself is the proof: the expression spans the startup
line and the selfcheck's final line. It now requires the startup line **and**
the selfcheck's success line:

```
... startup-hook=1 definitions-ok=\[Markdown,SystemVerilog,Tcl,VHDL,Verilog\].*APP-SHELL-SELFTEST ok=1
```

## 2. Verification (each point quoted)

Commands (pinned image, workspace mounted at `/vedit-build`; build only the
affected targets as instructed):

```
cmake -S /vedit-build -B /vedit-build/target/qt6-build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-Wall -Wextra" \
  -DVEDIT_RUST_FFI_LIB=/vedit-build/target/release/libvd_ffi.a \
  -DVEDIT_RUST_FFI_INCLUDE=/vedit-build/crates/vd-ffi/include      # rc=0, 01-configure.log
cmake --build /vedit-build/target/qt6-build \
  --target vedit_syntax_product_tests vedit-app --parallel $(nproc)  # rc=0, 02-build.log
ctest --test-dir /vedit-build/target/qt6-build \
  -R "^(vedit-app-syntax-startup|vedit-syntax-product-tests)$" -V  # rc=0, 04-two-syntax-tests.log
```

### (2) The normal run passes and prints the five RESOLVED names

`04-two-syntax-tests.log` (exit 0, 2/2):

```
1: syntax-product: grammar=verilog backend=1 registered=[systemverilog,verilog] active='verilog'
   backend-available=1 private-dir='/vedit-build/resources' definitions=452
   own-xml=[vedit-vhdl.xml,vedit-tcl.xml,vedit-systemverilog.xml,vedit-markdown.xml,vedit-verilog.xml]
   themes=30 verilog=1 systemverilog=1 kernel='KTextEditor 6.24.0' embargoed=[] startup-hook=1
   definitions-ok=[Markdown,SystemVerilog,Tcl,VHDL,Verilog]
1/2 Test #1: vedit-app-syntax-startup .........   Passed
2/2 Test #7: vedit-syntax-product-tests .......   Passed
100% tests passed, 0 tests failed out of 2
```

(Line wrapped here; it is one line in the log.)

### (1) The corrupt-definition control now FAILS — load-bearing verification

The reviewer's exact control: five files present, `vedit-tcl.xml` replaced by

```xml
<?xml version="1.0" encoding="UTF-8"?>
<syntax></syntax>
```

in a **temp resources dir** (`target/u4b-probe/broken/resources`, gitignored);
the real `resources/` was never touched (all five XMLs still have their
2026-11-02/05 mtimes). To avoid editing the shared generated test file (Lead's
build-tree notice), the test definition was **copied** from
`target/qt6-build/CTestTestfile.cmake` to `target/u4b-probe/ctest-*/`,
`subdirs()` stripped, and only `VEDIT_SYNTAX_DIR` rewritten. The shipped binary,
the shipped regex and the shipped selfcheck command are otherwise identical.

`07-negative-corrupt-tcl.log`:

```
===== ctest copy: ctest-good =====        (real resources/)
1/1 Test #1: vedit-app-syntax-startup .........   Passed
ctest_rc=0

===== ctest copy: ctest-broken =====      (corrupt vedit-tcl.xml, SHIPPED U4b regex)
1: syntax-product: grammar=verilog backend=1 registered=[systemverilog,verilog] active='verilog'
   backend-available=1 private-dir='/vedit-build/target/u4b-probe/broken/resources' definitions=451
   own-xml=[vedit-vhdl.xml,vedit-tcl.xml,vedit-systemverilog.xml,vedit-markdown.xml,vedit-verilog.xml]
   themes=30 verilog=1 systemverilog=1 kernel='KTextEditor 6.24.0' embargoed=[] startup-hook=1
   definitions-ok=[Markdown,SystemVerilog,VHDL,Verilog]
1/1 Test #1: vedit-app-syntax-startup .........***Failed  Required regular expression not found.
ctest_rc=8

===== ctest copy: ctest-broken-old =====  (corrupt vedit-tcl.xml, U4 regex only)
1/1 Test #1: vedit-app-syntax-startup .........   Passed
ctest_rc=0
```

`definitions-ok=[Markdown,SystemVerilog,VHDL,Verilog]` — `Tcl` is missing because
the file no longer parses as a definition. The third run reproduces the review's
finding (the U4 predicate is green on exactly this corrupt tree), so the failure
in the second run is caused by the new assertion, not by the copy.

The same control with **VHDL** corrupted (`12-negative-corrupt-vhdl.log`) also
fails under the new regex — `definitions-ok=[Markdown,SystemVerilog,Tcl,Verilog]`
— and passes under the U4 regex. So the fix is not Tcl-specific: the real XML
parse catches a corrupt VHDL/Markdown definition even though the distro library
also embeds a definition with that name.

### (3) The exit-code hole is closed

The generated property now contains both anchors (`03-generated-property.txt`,
read from the regenerated `target/qt6-build/CTestTestfile.cmake`):

```
PASS_REGULAR_EXPRESSION "syntax-product: .*private-dir='[^']+/resources' definitions=[0-9]+
 own-xml=\[vedit-(markdown|systemverilog|tcl|verilog|vhdl)\.xml, ...five...\]
 themes=[0-9]+ verilog=1 systemverilog=1 .*startup-hook=1
 definitions-ok=\[Markdown,SystemVerilog,Tcl,VHDL,Verilog\].*APP-SHELL-SELFTEST ok=1"
```

Reviewer's probe reproduced (`09-exit-code-probe.log`): a script prints the real
startup line and `exit 1`; no `APP-SHELL-SELFTEST ok=1`:

```
1/2 Test #1: fake-old-regex ...................   Passed      <- U4 regex: exit code masked
2/2 Test #2: fake-new-regex ...................***Failed     <- U4b regex: anchor absent
ctest_rc=8
```

### (4) The two syntax tests still pass

`04-two-syntax-tests.log`: `2/2`, exit 0 (quoted above).

Extra check of the other targets affected by the loader change (same build tree,
affected targets only): `vedit-syntax-kde-data` and
`vedit-syntax-directory-equivalence` also pass, 2/2, exit 0
(`13-kde-lane-build.log`, `14-kde-lane-tests.log`). The kde selftest output shows
the same four shadowing diagnostics quoted in §3.F1 and its own checks stay
green.

## 3. Findings beyond the task (report, not fixed — outside this write scope)

### F1 (important, pre-existing): four of the five definitions RESOLVE to the library-embedded KDE copies

A standalone probe (`05-definition-probe.log`, `target/u4b-probe/defprobe`) uses
`KSyntaxHighlighting::Repository` exactly like `KsyntaxData`:

```
=== no custom path ===
DEF name=SystemVerilog ... filePath=:/org.kde.syntax-highlighting/syntax/systemverilog.xml
DEF name=Verilog       ... filePath=:/org.kde.syntax-highlighting/syntax/verilog.xml
DEF name=VHDL          ... filePath=:/org.kde.syntax-highlighting/syntax/vhdl.xml
DEF name=Markdown      ... filePath=:/org.kde.syntax-highlighting/syntax/markdown.xml
RESOLVE Tcl valid=0            TOTAL=451
=== with VEDIT_SYNTAX_DIR=/vedit-build/resources ===
DEF name=Tcl           ... filePath=/vedit-build/resources/syntax/vedit-tcl.xml
RESOLVE Verilog/VHDL/Markdown/SystemVerilog -> :/org.kde.../*.xml   TOTAL=452
```

The repository keeps the definition with the **higher `version`** when a name
exists twice (KF6 `Repository` contract), and the distro library embeds KDE's
definitions (ours declare `version="1"`). So our Verilog/SystemVerilog/VHDL/
Markdown files parse (the U4b test validates that) but are shadowed by KDE's
embedded copies; only `Tcl` (no KDE `Tcl` definition in 6.24.0) resolves to our
file. The loader now records one diagnostic per shadowed file, e.g.:

```
DIAGNOSTICS definition 'VHDL' resolves to ':/org.kde.syntax-highlighting/syntax/vhdl.xml'
instead of the shipped file 'vedit-vhdl.xml' (higher-version copy wins inside the repository) | ...
```

This is consistent with `AGENT.md` §9 (the effective highlighting comes from
KDE's definitions), but **contradicts** `src/syntax/kde/README.md` §"SystemVerilog
degradation" ("no silent use of the GPL KDE definition that the distro library
may embed") — that sentence needs a captain/captain-owned correction. Bumping the
shipped `version=` attributes would change which definitions the product uses;
that is a product decision in `resources/syntax/` (outside this task's scope).

### F2 (correction the review asked for): the U4 evidence README's justification is false

`evidence/ux-batch-2/u4-final/README.md` says `own-xml=` lists the definitions
"the loader only reports ... after it resolved" and that the file-name assertion
is "equivalent in force". That is **not** true (the review proved it, and the
`ctest-broken-old` run above reproduces it). That file is outside this task's
write scope, so the correction is recorded here: U4's assertion was **file
presence**, not resolution; U4b adds the resolved-name assertion.

### F3 (for the packaging lane, not this task): the AppImage selfcheck has the same file-presence weakness

`packaging/runtime/selfcheck.sh` asserts the five XML **file names** in
`own-xml=`, so a corrupt definition would still pass the packaged selfcheck.
The startup line now carries `definitions-ok=` (appended last, so the existing
`own-xml=`/`verilog=` parsing there is unaffected), and the packaging lane can
adopt the same five-name assertion.

## 4. Bounds — what is not proven

- The U4b assertion is *"each of VEdit's five shipped files parses as a usable
  definition AND the repository resolves that name"*. It is **not** "the active
  definition object is ours" — because of F1, four names resolve to the embedded
  KDE copies. The test still fails if any of the five files stops parsing as a
  definition (proven for Tcl and VHDL).
- A file that keeps a well-formed `<language name="...">` root with a `<contexts>`
  section but has semantically broken rules *inside* still passes: the
  structural parse is not the KSyntaxHighlighting rule validator.
- `PASS_REGULAR_EXPRESSION` still means CTest ignores the exit code; the anchor
  closes the realistic failure mode (a failing selfcheck prints
  `APP-SHELL-SELFTEST ok=0 ...`, or crashes before printing the ok=1 line), but a
  process that printed `ok=1` and then exited non-zero would still pass. Making
  the exit code authoritative needs a wrapper test (`cmake -P` + `execute_process`)
  or `FAIL_REGULAR_EXPRESSION`, neither of which the task asked for.
- Only the affected targets were built (`vedit_syntax_product_tests`,
  `vedit-app`) and only the two syntax ctests were run; the full
  `build/gates/qt6-cxx.sh` gate is the captain's run.
- The `ctest-broken*` controls use a **copy** of the generated test definition
  (the shared `target/qt6-build/CTestTestfile.cmake` was never hand-edited;
  `10-generated-file-provenance.log` shows a fresh configure rewrites it
  byte-identically, sha256 `4ae24e52…6aff0`).

## 5. Logs in this directory

| File | Content |
|---|---|
| `00-commands-and-exit-codes.txt` | every command run and its observed exit code |
| `01-configure.log` | cmake configure with the new test property (rc=0) |
| `02-build.log` | build of `vedit_syntax_product_tests` + `vedit-app` (rc=0; `ksyntax_data.cpp`/`syntax_product.cpp` compiled) |
| `03-generated-property.txt` | the regenerated `add_test`/`PASS_REGULAR_EXPRESSION` line |
| `04-two-syntax-tests.log` | final run of both syntax tests: 2/2 passed, the five resolved names |
| `05-definition-probe.log` | standalone KF6 `Repository` probe: Tcl resolves to our file, the other four to the embedded KDE copies |
| `06-xml-parse-probe.log` | QXmlStreamReader probe of a shipped definition (structure: `<language><highlighting>…<contexts>`) |
| `07-negative-corrupt-tcl.log` | corrupt `vedit-tcl.xml`: U4b regex **fails**, U4 regex passes |
| `09-exit-code-probe.log` | exit-code probe: old regex Passed on exit 1, new regex Failed |
| `10-generated-file-provenance.log` | the generated test file is exactly what a fresh configure writes (same sha256); never hand-edited |
| `11-scope-check.log` | git status/diffstat: only the five task files + this directory |
| `12-negative-corrupt-vhdl.log` | corrupt `vedit-vhdl.xml`: U4b regex fails, U4 regex passes |
| `13-kde-lane-build.log` | build of the two kde-lane targets the loader change affects (rc=0) |
| `14-kde-lane-tests.log` | `vedit-syntax-kde-data` + `vedit-syntax-directory-equivalence`: 2/2 passed |

All experiments live under `target/u4b-probe/` (gitignored); the real
`resources/` tree was never modified and no generated file was hand-edited.
