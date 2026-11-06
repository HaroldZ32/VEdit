// UX batch 2, lane A2 selftest (menu order + main toolbar + i18n + built-in
// user manual).
//
// The ctest runs it with QT_QPA_PLATFORM=offscreen and no display: every
// assertion below is a real widget/action check, not a string search in the
// sources. A second ctest entry (`vedit-app-ux-screenshots`) runs the same
// binary under xvfb-run with `--screenshots <dir>`; there the checks are skipped
// and two real window grabs are written instead.
//
// What it proves:
//   * the top-level menu order is File, Edit, View, Project, Help and the old
//     Settings menu is gone; Preferences… is the LAST Edit entry;
//   * the main toolbar holds the SAME QAction objects the command registry
//     serves (so a button can never diverge from its menu entry), in the
//     requested order with the one separator, and Build/Format trigger the
//     diagnostics lane's real commands;
//   * Undo/Redo really change the active document: with the plain-text fallback
//     and, when the build has it, with the REAL KTextEditor provider (the
//     editor's own action is triggered, no fake "undo" button);
//   * after the zh_CN .qm is installed the five menu titles, Preferences… and a
//     panel toggle render in Chinese;
//   * Help -> User Manual (F1) opens the reader and renders the embedded
//     docs/USER_GUIDE.md (its shipped "VEdit 用户手册" H1 is rendered).
#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "help_viewer.h"
#include "i18n.h"
#include "panel_registry.h"

#if defined(VEDIT_UX_HAVE_EDIT_CORE)
#include "kernel/ktexteditor_provider.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QKeySequence>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#if defined(VEDIT_UX_HAVE_TERMINAL)
#include "terminal_panel.h"
#include "terminal_widget.h"
#include "vt_screen.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QTest>
#include <QThread>
#endif
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>

#include <cstdio>
#include <functional>

