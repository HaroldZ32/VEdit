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
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QTemporaryDir>

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
