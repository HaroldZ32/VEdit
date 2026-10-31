// Probe A2 (T14): how many DISTINCT undo steps does KTextEditor produce, and how
// do we make 100+ of them? Probe A showed consecutive insertText() calls at the
// same cursor merge into ONE undo step even without a transaction.
#include <QApplication>
#include <QCoreApplication>
#include <QMetaObject>
#include <QString>
#include <QTextStream>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>

namespace {
void out(const QString &line) { QTextStream(stdout) << line << Qt::endl; }

int undoCount(KTextEditor::Document *d) {
    int value = -1;
    if (!QMetaObject::invokeMethod(d, "undoCount", Q_RETURN_ARG(int, value))) {
        return -1;
    }
    return value;
}

int stepsToEmpty(KTextEditor::Document *d, int cap) {
    int steps = 0;
    while (steps < cap && !d->text().isEmpty()) {
        if (!QMetaObject::invokeMethod(d, "undo")) {
            return -1;
        }
        ++steps;
    }
    return steps;
}

enum class Mode { SamePos, DifferentPos, DifferentPosEvents, InsertLines };

void run(KTextEditor::Editor *editor, Mode mode, const QString &name) {
    KTextEditor::Document *doc = editor->createDocument(nullptr);
    doc->setText(QString());
    const int n = 100;
    for (int i = 0; i < n; ++i) {
        switch (mode) {
        case Mode::SamePos:
            doc->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("x"));
            break;
        case Mode::DifferentPos: {
            // append one character at the end of the document (a new position each time)
            const int line = doc->lines() - 1;
            doc->insertText(KTextEditor::Cursor(line, doc->lineLength(line)), QStringLiteral("x"));
            break;
        }
        case Mode::DifferentPosEvents: {
            const int line = doc->lines() - 1;
            doc->insertText(KTextEditor::Cursor(line, doc->lineLength(line)), QStringLiteral("x"));
            QCoreApplication::processEvents();
            break;
        }
        case Mode::InsertLines:
            doc->insertLine(doc->lines(), QStringLiteral("line"));
            break;
        }
    }
    const int uc = undoCount(doc);
    const int steps = stepsToEmpty(doc, 400);
    out(QStringLiteral("STEPS mode=%1 len_before=%2 undoCount=%3 steps_to_empty=%4 verdict=%5")
            .arg(name).arg(doc->text().size()).arg(uc).arg(steps)
            .arg(steps >= 100 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
    delete doc;
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (!editor) {
        out(QStringLiteral("STEPS editor=null FAIL"));
        return 2;
    }
    run(editor, Mode::SamePos, QStringLiteral("same_pos"));
    run(editor, Mode::DifferentPos, QStringLiteral("different_pos"));
    run(editor, Mode::DifferentPosEvents, QStringLiteral("different_pos_events"));
    run(editor, Mode::InsertLines, QStringLiteral("insert_lines"));
    // sanity: is undoCount invokable at all?
    KTextEditor::Document *d = editor->createDocument(nullptr);
    out(QStringLiteral("STEPS meta undo()=%1 redo()=%2 undoCount()=%3")
            .arg(d->metaObject()->indexOfMethod("undo()"))
            .arg(d->metaObject()->indexOfMethod("redo()"))
            .arg(d->metaObject()->indexOfMethod("undoCount()")));
    delete d;
    return 0;
}