namespace {

int failures = 0;

void check(const QString &name, bool ok, const QString &detail = QString())
{
    std::printf("CHECK %s %s%s%s\n", qPrintable(name), ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : " ", qPrintable(detail));
    std::fflush(stdout);
    if (!ok) {
        ++failures;
    }
}

void skipCheck(const QString &name, const QString &reason)
{
    std::printf("SKIP-CHECK %s %s\n", qPrintable(name), qPrintable(reason));
    std::fflush(stdout);
}

QStringList menuTitles(const QMainWindow &window)
{
    QStringList titles;
    for (QAction *action : window.menuBar()->actions()) {
        titles.append(action->text());
    }
    return titles;
}

QString stripped(const QString &text)
{
    QString plain = text;
    plain.remove(QLatin1Char('&'));
    return plain;
}

QMenu *menuByStrippedTitle(const QMainWindow &window, const QString &needle)
{
    for (QAction *action : window.menuBar()->actions()) {
        QMenu *menu = action->menu();
        if (menu != nullptr && stripped(action->text()).contains(needle)) {
            return menu;
        }
    }
    return nullptr;
}

vedit::app::ShellOptions testOptions(const QString &directory)
{
    vedit::app::ShellOptions options;
    options.stateDir = directory + QStringLiteral("/state");
    options.restoreSession = false;   // never read/write the user's real session
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    return options;
}

// The diagnostics lane registers the Project menu and Build/Format. The shell
// must order/translate/materialise them even though they arrive after its own
// constructor; the test simulates the lane by adding the same command ids.
void addLaneCommands(vedit::app::ApplicationShell &shell)
{
    struct Spec {
        const char *id;
        const char *text;
        const char *menu;
    };
    const Spec specs[] = {
        {"project.lint", "&Lint Project", "project"},
        {"diagnostics.format", "&Format Document", "project"},
        {"project.reparse", "Re&parse Project", "project"},
    };
    int order = 50;
    for (const Spec &spec : specs) {
        vedit::app::CommandSpec command;
        command.id = QString::fromLatin1(spec.id);
        // Same catalogue context the diagnostics lane uses, so the Chinese
        // screenshot proves the Project menu entries are translated too.
        command.text = QCoreApplication::translate("VEdit", spec.text);
        command.menu = QString::fromLatin1(spec.menu);
        command.order = order++;
        shell.commands()->add(command);
    }
}

// The search lane's panel: a widget whose query field carries the stable
// objectName the shell focuses ("search-query").
void installSearchPanelFactory(vedit::app::ApplicationShell &shell)
{
    shell.panels()->setFactory(QStringLiteral("search"), []() {
        auto *panel = new QWidget;
        panel->setObjectName(QStringLiteral("test-search-panel"));
        auto *layout = new QVBoxLayout(panel);
        auto *query = new QLineEdit(panel);
        query->setObjectName(QStringLiteral("search-query"));
        layout->addWidget(query);
        return panel;
    });
}

struct ToolbarEntry {
    const char *id;
};

// The SHIPPED toolbar order, exactly as `ApplicationShell` builds it
// (app_shell.cpp `kToolBarOrder`). The comparison below is an ordered equality,
// deliberately: the order is the requirement, not a subset. RM-2 added
// `file.switch-project` next to Open Folder, so it must appear here too.
const QList<const char *> kToolbarOrder{
    "file.new",       "file.open",     "file.open-folder", "file.switch-project",
    "file.save",      "file.save-all", "edit.undo",        "edit.redo",
    nullptr, /* separator */ "search.focus", "project.lint", "diagnostics.format",
};

// The shipped H1 of docs/USER_GUIDE.md (RM-8 rewrote the manual). ONE constant
// feeds both the header-accessor assertion and the rendered-viewer assertion so
// the two cannot drift apart; it is the exact shipped title, not the old
// "用户指南" substring.
const QString kManualTitle = QStringLiteral("VEdit 用户手册");

void testStructure(const QString &directory)
{
    vedit::app::ApplicationShell shell(testOptions(directory));
    addLaneCommands(shell);
    installSearchPanelFactory(shell);

    // ③a: no editor view yet => Undo/Redo are greyed out (never a dead button).
    QAction *earlyUndo = shell.commands()->action(QStringLiteral("edit.undo"));
    check(QStringLiteral("a2.toolbar.undo-disabled-without-view"),
          earlyUndo != nullptr && !earlyUndo->isEnabled());

    shell.startSession();
    shell.show();
    QApplication::processEvents();

    // ① the fixed top-level order and the disappeared Settings menu.
    const QStringList titles = menuTitles(shell);
    const QString expectedOrder = QStringLiteral("&File,&Edit,&View,&Project,&Help");
    check(QStringLiteral("a2.menu.order"),
          stripped(titles.join(QLatin1Char(','))) == stripped(expectedOrder),
          titles.join(QLatin1Char(',')));
    check(QStringLiteral("a2.menu.no-settings"), menuByStrippedTitle(shell, QStringLiteral("Settings")) == nullptr);

    // ② Preferences… is the LAST entry of Edit.
    QMenu *editMenu = menuByStrippedTitle(shell, QStringLiteral("Edit"));
    QAction *preferences = shell.commands()->action(QStringLiteral("settings.preferences"));
    const bool inEdit = editMenu != nullptr && preferences != nullptr
                        && editMenu->actions().contains(preferences);
    check(QStringLiteral("a2.menu.preferences-in-edit"), inEdit);
    check(QStringLiteral("a2.menu.preferences-last-in-edit"),
          inEdit && editMenu->actions().last() == preferences);

    // ③ the toolbar: exists, movable, exact action order, real commands.
    QToolBar *toolbar = shell.mainToolBar();
    check(QStringLiteral("a2.toolbar.exists"), toolbar != nullptr);
    if (toolbar == nullptr) {
        return;
    }
    check(QStringLiteral("a2.toolbar.movable"), toolbar->isMovable());
    check(QStringLiteral("a2.toolbar.object-name"),
          toolbar->objectName() == QStringLiteral("vedit-main-toolbar"));

    QStringList actualOrder;
    QStringList sharedActionProblems;
    for (QAction *action : toolbar->actions()) {
        if (action->isSeparator()) {
            actualOrder.append(QStringLiteral("---"));
            continue;
        }
        actualOrder.append(action->objectName());
    }
    QStringList expectedOrderNames;
    for (const char *id : kToolbarOrder) {
        expectedOrderNames.append(id == nullptr ? QStringLiteral("---")
                                                : QStringLiteral("action-") + QString::fromLatin1(id));
    }
    check(QStringLiteral("a2.toolbar.action-order"), actualOrder == expectedOrderNames,
          actualOrder.join(QLatin1Char(',')));
    // Every non-separator toolbar action IS the registry action (same pointer).
    for (QAction *action : toolbar->actions()) {
        if (action->isSeparator()) {
            continue;
        }
        const QString id = action->objectName().mid(QStringLiteral("action-").size());
        if (shell.commands()->action(id) != action) {
            sharedActionProblems.append(id);
        }
    }
    check(QStringLiteral("a2.toolbar.shares-registry-actions"),
          sharedActionProblems.isEmpty(), sharedActionProblems.join(QLatin1Char(',')));
    // Hideable: the View menu command hides the toolbar and brings it back (the
    // "可隐藏" half of the requirement; "可移动" is asserted above).
    QAction *toolbarToggle = shell.commands()->action(QStringLiteral("view.toolbar"));
    check(QStringLiteral("a2.toolbar.toggle-command"), toolbarToggle != nullptr);
    if (toolbarToggle != nullptr) {
        toolbarToggle->trigger();
        QApplication::processEvents();
        const bool hidden = !toolbar->isVisible();
        toolbarToggle->trigger();
        QApplication::processEvents();
        check(QStringLiteral("a2.toolbar.hide-and-show"), hidden && toolbar->isVisible());
    }

    // Build/Format are the diagnostics lane's commands and really run them.
    int lintRuns = 0;
    int formatRuns = 0;
    shell.commands()->setHandler(QStringLiteral("project.lint"), [&lintRuns]() { ++lintRuns; });
    shell.commands()->setHandler(QStringLiteral("diagnostics.format"), [&formatRuns]() { ++formatRuns; });
    shell.commands()->action(QStringLiteral("project.lint"))->trigger();
    shell.commands()->action(QStringLiteral("diagnostics.format"))->trigger();
    check(QStringLiteral("a2.toolbar.build-runs-command"), lintRuns == 1);
    check(QStringLiteral("a2.toolbar.format-runs-command"), formatRuns == 1);

    // ③b New: a dirty tab gets a NEW document tab next to it.
    vedit::app::EditorPane *pane = shell.editorArea()->activePane();
    check(QStringLiteral("a2.toolbar.active-pane"), pane != nullptr);
    if (pane == nullptr) {
        return;
    }
    auto *textEditor = qobject_cast<QPlainTextEdit *>(pane->editorWidget());
    check(QStringLiteral("a2.toolbar.demo-editor"), textEditor != nullptr);
    if (textEditor == nullptr) {
        return;
    }
    const QString beforeTyping = textEditor->toPlainText();
    textEditor->insertPlainText(QStringLiteral("hello"));
    QApplication::processEvents();
    const QString afterTyping = textEditor->toPlainText();
    const int tabsBefore = pane->tabCount();
    shell.commands()->action(QStringLiteral("file.new"))->trigger();
    QApplication::processEvents();
    check(QStringLiteral("a2.toolbar.new-opens-document"), pane->tabCount() == tabsBefore + 1,
          QStringLiteral("tabs %1 -> %2").arg(tabsBefore).arg(pane->tabCount()));

    // ③c Undo/Redo really change the document (plain-text fallback path).
    pane->setActiveTab(0);
    QApplication::processEvents();
    QAction *undo = shell.commands()->action(QStringLiteral("edit.undo"));
    check(QStringLiteral("a2.toolbar.undo-enabled-with-view"), undo != nullptr && undo->isEnabled());
    undo->trigger();
    QApplication::processEvents();
    check(QStringLiteral("a2.toolbar.undo-changes-document"),
          textEditor->toPlainText() == beforeTyping, textEditor->toPlainText());
    QAction *redo = shell.commands()->action(QStringLiteral("edit.redo"));
    check(QStringLiteral("a2.toolbar.redo-enabled"), redo != nullptr && redo->isEnabled());
    redo->trigger();
    QApplication::processEvents();
    check(QStringLiteral("a2.toolbar.redo-restores-document"),
          textEditor->toPlainText() == afterTyping,
          textEditor->toPlainText());

    // ③d Search: shows the search dock and focuses its query field.
    shell.commands()->action(QStringLiteral("search.focus"))->trigger();
    QApplication::processEvents();
    QWidget *searchContent = shell.panels()->dock(QStringLiteral("search")) != nullptr
                                 ? shell.panels()->dock(QStringLiteral("search"))->widget()
                                 : nullptr;
    auto *query = searchContent != nullptr
                      ? searchContent->findChild<QLineEdit *>(QStringLiteral("search-query"))
                      : nullptr;
    check(QStringLiteral("a2.toolbar.search-shows-panel"),
          shell.panels()->isVisible(QStringLiteral("search")));
    check(QStringLiteral("a2.toolbar.search-focuses-query"),
          query != nullptr && (query->hasFocus() || QApplication::focusWidget() == query));
}

void testHelpViewer(const QString &directory)
{
    vedit::app::ApplicationShell shell(testOptions(directory));
    shell.startSession();
    shell.show();
    QApplication::processEvents();

    const QString markdown = vedit::app::HelpViewer::manualMarkdown();
    check(QStringLiteral("a2.help.manual-resource-not-empty"), !markdown.isEmpty(),
          QStringLiteral("%1 bytes").arg(markdown.size()));
    check(QStringLiteral("a2.help.manual-has-title"),
          vedit::app::HelpViewer::manualTitle() == kManualTitle,
          vedit::app::HelpViewer::manualTitle());

    QAction *manual = shell.commands()->action(QStringLiteral("help.user-manual"));
    check(QStringLiteral("a2.help.command-exists"), manual != nullptr);
    check(QStringLiteral("a2.help.command-shortcut"),
          manual != nullptr && manual->shortcut() == QKeySequence(Qt::Key_F1));
    if (manual != nullptr) {
        manual->trigger();
        QApplication::processEvents();
    }
    auto *viewer = shell.findChild<vedit::app::HelpViewer *>();
    check(QStringLiteral("a2.help.viewer-opens"), viewer != nullptr && viewer->isVisible());
    check(QStringLiteral("a2.help.viewer-renders-title"),
          viewer != nullptr && viewer->textView() != nullptr
              && viewer->textView()->toPlainText().contains(kManualTitle));
    if (viewer != nullptr) {
        viewer->close();
    }
}

void testChinese(const QString &directory)
{
#if defined(VEDIT_UX_TRANSLATIONS_DIR)
    const QString translationsDir = QString::fromUtf8(VEDIT_UX_TRANSLATIONS_DIR);
    const QString qm = translationsDir + QStringLiteral("/vedit_zh_CN.qm");
    if (!QFile::exists(qm)) {
        skipCheck(QStringLiteral("a2.i18n.zh"), QStringLiteral("no %1").arg(qm));
        return;
    }
    const QString settings = directory + QStringLiteral("/zh-settings.toml");
    if (!QDir().mkpath(directory)) {
        check(QStringLiteral("a2.i18n.make-dir"), false, directory);
        return;
    }
    {
        QFile file(settings);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            check(QStringLiteral("a2.i18n.write-settings"), false, settings);
            return;
        }
        file.write("\"ui.language\" = \"zh_CN\"\n");
    }
    const vedit::app::i18n::InstallResult installed =
        vedit::app::i18n::installTranslations(settings, translationsDir);
    check(QStringLiteral("a2.i18n.qm-loaded"), installed.translated, installed.diagnostic);
    if (!installed.translated) {
        return;
    }

