// Acceptance test of the breadcrumb lane (task T19, AC-S2.2 "面包屑随语义走").
//
// Everything asserted here comes from the parser's structure query through
// `SyntaxProvider::breadcrumbs()` (systemverilog: resources/queries/systemverilog/
// structure.scm); the expected path is written down from the sample text the
// test builds, never from a fold registry. The test also proves the two
// properties the lane is responsible for:
//   * a cursor move reuses the cached parse (no reparse per move);
//   * a semantic update (text change) reparses exactly once and the path at a
//     new line follows it;
// and that the lane has no path to folding at all (no folding include/link).
#include "breadcrumb_model.h"
#include "syntax_breadcrumb_source.h"
#include "syntax_service.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>
#include <string>
#include <vector>

using vedit::breadcrumb::Entry;
using vedit::breadcrumb::Model;
using vedit::breadcrumb::SyntaxBreadcrumbSource;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

void checkLong(const char *name, long long actual, long long expected)
{
    const bool ok = actual == expected;
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s actual=%lld expected=%lld\n", name, ok ? "PASS" : "FAIL", actual,
                expected);
    std::fflush(stdout);
}

// Line numbers are part of the fixture's construction; the expectations below
// are written against them.
QString sampleText()
{
    return QStringLiteral(
        "module m0 #(parameter W = 8) (\n"   // 0
        "  input logic clk\n"                // 1
        ");\n"                               // 2
        "  function automatic logic inc(input logic v);\n" // 3
        "    inc = v + 1'b1;\n"              // 4
        "  endfunction\n"                    // 5
        "  always_comb begin\n"              // 6
        "    inc(1'b0);\n"                   // 7
        "  end\n"                            // 8
        "endmodule\n"                        // 9
        "module m1;\n"                       // 10
        "  logic q;\n"                       // 11
        "  assign q = 1'b1;\n"               // 12
        "endmodule\n"                        // 13
        "// trailing note\n");               // 14
}

std::vector<std::string> kinds(const std::vector<Entry> &path)
{
    std::vector<std::string> out;
    for (const Entry &entry : path) {
        out.push_back(entry.kind);
    }
    return out;
}

std::vector<std::string> names(const std::vector<Entry> &path)
{
    std::vector<std::string> out;
    for (const Entry &entry : path) {
        out.push_back(entry.name);
    }
    return out;
}

void testModelFormatting()
{
    Model model;
    check("model.initial-empty", model.path().empty() && model.display().isEmpty());
    model.setPath({Entry{"module", "m0", 0}, Entry{"function", "inc", 3}});
    std::printf("MODEL display='%s' describe='%s'\n", model.display().toUtf8().constData(),
                model.describe().toUtf8().constData());
    check("model.display-names",
          model.display() == QStringLiteral("m0 > inc"));
    check("model.describe-kinds",
          model.describe() == QStringLiteral("module:m0 > function:inc"));
    checkLong("model.updates", static_cast<long long>(model.updateCount()), 1);
    checkLong("model.changes", static_cast<long long>(model.changeCount()), 1);

    // Same path again: an update without a repaint-worthy change.
    model.setPath({Entry{"module", "m0", 0}, Entry{"function", "inc", 3}});
    checkLong("model.same-path.changes", static_cast<long long>(model.changeCount()), 1);
    checkLong("model.same-path.updates", static_cast<long long>(model.updateCount()), 2);

    // Cursor moved out of the function: the path shrinks.
    model.setPath({Entry{"module", "m0", 0}});
    check("model.changed-shortens", model.display() == QStringLiteral("m0"));
    checkLong("model.changed.changes", static_cast<long long>(model.changeCount()), 2);

    // No name captured -> the kind is the label.
    model.setPath({Entry{"function", "", 3}});
    check("model.unnamed-uses-kind", model.display() == QStringLiteral("function"));
    check("model.empty-path-hides", (model.setPath({}), model.display().isEmpty()));
}

