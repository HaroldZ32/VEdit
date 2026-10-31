#pragma once

// Edit-core selfchecks, run from the product's `--selfcheck` mode and from the
// standalone `vedit-edit-core-selftest` binary. Every check prints one
// `CHECK editcore.<name> PASS|FAIL` line and the result counts are returned.
#include <QString>

#include <functional>

namespace vedit::editor {

// The C ABI boundary is the Rust service layer. The product wires these hooks to
// the only ABI call sites it has (src/app/main.cpp `ffi` namespace); the kernel
// itself links no `vd_*` symbol, so during editing the count must not move.
struct BoundaryHooks {
    std::function<qint64()> calls;
    std::function<void()> positiveControl; // performs exactly one real ABI call
};

struct SelfCheckResult {
    int checks = 0;
    int failures = 0;
};

SelfCheckResult runSelfChecks(const BoundaryHooks &hooks = BoundaryHooks());

} // namespace vedit::editor