    vedit::app::ApplicationShell shell(testOptions(directory + QStringLiteral("/zh")));
    addLaneCommands(shell);
    shell.startSession();

    // The six strings the acceptance names: five menu titles + Preferences….
    const QStringList expected{
        QStringLiteral("文件(&F)"), QStringLiteral("编辑(&E)"), QStringLiteral("视图(&V)"),
        QStringLiteral("项目(&P)"), QStringLiteral("帮助(&H)")};
    const QStringList titles = menuTitles(shell);
    check(QStringLiteral("a2.i18n.menu-count"), titles.size() == expected.size(),
          titles.join(QLatin1Char(',')));
    for (int i = 0; i < expected.size() && i < titles.size(); ++i) {
        check(QStringLiteral("a2.i18n.menu-%1").arg(i), titles.at(i) == expected.at(i), titles.at(i));
    }
    QAction *preferences = shell.commands()->action(QStringLiteral("settings.preferences"));
    check(QStringLiteral("a2.i18n.preferences"),
          preferences != nullptr && preferences->text() == QStringLiteral("首选项(&P)…"),
          preferences != nullptr ? preferences->text() : QString());
    // The five panel toggles are translated (the acceptance names all five).
    QAction *panelToggle = shell.commands()->action(QStringLiteral("view.panel.file-tree"));
    check(QStringLiteral("a2.i18n.panel-toggle"),
          panelToggle != nullptr && panelToggle->text().contains(QStringLiteral("文件")),
          panelToggle != nullptr ? panelToggle->text() : QString());
    // The seven editor context-menu labels share the VEdit catalogue.
    const QString definition =
        QCoreApplication::translate("VEdit", "Go to &Definition");
    check(QStringLiteral("a2.i18n.context-menu-label"),
          definition != QStringLiteral("Go to &Definition"), definition);
#else
    skipCheck(QStringLiteral("a2.i18n.zh"), QStringLiteral("build has no translation directory"));
#endif
}

