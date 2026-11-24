// batch-20261115 (lane C, T5): the tab drag / drop acceptance.
//
// The contract (AC-6.1..6.7) asks for: same-bar reorder, a cross-pane move that
// keeps the buffer, an edge drop that splits in that direction, drop-zone
// highlighting, persistence, and a dirty buffer that survives all of it.
//
// Two kinds of evidence live here:
//   * the drop LOGIC called directly (tabDropZoneAt() and EditorArea::dropTab()),
//     which is the seam AC-6.6 names;
//   * an OFFSCREEN interaction case that delivers real QDragEnterEvent /
//     QDragMoveEvent / QDropEvent objects to the pane's own drop overlay, so the
//     highlight and the drop path are exercised through the shipped handlers.
//
// A real OS-level drag gesture (QDrag::exec) needs a windowing system and is not
// reproducible offscreen; that boundary is stated in the evidence, not hidden.
//
//   vedit-editor-tab-drag-tests
#include "editor_area.h"
#include "editor_provider.h"

#if defined(VEDIT_TAB_DRAG_HAVE_EDIT_CORE)
#include "ktexteditor_provider.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFrame>
#include <QJsonObject>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPoint>
#include <QRect>
#include <QTabBar>
#include <QTemporaryDir>
#include <QWidget>

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const QString &name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", qPrintable(name), ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", qPrintable(detail));
    std::fflush(stdout);
}

void pump()
{
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
    }
}

QString writeFile(const QString &directory, const QString &name, const QString &text)
{
    const QString path = directory + QLatin1Char('/') + name;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(text.toUtf8());
    }
    return path;
}

// Makes one tab of a pane dirty, through the pane's provider seam. The demo
// provider is a QPlainTextEdit; the real one is handled separately below.
bool makeDemoTabDirty(vedit::app::EditorPane *pane, int index, const QString &text)
{
    auto *editor = qobject_cast<QPlainTextEdit *>(pane != nullptr ? pane->tabEditorWidget(index)
                                                                  : nullptr);
    if (editor == nullptr) {
        return false;
    }
    editor->setPlainText(text);
    editor->document()->setModified(true);
    QCoreApplication::processEvents();
    return pane->isTabModified(index);
}

vedit::app::EditorPane *paneHolding(const vedit::app::EditorArea &area, const QString &path)
{
    for (vedit::app::EditorPane *pane : area.panes()) {
        if (pane != nullptr && pane->tabIndexForPath(path) >= 0) {
            return pane;
        }
    }
    return nullptr;
}

// --- AC-6.6: the pure zone rule, directly callable -------------------------
void checkZoneMapping()
{
    const QRect pane(0, 0, 1000, 700);
    const QRect tabBar(0, 0, 1000, 30);
    check(QStringLiteral("zone.tab-bar"),
          vedit::app::tabDropZoneAt(pane, tabBar, QPoint(500, 15))
              == vedit::app::TabDropZone::TabBar);
    check(QStringLiteral("zone.left"),
          vedit::app::tabDropZoneAt(pane, tabBar, QPoint(40, 300))
              == vedit::app::TabDropZone::Left);
    check(QStringLiteral("zone.right"),
          vedit::app::tabDropZoneAt(pane, tabBar, QPoint(960, 300))
              == vedit::app::TabDropZone::Right);
    check(QStringLiteral("zone.top"),
          vedit::app::tabDropZoneAt(pane, tabBar, QPoint(500, 60))
              == vedit::app::TabDropZone::Top);
    check(QStringLiteral("zone.bottom"),
          vedit::app::tabDropZoneAt(pane, tabBar, QPoint(500, 680))
              == vedit::app::TabDropZone::Bottom);
    check(QStringLiteral("zone.center"),
          vedit::app::tabDropZoneAt(pane, tabBar, QPoint(500, 365))
              == vedit::app::TabDropZone::Center);
    check(QStringLiteral("zone.outside"),
          vedit::app::tabDropZoneAt(pane, tabBar, QPoint(1200, 300))
              == vedit::app::TabDropZone::None);
    check(QStringLiteral("zone.names"),
          QString::fromLatin1(vedit::app::tabDropZoneName(vedit::app::TabDropZone::Left))
              == QStringLiteral("left"));
}

