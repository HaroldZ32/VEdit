// Probe A6 (T14): is the merge specific to single-character typing-like inserts?
// Try 100 insertLine() with distinct text and 100 replaceText() on distinct ranges.
#include <QApplication>
#include <QMetaObject>
#include <QString>
#include <QTextStream>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>

namespace {
void out(const QString &line) { QTextStream(stdout) << line << Qt::endl; }
int undoSafely(KTextEditor::Document *d) { return QMetaObject::invokeMethod(d, "undo") ? 0 : -1; }
int stepsToStable(KTextEditor::Document *d, int cap) {
    int steps = 0;
    QString last = d->text();
    while (steps < cap) {
        if (undoSafely(d) != 0) return -1;
        ++steps;
        const QString now = d->text();
        if (now == last) { --steps; break; }
        last = now;
    }
    return steps;
}
void runInsertLines(KTextEditor::Editor *e) {
    KTextEditor::Document *d = e->createDocument(nullptr);
    d->setText(QStringLiteral("start\n"));
    for (int i = 0; i < 100; ++i) {
        d->insertLine(d->lines(), QStringLiteral("line-%1").arg(i));
    }
    const int steps = stepsToStable(d, 200);
    out(QStringLiteral("STEPS5 insert_lines=100 steps=%1 verdict=%2")
            .arg(steps).arg(steps >= 100 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
    delete d;
}
void runReplaceText(KTextEditor::Editor *e) {
    KTextEditor::Document *d = e->createDocument(nullptr);
    QString text;
    for (int i = 0; i < 100; ++i) text += QStringLiteral("w%1 ").arg(i);
    d->setText(text);
    for (int i = 0; i < 100; ++i) {
        d->replaceText(KTextEditor::Range(0, i * 4, 0, i * 4 + 2), QStringLiteral("R%1").arg(i));
    }
    const int steps = stepsToStable(d, 200);
    out(QStringLiteral("STEPS5 replace_text=100 steps=%1 verdict=%2")
            .arg(steps).arg(steps >= 100 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
    delete d;
}
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (!editor) { out(QStringLiteral("STEPS5 editor=null FAIL")); return 2; }
    runInsertLines(editor);
    runReplaceText(editor);
    return 0;
}
