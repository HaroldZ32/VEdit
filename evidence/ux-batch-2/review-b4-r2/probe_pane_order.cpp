// Independent read-only probe (review-b4-r2, no product source is edited).
//
// Questions:
//   R1 — is the point the controller receives mapped into VIEW coordinates for
//        the real delivery target (KatePart's KateViewInternal) on the click
//        paths?
//   R2 — does a successful Ctrl+click collapse a 3-cursor set to one, and does
//        the refusal path stay a plain click (which collapses natively)?
//   R3 — with the new application-level pane activation in EditorArea, does a
//        Ctrl+click in a NON-active pane activate it BEFORE the kernel's
//        object-level filter dispatches? Application filters run before object
//        filters; a recorder filter installed on the delivery widget records
//        EditorArea::activePane() at the start of the object-filter pass. A
//        handler connected to `action-diagnostics.definition` then reads exactly
//        what BackendWiring::beginNavigation reads:
//        activePane()->currentFilePath() and the active pane view's caret.
//   R4 — does the one-time refusal latch re-arm when availability returns (the
//        same reason is surfaced once more), and is a changed reason surfaced?
//
// Everything is the real product classes: EditorArea with TWO panes, the real
// edit-core provider (which installs the real MultiCursorController and
// LspActionChannel), a QMainWindow host with the published gates property, ONE
// shell QAction and a status bar.
//
// Build inside the pinned image: see build-probe.sh in this directory.
// Run offscreen: QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-probe /tmp/probe_pane_order

#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "lsp_actions.h"
#include "multi_cursor.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPoint>
#include <QStatusBar>
#include <QTest>
#include <QVariantMap>
#include <QWidget>

#include <cstdio>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const QString &name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("CHECK %s FAIL%s\n", qPrintable(name),
                    detail.isEmpty() ? "" : qPrintable(QStringLiteral(" (%1)").arg(detail)));
        return;
    }
    std::printf("CHECK %s PASS\n", qPrintable(name));
}

QString widgetName(const QWidget *widget)
{
    return widget != nullptr ? QString::fromLatin1(widget->metaObject()->className())
                             : QStringLiteral("none");
}

QWidget *deliveryTarget(KTextEditor::View *view, const QPoint &viewPoint)
{
    if (view == nullptr) {
        return nullptr;
    }
    QWidget *target = view->childAt(viewPoint);
    return target != nullptr ? target : view;
}

QString paneId(vedit::app::EditorPane *pane)
{
    return pane != nullptr ? pane->paneId() : QStringLiteral("none");
}

void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(content);
        file.close();
    }
}

void publishDefinitionGate(QMainWindow *host, QAction *action, bool enabled,
                           const QString &reason)
{
    QVariantMap row;
    row.insert(QStringLiteral("enabled"), enabled);
    row.insert(QStringLiteral("known"), true);
    row.insert(QStringLiteral("source"), QStringLiteral("backend"));
    row.insert(QStringLiteral("reason"), reason);
    QVariantMap gates;
    gates.insert(QStringLiteral("definition"), row);
    host->setProperty("vedit.diagnostics.gates", gates);
    QVariantMap capabilities;
    capabilities.insert(QStringLiteral("backend"), QStringLiteral("verible"));
    host->setProperty("vedit.diagnostics.capabilities", capabilities);
    if (action != nullptr) {
        action->setEnabled(enabled);
        action->setToolTip(enabled ? QString() : reason);
    }
}

