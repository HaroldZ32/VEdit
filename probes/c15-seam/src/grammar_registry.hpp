// The grammar registry: the only translation unit that knows which concrete
// grammar was compiled in. It is byte-identical in every build; the identity
// and the query text come from a header CMake generates from SEAM_GRAMMAR.
#pragma once

#include <cstddef>

namespace seam {

struct GrammarSlot {
  const char *id;
  const char *display_name;
  const char *version;
  const char *license;
  // Opaque bridge: the C symbol exported by the generated parser, exposed as
  // a plain function pointer so that no tree-sitter type leaks out of the
  // implementation translation unit.
  const void *(*language)(void);
  const char *highlight_query;
  const char *fold_query;
  const char *structure_query;
};

const GrammarSlot &grammarSlot();

}  // namespace seam
