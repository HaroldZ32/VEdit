// Independent read-only probe for the B4 review: measure the widget the mouse
// really lands on, its origin inside KTextEditor::ViewPrivate, and drive the
// real MultiCursorController through both coordinate spaces.
#include "lsp_actions.h"
#include "multi_cursor.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QMainWindow>
#include <QStatusBar>
#include <QTest>
#include <QVariantMap>
#include <QWidget>

#include <cstdio>

static QString className(QObject *o)
{
    return o != nullptr ? QString::fromLatin1(o->metaObject()->className()) : QStringLiteral("none");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        std::printf("SKIP no KTextEditor\n");
        return 77;
    }

    QMainWindow host;
    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document->createView(&host);
    host.setCentralWidget(view);
    host.resize(820, 420);
    document->setText(QStringLiteral("module top;\n  wire sig;\nendmodule\n"));
    view->setProperty("vedit.openFilePath", QStringLiteral("/tmp/b4-definition.sv"));
    host.show();
    view->setFocus();
    QApplication::processEvents();
    QTest::qWait(80);
    QApplication::processEvents();

    std::printf("view class=%s geom=%dx%d\n", qPrintable(className(view)), view->width(), view->height());
    QWidget *inner = nullptr;
    for (QWidget *w : view->findChildren<QWidget *>()) {
        if (QString::fromLatin1(w->metaObject()->className()) == QStringLiteral("KateViewInternal")) {
            inner = w;
        }
    }
    if (inner == nullptr) {
        std::printf("no KateViewInternal\n");
        return 1;
    }
    const QPoint innerOrigin = inner->mapTo(view, QPoint(0, 0));
    std::printf("inner class=%s geom=(%d,%d %dx%d) originInView=(%d,%d) focus=%d\n",
                qPrintable(className(inner)), inner->x(), inner->y(), inner->width(), inner->height(),
                innerOrigin.x(), innerOrigin.y(), QApplication::focusWidget() == inner ? 1 : 0);

    const KTextEditor::Cursor target(1, 2);
    const QPoint viewPoint = view->cursorToCoordinate(target);
    std::printf("cursorToCoordinate(1,2)= view(%d,%d)\n", viewPoint.x(), viewPoint.y());
    const QPoint innerPoint = inner->mapFrom(view, viewPoint);
    std::printf("same point in inner=(%d,%d)\n", innerPoint.x(), innerPoint.y());
    const KTextEditor::Cursor viaViewCoords = view->coordinatesToCursor(viewPoint);
    const KTextEditor::Cursor viaInnerCoords = view->coordinatesToCursor(innerPoint);
    std::printf("coordinatesToCursor(viewPoint)=%d:%d   coordinatesToCursor(innerPoint)=%d:%d\n",
                viaViewCoords.line(), viaViewCoords.column(), viaInnerCoords.line(),
                viaInnerCoords.column());

    // --- install the real controller + channel exactly like the product -----
    auto *action = new QAction(QStringLiteral("&Go to Definition"), &host);
    action->setObjectName(QStringLiteral("action-diagnostics.definition"));
    action->setEnabled(true);
    host.addAction(action);
    int triggered = 0;
    int triggeredLine = -1;
    int triggeredColumn = -1;
    QObject::connect(action, &QAction::triggered, &host, [&]() {
        ++triggered;
        const KTextEditor::Cursor c = view->cursorPosition();
        triggeredLine = c.line();
        triggeredColumn = c.column();
    });
    QVariantMap gates;
    QVariantMap row;
    row.insert(QStringLiteral("enabled"), true);
    row.insert(QStringLiteral("known"), true);
    row.insert(QStringLiteral("source"), QStringLiteral("backend"));
    row.insert(QStringLiteral("reason"), QString());
    gates.insert(QStringLiteral("definition"), row);
    host.setProperty("vedit.diagnostics.gates", QVariant(gates));
    QVariantMap caps;
    caps.insert(QStringLiteral("backend"), QStringLiteral("verible"));
    host.setProperty("vedit.diagnostics.capabilities", QVariant(caps));

    vedit::editor::LspActionChannel *channel = vedit::editor::LspActionChannel::install(view);
    vedit::editor::MultiCursorController *cursors = vedit::editor::MultiCursorController::install(view);
    std::printf("seams channel=%d cursors=%d actionFound=%d\n", channel != nullptr ? 1 : 0,
                cursors != nullptr ? 1 : 0,
                channel != nullptr && channel->shellAction(vedit::editor::EditorAction::Definition) == action ? 1 : 0);

    const auto pressAt = [&](QWidget *target, const QPoint &point, Qt::KeyboardModifiers mods) {
        QTest::mousePress(target, Qt::LeftButton, mods, point);
        QTest::mouseRelease(target, Qt::LeftButton, mods, point);
        QApplication::processEvents();
    };

    // A: the real delivery space (what a user's mouse actually hits).
    vedit::editor::setViewCursors(view, {KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                                         KTextEditor::Cursor(2, 0)});
    QApplication::processEvents();
    triggered = 0;
    pressAt(inner, innerPoint, Qt::ControlModifier);
    std::printf("A ctrl-click on INNER at (%d,%d): triggered=%d line=%d column=%d dispatch=%d cursorCount=%d\n",
                innerPoint.x(), innerPoint.y(), triggered, triggeredLine, triggeredColumn,
                channel != nullptr ? channel->dispatchCount(vedit::editor::EditorAction::Definition) : -1,
                cursors->cursorCount());
    std::printf("A cursorPositions:");
    for (const KTextEditor::Cursor &c : view->cursorPositions()) {
        std::printf(" %d:%d", c.line(), c.column());
    }
    std::printf("\n");

    // B: view space (what the lane selftest sends).
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    triggered = 0;
    const int beforeB = channel != nullptr
                            ? channel->dispatchCount(vedit::editor::EditorAction::Definition)
                            : 0;
    pressAt(view, viewPoint, Qt::ControlModifier);
    std::printf("B ctrl-click on VIEW at (%d,%d): triggered=%d line=%d column=%d dispatchDelta=%d\n",
                viewPoint.x(), viewPoint.y(), triggered, triggeredLine, triggeredColumn,
                (channel != nullptr ? channel->dispatchCount(vedit::editor::EditorAction::Definition)
                                    : 0)
                    - beforeB);

    // C: a plain click through the inner widget is the ground truth for the
    // text position a user at innerPoint is pointing at.
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    QApplication::processEvents();
    pressAt(inner, innerPoint, Qt::NoModifier);
    const KTextEditor::Cursor plain = view->cursorPosition();
    std::printf("C plain click on INNER at (%d,%d): KatePart caret=%d:%d\n", innerPoint.x(),
                innerPoint.y(), plain.line(), plain.column());

    // D: multi-cursor persistence across setCursorPosition / Ctrl+click.
    vedit::editor::setViewCursors(view, {KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                                         KTextEditor::Cursor(2, 0)});
    view->setCursorPosition(KTextEditor::Cursor(1, 2));
    QApplication::processEvents();
    std::printf("D setCursorPosition(1,2) on a 3-cursor view -> count=%d positions:",
                static_cast<int>(view->cursorPositions().size()));
    for (const KTextEditor::Cursor &c : view->cursorPositions()) {
        std::printf(" %d:%d", c.line(), c.column());
    }
    std::printf("\n");

    return 0;
}