#if defined(VEDIT_UX_HAVE_TERMINAL)
bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QApplication::processEvents();
        if (predicate()) {
            return true;
        }
        QThread::msleep(5);
    }
    QApplication::processEvents();
    return predicate();
}

QString terminalScreenText(const vedit::terminal::TerminalWidget *terminal)
{
    QString text;
    const vedit::terminal::Screen &screen = terminal->screen();
    for (int line = 0; line < screen.bufferLines(); ++line) {
        text += QString::fromStdString(screen.bufferLineText(line));
        text += QLatin1Char('\n');
    }
    return text;
}

// Item 11 + the terminal lane: F1 is the User Manual shortcut at WINDOW level,
// but a focused terminal is a raw input surface and must receive F1 as VT100
// "OP". Both directions are asserted here through QTest's real key events, so
// "the override is claimed" and "the manual does not open" are the same test.
void testTerminalF1(const QString &directory)
{
    // The pty runs `cat -v` with terminal ECHO off: text can only appear if the
    // F1 bytes really reached the child, which renders ESC as "^[".
    vedit::terminal::TerminalOptions options;
    options.shell = QStringLiteral("/bin/sh");
    // Raw mode is required: in canonical mode the pty holds the bytes until a
    // newline, so `cat` would never see the F1 sequence.
    options.arguments = {QStringLiteral("-c"), QStringLiteral("stty raw -echo; exec cat -v")};
    options.autoStart = true;

    vedit::app::ApplicationShell shell(testOptions(directory));
    shell.panels()->setFactory(
        QStringLiteral("terminal"),
        [&options]() { return new vedit::terminal::TerminalPanel(options); });
    shell.resize(1280, 800);
    shell.show();
    shell.activateWindow();
    QApplication::processEvents();
    shell.startSession();
    shell.panels()->showPanel(QStringLiteral("terminal"), &shell);
    QApplication::processEvents();

    QDockWidget *dock = shell.panels()->dock(QStringLiteral("terminal"));
    auto *panel = dock != nullptr ? qobject_cast<vedit::terminal::TerminalPanel *>(dock->widget())
                                  : nullptr;
    check(QStringLiteral("a2.f1.terminal-panel"), panel != nullptr);
    if (panel == nullptr) {
        return;
    }
    check(QStringLiteral("a2.f1.terminal-running"),
          waitFor([panel]() { return panel->isRunning(); }, 8000));
    vedit::terminal::TerminalWidget *terminal = panel->terminal();
    check(QStringLiteral("a2.f1.terminal-widget"), terminal != nullptr);
    if (terminal == nullptr) {
        return;
    }
    terminal->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    check(QStringLiteral("a2.f1.terminal-focus"), terminal->hasFocus());

    // Direction 1: the terminal claims the ShortcutOverride for plain F1 ...
    QKeyEvent overrideEvent(QEvent::ShortcutOverride, Qt::Key_F1, Qt::NoModifier);
    QApplication::sendEvent(terminal, &overrideEvent);
    check(QStringLiteral("a2.f1.terminal-claims-shortcut-override"),
          overrideEvent.isAccepted());
    // ... so a real F1 press reaches the pty and NEVER opens the manual.
    QTest::keyClick(terminal, Qt::Key_F1);
    const bool reachedPty = waitFor(
        [terminal]() { return terminalScreenText(terminal).contains(QStringLiteral("^[OP")); },
        5000);
    check(QStringLiteral("a2.f1.terminal-f1-reaches-pty"), reachedPty);
    QApplication::processEvents();
    check(QStringLiteral("a2.f1.terminal-does-not-open-manual"),
          shell.findChild<vedit::app::HelpViewer *>() == nullptr);

    // Direction 2: with the editor focused, the same F1 key opens the manual.
    QWidget *editor = shell.editorArea()->activePane() != nullptr
                          ? shell.editorArea()->activePane()->editorWidget()
                          : nullptr;
    check(QStringLiteral("a2.f1.editor-focus-target"), editor != nullptr);
    if (editor == nullptr) {
        return;
    }
    editor->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    QTest::keyClick(editor, Qt::Key_F1);
    const bool manualOpened = waitFor(
        [&shell]() { return shell.findChild<vedit::app::HelpViewer *>() != nullptr; }, 5000);
    check(QStringLiteral("a2.f1.manual-opens-outside-terminal"), manualOpened);
    if (auto *viewer = shell.findChild<vedit::app::HelpViewer *>()) {
        viewer->close();
    }
}
#endif