// --- AC-6.1: dragging a tab inside its own strip still reorders ------------
void checkSameBarReorder(const QString &directory)
{
    const QString a = writeFile(directory, QStringLiteral("reorder-a.sv"), QStringLiteral("a\n"));
    const QString b = writeFile(directory, QStringLiteral("reorder-b.sv"), QStringLiteral("b\n"));
    const QString c = writeFile(directory, QStringLiteral("reorder-c.sv"), QStringLiteral("c\n"));
    vedit::app::DemoEditorProvider provider;
    vedit::app::EditorArea area;
    area.setProvider(&provider);
    area.resize(1100, 700);
    area.show();
    pump();
    vedit::app::EditorPane *pane = area.ensurePane();
    check(QStringLiteral("reorder.pane"), pane != nullptr);
    if (pane == nullptr) {
        return;
    }
    pane->openFile(a);
    pane->openFile(b);
    pane->openFile(c);
    pump();
    check(QStringLiteral("reorder.opened"), pane->tabCount() == 3,
          QString::number(pane->tabCount()));
    const quint64 serialA = pane->tabSerial(0);
    check(QStringLiteral("reorder.serial"), serialA != 0 && pane->tabIndexForSerial(serialA) == 0);
    check(QStringLiteral("reorder.drop"),
          area.dropTab(pane, serialA, pane, vedit::app::TabDropZone::TabBar, 2));
    pump();
    check(QStringLiteral("reorder.order"),
          pane->tabFilePath(0) == b && pane->tabFilePath(1) == c && pane->tabFilePath(2) == a,
          QStringLiteral("[0]=%1 [1]=%2 [2]=%3")
              .arg(pane->tabFilePath(0), pane->tabFilePath(1), pane->tabFilePath(2)));
    check(QStringLiteral("reorder.serial-stable"), pane->tabIndexForSerial(serialA) == 2);
    check(QStringLiteral("reorder.pane-count"), area.paneCount() == 1,
          QString::number(area.paneCount()));
    // "拖回原处取消不产生变化": dropping the tab back on its own index is a no-op.
    check(QStringLiteral("reorder.drop-back"),
          area.dropTab(pane, serialA, pane, vedit::app::TabDropZone::TabBar, 2));
    check(QStringLiteral("reorder.drop-back-no-change"),
          pane->tabFilePath(0) == b && pane->tabFilePath(1) == c && pane->tabFilePath(2) == a);
}

