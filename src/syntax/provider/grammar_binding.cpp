#include "grammar_binding.h"

#include "grammar_config_generated.h"

#include <algorithm>
#include <utility>

namespace vedit::syntax {

GrammarRegistry &GrammarRegistry::instance()
{
    static GrammarRegistry registry;
    return registry;
}

bool GrammarRegistry::registerBinding(std::string id, BindingFactory factory)
{
    if (id.empty() || !factory) {
        return false;
    }
    for (const Entry &entry : entries_) {
        if (entry.id == id) {
            return false; // first registration wins; no silent replacement
        }
    }
    entries_.push_back(Entry{std::move(id), std::move(factory)});
    return true;
}

bool GrammarRegistry::has(std::string_view id) const
{
    const std::string wanted(id);
    return std::any_of(entries_.begin(), entries_.end(),
                       [&wanted](const Entry &entry) { return entry.id == wanted; });
}

GrammarBinding GrammarRegistry::binding(std::string_view id) const
{
    const std::string wanted(id);
    for (const Entry &entry : entries_) {
        if (entry.id == wanted) {
            GrammarBinding binding = entry.factory ? entry.factory() : GrammarBinding{};
            if (binding.id.empty()) {
                binding.id = entry.id;
            }
            return binding;
        }
    }
    return {};
}

std::vector<std::string> GrammarRegistry::ids() const
{
    std::vector<std::string> ids;
    ids.reserve(entries_.size());
    for (const Entry &entry : entries_) {
        ids.push_back(entry.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void GrammarRegistry::clearForTests()
{
    entries_.clear();
}

CompileTimeConfig compileTimeConfig()
{
    CompileTimeConfig config;
    config.compiled_grammar = generated::kCompiledGrammar;
    config.backend_compiled = generated::kBackendCompiled;
    config.static_backend = generated::kStaticBackend;
    return config;
}

} // namespace vedit::syntax
