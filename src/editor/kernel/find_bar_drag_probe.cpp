// T5 / F2 xcb probe: is the Ctrl+F bar really draggable by its BACKGROUND, and
// does a press on a CONTROL leave it alone?
//
// The lane selftest delivers synthetic QMouseEvents straight to the bar object,
// which proves the handler but not the ROUTING: it bypasses the child-widget hit
// test and Qt's implicit grab. T26 (record rec-20261110T025554-a0cf8f) asked for
// the repository's existing xcb pattern (see kernel/fold_gutter_click_probe.cpp,
// ctest id `vedit-fold-gutter-click`): run under Xvfb + xcb, resolve the widget
// under a REAL global pointer position with QApplication::widgetAt(), and only
// then send the press/move/release to that widget.
//
// Assertions, printed as FINDBAR lines:
//   1. the global point over the bar's top-left margin resolves to the BAR
//      itself (every control is a child, so this is the background handle);
//   2. press there, move, release -> the bar moved by the same delta;
//   3. a huge drag clamps the bar inside the editor viewport;
//   4. the global point over the "Next" button resolves to that button, and a
//      press+move there does NOT move the bar (negative control).
//
// SKIP: without an xcb platform (no Xvfb) the probe exits 77 with the reason
// printed, exactly like the fold probe; it never silently passes.
#include "find_replace.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QPoint>
#include <QString>
#include <QToolButton>
#include <QWidget>

#include <cstdio>

