// tree-sitter backed implementation of the SyntaxProvider seam.
//
// Everything below the interface is an implementation detail: the public
// header never sees TSLanguage/TSNode/TSQuery, and the caller never includes
// this file. The grammar identity and the query texts arrive from the registry
// (which CMake fills in from SEAM_GRAMMAR), so this file is identical for
// every grammar.
#include "syntax_provider.hpp"

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "grammar_registry.hpp"

namespace seam {

// The opaque handle promised by the interface.
struct ParseTree {
  TSTree *tree = nullptr;
  std::string source;

  ~ParseTree() {
    if (tree != nullptr) {
      ts_tree_delete(tree);
    }
  }
};

namespace {

const TSLanguage *languageOfSlot() {
  const GrammarSlot &slot = grammarSlot();
  auto *fn = reinterpret_cast<const TSLanguage *(*)(void)>(slot.language);
  return fn();
}

struct Capture {
  TSNode node{};
  std::string name;
};

using Match = std::vector<Capture>;

std::vector<Match> runQuery(const TSQuery *query, const TSTree *tree) {
  std::vector<Match> matches;
  TSQueryCursor *cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));
  TSQueryMatch match{};
  while (ts_query_cursor_next_match(cursor, &match)) {
    Match captures;
    captures.reserve(match.capture_count);
    for (uint32_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture &capture = match.captures[i];
      uint32_t name_length = 0;
      const char *name = ts_query_capture_name_for_id(query, capture.index, &name_length);
      captures.push_back(Capture{capture.node, std::string(name, name_length)});
    }
    matches.push_back(std::move(captures));
  }
  ts_query_cursor_delete(cursor);
  return matches;
}

std::string textOf(const std::string &source, TSNode node) {
  const uint32_t start = ts_node_start_byte(node);
  const uint32_t end = ts_node_end_byte(node);
  if (end <= start || end > source.size()) {
    return {};
  }
  return source.substr(start, end - start);
}

// Last line that the node really occupies. A node that ends at column 0 of the
// following line does not cover that following line.
std::uint32_t lastOccupiedLine(TSNode node) {
  const TSPoint end = ts_node_end_point(node);
  const TSPoint start = ts_node_start_point(node);
  if (end.column == 0 && end.row > start.row) {
    return end.row - 1;
  }
  return end.row;
}

class TreeSitterProvider final : public SyntaxProvider {
 public:
  TreeSitterProvider()
      : language_(languageOfSlot()),
        slot_(grammarSlot()) {}

  ~TreeSitterProvider() override {
    for (auto &entry : queries_) {
      ts_query_delete(entry.second);
    }
  }

  std::string name() const override { return slot_.display_name; }
  std::string version() const override { return slot_.version; }
  std::string license() const override { return slot_.license; }

  Capabilities capabilities() const override {
    return Capabilities{true, true, true, true, true};
  }

  ParseTreePtr parse(std::string_view source) const override {
    auto tree = std::make_shared<ParseTree>();
    tree->source.assign(source);
    TSParser *parser = ts_parser_new();
    if (parser == nullptr) {
      throw std::runtime_error("ts_parser_new() returned null");
    }
    if (!ts_parser_set_language(parser, language_)) {
      ts_parser_delete(parser);
      throw std::runtime_error("ts_parser_set_language() rejected the grammar "
                               "(grammar ABI version " +
                               std::to_string(ts_language_version(language_)) + ")");
    }
    tree->tree = ts_parser_parse_string(parser, nullptr, tree->source.data(),
                                        static_cast<uint32_t>(tree->source.size()));
    ts_parser_delete(parser);
    if (tree->tree == nullptr) {
      throw std::runtime_error("ts_parser_parse_string() returned null");
    }
    return tree;
  }

  std::vector<std::string> queryIds() const override {
    return {"highlights", "folds", "structure"};
  }

  std::size_t queryMatchCount(const ParseTreePtr &tree, std::string_view query_id) const override {
    if (!tree) {
      return 0;
    }
    const TSQuery *query = queryFor(query_id);
    if (query == nullptr) {
      return 0;
    }
    return runQuery(query, tree->tree).size();
  }

