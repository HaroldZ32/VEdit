# src/syntax/kde — private-path KSyntaxHighlighting data (T16)

Loads the base highlighter's definitions/themes from the AppDir's private data
directory instead of the host system, and ships our own LGPL Verilog /
SystemVerilog definitions (with folding regions) — never KDE's files.

## Strategy (C6)

Three mechanisms were considered; the loader uses **`Repository::addCustomSearchPath()`**
and deliberately does not use the other two:

| mechanism | why not / why |
|---|---|
| `Repository::addCustomSearchPath(<dir>)` | **chosen**: runtime, per-repository, revertible, works with the packaging `--syntax <dir>` slot (T11), and leaves developer/user definitions intact |
| `XDG_DATA_DIRS` | rejected: mutating a process-global env var leaks into unrelated child processes and is not scoped to this repository |
| `-DNO_STANDARD_PATHS` (compile-time) | rejected: needs a rebuilt library (T60) and, in development, silently removes user definitions; it also cannot be switched per document |

`addCustomSearchPath(dir)` loads definitions from `dir + "/syntax"`, so the data
contract is:

```
repo:   resources/syntax/*.xml
shipped: <AppDir>/share/ksyntaxhighlighting/syntax/*.xml
VEDIT_SYNTAX_DIR = <AppDir>/share/ksyntaxhighlighting
loader: addCustomSearchPath(VEDIT_SYNTAX_DIR)
```

This matches `packaging/runtime/assemble-appdir.sh` (`--syntax <dir>`, T11) and
the AppRun-set `VEDIT_SYNTAX_DIR`; this lane does not edit packaging scripts.

## Data files (`resources/syntax/`)

| file | language name | folding regions (A4) |
|---|---|---|
| `vedit-verilog.xml` | `Verilog` | module/begin/case/function/task/generate |
| `vedit-systemverilog.xml` | `SystemVerilog` | the same + interface/program/class/package |

Both are our own LGPL-2.1-or-later definitions (semantic reference: GtkSourceView
4.6.1's LGPL `verilog.lang`/`systemverilog.lang`; no KDE file was copied). The
file names deliberately differ from the prohibited KDE names; the loader also
compares sha256 against the two forbidden KDE definitions and reports a hit
instead of loading it.

## The `.v` trap

`definitionForFileName("x.v")` resolves to the built-in **"V"** (vlang)
definition, not Verilog. The loader therefore never relies on file-name
resolution: `KsyntaxData::languageNameForExtension()` maps
`.v/.vh/.vlg → Verilog` and `.sv/.svh → SystemVerilog`, and the report records
the trap (`probe_v_definition = V`) so this cannot regress silently.

## SystemVerilog degradation (C7)

If our SystemVerilog definition is missing from the private directory, `.sv`
files fall back to the `Verilog` definition (`resolveLanguageForFile()`), and a
diagnostic explains the loss of SystemVerilog-only constructs — no crash, no
silent use of the GPL KDE definition that the distro library may embed.

## Build / test

```sh
cmake -S src/syntax/kde -B target/syn-kde -G Ninja
cmake --build target/syn-kde
ctest --test-dir target/syn-kde --output-on-failure
# or: target/syn-kde/vedit_syntax_kde_selftest   (full LOAD/DIAG/CHECK output)
```

Root build integration (out of this lane's write scope, same pattern as
`src/app`):

```cmake
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/syntax/kde/CMakeLists.txt")
  add_subdirectory(src/syntax/kde)
endif()
```