// --- AC-6.3 + dirty buffer: a tab moved to another pane keeps its editor ----
void checkCrossPaneMove(const QString &directory)
{
    const QString a = writeFile(directory, QStringLiteral("move-a.sv"), QStringLiteral("a\n"));
    const QString b = writeFile(directory, QStringLiteral("move-b.sv"), QStringLiteral("b\n"));
    const QString other = writeFile(directory, QStringLiteral("move-other.sv"), QStringLiteral("o\n"));
    vedit::app::DemoEditorProvider provider;
    vedit::app::EditorArea area;
    area.setProvider(&provider);
    area.resize(1200, 760);
    area.show();
    pump();
    vedit::app::EditorPane *paneA = area.ensurePane();
    vedit::app::EditorPane *paneB = area.splitActive(Qt::Horizontal);
    check(QStringLiteral("move.panes"), paneA != nullptr && paneB != nullptr && area.paneCount() == 2);
    if (paneA == nullptr || paneB == nullptr) {
        return;
    }
    paneA->openFile(a);
    paneA->openFile(other);
    paneB->openFile(b);
    pump();
    const QString dirtyText = QStringLiteral("module moved; // unsaved\n");
    check(QStringLiteral("move.dirty-setup"), makeDemoTabDirty(paneA, 0, dirtyText));
    QWidget *editorA = paneA->tabEditorWidget(0);
    const quint64 serial = paneA->tabSerial(0);
    check(QStringLiteral("move.drop"),
          area.dropTab(paneA, serial, paneB, vedit::app::TabDropZone::TabBar, 1));
    pump();
    check(QStringLiteral("move.source-count"), paneA->tabCount() == 1,
          QString::number(paneA->tabCount()));
    check(QStringLiteral("move.source-gone"), paneA->tabIndexForPath(a) < 0);
    check(QStringLiteral("move.target-count"), paneB->tabCount() == 2,
          QString::number(paneB->tabCount()));
    check(QStringLiteral("move.target-order"),
          paneB->tabFilePath(0) == b && paneB->tabFilePath(1) == a,
          QStringLiteral("[0]=%1 [1]=%2").arg(paneB->tabFilePath(0), paneB->tabFilePath(1)));
    check(QStringLiteral("move.same-editor"), paneB->tabEditorWidget(1) == editorA);
    check(QStringLiteral("move.dirty-preserved"), paneB->isTabModified(1));
    auto *moved = qobject_cast<QPlainTextEdit *>(paneB->tabEditorWidget(1));
    check(QStringLiteral("move.text-preserved"),
          moved != nullptr && moved->toPlainText() == dirtyText,
          moved != nullptr ? moved->toPlainText() : QStringLiteral("<no editor>"));
    check(QStringLiteral("move.active"), area.activePane() == paneB && paneB->activeTabIndex() == 1,
          QString::number(paneB->activeTabIndex()));
    check(QStringLiteral("move.pane-count"), area.paneCount() == 2);
}

// --- AC-6.2: an edge drop splits the target and moves the same buffer ------
void checkEdgeSplit(const QString &directory)
{
    const QString a = writeFile(directory, QStringLiteral("split-a.sv"), QStringLiteral("a\n"));
    const QString b = writeFile(directory, QStringLiteral("split-b.sv"), QStringLiteral("b\n"));
    vedit::app::DemoEditorProvider provider;
    vedit::app::EditorArea area;
    area.setProvider(&provider);
    area.resize(1200, 760);
    area.show();
    pump();
    vedit::app::EditorPane *paneA = area.ensurePane();
    vedit::app::EditorPane *paneB = area.splitActive(Qt::Horizontal);
    check(QStringLiteral("split.panes"), paneA != nullptr && paneB != nullptr && area.paneCount() == 2);
    if (paneA == nullptr || paneB == nullptr) {
        return;
    }
    paneA->openFile(a);
    paneB->openFile(b);
    pump();
    const QString dirtyText = QStringLiteral("module split; // unsaved\n");
    check(QStringLiteral("split.dirty-setup"), makeDemoTabDirty(paneA, 0, dirtyText));
    QWidget *editorA = paneA->tabEditorWidget(0);
    const quint64 serial = paneA->tabSerial(0);
    check(QStringLiteral("split.drop"),
          area.dropTab(paneA, serial, paneB, vedit::app::TabDropZone::Left));
    pump();
    check(QStringLiteral("split.pane-count"), area.paneCount() == 3,
          QString::number(area.paneCount()));
    const QList<vedit::app::EditorPane *> panes = area.panes();
    // Left of paneB = new pane FIRST in the target's split, i.e. tree order
    // [paneA, created, paneB].
    check(QStringLiteral("split.order"),
          panes.size() == 3 && panes.at(0) == paneA && panes.at(1) != paneA
              && panes.at(1) != paneB && panes.at(2) == paneB);
    if (panes.size() != 3) {
        return;
    }
    vedit::app::EditorPane *created = panes.at(1);
    check(QStringLiteral("split.holds-doc"), created->tabIndexForPath(a) == 0);
    check(QStringLiteral("split.source-empty"),
          paneA->tabCount() == 1 && paneA->currentFilePath().isEmpty(),
          QStringLiteral("count=%1 path=%2")
              .arg(paneA->tabCount())
              .arg(paneA->currentFilePath()));
    check(QStringLiteral("split.same-editor"), created->tabEditorWidget(0) == editorA);
    check(QStringLiteral("split.dirty-preserved"), created->isTabModified(0));
    auto *moved = qobject_cast<QPlainTextEdit *>(created->tabEditorWidget(0));
    check(QStringLiteral("split.text-preserved"),
          moved != nullptr && moved->toPlainText() == dirtyText,
          moved != nullptr ? moved->toPlainText() : QStringLiteral("<no editor>"));
    check(QStringLiteral("split.active"), area.activePane() == created);
}