#if defined(VEDIT_UX_HAVE_EDIT_CORE)
// The real product editor: KTextEditor's OWN undo/redo actions are what the
// toolbar triggers. The document text is asserted, so a "dead button" fails.
void testRealEditorUndoRedo(const QString &directory)
{
    vedit::app::ApplicationShell shell(testOptions(directory));
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    shell.startSession();
    shell.show();
    QApplication::processEvents();

    vedit::app::EditorPane *pane = shell.editorArea()->activePane();
    auto *view = pane != nullptr ? qobject_cast<KTextEditor::View *>(pane->editorWidget()) : nullptr;
    check(QStringLiteral("a2.undo.real-ktexteditor-view"), view != nullptr);
    if (view == nullptr) {
        return;
    }
    KTextEditor::Document *document = view->document();
    document->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("hello"));
    QApplication::processEvents();

    QAction *undo = shell.commands()->action(QStringLiteral("edit.undo"));
    QAction *redo = shell.commands()->action(QStringLiteral("edit.redo"));
    check(QStringLiteral("a2.undo.real-enabled-after-edit"), undo != nullptr && undo->isEnabled());
    if (undo != nullptr) {
        undo->trigger();
    }
    QApplication::processEvents();
    check(QStringLiteral("a2.undo.real-changes-document"),
          document->text().isEmpty(), document->text());
    check(QStringLiteral("a2.redo.real-enabled-after-undo"), redo != nullptr && redo->isEnabled());
    if (redo != nullptr) {
        redo->trigger();
    }
    QApplication::processEvents();
    check(QStringLiteral("a2.redo.real-restores-document"),
          document->text() == QStringLiteral("hello"), document->text());
}
#endif

