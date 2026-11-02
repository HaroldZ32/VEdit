#include "find_replace.h"

#include <KTextEditor/Document>

#include <QList>

namespace vedit::editor {
namespace {

KTextEditor::Cursor advanceOneCharacter(KTextEditor::Document *document,
                                        const KTextEditor::Cursor &position)
{
    const int line = position.line();
    if (position.column() < document->lineLength(line)) {
        return KTextEditor::Cursor(line, position.column() + 1);
    }
    if (line + 1 < document->lines()) {
        return KTextEditor::Cursor(line + 1, 0);
    }
    return KTextEditor::Cursor(-1, -1);
}

bool atOrAfter(const KTextEditor::Cursor &left, const KTextEditor::Cursor &right)
{
    if (left.line() != right.line()) {
        return left.line() > right.line();
    }
    return left.column() >= right.column();
}

} // namespace

FindResult findMatches(KTextEditor::Document *document, const QString &needle,
                       bool caseSensitive, QList<KTextEditor::Range> *ranges)
{
    FindResult result;
    if (ranges != nullptr) {
        ranges->clear();
    }
    if (document == nullptr) {
        result.error = QStringLiteral("find: no document");
        return result;
    }
    if (needle.isEmpty()) {
        result.error = QStringLiteral("find: the pattern is empty");
        return result;
    }

    const int lastLine = document->lines() - 1;
    if (lastLine < 0) {
        result.ok = true;
        return result;
    }
    // End the interval at the last real character: (lines(), 0) makes the
    // KTextEditor search emit "line 1 is not within interval" noise.
    const KTextEditor::Range wholeDocument(KTextEditor::Cursor(0, 0),
                                           KTextEditor::Cursor(lastLine,
                                                               document->lineLength(lastLine)));
    KTextEditor::SearchOptions options = KTextEditor::Default;
    if (!caseSensitive) {
        options |= KTextEditor::CaseInsensitive;
    }

    KTextEditor::Cursor start(0, 0);
    while (true) {
        if (atOrAfter(start, wholeDocument.end())) {
            break;
        }
        const QList<KTextEditor::Range> matches =
            document->searchText(KTextEditor::Range(start, wholeDocument.end()), needle, options);
        if (matches.isEmpty() || !matches.first().isValid()) {
            break;
        }
        const KTextEditor::Range match = matches.first();
        if (ranges != nullptr) {
            ranges->append(match);
        }
        ++result.matches;

        KTextEditor::Cursor next = match.end();
        if (match.start() == match.end()) {
            next = advanceOneCharacter(document, match.end());
        }
        if (next.line() < 0 || atOrAfter(start, next)) {
            break;
        }
        start = next;
    }
    result.ok = true;
    return result;
}

ReplaceResult replaceAll(KTextEditor::Document *document, const QString &needle,
                         const QString &replacement, bool caseSensitive)
{
    ReplaceResult result;
    QList<KTextEditor::Range> ranges;
    const FindResult found = findMatches(document, needle, caseSensitive, &ranges);
    if (!found.ok) {
        result.error = found.error;
        return result;
    }
    result.found = found.matches;
    if (ranges.isEmpty()) {
        result.ok = true;
        return result;
    }

    // One transaction = one undo step for the whole replace-all. The matches are
    // in forward order, so replacing from the last to the first keeps every
    // earlier range valid (later replacements cannot shift it).
    {
        KTextEditor::Document::EditingTransaction transaction(document);
        for (int index = ranges.size() - 1; index >= 0; --index) {
            if (document->replaceText(ranges.at(index), replacement)) {
                ++result.replaced;
            }
        }
    }
    if (result.replaced != result.found) {
        result.error = QStringLiteral("replace-all replaced %1 of %2 matches")
                           .arg(result.replaced)
                           .arg(result.found);
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace vedit::editor
