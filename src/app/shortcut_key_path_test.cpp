// RM-5b regression: shortcuts must be VISIBLE (effective + default + explicit
// "unbound"), EFFECTIVE (a user's binding fires through the real key path and
// survives a restart) and DEFAULTed (VS Code-like defaults that never duplicate
// another command or the editor's own KTextEditor actions).
//
// "Real key path" means: QTest sends the key to the main window's QWindow, so Qt's
// shortcut map decides — the same path a physical key press takes. A registry
// data check alone cannot prove "生效".
#include "app_shell.h"
#include "command_registry.h"
#include "desktop_shortcuts.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "settings_dialog.h"
#include "settings_panel.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QScopedPointer>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

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
        std::fflush(stdout);
        return;
    }
    ++g_failures;
    std::printf("CHECK %s FAIL%s\n", qPrintable(name),
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" (%1)").arg(detail)));
    std::fflush(stdout);
}

bool sendKey(QWidget *widget, Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    if (widget == nullptr) {
        return false;
    }
    QWindow *window = widget->windowHandle();
    if (window == nullptr) {
        return false;
    }
    QTest::keyClick(window, key, modifiers);
    QApplication::processEvents();
    return true;
}

bool sendSequence(QWidget *widget, const QKeySequence &sequence)
{
    if (sequence.isEmpty()) {
        return false;
    }
    const QKeyCombination combination = sequence[0];
    return sendKey(widget, combination.key(), combination.keyboardModifiers());
}

int rowForCommand(QTableWidget *table, const QString &commandId)
{
    if (table == nullptr) {
        return -1;
    }
    const QString suffix = QStringLiteral("(%1)").arg(commandId);
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem *item = table->item(row, 0);
        if (item != nullptr && item->text().endsWith(suffix)) {
            return row;
        }
    }
    return -1;
}

QString cellText(QTableWidget *table, int row, int column)
{
    if (table == nullptr || row < 0) {
        return QString();
    }
    const QTableWidgetItem *item = table->item(row, column);
    return item != nullptr ? item->text() : QString();
}