namespace {

void pump(int rounds = 10)
{
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents();
    }
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
// widget the window system put under the PRESS. Subsequent moves and the release
// go to the same widget because that is Qt's documented implicit mouse grab;
// resolving them again would break the grab the real drag relies on.
void sendTo(QWidget *target, const QPoint &global, QEvent::Type type, Qt::MouseButtons buttons)
{
    if (target == nullptr) {
        std::printf("FINDBAR no-target-at %d,%d\n", global.x(), global.y());
        return;
    }
    const QPoint local = target->mapFromGlobal(global);
    QMouseEvent event(type, QPointF(local), QPointF(global), Qt::LeftButton, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(target, &event);
    pump(2);
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-find-bar-drag-probe"));

    if (QApplication::platformName() != QLatin1String("xcb")) {
        std::printf("FINDBAR SKIP platform=%s (needs xcb + Xvfb)\n",
                    qPrintable(QApplication::platformName()));
        return 77;
    }
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        std::printf("FINDBAR no-editor\n");
        return 1;
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    if (document == nullptr) {
        std::printf("FINDBAR no-document\n");
        return 1;
    }
    document->setText(QStringLiteral("alpha beta alpha\n").repeated(40));
    KTextEditor::View *view = document->createView(nullptr);
    view->resize(1100, 700);
    view->show();
    view->activateWindow();
    pump(20);

    vedit::editor::FindReplaceBar *bar = vedit::editor::FindReplaceBar::install(view);
    if (bar == nullptr) {
        std::printf("FINDBAR no-bar\n");
        return 1;
    }
    bar->openFind();
    pump(20);
    bar->setPattern(QStringLiteral("alpha"));
    pump(10);
    std::printf("FINDBAR bar=%dx%d at=%d,%d view=%dx%d\n", bar->width(), bar->height(),
                bar->pos().x(), bar->pos().y(), view->width(), view->height());

    int failures = 0;
    const auto check = [&failures](const char *name, bool ok, const QString &detail) {
        std::printf("FINDBAR %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                    detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
        if (!ok) {
            ++failures;
        }
    };

    // 1. The background point really resolves to the bar (no control there).
    const QPoint background = bar->mapToGlobal(QPoint(3, 3));
    QWidget *backgroundTarget = QApplication::widgetAt(background);
    check("background-target-is-bar", backgroundTarget == bar,
          QStringLiteral("global=%1,%2 widget=%3")
              .arg(background.x())
              .arg(background.y())
              .arg(widgetLabel(backgroundTarget)));

    // 2. drag LEFT and DOWN by (-150, +80): the bar follows the pointer exactly
    // (the default position is already at the right edge, so it cannot move
    // further right).
    if (backgroundTarget == bar) {
        const QPoint before = bar->pos();
        const QPoint delta(-150, 80);
        sendTo(bar, background, QEvent::MouseButtonPress, Qt::LeftButton);
        for (int step = 1; step <= 4; ++step) {
            sendTo(bar, background + delta * step / 4, QEvent::MouseMove, Qt::LeftButton);
        }
        sendTo(bar, background + delta, QEvent::MouseButtonRelease, Qt::NoButton);
        pump(4);
        check("background-drag-moves", bar->pos() == before + delta,
              QStringLiteral("moved %1,%2 -> %3,%4 (want +%5,+%6)")
                  .arg(before.x())
                  .arg(before.y())
                  .arg(bar->pos().x())
                  .arg(bar->pos().y())
                  .arg(delta.x())
                  .arg(delta.y()));

        // 3. a drag far past the bottom-right corner clamps to it.
        const QPoint handle = bar->mapToGlobal(QPoint(3, 3));
        sendTo(bar, handle, QEvent::MouseButtonPress, Qt::LeftButton);
        sendTo(bar, handle + QPoint(4000, 4000), QEvent::MouseMove, Qt::LeftButton);
        sendTo(bar, handle + QPoint(4000, 4000), QEvent::MouseButtonRelease, Qt::NoButton);
        pump(4);
        const QPoint clampedTo(view->width() - bar->width(), view->height() - bar->height());
        check("drag-clamped-inside-viewport",
              view->rect().contains(bar->geometry()) && bar->pos() == clampedTo,
              QStringLiteral("bar=%1,%2 (clamp=%3,%4) %5x%6 view=%7x%8")
                  .arg(bar->pos().x())
                  .arg(bar->pos().y())
                  .arg(clampedTo.x())
                  .arg(clampedTo.y())
                  .arg(bar->width())
                  .arg(bar->height())
                  .arg(view->width())
                  .arg(view->height()));
    }

    // 4. negative control: the pointer over the "Next" button resolves to that
    // button, and dragging there must not move the bar.
    auto *nextButton = bar->findChild<QToolButton *>(QStringLiteral("vedit-find-next"));
    if (nextButton == nullptr) {
        check("negative-control-button-present", false, QStringLiteral("no vedit-find-next"));
    } else {
        const QPoint overButton = nextButton->mapToGlobal(nextButton->rect().center());
        QWidget *buttonTarget = QApplication::widgetAt(overButton);
        check("control-target-is-control",
              buttonTarget == nextButton || (buttonTarget != nullptr
                                             && nextButton->isAncestorOf(buttonTarget)),
              QStringLiteral("global=%1,%2 widget=%3")
                  .arg(overButton.x())
                  .arg(overButton.y())
                  .arg(widgetLabel(buttonTarget)));
        const QPoint before = bar->pos();
        // Deliver to the widget the pointer is OVER (the button, or a child of
        // it) — never to the bar.
        sendTo(buttonTarget, overButton, QEvent::MouseButtonPress, Qt::LeftButton);
        sendTo(buttonTarget, overButton + QPoint(-70, 50), QEvent::MouseMove, Qt::LeftButton);
        sendTo(buttonTarget, overButton + QPoint(-70, 50), QEvent::MouseButtonRelease,
               Qt::NoButton);
        pump(4);
        check("control-drag-does-not-move", bar->pos() == before,
              QStringLiteral("pos=%1,%2 (was %3,%4)")
                  .arg(bar->pos().x())
                  .arg(bar->pos().y())
                  .arg(before.x())
                  .arg(before.y()));
    }

    std::printf("FINDBAR SUMMARY failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
