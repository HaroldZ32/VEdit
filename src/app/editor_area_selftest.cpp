// T7 (batch 20261110d): the editor TAB STRIP and the two new Edit commands.
//
// Real interactions, no mocks of the pane:
//   * every tab-menu action is TRIGGERED on the menu the pane actually builds
//     (`createTabContextMenu`) and its effect is checked on the tab list;
//   * a REAL QTabBar move is performed and the documents_/tab/editor alignment
//     is asserted (the defect a bare setMovable(true) causes);
//   * a DIRTY document proves the close prompt seam (`onTabCloseRequested_`) is
//     consulted: Cancel keeps the tab, Discard closes it;
//   * a split with a provider that cannot share documents (the demo
//     placeholder) keeps the honest empty pane;
//   * with the REAL KTextEditor provider a split shows the SAME document in
//     both panes: one Document/two Views (the negative control against two
//     independent buffers), typing in one view appears in the other, and
//     closing one pane keeps the other pane's modified document;
//   * the two shell commands exist with their shortcuts and the Edit menu keeps
//     Preferences LAST.
//
//   vedit-editor-area-selftest
#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"

#if defined(VEDIT_EDIT_AREA_HAVE_EDIT_CORE)
#include "ktexteditor_provider.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QTest>
#endif

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QTabBar>
#include <QTemporaryDir>
#include <QWheelEvent>

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

QString writeFile(const QString &directory, const QString &name, const QString &text)
{
    const QString path = directory + QLatin1Char('/') + name;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(text.toUtf8());
    }
    file.close();
    return path;
}

QAction *menuAction(QMenu *menu, const QString &objectName)
{
    if (menu == nullptr) {
        return nullptr;
    }
    for (QAction *action : menu->actions()) {
        if (action->objectName() == objectName) {
            return action;
        }
    }
    return nullptr;
}

// Closes every open document (the pane keeps its mandatory fresh empty tab),
// then opens `files` in order — so the next check always starts at [a, b, c].
void resetPane(vedit::app::EditorPane *pane, const QStringList &files)
{
    while (pane->tabCount() > 1 || !pane->currentFilePath().isEmpty()) {
        if (!pane->closeTab(0)) {
            break;
        }
    }
    for (const QString &file : files) {
        pane->openFile(file);
    }
    QApplication::processEvents();
}

QString activeEditorText(vedit::app::EditorPane *pane)
{
    if (auto *editor = qobject_cast<QPlainTextEdit *>(pane->editorWidget())) {
        return editor->toPlainText();
    }
    return QString();
}

#if defined(VEDIT_EDIT_AREA_HAVE_EDIT_CORE)
// The real keystroke target of a KTextEditor view: KatePart handles editing in
// a nested widget (KateViewInternal), so a synthetic key must be delivered
// there — the same hit point a real click/focus would take.
QWidget *innerWidget(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    const QPoint point = view->cursorToCoordinate(view->cursorPosition());
    QWidget *target = view->childAt(point);
    return target != nullptr ? target : view;
}
#endif

