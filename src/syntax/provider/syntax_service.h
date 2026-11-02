#pragma once

// Runtime switch + active-provider resolution at the syntax-provider layer.
//
// Compile-time: -DVEDIT_SYNTAX_GRAMMAR (which grammar binding is built for this
// binary) and -DVEDIT_SYNTAX_TREESITTER (whether the tree-sitter backend exists
// at all) are baked in by CMake (grammar_config_generated.h).
//
// Runtime (this class): the whole tree-sitter backend can be switched off; the
// editor then falls back to the KSyntaxHighlighting base highlighter (A1h). The
// switch is defined here, at the syntax layer, not in the app shell and not in
// the editor — settings-ui only renders it.
#include "grammar_binding.h"
#include "syntax_provider.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace vedit::syntax {

class SyntaxService {
public:
    static SyntaxService &instance();

    // Runtime master switch (A1h / C15): off -> createProvider() returns null
    // and every caller must use the base highlighter. Independent of the
    // compile-time options above.
    void setTreeSitterEnabled(bool enabled);
    bool treeSitterEnabled() const;

    // Select the registry binding used by createProvider(). Returns false for
    // an unregistered id or when the tree-sitter backend was not compiled in.
    bool activate(std::string_view grammar_id);
    void deactivate();
    std::string activeGrammarId() const;

    // True iff a provider would be produced right now (enabled + compiled in +
    // an active, valid binding).
    bool available() const;

    // nullptr when unavailable; the caller falls back to KSyntaxHighlighting.
    std::unique_ptr<SyntaxProvider> createProvider();

    std::vector<std::string> availableGrammarIds() const;
    CompileTimeConfig compileTimeConfig() const;

    // Test hook: restore defaults (enabled, active = compiled grammar).
    void resetForTests();

private:
    SyntaxService();
    mutable std::mutex mutex_;
    bool enabled_ = true;
    std::string active_;
};

} // namespace vedit::syntax
