// VEdit application shell entry point (task T13).
//
// Modes:
//   vedit-app                       run the shell (interactive)
//   vedit-app --selfcheck           non-interactive checks, machine-readable output
//   vedit-app --screenshots <dir>   capture the two split states + empty state
//
// The shell has no project model and no editor of its own: those arrive through
// the seams in src/app (EditorProvider / PanelRegistry / IShortcutStore). See
// evidence/app-shell/ for the acceptance evidence of this task.
#include "app_shell.h"
#include "crash_reporter.h"

#include "command_registry.h"
#include "desktop_shortcuts.h"
#include "file_dialog_filters.h"
#include "editor_area.h"
#include "i18n.h"
#include "integration.h"
#include "../workspace/diagnostics/toolchain.h"
#include "panel_registry.h"
#include "path_display.h"
#include "terminal_panel.h"
#include "theme_manager.h"
#if defined(VEDIT_HAVE_EDIT_CORE)
#include "kernel/cross_boundary.h"
#include "kernel/edit_core_selftest.h"
#include "kernel/ktexteditor_provider.h"
#include "../editor/kernel/fold_breadcrumb.h"
#include "../editor/kernel/lsp_actions.h"
#include "../syntax/provider/syntax_service.h"
#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif
#ifdef VEDIT_HAVE_RUST_FFI
#include "vd_ffi.h"
#endif
// UX batch 2 / A4: appearance (font + interface scale) and the process-wide
// format/lint configuration the diagnostics lane consumes. The include path
// arrives through the workspace integration -> settings lane link edge; a tree
// without that lane still compiles (the __has_include guard).
#if __has_include("ui_scale.h")
#include "format_options.h"
#include "lint_options.h"
#include "settings_client.h"
#include "ui_scale.h"
#define VEDIT_HAVE_APPEARANCE_SETTINGS 1
#endif

#include <QAction>
#include <QApplication>
#include <QSet>
#include <QSignalBlocker>
#include <QCommandLineParser>

#include <csignal>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QToolButton>
#include <QString>
#include <QStatusBar>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <cstdio>
#include <cstring>
#include <utility>
#include <memory>
#include <string>

using vedit::app::ApplicationShell;
using vedit::app::CommandRegistry;
using vedit::app::EditorArea;
using vedit::app::EditorPane;
using vedit::app::PanelRegistry;
using vedit::app::ShellOptions;
using vedit::app::ThemeManager;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : " ", qPrintable(detail));
    std::fflush(stdout);
}

bool sameJson(const QJsonObject &left, const QJsonObject &right)
{
    return QJsonDocument(left).toJson(QJsonDocument::Compact) ==
           QJsonDocument(right).toJson(QJsonDocument::Compact);
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content);
    }
}

void pump()
{
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

#if defined(VEDIT_HAVE_EDIT_CORE)
// T94: the same 25 modules x 40 lines SystemVerilog sample the folding lane's
// acceptance uses (1000 lines): each module keeps its first line and folds the
// other 39, and the body carries function/always/generate/begin regions.
QByteArray largeSystemVerilogSample()
{
    QString text;
    for (int index = 0; index < 25; ++index) {
        const QString n = QString::number(index);
        text += QStringLiteral("module m%1 #(parameter W = 8) (\n").arg(n);
        text += QStringLiteral("  input  logic clk,\n");
        text += QStringLiteral("  input  logic rst_n,\n");
        text += QStringLiteral("  output logic [W-1:0] q\n");
        text += QStringLiteral(");\n");
        text += QStringLiteral("  logic [W-1:0] state, nxt;\n");
        text += QStringLiteral("  function automatic logic [W-1:0] inc(input logic [W-1:0] v);\n");
        text += QStringLiteral("    inc = v + 1'b1;\n");
        text += QStringLiteral("  endfunction\n");
        text += QStringLiteral("  always_ff @(posedge clk or negedge rst_n) begin\n");
        text += QStringLiteral("    if (!rst_n) begin\n");
        text += QStringLiteral("      state <= '0;\n");
        text += QStringLiteral("    end else begin\n");
        text += QStringLiteral("      state <= inc(nxt);\n");
        text += QStringLiteral("    end\n");
        text += QStringLiteral("  end\n");
        text += QStringLiteral("  always_comb begin\n");
        text += QStringLiteral("    nxt = state + 1'b1;\n");
        text += QStringLiteral("  end\n");
        text += QStringLiteral("  generate\n");
        text += QStringLiteral("    for (genvar i = 0; i < W; i = i + 1) begin : g_lane\n");
        text += QStringLiteral("      assign q[i] = state[i];\n");
        text += QStringLiteral("    end\n");
        text += QStringLiteral("  endgenerate\n");
        for (int line = 24; line < 39; ++line) {
            text += QStringLiteral("  // filler line %1 of module %2\n").arg(line).arg(n);
        }
        text += QStringLiteral("endmodule\n");
    }
    return text.toUtf8();
}
#endif

// --- Ctrl+S duplicate-binding detection (task T80) -------------------------
// Qt reports "Ambiguous shortcut overload" and triggers NEITHER action when two
// enabled actions carry the same shortcut in the same window context. The
// product had exactly that: the shell's file.save action (Ctrl+S) plus the
// KTextEditor view's own standard save action (KStandardAction::save). The
// helpers below enumerate what Qt can see, so the selfcheck can assert the
// invariant "exactly ONE Ctrl+S binding" instead of trusting one simulated key
// press (the simulated press missed the ambiguity in T71).
QList<QAction *> collectShortcutActions(QWidget *root)
{
    QList<QAction *> actions;
    if (root == nullptr) {
        return actions;
    }
    const auto add = [&actions](const QList<QAction *> &list) {
        for (QAction *action : list) {
            if (action != nullptr && !actions.contains(action)) {
                actions.append(action);
            }
        }
    };
    add(root->actions());
    const QList<QWidget *> widgets = root->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        add(widget->actions());
    }
    add(root->findChildren<QAction *>());
    return actions;
}

bool hasCtrlSShortcut(const QAction *action)
{
    if (action == nullptr) {
        return false;
    }
    const QKeySequence ctrlS(QStringLiteral("Ctrl+S"));
    for (const QKeySequence &sequence : action->shortcuts()) {
        if (sequence == ctrlS) {
            return true;
        }
    }
    return false;
}

QList<QAction *> ctrlSActions(QWidget *root)
{
    QList<QAction *> found;
    for (QAction *action : collectShortcutActions(root)) {
        if (hasCtrlSShortcut(action)) {
            found.append(action);
        }
    }
    return found;
}

void printCtrlSBindings(QWidget *root, const char *tag)
{
    for (QAction *action : ctrlSActions(root)) {
        QStringList shortcuts;
        for (const QKeySequence &sequence : action->shortcuts()) {
            shortcuts.append(sequence.toString(QKeySequence::PortableText));
        }
        QString chain;
        for (QObject *object = action->parent(); object != nullptr; object = object->parent()) {
            chain.append(QString::fromLatin1(object->metaObject()->className()));
            chain.append(QLatin1Char('<'));
        }
        QString associated;
        for (QObject *object : action->associatedObjects()) {
            associated.append(QString::fromLatin1(object->metaObject()->className()));
            if (auto *widget = qobject_cast<QWidget *>(object)) {
                associated.append(QLatin1Char('(') + widget->objectName() + QLatin1Char(')'));
            }
            associated.append(QLatin1Char(' '));
        }
        QString holders;
        if (root != nullptr) {
            for (QWidget *widget : root->findChildren<QWidget *>()) {
                if (widget->actions().contains(action)) {
                    holders.append(QString::fromLatin1(widget->metaObject()->className()));
                    holders.append(QLatin1Char('(') + widget->objectName() + QLatin1Char(')'));
                    holders.append(QLatin1Char(' '));
                }
            }
        }
        std::printf("CTRL-S-BINDING %s objectName=%s class=%s shortcut=%s context=%d "
                    "associated=[%s] holders=[%s] chain=%s\n",
                    tag, qPrintable(action->objectName()), action->metaObject()->className(),
                    qPrintable(shortcuts.join(QLatin1Char(','))),
                    static_cast<int>(action->shortcutContext()), qPrintable(associated),
                    qPrintable(holders), qPrintable(chain));
    }
}

// Cross-boundary counter (AC-S1.1 / C1): the Rust service layer is the only C
// ABI; the edit kernel links no `vd_*` symbol, and every ABI call site in this
// file is instrumented with this helper. The edit-core selfcheck asserts the
// counter does not move while editing, and that the counter is live.
void noteAbiCall()
{
#if defined(VEDIT_HAVE_EDIT_CORE)
    vedit::editor::noteCrossBoundaryCall();
#endif
}

// ---------------------------------------------------------------------------
// self-check
// ---------------------------------------------------------------------------

