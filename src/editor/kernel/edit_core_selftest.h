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

// Diagnosis of the REAL KTextEditor highlighting path (task T81): which
// definitions the editor's own KSyntaxHighlighting repository holds, which
// document mode the product open path selects, and how many format/folding
// ranges the resolved definition produces. The caller is expected to set
// delivery-like XDG data paths BEFORE constructing QApplication (the delivered
// AppRun narrows them to the AppDir); see `vedit-edit-core-selftest
// --diagnose-highlight`.
SelfCheckResult runHighlightDiagnosis();

} // namespace vedit::editor
