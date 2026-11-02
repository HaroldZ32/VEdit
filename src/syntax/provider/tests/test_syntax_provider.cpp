// Unit tests for the production SyntaxProvider seam, the C16 owner arbiter and
// the runtime switch. Plain assert harness: prints CHECK lines, exits non-zero
// on any failure (registered as the ctest target vedit-syntax-provider-tests).
#include "grammar_binding.h"
#include "owner_arbiter.h"
#include "syntax_provider.h"
#include "syntax_service.h"
#include "tree_sitter_provider.h"

#include "fixture_binding.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace vedit::syntax;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const std::string &name, bool ok, const std::string &detail = {})
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
                detail.empty() ? "" : "  ", detail.c_str());
    std::fflush(stdout);
}

std::string describe(const std::vector<OwnerSpan> &spans)
{
    std::string out;
    for (const OwnerSpan &span : spans) {
        out += "[" + std::to_string(span.start_byte) + "," + std::to_string(span.end_byte) + ":" +
               std::to_string(static_cast<int>(span.kind)) + "]";
    }
    return out;
}

void testCompileTimeConfig()
{
    const CompileTimeConfig config = compileTimeConfig();
    check("compiletime.grammar-matches-build-option",
          config.compiled_grammar == std::string(VEDIT_EXPECT_COMPILED_GRAMMAR),
          config.compiled_grammar);
    check("compiletime.tree-sitter-flag", config.backend_compiled == (VEDIT_EXPECT_TREE_SITTER != 0),
          config.backend_compiled ? "true" : "false");
    // The interface header must stay grammar-type free; the build script greps
    // this as static evidence (no run-time assertion can see the preprocessed
    // header from here).
    check("compiletime.runtime-switch-wired", SyntaxService::instance().treeSitterEnabled());
}

void testOwnerArbiter()
{
    OwnerArbiter &arbiter = OwnerArbiter::instance();
    arbiter.clear();
    const std::string lang = "verilog";
    const std::string otherLang = "systemverilog";

    check("arbiter.base-default", arbiter.ownerAt(lang, 5).kind == OwnerKind::Base);
    check("arbiter.base-owner-id",
          arbiter.ownerAt(lang, 5).owner_id == std::string(OwnerArbiter::baseOwnerId()));
    std::string detail;
    check("arbiter.single-owner-default",
          arbiter.assertSingleOwner(lang, 0, 100, &detail), detail);

    const ClaimOutcome granted = arbiter.claim(lang, "tree-sitter", 10, 30);
    check("arbiter.claim-granted", granted.result == ClaimResult::Granted, granted.message);
    check("arbiter.overlay-owns-range", arbiter.ownerAt(lang, 15).kind == OwnerKind::Overlay &&
                                            arbiter.ownerAt(lang, 15).owner_id == "tree-sitter");
    const auto spans = arbiter.ownerSpans(lang, 0, 100);
    check("arbiter.segments-partition", arbiter.assertSingleOwner(lang, 0, 100, &detail),
          describe(spans) + " " + detail);
    check("arbiter.segments-count", spans.size() == 3, describe(spans));
    check("arbiter.no-double-color",
          arbiter.ownerSpans(lang, 10, 30).size() == 1 &&
              arbiter.ownerSpans(lang, 10, 30).front().kind == OwnerKind::Overlay);

    const ClaimOutcome conflict = arbiter.claim(lang, "second-overlay", 20, 40);
    check("arbiter.overlap-conflict", conflict.result == ClaimResult::Conflict, conflict.message);
    check("arbiter.conflict-reports-owner", conflict.conflicting.owner_id == "tree-sitter",
          conflict.conflicting.owner_id);
    const ClaimOutcome adjacent = arbiter.claim(lang, "second-overlay", 30, 40);
    check("arbiter.adjacent-claim-granted", adjacent.result == ClaimResult::Granted,
          adjacent.message);
    check("arbiter.overlap-free-single-owner", arbiter.assertSingleOwner(lang, 0, 100, &detail),
          detail);

    check("arbiter.release-splits", arbiter.release(lang, "tree-sitter", 15, 20));
    check("arbiter.release-falls-back-to-base", arbiter.ownerAt(lang, 17).kind == OwnerKind::Base);
    check("arbiter.release-keeps-single-owner", arbiter.assertSingleOwner(lang, 0, 100, &detail),
          detail);
    check("arbiter.release-unowned-returns-false",
          !arbiter.release(lang, "no-such-overlay", 0, 100));

    arbiter.setBaseEnabled(lang, false);
    check("arbiter.base-disabled-none", arbiter.ownerAt(lang, 5).kind == OwnerKind::None);
    check("arbiter.base-disabled-still-partitions",
          arbiter.assertSingleOwner(lang, 0, 100, &detail), detail);
    arbiter.setBaseEnabled(lang, true);

    check("arbiter.release-all", arbiter.releaseAll(lang, "second-overlay"));
    const auto afterFirstReleaseAll = arbiter.ownerSpans(lang, 0, 100);
    check("arbiter.release-all-leaves-other-overlay",
          afterFirstReleaseAll.size() == 5 &&
              afterFirstReleaseAll[0].kind == OwnerKind::Base &&
              afterFirstReleaseAll[1].kind == OwnerKind::Overlay &&
              afterFirstReleaseAll[1].owner_id == "tree-sitter" &&
              afterFirstReleaseAll[2].kind == OwnerKind::Base &&
              afterFirstReleaseAll[3].kind == OwnerKind::Overlay &&
              afterFirstReleaseAll[4].kind == OwnerKind::Base,
          describe(afterFirstReleaseAll));
    check("arbiter.release-all-keeps-single-owner", arbiter.assertSingleOwner(lang, 0, 100, &detail),
          detail);
    check("arbiter.release-second-overlay", arbiter.releaseAll(lang, "tree-sitter"));
    check("arbiter.release-all-base-only", arbiter.ownerSpans(lang, 0, 100).size() == 1,
          describe(arbiter.ownerSpans(lang, 0, 100)));
    check("arbiter.other-language-independent",
          arbiter.ownerAt(otherLang, 15).kind == OwnerKind::Base);

    const ClaimOutcome invalid = arbiter.claim(lang, "tree-sitter", 10, 10);
    check("arbiter.invalid-range", invalid.result == ClaimResult::InvalidRange);
    arbiter.clear();
}

