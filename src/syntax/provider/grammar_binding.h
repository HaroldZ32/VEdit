#pragma once

// Compile-time grammar registry for the SyntaxProvider seam.
//
// A concrete grammar (owned by src/syn-grammars) contributes a GrammarBinding:
// an opaque language entry point plus the logical queries the provider needs.
// No tree-sitter type appears here — `language` is an opaque function pointer.
//
// Compile-time selection: `-DVEDIT_SYNTAX_GRAMMAR=<id>` is baked into the build
// by CMake (see grammar_config_generated.h). The registry itself is filled at
// static-init time by the grammar targets that are linked into the binary, so
// switching grammar = changing the CMake option/registrations, never the
// interface or a caller. Runtime enabling/disabling is a separate, independent
// switch owned by SyntaxService (syntax_service.h).
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace vedit::syntax {

struct NamedQuery {
    std::string id;    // "highlights" | "folds" | "structure" | any logical id
    std::string scm;   // query source text
};

struct GrammarBinding {
    std::string id;           // stable id, e.g. "verilog" / "systemverilog"
    std::string display_name; // human readable
    std::string version;      // upstream commit / generation marker
    std::string license;      // SPDX-ish licence string
    // Opaque grammar entry point. The tree-sitter implementation casts it to
    // its own language type; no other code may interpret it.
    const void *(*language)(void) = nullptr;
    std::vector<NamedQuery> queries;

    bool isValid() const { return language != nullptr && !id.empty(); }
};

using BindingFactory = std::function<GrammarBinding()>;

class GrammarRegistry {
public:
    static GrammarRegistry &instance();

    // Registration is idempotent per id and never throws.
    bool registerBinding(std::string id, BindingFactory factory);
    bool has(std::string_view id) const;
    GrammarBinding binding(std::string_view id) const; // invalid binding when absent
    std::vector<std::string> ids() const;
    void clearForTests();

private:
    GrammarRegistry() = default;
    struct Entry {
        std::string id;
        BindingFactory factory;
    };
    std::vector<Entry> entries_;
};

struct CompileTimeConfig {
    std::string compiled_grammar;   // from -DVEDIT_SYNTAX_GRAMMAR=
    bool backend_compiled = false;  // from -DVEDIT_SYNTAX_TREESITTER=
    bool static_backend = false;    // from -DVEDIT_SYNTAX_TS_LINK=static
};

CompileTimeConfig compileTimeConfig();

} // namespace vedit::syntax

// Registration helper for grammar targets (src/syn-grammars):
//   VEDIT_SYNTAX_REGISTER_GRAMMAR(verilog, [] { return makeVerilogBinding(); });
#define VEDIT_SYNTAX_REGISTER_GRAMMAR(identifier, factory)                                        \
    namespace {                                                                                   \
    const bool vedit_syntax_registration_##identifier =                                           \
        ::vedit::syntax::GrammarRegistry::instance().registerBinding(#identifier, (factory));      \
    }
