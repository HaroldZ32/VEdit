# Provenance — tree-sitter-json (vendored)

- **Upstream**: https://github.com/tree-sitter/tree-sitter-json
- **Pinned commit**: `254c42a6476413b776221e03982ac8ae159eeb72`
  (`Mon Aug 17 20:29:06 2026 +1000`, "test: add exponent tests")
- **Licence**: MIT (`LICENSE`, Copyright (c) 2014 Max Brunsfeld) — the probe
  links no GPL grammar.
- **Checked in**: `LICENSE`, `grammar.js`, `src/parser.c`,
  `src/tree_sitter/parser.h`, `queries/highlights.scm` (upstream) and
  `queries/{folds,structure}.scm` (authored here for the probe, same MIT terms
  as the rest of this probe).
- **sha256 of vendored parser.c**: recorded in `evidence/r2-c15/00-pins.txt`.

Reason for vendoring instead of cloning at build time: the probe must build
offline inside a pinned image, and a commit pin plus a file hash is a stronger
reproducibility statement than a mutable branch.
