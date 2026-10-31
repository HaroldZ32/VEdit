// Probe B (T14): replaceText batch semantics — adjacent matches, zero-length
// matches (regex anchors), longer and shorter replacements; each batch must be
// ONE undo step (inside a single EditingTransaction).
#include <QApplication>
#include <QCryptographicHash>
#include <QMetaObject>
#include <QString>
#include <QTextStream>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>

namespace {
QString sha(const QString &s) {
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex());
}
void out(const QString &line) { QTextStream(stdout) << line << Qt::endl; }

// The intended implementation: find every match first, then replace bottom-up
// inside one transaction, so earlier matches cannot shift later ranges.
int replaceAll(KTextEditor::Document *doc, const QString &pattern, const QString &replacement,
               KTextEditor::SearchOptions options) {
    const auto matches = doc->searchText(KTextEditor::Range(0, 0, doc->lines(), 0), pattern, options);
    int n = 0;
    KTextEditor::Document::EditingTransaction tx(doc);
    for (int i = matches.size() - 1; i >= 0; --i) {
        const auto &r = matches[i];
        if (!r.isValid()) {
            continue;
        }
        if (doc->replaceText(r, replacement)) {
            ++n;
        }
    }
    return n;
}

bool oneUndoBack(KTextEditor::Document *doc, const QString &expected) {
    if (doc->metaObject()->indexOfMethod("undo()") < 0) {
        return false;
    }
    if (!QMetaObject::invokeMethod(doc, "undo")) {
        return false;
    }
    return doc->text() == expected;
}

void runCase(KTextEditor::Editor *editor, const QString &name, const QString &initial,
             const QString &pattern, const QString &replacement, KTextEditor::SearchOptions options,
             const QString &expected, int expectedCount) {
    KTextEditor::Document *doc = editor->createDocument(nullptr);
    doc->setText(initial);
    const int n = replaceAll(doc, pattern, replacement, options);
    const QString got = doc->text();
    const bool textOk = got == expected;
    const bool countOk = n == expectedCount;
    const bool undoOk = oneUndoBack(doc, initial);
    out(QStringLiteral("REPLACE case=%1 matches=%2 expect_matches=%3 text_sha=%4 expect_sha=%5 "
                       "text_ok=%6 count_ok=%7 one_undo_ok=%8 verdict=%9")
            .arg(name).arg(n).arg(expectedCount).arg(sha(got), sha(expected))
            .arg(textOk ? 1 : 0).arg(countOk ? 1 : 0).arg(undoOk ? 1 : 0)
            .arg(textOk && countOk && undoOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
    delete doc;
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (!editor) {
        out(QStringLiteral("REPLACE editor=null FAIL"));
        return 2;
    }
    // 1. adjacent matches
    runCase(editor, QStringLiteral("adjacent"), QStringLiteral("aaaa"), QStringLiteral("a"),
            QStringLiteral("bb"), KTextEditor::Default, QStringLiteral("bbbbbbbb"), 4);
    // 2. zero-length matches: line-start anchor with regex
    runCase(editor, QStringLiteral("zero_length_anchor"), QStringLiteral("one\ntwo\nthree"),
            QStringLiteral("^"), QStringLiteral(">"), KTextEditor::Regex,
            QStringLiteral(">one\n>two\n>three"), 3);
    // 3. replacement longer than the match
    runCase(editor, QStringLiteral("longer"), QStringLiteral("foo bar foo"), QStringLiteral("foo"),
            QStringLiteral("foobarbaz"), KTextEditor::Default,
            QStringLiteral("foobarbaz bar foobarbaz"), 2);
    // 4. replacement shorter than the match
    runCase(editor, QStringLiteral("shorter"), QStringLiteral("alpha beta alpha"), QStringLiteral("alpha"),
            QStringLiteral("a"), KTextEditor::Default, QStringLiteral("a beta a"), 2);
    return 0;
}
