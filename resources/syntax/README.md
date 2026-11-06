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
reference only. The A3 languages were cross-checked the same way against KDE
`syntax-highlighting` v6.24.0 upstream (feature reference only; the sha256 of each
reference file is registered in `THIRD_PARTY_NOTICES.md` §9.1.1).

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

Evidence: `evidence/syn-kde-data/` (`01-load.log`, `02-degradation.log`,
`03-folding.log`, `04-licensing.txt`) for T16; `evidence/ux-batch-2/a3/` for A3.