void testProviderWithFixture()
{
    GrammarRegistry &registry = GrammarRegistry::instance();
    const bool registered = registry.registerBinding("fixture_json", tests::makeFixtureBinding);
    check("registry.register", registered);
    check("registry.duplicate-rejected", !registry.registerBinding("fixture_json", tests::makeFixtureBinding));
    check("registry.missing-binding-invalid", !registry.binding("does-not-exist").isValid());
    check("registry.ids-contain-fixture", !registry.ids().empty() && registry.ids().front() == "fixture_json");

    SyntaxService &service = SyntaxService::instance();
    service.resetForTests();
    check("service.activate-fixture", service.activate("fixture_json"));
    check("service.active-id", service.activeGrammarId() == "fixture_json");
    check("service.available", service.available());
    auto provider = service.createProvider();
    check("service.provider-created", provider != nullptr);
    if (!provider) {
        return;
    }
    check("provider.id", provider->id() == "fixture_json");
    check("provider.license", provider->license() == "MIT");
    const Capabilities caps = provider->capabilities();
    check("provider.caps.parse", caps.parse);
    check("provider.caps.query-ranges", caps.query_ranges);
    check("provider.caps.highlight-spans", caps.highlight_spans);
    check("provider.caps.fold-ranges", caps.fold_ranges);
    check("provider.caps.breadcrumbs", caps.breadcrumbs);
    check("provider.caps.incremental-edit-sync", caps.incremental_edit_sync);

    const std::string text = R"({"a":1,"b":{"c":[1,2]}})";
    auto tree = provider->parse(text);
    check("provider.parse-non-null", tree != nullptr);
    check("provider.no-syntax-errors", !provider->hasSyntaxErrors(tree));
    check("provider.text-roundtrip", provider->text() == text);
    const auto ids = provider->queryIds();
    check("provider.query-ids", ids.size() == 3);
    check("provider.highlight-spans-nonempty", !provider->highlightSpans(tree).empty());
    check("provider.query-ranges-nonempty", !provider->queryRanges(tree, "highlights").empty());

    const std::uint32_t nestedOffset = static_cast<std::uint32_t>(text.find("\"c\""));
    const auto crumbs = provider->breadcrumbs(tree, nestedOffset);
    std::string names;
    for (const Breadcrumb &crumb : crumbs) {
        names += crumb.name + " ";
    }
    check("provider.breadcrumbs-nonempty", !crumbs.empty(), names);
    bool hasOuter = false;
    bool hasInner = false;
    for (const Breadcrumb &crumb : crumbs) {
        hasOuter = hasOuter || crumb.name == "\"b\"";
        hasInner = hasInner || crumb.name == "\"c\"";
    }
    check("provider.breadcrumbs-nested-path", hasOuter && hasInner, names);

    // Folds need a multi-line document (the fixture folds objects/arrays), and
    // parsing it changes the provider text, so it runs after breadcrumbs.
    // Folds need a multi-line document (the fixture folds objects/arrays).
    const std::string multiLine = "{\n  \"a\": 1,\n  \"b\": [1, 2]\n}";
    auto multilineTree = provider->parse(multiLine);
    check("provider.fold-ranges-nonempty",
          !provider->foldRanges(multilineTree).empty());

    // Incremental edit sync: insert `"d":3,` right after the opening brace.
    const std::string before = R"({"a":1})";
    auto smallTree = provider->parse(before);
    check("provider.incremental.parse", smallTree != nullptr);
    const std::size_t pairsBefore = provider->queryRanges(smallTree, "structure").size();
    Edit edit;
    edit.start_byte = 1;
    edit.old_end_byte = 1;
    edit.start_line = 0;
    edit.start_column = 1;
    edit.old_end_line = 0;
    edit.old_end_column = 1;
    edit.new_text = R"("d":3,)";
    check("provider.incremental.apply-edit", provider->applyEdit(edit));
    check("provider.incremental.text-updated", provider->text() == std::string(R"({"d":3,"a":1})"));
    const std::size_t pairsAfter = provider->queryRanges(provider->tree(), "structure").size();
    // Each structure match yields two captures (@name + @item), so one new pair
    // adds two QueryRange entries.
    check("provider.incremental.tree-updated", pairsAfter == pairsBefore + 2,
          std::to_string(pairsBefore) + " -> " + std::to_string(pairsAfter));

    Edit badEdit;
    badEdit.start_byte = 0;
    badEdit.old_end_byte = 9999;
    badEdit.new_text = "x";
    check("provider.incremental.invalid-edit-rejected", !provider->applyEdit(badEdit));

    auto brokenTree = provider->parse("{\"unterminated\": [1, 2");
    check("provider.bad-input-does-not-crash", brokenTree != nullptr);
    check("provider.bad-input-reports-error", provider->hasSyntaxErrors(brokenTree));
    provider->parse(text);
}

