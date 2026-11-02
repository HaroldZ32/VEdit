#pragma once

// Factory declarations for the VEdit grammar lane (syn-grammars, task T17).
//
// The provider interface never includes this header: the lane's validator pulls
// the registration translation unit into the link through these factories, so
// the static initializers that fill GrammarRegistry are guaranteed to run.
// Node types never appear here — they live only in the .scm files.
#include "grammar_binding.h"
#include "grammars_config_generated.h"

#include <cstddef>

namespace vedit::syntax::grammars {

// Explicit, idempotent registration. Static initialisers below fill the registry
// when this translation unit is linked; because a static library only pulls an
// object file when a symbol from it is needed, integrators (and the lane
// validator) call this once instead of relying on link order. Returns the number
// of bindings this call actually added (0 when they were already registered).
std::size_t registerBuiltinGrammars();

#if VEDIT_GRAMMAR_VERILOG_ENABLED
GrammarBinding makeVerilogBinding();
#endif

#if VEDIT_GRAMMAR_SYSTEMVERILOG_ENABLED
GrammarBinding makeSystemVerilogBinding();
#endif

} // namespace vedit::syntax::grammars
