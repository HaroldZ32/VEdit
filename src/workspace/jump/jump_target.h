#pragma once

// The application-wide jump contract (acceptance AC-7.7).
//
// A jump sends the editor to a file position. Every surface that can do that -
// the outline/diagnostics list, the module tree, the project file list, the
// search results, the file tree and the breadcrumb bar - depends on THIS
// interface and the process-wide registry in jump_registry.h instead of growing
// its own file-opening code. The shell/editor lane registers the one real
// target (opening the file through the shell's open seam and positioning the
// cursor in the editing kernel); a panel that runs without that target gets a
// no-op, never a crash.
//
// Positions are 1-based - what the user sees in the status bar and what all
// panels (outline, diagnostics, search) record. The target converts to its
// editor's internal convention (KTextEditor cursors are 0-based).
#include <QString>

namespace vedit::workspace::jump {

class IJumpTarget {
public:
    virtual ~IJumpTarget() = default;

    // Opens `filePath` and puts the cursor at `line`/`column` (both 1-based).
    // Returns true only when the jump was actually performed; a failure
    // (missing file, no editor pane, ...) returns false instead of throwing.
    virtual bool jumpTo(const QString &filePath, int line, int column) = 0;
};

} // namespace vedit::workspace::jump
