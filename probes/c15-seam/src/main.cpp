// Caller of the SyntaxProvider seam. This file is the "consumer" that must
// never change when the grammar changes; it only knows the abstract interface.
#include "syntax_provider.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(const std::string &what, bool ok) {
  std::cout << "CHECK " << what << " " << (ok ? "PASS" : "FAIL") << "\n";
  if (!ok) {
    ++g_failures;
  }
}

std::string readFile(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::size_t countCaptures(const std::vector<seam::HighlightSpan> &spans, const std::string &capture) {
  std::size_t n = 0;
  for (const auto &span : spans) {
    if (span.capture == capture) {
      ++n;
    }
  }
  return n;
}

}  // namespace

int main(int argc, char **argv) {
  bool treeSitterRequested = true;
  std::string file = "tests/sample.json";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--tree-sitter=", 0) == 0) {
      treeSitterRequested = arg.substr(std::string("--tree-sitter=").size()) != "off";
    } else if (arg == "--file" && i + 1 < argc) {
      file = argv[++i];
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 2;
    }
  }

  std::cout << "TREE_SITTER_REQUESTED=" << (treeSitterRequested ? "on" : "off") << "\n";

  std::unique_ptr<seam::SyntaxProvider> provider =
      treeSitterRequested ? seam::makeBuiltinProvider() : seam::makeDisabledProvider();

  const seam::Capabilities caps = provider->capabilities();
  std::cout << "PROVIDER=" << provider->name() << "\n";
  std::cout << "PROVIDER_VERSION=" << provider->version() << "\n";
  std::cout << "PROVIDER_LICENSE=" << provider->license() << "\n";
  std::cout << "CAPABILITIES parse=" << caps.parse << " query=" << caps.query
            << " highlight=" << caps.highlight << " fold=" << caps.fold
            << " structure=" << caps.structure << "\n";

  const std::string source = readFile(file);
  check("input.read(" + file + ")", !source.empty());

  const seam::ParseTreePtr tree = provider->parse(source);
  std::cout << "PARSED_NONEMPTY=" << (tree != nullptr) << "\n";

  if (caps.parse) {
    check("parse.returns.tree", tree != nullptr);
    const auto spans = provider->highlightSpans(tree);
    const auto folds = provider->foldRegions(tree);
    const auto outline = provider->structure(tree);
    std::cout << "HIGHLIGHT_SPANS=" << spans.size() << "\n";
    std::cout << "FOLD_REGIONS=" << folds.size() << "\n";
    std::cout << "STRUCTURE_ITEMS=" << outline.size() << "\n";
    check("highlight.nonempty", !spans.empty());
    check("highlight.keyword.capture.seen", countCaptures(spans, "keyword") > 0 ||
                                                countCaptures(spans, "string.special.key") > 0);
    check("query.ids.count", provider->queryIds().size() == 3);
    check("query.count.highlights.gt0", provider->queryMatchCount(tree, "highlights") > 0);
    check("folds.nonempty", !folds.empty());
    check("structure.nonempty", !outline.empty());
    check("all.five.capabilities", caps.parse && caps.query && caps.highlight && caps.fold && caps.structure);

    for (std::size_t i = 0; i < spans.size() && i < 5; ++i) {
      std::cout << "SPAN[" << i << "] line=" << spans[i].start_line << " bytes=" << spans[i].start_byte
                << "-" << spans[i].end_byte << " capture=" << spans[i].capture << "\n";
    }
    for (std::size_t i = 0; i < folds.size() && i < 5; ++i) {
      std::cout << "FOLD[" << i << "] lines=" << folds[i].start_line << "-" << folds[i].end_line
                << " kind=" << folds[i].kind << "\n";
    }
    for (std::size_t i = 0; i < outline.size() && i < 5; ++i) {
      std::cout << "STRUCT[" << i << "] line=" << outline[i].line << " kind=" << outline[i].kind
                << " name=" << outline[i].name << "\n";
    }
  } else {
    check("fallback.parse.null", tree == nullptr);
    check("fallback.no.spans", provider->highlightSpans(tree).empty());
    check("fallback.no.folds", provider->foldRegions(tree).empty());
    check("fallback.no.structure", provider->structure(tree).empty());
    check("fallback.no.queries", provider->queryIds().empty());
    std::cout << "FALLBACK_PATH=ksyntaxhighlighting-only (tree-sitter unavailable)\n";
  }

  std::cout << "RESULT=" << (g_failures == 0 ? "PASS" : "FAIL") << " failures=" << g_failures << "\n";
  return g_failures == 0 ? 0 : 1;
}