void testSemanticPath()
{
    SyntaxBreadcrumbSource source(QStringLiteral("systemverilog"));
    check("source.semantic-active", source.semanticActive());
    const QString text = sampleText();

    const std::vector<Entry> moduleHeader = source.pathAt(text, 0);
    Model headerModel;
    headerModel.setPath(moduleHeader);
    std::printf("PATH line=0 depth=%zu display='%s' describe='%s'\n", moduleHeader.size(),
                headerModel.display().toUtf8().constData(),
                headerModel.describe().toUtf8().constData());
    checkLong("path.module-header.depth", static_cast<long long>(moduleHeader.size()), 1);
    check("path.module-header.kind",
          kinds(moduleHeader) == std::vector<std::string>{"module"});
    check("path.module-header.name", names(moduleHeader) == std::vector<std::string>{"m0"});

    const std::vector<Entry> functionBody = source.pathAt(text, 4);
    checkLong("path.function-body.depth", static_cast<long long>(functionBody.size()), 2);
    check("path.function-body.kinds",
          kinds(functionBody) == (std::vector<std::string>{"module", "function"}));
    check("path.function-body.names",
          names(functionBody) == (std::vector<std::string>{"m0", "inc"}));
    checkLong("path.function-body.start-line",
              functionBody.size() == 2 ? functionBody[1].line : -1, 3);

    // The always block is not a structure-query node: the path is the module.
    const std::vector<Entry> alwaysBody = source.pathAt(text, 7);
    checkLong("path.always-body.depth", static_cast<long long>(alwaysBody.size()), 1);
    check("path.always-body.name", names(alwaysBody) == std::vector<std::string>{"m0"});

    const std::vector<Entry> secondModule = source.pathAt(text, 12);
    checkLong("path.second-module.depth", static_cast<long long>(secondModule.size()), 1);
    check("path.second-module.name", names(secondModule) == std::vector<std::string>{"m1"});

    const std::vector<Entry> outside = source.pathAt(text, 14);
    checkLong("path.outside.depth", static_cast<long long>(outside.size()), 0);

    // Cursor moves reuse the cached parse; only a text change reparses.
    checkLong("source.parse-count-after-moves", static_cast<long long>(source.parseCount()), 1);
    check("source.query-count", source.queryCount() >= 5);

    // Semantic update: append a third module; exactly one reparse, and the path
    // at the new lines follows it.
    const QString updated = text + QStringLiteral("module m2;\n  logic r;\nendmodule\n");
    const std::vector<Entry> thirdModule = source.pathAt(updated, 15);
    checkLong("source.parse-count-after-reparse", static_cast<long long>(source.parseCount()), 2);
    checkLong("path.third-module.depth", static_cast<long long>(thirdModule.size()), 1);
    check("path.third-module.name", names(thirdModule) == std::vector<std::string>{"m2"});

    // The model follows the cursor: one semantic path per cursor position.
    Model model;
    model.setPath(source.pathAt(text, 0));
    const QString atHeader = model.display();
    model.setPath(source.pathAt(text, 4));
    const QString inFunction = model.display();
    model.setPath(source.pathAt(text, 4));
    model.setPath(source.pathAt(text, 0));
    std::printf("FOLLOW header='%s' function='%s' changes=%llu updates=%llu\n",
                atHeader.toUtf8().constData(), inFunction.toUtf8().constData(),
                static_cast<unsigned long long>(model.changeCount()),
                static_cast<unsigned long long>(model.updateCount()));
    check("follow.header", atHeader == QStringLiteral("m0"));
    check("follow.function", inFunction == QStringLiteral("m0 > inc"));
    checkLong("follow.updates", static_cast<long long>(model.updateCount()), 4);
    // 4 updates, distinct paths: {}, {m0}, {m0,inc}, {m0} again -> 3 changes.
    checkLong("follow.changes", static_cast<long long>(model.changeCount()), 3);

    // Tree-sitter OFF: the lane has no semantic structure to show (this is the
    // breadcrumb half of A1f; it must not invent a path from text heuristics).
    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(false);
    SyntaxBreadcrumbSource disabled(QStringLiteral("systemverilog"));
    check("source.disabled.semantic-inactive", !disabled.semanticActive());
    checkLong("path.disabled.depth", static_cast<long long>(disabled.pathAt(text, 4).size()), 0);
    checkLong("path.disabled.parse-count", static_cast<long long>(disabled.parseCount()), 0);
    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(true);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testModelFormatting();
    testSemanticPath();
    std::printf("BREADCRUMB-TESTS ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0, g_checks,
                g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
