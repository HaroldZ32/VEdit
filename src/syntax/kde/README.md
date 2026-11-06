# src/syntax/kde — private-path KSyntaxHighlighting data (T16, extended by A3)

Loads the base highlighter's definitions/themes from the AppDir's private data
directory instead of the host system, and ships our own LGPL definitions for
Verilog / SystemVerilog / VHDL / Markdown / Tcl (with the folding regions each
language supports) — never KDE's files.

## Strategy (C6)

Three mechanisms were considered; the loader uses **`Repository::addCustomSearchPath()`**
and deliberately does not use the other two:

| mechanism | why not / why |
|---|---|
| `Repository::addCustomSearchPath(<dir>)` | **chosen**: runtime, per-repository, revertible, works with the packaging `--syntax <dir>` slot (T11), and leaves developer/user definitions intact |
| `XDG_DATA_DIRS` | rejected: mutating a process-global env var leaks into unrelated child processes and is not scoped to this repository |
| `-DNO_STANDARD_PATHS` (compile-time) | rejected: needs a rebuilt library (T60) and, in development, silently removes user definitions; it also cannot be switched per document |

`addCustomSearchPath(dir)` loads definitions from `dir + "/syntax"`, so the data
contract is (T84/T110: the exported value is the DATA ROOT; the loader also
accepts the legacy `syntax/` subdirectory spelling):

```
repo:   resources/syntax/*.xml
shipped: <AppDir>/usr/share/org.kde.syntax-highlighting/syntax/*.xml
VEDIT_SYNTAX_DIR = <AppDir>/usr/share/org.kde.syntax-highlighting
loader: addCustomSearchPath(VEDIT_SYNTAX_DIR)
```

This matches `packaging/runtime/assemble-appdir.sh` (it installs the XML files
under `$OUT/usr/share/org.kde.syntax-highlighting/syntax/` and fails the package
when one of the five required definitions is missing) and the AppRun-set
`VEDIT_SYNTAX_DIR`; this lane does not edit packaging scripts.

## Data files (`resources/syntax/`)

| file | language name | extensions | folding regions |
|---|---|---|---|
| `vedit-verilog.xml` | `Verilog` | `.v .vh .vlg` | module/begin/case/function/task/generate |
| `vedit-systemverilog.xml` | `SystemVerilog` | `.sv .svh` | the same + interface/program/class/package |
| `vedit-vhdl.xml` (A3) | `VHDL` | `.vhd .vhdl` | entity/architecture/package/configuration/component + labelled process/block/function/procedure |
| `vedit-markdown.xml` (A3) | `Markdown` | `.md .markdown .mdown .mkd` | none (deliberate: Markdown has no block keyword structure worth a fold region) |
| `vedit-tcl.xml` (A3) | `Tcl` | `.tcl .tk` | none (deliberate: a fold region guessed from braces would be wrong more often than right) |

`.sdc` / `.xdc` deliberately have **no definition of their own**: the product's
extension table (`src/editor/kernel/highlight_mode.cpp`) maps them to `Tcl` (a
Synopsys SDC / Vivado XDC file is Tcl), which is what keeps the table-first rule
observable in the A3 test.

All five are our own LGPL-2.1-or-later definitions. Verilog/SystemVerilog are a
semantic port of GtkSourceView 4.6.1's LGPL `verilog.lang`/`systemverilog.lang`;
the A3 languages were authored for VEdit with KDE `syntax-highlighting` 6.24.0
used as a feature reference only (provenance, reference URLs and sha256 in
`THIRD_PARTY_NOTICES.md` §9.1/§9.1.1 and `resources/syntax/README.md`). The file
names deliberately differ from the prohibited KDE names; the loader also compares
sha256 against the **two** forbidden KDE definitions (`verilog.xml`,
`systemverilog.xml`) and reports a hit instead of loading it. A3 scopes that
embargo to those two files only, so the three definitions added since then are
never caught by it.

## The `.v` trap

`definitionForFileName("x.v")` resolves to the built-in **"V"** (vlang)
definition, not Verilog. The loader therefore never relies on file-name
resolution: `KsyntaxData::languageNameForExtension()` maps
`.v/.vh/.vlg → Verilog`, `.sv/.svh → SystemVerilog`, `.vhd/.vhdl → VHDL`,
`.md/.markdown/.mdown/.mkd → Markdown`, `.tcl/.tk/.sdc/.xdc → Tcl`, and the
report records the trap (`probe_v_definition = V`) so this cannot regress
silently. The editor kernel's explicit table is consulted first; this loader-side
map is its mirror (the A3 drift test compares both).

## SystemVerilog degradation (C7)

If our SystemVerilog definition is missing from the private directory, `.sv`
files fall back to the `Verilog` definition (`resolveLanguageForFile()`), and a
diagnostic explains the loss of SystemVerilog-only constructs — no crash. Which
definition is **active** when both our file and the library-embedded KDE copy
exist is a separate, measured question — see the section below.

## Which definition is ACTIVE — build/run environment vs packaged AppDir

Two different cases, measured separately. Do not mix them, and do not claim one
from the other:

**A. Build tree / host that also carries the KDE definitions (the measured case
here).** `KsyntaxData` parses every shipped `resources/syntax/vedit-*.xml` as a
definition and asks the repository to resolve it by name. The KF6 library in the
build/run image embeds KDE's own `verilog`, `systemverilog`, `vhdl` and
`markdown` definitions, and the repository keeps the **higher `version`** copy
when a name exists twice (ours declare `version="1"`). Measured by
`evidence/ux-batch-2/u4b-final/05-definition-probe.log`:

```
DEF name=Verilog       -> :/org.kde.syntax-highlighting/syntax/verilog.xml
DEF name=SystemVerilog -> :/org.kde.syntax-highlighting/syntax/systemverilog.xml
DEF name=VHDL          -> :/org.kde.syntax-highlighting/syntax/vhdl.xml
DEF name=Markdown      -> :/org.kde.syntax-highlighting/syntax/markdown.xml
DEF name=Tcl           -> /vedit-build/resources/syntax/vedit-tcl.xml   (KDE 6.24 has no Tcl)
```

So in this case all five files ship, parse and resolve **by name**, but the
ACTIVE definition is KDE's embedded copy for verilog / systemverilog / vhdl /
markdown; only Tcl is ours. The loader records one diagnostic per shadowed
file. This matches `AGENT.md` §9 (the effective highlighting comes from KDE's
definitions).

**B. The packaged AppDir (the delivered form).** `AppRun` narrows
`XDG_DATA_DIRS`/`XDG_DATA_HOME` to the AppDir, so only
`<AppDir>/usr/share/org.kde.syntax-highlighting/syntax/` is visible and all five
names win from there. `evidence/ux-batch-2/a3-repair/11-f2-isolation.log`
asserts the positive case plus host-leak / user-leak / higher-version-shadow
negative controls (`summary: pass=11 fail=0`). That evidence covers the
**packaged** form only; it does not change case A.

**Open item (product decision, NOT done in UX batch 2):** making our file the
active definition in case A is a `version` attribute bump in
`resources/syntax/*.xml` (e.g. `version="2"`), which changes which definition
the product uses. That is a product decision outside this lane; it is not done
here.

The sha256 embargo described above is a separate guard: it stops the loader from
loading the two forbidden KDE files *from the private directory*. It cannot
change what the library resolves among its own embedded definitions.

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