// --- AC-6.2 "empty pane keeps the usual fresh tab": the only tab splits away
void checkSelfEdgeSplit(const QString &directory)
{
    const QString a = writeFile(directory, QStringLiteral("self-a.sv"), QStringLiteral("a\n"));
    vedit::app::DemoEditorProvider provider;
    vedit::app::EditorArea area;
    area.setProvider(&provider);
    area.resize(1100, 700);
    area.show();
    pump();
    vedit::app::EditorPane *pane = area.ensurePane();
    check(QStringLiteral("self.pane"), pane != nullptr);
    if (pane == nullptr) {
        return;
    }
    pane->openFile(a);
    pump();
    const quint64 serial = pane->tabSerial(0);
    check(QStringLiteral("self.drop"),
          area.dropTab(pane, serial, pane, vedit::app::TabDropZone::Right));
    pump();
    check(QStringLiteral("self.pane-count"), area.paneCount() == 2,
          QString::number(area.paneCount()));
    const QList<vedit::app::EditorPane *> panes = area.panes();
    check(QStringLiteral("self.source-empty"),
          panes.size() == 2 && panes.at(0) == pane && pane->tabCount() == 1
              && pane->currentFilePath().isEmpty());
    check(QStringLiteral("self.new-holds-doc"),
          panes.size() == 2 && panes.at(1) != pane && panes.at(1)->tabIndexForPath(a) == 0);
}

