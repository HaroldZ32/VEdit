#pragma once

// In-file find / replace on the KTextEditor document.
//
// Probe constraint 2 (evidence/edit-core/00-probe.md §5): searchText() returns
// AT MOST ONE match per call, so matches are collected by iterating with the
// previous match's end() as the next start, and a zero-length match advances by
// one character. All replacements run bottom-up inside ONE EditingTransaction,
// so the whole replace-all is a single undo step.
#include <QList>
#include <QString>
#include <QStringList>

namespace KTextEditor {
class Document;
class Range;
} // namespace KTextEditor

namespace vedit::editor {

struct FindResult {
    bool ok = false;
    int matches = 0;
    QString error;
};

struct ReplaceResult {
    bool ok = false;
    int found = 0;
    int replaced = 0;
    QString error;
};

// All non-overlapping matches of `needle` in forward order.
FindResult findMatches(KTextEditor::Document *document, const QString &needle,
                       bool caseSensitive = true, QList<KTextEditor::Range> *ranges = nullptr);

// Replaces every match with `replacement` inside one editing transaction.
ReplaceResult replaceAll(KTextEditor::Document *document, const QString &needle,
                         const QString &replacement, bool caseSensitive = true);

} // namespace vedit::editor
