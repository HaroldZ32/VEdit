#include "fixture_binding.h"

#include "fixture_queries_generated.h"

extern "C" const void *tree_sitter_json(void);

namespace vedit::syntax::tests {

GrammarBinding makeFixtureBinding()
{
    GrammarBinding binding;
    binding.id = "fixture_json";
    binding.display_name = "tree-sitter-json (test fixture)";
    binding.version = "254c42a6476413b776221e03982ac8ae159eeb72";
    binding.license = "MIT";
    binding.language = reinterpret_cast<const void *(*)(void)>(&tree_sitter_json);
    binding.queries = {
        NamedQuery{"highlights", generated::kHighlights},
        NamedQuery{"folds", generated::kFolds},
        NamedQuery{"structure", generated::kStructure},
    };
    return binding;
}

} // namespace vedit::syntax::tests
