// Offscreen test (UX batch 2): clicking into a split pane's EDITOR must make
// that pane the ACTIVE pane of the EditorArea.
//
// Why the click target matters: the product editor is KTextEditor, and the real
// mouse target inside a KTextEditor::View is KatePart's inner widget
// (`KateViewInternal`), a CHILD of the public View (measured in
// evidence/ux-batch-2/review-b4/00-coordinate-space.txt). A filter installed only
// on the View therefore never sees the click; this test drives the real delivery
// path (QTest mouse events sent to the deepest child under the click point, the
// way Qt dispatches a user click) against a real EditorArea with TWO panes and
// the real edit-core provider.
//
// Covered:
//   * a press inside the NON-active pane's inner editor widget activates it;
//   * the previously active pane reports inactive and its badge hides;
//   * the symmetric control (press inside the already-active pane) is a no-op
//     (no second activation);
//   * clicking a tab still activates its pane;
//   * a focus-in inside the non-active pane's inner widget activates it;
//   * a single-pane layout is unchanged by the press.
//
// Exit 77 means the KTextEditor plugin is unavailable (ctest SKIP_RETURN_CODE),
// never 0.

#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QLabel>
#include <QPoint>
#include <QRect>
#include <QTabBar>
#include <QTest>
#include <QWidget>

#include <cstdio>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const QString &name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("CHECK %s PASS\n", qPrintable(name));
        return;
    }
    ++g_failures;
    std::printf("CHECK %s FAIL%s\n", qPrintable(name),
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" (%1)").arg(detail)));
}

// The pane's "active" badge; its visibility is the visible half of the active
// state the shell paints.
QLabel *activeBadge(vedit::app::EditorPane *pane)
{
    return pane != nullptr ? pane->findChild<QLabel *>(QStringLiteral("editor-pane-active-badge"))
                           : nullptr;
}

QString widgetName(const QWidget *widget)
{
    return widget != nullptr ? QString::fromLatin1(widget->metaObject()->className())
                            : QStringLiteral("none");
}

// The widget Qt hands a click at `viewPoint` to: the deepest child under the
// point. For KatePart that is KateViewInternal, NOT the public View.
QWidget *deliveryTarget(KTextEditor::View *view, const QPoint &viewPoint)
{
    if (view == nullptr) {
        return nullptr;
    }
    QWidget *target = view->childAt(viewPoint);
    return target != nullptr ? target : view;
}

QPoint pointInTarget(QWidget *target, KTextEditor::View *view, const QPoint &viewPoint)
{
    if (target == nullptr || target == view) {
        return viewPoint;
    }
    return target->mapFrom(view, viewPoint);
}

