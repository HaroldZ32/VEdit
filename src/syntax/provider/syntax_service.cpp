#include "syntax_service.h"

#include "tree_sitter_provider.h"

#include <algorithm>

namespace vedit::syntax {

SyntaxService &SyntaxService::instance()
{
    static SyntaxService service;
    return service;
}

SyntaxService::SyntaxService()
{
    const CompileTimeConfig config = compileTimeConfig();
    if (config.backend_compiled && config.compiled_grammar != "none") {
        active_ = config.compiled_grammar;
    }
}

void SyntaxService::setTreeSitterEnabled(bool enabled)
{
    const std::lock_guard<std::mutex> guard(mutex_);
    enabled_ = enabled;
}

bool SyntaxService::treeSitterEnabled() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return enabled_;
}

bool SyntaxService::activate(std::string_view grammar_id)
{
    const CompileTimeConfig config = compileTimeConfig();
    if (!config.backend_compiled) {
        return false;
    }
    const std::string wanted(grammar_id);
    if (!GrammarRegistry::instance().has(wanted)) {
        return false;
    }
    const std::lock_guard<std::mutex> guard(mutex_);
    active_ = wanted;
    return true;
}

void SyntaxService::deactivate()
{
    const std::lock_guard<std::mutex> guard(mutex_);
    active_.clear();
}

std::string SyntaxService::activeGrammarId() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return active_;
}

bool SyntaxService::available() const
{
    const CompileTimeConfig config = compileTimeConfig();
    const std::lock_guard<std::mutex> guard(mutex_);
    if (!enabled_ || !config.backend_compiled || active_.empty()) {
        return false;
    }
    return GrammarRegistry::instance().binding(active_).isValid();
}

std::unique_ptr<SyntaxProvider> SyntaxService::createProvider()
{
    if (!available()) {
        return nullptr;
    }
    const std::string active = activeGrammarId();
    const GrammarBinding binding = GrammarRegistry::instance().binding(active);
    if (!binding.isValid()) {
        return nullptr;
    }
    return makeTreeSitterProvider(binding);
}

std::vector<std::string> SyntaxService::availableGrammarIds() const
{
    return GrammarRegistry::instance().ids();
}

CompileTimeConfig SyntaxService::compileTimeConfig() const
{
    return vedit::syntax::compileTimeConfig();
}

void SyntaxService::resetForTests()
{
    const CompileTimeConfig config = compileTimeConfig();
    const std::lock_guard<std::mutex> guard(mutex_);
    enabled_ = true;
    active_ = config.backend_compiled && config.compiled_grammar != "none" ? config.compiled_grammar
                                                                               : std::string();
}

} // namespace vedit::syntax
