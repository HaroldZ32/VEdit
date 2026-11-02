// Registry implementation. No concrete grammar name is written here: the
// symbol, identity strings and queries all arrive through the generated header.
#include "grammar_registry.hpp"

#include "grammar_config_generated.hpp"

// Defined by the generated parser.c of the selected grammar (C linkage).
extern "C" const void *SEAM_GRAMMAR_SYMBOL(void);

namespace seam {

const GrammarSlot &grammarSlot() {
  static const GrammarSlot slot{
      generated::kGrammarId,
      generated::kGrammarDisplay,
      generated::kGrammarVersion,
      generated::kGrammarLicense,
      &SEAM_GRAMMAR_SYMBOL,
      generated::kHighlightQuery,
      generated::kFoldQuery,
      generated::kStructureQuery,
  };
  return slot;
}

}  // namespace seam
