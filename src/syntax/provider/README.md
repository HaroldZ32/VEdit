# src/syntax/provider — production SyntaxProvider seam (T15)

Layering (contract C15/C16/C18):

| file | role |
|---|---|
| `syntax_provider.h` | public language-agnostic interface: parse / query ranges / highlight spans / fold ranges / structure breadcrumbs / incremental edit sync. No tree-sitter type, no grammar name. |
| `grammar_binding.h` | compile-time grammar registry: a grammar contributes an opaque language entry point + logical queries. Registered by `src/syn-grammars`; the interface never mentions it. |
| `owner_arbiter.h` | C16 single-owner colouring arbiter + `assertSingleOwner()` (A1g judgement). Policy: `POLICY.md`. |
| `tree_sitter_provider.h` | generic tree-sitter backend for a binding. The only translation unit including `<tree_sitter/api.h>`. |
| `syntax_service.h` | runtime master switch (`setTreeSitterEnabled`) + active-grammar resolution. |
| `tests/` | unit tests + test-only tree-sitter-json fixture (not a product grammar). |

## Build / test (standalone, same pinned image as the rest of the C++ lane)

```sh
# inside the pinned image (or any host with tree-sitter 0.25.9 + cmake + C++20):
cmake -S src/syntax/provider -B target/syn-provider-build -G Ninja \
      -DVEDIT_SYNTAX_GRAMMAR=external -DVEDIT_SYNTAX_TREESITTER=ON \
      -DVEDIT_SYNTAX_TS_LINK=shared
cmake --build target/syn-provider-build --parallel
ctest --test-dir target/syn-provider-build --output-on-failure
```

Switches:

| option | values | effect |
|---|---|---|
| `VEDIT_SYNTAX_GRAMMAR` | `none` (default), `external`, `<id>` | grammar id baked into the build; switching = configuration + registrations, no source change |
| `VEDIT_SYNTAX_TREESITTER` | `ON`/`OFF` | compile the tree-sitter backend at all; OFF builds a clean fallback factory |
| `VEDIT_SYNTAX_TS_LINK` | `shared`/`static` | C18 linkage of libtree-sitter; static keeps grammar replaceability at the compile-time layer |
| `VEDIT_SYNTAX_BUILD_TESTS` | `ON`/`OFF` | unit tests (need a tree-sitter backend) |

Evidence for this lane: `evidence/syn-provider/`.

## Integration with the rest of the product

1. **Root build**: the build base should add this directory like `src/app`:
   ```cmake
   if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/syntax/provider/CMakeLists.txt")
     add_subdirectory(src/syntax/provider)
   endif()
   ```
   T15 does not edit the root build (scope); until then the standalone build above
   is the evidence path.
2. **Grammar lane (`src/syn-grammars`)**: implement `GrammarBinding` factories and
   register them (`VEDIT_SYNTAX_REGISTER_GRAMMAR(<id>, <factory>)`), then select the
   grammar through `-DVEDIT_SYNTAX_GRAMMAR=<id>` and `SyntaxService::activate()`.
3. **Overlay lane (`src/syn-overlay`)**: obtain candidate spans from the provider,
   `OwnerArbiter::claim()` each range before painting, `releaseAll()` when the
   overlay is torn down or tree-sitter is switched off. See `POLICY.md` §3.
4. **Editor / settings**: render `SyntaxService::treeSitterEnabled()`; the switch
   is owned by this layer, settings only displays it.
5. **Folding**: `foldRanges()` is consumed by the fold view (owned by the
   `edit-folds` lane); this lane does not implement a folding view.

## Constraints

* No concrete grammar is implemented here (boundary: `src/syn-grammars`). The
  tree-sitter-json fixture exists only for unit tests.
* No colouring code (boundary: `src/syn-overlay`); this lane only produces spans
  and arbitrates ownership.
* Incremental edit sync edits the provider's tree and its copy of the text; the
  caller must apply the same edit to its own buffer (`applyEdit()` returns false
  for inconsistent coordinates, in which case the caller must re-parse).