#if defined(VEDIT_UX_TRANSLATIONS_DIR)
void installChineseForScreenshots(const QString &directory)
{
    QDir().mkpath(directory + QStringLiteral("/state"));
    const QString settings = directory + QStringLiteral("/state/zh-shot-settings.toml");
    QFile file(settings);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write("\"ui.language\" = \"zh_CN\"\n");
    file.close();
    vedit::app::i18n::installTranslations(settings, QString::fromUtf8(VEDIT_UX_TRANSLATIONS_DIR));
}
#endif

bool saveGrab(QWidget *widget, const QString &path)
{
    if (widget == nullptr) {
        return false;
    }
    const bool ok = widget->grab().save(path);
    std::printf("SCREENSHOT %s %s\n", ok ? "OK" : "FAIL", qPrintable(path));
    std::fflush(stdout);
    return ok;
}

int runScreenshots(const QString &directory)
{
    if (!QDir().mkpath(directory)) {
        std::printf("SCREENSHOT-FAIL cannot create %s\n", qPrintable(directory));
        return 1;
    }
    bool ok = true;
    {
        vedit::app::ApplicationShell shell(testOptions(directory + QStringLiteral("/en")));
        addLaneCommands(shell);
        installSearchPanelFactory(shell);
        shell.resize(1600, 950);
        shell.show();
        QApplication::processEvents();
        shell.startSession();
        QApplication::processEvents();
        ok = saveGrab(&shell, directory + QStringLiteral("/10-main-toolbar-menu.png")) && ok;

        auto *viewer = vedit::app::HelpViewer::open(&shell);
        QApplication::processEvents();
        ok = saveGrab(viewer, directory + QStringLiteral("/11-user-manual.png")) && ok;
        if (viewer != nullptr) {
            viewer->close();
        }
    }
#if defined(VEDIT_UX_TRANSLATIONS_DIR)
    installChineseForScreenshots(directory);
    {
        vedit::app::ApplicationShell shell(testOptions(directory + QStringLiteral("/zh")));
        addLaneCommands(shell);
        shell.resize(1600, 950);
        shell.show();
        QApplication::processEvents();
        shell.startSession();
        QApplication::processEvents();
        ok = saveGrab(&shell, directory + QStringLiteral("/12-main-menu-zh.png")) && ok;
    }
#endif
    std::printf("A2-SCREENSHOTS ok=%d dir=%s\n", ok ? 1 : 0, qPrintable(directory));
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vedit-ux-selftest"));

    QString screenshotDirectory;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == QStringLiteral("--screenshots") && i + 1 < argc) {
            screenshotDirectory = QString::fromLocal8Bit(argv[++i]);
        }
    }
    if (!screenshotDirectory.isEmpty()) {
        return runScreenshots(screenshotDirectory);
    }

    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::printf("CHECK a2.temp-dir FAIL\n");
        return 1;
    }
    const QString root = temporary.path();

    testStructure(root + QStringLiteral("/structure"));
    testHelpViewer(root + QStringLiteral("/help"));
    testChinese(root + QStringLiteral("/i18n"));
#if defined(VEDIT_UX_HAVE_TERMINAL)
    testTerminalF1(root + QStringLiteral("/terminal"));
#else
    skipCheck(QStringLiteral("a2.f1.terminal-f1-reaches-pty"),
              QStringLiteral("build has no terminal lane"));
#endif
#if defined(VEDIT_UX_HAVE_EDIT_CORE)
    testRealEditorUndoRedo(root + QStringLiteral("/ktexteditor"));
#else
    skipCheck(QStringLiteral("a2.undo.real-ktexteditor-view"),
              QStringLiteral("build has no edit-core provider"));
#endif

    std::printf("A2-UX-SELFTEST failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