void testRuntimeSwitchAndFallback()
{
    SyntaxService &service = SyntaxService::instance();
    service.setTreeSitterEnabled(false);
    check("switch.disabled.available-false", !service.available());
    check("switch.disabled.no-provider", service.createProvider() == nullptr);
    service.setTreeSitterEnabled(true);
    check("switch.enabled.available-true", service.available());
    check("switch.enabled.provider", service.createProvider() != nullptr);

    service.deactivate();
    check("switch.deactivated.available-false", !service.available());
    check("switch.unknown-grammar-rejected", !service.activate("no-such-grammar"));
    check("switch.reactivate", service.activate("fixture_json"));

    auto disabled = makeDisabledProvider();
    check("fallback.disabled-provider-caps", !disabled->capabilities().parse &&
                                                 !disabled->capabilities().highlight_spans &&
                                                 !disabled->capabilities().incremental_edit_sync);
    check("fallback.disabled-provider-parse-null", disabled->parse("x") == nullptr);
    check("fallback.disabled-provider-spans-empty", disabled->highlightSpans(nullptr).empty());
}

} // namespace

int main()
{
    testCompileTimeConfig();
    testOwnerArbiter();
    testProviderWithFixture();
    testRuntimeSwitchAndFallback();

    std::printf("SYN-PROVIDER-TESTS ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