// Deepest child under the caret: KTextEditor delivers real keys to
// KateViewInternal, not to the public View.
QWidget *innerWidget(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    const QPoint point = view->cursorToCoordinate(view->cursorPosition());
    QWidget *target = view->childAt(point);
    return target != nullptr ? target : view;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    check(QStringLiteral("setup.temp-dir"), dir.isValid());
    if (!dir.isValid()) {
        return 1;
    }

    vedit::app::ShellOptions options;
    options.stateDir = dir.path() + QStringLiteral("/state-a");
    options.restoreSession = false;
    options.showDefaultPanels = false;

    const QString split = QStringLiteral("view.split.horizontal");
    const QKeySequence splitDefault(QStringLiteral("Ctrl+Alt+H"));
    const QKeySequence custom(QStringLiteral("Ctrl+Alt+Shift+K"));

    // ======================= Phase A: registry + Shortcuts page =============
    vedit::app::ApplicationShell shell(options);
    shell.show();
    QTest::qWait(100);
    QApplication::processEvents();
    vedit::app::CommandRegistry *commands = shell.commands();
    check(QStringLiteral("setup.registry"), commands != nullptr);
    if (commands == nullptr) {
        return 1;
    }

    // The full effective/default/state dump is the visible-binding evidence.
    for (const QString &id : commands->ids()) {
        std::printf("BINDING %-28s effective=%-16s default=%-16s state=%s\n", qPrintable(id),
                    qPrintable(commands->effectiveShortcut(id).toString(QKeySequence::PortableText)),
                    qPrintable(commands->defaultShortcut(id).toString(QKeySequence::PortableText)),
                    commands->hasUserOverride(id)
                        ? "custom"
                        : (commands->effectiveShortcut(id).isEmpty() ? "unbound" : "default"));
    }
    const QStringList duplicates = commands->duplicateShortcuts();
    check(QStringLiteral("defaults.no-duplicate-bindings"), duplicates.isEmpty(),
          duplicates.join(QStringLiteral(" | ")));
    // The commands that used to ship with NO default now carry one.
    for (const QString &id : {QStringLiteral("edit.undo"), QStringLiteral("edit.redo"),
                              QStringLiteral("file.new"), QStringLiteral("file.save-all"),
                              QStringLiteral("search.focus"), QStringLiteral("view.toolbar")}) {
        check(QStringLiteral("defaults.%1-has-binding").arg(id),
              !commands->defaultShortcut(id).isEmpty(),
              commands->defaultShortcut(id).toString());
    }
    check(QStringLiteral("defaults.split-horizontal-free-sequence"),
          commands->defaultShortcut(split) == splitDefault,
          commands->defaultShortcut(split).toString());

    // --- RM-5c (review M1): no shipped default may be DESKTOP-global --------
    // The desktop consumes its global shortcut before the application sees it, so
    // a default bound to one is a dead key even though every local check passes.
    // The data set lives in src/app/desktop_shortcuts.h; the assertion covers the
    // PRIMARY and every EXTRA binding of every command.
    for (const QString &id : commands->ids()) {
        for (const QKeySequence &sequence : commands->effectiveShortcuts(id)) {
            const QString owner = vedit::app::desktopGlobalShortcutOwner(sequence);
            check(QStringLiteral("de.no-global-default.%1.%2")
                      .arg(id, sequence.toString(QKeySequence::PortableText)),
                  owner.isEmpty(),
                  QStringLiteral("%1 is a desktop-global shortcut (%2)")
                      .arg(sequence.toString(QKeySequence::PortableText), owner));
        }
    }
    check(QStringLiteral("de.toolbar-moved-off-ctrl-alt-t"),
          commands->effectiveShortcut(QStringLiteral("view.toolbar"))
              == QKeySequence(QStringLiteral("Ctrl+Shift+Alt+T")),
          commands->effectiveShortcut(QStringLiteral("view.toolbar")).toString());
    // RM-5c (review M2): Ctrl+Y is a real extra default on redo.
    check(QStringLiteral("de.redo-has-ctrl-y-extra"),
          commands->effectiveShortcuts(QStringLiteral("edit.redo"))
              .contains(QKeySequence(QStringLiteral("Ctrl+Y"))),
          QStringLiteral("count=%1")
              .arg(commands->effectiveShortcuts(QStringLiteral("edit.redo")).size()));

    // The Shortcuts page: select the row and assign through the real widgets.
    struct Page {
        QScopedPointer<vedit::workspace::settings::SettingsPanel> panel;
        QScopedPointer<vedit::workspace::settings::SettingsDialog> dialog;
        QWidget *host = nullptr;
        QTableWidget *table = nullptr;
        QKeySequenceEdit *editor = nullptr;
        QPushButton *assign = nullptr;
        QPushButton *resetOne = nullptr;
        QPushButton *resetAll = nullptr;
    };
    const auto openPage = [&](Page &page, bool asDialog, const QString &tag) {
        if (asDialog) {
            page.dialog.reset(new vedit::workspace::settings::SettingsDialog(
                shell.themes(), commands, dir.path() + QStringLiteral("/settings.toml")));
            page.dialog->show();
            page.host = page.dialog.data();
            check(tag + QStringLiteral(".loaded"), page.dialog->panel()->flush());
        } else {
            page.panel.reset(new vedit::workspace::settings::SettingsPanel(
                shell.themes(), commands, dir.path() + QStringLiteral("/settings.toml")));
            page.host = page.panel.data();
            check(tag + QStringLiteral(".loaded"), page.panel->flush());
        }
        QTest::qWait(20);
        QApplication::processEvents();
        page.table = page.host->findChild<QTableWidget *>(QStringLiteral("settings-keys"));
        page.editor = page.host->findChild<QKeySequenceEdit *>(QStringLiteral("settings-shortcut-editor"));
        page.assign = page.host->findChild<QPushButton *>(QStringLiteral("settings-assign"));
        page.resetOne = page.host->findChild<QPushButton *>(QStringLiteral("settings-clear"));
        page.resetAll = page.host->findChild<QPushButton *>(QStringLiteral("settings-reset-all"));
        check(tag + QStringLiteral(".widgets"),
              page.table != nullptr && page.editor != nullptr && page.assign != nullptr
                  && page.resetOne != nullptr && page.resetAll != nullptr);
    };

    Page page;
    openPage(page, /*asDialog=*/false, QStringLiteral("page"));
    if (page.table != nullptr) {
        // --- VISIBLE: effective + default + explicit unbound ---------------
        const int splitRow = rowForCommand(page.table, split);
        check(QStringLiteral("visible.split-row"), splitRow >= 0, QString::number(splitRow));
        check(QStringLiteral("visible.split-effective-default"),
              cellText(page.table, splitRow, 1) == QStringLiteral("Ctrl+Alt+H"),
              cellText(page.table, splitRow, 1));
        check(QStringLiteral("visible.split-default-column"),
              cellText(page.table, splitRow, 2) == QStringLiteral("Ctrl+Alt+H"),
              cellText(page.table, splitRow, 2));
        check(QStringLiteral("visible.split-state-default"),
              cellText(page.table, splitRow, 3) == QStringLiteral("default"),
              cellText(page.table, splitRow, 3));
        const int redoRow = rowForCommand(page.table, QStringLiteral("edit.redo"));
        check(QStringLiteral("visible.redo-shows-both"),
              redoRow >= 0
                  && cellText(page.table, redoRow, 1) == QStringLiteral("Ctrl+Shift+Z / Ctrl+Y"),
              cellText(page.table, redoRow, 1));
        const int aboutRow = rowForCommand(page.table, QStringLiteral("help.about"));
        check(QStringLiteral("visible.unbound-is-explicit"),
              aboutRow >= 0
                  && cellText(page.table, aboutRow, 1) == QStringLiteral("(unbound)")
                  && cellText(page.table, aboutRow, 2) == QStringLiteral("(none)")
                  && cellText(page.table, aboutRow, 3) == QStringLiteral("unbound"),
              QStringLiteral("eff=%1 def=%2 state=%3").arg(cellText(page.table, aboutRow, 1),
                                                            cellText(page.table, aboutRow, 2),
                                                            cellText(page.table, aboutRow, 3)));

        // --- CONFLICT: assigning an owned sequence is REFUSED + named ------
        page.table->selectRow(splitRow);
        QApplication::processEvents();
        page.editor->setKeySequence(QKeySequence(QStringLiteral("Ctrl+S")));
        page.assign->click();
        QApplication::processEvents();
        check(QStringLiteral("conflict.registry-refused"),
              commands->effectiveShortcut(split) == splitDefault,
              commands->effectiveShortcut(split).toString());
        check(QStringLiteral("conflict.registry-owner-named"),
              page.panel->statusText().contains(QStringLiteral("file.save")),
              page.panel->statusText());

        // --- RM-5c: the page refuses a DESKTOP-global sequence too ----------
        page.table->selectRow(splitRow);
        QApplication::processEvents();
        page.editor->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+T")));
        page.assign->click();
        QApplication::processEvents();
        check(QStringLiteral("conflict.desktop-refused"),
              commands->effectiveShortcut(split) == splitDefault,
              commands->effectiveShortcut(split).toString());
        check(QStringLiteral("conflict.desktop-owner-named"),
              page.panel->statusText().contains(QStringLiteral("desktop")),
              page.panel->statusText());

        // --- EFFECTIVE: a user's binding fires through the real key path ---
        page.editor->setKeySequence(custom);
        page.assign->click();
        QApplication::processEvents();
        check(QStringLiteral("custom.effective-updated"), commands->effectiveShortcut(split) == custom,
              commands->effectiveShortcut(split).toString());
        check(QStringLiteral("visible.custom-state"),
              cellText(page.table, rowForCommand(page.table, split), 3) == QStringLiteral("custom"),
              cellText(page.table, rowForCommand(page.table, split), 3));
        const int panesBefore = shell.editorArea()->paneCount();
        sendSequence(&shell, custom);
        check(QStringLiteral("custom.new-key-fires"),
              shell.editorArea()->paneCount() == panesBefore + 1,
              QStringLiteral("panes %1 -> %2").arg(panesBefore).arg(shell.editorArea()->paneCount()));
        const int panesBeforeOld = shell.editorArea()->paneCount();
        sendSequence(&shell, splitDefault);
        check(QStringLiteral("custom.old-default-inert"),
              shell.editorArea()->paneCount() == panesBeforeOld,
              QStringLiteral("panes %1 -> %2").arg(panesBeforeOld).arg(shell.editorArea()->paneCount()));
    }

    // --- the MODAL Preferences dialog (deferred persistence) --------------
    // This is the surface the user actually edits: Assign previews immediately,
    // OK stores it, Cancel would discard. The key must not be pressed while the
    // dialog is on top (a modal dialog owns the keyboard), so the dialog is
    // accepted first and the key is then sent to the shell window.
    {
        const QString dialogCommand = QStringLiteral("view.toolbar");
        const QKeySequence dialogBinding(QStringLiteral("Ctrl+Alt+Shift+D"));
        vedit::workspace::settings::SettingsDialog dialog(
            shell.themes(), commands, dir.path() + QStringLiteral("/settings.toml"));
        dialog.show();
        check(QStringLiteral("dialog.loaded"), dialog.panel()->flush());
        QTest::qWait(20);
        QApplication::processEvents();
        auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("settings-keys"));
        auto *editor = dialog.findChild<QKeySequenceEdit *>(QStringLiteral("settings-shortcut-editor"));
        auto *assign = dialog.findChild<QPushButton *>(QStringLiteral("settings-assign"));
        check(QStringLiteral("dialog.widgets"),
              table != nullptr && editor != nullptr && assign != nullptr);
        const int row = rowForCommand(table, dialogCommand);
        check(QStringLiteral("dialog.row"), row >= 0);
        if (row >= 0 && editor != nullptr && assign != nullptr) {
            table->selectRow(row);
            QApplication::processEvents();
            editor->setKeySequence(dialogBinding);
            assign->click();
            QApplication::processEvents();
            check(QStringLiteral("dialog.preview-effective"),
                  commands->effectiveShortcut(dialogCommand) == dialogBinding,
                  commands->effectiveShortcut(dialogCommand).toString());
            dialog.accept();
            QApplication::processEvents();
            check(QStringLiteral("dialog.ok-persisted"),
                  commands->store()->shortcut(dialogCommand, QKeySequence()) == dialogBinding,
                  commands->store()->shortcut(dialogCommand, QKeySequence()).toString());
            // The stored binding is what a restart will load (the full restart
            // proof for a custom binding is the panel phase above).
            check(QStringLiteral("dialog.ok-restart-value"),
                  commands->defaultShortcut(dialogCommand)
                      != commands->effectiveShortcut(dialogCommand));
            // Clean up so later checks start from the defaults.
            page.panel->resetShortcut(dialogCommand);
            QApplication::processEvents();
            check(QStringLiteral("dialog.cleanup-default"),
                  commands->effectiveShortcut(dialogCommand)
                      == commands->defaultShortcut(dialogCommand));
        }
    }

    // --- RESTART persistence ----------------------------------------------
    {
        vedit::app::ApplicationShell restarted(options);
        restarted.show();
        QTest::qWait(100);
        QApplication::processEvents();
        QAction *restored = restarted.commands()->action(split);
        check(QStringLiteral("persist.restart-loaded"),
              restored != nullptr && restored->shortcut() == custom,
              restored != nullptr ? restored->shortcut().toString() : QStringLiteral("<missing>"));
        const int before = restarted.editorArea()->paneCount();
        sendSequence(&restarted, custom);
        check(QStringLiteral("persist.restart-key-fires"),
              restarted.editorArea()->paneCount() == before + 1,
              QStringLiteral("panes %1 -> %2").arg(before).arg(restarted.editorArea()->paneCount()));

        // --- RESET: per command and all-at-once ---------------------------
        vedit::workspace::settings::SettingsPanel resetPanel(
            restarted.themes(), restarted.commands(), dir.path() + QStringLiteral("/settings.toml"));
        resetPanel.flush();
        resetPanel.resetShortcut(split);
        QApplication::processEvents();
        check(QStringLiteral("reset.per-command"),
              restarted.commands()->effectiveShortcut(split) == splitDefault,
              restarted.commands()->effectiveShortcut(split).toString());
        // Two custom bindings, then one "reset all".
        resetPanel.setShortcut(QStringLiteral("view.split.vertical"),
                               QKeySequence(QStringLiteral("Ctrl+Alt+Shift+L")));
        resetPanel.setShortcut(split, custom);
        QApplication::processEvents();
        resetPanel.resetAllShortcuts();
        QApplication::processEvents();
        check(QStringLiteral("reset.all-at-once"),
              restarted.commands()->effectiveShortcut(split) == splitDefault
                  && restarted.commands()->effectiveShortcut(QStringLiteral("view.split.vertical"))
                         == QKeySequence(QStringLiteral("Ctrl+Shift+V")),
              QStringLiteral("split=%1 vertical=%2")
                  .arg(restarted.commands()->effectiveShortcut(split).toString(),
                       restarted.commands()
                           ->effectiveShortcut(QStringLiteral("view.split.vertical"))
                           .toString()));
    }

    // ======================= Phase B: the real editor =====================
    // The KTextEditor view ships its own bindings (Find Ctrl+F, Save Ctrl+S,
    // Undo Ctrl+Z, Print Ctrl+P, Find-Selected-Backwards Ctrl+Shift+H …). Qt
    // drops BOTH actions of a duplicated sequence, so this phase proves the
    // authority: shell-owned sequences are removed from the editor and the
    // shell's defaults fire; the editor's remaining keys are NOT touched.
    if (KTextEditor::Editor::instance() == nullptr) {
        std::printf("SKIP: KTextEditor plugin unavailable\n");
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return g_failures == 0 ? 77 : 1;
    }
    vedit::app::ShellOptions realOptions = options;
    realOptions.stateDir = dir.path() + QStringLiteral("/state-b");
    realOptions.deferSessionRestore = true;
    vedit::app::ApplicationShell realShell(realOptions);
    realShell.show();
    QTest::qWait(50);
    // A command that takes Ctrl+P (the product's quick-open key, registered by
    // the palette lane) — planted BEFORE startSession so it lands in a menu and
    // its QAction is associated with the window (otherwise no shortcut fires).
    int probeFired = 0;
    {
        vedit::app::CommandSpec probe;
        probe.id = QStringLiteral("test.ctrl-p-probe");
        probe.text = QStringLiteral("Ctrl+P probe");
        probe.menu = QStringLiteral("file");
        probe.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+P"));
        probe.order = 999;
        realShell.commands()->add(probe);
        realShell.commands()->setHandler(probe.id, [&probeFired]() { ++probeFired; });
    }
    realShell.startSession();
    // RM-5c: the data set itself is asserted here; the PRODUCT-wide assertion
    // (every lane's command, including view.panel.diagnostics) lives in
    // `vedit-app --selfcheck` (`commands.no-desktop-global-shortcuts`), which
    // installs the lanes this bare shell does not have.
    check(QStringLiteral("de.data-names-kde-show-desktop"),
          vedit::app::desktopGlobalShortcutOwner(QKeySequence(QStringLiteral("Ctrl+Alt+D")))
              .contains(QStringLiteral("KDE")));
    check(QStringLiteral("de.data-names-gnome-terminal"),
          vedit::app::desktopGlobalShortcutOwner(QKeySequence(QStringLiteral("Ctrl+Alt+T")))
              .contains(QStringLiteral("terminal")));
    realShell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    QTest::qWait(100);
    QApplication::processEvents();
    vedit::app::EditorPane *pane = realShell.editorArea()->ensurePane();
    check(QStringLiteral("real.setup-pane"), pane != nullptr);
    if (pane == nullptr) {
        return 1;
    }
    const QString filePath = dir.filePath(QStringLiteral("rm5b.sv"));
    {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            check(QStringLiteral("real.setup-file"), false);
            return 1;
        }
        file.write("module rm5b;\n  wire a;\nendmodule\n");
    }
    QString openError;
    check(QStringLiteral("real.setup-open"), pane->openFile(filePath, &openError), openError);
    QApplication::processEvents();
    KTextEditor::View *view = vedit::editor::viewFor(pane->editorWidget());
    check(QStringLiteral("real.setup-view"), view != nullptr);
    if (view == nullptr) {
        return 1;
    }
    KTextEditor::Document *document = view->document();

    // --- L2 (review): the SHIPPED zero-edit Ctrl+Z path -------------------
    // RM-5a proved zero-edit Ctrl+Z cannot blank the buffer, but its test drove
    // the editor's own action; in the product RM-5b hands Ctrl+Z to the shell
    // command. Press the physical key through the shell on a buffer with ZERO
    // edits: the text must not change and the shell's undo command must still be
    // disabled (no undo step exists).
    {
        const QString textBefore = document->text();
        QAction *undoCommand = realShell.commands()->action(QStringLiteral("edit.undo"));
        check(QStringLiteral("zero-edit-shell.undo-command-disabled"),
              undoCommand != nullptr && !undoCommand->isEnabled());
        sendSequence(&realShell, QKeySequence(QStringLiteral("Ctrl+Z")));
        check(QStringLiteral("zero-edit-shell.buffer-unchanged"), document->text() == textBefore,
              QStringLiteral("size=%1").arg(document->text().size()));
        check(QStringLiteral("zero-edit-shell.identity"),
              pane->editorWidget() == view && view->document() == document);
        check(QStringLiteral("zero-edit-shell.undo-still-disabled"),
              undoCommand != nullptr && !undoCommand->isEnabled());
    }

    // --- RM-5c (M2): Ctrl+Tab / Ctrl+Shift+Tab are ENFORCED commands -----
    {
        const QString secondPath = dir.filePath(QStringLiteral("rm5b-second.sv"));
        QFile secondFile(secondPath);
        if (secondFile.open(QIODevice::WriteOnly)) {
            secondFile.write("module rm5b_second;\nendmodule\n");
            secondFile.close();
        }
        check(QStringLiteral("tabs.setup-second-open"), pane->openFile(secondPath, &openError),
              openError);
        QApplication::processEvents();
        check(QStringLiteral("tabs.setup-two-tabs"),
              pane->tabCount() == 2 && pane->activeTabIndex() == 1,
              QStringLiteral("tabs=%1 active=%2")
                  .arg(pane->tabCount())
                  .arg(pane->activeTabIndex()));
        sendSequence(&realShell, QKeySequence(QStringLiteral("Ctrl+Tab")));
        check(QStringLiteral("tabs.ctrl-tab-next"), pane->activeTabIndex() == 0,
              QString::number(pane->activeTabIndex()));
        sendSequence(&realShell, QKeySequence(QStringLiteral("Ctrl+Shift+Tab")));
        check(QStringLiteral("tabs.ctrl-shift-tab-previous"), pane->activeTabIndex() == 1,
              QString::number(pane->activeTabIndex()));
        // Back to the first tab for the undo tests below.
        pane->setActiveTab(0);
        QApplication::processEvents();
    }

    // The editor's own keys are still there (nothing was broken)…
    const QKeySequence findKey(QStringLiteral("Ctrl+F"));
    check(QStringLiteral("authority.editor-find-kept"),
          !realShell.commands()->foreignShortcutOwners(findKey).isEmpty(),
          realShell.commands()->foreignShortcutOwners(findKey).join(QStringLiteral(", ")));
    // …but every sequence the shell owns was removed from the editor, so it can
    // never be ambiguous.
    for (const QString &id : realShell.commands()->ids()) {
        const QKeySequence sequence = realShell.commands()->effectiveShortcut(id);
        if (sequence.isEmpty()) {
            continue;
        }
        const QStringList foreign = realShell.commands()->foreignShortcutOwners(sequence);
        check(QStringLiteral("authority.no-editor-collision.%1").arg(id), foreign.isEmpty(),
              QStringLiteral("%1 also owned by %2").arg(sequence.toString(),
                                                        foreign.join(QStringLiteral(", "))));
    }
    // The planted Ctrl+P command owns the key now (the editor's Print copy is
    // gone) and fires through the real key path.
    check(QStringLiteral("authority.ctrl-p-shell-owned"),
          realShell.commands()->foreignShortcutOwners(QKeySequence(QStringLiteral("Ctrl+P"))).isEmpty());
    sendSequence(&realShell, QKeySequence(QStringLiteral("Ctrl+P")));
    check(QStringLiteral("authority.ctrl-p-fires"), probeFired == 1, QString::number(probeFired));

    // The default that used to be dead (Ctrl+Shift+H, the editor's
    // Find-Selected-Backwards) was moved to a free sequence and fires.
    const int beforeH = realShell.editorArea()->paneCount();
    sendSequence(&realShell, splitDefault);
    check(QStringLiteral("real.split-horizontal-fires"),
          realShell.editorArea()->paneCount() == beforeH + 1,
          QStringLiteral("panes %1 -> %2").arg(beforeH).arg(realShell.editorArea()->paneCount()));
    const int beforeV = realShell.editorArea()->paneCount();
    sendSequence(&realShell, QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    check(QStringLiteral("real.split-vertical-fires"),
          realShell.editorArea()->paneCount() == beforeV + 1,
          QStringLiteral("panes %1 -> %2").arg(beforeV).arg(realShell.editorArea()->paneCount()));

    // Undo/redo now belong to the shell commands and drive the editor's own
    // undo slot; a real Ctrl+Z / Ctrl+Shift+Z must fire them.
    realShell.editorArea()->setActivePane(pane);
    pane->editorWidget()->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    const QString beforeTyping = document->text();
    QTest::keyClicks(innerWidget(view), QStringLiteral("x"));
    QApplication::processEvents();
    check(QStringLiteral("real.typing-lands"), document->text() != beforeTyping);
    sendSequence(&realShell, QKeySequence(QStringLiteral("Ctrl+Z")));
    check(QStringLiteral("real.ctrl-z-undoes"), document->text() == beforeTyping,
          QStringLiteral("text=%1").arg(document->text().size()));
    sendSequence(&realShell, QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    check(QStringLiteral("real.ctrl-shift-z-redoes"), document->text() != beforeTyping);
    sendSequence(&realShell, QKeySequence(QStringLiteral("Ctrl+Z")));
    check(QStringLiteral("real.ctrl-z-undoes-again"), document->text() == beforeTyping);
    // RM-5c (M2): Ctrl+Y is an EXTRA default on redo — the VS Code habit works.
    sendSequence(&realShell, QKeySequence(QStringLiteral("Ctrl+Y")));
    check(QStringLiteral("real.ctrl-y-redoes"), document->text() != beforeTyping,
          QStringLiteral("size=%1").arg(document->text().size()));
    sendSequence(&realShell, QKeySequence(QStringLiteral("Ctrl+Z")));
    check(QStringLiteral("real.ctrl-z-after-ctrl-y"), document->text() == beforeTyping);

    // A user cannot silently take an editor-owned sequence: the page names it.
    {
        vedit::workspace::settings::SettingsPanel page(
            realShell.themes(), realShell.commands(), dir.path() + QStringLiteral("/settings.toml"));
        page.flush();
        page.setShortcut(split, findKey);
        QApplication::processEvents();
        check(QStringLiteral("real.editor-conflict-refused"),
              realShell.commands()->effectiveShortcut(split) == splitDefault,
              realShell.commands()->effectiveShortcut(split).toString());
        check(QStringLiteral("real.editor-conflict-named"),
              page.statusText().contains(QStringLiteral("Find")),
              page.statusText());
    }

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
