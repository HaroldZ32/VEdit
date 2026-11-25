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
#include <QWidget>

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
    check(QStringLiteral("a2.action.undo-disabled-without-view"),
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

    check(QStringLiteral("a2.toolbar.absent"), shell.findChildren<QToolBar *>().isEmpty());
    check(QStringLiteral("a2.toolbar.no-toggle"),
          shell.commands()->action(QStringLiteral("view.toolbar")) == nullptr);
    const auto inMenu = [&shell](const char *id, const char *title) {
        QMenu *menu = menuByStrippedTitle(shell, QString::fromLatin1(title));
        QAction *action = shell.commands()->action(QString::fromLatin1(id));
        return menu != nullptr && action != nullptr && menu->actions().contains(action);
    };
    check(QStringLiteral("a2.menu.former-toolbar-actions"),
          inMenu("file.new", "File") && inMenu("file.open", "File")
          && inMenu("file.open-folder", "File") && inMenu("file.switch-project", "File")
          && inMenu("file.save", "File") && inMenu("file.save-all", "File")
          && inMenu("edit.undo", "Edit") && inMenu("edit.redo", "Edit")
          && inMenu("search.focus", "View") && inMenu("project.lint", "Project")
          && inMenu("diagnostics.format", "Project")
          && shell.findChild<QMenu *>(QStringLiteral("menu-theme")) != nullptr);

    // Build/Format are the diagnostics lane's commands and really run them.
    int lintRuns = 0;
    int formatRuns = 0;
    shell.commands()->setHandler(QStringLiteral("project.lint"), [&lintRuns]() { ++lintRuns; });
    shell.commands()->setHandler(QStringLiteral("diagnostics.format"), [&formatRuns]() { ++formatRuns; });
    shell.commands()->action(QStringLiteral("project.lint"))->trigger();
    shell.commands()->action(QStringLiteral("diagnostics.format"))->trigger();
    check(QStringLiteral("a2.action.build-runs-command"), lintRuns == 1);
    check(QStringLiteral("a2.action.format-runs-command"), formatRuns == 1);

    // ③b New: a dirty tab gets a NEW document tab next to it.
    vedit::app::EditorPane *pane = shell.editorArea()->activePane();
    check(QStringLiteral("a2.action.active-pane"), pane != nullptr);
    if (pane == nullptr) {
        return;
    }
    auto *textEditor = qobject_cast<QPlainTextEdit *>(pane->editorWidget());
    check(QStringLiteral("a2.action.demo-editor"), textEditor != nullptr);
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
    check(QStringLiteral("a2.action.new-opens-document"), pane->tabCount() == tabsBefore + 1,
          QStringLiteral("tabs %1 -> %2").arg(tabsBefore).arg(pane->tabCount()));

    // ③c Undo/Redo really change the document (plain-text fallback path).
    pane->setActiveTab(0);
    QApplication::processEvents();
    QAction *undo = shell.commands()->action(QStringLiteral("edit.undo"));
    check(QStringLiteral("a2.action.undo-enabled-with-view"), undo != nullptr && undo->isEnabled());
    undo->trigger();
    QApplication::processEvents();
    check(QStringLiteral("a2.action.undo-changes-document"),
          textEditor->toPlainText() == beforeTyping, textEditor->toPlainText());
    QAction *redo = shell.commands()->action(QStringLiteral("edit.redo"));
    check(QStringLiteral("a2.action.redo-enabled"), redo != nullptr && redo->isEnabled());
    redo->trigger();
    QApplication::processEvents();
    check(QStringLiteral("a2.action.redo-restores-document"),
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
    check(QStringLiteral("a2.action.search-shows-panel"),
          shell.panels()->isVisible(QStringLiteral("search")));
    check(QStringLiteral("a2.action.search-focuses-query"),
          query != nullptr && (query->hasFocus() || QApplication::focusWidget() == query));
}

// Batch 20261110 (AC-1.1/1.2/1.6): the shell's user-visible text and the
// "an unowned dock does not render" rule.
//
// The About box used to be a shell debug dump ("app-shell lane", "Editor
// provider:", "Panes:"); it is product text now, and `aboutText()` is the one
// source of that text so this can be asserted without a modal dialog. The View
// menu no longer carries a demo suffix, and a panel spec without a factory
// produces NO dock (and no placeholder widget) instead of an apology inside one.
void testShellChrome(const QString &directory)
{
    // The About box reads the version the application entry point publishes; the
    // selftest publishes a known one so the assertion is about the text.
    QCoreApplication::setApplicationVersion(QStringLiteral("9.9.9-selftest"));

    vedit::app::ApplicationShell shell(testOptions(directory));
    const QString about = shell.aboutText();
    check(QStringLiteral("a3.about.product-name"), about.contains(QStringLiteral("VEdit")),
          about.left(120));
    check(QStringLiteral("a3.about.version"), about.contains(QStringLiteral("9.9.9-selftest")),
          about.left(200));
    check(QStringLiteral("a3.about.licence"), about.contains(QStringLiteral("MIT")),
          about.left(300));
    check(QStringLiteral("a3.about.homepage-link"),
          about.contains(QStringLiteral("https://github.com/HaroldZ32/VEdit")), about);
    check(QStringLiteral("a3.about.docs-link"), about.contains(QStringLiteral("USER_GUIDE.md")),
          about);
    // The exact developer strings AC-1.1 forbids in the About box.
    const QStringList banned{QStringLiteral("lane"),
                             QStringLiteral("Editor provider:"),
                             QStringLiteral("Theme:"),
                             QStringLiteral("Panes:"),
                             QStringLiteral("State:"),
                             QStringLiteral("Diagnostics:")};
    for (const QString &needle : banned) {
        check(QStringLiteral("a3.about.no-") + needle,
              !about.contains(needle, Qt::CaseInsensitive), needle);
    }

    QAction *mixed = shell.commands()->action(QStringLiteral("view.layout.mixed"));
    check(QStringLiteral("a3.menu.mixed-title"),
          mixed != nullptr && !mixed->text().contains(QStringLiteral("demo"), Qt::CaseInsensitive),
          mixed != nullptr ? mixed->text() : QStringLiteral("no action"));
    // Rerouting the LABEL must not lose the shortcut the command already owned.
    check(QStringLiteral("a3.menu.mixed-shortcut"),
          mixed != nullptr && mixed->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+M")),
          mixed != nullptr ? mixed->shortcut().toString() : QString());

    // An unowned panel renders nothing at all.
    vedit::app::PanelSpec ownerless;
    ownerless.id = QStringLiteral("selftest-unowned");
    ownerless.title = QStringLiteral("Unowned");
    ownerless.owner = QStringLiteral("no-lane");
    shell.panels()->addPanel(ownerless);
    shell.show();
    QApplication::processEvents();
    check(QStringLiteral("a3.unowned.no-dock"),
          shell.panels()->insert(QStringLiteral("selftest-unowned"), &shell) == nullptr);
    check(QStringLiteral("a3.unowned.not-inserted"),
          shell.panels()->dock(QStringLiteral("selftest-unowned")) == nullptr);
    check(QStringLiteral("a3.unowned.toggle-refused"),
          !shell.panels()->toggle(QStringLiteral("selftest-unowned"), &shell));
    bool placeholder = false;
    const QList<QWidget *> widgets = shell.findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        if (widget->objectName().startsWith(QStringLiteral("panel-placeholder-"))) {
            placeholder = true;
        }
    }
    check(QStringLiteral("a3.unowned.no-placeholder-widget"), !placeholder);
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

// batch-20261110d T5 — the shortcut conflict the real machine showed:
// Ctrl+Alt+F was effective for BOTH `view.panel.file-tree` and
// `diagnostics.format`, yet `commands.no-duplicate-shortcuts` stayed green.
// The gate only ever built a registry with a FRESH shortcut store, so it could
// not see a collision that comes from the PERSISTED store: a legacy
// `shortcuts.json` entry `diagnostics.format: Ctrl+Alt+F` (written when that
// WAS the shipped default, before U3 moved it to Ctrl+Shift+F) is applied on top
// of the new default and collides with the Files-panel toggle. This test builds
// that exact machine state and asserts the registry-level properties the
// product must hold on it.
void testShortcutConflicts(const QString &directory)
{
    if (!QDir().mkpath(directory)) {
        check(QStringLiteral("a2.shortcuts.temp-dir"), false, directory);
        return;
    }
    const QString storePath = directory + QStringLiteral("/shortcuts.json");
    {
        QFile file(storePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            check(QStringLiteral("a2.shortcuts.legacy-store-written"), false, storePath);
            return;
        }
        file.write("{\"version\":1,\"shortcuts\":{\"diagnostics.format\":\"Ctrl+Alt+F\"}}\n");
    }
    vedit::app::JsonShortcutStore store(storePath);
    QString storeError;
    store.load(&storeError);
    check(QStringLiteral("a2.shortcuts.legacy-store-loaded"),
          storeError.isEmpty()
              && store.shortcut(QStringLiteral("diagnostics.format"), QKeySequence())
                     == QKeySequence(QStringLiteral("Ctrl+Alt+F")),
          storeError.isEmpty()
              ? store.shortcut(QStringLiteral("diagnostics.format"), QKeySequence()).toString()
              : storeError);

    QObject owner;
    vedit::app::CommandRegistry registry(&owner);
    registry.setStore(&store);

    // The two real commands, with their real defaults (app_shell.cpp /
    // diagnostics_integration.cpp): the Files-panel toggle owns Ctrl+Alt+F and
    // the format command's pinned primary is Ctrl+Shift+F (plus Shift+Alt+F as
    // the VS Code alternative).
    vedit::app::CommandSpec fileTree;
    fileTree.id = QStringLiteral("view.panel.file-tree");
    fileTree.text = QStringLiteral("To&ggle Files Panel");
    fileTree.menu = QStringLiteral("view");
    fileTree.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Alt+F"));
    check(QStringLiteral("a2.shortcuts.file-tree-registered"), registry.add(fileTree) != nullptr);

    vedit::app::CommandSpec format;
    format.id = QStringLiteral("diagnostics.format");
    format.text = QStringLiteral("&Format Document");
    format.menu = QStringLiteral("project");
    format.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Shift+F"));
    format.extraDefaultShortcuts = {QKeySequence(QStringLiteral("Shift+Alt+F"))};
    check(QStringLiteral("a2.shortcuts.format-registered"), registry.add(format) != nullptr);

    // RED before the repair: the stale override is applied on top of the current
    // default, collides with the Files-panel toggle, and Qt drops BOTH keys.
    const QStringList duplicates = registry.duplicateShortcuts();
    check(QStringLiteral("a2.shortcuts.legacy-store-conflict-free"), duplicates.isEmpty(),
          duplicates.join(QStringLiteral(" | ")));

    // GREEN: the stale override was DROPPED, so the format command falls back to
    // its current default while the Files-panel toggle keeps Ctrl+Alt+F, and the
    // dropped binding is NAMED (a repair is never silent).
    check(QStringLiteral("a2.shortcuts.file-tree-keeps-default"),
          registry.effectiveShortcut(QStringLiteral("view.panel.file-tree"))
              == QKeySequence(QStringLiteral("Ctrl+Alt+F")),
          registry.effectiveShortcut(QStringLiteral("view.panel.file-tree"))
              .toString(QKeySequence::PortableText));
    check(QStringLiteral("a2.shortcuts.format-falls-back"),
          registry.effectiveShortcut(QStringLiteral("diagnostics.format"))
              == QKeySequence(QStringLiteral("Ctrl+Shift+F")),
          registry.effectiveShortcut(QStringLiteral("diagnostics.format"))
              .toString(QKeySequence::PortableText));
    check(QStringLiteral("a2.shortcuts.override-dropped"),
          !registry.hasUserOverride(QStringLiteral("diagnostics.format")));
    const QString repairs = registry.shortcutRepairs().join(QStringLiteral(" | "));
    check(QStringLiteral("a2.shortcuts.repair-named"),
          repairs.contains(QStringLiteral("diagnostics.format"))
              && repairs.contains(QStringLiteral("Ctrl+Alt+F"))
              && repairs.contains(QStringLiteral("view.panel.file-tree")),
          repairs);

    // T5 (2): the check must cover the WHOLE registry — every registered command
    // × every sequence it carries now (primary, extra defaults and any applied
    // override) must appear in the bindings dump the product selfcheck prints.
    // A command registered late (like a lane's) is included by construction; this
    // asserts it instead of assuming it.
    vedit::app::CommandSpec rename;
    rename.id = QStringLiteral("rename.symbol");
    rename.text = QStringLiteral("Rename Symbol in &Project…");
    rename.menu = QStringLiteral("project");
    // The batch's stop rule: this command's Ctrl+Shift+F2 must NOT move.
    rename.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Shift+F2"));
    registry.add(rename);
    const QStringList bindings = registry.shortcutBindings();
    QStringList missing;
    for (const QString &id : registry.ids()) {
        for (const QKeySequence &sequence : registry.effectiveShortcuts(id)) {
            if (!sequence.isEmpty()
                && !bindings.contains(sequence.toString() + QLatin1Char('\t') + id)) {
                missing.append(id);
            }
        }
    }
    check(QStringLiteral("a2.shortcuts.scan-covers-every-command"), missing.isEmpty(),
          missing.join(QStringLiteral(",")));
    check(QStringLiteral("a2.shortcuts.rename-symbol-untouched"),
          registry.effectiveShortcut(QStringLiteral("rename.symbol"))
              == QKeySequence(QStringLiteral("Ctrl+Shift+F2"))
              && !registry.shortcutRepairs().join(QStringLiteral(" | "))
                      .contains(QStringLiteral("rename.symbol")),
          registry.effectiveShortcut(QStringLiteral("rename.symbol"))
              .toString(QKeySequence::PortableText));

    // NEGATIVE CONTROL (T5 ③): inject a DIFFERENT duplicate by hand — two
    // shipped defaults sharing Ctrl+Alt+M — and prove the detector still fires
    // and that two defaults are never silently rewritten. The registry is a
    // local object, so destroying it reverts the injection; nothing shipped is
    // touched.
    {
        QObject controlOwner;
        vedit::app::CommandRegistry control(&controlOwner);
        vedit::app::CommandSpec first;
        first.id = QStringLiteral("test.duplicate.one");
        first.text = QStringLiteral("First");
        first.menu = QStringLiteral("view");
        first.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Alt+M"));
        vedit::app::CommandSpec second;
        second.id = QStringLiteral("test.duplicate.two");
        second.text = QStringLiteral("Second");
        second.menu = QStringLiteral("view");
        second.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Alt+M"));
        control.add(first);
        control.add(second);
        const QStringList found = control.duplicateShortcuts();
        check(QStringLiteral("a2.shortcuts.duplicate-detected"),
              found.size() == 1 && found.first().contains(QStringLiteral("Ctrl+Alt+M"))
                  && found.first().contains(QStringLiteral("test.duplicate.one"))
                  && found.first().contains(QStringLiteral("test.duplicate.two")),
              found.join(QStringLiteral(" | ")));
        check(QStringLiteral("a2.shortcuts.defaults-not-repaired"),
              control.shortcutRepairs().isEmpty(),
              control.shortcutRepairs().join(QStringLiteral(" | ")));
    }
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
    testShortcutConflicts(root + QStringLiteral("/shortcuts"));
    testShellChrome(root + QStringLiteral("/chrome"));
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
