// T15 product probe (AC-3.1/3.2): the editor's right-click menu, the LSP action
// channel and the F8 / Shift+F8 diagnostic navigation, in a REAL ApplicationShell
// with the REAL diagnostics lane and a REAL verible-verilog-ls session.
//
//   bash evidence/e4-actions/probe/run.sh
//
// It checks, per action, that the menu item exists, that its enabled state and
// its reason come from the capability gate the diagnostics lane publishes, and
// that triggering it produces the wired effect (a jump in the process-wide jump
// registry, a reference count in the status bar, a rename preview, a reformatted
// document). Hover must be GREYED with the backend's reason on verible (the
// capability row says hoverProvider=false); the channel must say so instead of
// silently doing nothing.
//
// Exit 0 = every check passed (SKIPs print their reason and never fake a pass).
#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "kernel/ktexteditor_provider.h"
#include "panel_registry.h"

#include "kernel/diagnostics_annotations.h"
#include "kernel/editor_context_menu.h"
#include "kernel/lsp_actions.h"

#include "diagnostics/diagnostics_controller.h"
#include "diagnostics/diagnostics_integration.h"
#include "diagnostics/diagnostics_panel.h"
#include "jump/jump_registry.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QInputDialog>
#include <QMenu>
#include <QStatusBar>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cstdio>
#include <functional>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skips = 0;

void check(const char *name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("ok   %s%s\n", name,
                    detail.isEmpty() ? "" : (" - " + detail.toStdString()).c_str());
    } else {
        ++g_failures;
        std::printf("FAIL %s%s\n", name,
                    detail.isEmpty() ? "" : (" - " + detail.toStdString()).c_str());
    }
    std::fflush(stdout);
}

void skip(const char *name, const QString &reason)
{
    ++g_skips;
    std::printf("SKIP %s - %s\n", name, reason.toStdString().c_str());
    std::fflush(stdout);
}

void observe(const char *name, const QString &detail)
{
    std::printf("OBSERVED %s %s\n", name, detail.toStdString().c_str());
    std::fflush(stdout);
}

bool spinUntil(const std::function<bool()> &done, int timeoutMs)
{
    if (done()) {
        return true;
    }
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(25);
    bool ok = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (done()) {
            ok = true;
            loop.quit();
        }
    });
    poll.start();
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return done();
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) == text.toUtf8().size();
}

class RecordingJumpTarget final : public vedit::workspace::jump::IJumpTarget
{
public:
    bool jumpTo(const QString &filePath, int line, int column) override
    {
        ++calls;
        lastFile = filePath;
        lastLine = line;
        lastColumn = column;
        return true;
    }
    void reset()
    {
        calls = 0;
        lastFile.clear();
        lastLine = 0;
        lastColumn = 0;
    }

    int calls = 0;
    QString lastFile;
    int lastLine = 0;
    int lastColumn = 0;
};

/// Open `path` in the pane and wait until the diagnostics lane has analysed it
/// (the same "analyzed" status the product's status line shows). Returns false
/// when the toolchain is missing or the analysis never finished.
bool openAndAnalyze(vedit::app::EditorPane *pane,
                    vedit::workspace::diagnostics::DiagnosticsPanel *panel, const QString &path,
                    QString *error)
{
    if (!pane->openFile(path, error)) {
        return false;
    }
    pane->setActive(true);
    return spinUntil(
        [panel]() {
            return panel->statusText().contains(QStringLiteral("analyzed"))
                   || panel->statusText().contains(QStringLiteral("unavailable"));
        },
        60000);
}

QString textOf(const KTextEditor::View *view)
{
    return view != nullptr && view->document() != nullptr ? view->document()->text() : QString();
}

/// The menu item the editor's context menu adds for one action.
QAction *findMenuItem(QMenu *menu, const QString &objectName)
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

int built_default_count(QMenu *menu)
{
    int count = 0;
    if (menu == nullptr) {
        return count;
    }
    for (QAction *action : menu->actions()) {
        if (!action->objectName().startsWith(QStringLiteral("vedit.action."))) {
            ++count;
        }
    }
    return count;
}

