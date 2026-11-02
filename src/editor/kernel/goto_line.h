#pragma once

// Go to line (A1d): the requested line is clamped to the document, the cursor is
// moved to column 0 and the result is read back instead of assumed.
#include <QString>

namespace KTextEditor {
class View;
}

namespace vedit::editor {

struct GotoResult {
    bool ok = false;
    int line = 0; // 0-based line the cursor actually sits on
    QString error;
};

// `oneBasedLine` is what a user types. 0/negative clamps to line 1, past the end
// clamps to the last line; an empty document resolves to line 0.
GotoResult gotoLine(KTextEditor::View *view, int oneBasedLine);

} // namespace vedit::editor
