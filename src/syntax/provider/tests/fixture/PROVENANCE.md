# Test fixture — tree-sitter-json (not a product grammar)

- Upstream: https://github.com/tree-sitter/tree-sitter-json
- Pinned commit: `254c42a6476413b776221e03982ac8ae159eeb72`
- Licence: MIT (see `LICENSE`)
- Files: `parser.c`, `tree_sitter/parser.h`, `queries/{highlights,folds,structure}.scm`
- sha256(parser.c) = `9a266ae4a0305b8a2769bee90a14ac06914bdc83a509f16428311eac2845637e`
  (same file the C15 probe pinned)

Why it is here: the provider's generic tree-sitter backend (queries, highlight
spans, fold ranges, breadcrumbs, incremental edit sync) must be exercised against
a real grammar in unit tests. Product grammars (Verilog/SystemVerilog) belong to
src/syn-grammars and are deliberately not implemented here.
