// Probe A3 (T14): per-operation EditingTransaction -> one undo step per operation?
// Probe A showed: N inserts in ONE transaction = 1 undo step (correct), but also
// N inserts with NO transaction = 1 undo step (headless: implicit session never
// closes). If each edit is wrapped in its OWN transaction, do we get N steps?
#include <QApplication>
#include <QCoreApplication>
#include <QMetaObject>
#include <QString>
#include <QTextStream>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

namespace {
void out(const QString &line) { QTextStream(stdout) << line << Qt::endl; }

int undoSafely(KTextEditor::Document *d) {
    return QMetaObject::invokeMethod(d, "undo") ? 0 : -1;
}

// Undo until the document no longer changes (or cap), returning the count.
int stepsToStable(KTextEditor::Document *d, int cap) {
    int steps = 0;
    QString last = d->text();
    while (steps < cap) {
        if (undoSafely(d) != 0) {
            return -1;
        }
        ++steps;
        const QString now = d->text();
        if (now == last) {
            // undo no longer changes anything: we went one past the first state
            --steps;
            break;
        }
        last = now;
    }
    return steps;
}

void run(KTextEditor::Editor *editor, bool withView) {
    const QString tag = withView ? QStringLiteral("view") : QStringLiteral("noview");
    KTextEditor::Document *doc = editor->createDocument(nullptr);
    KTextEditor::View *view = withView ? doc->createView(nullptr) : nullptr;
    doc->setText(QString());

    const int n = 100;
    for (int i = 0; i < n; ++i) {
        KTextEditor::Document::EditingTransaction tx(doc);
        const int line = doc->lines() - 1;
        doc->insertText(KTextEditor::Cursor(line, doc->lineLength(line)), QStringLiteral("x"));
        if (view) {
            view->setCursorPosition(KTextEditor::Cursor(line, doc->lineLength(line)));
        }
    }
    const QString after = doc->text();
    const int steps = stepsToStable(doc, 400);
    out(QStringLiteral("STEPS2 %1 case=one_tx_per_edit len_before=%2 steps_to_stable=%3 "
                       "empty_after=%4 verdict=%5")
            .arg(tag).arg(after.size()).arg(steps)
            .arg(doc->text().isEmpty() ? 1 : 0)
            .arg(steps >= 100 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

    // redo all the way back and compare byte-exact
    QString restored = doc->text();
    for (int i = 0; i < steps; ++i) {
        QMetaObject::invokeMethod(doc, "redo");
    }
    out(QStringLiteral("STEPS2 %1 case=redo_all restored_ok=%2 verdict=%3")
            .arg(tag)
            .arg(doc->text() == after ? 1 : 0)
            .arg(doc->text() == after ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

    // control: 100 edits in ONE transaction must collapse to exactly 1 step
    doc->setText(QString());
    {
        KTextEditor::Document::EditingTransaction tx(doc);
        for (int i = 0; i < 100; ++i) {
            const int line = doc->lines() - 1;
            doc->insertText(KTextEditor::Cursor(line, doc->lineLength(line)), QStringLiteral("y"));
        }
    }
    const int one = stepsToStable(doc, 10);
    out(QStringLiteral("STEPS2 %1 case=one_tx_100_edits steps=%2 verdict=%3")
            .arg(tag).arg(one)
            .arg(one == 1 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

    if (view) {
        delete view;
    }
    delete doc;
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (!editor) {
        out(QStringLiteral("STEPS2 editor=null FAIL"));
        return 2;
    }
    run(editor, false);
    run(editor, true);
    return 0;
}