void checkScrollingTabs(const QString &directory)
{
    vedit::app::DemoEditorProvider provider;
    vedit::app::EditorArea area;
    area.setProvider(&provider);
    area.resize(540, 380);
    area.show();
    auto *pane = area.ensurePane();
    auto *tabs = pane->tabBar();
    auto *strip = pane->findChild<QScrollArea *>(QStringLiteral("editor-tab-scroll-area"));
    check(QStringLiteral("scroll.strip"), strip != nullptr);
    if (strip == nullptr) {
        return;
    }
    auto *scrollbar = strip->horizontalScrollBar();
    const auto visible = [&]() {
        const QRect tab = tabs->tabRect(pane->activeTabIndex());
        return strip->viewport()->rect().contains(QRect(tabs->mapTo(strip->viewport(), tab.topLeft()),
                                                        tab.size()));
    };

    const QString name = QStringLiteral("long_%1.txt").arg(QString(90, QLatin1Char('x')));
    const QString first = writeFile(directory, name, QStringLiteral("first\n"));
    bool opened = pane->openFile(first);
    QApplication::processEvents();
    const int singleWidth = tabs->tabRect(0).width();
    check(QStringLiteral("scroll.fits-hidden"), !scrollbar->isVisible() && visible());
    check(QStringLiteral("scroll.long-full-tooltip"), tabs->tabToolTip(0).endsWith(name));

    for (int index = 1; index < 12; ++index) {
        const QString path = writeFile(directory, QStringLiteral("scroll-%1.txt").arg(index),
                                       QStringLiteral("next\n"));
        opened = pane->openFile(path) && opened;
    }
    QApplication::processEvents();
    bool sameWidths = singleWidth > 0;
    for (int index = 0; index < tabs->count(); ++index) {
        sameWidths = sameWidths && tabs->tabRect(index).width() == singleWidth;
    }
    check(QStringLiteral("scroll.fixed-width"), opened && tabs->count() == 12 && sameWidths,
          QStringLiteral("single=%1 first=%2 last=%3")
              .arg(singleWidth).arg(tabs->tabRect(0).width()).arg(tabs->tabRect(11).width()));
    check(QStringLiteral("scroll.overflow"), scrollbar->isVisible() && scrollbar->maximum() > 0,
          QStringLiteral("maximum=%1").arg(scrollbar->maximum()));
    check(QStringLiteral("scroll.open-reveals"), pane->activeTabIndex() == 11 && visible());
    pane->setActiveTab(0);
    QApplication::processEvents();
    check(QStringLiteral("scroll.select-reveals"), visible() && scrollbar->value() == 0);

    const auto wheel = [&](const QPoint &pixelDelta, const QPoint &angleDelta) {
        const QPoint position = tabs->mapFrom(strip->viewport(), QPoint(30, 8));
        QWheelEvent event(QPointF(position), QPointF(tabs->mapToGlobal(position)), pixelDelta,
                          angleDelta, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QApplication::sendEvent(tabs, &event);
        QApplication::processEvents();
    };
    wheel({}, QPoint(0, -120));
    const int afterWheel = scrollbar->value();
    check(QStringLiteral("scroll.wheel-keeps-file"), afterWheel > 0 && pane->currentFilePath() == first);
    wheel(QPoint(-35, 0), {});
    check(QStringLiteral("scroll.trackpad-keeps-file"),
          scrollbar->value() > afterWheel && pane->currentFilePath() == first);

    scrollbar->setValue(0);
    QStyleOptionSlider option;
    option.initFrom(scrollbar);
    option.orientation = Qt::Horizontal;
    option.minimum = scrollbar->minimum();
    option.maximum = scrollbar->maximum();
    option.sliderPosition = scrollbar->sliderPosition();
    option.sliderValue = scrollbar->value();
    option.singleStep = scrollbar->singleStep();
    option.pageStep = scrollbar->pageStep();
    const QPoint thumb = scrollbar->style()
                             ->subControlRect(QStyle::CC_ScrollBar, &option,
                                              QStyle::SC_ScrollBarSlider, scrollbar)
                             .center();
    const QPoint target = thumb + QPoint(scrollbar->width() / 3, 0);
    const auto mouse = [scrollbar](QEvent::Type type, QPoint position, Qt::MouseButton button,
                                  Qt::MouseButtons buttons) {
        QMouseEvent event(type, QPointF(position), QPointF(scrollbar->mapToGlobal(position)),
                          button, buttons, Qt::NoModifier);
        QApplication::sendEvent(scrollbar, &event);
    };
    mouse(QEvent::MouseButtonPress, thumb, Qt::LeftButton, Qt::LeftButton);
    const bool grabbed = scrollbar->isSliderDown();
    mouse(QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
    check(QStringLiteral("scroll.thumb-drag-keeps-file"),
          grabbed && scrollbar->value() > 0 && pane->currentFilePath() == first,
          QStringLiteral("value=%1").arg(scrollbar->value()));

    check(QStringLiteral("scroll.reopen-reveals"), pane->openFile(first) && visible());
    pane->setActiveTab(11);
    QApplication::processEvents();
    QWidget *overlay = pane->findChild<QWidget *>(QStringLiteral("editor-drop-overlay"));
    const QPoint drop = strip->viewport()->rect().center();
    const int dropIndex = tabs->tabAt(tabs->mapFrom(strip->viewport(), drop));
    const quint64 serial = pane->tabSerial(11);
    if (overlay != nullptr && dropIndex >= 0) {
        const QPoint dropPosition = strip->viewport()->mapTo(pane, drop);
        QMimeData mime;
        mime.setData(QString::fromLatin1(vedit::app::kEditorTabMimeType), QByteArrayLiteral("drag"));
        area.beginTabDrag(pane, serial);
        QDragEnterEvent enter(dropPosition, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(overlay, &enter);
        QDropEvent event(QPointF(dropPosition), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(overlay, &event);
        area.endTabDrag();
    }
    check(QStringLiteral("scroll.drop-maps-visible-tab"),
          dropIndex > 0 && dropIndex < 11 && pane->tabIndexForSerial(serial) == dropIndex,
          QStringLiteral("target=%1 actual=%2").arg(dropIndex).arg(pane->tabIndexForSerial(serial)));
    while (pane->tabCount() > 1) {
        if (!pane->closeTab(pane->tabCount() - 1)) {
            break;
        }
    }
    QApplication::processEvents();
    check(QStringLiteral("scroll.scrollbar-hides-again"),
          pane->tabCount() == 1 && !scrollbar->isVisible() && visible());
}

void checkAuxiliaryTabs(const QString &directory, const QString &ordinaryFile)
{
    using namespace vedit::app;
    ShellOptions options;
    options.stateDir = directory + QStringLiteral("/aux-state");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    ApplicationShell shell(options);
    DemoEditorProvider provider;
    auto *area = shell.editorArea();
    area->setProvider(&provider);
    shell.resize(900, 570);
    shell.show();
    auto *left = area->ensurePane();
    const QString id = QStringLiteral("aux:compare");
    const QString title = QStringLiteral("Compare a and b");
    check(QStringLiteral("aux.open-ordinary"), left->openFile(ordinaryFile));

    bool dirty = false;
    bool veto = false;
    bool canSave = true;
    int saves = 0;
    int undos = 0;
    AuxiliaryTabCallbacks callbacks;
    callbacks.isModified = [&dirty]() { return dirty; };
    callbacks.save = [&](QString *) {
        if (!canSave) {
            return false;
        }
        dirty = false;
        ++saves;
        return true;
    };
    callbacks.canClose = [&veto]() { return !veto; };
    callbacks.actionEnabled = [](const QStringList &names) {
        return names.contains(QStringLiteral("edit_undo"));
    };
    callbacks.triggerAction = [&](const QStringList &names, const char *) {
        if (!names.contains(QStringLiteral("edit_undo"))) {
            return false;
        }
        ++undos;
        return true;
    };
    const QPointer<QPlainTextEdit> page(new QPlainTextEdit);
    check(QStringLiteral("aux.hosted-in-tab"),
          area->openAuxiliaryTab(id, title, page, std::move(callbacks)) == page &&
              left->isAuxiliaryTab(left->activeTabIndex()) && left->tabCount() == 2 &&
              left->editorWidget() == nullptr && left->currentFilePath().isEmpty());
    const QPointer<QWidget> spare(new QWidget);
    check(QStringLiteral("aux.same-id-activates"),
          area->openAuxiliaryTab(id, title, spare) == page && spare.isNull() && left->tabCount() == 2);
    dirty = true;
    area->refreshAuxiliaryTab(id);
    const int index = left->activeTabIndex();
    check(QStringLiteral("aux.dirty-label"),
          left->isActiveTabModified() && left->tabText(index).contains(QStringLiteral("●")) &&
              left->tabDisplayName(index) == title && left->tabBar()->tabToolTip(index) == title);
    int active = 0;
    const auto savedDocuments = left->documentStates(&active);
    check(QStringLiteral("aux.session-ordinary-only"),
          savedDocuments.size() == 1 && savedDocuments.first().path == ordinaryFile && active == -1);

    int saveAsCalls = 0;
    shell.setSaveAsDialogProvider([&](const QString &) {
        ++saveAsCalls;
        return QString();
    });
    check(QStringLiteral("aux.save-current"),
          shell.saveActiveDocument() && saves == 1 && saveAsCalls == 0 && !left->isActiveTabModified());
    check(QStringLiteral("aux.save-as-refused-before-dialog"),
          !shell.saveActiveDocumentAs() && saveAsCalls == 0);
    QAction *undo = shell.commands()->action(QStringLiteral("edit.undo"));
    if (undo != nullptr) {
        undo->trigger();
    }
    check(QStringLiteral("aux.shell-undo"), undo != nullptr && undo->isEnabled() && undos == 1);

    auto *right = area->splitActive(Qt::Horizontal);
    check(QStringLiteral("aux.split-preserves-source"),
          right != nullptr && right->tabCount() == 1 && !right->isAuxiliaryTab(0) &&
              area->findAuxiliaryTab(id) == page);
    if (right == nullptr) {
        return;
    }
    const quint64 serial = left->tabSerial(index);
    check(QStringLiteral("aux.move-keeps-identity"),
          area->dropTab(left, serial, right, TabDropZone::Center) &&
              right->tabIndexForSerial(serial) == 0 && area->findAuxiliaryTab(id) == page &&
              left->tabCount() == 1 && right->tabCount() == 1);
    check(QStringLiteral("aux.activate-moved-page"),
          area->activateAuxiliaryTab(id) && area->activePane() == right && right->isAuxiliaryTab(0));

    dirty = true;
    area->refreshAuxiliaryTab(id);
    QAction *saveAll = shell.commands()->action(QStringLiteral("file.save-all"));
    if (saveAll != nullptr) {
        saveAll->trigger();
    }
    check(QStringLiteral("aux.save-all"), saves == 2 && !dirty);
    dirty = true;
    veto = true;
    check(QStringLiteral("aux.veto-guards-tab-pane-window"),
          !right->closeTab(0) && !area->closePane(right) && !shell.confirmUnsavedDocuments());
    veto = false;
    QStringList prompted;
    shell.setUnsavedPromptProvider([&](const QStringList &names, const QStringList &) {
        prompted = names;
        return ApplicationShell::CloseChoice::Save;
    });
    canSave = false;
    check(QStringLiteral("aux.failed-save-blocks-window"),
          !shell.confirmUnsavedDocuments() && dirty && area->findAuxiliaryTab(id) == page);
    canSave = true;
    check(QStringLiteral("aux.window-save-uses-title"),
          shell.confirmUnsavedDocuments() && prompted == QStringList{title} && saves == 3 && !dirty);
    dirty = true;
    int closePrompts = 0;
    right->setOnTabCloseRequested([&](EditorPane *, int) {
        return ++closePrompts == 1 ? SaveDecision::Cancel : SaveDecision::Save;
    });
    check(QStringLiteral("aux.close-cancel-keeps-page"), !right->closeTab(0) && !page.isNull());
    check(QStringLiteral("aux.close-save-releases-page"),
          right->closeTab(0) && closePrompts == 2 && saves == 4 && page.isNull() &&
              area->findAuxiliaryTab(id) == nullptr && right->tabCount() == 1);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-editor-area-selftest"));

    QTemporaryDir temp;
    check(QStringLiteral("fixture.temp"), temp.isValid());
    if (!temp.isValid()) {
        return 1;
    }
    const QString a = writeFile(temp.path(), QStringLiteral("a.txt"), QStringLiteral("alpha content\n"));
    const QString b = writeFile(temp.path(), QStringLiteral("b.txt"), QStringLiteral("bravo content\n"));
    const QString c = writeFile(temp.path(), QStringLiteral("c.txt"), QStringLiteral("charlie content\n"));

    checkScrollingTabs(temp.path());
    checkAuxiliaryTabs(temp.path(), a);

    vedit::app::DemoEditorProvider provider;
    vedit::app::EditorArea area;
    area.setProvider(&provider);
    area.resize(1000, 700);
    area.show();
    QApplication::processEvents();
    vedit::app::EditorPane *pane = area.ensurePane();
    check(QStringLiteral("pane.created"), pane != nullptr);
    if (pane == nullptr) {
        return 1;
    }
    resetPane(pane, {a, b, c});
    check(QStringLiteral("tabs.opened"), pane->tabCount() == 3,
          QString::number(pane->tabCount()));
    check(QStringLiteral("tabs.paths"),
          pane->tabFilePath(0) == a && pane->tabFilePath(1) == b && pane->tabFilePath(2) == c);

    // --- DRAG: a real QTabBar move must permute documents_ -----------------
    pane->tabBar()->moveTab(0, 2);
    QApplication::processEvents();
    check(QStringLiteral("drag.order"),
          pane->tabFilePath(0) == b && pane->tabFilePath(1) == c && pane->tabFilePath(2) == a,
          QStringLiteral("[0]=%1 [1]=%2 [2]=%3")
              .arg(pane->tabFilePath(0), pane->tabFilePath(1), pane->tabFilePath(2)));
    bool aligned = true;
    const QStringList expected{b, c, a};
    for (int index = 0; index < pane->tabCount(); ++index) {
        pane->setActiveTab(index);
        QApplication::processEvents();
        const QString shown = activeEditorText(pane);
        const bool sameFile = pane->currentFilePath() == expected.at(index) &&
                              QFileInfo(expected.at(index)).fileName().left(1) == shown.left(1);
        if (!sameFile) {
            aligned = false;
            std::printf("CHECK-DETAIL drag misaligned index=%d path=%s shown='%s'\n", index,
                        qPrintable(pane->currentFilePath()), qPrintable(shown.left(12)));
        }
    }
    check(QStringLiteral("drag.tab-editor-aligned"), aligned);
    check(QStringLiteral("drag.index-for-path"),
          pane->tabIndexForPath(a) == 2 && pane->tabIndexForPath(b) == 0);

    // --- RIGHT-CLICK menu: every entry is really executed -------------------
    resetPane(pane, {a, b, c});
    QMenu *menu = pane->createTabContextMenu(1);
    check(QStringLiteral("menu.created"), menu != nullptr);
    const QStringList expectedItems{QStringLiteral("tab-menu.close"),
                                    QStringLiteral("tab-menu.close-others"),
                                    QStringLiteral("tab-menu.close-left"),
                                    QStringLiteral("tab-menu.close-right"),
                                    QStringLiteral("tab-menu.close-all"),
                                    QStringLiteral("tab-menu.copy-path")};
    bool everyItem = menu != nullptr;
    for (const QString &name : expectedItems) {
        if (menuAction(menu, name) == nullptr) {
            everyItem = false;
            std::printf("CHECK-DETAIL menu missing %s\n", qPrintable(name));
        }
    }
    check(QStringLiteral("menu.items"), everyItem);
    if (menu != nullptr) {
        QGuiApplication::clipboard()->clear();
        menuAction(menu, QStringLiteral("tab-menu.copy-path"))->trigger();
        check(QStringLiteral("menu.copy-path"), QGuiApplication::clipboard()->text() == b,
              QGuiApplication::clipboard()->text());
        menu->deleteLater();
    }

    // Close (the tab the menu was opened on).
    resetPane(pane, {a, b, c});
    menu = pane->createTabContextMenu(1);
    menuAction(menu, QStringLiteral("tab-menu.close"))->trigger();
    menu->deleteLater();
    check(QStringLiteral("menu.close"),
          pane->tabCount() == 2 && pane->tabIndexForPath(b) < 0,
          QStringLiteral("count=%1").arg(pane->tabCount()));

    // Close Others.
    resetPane(pane, {a, b, c});
    menu = pane->createTabContextMenu(1);
    menuAction(menu, QStringLiteral("tab-menu.close-others"))->trigger();
    menu->deleteLater();
    check(QStringLiteral("menu.close-others"),
          pane->tabCount() == 1 && pane->tabFilePath(0) == b,
          QStringLiteral("count=%1").arg(pane->tabCount()));

    // Close to the Left.
    resetPane(pane, {a, b, c});
    menu = pane->createTabContextMenu(2);
    menuAction(menu, QStringLiteral("tab-menu.close-left"))->trigger();
    menu->deleteLater();
    check(QStringLiteral("menu.close-left"),
          pane->tabCount() == 1 && pane->tabFilePath(0) == c,
          QStringLiteral("count=%1 path=%2").arg(pane->tabCount()).arg(pane->tabFilePath(0)));

    // Close to the Right.
    resetPane(pane, {a, b, c});
    menu = pane->createTabContextMenu(0);
    menuAction(menu, QStringLiteral("tab-menu.close-right"))->trigger();
    menu->deleteLater();
    check(QStringLiteral("menu.close-right"),
          pane->tabCount() == 1 && pane->tabFilePath(0) == a,
          QStringLiteral("count=%1 path=%2").arg(pane->tabCount()).arg(pane->tabFilePath(0)));

    // Close All leaves the pane's mandatory fresh tab.
    resetPane(pane, {a, b, c});
    menu = pane->createTabContextMenu(0);
    menuAction(menu, QStringLiteral("tab-menu.close-all"))->trigger();
    menu->deleteLater();
    check(QStringLiteral("menu.close-all"),
          pane->tabCount() == 1 && pane->currentFilePath().isEmpty(),
          QStringLiteral("count=%1 path=%2").arg(pane->tabCount()).arg(pane->currentFilePath()));

    // --- DIRTY document: the close prompt seam is consulted -----------------
    resetPane(pane, {a, b, c});
    pane->setActiveTab(1);
    if (auto *editor = qobject_cast<QPlainTextEdit *>(pane->editorWidget())) {
        editor->document()->setModified(true);
    }
    QApplication::processEvents();
    check(QStringLiteral("dirty.active-tab"), pane->isActiveTabModified());
    int prompts = 0;
    pane->setOnTabCloseRequested([&prompts](vedit::app::EditorPane *, int) {
        ++prompts;
        return vedit::app::SaveDecision::Cancel;
    });
    menu = pane->createTabContextMenu(1);
    menuAction(menu, QStringLiteral("tab-menu.close"))->trigger();
    menu->deleteLater();
    check(QStringLiteral("dirty.cancel-keeps-tab"), prompts == 1 && pane->tabCount() == 3,
          QStringLiteral("prompts=%1 count=%2").arg(prompts).arg(pane->tabCount()));
    pane->setOnTabCloseRequested([&prompts](vedit::app::EditorPane *, int) {
        ++prompts;
        return vedit::app::SaveDecision::Discard;
    });
    menu = pane->createTabContextMenu(1);
    menuAction(menu, QStringLiteral("tab-menu.close"))->trigger();
    menu->deleteLater();
    check(QStringLiteral("dirty.discard-closes"), prompts == 2 && pane->tabCount() == 2,
          QStringLiteral("prompts=%1 count=%2").arg(prompts).arg(pane->tabCount()));

    // --- SPLIT with a provider that CANNOT share: honest EMPTY pane --------
    // The demo placeholder has no document object to hand to a second view, so
    // it must keep the empty pane rather than pretend to mirror the document.
    const int panesBefore = area.paneCount();
    vedit::app::EditorPane *split = area.splitActive(Qt::Horizontal);
    check(QStringLiteral("split.new-pane"), split != nullptr && area.paneCount() == panesBefore + 1,
          QStringLiteral("panes=%1").arg(area.paneCount()));
    check(QStringLiteral("split.empty-pane"),
          split != nullptr && split->tabCount() == 1 && split->currentFilePath().isEmpty(),
          split != nullptr ? split->currentFilePath() : QStringLiteral("<none>"));

#if defined(VEDIT_EDIT_AREA_HAVE_EDIT_CORE)
    // --- SPLIT VIEW: the SAME document in two panes (real KTextEditor) ------
    // The product editor exposes a second View over an existing Document. The
    // negative control is the identity check: a naive "open the same path
    // twice" implementation produces two Documents, which this must fail.
    {
        auto editCore = vedit::editor::makeEditCoreProvider();
        vedit::app::EditorArea sharedArea;
        sharedArea.setProvider(editCore.get());
        sharedArea.resize(1000, 700);
        sharedArea.show();
        QApplication::processEvents();
        vedit::app::EditorPane *left = sharedArea.ensurePane();
        check(QStringLiteral("shared.pane"), left != nullptr);
        const QString sharedPath = writeFile(temp.path(), QStringLiteral("split.txt"),
                                             QStringLiteral("first line\nsecond line\n"));
        QString openError;
        const bool opened = left != nullptr && left->openFile(sharedPath, &openError);
        check(QStringLiteral("shared.open"), opened, openError);
        QApplication::processEvents();
        QWidget *leftWidget = left != nullptr ? left->editorWidget() : nullptr;
        KTextEditor::View *leftView = vedit::editor::viewFor(leftWidget);
        KTextEditor::Document *leftDocument = vedit::editor::documentFor(leftWidget);
        check(QStringLiteral("shared.left-view"), leftView != nullptr && leftDocument != nullptr);

        const int panesBeforeShared = sharedArea.paneCount();
        vedit::app::EditorPane *right = sharedArea.splitActive(Qt::Horizontal);
        QApplication::processEvents();
        check(QStringLiteral("shared.split"),
              right != nullptr && sharedArea.paneCount() == panesBeforeShared + 1,
              QStringLiteral("panes=%1").arg(sharedArea.paneCount()));
        QWidget *rightWidget = right != nullptr ? right->editorWidget() : nullptr;
        KTextEditor::View *rightView = vedit::editor::viewFor(rightWidget);
        KTextEditor::Document *rightDocument = vedit::editor::documentFor(rightWidget);
        // NEGATIVE CONTROL for "two independent buffers": same Document object.
        check(QStringLiteral("shared.one-document"),
              leftDocument != nullptr && leftDocument == rightDocument,
              QStringLiteral("left=%1 right=%2")
                  .arg(reinterpret_cast<quintptr>(leftDocument))
                  .arg(reinterpret_cast<quintptr>(rightDocument)));
        check(QStringLiteral("shared.path"),
              right != nullptr && right->currentFilePath() == sharedPath,
              right != nullptr ? right->currentFilePath() : QStringLiteral("<none>"));
        check(QStringLiteral("shared.no-empty-tab"), right != nullptr && right->tabCount() == 1,
              QStringLiteral("tabs=%1").arg(right != nullptr ? right->tabCount() : -1));

        // Type into the LEFT view through a real key event; the RIGHT view must
        // show it because both Views render the same Document.
        const QString beforeTyping = leftDocument != nullptr ? leftDocument->text() : QString();
        if (leftView != nullptr && rightView != nullptr) {
            QTest::keyClicks(innerWidget(leftView), QStringLiteral("Q"));
        }
        QApplication::processEvents();
        const QString afterTyping = leftDocument != nullptr ? leftDocument->text() : QString();
        const QString rightSees = rightDocument != nullptr ? rightDocument->text() : QString();
        check(QStringLiteral("shared.typing-shared"),
              afterTyping != beforeTyping && !rightSees.isEmpty() && rightSees == afterTyping,
              QStringLiteral("left='%1' right='%2'").arg(afterTyping, rightSees));
        check(QStringLiteral("shared.modified-both"),
              editCore->isModified(leftWidget) && editCore->isModified(rightWidget));

        // Closing one pane must NOT discard the buffer: the document stays
        // modified and reachable through the surviving pane.
        const QString editedText = afterTyping;
        check(QStringLiteral("shared.close-right"), sharedArea.closePane(right));
        QApplication::processEvents();
        QWidget *survivingWidget = left != nullptr ? left->editorWidget() : nullptr;
        KTextEditor::Document *survivingDocument = vedit::editor::documentFor(survivingWidget);
        check(QStringLiteral("shared.document-survives"),
              sharedArea.paneCount() == panesBeforeShared && survivingDocument == leftDocument,
              QStringLiteral("panes=%1 same=%2")
                  .arg(sharedArea.paneCount())
                  .arg(survivingDocument == leftDocument ? 1 : 0));
        check(QStringLiteral("shared.modified-survives"),
              survivingDocument != nullptr && survivingDocument->isModified() &&
                  survivingDocument->text() == editedText,
              QStringLiteral("modified=%1 text='%2'")
                  .arg(survivingDocument != nullptr && survivingDocument->isModified() ? 1 : 0)
                  .arg(survivingDocument != nullptr ? survivingDocument->text() : QString()));
    }
#endif

    // --- the two Edit commands are registered and checkable -----------------
    {
        vedit::app::ShellOptions options;
        options.stateDir = temp.path() + QStringLiteral("/shell");
        options.restoreSession = false;
        options.showDefaultPanels = false;
        vedit::app::ApplicationShell shell(options);
        QAction *block = shell.commands()->action(QStringLiteral("edit.block-selection"));
        check(QStringLiteral("commands.block-selection"),
              block != nullptr && block->isCheckable() &&
                  block->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+B")),
              block != nullptr ? block->shortcut().toString(QKeySequence::PortableText)
                               : QStringLiteral("<none>"));
        QAction *lineEnds =
            shell.commands()->action(QStringLiteral("edit.multi-cursor-from-selection"));
        check(QStringLiteral("commands.multi-cursor-from-selection"),
              lineEnds != nullptr &&
                  lineEnds->shortcut() == QKeySequence(QStringLiteral("Alt+Shift+I")),
              lineEnds != nullptr ? lineEnds->shortcut().toString(QKeySequence::PortableText)
                                  : QStringLiteral("<none>"));
        QAction *preferences =
            shell.commands()->action(QStringLiteral("settings.preferences"));
        QMenu *editMenu = nullptr;
        for (QMenu *candidate : shell.menuBar()->findChildren<QMenu *>()) {
            if (candidate->title().remove(QLatin1Char('&')) == QStringLiteral("Edit")) {
                editMenu = candidate;
                break;
            }
        }
        check(QStringLiteral("commands.preferences-still-last"),
              editMenu != nullptr && preferences != nullptr &&
                  editMenu->actions().last() == preferences);
    }

    std::printf("EDITOR-AREA-SELFTEST ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