// Object-level recorder installed on the click's delivery widget: it runs in the
// object-filter pass, i.e. AFTER every application-level filter and BEFORE the
// widget's own mouse handling. `paneAtPress` is EditorArea::activePane() at that
// instant.
struct FilterRecorder : QObject {
    vedit::app::EditorArea *area = nullptr;
    vedit::app::EditorPane *paneAtPress = nullptr;
    QObject *pressReceiver = nullptr;
    int presses = 0;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonPress) {
            ++presses;
            pressReceiver = watched;
            paneAtPress = area != nullptr ? area->activePane() : nullptr;
            std::printf("RECORDER press receiver=%s activeAtPress=%s\n",
                        qPrintable(widgetName(qobject_cast<QWidget *>(watched))),
                        qPrintable(paneId(paneAtPress)));
            std::fflush(stdout);
        }
        return false;
    }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    if (KTextEditor::Editor::instance() == nullptr) {
        std::printf("SKIP: the KTextEditor plugin is unavailable\n");
        return 77;
    }

    const QString pathA = QStringLiteral("/tmp/b4-r2-probe-a.sv");
    const QString pathB = QStringLiteral("/tmp/b4-r2-probe-b.sv");
    writeFile(pathA, "module a;\n  wire aa;\nendmodule\n");
    writeFile(pathB, "module b;\n  wire bb;\nendmodule\n");

    // The product shape: a QMainWindow host with the EditorArea, the gates
    // property, one QAction and the status bar the refusal path writes to.
    QMainWindow host;
    auto *area = new vedit::app::EditorArea(&host);
    host.setCentralWidget(area);
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    area->setProvider(provider.get());
    host.resize(1200, 760);
    host.show();
    QTest::qWait(100);
    QApplication::processEvents();

    vedit::app::EditorPane *paneA = area->ensurePane();
    check(QStringLiteral("setup.pane-a"), paneA != nullptr && area->activePane() == paneA);
    if (paneA == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, ++g_failures);
        return 1;
    }
    check(QStringLiteral("setup.open-a"), paneA->openFile(pathA), paneA->currentFilePath());
    QApplication::processEvents();

    vedit::app::EditorPane *paneB = area->splitActive(Qt::Horizontal);
    QApplication::processEvents();
    QTest::qWait(100);
    check(QStringLiteral("setup.pane-b"),
          paneB != nullptr && paneB != paneA && area->paneCount() == 2);
    if (paneB == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, ++g_failures);
        return 1;
    }
    check(QStringLiteral("setup.open-b"), paneB->openFile(pathB), paneB->currentFilePath());
    QApplication::processEvents();
    QTest::qWait(50);

    // A is the previously active pane; clicks land in B.
    area->setActivePane(paneA);
    QApplication::processEvents();
    check(QStringLiteral("setup.a-active"),
          area->activePane() == paneA && paneA->isActive() && !paneB->isActive());

    KTextEditor::View *viewA = vedit::editor::viewFor(paneA->editorWidget());
    KTextEditor::View *viewB = vedit::editor::viewFor(paneB->editorWidget());
    check(QStringLiteral("setup.view-b"), viewB != nullptr, widgetName(paneB->editorWidget()));
    if (viewA == nullptr || viewB == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, ++g_failures);
        return 1;
    }

    auto *definitionAction = new QAction(QStringLiteral("&Go to Definition"), &host);
    definitionAction->setObjectName(QStringLiteral("action-diagnostics.definition"));
    definitionAction->setEnabled(true);
    host.addAction(definitionAction);
    publishDefinitionGate(&host, definitionAction, true, QString());

    struct HandlerRead {
        int triggered = 0;
        vedit::app::EditorPane *pane = nullptr;
        QString file;
        KTextEditor::Cursor caret;
        bool caretValid = false;
    } handler;
    QObject::connect(definitionAction, &QAction::triggered, &host, [&handler, area]() {
        ++handler.triggered;
        handler.pane = area->activePane();
        if (handler.pane == nullptr) {
            return;
        }
        // Exactly what BackendWiring::beginNavigation reads.
        handler.file = handler.pane->currentFilePath();
        auto *view = qobject_cast<KTextEditor::View *>(handler.pane->editorWidget());
        if (view != nullptr) {
            handler.caret = view->cursorPosition();
            handler.caretValid = true;
        }
        std::printf("HANDLER active=%s file=%s caret=%d:%d\n",
                    qPrintable(paneId(handler.pane)), qPrintable(handler.file),
                    handler.caret.line(), handler.caret.column());
        std::fflush(stdout);
    });

    // ---- R1 + R3: one real Ctrl+click into the non-active pane's inner widget.
    viewA->setCursorPosition(KTextEditor::Cursor(3, 0));
    const KTextEditor::Cursor aCaretBefore = viewA->cursorPosition();
    const KTextEditor::Cursor clickCursor(1, 2);
    const QPoint viewPoint = viewB->cursorToCoordinate(clickCursor);
    QWidget *target = deliveryTarget(viewB, viewPoint);
    check(QStringLiteral("setup.inner-widget"),
          target != nullptr && target != viewB && viewB->isAncestorOf(target),
          widgetName(target));
    if (target == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, ++g_failures);
        return 1;
    }
    const QPoint local = target == viewB ? viewPoint : target->mapFrom(viewB, viewPoint);
    const QPoint roundTrip = target == viewB ? local : target->mapTo(viewB, local);
    std::printf("MATH viewPoint=%d,%d targetPoint=%d,%d roundTrip=%d,%d delta=%d,%d (%s)\n",
                viewPoint.x(), viewPoint.y(), local.x(), local.y(), roundTrip.x(),
                roundTrip.y(), viewPoint.x() - local.x(), viewPoint.y() - local.y(),
                qPrintable(widgetName(target)));
    check(QStringLiteral("r1.inner-and-view-space-differ"), local != viewPoint);
    check(QStringLiteral("r1.mapping-round-trips"), roundTrip == viewPoint,
          QStringLiteral("roundTrip=%1,%2 view=%3,%4")
              .arg(roundTrip.x())
              .arg(roundTrip.y())
              .arg(viewPoint.x())
              .arg(viewPoint.y()));

    FilterRecorder recorder;
    recorder.area = area;
    target->installEventFilter(&recorder);
    QTest::mousePress(target, Qt::LeftButton, Qt::ControlModifier, local);
    QTest::mouseRelease(target, Qt::LeftButton, Qt::ControlModifier, local);
    QApplication::processEvents();
    QTest::qWait(50);

    check(QStringLiteral("r3.recorder-saw-press"), recorder.presses == 1,
          QString::number(recorder.presses));
    check(QStringLiteral("r3.pane-activated-before-object-filter"),
          recorder.paneAtPress == paneB,
          QStringLiteral("activeAtPress=%1").arg(paneId(recorder.paneAtPress)));
    check(QStringLiteral("r3.press-receiver-is-inner-widget"),
          recorder.pressReceiver == target,
          widgetName(qobject_cast<QWidget *>(recorder.pressReceiver)));
    check(QStringLiteral("r3.action-triggered"), handler.triggered == 1,
          QString::number(handler.triggered));
    check(QStringLiteral("r3.handler-reads-clicked-pane"), handler.pane == paneB,
          paneId(handler.pane));
    check(QStringLiteral("r3.handler-reads-clicked-file"), handler.file == pathB, handler.file);
    check(QStringLiteral("r3.handler-reads-clicked-caret"),
          handler.caretValid && handler.caret == clickCursor,
          QStringLiteral("%1:%2").arg(handler.caret.line()).arg(handler.caret.column()));
    check(QStringLiteral("r1.clicked-caret-is-the-clicked-position"),
          viewB->cursorPosition() == clickCursor,
          QStringLiteral("%1:%2")
              .arg(viewB->cursorPosition().line())
              .arg(viewB->cursorPosition().column()));
    check(QStringLiteral("r3.other-pane-caret-untouched"),
          viewA->cursorPosition() == aCaretBefore,
          QStringLiteral("%1:%2").arg(viewA->cursorPosition().line())
              .arg(viewA->cursorPosition().column()));

    // ---- R2: a successful Ctrl+click collapses an existing 3-cursor set.
    check(QStringLiteral("r2.setup-three-cursors"),
          vedit::editor::setViewCursors(
              viewB, {KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                     KTextEditor::Cursor(2, 0)})
              .ok);
    const int triggeredBeforeCollapse = handler.triggered;
    const KTextEditor::Cursor collapseClick(1, 2);
    const QPoint collapseViewPoint = viewB->cursorToCoordinate(collapseClick);
    QWidget *collapseTarget = deliveryTarget(viewB, collapseViewPoint);
    const QPoint collapseLocal =
        collapseTarget == viewB ? collapseViewPoint
                                : collapseTarget->mapFrom(viewB, collapseViewPoint);
    QTest::mousePress(collapseTarget, Qt::LeftButton, Qt::ControlModifier, collapseLocal);
    QTest::mouseRelease(collapseTarget, Qt::LeftButton, Qt::ControlModifier, collapseLocal);
    QApplication::processEvents();
    check(QStringLiteral("r2.ctrl-click-collapses-to-one"),
          vedit::editor::viewCursors(viewB).size() == 1,
          QStringLiteral("count=%1").arg(vedit::editor::viewCursors(viewB).size()));
    check(QStringLiteral("r2.ctrl-click-caret"),
          viewB->cursorPosition() == collapseClick,
          QStringLiteral("%1:%2").arg(viewB->cursorPosition().line())
              .arg(viewB->cursorPosition().column()));
    check(QStringLiteral("r2.ctrl-click-dispatched"),
          handler.triggered == triggeredBeforeCollapse + 1,
          QStringLiteral("%1 -> %2").arg(triggeredBeforeCollapse).arg(handler.triggered));

    // ---- R2 (refusal) + R4: gate off => plain click, ONE reason; re-arm.
    const QString reason1 = QStringLiteral("probe: definition backend went away");
    publishDefinitionGate(&host, definitionAction, false, reason1);
    QApplication::processEvents();
    check(QStringLiteral("r2.refusal.setup-three-cursors"),
          vedit::editor::setViewCursors(
              viewB, {KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                     KTextEditor::Cursor(2, 0)})
              .ok);
    const int triggeredBeforeRefusal = handler.triggered;
    host.statusBar()->clearMessage();
    const KTextEditor::Cursor refusedClick(1, 4);
    const QPoint refusedViewPoint = viewB->cursorToCoordinate(refusedClick);
    QWidget *refusedTarget = deliveryTarget(viewB, refusedViewPoint);
    const QPoint refusedLocal =
        refusedTarget == viewB ? refusedViewPoint
                               : refusedTarget->mapFrom(viewB, refusedViewPoint);
    const auto refusedPress = [&refusedTarget, &refusedLocal]() {
        QTest::mousePress(refusedTarget, Qt::LeftButton, Qt::ControlModifier, refusedLocal);
        QTest::mouseRelease(refusedTarget, Qt::LeftButton, Qt::ControlModifier, refusedLocal);
        QApplication::processEvents();
    };
    refusedPress();
    check(QStringLiteral("r2.refusal.no-dispatch"),
          handler.triggered == triggeredBeforeRefusal,
          QStringLiteral("%1 -> %2").arg(triggeredBeforeRefusal).arg(handler.triggered));
    check(QStringLiteral("r2.refusal.plain-click-collapses"),
          vedit::editor::viewCursors(viewB).size() == 1
              && viewB->cursorPosition() == refusedClick,
          QStringLiteral("count=%1 caret=%2:%3")
              .arg(vedit::editor::viewCursors(viewB).size())
              .arg(viewB->cursorPosition().line())
              .arg(viewB->cursorPosition().column()));
    check(QStringLiteral("r4.first-reason-shown"),
          host.statusBar()->currentMessage().contains(reason1),
          host.statusBar()->currentMessage());
    host.statusBar()->clearMessage();
    // Same reason again inside the episode: silent (one-time latch).
    refusedPress();
    check(QStringLiteral("r4.same-reason-one-time"),
          host.statusBar()->currentMessage().isEmpty(),
          host.statusBar()->currentMessage());

    // Availability returns without a successful Ctrl+click, then the same reason
    // comes back: the latch must have re-armed, so the reason is shown again.
    publishDefinitionGate(&host, definitionAction, true, QString());
    QApplication::processEvents();
    check(QStringLiteral("r4.restored"),
          vedit::editor::LspActionChannel::instanceFor(viewB) != nullptr
              && vedit::editor::LspActionChannel::instanceFor(viewB)
                     ->availability(vedit::editor::EditorAction::Definition)
                     .enabled);
    publishDefinitionGate(&host, definitionAction, false, reason1);
    QApplication::processEvents();
    host.statusBar()->clearMessage();
    refusedPress();
    check(QStringLiteral("r4.same-reason-shown-after-rearm"),
          host.statusBar()->currentMessage().contains(reason1),
          host.statusBar()->currentMessage());

    // A CHANGED reason is new information even while the latch is set.
    const QString reason2 = QStringLiteral("probe: a different definition reason");
    publishDefinitionGate(&host, definitionAction, false, reason2);
    QApplication::processEvents();
    host.statusBar()->clearMessage();
    refusedPress();
    check(QStringLiteral("r4.changed-reason-shown"),
          host.statusBar()->currentMessage().contains(reason2),
          host.statusBar()->currentMessage());
    host.statusBar()->clearMessage();
    refusedPress();
    check(QStringLiteral("r4.changed-reason-one-time"),
          host.statusBar()->currentMessage().isEmpty(),
          host.statusBar()->currentMessage());

    // The click landed in B and never left A active.
    check(QStringLiteral("r3.clicked-pane-still-active"),
          area->activePane() == paneB, paneId(area->activePane()));
    check(QStringLiteral("r3.dispatches-from-clicked-channel"),
          vedit::editor::LspActionChannel::instanceFor(viewB) != nullptr
              && vedit::editor::LspActionChannel::instanceFor(viewB)
                     ->dispatchCount(vedit::editor::EditorAction::Definition)
                     >= 2,
          QStringLiteral("dispatches=%1")
              .arg(vedit::editor::LspActionChannel::instanceFor(viewB) != nullptr
                       ? vedit::editor::LspActionChannel::instanceFor(viewB)
                             ->dispatchCount(vedit::editor::EditorAction::Definition)
                       : -1));

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