// --- AC-6.6: the real drop handlers, driven by synthetic drag events -------
void checkDropInteraction(const QString &directory)
{
    const QString a = writeFile(directory, QStringLiteral("drag-a.sv"), QStringLiteral("a\n"));
    const QString b = writeFile(directory, QStringLiteral("drag-b.sv"), QStringLiteral("b\n"));
    vedit::app::DemoEditorProvider provider;
    vedit::app::EditorArea area;
    area.setProvider(&provider);
    area.resize(1200, 760);
    area.show();
    pump();
    vedit::app::EditorPane *paneA = area.ensurePane();
    vedit::app::EditorPane *paneB = area.splitActive(Qt::Horizontal);
    check(QStringLiteral("drag.panes"), paneA != nullptr && paneB != nullptr && area.paneCount() == 2);
    if (paneA == nullptr || paneB == nullptr) {
        return;
    }
    paneA->openFile(a);
    paneB->openFile(b);
    pump();
    const QString dirtyText = QStringLiteral("module drag; // unsaved\n");
    makeDemoTabDirty(paneA, 0, dirtyText);
    const quint64 serial = paneA->tabSerial(0);
    QWidget *overlay = paneB->findChild<QWidget *>(QStringLiteral("editor-drop-overlay"));
    auto *highlight = paneB->findChild<QFrame *>(QStringLiteral("editor-drop-highlight"));
    check(QStringLiteral("drag.overlay"), overlay != nullptr);
    check(QStringLiteral("drag.highlight"), highlight != nullptr);
    if (overlay == nullptr || highlight == nullptr) {
        return;
    }

    // Esc / no drop: the drag lifecycle alone must change nothing.
    area.beginTabDrag(paneA, serial);
    check(QStringLiteral("drag.active"), area.isTabDragActive());
    check(QStringLiteral("drag.overlays-shown"),
          paneA->dropOverlayVisible() && paneB->dropOverlayVisible());
    area.endTabDrag();
    pump();
    check(QStringLiteral("drag.cancel-no-change"),
          !area.isTabDragActive() && area.paneCount() == 2 && paneA->tabCount() == 1
              && paneB->tabCount() == 1 && paneA->tabIndexForPath(a) == 0);

    area.beginTabDrag(paneA, serial);
    pump();
    // A foreign drag (plain text) must be refused: the layout is not a text
    // target.
    QMimeData foreign;
    foreign.setText(QStringLiteral("hello"));
    QDragEnterEvent foreignEnter(QPoint(overlay->width() / 8, overlay->height() / 2),
                                 Qt::CopyAction, &foreign, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(overlay, &foreignEnter);
    check(QStringLiteral("drag.foreign-refused"), !foreignEnter.isAccepted());
    check(QStringLiteral("drag.foreign-no-highlight"), !highlight->isVisible());

    QMimeData ours;
    ours.setData(QString::fromLatin1(vedit::app::kEditorTabMimeType), QByteArrayLiteral("drag"));
    QTabBar *tabBar = paneB->findChild<QTabBar *>(QStringLiteral("editor-tab-bar"));
    check(QStringLiteral("drag.tab-bar"), tabBar != nullptr);
    const QRect tabRect = tabBar != nullptr
                              ? QRect(tabBar->mapTo(paneB, QPoint(0, 0)), tabBar->size())
                              : QRect();
    const QPoint leftDrop(overlay->width() / 10, qMax(tabRect.bottom() + 20, overlay->height() / 2));
    QDragEnterEvent enter(leftDrop, Qt::MoveAction, &ours, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(overlay, &enter);
    check(QStringLiteral("drag.enter-accepted"), enter.isAccepted());
    QDragMoveEvent move(leftDrop, Qt::MoveAction, &ours, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(overlay, &move);
    pump();
    check(QStringLiteral("drag.highlight-visible"), highlight->isVisible());
    check(QStringLiteral("drag.highlight-zone"),
          highlight->property("dropZone").toString() == QStringLiteral("left"),
          highlight->property("dropZone").toString());
    const QRect highlighted = highlight->geometry();
    check(QStringLiteral("drag.highlight-geometry"),
          highlighted.left() == 0 && highlighted.width() == paneB->width() / 2
              && highlighted.top() == tabRect.bottom() + 1,
          QStringLiteral("x=%1 w=%2 top=%3 paneW=%4")
              .arg(highlighted.left())
              .arg(highlighted.width())
              .arg(highlighted.top())
              .arg(paneB->width()));

    QDropEvent drop(QPointF(leftDrop), Qt::MoveAction, &ours, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(overlay, &drop);
    pump();
    check(QStringLiteral("drag.drop-accepted"), drop.isAccepted());
    area.endTabDrag();
    pump();
    check(QStringLiteral("drag.drop-split"), area.paneCount() == 3,
          QString::number(area.paneCount()));
    vedit::app::EditorPane *created = paneHolding(area, a);
    check(QStringLiteral("drag.drop-created"), created != nullptr && created != paneA
                                                   && created != paneB);
    check(QStringLiteral("drag.drop-source-empty"),
          paneA->tabIndexForPath(a) < 0 && paneA->tabCount() == 1);
    if (created != nullptr) {
        check(QStringLiteral("drag.drop-dirty"), created->isTabModified(0));
        auto *moved = qobject_cast<QPlainTextEdit *>(created->tabEditorWidget(0));
        check(QStringLiteral("drag.drop-text"),
              moved != nullptr && moved->toPlainText() == dirtyText);
    }
    check(QStringLiteral("drag.overlays-hidden"),
          !paneA->dropOverlayVisible() && !paneB->dropOverlayVisible());
    check(QStringLiteral("drag.highlight-hidden"), !highlight->isVisible());

    // A drop on ANOTHER pane's tab strip moves the document (AC-6.3) without
    // growing the layout: fresh area so the counts above stay readable.
    vedit::app::DemoEditorProvider provider2;
    vedit::app::EditorArea area2;
    area2.setProvider(&provider2);
    area2.resize(1200, 760);
    area2.show();
    pump();
    vedit::app::EditorPane *left = area2.ensurePane();
    vedit::app::EditorPane *right = area2.splitActive(Qt::Horizontal);
    left->openFile(a);
    right->openFile(b);
    pump();
    const QString dirty2 = QStringLiteral("module tabbar; // unsaved\n");
    makeDemoTabDirty(left, 0, dirty2);
    const quint64 serial2 = left->tabSerial(0);
    QWidget *overlay2 = right->findChild<QWidget *>(QStringLiteral("editor-drop-overlay"));
    auto *highlight2 = right->findChild<QFrame *>(QStringLiteral("editor-drop-highlight"));
    QTabBar *tabBar2 = right->findChild<QTabBar *>(QStringLiteral("editor-tab-bar"));
    check(QStringLiteral("drag.tabbar-overlay"), overlay2 != nullptr);
    check(QStringLiteral("drag.tabbar-highlight-widget"), highlight2 != nullptr);
    check(QStringLiteral("drag.tabbar-bar"), tabBar2 != nullptr);
    if (overlay2 == nullptr || highlight2 == nullptr || tabBar2 == nullptr) {
        return;
    }
    QMimeData ours2;
    ours2.setData(QString::fromLatin1(vedit::app::kEditorTabMimeType), QByteArrayLiteral("drag"));
    area2.beginTabDrag(left, serial2);
    pump();
    const QRect tabRect2 = tabBar2 != nullptr
                               ? QRect(tabBar2->mapTo(right, QPoint(0, 0)), tabBar2->size())
                               : QRect();
    const QPoint barDrop(tabRect2.center());
    // A real drag always starts with an accepted DragEnter; the synthetic session
    // does the same, then moves onto the tab strip and drops.
    QDragEnterEvent barEnter(barDrop, Qt::MoveAction, &ours2, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(overlay2, &barEnter);
    check(QStringLiteral("drag.tabbar-enter"), barEnter.isAccepted());
    QDragMoveEvent barMove(barDrop, Qt::MoveAction, &ours2, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(overlay2, &barMove);
    pump();
    check(QStringLiteral("drag.tabbar-highlight"),
          highlight2 != nullptr && highlight2->isVisible()
              && highlight2->property("dropZone").toString() == QStringLiteral("tab-bar")
              && highlight2->geometry() == tabRect2,
          highlight2 != nullptr ? highlight2->property("dropZone").toString() : QString());
    QDropEvent barDropEvent(QPointF(barDrop), Qt::MoveAction, &ours2, Qt::LeftButton,
                            Qt::NoModifier);
    QCoreApplication::sendEvent(overlay2, &barDropEvent);
    area2.endTabDrag();
    pump();
    check(QStringLiteral("drag.tabbar-panes"), area2.paneCount() == 2,
          QString::number(area2.paneCount()));
    check(QStringLiteral("drag.tabbar-moved"),
          right->tabCount() == 2 && right->tabIndexForPath(a) >= 0,
          QString::number(right->tabIndexForPath(a)));
    check(QStringLiteral("drag.tabbar-source"), left->tabIndexForPath(a) < 0);
    check(QStringLiteral("drag.tabbar-dirty"),
          right->tabIndexForPath(a) >= 0 && right->isTabModified(right->tabIndexForPath(a)));
}

// --- AC-6.5: the layout after a split/move persists and restores -----------
void checkPersistence(const QString &directory)
{
    const QString a1 = writeFile(directory, QStringLiteral("persist-a1.sv"), QStringLiteral("a1\n"));
    const QString a2 = writeFile(directory, QStringLiteral("persist-a2.sv"), QStringLiteral("a2\n"));
    const QString b1 = writeFile(directory, QStringLiteral("persist-b1.sv"), QStringLiteral("b1\n"));
    QJsonObject layout;
    {
        vedit::app::DemoEditorProvider provider;
        vedit::app::EditorArea area;
        area.setProvider(&provider);
        area.resize(1200, 760);
        area.show();
        pump();
        vedit::app::EditorPane *paneA = area.ensurePane();
        vedit::app::EditorPane *paneB = area.splitActive(Qt::Horizontal);
        check(QStringLiteral("persist.panes"), paneA != nullptr && paneB != nullptr);
        if (paneA == nullptr || paneB == nullptr) {
            return;
        }
        paneA->openFile(a1);
        paneA->openFile(a2);
        paneB->openFile(b1);
        pump();
        check(QStringLiteral("persist.move"),
              area.dropTab(paneA, paneA->tabSerial(0), paneB,
                           vedit::app::TabDropZone::TabBar, 1));
        pump();
        layout = area.saveLayout();
    }
    check(QStringLiteral("persist.layout"), !layout.isEmpty());
    vedit::app::DemoEditorProvider provider2;
    vedit::app::EditorArea restored;
    restored.setProvider(&provider2);
    restored.resize(1200, 760);
    restored.show();
    pump();
    restored.ensurePane();
    QString error;
    check(QStringLiteral("persist.restore"), restored.restoreLayout(layout, &error), error);
    pump();
    const QList<vedit::app::EditorPane *> panes = restored.panes();
    check(QStringLiteral("persist.restored-panes"), panes.size() == 2,
          QString::number(panes.size()));
    if (panes.size() != 2) {
        return;
    }
    check(QStringLiteral("persist.first"),
          panes.at(0)->tabCount() == 1 && panes.at(0)->tabFilePath(0) == a2,
          QStringLiteral("count=%1 path=%2")
              .arg(panes.at(0)->tabCount())
              .arg(panes.at(0)->tabFilePath(0)));
    check(QStringLiteral("persist.second"),
          panes.at(1)->tabCount() == 2 && panes.at(1)->tabFilePath(0) == b1
              && panes.at(1)->tabFilePath(1) == a1,
          QStringLiteral("count=%1 [0]=%2 [1]=%3")
              .arg(panes.at(1)->tabCount())
              .arg(panes.at(1)->tabFilePath(0), panes.at(1)->tabFilePath(1)));
}

// --- AC-6.7: the existing split / close paths are untouched ----------------
void checkExistingPaths(const QString &directory)
{
    const QString a = writeFile(directory, QStringLiteral("existing-a.sv"), QStringLiteral("a\n"));
    const QString b = writeFile(directory, QStringLiteral("existing-b.sv"), QStringLiteral("b\n"));
    vedit::app::DemoEditorProvider provider;
    vedit::app::EditorArea area;
    area.setProvider(&provider);
    area.resize(1100, 700);
    area.show();
    pump();
    vedit::app::EditorPane *pane = area.ensurePane();
    pane->openFile(a);
    pane->openFile(b);
    pump();
    check(QStringLiteral("existing.close"),
          pane->closeTab(0) && pane->tabCount() == 1 && pane->currentFilePath() == b,
          QStringLiteral("count=%1 path=%2").arg(pane->tabCount()).arg(pane->currentFilePath()));
    // The command path still splits (and, with a provider that cannot share a
    // document, keeps the honest empty pane).
    vedit::app::EditorPane *split = area.splitActive(Qt::Horizontal);
    check(QStringLiteral("existing.split"), split != nullptr && area.paneCount() == 2);
    check(QStringLiteral("existing.split-empty-pane"),
          split != nullptr && split->tabCount() == 1 && split->currentFilePath().isEmpty());
    check(QStringLiteral("existing.no-drag"), !area.isTabDragActive());
}

#if defined(VEDIT_TAB_DRAG_HAVE_EDIT_CORE)
// The real KTextEditor provider: a DIRTY buffer survives a cross-pane move and an
// edge split (the widget, and therefore the document, travels untouched).
void checkRealProvider(const QString &directory)
{
    auto provider = vedit::editor::makeEditCoreProvider();
    check(QStringLiteral("real.provider"), provider != nullptr);
    if (provider == nullptr) {
        return;
    }
    const QString a = writeFile(directory, QStringLiteral("real-a.sv"),
                                QStringLiteral("module real_a;\nendmodule\n"));
    const QString b = writeFile(directory, QStringLiteral("real-b.sv"),
                                QStringLiteral("module real_b;\nendmodule\n"));
    vedit::app::EditorArea area;
    area.setProvider(provider.get());
    area.resize(1300, 820);
    area.show();
    pump();
    vedit::app::EditorPane *paneA = area.ensurePane();
    // Split while paneA is still empty: the real provider cannot share the demo
    // fallback editor, so paneB starts as the honest empty pane.
    vedit::app::EditorPane *paneB = area.splitActive(Qt::Horizontal);
    check(QStringLiteral("real.panes"), paneA != nullptr && paneB != nullptr);
    if (paneA == nullptr || paneB == nullptr) {
        return;
    }
    check(QStringLiteral("real.open-a"), paneA->openFile(a));
    check(QStringLiteral("real.open-b"), paneB->openFile(b));
    pump();
    KTextEditor::View *view = vedit::editor::viewFor(paneA->tabEditorWidget(0));
    check(QStringLiteral("real.view"), view != nullptr);
    if (view == nullptr) {
        return;
    }
    const QString dirtyText = QStringLiteral("// unsaved marker\n");
    view->document()->insertText(KTextEditor::Cursor(0, 0), dirtyText);
    view->document()->setModified(true);
    pump();
    check(QStringLiteral("real.dirty"), paneA->isTabModified(0));
    const quint64 serial = paneA->tabSerial(0);
    check(QStringLiteral("real.move"),
          area.dropTab(paneA, serial, paneB, vedit::app::TabDropZone::TabBar, 1));
    pump();
    check(QStringLiteral("real.moved-count"), paneB->tabIndexForPath(a) == 1,
          QString::number(paneB->tabIndexForPath(a)));
    check(QStringLiteral("real.moved-dirty"), paneB->isTabModified(1));
    KTextEditor::View *movedView = vedit::editor::viewFor(paneB->tabEditorWidget(1));
    check(QStringLiteral("real.moved-text"),
          movedView != nullptr && movedView->document()->text().contains(dirtyText),
          movedView != nullptr ? movedView->document()->text().left(40) : QString());
    check(QStringLiteral("real.source-empty"),
          paneA->tabCount() == 1 && paneA->currentFilePath().isEmpty());

    // Edge split with the same real document: it stays dirty and complete.
    const quint64 serial2 = paneB->tabSerial(1);
    check(QStringLiteral("real.split"),
          area.dropTab(paneB, serial2, paneB, vedit::app::TabDropZone::Bottom));
    pump();
    check(QStringLiteral("real.split-panes"), area.paneCount() == 3,
          QString::number(area.paneCount()));
    vedit::app::EditorPane *created = paneHolding(area, a);
    check(QStringLiteral("real.split-created"), created != nullptr && created != paneA
                                                   && created != paneB);
    if (created != nullptr) {
        check(QStringLiteral("real.split-dirty"), created->isTabModified(0));
        KTextEditor::View *splitView = vedit::editor::viewFor(created->tabEditorWidget(0));
        check(QStringLiteral("real.split-text"),
              splitView != nullptr && splitView->document()->text().contains(dirtyText));
    }
}
#endif

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-editor-tab-drag-tests"));

    QTemporaryDir files;
    check(QStringLiteral("fixture.temp-dir"), files.isValid());
    if (!files.isValid()) {
        return 1;
    }

    checkZoneMapping();
    checkSameBarReorder(files.path());
    checkCrossPaneMove(files.path());
    checkEdgeSplit(files.path());
    checkSelfEdgeSplit(files.path());
    checkDropInteraction(files.path());
    checkPersistence(files.path());
    checkExistingPaths(files.path());
#if defined(VEDIT_TAB_DRAG_HAVE_EDIT_CORE)
    checkRealProvider(files.path());
#else
    std::printf("TAB-DRAG SKIP real provider (edit-core lane not linked)\n");
#endif

    std::printf("TAB-DRAG-TESTS %s checks=%d failures=%d\n",
                g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
