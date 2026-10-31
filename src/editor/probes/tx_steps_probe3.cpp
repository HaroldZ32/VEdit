// Probe A4 (T14): can we obtain MANY distinct undo steps?
//  (1) is DocumentPrivate::setUndoMergeAllEdits(bool) reachable (slot)?
//  (2) do real key events on a view (QTest::keyClicks) create per-keystroke undo steps?
#include <QApplication>
#include <QMetaObject>
#include <QString>
#include <QTextStream>
#include <QtTest/QTest>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

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
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (!editor) { out(QStringLiteral("STEPS3 editor=null FAIL")); return 2; }

    // (1) merge flag reachable?
    KTextEditor::Document *d = editor->createDocument(nullptr);
    const int mergeIdx = d->metaObject()->indexOfMethod("setUndoMergeAllEdits(bool)");
    out(QStringLiteral("STEPS3 mergeFlag indexOfMethod=%1 invokable=%2")
            .arg(mergeIdx)
            .arg(mergeIdx >= 0 ? QStringLiteral("1") : QStringLiteral("0")));
    if (mergeIdx >= 0) {
        QMetaObject::invokeMethod(d, "setUndoMergeAllEdits", Q_ARG(bool, false));
        d->setText(QString());
        for (int i = 0; i < 3; ++i) {
            KTextEditor::Document::EditingTransaction tx(d);
            const int line = d->lines() - 1;
            d->insertText(KTextEditor::Cursor(line, d->lineLength(line)), QStringLiteral("q"));
        }
        out(QStringLiteral("STEPS3 mergeFlag=off case=3_tx steps=%1 verdict=%2")
                .arg(stepsToStable(d, 10))
                .arg(stepsToStable(d, 10) < 0 ? QStringLiteral("FAIL") : QStringLiteral("SEE_STEPS")));
    }
    delete d;

    // (2) real key events on a view
    KTextEditor::Document *kd = editor->createDocument(nullptr);
    KTextEditor::View *view = kd->createView(nullptr);
    kd->setText(QString());
    view->setFocus();
    const int n = 40; // 40 keystrokes is enough to decide the granularity question
    for (int i = 0; i < n; ++i) {
        QTest::keyClicks(view, QStringLiteral("k"));
    }
    const QString after = kd->text();
    const int steps = stepsToStable(kd, 200);
    out(QStringLiteral("STEPS3 keyevents chars=%1 steps_to_stable=%2 verdict=%3")
            .arg(after.size()).arg(steps)
            .arg(steps >= n ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
    QString restored = kd->text();
    for (int i = 0; i < steps; ++i) QMetaObject::invokeMethod(kd, "redo");
    out(QStringLiteral("STEPS3 keyevents redo_all restored_ok=%1")
            .arg(kd->text() == after ? 1 : 0));
    delete view;
    delete kd;
    return 0;
}
