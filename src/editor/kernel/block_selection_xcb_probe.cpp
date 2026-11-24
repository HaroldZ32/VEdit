// T7 xcb probe: druck with the pointer a RECTANGLE and prove the native block
// selection SURVIVES the release — Delete once removes the whole block, Copy
// yields its rectangular text — with a line OUTSIDE the rectangle as the
// negative control.
//
// Why a second binary next to the lane selftest: the selftest drives the
// controller's actions directly, which proves the handler but not the ROUTING.
// This probe follows the repository's xcb pattern (kernel/fold_gutter_click_probe
// .cpp, kernel/find_bar_drag_probe.cpp): run under Xvfb + xcb, resolve the widget
// under a REAL global pointer position with QApplication::widgetAt(), and send
// the press/move/release to THAT widget. These are Qt-level synthetic events on
// the real delivery target — NOT OS-level pointer injection (AGENT.md §9/§10).
//
// SKIP: without an xcb platform (no Xvfb) the probe exits 77 with the reason
// printed; it never silently passes.
#include "multi_cursor.h"
#include "undo_ops.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPoint>
#include <QString>
#include <QTest>
#include <QWidget>

#include <cstdio>

namespace {

void pump(int rounds = 10)
{
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents();
    }
}

QString lineText(KTextEditor::Document *document, int line)
{
    return document->line(line);
}

QString widgetLabel(QWidget *widget)
{
    if (widget == nullptr) {
        return QStringLiteral("<none>");
    }
    QString name = QString::fromLatin1(widget->metaObject()->className());
    if (!widget->objectName().isEmpty()) {
        name += QLatin1Char('#') + widget->objectName();
    }
    return name;
}

// One pointer step with a REAL global position, delivered to `target` — the
// widget the window system put under the PRESS. Moves and the release go to the
// same widget because that is Qt's documented implicit mouse grab.
void sendTo(QWidget *target, const QPoint &global, QEvent::Type type, Qt::MouseButtons buttons,
            Qt::KeyboardModifiers modifiers)
{
    if (target == nullptr) {
        std::printf("BLOCK no-target-at %d,%d\n", global.x(), global.y());
        return;
    }
    const QPoint local = target->mapFromGlobal(global);
    QMouseEvent event(type, QPointF(local), QPointF(global), Qt::LeftButton, buttons, modifiers);
    QApplication::sendEvent(target, &event);
    pump(2);
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-block-selection-xcb-probe"));

    if (QApplication::platformName() != QLatin1String("xcb")) {
        std::printf("BLOCK SKIP platform=%s (needs xcb + Xvfb)\n",
                    qPrintable(QApplication::platformName()));
        return 77;
    }
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        std::printf("BLOCK SKIP no-editor\n");
        return 77;
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    if (document == nullptr) {
        std::printf("BLOCK SKIP no-document\n");
        return 77;
    }
    document->setText(QStringLiteral("one\ntwo\nthree\nfour\nfive\n"));
    KTextEditor::View *view = document->createView(nullptr);
    if (view == nullptr) {
        std::printf("BLOCK SKIP no-view\n");
        return 77;
    }
    view->resize(900, 500);
    view->show();
    view->activateWindow();
    view->setFocus();
    pump(30);

    // Same install order as the product's EditCoreProvider.
    vedit::editor::EditorEnhancements *enhancements =
        vedit::editor::EditorEnhancements::install(view);
    vedit::editor::MultiCursorController *multi =
        vedit::editor::MultiCursorController::install(view);
    if (enhancements == nullptr || multi == nullptr) {
        std::printf("BLOCK SKIP no-controllers\n");
        return 77;
    }
    vedit::editor::setViewCursors(view, {KTextEditor::Cursor(0, 0)});
    pump(10);

    int failures = 0;
    const auto check = [&failures](const char *name, bool ok, const QString &detail = QString()) {
        std::printf("BLOCK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                    detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
        if (!ok) {
            ++failures;
        }
        std::fflush(stdout);
    };

    // --- the rectangle: lines 1..3, columns 0..2 --------------------------
    const KTextEditor::Cursor anchor(1, 0);
    const KTextEditor::Cursor extent(3, 2);
    const QPoint startGlobal = view->mapToGlobal(view->cursorToCoordinate(anchor));
    const QPoint endGlobal = view->mapToGlobal(view->cursorToCoordinate(extent));
    QWidget *target = QApplication::widgetAt(startGlobal);
    check("delivery-target",
          target != nullptr && target != view && view->isAncestorOf(target),
          widgetLabel(target));
    if (target == nullptr) {
        std::printf("BLOCK SUMMARY failures=%d\n", failures);
        return 1;
    }
    const Qt::KeyboardModifiers modifiers = Qt::ShiftModifier | Qt::AltModifier;
    sendTo(target, startGlobal, QEvent::MouseButtonPress, Qt::LeftButton, modifiers);
    sendTo(target, endGlobal, QEvent::MouseMove, Qt::LeftButton, modifiers);
    sendTo(target, endGlobal, QEvent::MouseButtonRelease, Qt::NoButton, modifiers);
    pump(10);

    check("selection-kept", view->blockSelection() && view->selection());
    const KTextEditor::Range rectangle = view->selectionRange();
    check("rectangle-range",
          rectangle == KTextEditor::Range(anchor, extent),
          QStringLiteral("%1,%2 -> %3,%4")
              .arg(rectangle.start().line())
              .arg(rectangle.start().column())
              .arg(rectangle.end().line())
              .arg(rectangle.end().column()));
    check("native-mode-on", view->blockSelection());
    check("cursor-count-one", multi->cursorCount() == 1,
          QString::number(multi->cursorCount()));

    // --- Copy: the rectangle text -----------------------------------------
    QWidget *keyTarget = QApplication::focusWidget() != nullptr ? QApplication::focusWidget()
                                                                : view;
    const QString rectangleText = view->selectionText();
    check("copy-text", rectangleText == QStringLiteral("tw\nth\nfo"), rectangleText);
    QGuiApplication::clipboard()->clear();
    QTest::keyClick(keyTarget, Qt::Key_C, Qt::ControlModifier);
    pump(10);
    check("copy-clipboard", QGuiApplication::clipboard()->text() == rectangleText,
          QGuiApplication::clipboard()->text());

    // --- Delete ONCE removes the whole block; line 0 (outside) is untouched -
    const QString original = document->text();
    QTest::keyClick(keyTarget, Qt::Key_Delete);
    pump(10);
    const QString afterDelete = QStringLiteral("[0]='%1', [1]='%2', [2]='%3', [3]='%4', [4]='%5'")
                                    .arg(lineText(document, 0), lineText(document, 1),
                                         lineText(document, 2), lineText(document, 3),
                                         lineText(document, 4));
    check("delete-block",
          lineText(document, 1) == QStringLiteral("o") &&
              lineText(document, 2) == QStringLiteral("ree") &&
              lineText(document, 3) == QStringLiteral("ur"),
          afterDelete);
    check("delete-outside-line", lineText(document, 0) == QStringLiteral("one"), afterDelete);

    QString undoError;
    check("one-undo",
          vedit::editor::documentUndo(document, &undoError) && document->text() == original,
          undoError);

    std::printf("BLOCK SUMMARY failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