int runSelfCheck()
{
    // T82: toolchain observability is part of the self-check output. The line
    // names each tool's source (bundled > PATH > env) and the missing list.
    std::printf("%s\n", qPrintable(vedit::workspace::diagnostics::toolchainStatusLine()));
    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("APP-SHELL-SELFTEST ok=0 reason=temporary-directory\n");
        return 1;
    }
    const QString stateDir = temp.path() + QStringLiteral("/state");

    // --- FIX 1: the ONE path-display rule (src/common/path_display.h) --------
    // The three cases the user ruling names, asserted directly; every surface
    // below calls this one function, so this is the rule the UI renders.
    {
        const auto absoluteOf = [](const QString &value) {
            return QDir::cleanPath(QFileInfo(value).absoluteFilePath());
        };
        const QString root = temp.path() + QStringLiteral("/display-root/");
        const QString under = temp.path() + QStringLiteral("/display-root/rtl/top.sv");
        const QString outside = temp.path() + QStringLiteral("/display-outside/top.sv");
        check("path.display.under-root",
              vedit::paths::displayPath(root, under) == QStringLiteral("rtl/top.sv"),
              vedit::paths::displayPath(root, under));
        check("path.display.under-root.normalized-root",
              vedit::paths::displayPath(vedit::paths::normalizedRoot(root), under)
                  == QStringLiteral("rtl/top.sv"));
        check("path.display.outside-root",
              vedit::paths::displayPath(root, outside) == absoluteOf(outside),
              vedit::paths::displayPath(root, outside));
        check("path.display.no-root",
              vedit::paths::displayPath(QString(), outside) == absoluteOf(outside),
              vedit::paths::displayPath(QString(), outside));
        // A sibling whose NAME starts with the root's name ("/a/root2" next to
        // "/a/root") is outside: the separator is what makes that safe.
        check("path.display.sibling-prefix-outside",
              vedit::paths::displayPath(root,
                                        temp.path() + QStringLiteral("/display-root-2/x.sv"))
                  == absoluteOf(temp.path() + QStringLiteral("/display-root-2/x.sv")));
        // The root itself is not "inside" itself: no bare "." in the UI.
        check("path.display.root-itself-absolute",
              vedit::paths::displayPath(root, root) == absoluteOf(root));
        check("path.display.root-normalized",
              vedit::paths::normalizedRoot(root) == absoluteOf(root)
                  && !vedit::paths::normalizedRoot(root).endsWith(QLatin1Char('/')),
              vedit::paths::normalizedRoot(root));
    }

    ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    auto shell = std::make_unique<ApplicationShell>(options);
    shell->show();
    pump();

    check("empty.no-project-label", shell->projectLabel() == QStringLiteral("no project"));
    check("empty.one-pane", shell->editorArea()->paneCount() == 1);
    check("empty.window-title", shell->windowTitle().contains(QStringLiteral("no project")));

    // T132: settings are a modal Preferences dialog, not a dock panel, so five
    // panels remain (the Settings menu entry replaces the old dock + toggle).
    const QStringList expectedPanels{QStringLiteral("file-tree"), QStringLiteral("search"),
                                     QStringLiteral("terminal"), QStringLiteral("vcs"),
                                     QStringLiteral("outline")};
    check("panels.no-settings-dock", !shell->panels()->contains(QStringLiteral("settings")));
    check("panels.registry.five", shell->panels()->ids().size() == expectedPanels.size());
    for (const QString &id : expectedPanels) {
        check(("panels.registered." + id.toStdString()).c_str(), shell->panels()->contains(id));
    }
    check("panels.none-inserted-by-default", shell->panels()->insertedIds().isEmpty());

    // On-demand insertion + toggle for every panel.
    for (const QString &id : expectedPanels) {
        shell->panels()->showPanel(id, shell.get());
        pump();
        check(("panels.show." + id.toStdString()).c_str(), shell->panels()->isVisible(id));
        const bool hidden = shell->panels()->toggle(id, shell.get());
        pump();
        check(("panels.toggle-hide." + id.toStdString()).c_str(), !hidden && !shell->panels()->isVisible(id));
        const bool shown = shell->panels()->toggle(id, shell.get());
        pump();
        check(("panels.toggle-show." + id.toStdString()).c_str(), shown && shell->panels()->isVisible(id));
    }
    check("panels.five-docks", shell->panels()->docks().size() == expectedPanels.size());

    // Menu / shortcut framework.
    check("menus.file", shell->commands()->menus().contains(QStringLiteral("file")));
    check("menus.view", shell->commands()->menus().contains(QStringLiteral("view")));
    check("menus.edit", shell->commands()->menus().contains(QStringLiteral("edit")));
    check("menus.help", shell->commands()->menus().contains(QStringLiteral("help")));
    // T132: Settings -> Preferences... (Ctrl+,) replaces the old panel toggle.
    QAction *preferences = shell->commands()->action(QStringLiteral("settings.preferences"));
    check("settings.preferences.exists", preferences != nullptr);
    // AC-4.7: compared through the translation key, not the English original, so
    // the check holds in every interface language (the selfcheck installs none,
    // so today it still reads "&Settings").
    check("settings.preferences.menu",
          preferences != nullptr && shell->commands()->menuLabel(QStringLiteral("edit"))
              == QCoreApplication::translate("vedit::app::ApplicationShell", "&Edit"));
    check("settings.preferences.shortcut",
          preferences != nullptr
              && preferences->shortcut() == QKeySequence(QStringLiteral("Ctrl+,")),
          preferences != nullptr ? preferences->shortcut().toString() : QStringLiteral("<missing>"));
    int ctrlCommaBindings = 0;
    for (const QString &command : shell->commands()->ids()) {
        if (QAction *action = shell->commands()->action(command);
            action != nullptr && action->shortcut() == QKeySequence(QStringLiteral("Ctrl+,"))) {
            ++ctrlCommaBindings;
        }
    }
    check("settings.preferences.unique-shortcut", ctrlCommaBindings == 1,
          QStringLiteral("bindings=%1").arg(ctrlCommaBindings));

    // A2 review M2 (requirement 7 was only pinned by the simulated-shell ctest):
    // the shell above is lane-free, so it can only ever see the four menus the
    // shell itself registers. The product installs the workspace lanes BEFORE
    // startSession(); repeat that exact sequence (the same installPanels()
    // main() calls) and assert what the user really sees: the five canonical
    // menus, their fixed order, where Preferences lives, and the registry's
    // shortcut table.
    {
        ShellOptions laneOptions;
        laneOptions.stateDir = temp.path() + QStringLiteral("/lane-state");
        laneOptions.restoreSession = false;
        laneOptions.showDefaultPanels = false;
        laneOptions.deferSessionRestore = true;
        auto laneShell = std::make_unique<ApplicationShell>(laneOptions);
        vedit::workspace::IntegrationOptions laneIntegration;
        laneIntegration.projectRoot = temp.path() + QStringLiteral("/lane-project");
        laneIntegration.stateDir = laneOptions.stateDir;
        QDir().mkpath(laneIntegration.projectRoot);
        vedit::workspace::installPanels(*laneShell, laneIntegration);
        laneShell->startSession();
        laneShell->show();
        pump();

        // (a) all five canonical menus exist — Project is registered by the
        // diagnostics lane, so a lane-free shell cannot satisfy this.
        const QStringList laneMenus = laneShell->commands()->menus();
        const QStringList requiredMenus{QStringLiteral("file"), QStringLiteral("edit"),
                                        QStringLiteral("view"), QStringLiteral("project"),
                                        QStringLiteral("help")};
        QStringList missingMenus;
        for (const QString &menu : requiredMenus) {
            if (!laneMenus.contains(menu)) {
                missingMenus.append(menu);
            }
        }
        check("menus.five.registered", missingMenus.isEmpty(),
              missingMenus.isEmpty()
                  ? QStringLiteral("all five")
                  : QStringLiteral("missing=%1").arg(missingMenus.join(QLatin1Char(','))));

        // (b) the top-level order is FIXED: File, Edit, View, Project, Help.
        QStringList barMenus;
        for (QAction *menuAction : laneShell->menuBar()->actions()) {
            if (menuAction != nullptr && menuAction->menu() != nullptr) {
                barMenus.append(menuAction->menu()->objectName());
            }
        }
        const QStringList expectedBar{QStringLiteral("menu-file"), QStringLiteral("menu-edit"),
                                      QStringLiteral("menu-view"), QStringLiteral("menu-project"),
                                      QStringLiteral("menu-help")};
        check("menus.five.order", barMenus == expectedBar, barMenus.join(QLatin1Char(',')));

        // (c) Preferences is IN the Edit menu — resolved through the registry
        // action, not by comparing the Edit menu label with itself.
        QAction *lanePreferences =
            laneShell->commands()->action(QStringLiteral("settings.preferences"));
        const QList<QAction *> editActions =
            laneShell->commands()->actionsInMenu(QStringLiteral("edit"));
        check("menus.preferences.in-edit-registry",
              lanePreferences != nullptr && editActions.contains(lanePreferences),
              QStringLiteral("edit-actions=%1").arg(editActions.size()));
        bool preferencesOutsideEdit = false;
        for (const QString &menu : requiredMenus) {
            if (menu == QStringLiteral("edit")) {
                continue;
            }
            if (laneShell->commands()->actionsInMenu(menu).contains(lanePreferences)) {
                preferencesOutsideEdit = true;
            }
        }
        check("menus.preferences.only-edit",
              lanePreferences != nullptr && !preferencesOutsideEdit);
        check("menus.preferences.last-in-edit",
              lanePreferences != nullptr && !editActions.isEmpty()
                  && editActions.last() == lanePreferences);

        // A2 review H1 / U3-F1: no two commands may share one default sequence.
        // The registry warns on registration; this is the machine check, and the
        // dump below is the evidence the verifier reads.
        const QStringList bindings = laneShell->commands()->shortcutBindings();
        std::printf("COMMAND-BINDINGS count=%d\n", static_cast<int>(bindings.size()));
        for (const QString &binding : bindings) {
            std::printf("COMMAND-BINDING %s\n", qPrintable(binding));
        }
        const QStringList duplicates = laneShell->commands()->duplicateShortcuts();
        check("commands.no-duplicate-shortcuts", duplicates.isEmpty(),
              duplicates.join(QStringLiteral(" | ")));
        // RM-5c (review M1): a shipped default that a DESKTOP claims globally is
        // a dead key before the application sees it (the WM consumes it). This
        // runs on the LANE-INSTALLED registry, so it covers commands registered
        // outside src/app (e.g. view.panel.diagnostics from the diagnostics lane)
        // — the bare shell in the shortcut regression cannot see those. Every
        // sequence the action carries is checked, extras included.
        QStringList desktopGlobal;
        for (const QString &id : laneShell->commands()->ids()) {
            for (const QKeySequence &sequence : laneShell->commands()->effectiveShortcuts(id)) {
                const QString owner = vedit::app::desktopGlobalShortcutOwner(sequence);
                if (!owner.isEmpty()) {
                    desktopGlobal.append(QStringLiteral("%1: %2 (%3)").arg(
                        id, sequence.toString(QKeySequence::PortableText), owner));
                }
            }
        }
        check("commands.no-desktop-global-shortcuts", desktopGlobal.isEmpty(),
              desktopGlobal.join(QStringLiteral(" | ")));

        // U3r2-F2: the editor context menu's display fallback and the registry
        // default must be ONE value. The menu entry and the toolbar button are
        // the SAME QAction (buildMenuBar()/buildToolBar() both take
        // commands_->action(id)); the context menu prefers that wired action's
        // shortcut and falls back to editorActionShortcut() only when the shell
        // action cannot be resolved. This line proves the two cannot drift.
#if defined(VEDIT_HAVE_EDIT_CORE)
        const QAction *formatAction =
            laneShell->commands()->action(QStringLiteral("diagnostics.format"));
        const QKeySequence registryFormat =
            formatAction != nullptr ? formatAction->shortcut() : QKeySequence();
        const QKeySequence fallbackFormat =
            vedit::editor::editorActionShortcut(vedit::editor::EditorAction::Format);
        check("shortcut.format.registry-equals-fallback",
              formatAction != nullptr && registryFormat == fallbackFormat,
              QStringLiteral("registry=%1 fallback=%2")
                  .arg(registryFormat.toString(), fallbackFormat.toString()));
#endif
    }
    const char *commandIds[] = {"file.new-layout", "file.close-pane", "file.quit",
                                "view.split.horizontal", "view.split.vertical",
                                "view.focus.next", "view.layout.reset", "help.about",
                                "view.panel.file-tree", "view.panel.outline"};
    for (const char *id : commandIds) {
        check((std::string("commands.action.") + id).c_str(),
              shell->commands()->action(QString::fromLatin1(id)) != nullptr);
    }
    check("commands.file-menu-actions",
          shell->commands()->actionsInMenu(QStringLiteral("file")).size() >= 4);
    check("commands.view-panel-actions-checkable",
          shell->commands()->action(QStringLiteral("view.panel.file-tree"))->isCheckable());

    // T129: the View-menu check mark and the dock visibility are one state.
    // Step 2 (the dock's own X) is the bug: without the visibility listener the
    // action stays checked and the next menu click appears to do nothing.
    for (const QString &id : expectedPanels) {
        const QString commandId = QStringLiteral("view.panel.") + id;
        QAction *action = shell->commands()->action(commandId);
        QDockWidget *dock = shell->panels()->dock(id);
        if (action == nullptr || dock == nullptr) {
            check(("panels.sync.available." + id.toStdString()).c_str(), false);
            continue;
        }
        // Normalise: closed => action unchecked.
        dock->close();
        pump();
        check(("panels.sync.closed." + id.toStdString()).c_str(),
              !dock->isVisible() && !action->isChecked());
        // 1) menu action shows it, check mark follows.
        action->trigger();
        pump();
        check(("panels.sync.action-show." + id.toStdString()).c_str(),
              dock->isVisible() && action->isChecked());
        // 2) the panel's own X hides it; the check mark must clear.
        dock->close();
        pump();
        check(("panels.sync.close-clears-check." + id.toStdString()).c_str(),
              !dock->isVisible() && !action->isChecked());
        // 3) one menu click reopens it (no stale checked state).
        action->trigger();
        pump();
        check(("panels.sync.one-click-reopen." + id.toStdString()).c_str(),
              dock->isVisible() && action->isChecked());
        dock->close();
        pump();
    }

    // Reverse control: block the dock's visibility signal and close it. The
    // action MUST stay checked, proving the positive step above really depends
    // on the listener (it would be red if the sync were removed).
    {
        QDockWidget *dock = shell->panels()->dock(QStringLiteral("file-tree"));
        QAction *action = shell->commands()->action(QStringLiteral("view.panel.file-tree"));
        dock->show();
        pump();
        check("panels.sync.reverse-precondition",
              dock->isVisible() && action != nullptr && action->isChecked());
        {
            QSignalBlocker blocker(dock);
            dock->close();
            pump();
            check("panels.sync.reverse-control",
                  !dock->isVisible() && action != nullptr && action->isChecked());
        }
        dock->show();
        pump();
    }

    // Horizontal split.
    shell->applyHorizontalSplitPreset(3);
    pump();
    check("split.horizontal.pane-count", shell->editorArea()->paneCount() == 3);
    check("split.horizontal.root-orientation",
          shell->editorArea()->saveLayout()
                  .value(QStringLiteral("root"))
                  .toObject()
                  .value(QStringLiteral("orientation"))
                  .toString() == QStringLiteral("horizontal"));

    // Vertical split.
    shell->applyVerticalSplitPreset(3);
    pump();
    check("split.vertical.pane-count", shell->editorArea()->paneCount() == 3);
    check("split.vertical.root-orientation",
          shell->editorArea()->saveLayout()
                  .value(QStringLiteral("root"))
                  .toObject()
                  .value(QStringLiteral("orientation"))
                  .toString() == QStringLiteral("vertical"));

    // Focus switching.
    const auto panes = shell->editorArea()->panes();
    check("panes.three", panes.size() == 3);
    shell->editorArea()->setActivePane(panes.at(1));
    check("focus.set-second", shell->editorArea()->activePane() == panes.at(1));
    shell->editorArea()->focusNextPane();
    check("focus.next-wraps", shell->editorArea()->activePane() == panes.at(2));

    // Close / re-layout.
    shell->editorArea()->closeActivePane();
    pump();
    check("close.pane-count", shell->editorArea()->paneCount() == 2);
    shell->editorArea()->resetLayout();
    pump();
    check("reset.one-pane", shell->editorArea()->paneCount() == 1);
    shell->editorArea()->closeActivePane();
    pump();
    check("close.last-pane.keeps-one", shell->editorArea()->paneCount() == 1);
    check("close.last-pane.active", shell->editorArea()->activePane() != nullptr);

    // T130: the split panes have a visible close path, and unsaved changes are
    // never discarded silently.
    QAction *closePaneAction = shell->commands()->action(QStringLiteral("file.close-pane"));
    check("close.command.exists", closePaneAction != nullptr);
    check("close.command.in-view", closePaneAction != nullptr
          && shell->commands()->actionsInMenu(QStringLiteral("view")).contains(closePaneAction));
    check("close.command.shortcut-ctrl-w",
          closePaneAction != nullptr
          && closePaneAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+W")));
    {
        QSet<QString> seen;
        bool duplicate = false;
        int bindings = 0;
        for (const QString &id : shell->commands()->ids()) {
            QAction *action = shell->commands()->action(id);
            if (action == nullptr || action->shortcut().isEmpty()) {
                continue;
            }
            const QString key = action->shortcut().toString();
            if (seen.contains(key)) {
                duplicate = true;
            }
            seen.insert(key);
            ++bindings;
        }
        check("close.shortcuts.no-conflicts", !duplicate);
        check("close.shortcuts.binding-count", bindings >= 10);
    }

    EditorPane *splitPane = shell->editorArea()->splitActive(Qt::Horizontal);
    pump();
    check("close.split.pane-count", splitPane != nullptr && shell->editorArea()->paneCount() == 2);
    auto *dirtyEditor = qobject_cast<QPlainTextEdit *>(
        shell->editorArea()->activePane() != nullptr
            ? shell->editorArea()->activePane()->editorWidget()
            : nullptr);
    check("close.split.editor-editable", dirtyEditor != nullptr && !dirtyEditor->isReadOnly());
    const int beforeDirtyClose = shell->editorArea()->paneCount();
    if (dirtyEditor != nullptr) {
        dirtyEditor->document()->setModified(true);
    }
    closePaneAction->trigger();
    pump();
    check("close.unsaved.blocked",
          shell->editorArea()->paneCount() == beforeDirtyClose
              && shell->statusBar()->currentMessage().contains(QStringLiteral("unsaved"),
                                                               Qt::CaseInsensitive));
    if (dirtyEditor != nullptr) {
        dirtyEditor->document()->setModified(false);
    }

    // Reverse control: with the entry disabled the same trigger cannot close;
    // re-enabling it must close, so step 1 is not a no-op assertion.
    closePaneAction->setEnabled(false);
    const int beforeDisabled = shell->editorArea()->paneCount();
    closePaneAction->trigger();
    pump();
    check("close.reverse-control", shell->editorArea()->paneCount() == beforeDisabled);
    closePaneAction->setEnabled(true);

    closePaneAction->trigger();
    pump();
    check("close.unsaved-saved-close", shell->editorArea()->paneCount() == beforeDirtyClose - 1);
    check("close.remaining-editable",
          shell->editorArea()->activePane() != nullptr
              && shell->editorArea()->activePane()->editorWidget() != nullptr);

    while (shell->editorArea()->paneCount() > 1) {
        closePaneAction->trigger();
        pump();
    }
    check("close.last-single-view",
          shell->editorArea()->paneCount() == 1 && shell->isVisible());

    // Per-pane X button: splitting once more gives the new pane a button; one
    // click removes exactly that split and the window stays open.
    EditorPane *buttonPane = shell->editorArea()->splitActive(Qt::Horizontal);
    pump();
    check("close.button.split", buttonPane != nullptr && shell->editorArea()->paneCount() == 2);
    auto *button = buttonPane != nullptr
                       ? buttonPane->findChild<QToolButton *>(QStringLiteral("pane-close-button"))
                       : nullptr;
    check("close.button.present", button != nullptr);
    if (button != nullptr) {
        button->click();
    }
    pump();
    check("close.button.single-view",
          shell->editorArea()->paneCount() == 1 && shell->isVisible());

    // T137: File -> Open is a real command, and the shared openPaths() used by
    // positional arguments and the dialog validates before mutating anything.
    QAction *openAction = shell->commands()->action(QStringLiteral("file.open"));
    check("open.command.exists", openAction != nullptr);
    check("open.command.in-file-menu",
          openAction != nullptr
              && shell->commands()->actionsInMenu(QStringLiteral("file")).contains(openAction));
    check("open.command.shortcut-ctrl-o",
          openAction != nullptr
          && openAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+O")));

    const QString openA = temp.path() + QStringLiteral("/open-a.sv");
    const QString openB = temp.path() + QStringLiteral("/open-b.sv");
    const auto writeFixture = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write("module m;\n// 中文注释\nendmodule\n") > 0;
    };
    check("open.fixture-a", writeFixture(openA));
    check("open.fixture-b", writeFixture(openB));
    QString missingError;
    check("open.missing.rejected",
          !shell->openPaths({temp.path() + QStringLiteral("/does-not-exist.sv")}, &missingError)
              && missingError.contains(QStringLiteral("no such file")));
    check("open.missing.nonzero-readable", !missingError.isEmpty());

    shell->editorArea()->resetLayout();
    QString openError;
    check("open.multiple.ok", shell->openPaths({openA, openB}, &openError));
    check("open.multiple.pane-count", shell->editorArea()->paneCount() == 2);
    const auto openedPanes = shell->editorArea()->panes();
    check("open.multiple.paths",
          openedPanes.size() == 2
              && openedPanes.at(0)->currentFilePath() == QFileInfo(openA).absoluteFilePath()
              && openedPanes.at(1)->currentFilePath() == QFileInfo(openB).absoluteFilePath());
    check("open.multiple.active-second",
          shell->editorArea()->activePane() == openedPanes.at(1));
    shell->editorArea()->resetLayout();

    // File -> Open: exercise the real action/handler wiring; the headless seam
    // supplies the path instead of a modal QFileDialog.
    shell->setOpenFileDialogProvider([openA]() { return openA; });
    openAction->trigger();
    pump();
    check("open.dialog.action-opens",
          shell->editorArea()->activePane() != nullptr
              && shell->editorArea()->activePane()->currentFilePath()
                     == QFileInfo(openA).absoluteFilePath());

    // FIX 1 (no project): the title and the tab tooltip show the ABSOLUTE path.
    {
        const QString absoluteOpenA = QDir::cleanPath(QFileInfo(openA).absoluteFilePath());
        EditorPane *pathPane = shell->editorArea()->activePane();
        check("path.display.title.absolute-no-root",
              shell->windowTitle().contains(absoluteOpenA), shell->windowTitle());
        check("path.display.tab-tooltip.absolute-no-root",
              pathPane != nullptr && pathPane->tabBar() != nullptr
                  && pathPane->tabBar()->tabToolTip(pathPane->activeTabIndex()) == absoluteOpenA,
              pathPane != nullptr && pathPane->tabBar() != nullptr
                  ? pathPane->tabBar()->tabToolTip(pathPane->activeTabIndex())
                  : QStringLiteral("<no pane>"));
    }
    shell->setOpenFileDialogProvider(nullptr);
    shell->editorArea()->resetLayout();

    // --- FIX 2: ONE lane-agnostic signal for every successful open ----------
    // The project lane is not installed in this self-check (that wiring is
    // asserted by the project lane's own selftest against a real shell), so here
    // the contract of the app layer is asserted: the filter offers filelists,
    // and the signal fires for every open seam a panel uses.
    {
        const QString filter = vedit::app::openFileDialogFilter();
        // RM-16 (requirements-driven assertion change): the filelist set is the
        // one the parser recognises - the `f` extension, case-insensitively (see
        // src/common/file_dialog_filters.h and crates/vd-config/src/project.rs).
        // The old check demanded the GUESSED `*.flist` as well; that glob is gone
        // from every filter, and a guessed extension reappearing is a failure now.
        bool hasSharedFilelistGlobs = !filter.isEmpty();
        for (const QString &glob : vedit::common::filelistGlobs()) {
            hasSharedFilelistGlobs = hasSharedFilelistGlobs && filter.contains(glob);
        }
        check("open.filter.filelist", hasSharedFilelistGlobs, filter);
        check("open.filter.no-guessed-flist", !filter.contains(QStringLiteral("*.flist")),
              filter);
        check("open.filter.keeps-existing-types",
              filter.contains(QStringLiteral("*.v")) && filter.contains(QStringLiteral("*.sv"))
                  && filter.contains(QStringLiteral("*.svh"))
                  && filter.contains(QStringLiteral("*.vh")),
              filter);

        QStringList fileOpened;
        // The connection must not outlive this block: the lambda captures a
        // stack object. Explicit disconnect keeps that contract obvious.
        const QMetaObject::Connection fileOpenedConnection =
            QObject::connect(shell.get(), &ApplicationShell::fileOpened, shell.get(),
                             [&fileOpened](const QString &path) { fileOpened.append(path); });
        const QString absoluteA = QDir::cleanPath(QFileInfo(openA).absoluteFilePath());
        const QString absoluteB = QDir::cleanPath(QFileInfo(openB).absoluteFilePath());

        QString signalError;
        check("open.signal.opens", shell->openPaths({openA, openB}, &signalError), signalError);
        check("open.signal.one-per-open",
              fileOpened == QStringList{absoluteA, absoluteB},
              fileOpened.join(QLatin1Char(',')));

        fileOpened.clear();
        check("open.signal.failed-open-silent",
              !shell->openPaths({temp.path() + QStringLiteral("/does-not-exist.f")}, &signalError)
                  && fileOpened.isEmpty());

        // The panel/palette/search/jump surfaces call EditorPane::openFile
        // directly; that is the same seam, so it must fire the same signal.
        EditorPane *signalPane = shell->editorArea()->activePane();
        fileOpened.clear();
        check("open.signal.direct-pane-open",
              signalPane != nullptr && signalPane->openFile(absoluteA, &signalError)
                  && fileOpened == QStringList{absoluteA},
              fileOpened.join(QLatin1Char(',')));
        QObject::disconnect(fileOpenedConnection);
        shell->editorArea()->resetLayout();
    }

    // Persistence round-trip.
    shell->applyMixedSplitPreset();
    shell->panels()->showPanel(QStringLiteral("file-tree"), shell.get());
    shell->panels()->showPanel(QStringLiteral("search"), shell.get());
    pump();
    QStringList availableThemes = shell->themes()->availableThemes();
    check("theme.catalogue", availableThemes.size() >= 5);
    QString chosenTheme;
    for (const QString &name : availableThemes) {
        if (name != shell->themes()->currentTheme()) {
            chosenTheme = name;
            break;
        }
    }
    QString themeError;
    check("theme.apply", !chosenTheme.isEmpty() && shell->themes()->applyTheme(chosenTheme, &themeError));
    const QColor paletteBefore = shell->themes()->windowColor();
    QString invalidThemeError;
    const bool invalidApplied =
        shell->themes()->applyTheme(QStringLiteral("__definitely-not-a-theme__"), &invalidThemeError);
    check("theme.invalid.rejected", !invalidApplied && !invalidThemeError.isEmpty());
    check("theme.invalid.keeps-palette", shell->themes()->windowColor() == paletteBefore);

    shell->shortcutStore()->setShortcut(QStringLiteral("view.split.horizontal"),
                                        QKeySequence(QStringLiteral("Ctrl+Shift+J")));
    shell->commands()->refreshShortcuts();
    check("shortcut.override.applied",
          shell->commands()->action(QStringLiteral("view.split.horizontal"))->shortcut() ==
              QKeySequence(QStringLiteral("Ctrl+Shift+J")));

    const QJsonObject layoutBefore = shell->editorArea()->saveLayout();
    const int panesBefore = shell->editorArea()->paneCount();
    const QString activeBefore = layoutBefore.value(QStringLiteral("active")).toString();
    const QString themeBefore = shell->themes()->currentTheme();
    const bool fileTreeVisibleBefore = shell->panels()->isVisible(QStringLiteral("file-tree"));
    QString saveError;
    check("session.save", shell->saveSession(&saveError));

    ShellOptions restoredOptions = options;
    restoredOptions.restoreSession = true;
    auto restored = std::make_unique<ApplicationShell>(restoredOptions);
    restored->show();
    pump();
    check("restore.pane-count", restored->editorArea()->paneCount() == panesBefore);
    check("restore.layout-tree",
          sameJson(restored->editorArea()->saveLayout().value(QStringLiteral("root")).toObject(),
                   layoutBefore.value(QStringLiteral("root")).toObject()));
    check("restore.active-pane",
          restored->editorArea()->saveLayout().value(QStringLiteral("active")).toString() ==
              activeBefore);
    check("restore.theme", restored->themes()->currentTheme() == themeBefore);
    check("restore.panel-visible",
          restored->panels()->isVisible(QStringLiteral("file-tree")) == fileTreeVisibleBefore);
    check("restore.shortcut",
          restored->commands()->action(QStringLiteral("view.split.horizontal"))->shortcut() ==
              QKeySequence(QStringLiteral("Ctrl+Shift+J")));

    // --- a restored dock gets its lane content (review M1) ------------------
    // A session that lists the terminal and search panels is restored AFTER the
    // lanes registered their factories (the product sequence: deferSessionRestore
    // + startSession). Before that fix the restore ran during construction, when
    // both factories were still empty, and the docks kept the shell placeholder
    // forever: this check fails there and passes here.
    {
        const QString seamStateDir = temp.path() + QStringLiteral("/panel-seam");
        ShellOptions seamOptions;
        seamOptions.stateDir = seamStateDir;
        seamOptions.showDefaultPanels = false;
        seamOptions.restoreSession = false;
        auto seed = std::make_unique<ApplicationShell>(seamOptions);
        seed->panels()->showPanel(QStringLiteral("terminal"), seed.get());
        seed->panels()->showPanel(QStringLiteral("search"), seed.get());
        // A non-default editor layout as well: the deferred start must restore it
        // exactly like the constructor-time restore did.
        seed->editorArea()->splitActive(Qt::Horizontal);
        pump();
        QString seamSaveError;
        check("panel-seam.seed-session", seed->saveSession(&seamSaveError), seamSaveError);
        seed.reset();

        ShellOptions seamRestored = seamOptions;
        seamRestored.restoreSession = true;
        seamRestored.deferSessionRestore = true;
        auto seamShell = std::make_unique<ApplicationShell>(seamRestored);
        seamShell->show();
        pump();
        check("panel-seam.deferred-nothing-inserted", seamShell->panels()->insertedIds().isEmpty());
        // The factories are registered here, exactly as main() does, and only then
        // the session starts.
        seamShell->panels()->setFactory(QStringLiteral("terminal"),
                                        vedit::terminal::terminalPanelFactory());
        seamShell->panels()->setFactory(QStringLiteral("search"), []() -> QWidget * {
            auto *probe = new QLabel(QCoreApplication::translate("VEdit", "search panel from a registered factory"));
            probe->setObjectName(QStringLiteral("search-panel-probe"));
            return probe;
        });
        seamShell->startSession();
        pump();
        QDockWidget *terminalDock = seamShell->panels()->dock(QStringLiteral("terminal"));
        QDockWidget *searchDock = seamShell->panels()->dock(QStringLiteral("search"));
        check("panel-seam.dock-restored", terminalDock != nullptr && searchDock != nullptr);
        check("panel-seam.terminal-content",
              terminalDock != nullptr &&
                  qobject_cast<vedit::terminal::TerminalPanel *>(terminalDock->widget()) != nullptr);
        check("panel-seam.search-content",
              searchDock != nullptr && searchDock->widget() != nullptr &&
                  searchDock->widget()->objectName() == QStringLiteral("search-panel-probe"));
        check("panel-seam.no-placeholder",
              terminalDock != nullptr && terminalDock->widget() != nullptr &&
                  !terminalDock->widget()->objectName().startsWith(
                      QStringLiteral("panel-placeholder-")));
        check("panel-seam.editor-layout-restored", seamShell->editorArea()->paneCount() == 2,
              QStringLiteral("panes=%1").arg(seamShell->editorArea()->paneCount()));
    }

    // Bad configuration must never prevent startup.
    const QString brokenStateDir = temp.path() + QStringLiteral("/broken");
    writeFile(brokenStateDir + QStringLiteral("/session.json"), QByteArray("{not json at all"));
    writeFile(brokenStateDir + QStringLiteral("/shortcuts.json"), QByteArray("[also broken"));
    ShellOptions brokenOptions;
    brokenOptions.stateDir = brokenStateDir;
    brokenOptions.restoreSession = true;
    auto broken = std::make_unique<ApplicationShell>(brokenOptions);
    broken->show();
    pump();
    check("broken.config.survives", broken->editorArea()->paneCount() >= 1);
    check("broken.config.diagnostics", !broken->diagnostics().isEmpty());

    // Empty state stress: no project, panels toggled, panes closed repeatedly.
    ShellOptions emptyOptions;
    emptyOptions.stateDir = temp.path() + QStringLiteral("/empty");
    emptyOptions.restoreSession = false;
    auto empty = std::make_unique<ApplicationShell>(emptyOptions);
    empty->show();
    pump();
    check("stress.one-pane-initial", empty->editorArea()->paneCount() == 1);
    for (const QString &id : empty->panels()->ids()) {
        empty->panels()->toggle(id, empty.get());
    }
    pump();
    for (int i = 0; i < 5; ++i) {
        empty->editorArea()->closeActivePane();
    }
    pump();
    check("stress.pane-invariant", empty->editorArea()->paneCount() >= 1);
    check("stress.non-null-active", empty->editorArea()->activePane() != nullptr);

    // --- save entry: File > Save / Ctrl+S (task T71) ------------------------
    // Stop 1's acceptance edits a file and saves it. The entry must exist both in
    // the File menu and on Ctrl+S, and both paths must go through the ACTIVE
    // pane's EditorProvider seam (never a QPlainTextEdit cast in the shell), so
    // the installed provider decides how the document is written. The
    // KTextEditor provider is exercised further down; the demo provider first.
    QAction *saveAction = shell->commands()->action(QStringLiteral("file.save"));
    check("save.command.registered", saveAction != nullptr);
    check("save.shortcut.ctrl-s",
          saveAction != nullptr
              && saveAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+S")));
    bool saveInFileMenu = false;
    for (QAction *topLevelAction : shell->menuBar()->actions()) {
        QMenu *menu = topLevelAction != nullptr ? topLevelAction->menu() : nullptr;
        if (menu != nullptr && menu->actions().contains(saveAction)) {
            saveInFileMenu = true;
        }
    }
    check("save.menu.file-item", saveInFileMenu);

    const auto readFileBytes = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    };
    const auto leftoverSaveTemps = [](const QString &directory) {
        return QDir(directory).entryList(QStringList{QStringLiteral("*.vedit-tmp-*")},
                                         QDir::Files);
    };

#if defined(VEDIT_HAVE_EDIT_CORE)
    // The shell still runs the demo provider here: the same entry must save a
    // demo document too (the entry is provider-agnostic by construction).
    EditorPane *saveDemoPane = shell->editorArea()->activePane();
    if (saveDemoPane != nullptr && saveAction != nullptr) {
        const QString demoPath = temp.path() + QStringLiteral("/save-entry-demo.v");
        writeFile(demoPath, QByteArrayLiteral("module demo_before;\nendmodule\n"));
        QString demoError;
        check("save.demo.open", saveDemoPane->openFile(demoPath, &demoError));
        if (auto *demoEditor = qobject_cast<QPlainTextEdit *>(saveDemoPane->editorWidget())) {
            demoEditor->setPlainText(QStringLiteral("module demo_after;\nendmodule\n"));
        }
        saveAction->trigger();
        pump();
        check("save.demo.action.disk-bytes",
              readFileBytes(demoPath) == QByteArrayLiteral("module demo_after;\nendmodule\n"));
        check("save.demo.action.path-unchanged",
              saveDemoPane->currentFilePath() == QFileInfo(demoPath).absoluteFilePath());
        check("save.demo.action.no-temp-file",
              leftoverSaveTemps(QFileInfo(demoPath).absolutePath()).isEmpty());
    }

    // --- edit-core kernel in the product (Slice 1, AC-S1.1) ----------------
    // The provider is swapped on a live shell: every pane must get the real
    // KTextEditor widget, and the pane's document seam must open/save a file.
    shell->setEditorProvider(vedit::editor::makeEditCoreProvider());
    pump();
    check("editor.provider.id",
          shell->editorProvider() != nullptr
              && shell->editorProvider()->id() == QStringLiteral("ktexteditor"));
    EditorPane *editPane = shell->editorArea()->activePane();
    check("editor.pane.ktexteditor-widget",
          editPane != nullptr && vedit::editor::isEditCoreWidget(editPane->editorWidget()));

    // --- T80: exactly ONE authority for Ctrl+S ------------------------------
    // The user hit "Ambiguous shortcut overload" on the real machine: the shell
    // registered Ctrl+S for file.save while the KTextEditor view brought its own
    // standard save action. Two enabled Ctrl+S actions in one window make Qt
    // trigger neither, so the invariant is asserted directly.
    printCtrlSBindings(shell.get(), "after-provider-install");
    {
        const QList<QAction *> ctrlS = ctrlSActions(shell.get());
        check("save.shortcut.unique", ctrlS.size() == 1,
              QStringLiteral("bindings=%1").arg(ctrlS.size()));
        check("save.shortcut.authority", ctrlS.size() == 1 && ctrlS.first() == saveAction);
    }
    if (editPane != nullptr) {
        // --- T90/F-2: runtime re-application of an editor Ctrl+S binding ----
        // T80 clears the editor-side save shortcut when a pane is created, shown
        // or focused. A runtime re-apply (KDE system settings re-applying
        // KStandardShortcut, a provider repopulating its action collection) can
        // put a second Ctrl+S back afterwards. The EditorArea guard re-enforces
        // on activation/window-activation and on a slow timer; this simulates the
        // re-application by injecting the binding at runtime.
        QWidget *editorWidget = editPane->editorWidget();
        auto *injected = new QAction(QStringLiteral("injected-ctrl-s"), editorWidget);
        injected->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
        injected->setShortcutContext(Qt::WindowShortcut);
        editorWidget->addAction(injected);
        check("save.shortcut.runtime-duplicate-enumerated",
              ctrlSActions(shell.get()).size() == 2,
              QStringLiteral("bindings=%1").arg(ctrlSActions(shell.get()).size()));
        // One event-loop turn without an activation is the pre-T90 exposure: no
        // T80 trigger runs, so the duplicate stays and a Ctrl+S press would be
        // ambiguous again.
        pump();
        check("save.shortcut.runtime-unguarded-turn-keeps-duplicate",
              ctrlSActions(shell.get()).size() == 2,
              QStringLiteral("bindings=%1").arg(ctrlSActions(shell.get()).size()));
        QEvent activation(QEvent::ApplicationActivate);
        QCoreApplication::sendEvent(QCoreApplication::instance(), &activation);
        pump();
        check("save.shortcut.runtime-guard-clears",
              ctrlSActions(shell.get()).size() == 1 && hasCtrlSShortcut(saveAction)
                  && !hasCtrlSShortcut(injected),
              QStringLiteral("bindings=%1").arg(ctrlSActions(shell.get()).size()));
        check("save.shortcut.runtime-guard-authority",
              ctrlSActions(shell.get()).size() == 1
                  && ctrlSActions(shell.get()).first() == saveAction);
        // The slow timer calls the same entry point; assert the entry point and
        // that the timer is actually armed.
        auto *injectedAgain = new QAction(QStringLiteral("injected-ctrl-s-2"), editorWidget);
        injectedAgain->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
        editorWidget->addAction(injectedAgain);
        const int removed = shell->editorArea()->enforceSingleCtrlSAuthority();
        check("save.shortcut.runtime-guard-entry-point",
              removed >= 1 && ctrlSActions(shell.get()).size() == 1,
              QStringLiteral("removed=%1 bindings=%2")
                  .arg(removed)
                  .arg(ctrlSActions(shell.get()).size()));
        QTimer *shortcutGuard =
            shell->editorArea()->findChild<QTimer *>(QStringLiteral("ctrlSAuthorityGuard"));
        check("save.shortcut.runtime-guard-timer",
              shortcutGuard != nullptr && shortcutGuard->isActive());
        delete injected;
        delete injectedAgain;

        const QString paneFixture = temp.path() + QStringLiteral("/pane-open.v");
        writeFile(paneFixture, QByteArrayLiteral("module pane_open;\nendmodule\n"));
        QString paneError;
        const bool paneOpened = editPane->openFile(paneFixture, &paneError);
        check("editor.pane.open", paneOpened);
        if (!paneError.isEmpty()) {
            std::printf("EDITOR-PANE-ERROR %s\n", qPrintable(paneError));
        }
        check("editor.pane.open-path",
              editPane->currentFilePath() == QFileInfo(paneFixture).absoluteFilePath(),
              editPane->currentFilePath());
        check("editor.pane.save", editPane->saveFile(&paneError));

        // --- T71: Ctrl+S and File > Save on the KTextEditor provider -------
        // Both paths run end-to-end: the document text is changed, the entry is
        // invoked, and the bytes on disk must change while the pane keeps its
        // path and the edit-core lane's atomic save leaves no temporary file.
        KTextEditor::Document *document = vedit::editor::documentFor(editPane->editorWidget());
        check("save.ktexteditor.document", document != nullptr);
        const QString savePath = editPane->currentFilePath();
        if (document != nullptr && saveAction != nullptr) {
            // 1) Keyboard path: a real Ctrl+S key event sent to the shell's
            //    window goes through Qt's shortcut map to the QAction. T80: the
            //    editor view is FOCUSED first - that is the real user path, and
            //    it is exactly what exposes a duplicate binding (Qt then sees
            //    both the shell action and the editor view's own save action).
            document->setText(QStringLiteral("module saved_by_ctrl_s;\nendmodule\n"));
            document->setModified(true);
            shell->activateWindow();
            editPane->editorWidget()->setFocus(Qt::OtherFocusReason);
            pump();
            QWindow *shellWindow = shell->windowHandle();
            check("save.shortcut.window", shellWindow != nullptr);
            if (shellWindow != nullptr) {
                QKeyEvent press(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier);
                QKeyEvent release(QEvent::KeyRelease, Qt::Key_S, Qt::ControlModifier);
                QApplication::sendEvent(shellWindow, &press);
                QApplication::sendEvent(shellWindow, &release);
            }
            pump();
            check("save.shortcut.disk-bytes",
                  readFileBytes(savePath)
                      == QByteArrayLiteral("module saved_by_ctrl_s;\nendmodule\n"));
            check("save.shortcut.modified-cleared", !document->isModified());
            check("save.shortcut.path-unchanged", editPane->currentFilePath() == savePath);

            // 2) Menu path: the very QAction the File menu holds, triggered.
            document->setText(QStringLiteral("module saved_from_menu;\nendmodule\n"));
            document->setModified(true);
            saveAction->trigger();
            pump();
            check("save.menu.disk-bytes",
                  readFileBytes(savePath)
                      == QByteArrayLiteral("module saved_from_menu;\nendmodule\n"));
            check("save.menu.path-unchanged", editPane->currentFilePath() == savePath);
            check("save.no-temp-file",
                  leftoverSaveTemps(QFileInfo(savePath).absolutePath()).isEmpty());

            // 3) A failing save must be visible and diagnosable, never silent.
            //    atomic_save's documented test fault injection is used so the
            //    failure runs through the real KTextEditor/atomic path.
            document->setText(QStringLiteral("module must_not_reach_disk;\nendmodule\n"));
            document->setModified(true);
            const QByteArray beforeFailure = readFileBytes(savePath);
            qputenv("VEDIT_ATOMIC_FAULT", QByteArrayLiteral("write"));
            saveAction->trigger();
            qunsetenv("VEDIT_ATOMIC_FAULT");
            pump();
            check("save.failure.disk-unchanged", readFileBytes(savePath) == beforeFailure);
            check("save.failure.status-bar",
                  shell->statusBar()->currentMessage().contains(QCoreApplication::translate("VEdit", "Save failed")));
            bool failureDiagnostic = false;
            for (const QString &diagnostic : shell->diagnostics()) {
                if (diagnostic.contains(QCoreApplication::translate("VEdit", "Save failed"))) {
                    failureDiagnostic = true;
                }
            }
            check("save.failure.diagnostic", failureDiagnostic);
            check("save.failure.still-modified", document->isModified());
            // 4) ... and the same entry saves again once the fault is gone.
            saveAction->trigger();
            pump();
            check("save.recovery.disk-bytes",
                  readFileBytes(savePath)
                      == QByteArrayLiteral("module must_not_reach_disk;\nendmodule\n"));
            check("save.recovery.not-modified", !document->isModified());
        }
    }

    // T80: a split creates ANOTHER editor view, so the "exactly one Ctrl+S"
    // invariant must cover every pane, not only the first one. This is what the
    // per-widget cleanup in EditorPane::replaceEditorWidget guarantees.
    {
        EditorPane *extraPane = shell->editorArea()->splitActive(Qt::Horizontal);
        pump();
        const QList<QAction *> ctrlS = ctrlSActions(shell.get());
        printCtrlSBindings(shell.get(), "after-split");
        check("save.shortcut.unique.multi-pane", ctrlS.size() == 1,
              QStringLiteral("bindings=%1").arg(ctrlS.size()));
        if (extraPane != nullptr) {
            shell->editorArea()->closeActivePane();
            pump();
        }
    }

    // --- T94/T128: breadcrumb wired; folding is KDE-NATIVE (no injection) ----
    // T127's xcb experiment proved the gutter marker folds/unfolds in both
    // directions with our build; VEdit therefore injects nothing, and a file
    // opens with no auto-folds. These checks lock the new contract: no automatic
    // folding, breadcrumb intact, and the same-pane second open + cursor moves
    // (the T117/T121 crash triggers) are safe.
    {
        shell->resize(1500, 1200);
        pump();
        EditorPane *foldPane = shell->editorArea()->splitActive(Qt::Horizontal);
        pump();
        check("fold.native.pane", foldPane != nullptr);
        if (foldPane != nullptr) {
            const QString foldPath = temp.path() + QStringLiteral("/fold-1000.sv");
            writeFile(foldPath, largeSystemVerilogSample());
            QString foldError;
            check("fold.native.open", foldPane->openFile(foldPath, &foldError));
            pump();
            KTextEditor::View *foldView = vedit::editor::viewFor(foldPane->editorWidget());
            KTextEditor::Document *foldDocument =
                vedit::editor::documentFor(foldPane->editorWidget());
            check("fold.native.view", foldView != nullptr && foldDocument != nullptr);
            check("fold.native.highlight-mode",
                  foldDocument != nullptr
                      && foldDocument->highlightingMode() == QStringLiteral("SystemVerilog"),
                  foldDocument != nullptr ? foldDocument->highlightingMode() : QString());
            auto *binding =
                vedit::editor::FoldBreadcrumbBinding::instanceFor(foldPane->editorWidget());
            check("fold.native.binding", binding != nullptr);
            if (binding != nullptr && foldView != nullptr) {
                // No VEdit-side folding: the view must not hide anything on open.
                const int firstReal = foldView->firstDisplayedLine(KTextEditor::View::RealLine);
                const int lastReal = foldView->lastDisplayedLine(KTextEditor::View::RealLine);
                const int firstVisible =
                    foldView->firstDisplayedLine(KTextEditor::View::VisibleLine);
                const int lastVisible =
                    foldView->lastDisplayedLine(KTextEditor::View::VisibleLine);
                const int hidden = (lastReal - firstReal) - (lastVisible - firstVisible);
                check("fold.native.no-auto-folds", hidden == 0,
                      QStringLiteral("hidden=%1").arg(hidden));

                // Breadcrumb still follows the cursor into the function body.
                foldView->setCursorPosition(KTextEditor::Cursor(7, 4));
                pump();
                std::printf("FOLD-NATIVE breadcrumb describe='%s' bar='%s'\n",
                            qPrintable(binding->breadcrumbDescribe()),
                            qPrintable(foldPane->breadcrumbText()));
                check("fold.native.breadcrumb.describe",
                      binding->breadcrumbDescribe()
                          == QStringLiteral("module:m0 > function:inc"),
                      binding->breadcrumbDescribe());
                check("fold.native.breadcrumb.pane-bar",
                      foldPane->breadcrumbText().contains(QStringLiteral("m0 > inc")),
                      foldPane->breadcrumbText());

                binding->setTreeSitterEnabled(false);
                pump();
                check("fold.native.breadcrumb.no-invention",
                      binding->breadcrumbDescribe().isEmpty()
                          && foldPane->breadcrumbText().isEmpty(),
                      QStringLiteral("describe='%1' bar='%2'")
                          .arg(binding->breadcrumbDescribe(), foldPane->breadcrumbText()));
                binding->setTreeSitterEnabled(true);
                pump();

                // T117/T121 triggers: move the cursor across the document, then
                // open a SECOND file in the same pane. With no injected folds
                // there is no hidden-line cursor move and no fold state to carry
                // across setText().
                for (int line = 0; line < foldDocument->lines(); line += 137) {
                    foldView->setCursorPosition(KTextEditor::Cursor(line, 0));
                    pump();
                }
                const QString secondPath = temp.path() + QStringLiteral("/fold-second.sv");
                writeFile(secondPath, largeSystemVerilogSample());
                check("fold.native.second-open", foldPane->openFile(secondPath, &foldError),
                      foldError);
                pump();
                KTextEditor::View *secondView = vedit::editor::viewFor(foldPane->editorWidget());
                check("fold.native.second-view", secondView != nullptr);
                if (secondView != nullptr) {
                    secondView->setCursorPosition(
                        KTextEditor::Cursor(secondView->document()->lines() - 1, 0));
                    pump();
                    check("fold.native.second-cursor", secondView->cursorPosition().line() >= 0);
                }
            }
            shell->editorArea()->closeActivePane();
            pump();
        }
    }

    vedit::editor::BoundaryHooks hooks;
    hooks.calls = []() { return vedit::editor::crossBoundaryCallCount(); };
#if defined(VEDIT_HAVE_RUST_FFI)
    hooks.positiveControl = []() {
        noteAbiCall();
        (void)vd_abi_version();
    };
#endif
    const vedit::editor::SelfCheckResult editCore = vedit::editor::runSelfChecks(hooks);
    g_checks += editCore.checks;
    g_failures += editCore.failures;
#endif

    // --- AC-1.1/AC-1.2 (batch 1): the ONE workspace root --------------------
    // The shell resolves the persisted session root in its CONSTRUCTOR, i.e.
    // before any lane registers a panel factory (main.cpp installs the lanes
    // after the shell exists), and a runtime switch is broadcast through
    // projectRootChanged. These are shell-level assertions only: the panel
    // propagation itself is proven by the real-shell probe in
    // evidence/batch1-shell/, never by --selfcheck (which returns before
    // installPanels and can therefore never see a panel).
    {
        const QString rootStateDir = temp.path() + QStringLiteral("/root-restore");
        const QString projectA = temp.path() + QStringLiteral("/project-a");
        const QString projectB = temp.path() + QStringLiteral("/project-b");
        QDir().mkpath(projectA);
        QDir().mkpath(projectB);
        QJsonObject archive;
        archive.insert(QStringLiteral("version"), 2);
        archive.insert(QStringLiteral("project"), projectA);
        writeFile(rootStateDir + QStringLiteral("/session.json"),
                  QJsonDocument(archive).toJson(QJsonDocument::Compact));

        // 1) An ordinary start (no --project) knows the root BEFORE the lanes
        //    install: projectRoot() already carries the persisted value.
        ShellOptions rootOptions;
        rootOptions.stateDir = rootStateDir;
        rootOptions.restoreSession = true;
        rootOptions.deferSessionRestore = true;
        rootOptions.showDefaultPanels = false;
        auto rootShell = std::make_unique<ApplicationShell>(rootOptions);
        check("root.restored-before-install", rootShell->projectRoot() == projectA,
              rootShell->projectRoot());
        // An explicit --project still wins over the archive.
        ShellOptions explicitOptions = rootOptions;
        explicitOptions.stateDir = temp.path() + QStringLiteral("/root-explicit");
        explicitOptions.projectRoot = projectB;
        auto explicitShell = std::make_unique<ApplicationShell>(explicitOptions);
        check("root.explicit-wins", explicitShell->projectRoot() == projectB,
              explicitShell->projectRoot());

        // FIX 1: the SAME display rule on the rendered surfaces once a root
        // exists. The open key stays absolute; only the rendering changes.
        const QString underRoot = projectA + QStringLiteral("/rtl/foo.sv");
        const QString outsideRoot = temp.path() + QStringLiteral("/outside/other.sv");
        const auto absoluteOf = [](const QString &value) {
            return QDir::cleanPath(QFileInfo(value).absoluteFilePath());
        };
        writeFile(underRoot, QByteArray("module foo; endmodule\n"));
        writeFile(outsideRoot, QByteArray("module other; endmodule\n"));
        rootShell->show();
        pump();
        QString displayError;
        check("path.display.under-root.open", rootShell->openPaths({underRoot}, &displayError),
              displayError);
        pump();
        check("path.display.window-title.relative",
              rootShell->windowTitle().contains(QStringLiteral("rtl/foo.sv"))
                  && !rootShell->windowTitle().contains(projectA),
              rootShell->windowTitle());
        EditorPane *displayPane = rootShell->editorArea()->activePane();
        check("path.display.tab-tooltip.relative",
              displayPane != nullptr && displayPane->tabBar() != nullptr
                  && displayPane->tabBar()->tabToolTip(displayPane->activeTabIndex())
                         == QStringLiteral("rtl/foo.sv"),
              displayPane != nullptr && displayPane->tabBar() != nullptr
                  ? displayPane->tabBar()->tabToolTip(displayPane->activeTabIndex())
                  : QStringLiteral("<no pane>"));
        check("path.display.open-key-still-absolute",
              displayPane != nullptr && displayPane->currentFilePath() == absoluteOf(underRoot),
              displayPane != nullptr ? displayPane->currentFilePath() : QStringLiteral("<no pane>"));
        check("path.display.outside-root.open", rootShell->openPaths({outsideRoot}, &displayError),
              displayError);
        pump();
        check("path.display.window-title.outside-absolute",
              rootShell->windowTitle().contains(absoluteOf(outsideRoot)),
              rootShell->windowTitle());
        displayPane = rootShell->editorArea()->activePane();
        check("path.display.tab-tooltip.outside-absolute",
              displayPane != nullptr && displayPane->tabBar() != nullptr
                  && displayPane->tabBar()->tabToolTip(displayPane->activeTabIndex())
                         == absoluteOf(outsideRoot),
              displayPane != nullptr && displayPane->tabBar() != nullptr
                  ? displayPane->tabBar()->tabToolTip(displayPane->activeTabIndex())
                  : QStringLiteral("<no pane>"));

        // 2) A runtime switch is exactly ONE broadcast, and the new root is what
        //    the status bar/title and the next archive carry.
        QStringList broadcast;
        QObject::connect(rootShell.get(), &ApplicationShell::projectRootChanged,
                         rootShell.get(),
                         [&broadcast](const QString &root) { broadcast.append(root); });
        rootShell->show();
        pump();
        rootShell->setProjectRoot(projectB);
        pump();
        check("root.switch-broadcast", broadcast == QStringList{projectB},
              broadcast.join(QLatin1Char(',')));
        check("root.switch-value", rootShell->projectRoot() == projectB,
              rootShell->projectRoot());
        check("root.switch-label", rootShell->projectLabel() == QFileInfo(projectB).fileName(),
              rootShell->projectLabel());
        // The same root again is not a second event: a panel that re-reports the
        // root it was just given must not loop the broadcast.
        rootShell->setProjectRoot(projectB);
        pump();
        check("root.switch-idempotent", broadcast.size() == 1,
              QStringLiteral("events=%1").arg(broadcast.size()));

        // 3) File > Open Folder is the user entry point and goes through the SAME
        //    path. Only the modal dialog is replaced by the probe seam.
        QAction *openFolder = rootShell->commands()->action(QStringLiteral("file.open-folder"));
        check("root.open-folder-command", openFolder != nullptr);
        bool openFolderInFileMenu = false;
        for (QAction *topLevel : rootShell->menuBar()->actions()) {
            QMenu *menu = topLevel != nullptr ? topLevel->menu() : nullptr;
            if (menu != nullptr && menu->actions().contains(openFolder)) {
                openFolderInFileMenu = true;
            }
        }
        check("root.open-folder-menu", openFolderInFileMenu);
        rootShell->setOpenFolderDialogProvider([projectA]() { return projectA; });
        if (openFolder != nullptr) {
            openFolder->trigger();
        }
        pump();
        check("root.open-folder-switches", rootShell->projectRoot() == projectA,
              rootShell->projectRoot());
        check("root.open-folder-broadcast",
              broadcast.size() == 2 && broadcast.last() == projectA,
              QStringLiteral("events=%1").arg(broadcast.size()));
        // A cancelled dialog changes nothing at all (no write, no broadcast).
        rootShell->setOpenFolderDialogProvider([]() { return QString(); });
        if (openFolder != nullptr) {
            openFolder->trigger();
        }
        pump();
        check("root.open-folder-cancel",
              rootShell->projectRoot() == projectA && broadcast.size() == 2,
              QStringLiteral("events=%1").arg(broadcast.size()));

        // 4) The switched root is what the next session archive persists, so the
        //    next ordinary start comes back to it.
        QString rootSaveError;
        check("root.switch.persisted", rootShell->saveSession(&rootSaveError), rootSaveError);
        QFile rootArchiveFile(rootStateDir + QStringLiteral("/session.json"));
        QJsonObject rootArchiveJson;
        if (rootArchiveFile.open(QIODevice::ReadOnly)) {
            rootArchiveJson = QJsonDocument::fromJson(rootArchiveFile.readAll()).object();
        }
        check("root.switch.persisted-value",
              rootArchiveJson.value(QStringLiteral("project")).toString() == projectA);
    }

    std::printf("APP-SHELL-SELFTEST ok=%d checks=%d failures=%d state=%s\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures, qPrintable(stateDir));
    return g_failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Rust service layer (C ABI) self-check — Slice 0
// ---------------------------------------------------------------------------
//
// This runs the C side of the vertical slice: it calls the generated header from
// a real C++ caller linked against libvd_ffi.a, and asserts the project/.f
// expansion, the FIM default-off state, VCS backend detection and a settings
// round-trip. Every call is start-then-poll; nothing here waits on the ABI.
namespace ffi {

#ifdef VEDIT_HAVE_RUST_FFI

QString errorText(VdService *service)
{
    VdError error{};
    if (vd_service_error(service, &error) != 1) {
        return QString();
    }
    return QString::fromUtf8(error.message);
}

QStringList stringList(const VdStringList &list)
{
    QStringList values;
    for (size_t index = 0; index < list.count; ++index) {
        values.append(QString::fromUtf8(list.items[index]));
    }
    return values;
}

int runSelfCheck(const QString &fixtureDir)
{
    std::printf("FFI-ABI fixture=%s\n", qPrintable(fixtureDir));

    // T97: this selfcheck asserts the exact file set of the Rust C ABI smoke
    // fixture (crates/vd-ffi/tests/fixtures/project: vedit.toml + 3 rtl files,
    // +incdir+inc, WIDTH=8/SIM). The T24 review passed the repository's
    // project-model fixture (tests/fixtures/verilog-project: 7 files and its own
    // filelist semantics) and got four ffi.project.* failures that looked like a
    // project-model regression. Refuse the wrong fixture explicitly, with the
    // correct command; on the right fixture a real regression still fails those
    // same four checks (nothing here filters their result).
    const QFileInfo fixtureInfo(fixtureDir);
    const QString manifest = fixtureInfo.absoluteFilePath() + QStringLiteral("/vedit.toml");
    if (!fixtureInfo.exists() || !fixtureInfo.isDir() || !QFileInfo(manifest).isFile()) {
        std::printf("FFI-ABI-SELFTEST ok=0 reason=wrong-fixture\n");
        std::printf("FFI-ABI hint: --selfcheck-ffi expects the Rust C ABI smoke fixture:\n"
                    "FFI-ABI hint:   crates/vd-ffi/tests/fixtures/project  (vedit.toml + 3 rtl files)\n"
                    "FFI-ABI hint: tests/fixtures/verilog-project is the PROJECT-MODEL fixture (T14,\n"
                    "FFI-ABI hint:   7 files); its expectations live in expected/ and are validated by:\n"
                    "FFI-ABI hint:   cargo test -p vd-ffi -p vd-config\n");
        return 2;
    }
    const QByteArray fixturePath = QFile::encodeName(fixtureDir);

    noteAbiCall();
    VdService *service = vd_service_create();
    check("ffi.service.create", service != nullptr);
    if (service == nullptr) {
        std::printf("FFI-ABI-SELFTEST ok=0 checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    noteAbiCall();
    check("ffi.abi-version", vd_abi_version() == VD_ABI_VERSION);

    // 1/7 project model + .f. The start call must return immediately (C2).
    QElapsedTimer startTimer;
    startTimer.start();
    noteAbiCall();
    const quint64 projectJob = vd_project_parse(service, fixturePath.constData());
    const qint64 startMs = startTimer.elapsed();
    check("ffi.project.start.returns-job", projectJob != 0);
    check("ffi.project.start.non-blocking", startMs < 200);
    std::printf("FFI-TIMING project-start-ms=%lld\n", static_cast<long long>(startMs));

    VdProjectInfo project{};
    QElapsedTimer projectTimer;
    projectTimer.start();
    qint32 projectStatus = VD_STATUS_PENDING;
    while (projectTimer.elapsed() < 20000 &&
           (projectStatus = vd_project_poll(service, projectJob, &project)) == VD_STATUS_PENDING) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    const bool projectReady = projectStatus == VD_STATUS_READY;
    check("ffi.project.ready", projectReady);
    if (!projectReady) {
        std::printf("FFI-ERROR project: %s\n", qPrintable(errorText(service)));
    }
    const QStringList files = stringList(project.files);
    check("ffi.project.file-count", files.size() == 3,
          QStringLiteral("actual=%1 files=[%2] (C ABI fixture expects 3)")
              .arg(files.size())
              .arg(files.join(QLatin1Char(','))));
    QStringList basenames;
    for (const QString &path : files) {
        basenames.append(QFileInfo(path).fileName());
    }
    check("ffi.project.file-order",
          basenames == QStringList{QStringLiteral("rtl_a.sv"), QStringLiteral("rtl_b.sv"),
                                   QStringLiteral("rtl_c.sv")},
          QStringLiteral("actual=[%1]").arg(basenames.join(QLatin1Char(','))));
    const QStringList includeDirs = stringList(project.include_dirs);
    check("ffi.project.include-dir",
          includeDirs.size() == 1 && includeDirs.first().endsWith(QStringLiteral("inc")),
          QStringLiteral("actual=[%1]").arg(includeDirs.join(QLatin1Char(','))));
    const QStringList defines = stringList(project.defines);
    check("ffi.project.defines",
          defines.contains(QStringLiteral("WIDTH=8")) && defines.contains(QStringLiteral("SIM")),
          QStringLiteral("actual=[%1]").arg(defines.join(QLatin1Char(','))));
    noteAbiCall();
    vd_project_info_free(&project);

    // Unknown job ids are rejected instead of silently accepted.
    VdProjectInfo rejected{};
    noteAbiCall();
    check("ffi.project.unknown-job-rejected", vd_project_poll(service, 424242, &rejected) == -1);

    // 4/7 FIM: the product default is off, and the call makes no request.
    noteAbiCall();
    const quint64 fimJob = vd_fim_complete(service, "module m;", "");
    check("ffi.fim.start", fimJob != 0);
    VdFimResult fim{};
    QElapsedTimer fimTimer;
    fimTimer.start();
    qint32 fimStatus = VD_STATUS_PENDING;
    while (fimTimer.elapsed() < 5000 &&
           (fimStatus = vd_fim_poll(service, fimJob, &fim)) == VD_STATUS_PENDING) {
        QThread::msleep(1);
    }
    check("ffi.fim.disabled-by-default", fimStatus == VD_STATUS_READY && fim.state == 0);
    noteAbiCall();
    vd_fim_result_free(&fim);

    // 6/7 VCS: the fixture lives inside this repository, so Git is detected.
    noteAbiCall();
    const quint64 vcsJob = vd_vcs_status(service, fixturePath.constData());
    check("ffi.vcs.start", vcsJob != 0);
    VdVcsStatus vcs{};
    QElapsedTimer vcsTimer;
    vcsTimer.start();
    qint32 vcsStatus = VD_STATUS_PENDING;
    while (vcsTimer.elapsed() < 10000 &&
           (vcsStatus = vd_vcs_poll(service, vcsJob, &vcs)) == VD_STATUS_PENDING) {
        QThread::msleep(1);
    }
    check("ffi.vcs.backend-detected", vcsStatus == VD_STATUS_READY && vcs.backend == 1);
    noteAbiCall();
    vd_vcs_status_free(&vcs);

    // 7/7 settings: write and read back through the ABI.
    QTemporaryDir stateDir;
    check("ffi.settings.temp-dir", stateDir.isValid());
    const QByteArray settingsPath =
        QFile::encodeName(stateDir.path() + QStringLiteral("/settings.toml"));
    const QByteArray settingKey = QByteArrayLiteral("theme");
    const QByteArray settingValue = QByteArrayLiteral("Breeze Dark");
    const VdSettingInput setting{settingKey.constData(), settingValue.constData()};
    noteAbiCall();
    const quint64 saveJob = vd_settings_save(service, settingsPath.constData(), &setting, 1);
    check("ffi.settings.save.start", saveJob != 0);
    VdSettings saved{};
    QElapsedTimer settingsTimer;
    settingsTimer.start();
    qint32 saveStatus = VD_STATUS_PENDING;
    while (settingsTimer.elapsed() < 10000 &&
           (saveStatus = vd_settings_poll(service, saveJob, &saved)) == VD_STATUS_PENDING) {
        QThread::msleep(1);
    }
    check("ffi.settings.save.ready", saveStatus == VD_STATUS_READY);
    noteAbiCall();
    vd_settings_free(&saved);

    noteAbiCall();
    const quint64 loadJob = vd_settings_load(service, settingsPath.constData());
    check("ffi.settings.load.start", loadJob != 0);
    VdSettings loaded{};
    settingsTimer.restart();
    qint32 loadStatus = VD_STATUS_PENDING;
    while (settingsTimer.elapsed() < 10000 &&
           (loadStatus = vd_settings_poll(service, loadJob, &loaded)) == VD_STATUS_PENDING) {
        QThread::msleep(1);
    }
    check("ffi.settings.load.ready", loadStatus == VD_STATUS_READY && loaded.count == 1);
    if (loaded.count == 1) {
        check("ffi.settings.round-trip",
              QString::fromUtf8(loaded.items[0].key) == QStringLiteral("theme") &&
                  QString::fromUtf8(loaded.items[0].value) == QStringLiteral("Breeze Dark"));
    } else {
        check("ffi.settings.round-trip", false);
    }
    noteAbiCall();
    vd_settings_free(&loaded);

    noteAbiCall();
    vd_service_destroy(service);
    std::printf("FFI-ABI-SELFTEST ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#else // VEDIT_HAVE_RUST_FFI

int runSelfCheck(const QString &)
{
    std::printf("FFI-ABI-SELFTEST ok=0 reason=built-without-rust-ffi\n");
    return 1;
}

#endif // VEDIT_HAVE_RUST_FFI

} // namespace ffi

// ---------------------------------------------------------------------------
// screenshots
// ---------------------------------------------------------------------------

int captureScreenshots(const QString &directory)
{
    if (!QDir().mkpath(directory)) {
        std::printf("SCREENSHOT-FAIL cannot create %s\n", qPrintable(directory));
        return 1;
    }
    ShellOptions options;
    options.stateDir = directory + QStringLiteral("/state");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.projectRoot = QString();
    ApplicationShell shell(options);
    shell.resize(1440, 900);
    shell.show();
    pump();

    const auto save = [&shell, &directory](const QString &name) {
        const QString path = directory + QLatin1Char('/') + name;
        const bool ok = shell.grab().save(path);
        std::printf("SCREENSHOT %s %s\n", ok ? "OK" : "FAIL", qPrintable(path));
        return ok;
    };

    bool ok = true;
    // Two split states, each with the two default docks visible.
    shell.panels()->hidePanel(QStringLiteral("search"));
    shell.panels()->hidePanel(QStringLiteral("terminal"));
    shell.panels()->hidePanel(QStringLiteral("vcs"));
    shell.panels()->hidePanel(QStringLiteral("settings"));
    shell.panels()->showPanel(QStringLiteral("file-tree"), &shell);
    shell.panels()->showPanel(QStringLiteral("outline"), &shell);

    shell.applyHorizontalSplitPreset(3);
    pump();
    ok = save(QStringLiteral("10-horizontal-split.png")) && ok;

    shell.applyVerticalSplitPreset(3);
    pump();
    ok = save(QStringLiteral("11-vertical-split.png")) && ok;

    // Panels and docks, all six on screen.
    shell.applyHorizontalSplitPreset(2);
    shell.insertAllPanelsForDemo();
    pump();
    ok = save(QStringLiteral("12-panels-and-docks.png")) && ok;

    // Empty state: no project, no panel content, one pane.
    for (const QString &id : shell.panels()->ids()) {
        shell.panels()->hidePanel(id);
    }
    shell.editorArea()->resetLayout();
    pump();
    ok = save(QStringLiteral("13-empty-state-no-project.png")) && ok;

    std::printf("APP-SHELL-SCREENSHOTS ok=%d dir=%s\n", ok ? 1 : 0, qPrintable(directory));
    return ok ? 0 : 1;
}

} // namespace

namespace {

#if defined(VEDIT_HAVE_APPEARANCE_SETTINGS)
// UX batch 2 / A4: `--state-dir` must be known BEFORE the QApplication is
// constructed (the interface scale is applied through the environment at that
// moment), but QCommandLineParser itself needs the application object. This is
// the smallest possible pre-scan: exactly the one option, no validation, no
// side effects. The parser later reads the same value the normal way.
QString stateDirArgument(int argc, char **argv)
{
    static constexpr char kOption[] = "--state-dir";
    constexpr int kOptionLength = sizeof(kOption) - 1;
    for (int index = 1; index < argc; ++index) {
        if (std::strncmp(argv[index], kOption, kOptionLength) == 0) {
            if (argv[index][kOptionLength] == '\0') {
                return index + 1 < argc ? QString::fromLocal8Bit(argv[index + 1]) : QString();
            }
            if (argv[index][kOptionLength] == '=') {
                return QString::fromLocal8Bit(argv[index] + kOptionLength + 1);
            }
        }
    }
    return QString();
}

// UX batch 2 / A4: where the STARTUP appearance read looks for settings.toml.
// With `--state-dir` the answer is exact. Without it, the store lives at Qt's
// AppConfigLocation, which is only computable AFTER the QApplication exists (it
// embeds the organization/application names main() sets at that point) — so the
// same location is spelled out here for the pre-QApplication read, and the first
// EXISTING candidate wins. An ordinary start (no settings file anywhere) simply
// keeps the defaults, exactly like the store's own "missing file" rule.
QStringList startupSettingsCandidates(const QString &stateDir)
{
    QStringList candidates;
    candidates.append(vedit::app::i18n::settingsFilePath(stateDir));
    if (stateDir.isEmpty()) {
        const QString configHome = qEnvironmentVariable(
            "XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config"));
        candidates.append(configHome
                          + QStringLiteral("/VEdit/vedit-app/settings.toml"));
    }
    return candidates;
}

QString existingSettingsPath(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.value(0);
}
#endif // VEDIT_HAVE_APPEARANCE_SETTINGS

} // namespace

int main(int argc, char **argv)
{
    // T134: Rust is STATICALLY LINKED into this C++ host (crates/vd-lsp drives
    // the verible/verible-verilog-ls child through tokio). The Rust runtime's
    // startup — the code that calls `signal(SIGPIPE, SIG_IGN)` in a Rust binary
    // — never runs here, so a write to a dead child's stdin (e.g. while the
    // user switches files and the LSP session is being torn down) would raise
    // SIGPIPE and kill the whole GUI with "Broken pipe" before any error path
    // runs (and before the crash reporter: SIGPIPE is not one of the signals it
    // handles). Ignore it FIRST, before Qt / Rust / any child process starts;
    // the write then fails with EPIPE, which the Rust side records as a state.
    // Do not add SA_RESETHAND: the ignore must stay for the whole process.
    std::signal(SIGPIPE, SIG_IGN);

    // UX batch 2 / A4: the interface scale is a RESTART switch — Qt reads
    // QT_SCALE_FACTOR once, while the QApplication is being constructed — so the
    // value has to be in the environment BEFORE the next statement. 100 % (the
    // default) sets nothing and leaves an ambient HiDPI value alone.
#if defined(VEDIT_HAVE_APPEARANCE_SETTINGS)
    const QString startupSettingsPath =
        existingSettingsPath(startupSettingsCandidates(stateDirArgument(argc, argv)));
    vedit::workspace::settings::ui_scale::applyScaleEnvironment(
        vedit::workspace::settings::ui_scale::readValues(startupSettingsPath).scalePercent);
#endif

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-app"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("VEdit"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("VEdit", "VEdit application shell (Qt6/KTextEditor rewrite)."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("File(s) to open (each additional file gets a split)."),
                                 QStringLiteral("[file...]"));
    parser.addOption({QStringLiteral("state-dir"),
                      QCoreApplication::translate("VEdit", "Directory for session.json and shortcuts.json."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("project"), QStringLiteral("Project root to display (optional)."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("theme"), QCoreApplication::translate("VEdit", "KSyntaxHighlighting theme name."),
                      QStringLiteral("name")});
    parser.addOption({QStringLiteral("selfcheck"),
                      QStringLiteral("Run non-interactive layout/theme/persistence checks and exit.")});
    // UX batch 2 / A4: print the startup appearance line and exit, so a test can
    // assert the RESTART path of `ui.scale_percent` (the QT_SCALE_FACTOR the next
    // start really has) without running the whole shell self-check.
    parser.addOption({QStringLiteral("ui-scale-echo"),
                      QStringLiteral("Print the resolved UI-SCALE line and exit. Test-only.")});
    parser.addOption({QStringLiteral("selfcheck-ffi"),
                      QStringLiteral("Run the Rust C ABI checks against the vd-ffi smoke fixture "
                                     "(crates/vd-ffi/tests/fixtures/project) and exit."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("screenshots"),
                      QCoreApplication::translate("VEdit", "Capture split-state screenshots into this directory and exit."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("no-session-restore"),
                      QCoreApplication::translate("VEdit", "Ignore an existing session file at startup.")});
    parser.addOption({QStringLiteral("hold"), QCoreApplication::translate("VEdit", "Keep the window for this many ms."),
                      QStringLiteral("ms"), QStringLiteral("0")});
    // T117: proves the crash reporter works on the SHIPPED binary (the ctest
    // `vedit-app-crash-report-shipped` runs exactly this). It raises before any
    // window or display is needed.
    parser.addOption({QStringLiteral("crash-test"),
                      QStringLiteral("Raise a signal after installing the crash reporter "
                                     "(segv|abort|bus|fpe|ill) and exit. Test-only."),
                      QStringLiteral("signal")});
    parser.process(app);

#if defined(VEDIT_HAVE_APPEARANCE_SETTINGS)
    // UX batch 2 / A4, part 2: the font applies to the running application, and
    // the format/lint configuration has to be in the process-wide holders before
    // the shell can trigger a format or a project lint. Both values come from the
    // SAME store the Preferences page writes (`<stateDir>/settings.toml`).
    {
        namespace settings = vedit::workspace::settings;
        const QString settingsPath = existingSettingsPath(
            startupSettingsCandidates(parser.value(QStringLiteral("state-dir"))));
        settings::ui_scale::rememberStartupFont();
        settings::ui_scale::applyFontPointSize(
            settings::ui_scale::readValues(settingsPath).fontPointSize);
        settings::SettingsClient client(settingsPath);
        settings::Snapshot snapshot;
        bool delivered = false;
        client.load([&snapshot, &delivered](settings::Snapshot loaded) {
            snapshot = std::move(loaded);
            delivered = true;
        });
        // flush() pumps the store job; it is the same wait a restart-free
        // Preferences reload does. A store that does not answer leaves the
        // defaults in place — startup never fails over settings.
        if (client.flush() && delivered && snapshot.completed) {
            settings::setCurrentFormatValues(settings::formatValuesFromEntries(snapshot.entries));
            settings::setCurrentLintValues(settings::lintValuesFromEntries(snapshot.entries));
        }
        // The restart path is observable from the OUTSIDE (the ctest runs the
        // real binary): the line reports both the stored value and what the
        // environment carries AFTER the QApplication was constructed.
        const bool echoScale = parser.isSet(QStringLiteral("selfcheck"))
            || parser.isSet(QStringLiteral("ui-scale-echo"));
        if (echoScale) {
            const QByteArray factor = qgetenv("QT_SCALE_FACTOR");
            std::printf("UI-SCALE scale_percent=%d qt_scale_factor=%s font_point_size=%d "
                        "settings=%s\n",
                        settings::ui_scale::readValues(settingsPath).scalePercent,
                        factor.isEmpty() ? "<unset>" : factor.constData(),
                        QApplication::font().pointSize(), qPrintable(settingsPath));
            std::fflush(stdout);
        }
        if (parser.isSet(QStringLiteral("ui-scale-echo"))) {
            return 0; // the seam above is the whole job of this invocation
        }
    }
#endif

    // Crash self-reporting is installed as early as possible: from here on a
    // SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT writes $XDG_STATE_HOME/vedit/crash-*.log
    // (signal, fault address, symbolized backtrace, open file, recent events,
    // build identity) before the process dies.
    {
        const QString buildInfo = QStringLiteral(VEDIT_BUILD_HEAD) + QStringLiteral(" built ")
            + QStringLiteral(__DATE__) + QStringLiteral(" ") + QStringLiteral(__TIME__);
        vedit::app::crash::install(buildInfo);
        vedit::app::crash::event("start argv0=%s", argv[0]);
    }
    if (parser.isSet(QStringLiteral("crash-test"))) {
        const QString which = parser.value(QStringLiteral("crash-test")).trimmed().toLower();
        int signal = SIGSEGV;
        if (which == QStringLiteral("abort")) {
            signal = SIGABRT;
        } else if (which == QStringLiteral("bus")) {
            signal = SIGBUS;
        } else if (which == QStringLiteral("fpe")) {
            signal = SIGFPE;
        } else if (which == QStringLiteral("ill")) {
            signal = SIGILL;
        }
        vedit::app::crash::event("crash-test %s", qPrintable(which));
        vedit::app::crash::setCurrentFile(QStringLiteral("/tmp/vedit-crash-test/input.sv"));
        std::raise(signal);
        return 3; // unreachable unless the handler failed
    }

    ShellOptions options;
    options.stateDir = parser.value(QStringLiteral("state-dir"));
    options.projectRoot = parser.value(QStringLiteral("project"));
    options.themeName = parser.value(QStringLiteral("theme"));
    options.restoreSession = !parser.isSet(QStringLiteral("no-session-restore"));

    if (parser.isSet(QStringLiteral("selfcheck"))) {
        return runSelfCheck();
    }
    if (parser.isSet(QStringLiteral("selfcheck-ffi"))) {
        return ffi::runSelfCheck(parser.value(QStringLiteral("selfcheck-ffi")));
    }
    if (parser.isSet(QStringLiteral("screenshots"))) {
        return captureScreenshots(parser.value(QStringLiteral("screenshots")));
    }

    // T137: validate positional files before creating the shell, so a typo
    // prints one readable line and exits non-zero instead of opening an empty
    // window. The same validation lives in ApplicationShell::openPaths for
    // File -> Open; this early pass only exists to avoid constructing the GUI.
    const QStringList requestedFiles = parser.positionalArguments();
    for (const QString &raw : requestedFiles) {
        const QFileInfo info(raw);
        if (!info.exists() || !info.isFile()) {
            std::fprintf(stderr, "vedit-app: cannot open %s: %s\n", qPrintable(raw),
                         info.exists() ? "not a regular file" : "no such file or directory");
            return 2;
        }
    }

    // Session restore is deferred until the lanes registered their panel
    // factories: setFactory() only affects FUTURE inserts, so restoring during
    // construction left every restored dock a shell placeholder (review M1 --
    // terminal and search). startSession() runs once the factories are ready.
    options.deferSessionRestore = true;

    // T18/AC-4.1/AC-4.8: the interface language is installed BEFORE the shell is
    // built, so menus, panel titles and dialog strings are created in the
    // persisted language. Auto follows the system locale; a missing .qm is a
    // diagnostic, never a failed start. The --selfcheck path above deliberately
    // runs without a translator: its assertions are the English source strings.
    {
        const QString settingsPath = vedit::app::i18n::settingsFilePath(options.stateDir);
        const vedit::app::i18n::InstallResult language =
            vedit::app::i18n::installTranslations(settingsPath);
        if (!language.diagnostic.isEmpty()) {
            std::fprintf(stderr, "vedit-app: %s\n", qPrintable(language.diagnostic));
        }
    }

    ApplicationShell shell(options);
    // AC-1.1 (batch 1): the shell resolved the workspace root in its constructor
    // — from --project, else from the persisted session archive — BEFORE any
    // lane registered a panel factory. Hand that ONE root to every lane through
    // the options they already consume (Files / Search / VCS / project / module
    // tree / palette / diagnostics), so no lane needs a second session peek and
    // an ordinary start (no --project) shows the restored project everywhere.
    options.projectRoot = shell.projectRoot();
    // T18/AC-4.2: the Preferences page drives the one restart path through this
    // handler; the shell owns the strict ordering (guard -> persist ->
    // saveSession -> relaunch), so the dialog never touches shell internals.
    vedit::app::i18n::setRestartHandler(
        [&shell](const QString &tag, const vedit::app::i18n::PersistLanguage &persist,
                 QString *error) { return shell.restartForLanguageChange(tag, persist, error); });
#if defined(VEDIT_HAVE_EDIT_CORE)
    // Slice 1 (AC-S1.1): the product editor is the KTextEditor editing kernel.
    // A deferred start makes the restored panes use it directly (no re-creation
    // of the demo pane).
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());

    // T94: the runtime tree-sitter switch is a product command, not a test hook:
    // turning it off releases the fold registry (the injector unfolds it first)
    // and clears the breadcrumb (no text-heuristic path is invented).
    if (shell.commands() != nullptr) {
        vedit::app::CommandSpec spec;
        spec.id = QStringLiteral("view.treesitter");
        spec.text = QCoreApplication::translate("VEdit", "Use &tree-sitter parsing");
        spec.menu = QStringLiteral("view");
        spec.order = 90;
        spec.checkable = true;
        if (shell.commands()->add(spec) == nullptr) {
            shell.commands()->action(spec.id);
        }
        QAction *treeSitterAction = shell.commands()->action(spec.id);
        const auto applyTreeSitter = [&shell, treeSitterAction](bool enabled) {
            for (EditorPane *pane : shell.editorArea()->panes()) {
                if (auto *binding = vedit::editor::FoldBreadcrumbBinding::instanceFor(
                        pane->editorWidget())) {
                    binding->setTreeSitterEnabled(enabled);
                }
            }
            vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(enabled);
            if (treeSitterAction != nullptr) {
                treeSitterAction->setChecked(enabled);
            }
        };
        if (treeSitterAction != nullptr) {
            treeSitterAction->setChecked(
                vedit::syntax::SyntaxService::instance().treeSitterEnabled());
        }
        shell.commands()->setHandler(spec.id, [applyTreeSitter]() {
            applyTreeSitter(!vedit::syntax::SyntaxService::instance().treeSitterEnabled());
        });
    }
#endif
    vedit::workspace::installPanels(shell, {options.projectRoot, options.stateDir});
    // Terminal panel lane (T35): the shell created the "terminal" panel spec with
    // an empty factory; the widget lives in the terminal lane.
    shell.panels()->setFactory(QStringLiteral("terminal"),
                               vedit::terminal::terminalPanelFactory());
    shell.startSession();
    if (!requestedFiles.isEmpty()) {
        QString openError;
        if (!shell.openPaths(requestedFiles, &openError)) {
            std::fprintf(stderr, "vedit-app: %s\n", qPrintable(openError));
            return 1;
        }
    }
    shell.show();

    const int holdMs = parser.value(QStringLiteral("hold")).toInt();
    if (holdMs > 0) {
        QTimer::singleShot(holdMs, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
