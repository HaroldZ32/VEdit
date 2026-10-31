# resources/syntax — VEdit's own base syntax data (T16)

These are **our own** KSyntaxHighlighting definitions, licensed
**LGPL-2.1-or-later** (see `LICENSE-LGPL-2.1`):

- `vedit-verilog.xml` → `<language name="Verilog">`
- `vedit-systemverilog.xml` → `<language name="SystemVerilog">`

Provenance: authored for VEdit for this task. The feature set (keywords, block
structure, folding constructs) was derived from the product's Verilog/
SystemVerilog requirements and cross-checked against the LGPL GtkSourceView
4.6.1 `verilog.lang` / `systemverilog.lang` (pinned in `scripts/versions.env`).
**No KDE syntax definition was copied**: KDE's `verilog.xml` /
`systemverilog.xml` are GPL-3.0-or-later / licence-unstated and must not ship
(enforced by `packaging/runtime/verify-not-shipped.sh` and by
`KsyntaxData::isForbiddenDefinitionFile()`).

File names deliberately differ from `verilog.xml` / `systemverilog.xml`.

## Folding (A4 user decision)

`vedit-systemverilog.xml` carries `<keyword ... beginRegion/endRegion>` pairs for
`module`, `interface`, `program`, `class`, `function`, `task`, `generate`,
`case`/`casex`/`casez`/`randcase`, `begin`/`end` (plus `package`). These regions
are independent from tree-sitter's `folds.scm`: with tree-sitter switched off,
`.sv` still folds through this definition.

Evidence: `evidence/syn-kde-data/` (`01-load.log`, `02-degradation.log`,
`03-folding.log`, `04-licensing.txt`).
