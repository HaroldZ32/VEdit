# U4 — syntax startup test integrity (CMake deferred path + PASS regex)

Task: `CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U4.
Write scope: `src/syntax/product/CMakeLists.txt` + this directory.
Result: **done** — one file changed (`src/syntax/product/CMakeLists.txt`).

## 1. Defect (reproduced before the fix)

`vedit_syntax_link_product()` is registered with

```cmake
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL vedit_syntax_link_product)
```

and builds the ctest environment from `${CMAKE_CURRENT_SOURCE_DIR}` **inside that
function**. At the end of the top-level configure `CMAKE_CURRENT_SOURCE_DIR` is
the workspace root, so `.../../../../resources` resolved to `/resources`:

```
VEDIT_SYNTAX_DIR=/vedit-build/../../../resources     (generated property, before)
private-dir='/vedit-build/../../../resources' definitions=451 own-xml=[] verilog=0 systemverilog=0
```

The loader then found no private directory, silently fell back to KDE's own
definitions, and the test still passed because the old PASS condition was

```
PASS_REGULAR_EXPRESSION "syntax-product: grammar=verilog"
```

— the *compile-time* grammar id, which has nothing to do with what the loader
loaded. Baseline (`00-baseline-before-fix*.log`), with **zero** bundled
definitions loaded:

```
1/1 Test #1: vedit-app-syntax-startup .........   Passed    0.71 sec
100% tests passed, 0 tests failed out of 1
```

## 2. Fix (one file: `src/syntax/product/CMakeLists.txt`)

1. **Path**: the resources directory is captured once, at file scope, and
   normalised:

   ```cmake
   set(SYNTAX_PRODUCT_RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../resources")
   cmake_path(ABSOLUTE_PATH SYNTAX_PRODUCT_RESOURCES_DIR NORMALIZE
     OUTPUT_VARIABLE SYNTAX_PRODUCT_RESOURCES_DIR)
   set_property(GLOBAL PROPERTY VEDIT_SYNTAX_PRODUCT_RESOURCES_DIR "${SYNTAX_PRODUCT_RESOURCES_DIR}")
   ```

   A plain variable is **not** enough: directory scope ends with this
   subdirectory, and the deferred call runs in the top-level scope — the first
   attempt produced `VEDIT_SYNTAX_DIR=` (empty), which put the loader back on
   KDE's fallback (`private-dir=''`, still green under the old regex; see the
   session record). The captured path therefore travels as a GLOBAL property,
   and the deferred function **fails the configure** if the property is empty.

2. **PASS condition**: the assertion now requires the load itself and every
   definition VEdit ships. `own-xml=` is the loader's own list of the private
   definitions it loaded, and its order comes from `QDirIterator` (observed
   `vhdl,tcl,systemverilog,markdown,verilog` — not alphabetical), so the entry
   pattern is repeated five times instead of being written in a fixed order:

   ```cmake
   set(_u4_definition "vedit-(markdown|systemverilog|tcl|verilog|vhdl)\\.xml")
   ...
   PASS_REGULAR_EXPRESSION
     "syntax-product: .*private-dir='[^']+/resources' definitions=[0-9]+ own-xml=\\[${_u4_own_xml}\\] themes=[0-9]+ verilog=1 systemverilog=1 .*startup-hook=1"
   ```

   Five entries drawn from exactly these five file names can only be the five
   shipped files (a directory iterator never lists one file twice), so **all
   five definitions must be present**. Fail-closed by design: a sixth shipped
   definition fails the test on purpose — the whitelist is five.

   Definition ↔ file (1:1): `vedit-verilog.xml`→Verilog,
   `vedit-systemverilog.xml`→SystemVerilog, `vedit-vhdl.xml`→VHDL,
   `vedit-markdown.xml`→Markdown, `vedit-tcl.xml`→Tcl.

## 3. Verification (three points, each quoted)

### (1) Generated ctest property now carries the correct absolute path

```
$ grep -n "vedit-app-syntax-startup" -A 1 target/qt6-build/CTestTestfile.cmake
set_tests_properties([=[vedit-app-syntax-startup]=] PROPERTIES  ENVIRONMENT
"QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=/vedit-build/target/qt6-build/xdg-runtime;VEDIT_SYNTAX_DIR=/vedit-build/resources"
PASS_REGULAR_EXPRESSION "syntax-product: .*private-dir='[^']+/resources' definitions=[0-9]+ own-xml=\[vedit-(markdown|systemverilog|tcl|verilog|vhdl)\.xml, ... five ... \] themes=[0-9]+ verilog=1 systemverilog=1 .*startup-hook=1"
```

Full line: `10-generated-property-line-after.txt`. Before: `VEDIT_SYNTAX_DIR=/vedit-build/../../../resources`.

### (2) Test output contains all five definitions

```
$ ctest -R "^vedit-app-syntax-startup$" -V        (after the fix, correct path)
syntax-product: grammar=verilog backend=1 registered=[systemverilog,verilog] active='verilog'
backend-available=1 private-dir='/vedit-build/resources' definitions=452
own-xml=[vedit-vhdl.xml,vedit-tcl.xml,vedit-systemverilog.xml,vedit-markdown.xml,vedit-verilog.xml]
themes=30 verilog=1 systemverilog=1 kernel='KTextEditor 6.24.0' embargoed=[] startup-hook=1
1/1 Test #1: vedit-app-syntax-startup .........   Passed    0.70 sec
```

Log: `03-strict-regex-correct-path.log`, `05-restored-path-passes.log`.

> **Disclosure (interpretation):** the product's startup line identifies the
> five definitions by their shipped XML file names (`own-xml=`) plus the two
> repository name lookups (`verilog=1 systemverilog=1`). It does **not** print
> the literal strings `Verilog`, `SystemVerilog`, `VHDL`, `Markdown`, `Tcl`.
> Printing those would need `vedit::syntax::product::describe()` in
> `src/syntax/product/syntax_product.cpp` to emit `definition_names` — outside
> U4's write scope. The five file names are 1:1 with the five definitions and
> the loader only reports a file after it resolved it as a definition, so the
> assertion is equivalent in force; flagged to the Lead in case literal names
> are required.

### (3) Broken path ⇒ FAIL, restored ⇒ PASS

Path deliberately pointed at `.../../../../resources-does-not-exist`
(`04-broken-path-must-fail.log`):

```
syntax-product: ... private-dir='/vedit-build/resources-does-not-exist' definitions=451
own-xml=[] themes=30 verilog=0 systemverilog=0 ... startup-hook=1
1/1 Test #1: vedit-app-syntax-startup .........***Failed  Required regular expression not found.
0% tests passed, 1 tests failed out of 1
```

Restored, re-configured, rebuilt (`07-affected-targets-build.log`) and re-run
(`05-restored-path-passes.log`):

```
1/1 Test #1: vedit-app-syntax-startup .........   Passed    0.70 sec
100% tests passed, 0 tests failed out of 1
```

## 4. Before / after summary (affected ctest test)

| state | generated `VEDIT_SYNTAX_DIR` | startup line | ctest |
|---|---|---|---|
| before fix | `/vedit-build/../../../resources` | `own-xml=[] verilog=0 systemverilog=0` | **Passed** (weak regex) |
| fix, only the path | `/vedit-build/resources` | 5 definitions, `own-xml=[5 files]` | Passed |
| fix, path broken on purpose | `.../resources-does-not-exist` | `own-xml=[] verilog=0` | **Failed** |
| fix, restored | `/vedit-build/resources` | 5 definitions | **Passed** |

The sibling unit test that shares the same resource path
(`vedit-syntax-product-tests`) still passes after the change
(`08-both-tests.log`: 2/2 tests passed; it now also receives the normalised
path through both its compile-time default and its test environment).

Product selfcheck exercised by the test (`09-selfcheck-full.log`):

```
$ QT_QPA_PLATFORM=offscreen VEDIT_SYNTAX_DIR=/vedit-build/resources vedit-app --selfcheck
selfcheck exit=0
APP-SHELL-SELFTEST ok=1 checks=312 failures=0
```

## 5. Commands used / not used

Re-configure and build ran inside the pinned image (`vedit-qt6-build:local`),
affected targets only:

```
cmake -S /vedit-build -B /vedit-build/target/qt6-build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-Wall -Wextra" \
  -DVEDIT_RUST_FFI_LIB=/vedit-build/target/release/libvd_ffi.a \
  -DVEDIT_RUST_FFI_INCLUDE=/vedit-build/crates/vd-ffi/include
cmake --build . --target vedit_syntax_product_tests vedit-app --parallel $(nproc)
ctest -R "^(vedit-app-syntax-startup|vedit-syntax-product-tests)$"
```

The full `scripts/build-qt.sh` (image rebuild + all ctest + xcb smoke) was
**not** run, per the task instruction; `packaging/runtime/**` and
`packaging/runtime/AppRun` were not touched.

## 6. Note — a concurrent negative-control run in the shared build dir

`target/qt6-build/CTestTestfile.cmake` is a generated file in a build directory
shared with other lanes. Between my runs, a U4 review lane patched that
*generated* file (never the source) to point `VEDIT_SYNTAX_DIR` at
`target/review-u4/empty/resources` / `/vedit-build/resources-review-missing`, to
exercise the negative controls. One of my re-runs therefore picked up their
patched environment (`private-dir='…/review-u4/empty/resources'`, `own-xml=[]`)
and failed — as it should, since that is exactly the broken-path case.

The build dir was restored from that lane's own snapshot
(`target/review-u4/ctest-orig.cmake`, which is byte-identical to what the fixed
configure generates: `VEDIT_SYNTAX_DIR=/vedit-build/resources`) and the test was
re-run; `05-restored-path-passes.log` now carries the property line and the
result inline, so it is self-consistent:

```
PROPERTY: VEDIT_SYNTAX_DIR=/vedit-build/resources
syntax-product: ... private-dir='/vedit-build/resources' definitions=452 own-xml=[five files] ...
1/1 Test #1: vedit-app-syntax-startup .........   Passed    0.74 sec
```

No source file was changed by that lane; `src/syntax/product/CMakeLists.txt`
kept its 10:03 state (this fix) throughout.