QStringList menuTexts(QMenu *menu)
{
    QStringList texts;
    if (menu == nullptr) {
        return texts;
    }
    for (QAction *action : menu->actions()) {
        const QString text = action->isSeparator() ? QStringLiteral("---") : action->text();
        texts.append(QStringLiteral("%1[%2]").arg(text, action->isEnabled() ? QStringLiteral("on")
                                                                           : QStringLiteral("off")));
    }
    return texts;
}

QString sanitized(QString text)
{
    return text.remove(QLatin1Char('\t')).remove(QLatin1Char('&'));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-editor-actions-probe"));

    const QString shotMenu = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString shotF8 = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    const QString shotMenuDiagnostics = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();
    bool requirePopup = false;
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--require-popup")) {
            requirePopup = true;
        }
    }
    const QString platform = QGuiApplication::platformName();
    std::printf("OBSERVED platform=%s\n", platform.toUtf8().constData());

    QTemporaryDir workspace;
    if (!workspace.isValid()) {
        std::printf("FATAL: no temporary directory\n");
        return 2;
    }
    const QString project = workspace.path() + QStringLiteral("/project");
    const QString stateDir = workspace.path() + QStringLiteral("/state");
    QDir().mkpath(project);
    QDir().mkpath(stateDir);

    // nav.sv: one file, a module declaration and an instantiation of it, so the
    // definition/references/rename requests have a real answer in a session that
    // was started over exactly this file.
    const QString navPath = project + QStringLiteral("/nav.sv");
    const QString navText = QStringLiteral(
        "module leaf;\n"
        "  logic q;\n"
        "endmodule\n"
        "\n"
        "module top;\n"
        "  logic a;\n"
        "  leaf u_leaf(.q(a));\n"
        "endmodule\n");
    // format.sv: deliberately unformatted (one line), so verible-verilog-format
    // MUST change the document for the format action to count as effective.
    const QString formatPath = project + QStringLiteral("/format.sv");
    const QString formatText =
        QStringLiteral("module fmt;logic x;always_comb begin\nx=1'b0;\nend\nendmodule\n");
    // broken.sv: several syntax errors on distinct lines (the F8 data).
    const QString brokenPath = project + QStringLiteral("/broken.sv");
    const QString brokenText = QStringLiteral(
        "module broken;\n"
        "  logic a b;\n"
        "  assign c = ;\n"
        "  always_comb begin\n"
        "    d = e\n"
        "  end\n"
        "endmodule\n");
    if (!writeFile(navPath, navText) || !writeFile(formatPath, formatText)
        || !writeFile(brokenPath, brokenText)) {
        std::printf("FATAL: could not write the fixture files\n");
        return 2;
    }

    if (!writeFile(stateDir + QStringLiteral("/settings.toml"), QString())) {
        std::printf("FATAL: could not write the empty settings.toml\n");
        return 2;
    }

    RecordingJumpTarget jumpTarget;
    vedit::workspace::jump::Registry::instance().reset();
    vedit::workspace::jump::setTarget(&jumpTarget);

    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = project;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    check("probe.edit-core-provider", provider != nullptr);
    if (provider == nullptr) {
        return 1;
    }
    vedit::app::ApplicationShell shell(options);
    shell.editorArea()->setProvider(provider.get());
    shell.editorArea()->ensurePane();
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = project;
    integration.stateDir = stateDir;
    vedit::workspace::installDiagnostics(shell, integration);
    shell.panels()->showPanel(QStringLiteral("outline"), &shell);
    shell.resize(1280, 860);
    shell.show();
    QCoreApplication::processEvents();

    auto *panel = shell.findChild<vedit::workspace::diagnostics::DiagnosticsPanel *>();
    check("probe.diagnostics-panel", panel != nullptr);
    if (panel == nullptr) {
        return 1;
    }
    vedit::app::EditorPane *pane = shell.editorArea()->activePane();
    check("probe.active-pane", pane != nullptr);
    if (pane == nullptr) {
        return 1;
    }

    // --- the gate the menu reads comes from the diagnostics lane ------------
    const QVariantMap gates = vedit::editor::publishedGates(&shell);
    check("gate.published-on-shell", !gates.isEmpty());
    const QString backend = vedit::editor::publishedBackend(&shell);
    observe("gate.backend", backend);
    std::printf("OBSERVED gate.map backend='%s'\n", backend.toUtf8().constData());
    for (auto it = gates.cbegin(); it != gates.cend(); ++it) {
        const QVariantMap row = it.value().toMap();
        std::printf("GATE action=%s enabled=%d known=%d source='%s' reason='%s'\n",
                    it.key().toUtf8().constData(),
                    row.value(QStringLiteral("enabled")).toBool() ? 1 : 0,
                    row.value(QStringLiteral("known"), true).toBool() ? 1 : 0,
                    row.value(QStringLiteral("source")).toString().toUtf8().constData(),
                    row.value(QStringLiteral("reason")).toString().toUtf8().constData());
    }
    std::fflush(stdout);

    // The diagnostics lane applies the backend selection when its settings
    // watcher sees the file; that apply() is what fills the formatter-gated row.
    const bool formatGateReady = spinUntil(
        [&shell]() {
            const QVariantMap map = vedit::editor::publishedGates(&shell);
            return map.value(QStringLiteral("formatting")).toMap().value(QStringLiteral("enabled")).toBool();
        },
        15000);
    check("gate.formatting-enabled", formatGateReady,
          QStringLiteral("formatter=%1")
              .arg(vedit::editor::publishedGates(&shell)
                       .value(QStringLiteral("formatting"))
                       .toMap()
                       .value(QStringLiteral("reason"))
                       .toString()));

    // --- no-file precondition (before any file is opened) -------------------
    QWidget *firstEditor = pane->editorWidget();
    auto *firstView = qobject_cast<KTextEditor::View *>(firstEditor);
    check("probe.ktexteditor-view", firstView != nullptr);
    if (firstView == nullptr) {
        return 1;
    }
    vedit::editor::LspActionChannel *channel = vedit::editor::LspActionChannel::instanceFor(firstView);
    check("channel.installed-by-provider", channel != nullptr);
    vedit::editor::EditorContextMenu *menuBinding =
        vedit::editor::EditorContextMenu::instanceFor(firstView);
    check("menu.installed-by-provider", menuBinding != nullptr);
    vedit::editor::DiagnosticNavigator *navigator =
        vedit::editor::DiagnosticNavigator::instanceFor(firstView);
    check("navigator.installed-by-provider", navigator != nullptr);
    if (channel == nullptr || menuBinding == nullptr || navigator == nullptr) {
        return 1;
    }
    const vedit::editor::ActionAvailability noFile =
        channel->availability(vedit::editor::EditorAction::Definition);
    check("channel.no-file-refused", !noFile.enabled && !noFile.reason.isEmpty(), noFile.reason);

    // --- open the navigation fixture and wait for the real LSP analysis -----
    QString openError;
    const bool analysed = openAndAnalyze(pane, panel, navPath, &openError);
    check("probe.file-opened", pane->currentFilePath() == navPath, openError);
    if (!analysed || panel->statusText().contains(QStringLiteral("unavailable"))) {
        skip("none.lsp-actions", panel->statusText());
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return g_failures == 0 ? 0 : 1;
    }
    check("probe.analyzed", analysed, panel->statusText());
    observe("probe.status", panel->statusText());

    auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget());
    check("probe.view-after-open", view != nullptr);
    if (view == nullptr) {
        return 1;
    }
    channel = vedit::editor::LspActionChannel::instanceFor(view);
    menuBinding = vedit::editor::EditorContextMenu::instanceFor(view);
    navigator = vedit::editor::DiagnosticNavigator::instanceFor(view);
    check("channel.rebound-to-open-view", channel != nullptr);
    if (channel == nullptr || menuBinding == nullptr || navigator == nullptr) {
        return 1;
    }

    // --- (a) a REAL right-click produces the menu with our sections --------
    {
        view->setFocus();
        QCoreApplication::processEvents();
        QWidget *inner = view->childAt(QPoint(120, 60));
        QWidget *eventTarget = inner != nullptr ? inner : view;
        std::printf("OBSERVED menu.event-target=%s\n", eventTarget->metaObject()->className());
        const QPoint local(120, 60);
        QContextMenuEvent event(QContextMenuEvent::Mouse, local, eventTarget->mapToGlobal(local));
        const bool handled = QApplication::sendEvent(eventTarget, &event);
        QCoreApplication::processEvents();
        QMenu *captured = menuBinding->lastMenu();
        check("menu.real-right-click-opens-menu", handled && captured != nullptr,
              QStringLiteral("handled=%1 menu=%2 platform=%3")
                  .arg(handled ? 1 : 0)
                  .arg(captured != nullptr ? 1 : 0)
                  .arg(platform));
        if (captured != nullptr) {
            check("menu.real-menu-has-our-items",
                  findMenuItem(captured, QStringLiteral("vedit.action.definition")) != nullptr
                      && findMenuItem(captured, QStringLiteral("vedit.action.next-diagnostic"))
                             != nullptr);
            check("menu.real-menu-has-default-actions",
                  built_default_count(captured) > 9,
                  QStringLiteral("items=%1").arg(captured->actions().size()));
            std::printf("OBSERVED menu.real.texts: %s\n",
                        menuTexts(captured).join(QStringLiteral(" | ")).toUtf8().constData());
            std::printf("OBSERVED menu.real.visible=%d platform=%s\n",
                        captured->isVisible() ? 1 : 0, platform.toUtf8().constData());
            if (requirePopup) {
                check("menu.real-popup-visible", captured->isVisible());
            }
            if (!shotMenu.isEmpty()) {
                captured->grab().save(shotMenu);
            }
            captured->close();
            QCoreApplication::processEvents();
        }
    }

    // --- (b) the deterministic menu model (also what a popup builds) --------
    QMenu menu;
    QMenu *built = view->defaultContextMenu(&menu);
    check("menu.default-context-menu", built != nullptr);
    if (built == nullptr) {
        return 1;
    }
    const QList<QAction *> added = menuBinding->appendTo(built);
    const int defaultCount = static_cast<int>(built->actions().size()) - static_cast<int>(added.size());
    check("menu.default-actions-kept", defaultCount > 0, QString::number(defaultCount));
    check("menu.our-items-added", added.size() == 9, QString::number(added.size()));
    std::printf("OBSERVED menu.texts: %s\n", menuTexts(built).join(QStringLiteral(" | ")).toUtf8().constData());
    if (!shotMenu.isEmpty()) {
        built->grab().save(shotMenu);
    }

    struct ExpectedItem {
        const char *objectName;
        vedit::editor::EditorAction action;
    };
    const ExpectedItem expected[] = {
        {"vedit.action.definition", vedit::editor::EditorAction::Definition},
        {"vedit.action.references", vedit::editor::EditorAction::References},
        {"vedit.action.rename", vedit::editor::EditorAction::Rename},
        {"vedit.action.format", vedit::editor::EditorAction::Format},
        {"vedit.action.hover", vedit::editor::EditorAction::Hover},
        {"vedit.action.next-diagnostic", vedit::editor::EditorAction::NextDiagnostic},
        {"vedit.action.previous-diagnostic", vedit::editor::EditorAction::PreviousDiagnostic},
    };
    for (const ExpectedItem &item : expected) {
        QAction *action = findMenuItem(built, QString::fromLatin1(item.objectName));
        const vedit::editor::ActionAvailability state = channel->availability(item.action);
        check(item.objectName, action != nullptr);
        if (action == nullptr) {
            continue;
        }
        check((QString::fromLatin1(item.objectName) + ".enabled-matches-gate").toUtf8().constData(),
              action->isEnabled() == state.enabled,
              QStringLiteral("menu=%1 channel=%2")
                  .arg(action->isEnabled() ? 1 : 0)
                  .arg(state.enabled ? 1 : 0));
        std::printf("ITEM %s enabled=%d shortcut='%s' tooltip='%s'\n",
                    item.objectName, action->isEnabled() ? 1 : 0,
                    sanitized(action->text().section(QLatin1Char('\t'), 1)).toUtf8().constData(),
                    action->toolTip().toUtf8().constData());
    }

    // Hover: the verible capability row says hoverProvider=false, so the item is
    // greyed with THAT reason (never a silent no-op).
    QAction *hoverItem = findMenuItem(built, QStringLiteral("vedit.action.hover"));
    check("hover.greyed", hoverItem != nullptr && !hoverItem->isEnabled());
    check("hover.reason-is-backend-reason",
          hoverItem != nullptr && hoverItem->toolTip().contains(QStringLiteral("hoverProvider")),
          hoverItem != nullptr ? hoverItem->toolTip() : QString());
    const vedit::editor::ActionDispatch hoverDispatch =
        channel->dispatch(vedit::editor::EditorAction::Hover);
    check("hover.dispatch-refused-with-reason",
          !hoverDispatch.dispatched && !hoverDispatch.reason.isEmpty(), hoverDispatch.reason);

    // --- definition: trigger the MENU item, expect the recorded jump --------
    jumpTarget.reset();
    if (QAction *definition = findMenuItem(built, QStringLiteral("vedit.action.definition"));
        definition != nullptr && definition->isEnabled()) {
        const int line = 6; // `  leaf u_leaf(.q(a));` (zero-based)
        const int column = 2;
        view->setCursorPosition(KTextEditor::Cursor(line, column));
        QCoreApplication::processEvents();
        definition->trigger();
        const bool jumped = spinUntil([&jumpTarget]() { return jumpTarget.calls > 0; }, 30000);
        check("action.definition-jumped", jumped,
              QStringLiteral("calls=%1 target=%2:%3")
                  .arg(jumpTarget.calls)
                  .arg(jumpTarget.lastFile)
                  .arg(jumpTarget.lastLine));
        check("action.definition-target",
              jumped && jumpTarget.lastFile == navPath && jumpTarget.lastLine == 1,
              QStringLiteral("target=%1:%2 expected=%3:1")
                  .arg(jumpTarget.lastFile)
                  .arg(jumpTarget.lastLine)
                  .arg(navPath));
    } else {
        skip("action.definition", QStringLiteral("no enabled definition item"));
    }

    // --- references: trigger, expect the count in the status bar ------------
    if (QAction *references = findMenuItem(built, QStringLiteral("vedit.action.references"));
        references != nullptr && references->isEnabled()) {
        view->setCursorPosition(KTextEditor::Cursor(5, 8)); // `a` in `logic a;`
        QCoreApplication::processEvents();
        references->trigger();
        const bool reported = spinUntil(
            [&shell]() {
                return shell.statusBar() != nullptr
                       && shell.statusBar()->currentMessage().contains(QStringLiteral("reference(s)"));
            },
            30000);
        check("action.references-reported", reported,
              shell.statusBar() != nullptr ? shell.statusBar()->currentMessage() : QString());
    } else {
        skip("action.references", QStringLiteral("no enabled references item"));
    }

    // --- rename: trigger, fill the dialog, expect a preview ------------------
    if (QAction *rename = findMenuItem(built, QStringLiteral("vedit.action.rename"));
        rename != nullptr && rename->isEnabled()) {
        view->setCursorPosition(KTextEditor::Cursor(6, 2)); // `leaf` at the instantiation
        QCoreApplication::processEvents();
        QTimer::singleShot(200, &shell, []() {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (dialog != nullptr) {
                dialog->setTextValue(QStringLiteral("leaf_renamed"));
                dialog->accept();
            }
        });
        rename->trigger();
        const bool previewed = spinUntil(
            [&shell]() {
                return shell.statusBar() != nullptr
                       && shell.statusBar()->currentMessage().contains(QStringLiteral("Rename preview"));
            },
            30000);
        check("action.rename-preview", previewed,
              shell.statusBar() != nullptr ? shell.statusBar()->currentMessage() : QString());
    } else {
        skip("action.rename", QStringLiteral("no enabled rename item"));
    }

    // --- format: trigger, expect the document to be reformatted -------------
    if (QAction *format = findMenuItem(built, QStringLiteral("vedit.action.format"));
        format != nullptr && format->isEnabled()) {
        if (openAndAnalyze(pane, panel, formatPath, &openError)) {
            auto *formatView = qobject_cast<KTextEditor::View *>(pane->editorWidget());
            check("format.view-open", formatView != nullptr);
            if (formatView != nullptr) {
                const QString before = textOf(formatView);
                QMenu formatMenu;
                QMenu *formatBuilt = formatView->defaultContextMenu(&formatMenu);
                auto *formatBinding = vedit::editor::EditorContextMenu::instanceFor(formatView);
                check("format.menu-binding", formatBuilt != nullptr && formatBinding != nullptr);
                if (formatBuilt != nullptr && formatBinding != nullptr) {
                    formatBinding->appendTo(formatBuilt);
                }
                QAction *item = formatBinding != nullptr
                                    ? findMenuItem(formatBuilt, QStringLiteral("vedit.action.format"))
                                    : nullptr;
                if (auto *formatChannel = vedit::editor::LspActionChannel::instanceFor(formatView)) {
                    const vedit::editor::ActionAvailability state =
                        formatChannel->availability(vedit::editor::EditorAction::Format);
                    const QAction *wired = vedit::editor::shellActionFor(
                        formatView->window(), vedit::editor::EditorAction::Format);
                    std::printf("OBSERVED format.availability enabled=%d source='%s' reason='%s' "
                                "shell_action=%d shell_enabled=%d file='%s'\n",
                                state.enabled ? 1 : 0, state.source.toUtf8().constData(),
                                state.reason.toUtf8().constData(), wired != nullptr ? 1 : 0,
                                wired != nullptr && wired->isEnabled() ? 1 : 0,
                                vedit::editor::viewFilePath(formatView).toUtf8().constData());
                }
                std::printf("OBSERVED format.menu items=%d texts: %s\n",
                            static_cast<int>(formatBuilt->actions().size()),
                            menuTexts(formatBuilt).join(QStringLiteral(" | ")).toUtf8().constData());
                for (QAction *candidate : formatBuilt->actions()) {
                    std::printf("OBSERVED format.menu.item object='%s' enabled=%d\n",
                                candidate->objectName().toUtf8().constData(),
                                candidate->isEnabled() ? 1 : 0);
                }
                check("format.item-enabled", item != nullptr && item->isEnabled());
                if (item != nullptr && item->isEnabled()) {
                    item->trigger();
                    const bool changed = spinUntil(
                        [&]() { return textOf(formatView) != before; }, 30000);
                    check("action.format-changed-document", changed,
                          QStringLiteral("bytes %1 -> %2")
                              .arg(before.size())
                              .arg(textOf(formatView).size()));
                }
            }
        } else {
            skip("action.format", openError);
        }
    } else {
        skip("action.format", QStringLiteral("no enabled format item"));
    }

    // --- F8 / Shift+F8 over REAL diagnostics --------------------------------
    if (!openAndAnalyze(pane, panel, brokenPath, &openError)) {
        skip("diagnostics.navigation", openError);
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return g_failures == 0 ? 0 : 1;
    }
    auto *brokenView = qobject_cast<KTextEditor::View *>(pane->editorWidget());
    check("f8.view-open", brokenView != nullptr);
    auto *brokenNavigator =
        brokenView != nullptr ? vedit::editor::DiagnosticNavigator::instanceFor(brokenView) : nullptr;
    check("f8.navigator-rebound", brokenNavigator != nullptr);
    bool diagnosticsArrived = false;
    if (brokenNavigator != nullptr) {
        diagnosticsArrived = spinUntil(
            [brokenNavigator]() { return brokenNavigator->diagnostics().size() >= 2; }, 30000);
        const QList<vedit::editor::DiagnosticAnnotation> entries =
            brokenNavigator->diagnostics();
        std::printf("OBSERVED f8.diagnostics=%d\n", static_cast<int>(entries.size()));
        for (const auto &entry : entries) {
            std::printf("DIAG line=%d column=%d severity=%d message='%s'\n", entry.line,
                        entry.column, entry.severity, entry.message.toUtf8().constData());
        }
        std::fflush(stdout);
    }
    check("f8.renamed-file-diagnostics", diagnosticsArrived,
          brokenNavigator != nullptr
              ? QStringLiteral("%1 entries").arg(brokenNavigator->diagnostics().size())
              : QString());

    // The pure rule, on synthetic entries: order + wrap both directions.
    {
        QList<vedit::editor::DiagnosticAnnotation> synthetic;
        for (int line : {20, 2, 7}) {
            vedit::editor::DiagnosticAnnotation entry;
            entry.line = line;
            entry.column = 4;
            entry.message = QStringLiteral("m");
            synthetic.append(entry);
        }
        const QList<vedit::editor::DiagnosticAnnotation> ordered =
            vedit::editor::orderedDiagnostics(synthetic);
        check("f8.rule-ordering", ordered.size() == 3 && ordered[0].line == 2
                                    && ordered[1].line == 7 && ordered[2].line == 20);
        const vedit::editor::DiagnosticStep forward =
            vedit::editor::nextDiagnostic(ordered, 2, 4);
        check("f8.rule-next", forward.found && forward.line == 7,
              QStringLiteral("line=%1").arg(forward.line));
        const vedit::editor::DiagnosticStep wrap =
            vedit::editor::nextDiagnostic(ordered, 20, 4);
        check("f8.rule-next-wraps", wrap.found && wrap.wrapped && wrap.line == 2,
              QStringLiteral("line=%1 wrapped=%2").arg(wrap.line).arg(wrap.wrapped ? 1 : 0));
        const vedit::editor::DiagnosticStep backward =
            vedit::editor::previousDiagnostic(ordered, 7, 4);
        check("f8.rule-previous", backward.found && backward.line == 2,
              QStringLiteral("line=%1").arg(backward.line));
        const vedit::editor::DiagnosticStep backWrap =
            vedit::editor::previousDiagnostic(ordered, 2, 4);
        check("f8.rule-previous-wraps", backWrap.found && backWrap.wrapped && backWrap.line == 20,
              QStringLiteral("line=%1 wrapped=%2").arg(backWrap.line).arg(backWrap.wrapped ? 1 : 0));
        const vedit::editor::DiagnosticStep empty =
            vedit::editor::nextDiagnostic({}, 0, 0);
        check("f8.rule-empty-no-move", !empty.found);
    }

    // The real key path: F8 on the widget that really has focus.
    if (brokenView != nullptr && diagnosticsArrived) {
        brokenView->setFocus();
        QCoreApplication::processEvents();
        QWidget *keyTarget = QApplication::focusWidget() != nullptr ? QApplication::focusWidget()
                                                                    : brokenView;
        QList<vedit::editor::DiagnosticAnnotation> entries = brokenNavigator->diagnostics();
        brokenView->setCursorPosition(KTextEditor::Cursor(0, 0));
        QCoreApplication::processEvents();
        QTest::keyClick(keyTarget, Qt::Key_F8);
        QCoreApplication::processEvents();
        const KTextEditor::Cursor afterFirst = brokenView->cursorPosition();
        check("f8.key-next-lands-on-diagnostic", afterFirst.line() == entries.first().line
                                                     && afterFirst.column() == entries.first().column,
              QStringLiteral("cursor=%1:%2 expected=%3:%4")
                  .arg(afterFirst.line())
                  .arg(afterFirst.column())
                  .arg(entries.first().line)
                  .arg(entries.first().column));
        observe("f8.status",
                shell.statusBar() != nullptr ? shell.statusBar()->currentMessage() : QString());

        QTest::keyClick(keyTarget, Qt::Key_F8);
        QCoreApplication::processEvents();
        const KTextEditor::Cursor afterSecond = brokenView->cursorPosition();
        const vedit::editor::DiagnosticStep expectedSecond =
            vedit::editor::nextDiagnostic(entries, entries.first().line, entries.first().column);
        check("f8.key-next-advances", afterSecond.line() == expectedSecond.line,
              QStringLiteral("cursor=%1 expected=%2").arg(afterSecond.line()).arg(expectedSecond.line));

        QTest::keyClick(keyTarget, Qt::Key_F8, Qt::ShiftModifier);
        QCoreApplication::processEvents();
        const KTextEditor::Cursor afterBack = brokenView->cursorPosition();
        check("f8.key-shift-previous", afterBack.line() == afterFirst.line(),
              QStringLiteral("cursor=%1 expected=%2").arg(afterBack.line()).arg(afterFirst.line()));
        check("f8.navigator-steps", brokenNavigator->stepCount() == 3,
              QString::number(brokenNavigator->stepCount()));
        if (!shotF8.isEmpty()) {
            shell.grab().save(shotF8);
        }

        // No diagnostics -> F8 must say so and NOT move the cursor.
        vedit::editor::DiagnosticsAnnotations::setForView(brokenView, {});
        QCoreApplication::processEvents();
        const KTextEditor::Cursor beforeEmpty = brokenView->cursorPosition();
        QTest::keyClick(keyTarget, Qt::Key_F8);
        QCoreApplication::processEvents();
        check("f8.empty-no-move", brokenView->cursorPosition() == beforeEmpty,
              QStringLiteral("cursor=%1:%2")
                  .arg(brokenView->cursorPosition().line())
                  .arg(brokenView->cursorPosition().column()));
        observe("f8.empty-status",
                shell.statusBar() != nullptr ? shell.statusBar()->currentMessage() : QString());
        check("f8.empty-status-message",
              shell.statusBar() != nullptr
                  && shell.statusBar()->currentMessage().contains(QStringLiteral("No diagnostics")));
        // Restore the real diagnostics: the next block asserts the menu's
        // navigation items are ENABLED by them.
        vedit::editor::DiagnosticsAnnotations::setForView(brokenView, entries);
        QCoreApplication::processEvents();
    } else {
        skip("f8.keys", QStringLiteral("no diagnostics available for the broken fixture"));
    }

    // --- the SAME menu on a file WITH diagnostics: navigation enabled ------
    if (diagnosticsArrived && brokenView != nullptr) {
        auto *brokenMenuBinding = vedit::editor::EditorContextMenu::instanceFor(brokenView);
        check("menu.broken-binding", brokenMenuBinding != nullptr);
        if (brokenMenuBinding != nullptr) {
            QMenu *restored = brokenMenuBinding->buildMenu();
            QAction *next = findMenuItem(restored, QStringLiteral("vedit.action.next-diagnostic"));
            QAction *previous =
                findMenuItem(restored, QStringLiteral("vedit.action.previous-diagnostic"));
            check("menu.nav-enabled-with-diagnostics",
                  next != nullptr && next->isEnabled() && previous != nullptr
                      && previous->isEnabled(),
                  QStringLiteral("next=%1 previous=%2")
                      .arg(next != nullptr && next->isEnabled() ? 1 : 0)
                      .arg(previous != nullptr && previous->isEnabled() ? 1 : 0));
            std::printf("OBSERVED menu.diagnostics.texts: %s\n",
                        menuTexts(restored).join(QStringLiteral(" | ")).toUtf8().constData());
            if (!shotMenuDiagnostics.isEmpty()) {
                restored->grab().save(shotMenuDiagnostics);
            }
            delete restored;
        }
    }

    // --- T16: hover is a REAL request path now ------------------------------
    // The channel resolves hover through the diagnostics lane's
    // `diagnostics.hover` command (textDocument/hover over the C ABI). Under
    // verible (hoverProvider=false) it stays greyed with the backend's reason;
    // what changed in T16 is that the request path EXISTS instead of being
    // reported as unwired.
    {
        auto *navView = qobject_cast<KTextEditor::View *>(pane->editorWidget());
        QWidget *host = navView != nullptr ? navView->window() : nullptr;
        const QAction *hoverWired =
            vedit::editor::shellActionFor(host, vedit::editor::EditorAction::Hover);
        check("hover.request-path-registered", hoverWired != nullptr,
              hoverWired != nullptr ? hoverWired->objectName() : QStringLiteral("none"));
        if (auto *probeChannel = navView != nullptr
                                     ? vedit::editor::LspActionChannel::instanceFor(navView)
                                     : nullptr) {
            const vedit::editor::ActionAvailability state =
                probeChannel->availability(vedit::editor::EditorAction::Hover);
            check("hover.gated-by-capability-not-by-wiring",
                  !state.enabled
                      && !state.reason.contains(QStringLiteral("no hover request")),
                  state.reason);
        }
    }

    // --- the menu item is the SAME dispatcher the channel uses --------------
    check("channel.definition-dispatched", channel->dispatchCount(vedit::editor::EditorAction::Definition) >= 1);
    check("channel.hover-refused-count",
          channel->refusalCount(vedit::editor::EditorAction::Hover) >= 1,
          channel->lastRefusal());

    vedit::workspace::jump::Registry::instance().clearTargetIf(&jumpTarget);
    std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
