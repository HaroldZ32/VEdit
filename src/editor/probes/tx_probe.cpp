// Probe A (T14): does KTextEditor::Document::EditingTransaction merge N edits
// into ONE undo step, with and without a view? Also: is undo() reachable at all
// from the public Document object (the installed header does not declare it)?
#include <QApplication>
#include <QCryptographicHash>
#include <QMetaObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

namespace {
QString sha(const QString &s) {
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool undoViaMeta(KTextEditor::Document *d, int *index) {
    if (index) {
        *index = d->metaObject()->indexOfMethod("undo()");
    }
    return QMetaObject::invokeMethod(d, "undo");
}
bool redoViaMeta(KTextEditor::Document *d) {
    return QMetaObject::invokeMethod(d, "redo");
}
void out(const QString &line) {
    QTextStream(stdout) << line << Qt::endl;
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (!editor) {
        out(QStringLiteral("TX editor=null FAIL"));
        return 2;
    }
    out(QStringLiteral("TX editor=ok class=%1").arg(QStringLiteral("KTextEditor")));

    for (const bool withView : {false, true}) {
        KTextEditor::Document *doc = editor->createDocument(nullptr);
        if (!doc) {
            out(QStringLiteral("TX doc=null FAIL view=%1").arg(withView));
            return 2;
        }
        KTextEditor::View *view = withView ? doc->createView(nullptr) : nullptr;
        const QString tag = withView ? QStringLiteral("view") : QStringLiteral("noview");

        int undoIndex = -1;
        const int idx = doc->metaObject()->indexOfMethod("undo()");
        undoIndex = idx;
        out(QStringLiteral("TX %1 metaClass=%2 undoIndex=%3 redoIndex=%4")
                .arg(tag, QString::fromLatin1(doc->metaObject()->className()))
                .arg(idx)
                .arg(doc->metaObject()->indexOfMethod("redo()")));
        if (idx < 0) {
            out(QStringLiteral("TX %1 undo_unreachable=1").arg(tag));
            delete doc;
            continue;
        }

        // Case 1: three inserts, no transaction -> one undo must revert only the last one.
        doc->setText(QString());
        for (int i = 0; i < 3; ++i) {
            doc->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("x"));
        }
        const QString after3 = doc->text();
        undoViaMeta(doc, &undoIndex);
        const QString after1Undo = doc->text();
        out(QStringLiteral("TX %1 case=plain3_insert text3=%2 after_undo=%3 verdict=%4")
                .arg(tag, sha(after3), sha(after1Undo),
                     after1Undo == QStringLiteral("xx") ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        // Case 2: three inserts inside ONE transaction -> one undo back to empty.
        doc->setText(QString());
        {
            KTextEditor::Document::EditingTransaction tx(doc);
            for (int i = 0; i < 3; ++i) {
                doc->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("x"));
            }
        }
        const QString txText = doc->text();
        undoViaMeta(doc, &undoIndex);
        const QString txUndo = doc->text();
        out(QStringLiteral("TX %1 case=tx3_insert text=%2 after_undo=%3 empty=%4 verdict=%5")
                .arg(tag, sha(txText), sha(txUndo), txUndo.isEmpty() ? QStringLiteral("1") : QStringLiteral("0"),
                     txUndo.isEmpty() ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        // Case 3: 100 inserts in one transaction -> one undo back to empty.
        doc->setText(QString());
        {
            KTextEditor::Document::EditingTransaction tx(doc);
            for (int i = 0; i < 100; ++i) {
                doc->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("y"));
            }
        }
        const QString tx100 = doc->text();
        undoViaMeta(doc, &undoIndex);
        const QString tx100Undo = doc->text();
        out(QStringLiteral("TX %1 case=tx100_insert len=%2 after_undo_len=%3 empty=%4 verdict=%5")
                .arg(tag).arg(tx100.size()).arg(tx100Undo.size())
                .arg(tx100Undo.isEmpty() ? QStringLiteral("1") : QStringLiteral("0"),
                     tx100Undo.isEmpty() ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        // Case 4: 100 inserts WITHOUT a transaction -> one undo reverts exactly one.
        doc->setText(QString());
        for (int i = 0; i < 100; ++i) {
            doc->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("z"));
        }
        const QString plain100 = doc->text();
        undoViaMeta(doc, &undoIndex);
        const QString plain100Undo = doc->text();
        out(QStringLiteral("TX %1 case=plain100_insert len=%2 after_undo_len=%3 one_step=%4 verdict=%5")
                .arg(tag).arg(plain100.size()).arg(plain100Undo.size())
                .arg(plain100Undo.size() == plain100.size() - 1 ? QStringLiteral("1") : QStringLiteral("0"),
                     plain100Undo.size() == plain100.size() - 1 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        // Case 5: redo after the transaction undo must restore all 100.
        redoViaMeta(doc); // redo the one-step undo of case 4
        out(QStringLiteral("TX %1 case=redo_after_plain_undo len=%2 verdict=%3")
                .arg(tag).arg(doc->text().size())
                .arg(doc->text().size() == plain100.size() ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        if (view) {
            delete view;
        }
        delete doc;
    }
    return 0;
}
