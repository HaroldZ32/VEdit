#include "bindings.h"

// The vendored grammar exposes its tree-sitter entry point as a C symbol. This
// binding layer stores it as the provider's OPAQUE function pointer
// (grammar_binding.h): no tree-sitter header is included here and no node type
// is named here — node types live only in resources/queries/*.scm (contract (c)).
extern "C" const void *tree_sitter_verilog(void);

namespace vedit::syntax::grammars {
#if VEDIT_GRAMMAR_VERILOG_ENABLED || VEDIT_GRAMMAR_SYSTEMVERILOG_ENABLED
namespace {

GrammarBinding makeBinding(const generated::GrammarInfo &info,
                           const char *highlights,
                           const char *folds,
                           const char *structure)
{
    GrammarBinding binding;
    binding.id = info.id;
    binding.display_name = info.display;
    binding.version = info.version;
    binding.license = info.license;
    binding.language = &tree_sitter_verilog;
    binding.queries.push_back(NamedQuery{"highlights", highlights});
    binding.queries.push_back(NamedQuery{"folds", folds});
    binding.queries.push_back(NamedQuery{"structure", structure});
    return binding;
}

} // namespace
#endif

std::size_t registerBuiltinGrammars()
{
    std::size_t added = 0;
#if VEDIT_GRAMMAR_VERILOG_ENABLED
    added += GrammarRegistry::instance().registerBinding("verilog", [] { return makeVerilogBinding(); }) ? 1 : 0;
#endif
#if VEDIT_GRAMMAR_SYSTEMVERILOG_ENABLED
    added += GrammarRegistry::instance().registerBinding("systemverilog",
                                                         [] { return makeSystemVerilogBinding(); })
                 ? 1
                 : 0;
#endif
    return added;
}

#if VEDIT_GRAMMAR_VERILOG_ENABLED
GrammarBinding makeVerilogBinding()
{
    return makeBinding(generated::kVerilog,
                       generated::kVerilogHighlights,
                       generated::kVerilogFolds,
                       generated::kVerilogStructure);
}

VEDIT_SYNTAX_REGISTER_GRAMMAR(verilog, [] { return makeVerilogBinding(); });
#endif

#if VEDIT_GRAMMAR_SYSTEMVERILOG_ENABLED
GrammarBinding makeSystemVerilogBinding()
{
    return makeBinding(generated::kSystemVerilog,
                       generated::kSystemVerilogHighlights,
                       generated::kSystemVerilogFolds,
                       generated::kSystemVerilogStructure);
}

VEDIT_SYNTAX_REGISTER_GRAMMAR(systemverilog, [] { return makeSystemVerilogBinding(); });
#endif

} // namespace vedit::syntax::grammars