// A real press + release at the position of `cursor`, delivered to the widget a
// user click would land on (the inner widget), not to the KTextEditor::View.
void clickInEditor(KTextEditor::View *view, const KTextEditor::Cursor &cursor)
{
    if (view == nullptr) {
        return;
    }
    const QPoint viewPoint = view->cursorToCoordinate(cursor);
    QWidget *target = deliveryTarget(view, viewPoint);
    if (target == nullptr) {
        return;
    }
    const QPoint local = pointInTarget(target, view, viewPoint);
    QTest::mousePress(target, Qt::LeftButton, Qt::NoModifier, local);
    QTest::mouseRelease(target, Qt::LeftButton, Qt::NoModifier, local);
    QApplication::processEvents();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    if (KTextEditor::Editor::instance() == nullptr) {
        std::printf("SKIP: the KTextEditor plugin is unavailable\n");
        return 77;
    }

    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    vedit::app::EditorArea area;
    area.setProvider(provider.get());
    area.resize(1200, 700);
    area.show();
    QTest::qWait(100);
    QApplication::processEvents();

    vedit::app::EditorPane *paneA = area.ensurePane();
    check(QStringLiteral("setup.one-pane"),
          paneA != nullptr && area.paneCount() == 1 && area.activePane() == paneA,
          QStringLiteral("panes=%1").arg(area.paneCount()));

    int switches = 0;
    vedit::app::EditorPane *switchedTo = nullptr;
    area.setOnActivePaneChanged([&switches, &switchedTo](vedit::app::EditorPane *pane) {
        ++switches;
        switchedTo = pane;
    });

    KTextEditor::View *viewA = vedit::editor::viewFor(paneA->editorWidget());
    check(QStringLiteral("setup.editor-a"), viewA != nullptr, widgetName(paneA->editorWidget()));
    if (viewA == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, ++g_failures);
        return 1;
    }
    viewA->document()->setText(QStringLiteral("alpha one\nalpha two\n"));
    QApplication::processEvents();
    QTest::qWait(50);

    // --- single-pane layout: a press inside the editor changes nothing --------
    switches = 0;
    switchedTo = nullptr;
    clickInEditor(viewA, KTextEditor::Cursor(0, 2));
    check(QStringLiteral("single-pane.press-keeps-active"),
          area.activePane() == paneA && paneA->isActive(), widgetName(area.activePane()));
    check(QStringLiteral("single-pane.no-activation-callback"), switches == 0,
          QString::number(switches));

    // --- two panes ------------------------------------------------------------
    vedit::app::EditorPane *paneB = area.splitActive(Qt::Horizontal);
    QApplication::processEvents();
    QTest::qWait(100);
    check(QStringLiteral("setup.two-panes"),
          paneB != nullptr && paneB != paneA && area.paneCount() == 2
              && area.activePane() == paneB,
          QStringLiteral("panes=%1").arg(area.paneCount()));

    area.setActivePane(paneA);
    QApplication::processEvents();
    check(QStringLiteral("setup.a-active"),
          area.activePane() == paneA && paneA->isActive() && !paneB->isActive(),
          widgetName(area.activePane()));

    KTextEditor::View *viewB = vedit::editor::viewFor(paneB->editorWidget());
    check(QStringLiteral("setup.editor-b"), viewB != nullptr, widgetName(paneB->editorWidget()));
    if (viewB == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, ++g_failures);
        return 1;
    }
    viewB->document()->setText(QStringLiteral("beta one\nbeta two\n"));
    QApplication::processEvents();
    QTest::qWait(50);

    const QPoint viewPointB = viewB->cursorToCoordinate(KTextEditor::Cursor(1, 2));
    QWidget *targetB = deliveryTarget(viewB, viewPointB);
    check(QStringLiteral("press.target-is-inner-widget"),
          targetB != nullptr && targetB != viewB && viewB->isAncestorOf(targetB),
          QStringLiteral("%1 at view point %2,%3")
              .arg(widgetName(targetB))
              .arg(viewPointB.x())
              .arg(viewPointB.y()));

    // --- THE BUG: press inside the NON-active pane's inner editor widget ------
    switches = 0;
    switchedTo = nullptr;
    const QPoint localB = pointInTarget(targetB, viewB, viewPointB);
    QTest::mousePress(targetB, Qt::LeftButton, Qt::NoModifier, localB);
    QTest::mouseRelease(targetB, Qt::LeftButton, Qt::NoModifier, localB);
    QApplication::processEvents();

    check(QStringLiteral("press.non-active-pane-becomes-active"),
          area.activePane() == paneB,
          QStringLiteral("active=%1 (expected %2)")
              .arg(area.activePane() != nullptr ? area.activePane()->paneId() : QStringLiteral("none"),
                   paneB->paneId()));
    check(QStringLiteral("press.new-pane-reports-active"), paneB->isActive());
    check(QStringLiteral("press.previous-pane-reports-inactive"), !paneA->isActive());
    check(QStringLiteral("press.exactly-one-activation"), switches == 1 && switchedTo == paneB,
          QStringLiteral("switches=%1 to=%2")
              .arg(switches)
              .arg(switchedTo != nullptr ? switchedTo->paneId() : QStringLiteral("none")));
    check(QStringLiteral("press.active-editor-is-clicked-pane"),
          area.activePane() != nullptr
              && area.activePane()->editorWidget() == paneB->editorWidget());
    check(QStringLiteral("press.focus-is-in-clicked-pane"),
          QApplication::focusWidget() != nullptr
              && paneB->editorWidget()->isAncestorOf(QApplication::focusWidget()),
          widgetName(QApplication::focusWidget()));
    check(QStringLiteral("press.badges-follow-the-active-pane"),
          activeBadge(paneB) != nullptr && activeBadge(paneB)->isVisible()
              && activeBadge(paneA) != nullptr && !activeBadge(paneA)->isVisible());

    // --- symmetric control: press inside the ALREADY-active pane --------------
    // Must not activate twice, must not disturb the active pane, must not crash.
    switches = 0;
    switchedTo = nullptr;
    clickInEditor(viewB, KTextEditor::Cursor(0, 1));
    check(QStringLiteral("control.already-active-pane-stays-active"),
          area.activePane() == paneB && paneB->isActive());
    check(QStringLiteral("control.no-double-activation"), switches == 0,
          QString::number(switches));
    check(QStringLiteral("control.previous-pane-stays-inactive"), !paneA->isActive());

    // --- the tab path still activates its pane (looked at explicitly) ---------
    switches = 0;
    switchedTo = nullptr;
    QTabBar *tabsA = paneA->tabBar();
    check(QStringLiteral("tab.tab-bar-present"), tabsA != nullptr && tabsA->count() > 0);
    if (tabsA != nullptr && tabsA->count() > 0) {
        const QRect tab = tabsA->tabRect(0);
        QTest::mouseClick(tabsA, Qt::LeftButton, Qt::NoModifier, tab.center());
        QApplication::processEvents();
        check(QStringLiteral("tab.click-activates-its-pane"),
              area.activePane() == paneA && paneA->isActive() && !paneB->isActive(),
              QStringLiteral("active=%1")
                  .arg(area.activePane() != nullptr ? area.activePane()->paneId()
                                                    : QStringLiteral("none")));
        check(QStringLiteral("tab.exactly-one-activation"), switches == 1,
              QString::number(switches));
    }

    // --- focus-in inside the non-active pane's inner widget activates it ------
    // (the second half of the contract: focus-in, not only a press)
    clickInEditor(viewB, KTextEditor::Cursor(0, 1));
    check(QStringLiteral("focus-in.precondition-b-active"), area.activePane() == paneB);
    switches = 0;
    switchedTo = nullptr;
    QWidget *targetA = deliveryTarget(viewA, viewA->cursorToCoordinate(KTextEditor::Cursor(0, 1)));
    check(QStringLiteral("focus-in.target-is-inner-widget"),
          targetA != nullptr && targetA != viewA && viewA->isAncestorOf(targetA),
          widgetName(targetA));
    if (targetA != nullptr) {
        targetA->setFocus(Qt::OtherFocusReason);
        QApplication::processEvents();
        check(QStringLiteral("focus-in.activates-its-pane"),
              area.activePane() == paneA && paneA->isActive() && !paneB->isActive(),
              QStringLiteral("active=%1")
                  .arg(area.activePane() != nullptr ? area.activePane()->paneId()
                                                    : QStringLiteral("none")));
        check(QStringLiteral("focus-in.exactly-one-activation"), switches == 1,
              QString::number(switches));
    }

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
