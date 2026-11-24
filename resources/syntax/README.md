# resources/syntax — VEdit's own base syntax data (T16, extended by A3)

These are **our own** KSyntaxHighlighting definitions, licensed
**LGPL-2.1-or-later** (see `LICENSE-LGPL-2.1`):

| File | `<language name>` | `extensions=` | Since |
|---|---|---|---|
| `vedit-verilog.xml` | `Verilog` | `*.v;*.vh;*.vlg` | T16 |
| `vedit-systemverilog.xml` | `SystemVerilog` | `*.sv;*.svh` | T16 |
| `vedit-vhdl.xml` | `VHDL` | `*.vhd;*.vhdl` | A3 (UX batch 2) |
| `vedit-markdown.xml` | `Markdown` | `*.md;*.markdown;*.mdown;*.mkd` | A3 |
| `vedit-tcl.xml` | `Tcl` | `*.tcl;*.tk` | A3 |
| `vedit-scala.xml` | `Scala` | `*.scala;*.sbt` | T17 (batch 20261110b) |

`.sdc` / `.xdc` deliberately have **no definition file of their own**: the
product's extension table (`src/editor/kernel/highlight_mode.cpp`) maps them to
`Tcl` (a Synopsys SDC / Vivado XDC file is Tcl). Keeping them out of the Tcl
definition's `extensions=` attribute is what makes the table-first rule
observable in the A3 test: a decoy definition that *does* claim `*.sdc` must not
steal the file from the table.

Provenance: authored for VEdit. The feature set (keywords, block structure,
folding constructs) was derived from the product's Verilog/SystemVerilog
requirements and cross-checked against the LGPL GtkSourceView 4.6.1
`verilog.lang` / `systemverilog.lang` (pinned in `scripts/versions.env`), feature
reference only. The A3 and T17 languages were cross-checked the same way against
KDE `syntax-highlighting` v6.24.0 upstream (feature reference only; the sha256 of
each reference file is registered in `THIRD_PARTY_NOTICES.md` §9.1.1 and §9.1.2).

**No KDE syntax definition was copied**, for two independent reasons:

1. **Licence.** KDE's `verilog.xml` states no licence (`license=""`) and its
   `systemverilog.xml` is GPL-3.0-or-later — both are banned from the deliverable
   (`packaging/runtime/verify-not-shipped.sh` and
   `KsyntaxData::isForbiddenDefinitionFile()`). The A3 references are no cleaner
   as *copies*: `markdown.xml` is dual `GPL,BSD` (the BSD variant is not named)
   and `tcl.xml` is `BSD`. `vhdl.xml` carries **no licence statement inside the
   file**, but it is *not* unlicensed: the distribution's own copyright file
   (`evidence/ux-batch-2/a3/raw/debian-kf6-copyright.txt`) lists
   `data/syntax/vhdl.xml` in its **MIT** block and the upstream README says
   contributions are MIT. Self-authoring was therefore chosen because there is no
   file-level licence text to carry with a copy (and because of the naming reason
   below), **not** because the file is unlicensed.
   If a file here is ever changed to be *adapted from* an upstream definition,
   that file must be re-registered under the upstream licence (`tcl.xml` BSD,
   `markdown.xml` GPL/BSD, `vhdl.xml` MIT) — the self-authored LGPL below applies
   only to text written for VEdit.
2. **Ownership of the definition name.** The product sets the mode by *definition
   name* (`modeFromExtension` → `VHDL` / `Markdown` / `Tcl`), and A3's acceptance
   asserts exactly those names; upstream's Tcl definition is named `Tcl/Tk`, so a
   copy would have to be modified anyway.

File names deliberately differ from `verilog.xml` / `systemverilog.xml` / …, so
a hash or name scan can never confuse a shipped file with a forbidden one.

## Folding (A4 user decision)

`vedit-systemverilog.xml` carries `<keyword ... beginRegion/endRegion>` pairs for
`module`, `interface`, `program`, `class`, `function`, `task`, `generate`,
`case`/`casex`/`casez`/`randcase`, `begin`/`end` (plus `package`). These regions
are independent from tree-sitter's `folds.scm`: with tree-sitter switched off,
`.sv` still folds through this definition.

`vedit-vhdl.xml` folds `entity`/`architecture`/`package`/`configuration`/
`component` regions, plus the constructs that may carry a label
(`process`/`block`/`function`/`procedure`, e.g. `p1: process (clk) is`), all
closed by `end`. `generate`/`record` are **not** fold regions (the opener regex
never matched them; an unlabelled `for … generate` would need its own rule).
`vedit-markdown.xml` and `vedit-tcl.xml` define **no** fold
regions: Markdown headings and Tcl braces are not reliable block structure (a
brace is data in Tcl as often as it is a body), so a fold region guessed from
them would be wrong more often than right.
`vedit-scala.xml` folds `{` / `}` as **one brace region** (open on `{`, close on
`}`), which stays balanced for single-statement bodies and cannot be unbalanced
by a keyword repeating inside its own block; braces inside strings and comments
are separate contexts and are not counted. It does **not** invent
keyword-anchored regions for `class`/`object`/`def`: Scala's block delimiter is
the brace, not the declaration keyword.

## Effective definition (AC-7.8 — do not overclaim)

`AGENT.md` §9 records the measured fact that KTextEditor's
`KSyntaxHighlighting::Repository` resolves `Verilog` / `SystemVerilog` to KDE's
**own** definitions, and that `addCustomSearchPath()` / a private XDG directory
do not change what the editor renders with **when the library has KDE's
definitions embedded**. That form is the one most developers run and every ctest
run uses. The same applies to `Scala` there: shipping a well-formed
`vedit-scala.xml` proves the file is *installed and loadable*, **not** that it is
the renderer in that form.

The batch evidence measures the actual winner per form with the product's own
`vedit-ksh-env-probe` (it prints every loaded definition's winning
`Definition::filePath()`, including KTextEditor's own repository via
`--editor-repo`):

- distro KSyntaxHighlighting (the ctest/dev build form, the library embeds KDE's
  definitions): `Scala` wins from the **library-embedded**
  `:/org.kde.syntax-highlighting/syntax/scala.xml` — KDE's definition, not ours —
  in BOTH the plain repository and KTextEditor's repository;
- self-built F2 library (`QRC_SYNTAX=OFF`, the delivered form) with
  `XDG_DATA_DIRS` narrowed to the private data root: `Scala` wins from **our**
  `vedit-scala.xml` in BOTH repositories (the editor repository then holds
  exactly the six whitelist definitions, `defs=6`), and the product's own
  `vedit_syntax_product_tests` measures `format-ranges=4` with
  `definition-file=<repo>/resources/syntax/vedit-scala.xml`.

`AGENT.md` §9 records the **embedded-library** form; this batch additionally
measures the F2 delivered form, where a definition that is absent from the
private root has no fallback at all (AppRun narrows the data dirs), so the file
shipped here is what the editor resolves. The product's private loader
(`KsyntaxData`, a custom search path) also resolves `Scala` from our file. What
is NOT claimed: that this file renders in the dev/ctest form, where KDE's
definition wins the name.

Evidence: `evidence/syn-kde-data/` (`01-load.log`, `02-degradation.log`,
`03-folding.log`, `04-licensing.txt`) for T16; `evidence/ux-batch-2/a3/` for A3;
`evidence/batch-20261110b/scala-syntax/` for T17 (Scala).
