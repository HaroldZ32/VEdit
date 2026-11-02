#pragma once

// Undo / redo of the KTextEditor document (probe constraint 1).
//
// KTextEditor 6.24's public document.h declares neither undo() nor redo(), but
// the runtime document object (KTextEditor::DocumentPrivate) exposes them as
// slots. The probe (evidence/edit-core/00-probe.md §0 A′) recorded undo()=70 and
// redo()=71 at runtime, and the slot number is NOT stable across upgrades.
//
// Therefore: look the method up by NAME, fail loudly when it is missing, and
// never hard-code a slot index or include a private header.
#include <QString>

namespace KTextEditor {
class Document;
}

namespace vedit::editor {

// True when the runtime metaobject exposes an `undo()` / `redo()` slot. The
// product selfcheck asserts this before using undo at all: a framework change
// must turn into a red check, not a silently dead undo button.
bool undoSlotAvailable(KTextEditor::Document *document);
bool redoSlotAvailable(KTextEditor::Document *document);

// One undo / redo step. Returns false (with *error) when the slot is missing or
// the invocation fails; never degrades silently.
bool documentUndo(KTextEditor::Document *document, QString *error = nullptr);
bool documentRedo(KTextEditor::Document *document, QString *error = nullptr);

} // namespace vedit::editor
