// Standalone driver for the edit-core selfchecks (Slice 1, task T10).
//
// The product runs the same checks through `vedit-app --selfcheck`; this binary
// makes the kernel testable on its own (ctest) without the shell and without the
// Rust service layer.
#include "edit_core_selftest.h"

#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-edit-core-selftest"));
    const vedit::editor::SelfCheckResult result = vedit::editor::runSelfChecks();
    return result.failures == 0 ? 0 : 1;
}
