// Probe B2 (T14): searchText returns ONE match per call (probe B showed 1 instead
// of N). Iterate it to collect all matches, handling zero-length matches, then
// replace bottom-up in one transaction and assert text + match count + one undo.
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

// Collect every match by advancing the search start past the previous match.
// A zero-length match advances by one character, otherwise the loop would spin.
QList<KTextEditor::Range> findAll(KTextEditor::Document *doc, const QString &pattern,
                                  KTextEditor::SearchOptions options) {
    QList<KTextEditor::Range> all;
    KTextEditor::Cursor start(0, 0);
    const KTextEditor::Cursor end(doc->lines(), 0);
    for (int guard = 0; guard < 100000; ++guard) {
        const auto matches = doc->searchText(KTextEditor::Range(start, end), pattern, options);
        if (matches.isEmpty() || !matches.first().isValid()) {
            break;
        }
        const auto m = matches.first();
        if (m.start() < start) { // never go backwards
            break;
        }
        all.append(m);
        if (m.start() == m.end()) {
            // zero-length: step one character forward
            const int line = m.end().line();
            const int col = m.end().column();
            if (col < doc->lineLength(line)) {
                start = KTextEditor::Cursor(line, col + 1);
            } else if (line + 1 < doc->lines()) {
                start = KTextEditor::Cursor(line + 1, 0);
            } else {
                break;
            }
        } else {
            start = m.end();
        }
    }
    return all;
}

int replaceAll(KTextEditor::Document *doc, const QString &pattern, const QString &replacement,
               KTextEditor::SearchOptions options, int *found) {
    const auto matches = findAll(doc, pattern, options);
    if (found) {
        *found = matches.size();
    }
    int n = 0;
    KTextEditor::Document::EditingTransaction tx(doc);
    for (int i = matches.size() - 1; i >= 0; --i) {
        if (doc->replaceText(matches[i], replacement)) {
            ++n;
        }
    }
    return n;
}

bool oneUndoBack(KTextEditor::Document *doc, const QString &expected) {
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
    int found = -1;
    const int n = replaceAll(doc, pattern, replacement, options, &found);
    const QString got = doc->text();
    const bool textOk = got == expected;
    const bool countOk = (n == expectedCount) && (found == expectedCount);
    const bool undoOk = oneUndoBack(doc, initial);
    out(QStringLiteral("REPLACE2 case=%1 found=%2 replaced=%3 expect=%4 text_sha=%5 expect_sha=%6 "
                       "text_ok=%7 count_ok=%8 one_undo_ok=%9 verdict=%10")
            .arg(name).arg(found).arg(n).arg(expectedCount).arg(sha(got), sha(expected))
            .arg(textOk ? 1 : 0).arg(countOk ? 1 : 0).arg(undoOk ? 1 : 0)
            .arg(textOk && countOk && undoOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
    delete doc;
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (!editor) {
        out(QStringLiteral("REPLACE2 editor=null FAIL"));
        return 2;
    }
    runCase(editor, QStringLiteral("adjacent"), QStringLiteral("aaaa"), QStringLiteral("a"),
            QStringLiteral("bb"), KTextEditor::Default, QStringLiteral("bbbbbbbb"), 4);
    runCase(editor, QStringLiteral("zero_length_anchor"), QStringLiteral("one\ntwo\nthree"),
            QStringLiteral("^"), QStringLiteral(">"), KTextEditor::Regex,
            QStringLiteral(">one\n>two\n>three"), 3);
    runCase(editor, QStringLiteral("longer"), QStringLiteral("foo bar foo"), QStringLiteral("foo"),
            QStringLiteral("foobarbaz"), KTextEditor::Default,
            QStringLiteral("foobarbaz bar foobarbaz"), 2);
    runCase(editor, QStringLiteral("shorter"), QStringLiteral("alpha beta alpha"), QStringLiteral("alpha"),
            QStringLiteral("a"), KTextEditor::Default, QStringLiteral("a beta a"), 2);
    return 0;
}
