// The SyntaxProvider seam.
//
// This header is the contract that the rest of the editor talks to. It is
// deliberately free of any concrete grammar type: no tree-sitter header is
// included here, no node/grammar symbol is named. The five capabilities the
// editor needs are declared as plain C++ data + opaque handles:
//
//   parse                       -> parse()
//   query (by logical query id)  -> queryIds() / queryMatchCount()
//   highlight spans              -> highlightSpans()
//   fold ranges                  -> foldRegions()
//   structure (outline)          -> structure()
//
// Swapping the grammar must never touch this file or any caller (see
// evidence/r2-c15/: the file hashes are identical across both builds).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace seam {

// Opaque parse result. Defined only inside the implementation translation unit.
struct ParseTree;
using ParseTreePtr = std::shared_ptr<ParseTree>;

struct HighlightSpan {
  std::uint32_t start_byte = 0;
  std::uint32_t end_byte = 0;
  std::uint32_t start_line = 0;   // 0-based
  std::uint32_t end_line = 0;     // 0-based
  std::string capture;            // logical capture name, e.g. "keyword"
};

struct FoldRegion {
  std::uint32_t start_line = 0;   // 0-based
  std::uint32_t end_line = 0;     // 0-based, inclusive
  std::string kind;               // logical fold kind, e.g. "fold"
};

struct StructureNode {
  std::uint32_t line = 0;         // 0-based
  std::uint32_t depth = 0;
  std::string kind;               // logical node kind, e.g. "item"
  std::string name;               // capture text of the "name" capture, if any
};

struct Capabilities {
  bool parse = false;
  bool query = false;
  bool highlight = false;
  bool fold = false;
  bool structure = false;
};

class SyntaxProvider {
 public:
  virtual ~SyntaxProvider() = default;

  virtual std::string name() const = 0;
  virtual std::string version() const = 0;
  virtual std::string license() const = 0;
  virtual Capabilities capabilities() const = 0;

  // Capability 1: parse. Returns nullptr when the provider is disabled.
  virtual ParseTreePtr parse(std::string_view source) const = 0;

  // Capability 2: query. Query ids are logical names ("highlights",
  // "folds", "structure"); the grammar owns the query text, not the caller.
  virtual std::vector<std::string> queryIds() const = 0;
  virtual std::size_t queryMatchCount(const ParseTreePtr &tree,
                                     std::string_view query_id) const = 0;

  // Capability 3: highlight spans.
  virtual std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &tree) const = 0;

  // Capability 4: fold ranges.
  virtual std::vector<FoldRegion> foldRegions(const ParseTreePtr &tree) const = 0;

  // Capability 5: structure / outline.
  virtual std::vector<StructureNode> structure(const ParseTreePtr &tree) const = 0;
};

// The provider compiled into this build. When the build was configured with
// tree-sitter disabled this returns the disabled provider, so callers keep
// working unchanged (runtime switch, not a compile-time fork of the caller).
std::unique_ptr<SyntaxProvider> makeBuiltinProvider();

// Always-available no-op provider: every capability reports false/empty.
std::unique_ptr<SyntaxProvider> makeDisabledProvider();

}  // namespace seam