  std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &tree) const override {
    std::vector<HighlightSpan> spans;
    if (!tree) {
      return spans;
    }
    for (const Match &match : runQuery(queryFor("highlights"), tree->tree)) {
      for (const Capture &capture : match) {
        const TSPoint start = ts_node_start_point(capture.node);
        const TSPoint end = ts_node_end_point(capture.node);
        spans.push_back(HighlightSpan{ts_node_start_byte(capture.node),
                                      ts_node_end_byte(capture.node),
                                      start.row,
                                      end.row,
                                      capture.name});
      }
    }
    std::sort(spans.begin(), spans.end(), [](const HighlightSpan &a, const HighlightSpan &b) {
      if (a.start_byte != b.start_byte) return a.start_byte < b.start_byte;
      return a.capture < b.capture;
    });
    return spans;
  }

  std::vector<FoldRegion> foldRegions(const ParseTreePtr &tree) const override {
    std::vector<FoldRegion> regions;
    if (!tree) {
      return regions;
    }
    for (const Match &match : runQuery(queryFor("folds"), tree->tree)) {
      for (const Capture &capture : match) {
        if (capture.name != "fold") {
          continue;
        }
        const std::uint32_t first_line = ts_node_start_point(capture.node).row;
        const std::uint32_t last_line = lastOccupiedLine(capture.node);
        if (last_line > first_line) {
          regions.push_back(FoldRegion{first_line, last_line, "fold"});
        }
      }
    }
    std::sort(regions.begin(), regions.end(), [](const FoldRegion &a, const FoldRegion &b) {
      if (a.start_line != b.start_line) return a.start_line < b.start_line;
      return a.end_line > b.end_line;
    });
    return regions;
  }

  std::vector<StructureNode> structure(const ParseTreePtr &tree) const override {
    std::vector<StructureNode> nodes;
    if (!tree) {
      return nodes;
    }
    for (const Match &match : runQuery(queryFor("structure"), tree->tree)) {
      const Capture *item = nullptr;
      const Capture *name = nullptr;
      for (const Capture &capture : match) {
        if (capture.name == "item") item = &capture;
        if (capture.name == "name") name = &capture;
      }
      if (item == nullptr) {
        continue;
      }
      nodes.push_back(StructureNode{ts_node_start_point(item->node).row,
                                   0,
                                   "item",
                                   name != nullptr ? textOf(tree->source, name->node) : std::string{}});
    }
    std::sort(nodes.begin(), nodes.end(), [](const StructureNode &a, const StructureNode &b) {
      return a.line < b.line;
    });
    return nodes;
  }

 private:
  const TSQuery *queryFor(std::string_view id) const {
    const std::string key(id);
    auto found = queries_.find(key);
    if (found != queries_.end()) {
      return found->second;
    }
    const char *source = nullptr;
    if (key == "highlights") {
      source = slot_.highlight_query;
    } else if (key == "folds") {
      source = slot_.fold_query;
    } else if (key == "structure") {
      source = slot_.structure_query;
    } else {
      return nullptr;
    }
    TSQuery *query = compileQuery(source);
    queries_.emplace(key, query);
    return query;
  }

  TSQuery *compileQuery(const char *source) const {
    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    TSQuery *query = ts_query_new(language_, source, static_cast<uint32_t>(std::strlen(source)),
                                  &error_offset, &error_type);
    if (query == nullptr) {
      throw std::runtime_error("tree-sitter query compilation failed at offset " +
                               std::to_string(error_offset) + " (error type " +
                               std::to_string(static_cast<int>(error_type)) + ")");
    }
    return query;
  }

  const TSLanguage *language_;
  const GrammarSlot &slot_;
  mutable std::map<std::string, TSQuery *> queries_;
};

}  // namespace

std::unique_ptr<SyntaxProvider> makeBuiltinProvider() {
  return std::make_unique<TreeSitterProvider>();
}

}  // namespace seam
