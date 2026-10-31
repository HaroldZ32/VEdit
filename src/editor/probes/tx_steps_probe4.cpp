// Probe A5 (T14): can user-level key events (or event-loop boundaries between
// programmatic edits) produce per-edit undo steps?
#include <QApplication>
#include <QCoreApplication>
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
    if (!editor) { out(QStringLiteral("STEPS4 editor=null FAIL")); return 2; }

    // (1) real key events on a SHOWN view
    KTextEditor::Document *kd = editor->createDocument(nullptr);
    KTextEditor::View *view = kd->createView(nullptr);
    kd->setText(QString());
    view->show();
    QApplication::processEvents();
    view->setFocus();
    QApplication::processEvents();
    const int n = 40;
    for (int i = 0; i < n; ++i) {
        QTest::keyClicks(view, QStringLiteral("k"));
        QApplication::processEvents();
    }
    const QString after = kd->text();
    const int keySteps = stepsToStable(kd, 200);
    out(QStringLiteral("STEPS4 keys chars=%1 steps_to_stable=%2 verdict=%3")
            .arg(after.size()).arg(keySteps)
            .arg(after.size() > 0 && keySteps >= after.size() ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

    // (2) programmatic edits with an event-loop boundary between each
    KTextEditor::Document *d = editor->createDocument(nullptr);
    d->setText(QString());
    for (int i = 0; i < 10; ++i) {
        const int line = d->lines() - 1;
        d->insertText(KTextEditor::Cursor(line, d->lineLength(line)), QStringLiteral("p"));
        QApplication::processEvents();
    }
    const int progSteps = stepsToStable(d, 100);
    out(QStringLiteral("STEPS4 prog_with_events chars=%1 steps=%2 verdict=%3")
            .arg(10).arg(progSteps)
            .arg(progSteps >= 10 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

    if (view) delete view;
    delete kd;
    delete d;
    return 0;
}
