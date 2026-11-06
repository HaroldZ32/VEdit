// Lane validator for the settings panel (Slice 3 / AC-S3.1, item 10.2).
//
//   vedit-workspace-settings-selftest --selfcheck [--screenshot <dir>]
//
// Drives the real panel against the REAL Rust settings store (through the C ABI,
// no stub): theme switch + persistence, key-binding edit + persistence, the
// conflict hint, the bad-configuration fallback/recovery, the three Preferences
// pages (tools + FIM persistence, a simulated restart, the write-only FIM API key
// whose secret never reaches settings.toml), and a non-blocking check that a
// large store load never runs on the GUI thread. `--screenshot <dir>` also
// renders the three pages to PNG for the evidence record.
#include "command_registry.h"
#include "format_options.h"
#include "lint_options.h"
#include "settings_client.h"
#include "settings_dialog.h"
#include "settings_keys.h"
#include "settings_panel.h"
#include "theme_manager.h"
#include "ui_scale.h"
#if defined(VEDIT_HAVE_DIAGNOSTICS_FORMAT)
#include "toolchain.h" // the REAL formatVerilog the product command uses
#endif

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QToolButton>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <thread>

#if defined(Q_OS_UNIX)
#include <sys/stat.h>
#include <sys/types.h>
#endif

using vedit::app::CommandRegistry;
using vedit::app::CommandSpec;
using vedit::app::JsonShortcutStore;
using vedit::app::ThemeManager;
using vedit::workspace::settings::FimValues;
using vedit::workspace::settings::SettingsClient;
using vedit::workspace::settings::SettingsDialog;
using vedit::workspace::settings::SettingsPanel;
using vedit::workspace::settings::ToolValues;

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
                detail.isEmpty() ? "" : " ", detail.toUtf8().constData());
    std::fflush(stdout);
}

// The first place two store snapshots differ (used as the check's detail, so a
// FAIL says WHAT changed instead of dumping two files).
QString firstDifference(const QString &left, const QString &right)
{
    const QStringList leftLines = left.split(QLatin1Char('\n'));
    const QStringList rightLines = right.split(QLatin1Char('\n'));
    const int count = std::max(leftLines.size(), rightLines.size());
    for (int index = 0; index < count; ++index) {
        const QString leftLine = index < leftLines.size() ? leftLines.at(index) : QString();
        const QString rightLine = index < rightLines.size() ? rightLines.at(index) : QString();
        if (leftLine != rightLine) {
            return QStringLiteral("line %1: before=[%2] after=[%3]")
                .arg(index + 1)
                .arg(leftLine, rightLine);
        }
    }
    return QString();
}

// Only the `lint.*` lines of a store snapshot: the cross-engine assertion is
// about the TWO lint configurations, not about `tools.lint_engine` (which the
// engine switch is supposed to change).
QString lintSection(const QString &text)
{
    QStringList lines;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("\"lint."))) {
            lines.append(line);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool writeText(const QString &path, const QByteArray &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text) == text.size();
}

void addCommands(CommandRegistry &commands)
{
    CommandSpec save;
    save.id = QStringLiteral("file.save");
    save.text = QStringLiteral("Save");
    save.menu = QStringLiteral("file");
    save.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+S"));
    save.order = 10;
    commands.add(save);

    CommandSpec split;
    split.id = QStringLiteral("view.split.horizontal");
    split.text = QStringLiteral("Split Horizontally");
    split.menu = QStringLiteral("view");
    split.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Shift+J"));
    split.order = 20;
    commands.add(split);

    CommandSpec panel;
    panel.id = QStringLiteral("view.toggle-settings");
    panel.text = QStringLiteral("Toggle Settings Panel");
    panel.menu = QStringLiteral("view");
    panel.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Alt+P"));
    panel.order = 30;
    commands.add(panel);

    // AC-10.2.2: the commands the other lanes register (project re-run, FIM)
    // must show up in the shortcut table without special-casing them there.
    CommandSpec rerun;
    rerun.id = QStringLiteral("lint.rerun");
    rerun.text = QStringLiteral("Re-run Project Lint");
    rerun.menu = QStringLiteral("lint");
    rerun.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Shift+L"));
    rerun.order = 40;
    commands.add(rerun);

    CommandSpec fim;
    fim.id = QStringLiteral("fim.trigger");
    fim.text = QStringLiteral("Fill in the Middle");
    fim.menu = QStringLiteral("edit");
    fim.defaultShortcut = QKeySequence(QStringLiteral("Alt+\\"));
    fim.order = 50;
    commands.add(fim);
}

bool tableHasCommand(QTableWidget *table, const QString &command)
{
    if (table == nullptr) {
        return false;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem *item = table->item(row, 0);
        if (item != nullptr && item->text().contains(command)) {
            return true;
        }
    }
    return false;
}

QString differentTheme(const ThemeManager &themes)
{
    const QString current = themes.currentTheme();
    for (const QString &name : themes.availableThemes()) {
        if (!name.isEmpty() && name != current) {
            return name;
        }
    }
    return {};
}

// A panel+registry pair as the shell would build it; `nullptr` when the build has
// no Rust settings ABI (the checks are then skipped, never faked).
struct Harness {
    ThemeManager themes;
    QObject owner;
    CommandRegistry commands{&owner};
    std::unique_ptr<JsonShortcutStore> store;
    std::unique_ptr<SettingsPanel> panel;
};

bool buildHarness(Harness *harness, const QString &settingsPath, const QString &shortcutPath)
{
    harness->store = std::make_unique<JsonShortcutStore>(shortcutPath);
    QString error;
    harness->store->load(&error);
    harness->commands.setStore(harness->store.get());
    addCommands(harness->commands);
    // What the shell does at startup before a session exists: apply the default
    // theme. The panel then overrides it with the stored one, if any.
    QString themeError;
    harness->themes.applyTheme(QString(), &themeError);
    harness->panel = std::make_unique<SettingsPanel>(&harness->themes, &harness->commands,
                                                     settingsPath);
    return harness->panel->flush();
}

void checkPanelAndPersistence(const QString &settingsPath, const QString &shortcutPath)
{
    if (!SettingsClient::ffiAvailable()) {
        check("settings.ffi.available", false, "lane built without the vd-ffi settings ABI");
        return;
    }
    check("settings.ffi.available", true);

    Harness harness;
    check("settings.panel.loaded", buildHarness(&harness, settingsPath, shortcutPath));
    SettingsPanel *panel = harness.panel.get();

    check("settings.panel.object-name", panel->objectName() == QStringLiteral("settings-panel"));
    check("settings.theme.list", !harness.themes.availableThemes().isEmpty(),
          QStringLiteral("%1 theme(s)").arg(harness.themes.availableThemes().size()));
    check("settings.theme.initial", !harness.themes.currentTheme().isEmpty(),
          harness.themes.currentTheme());
    check("settings.keys.rows", panel->keyTable()->rowCount() == 5,
          QStringLiteral("rows=%1").arg(panel->keyTable()->rowCount()));
    check("settings.keys.new-commands",
          tableHasCommand(panel->keyTable(), QStringLiteral("lint.rerun"))
              && tableHasCommand(panel->keyTable(), QStringLiteral("fim.trigger")),
          QStringLiteral("lint.rerun / fim.trigger must be listed"));

    const QString chosen = differentTheme(harness.themes);
    if (chosen.isEmpty()) {
        check("settings.theme.applied", false, "no second theme available in the repository");
        return;
    }
    panel->setTheme(chosen);
    check("settings.theme.applied", panel->flush() && harness.themes.currentTheme() == chosen,
          harness.themes.currentTheme());
    check("settings.theme.status", panel->statusText().contains(QStringLiteral("Stored by the Rust service")),
          panel->statusText());
    const QString afterTheme = readText(settingsPath);
    check("settings.theme.persisted",
          afterTheme.contains(QStringLiteral("theme = \"%1\"").arg(chosen)),
          afterTheme.left(80));

    // Simulated restart: fresh managers and a fresh panel read the SAME file.
    Harness restarted;
    check("settings.restart.loaded",
          buildHarness(&restarted, settingsPath, shortcutPath + QStringLiteral(".restart")));
    check("settings.theme.restart", restarted.themes.currentTheme() == chosen,
          restarted.themes.currentTheme());

    // Key-binding edit through the panel, then persist + mirror.
    panel->setShortcut(QStringLiteral("view.split.horizontal"),
                       QKeySequence(QStringLiteral("Ctrl+Alt+J")));
    check("settings.keys.flush", panel->flush());
    const QAction *splitAction = harness.commands.action(QStringLiteral("view.split.horizontal"));
    check("settings.keys.applied",
          splitAction != nullptr
              && splitAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+J")),
          splitAction != nullptr ? splitAction->shortcut().toString() : QStringLiteral("<missing>"));
    const QString afterKeys = readText(settingsPath);
    check("settings.keys.persisted",
          afterKeys.contains(QStringLiteral("keymap.view.split.horizontal"))
              && afterKeys.contains(QStringLiteral("Ctrl+Alt+J")),
          afterKeys.left(160));
    check("settings.keys.mirror", readText(shortcutPath).contains(QStringLiteral("view.split.horizontal")));
    // The shell loads shortcuts.json at startup, before any panel exists: the
    // mirror alone must already carry the new binding.
    JsonShortcutStore mirror(shortcutPath);
    QString mirrorError;
    mirror.load(&mirrorError);
    check("settings.keys.mirror-restart",
          mirror.shortcut(QStringLiteral("view.split.horizontal"), QKeySequence())
              == QKeySequence(QStringLiteral("Ctrl+Alt+J")),
          mirrorError);

    // Restart with an EMPTY Qt shortcut file: the Rust store alone must restore it.
    Harness fromRust;
    check("settings.restart.keys-loaded",
          buildHarness(&fromRust, settingsPath, shortcutPath + QStringLiteral(".empty")));
    const QAction *restoredAction =
        fromRust.commands.action(QStringLiteral("view.split.horizontal"));
    check("settings.keys.restart",
          restoredAction != nullptr
              && restoredAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+J")),
          restoredAction != nullptr ? restoredAction->shortcut().toString() : QStringLiteral("<missing>"));

    // RM-5b: a USER edit can no longer CREATE a duplicate. Qt drops BOTH actions
    // of an ambiguous sequence, so the page refuses the assignment and names the
    // command that already owns it (the old behaviour stored it and left the user
    // with two dead keys).
    const QKeySequence saveKey(QStringLiteral("Ctrl+S"));
    const QKeySequence panelBefore =
        fromRust.commands.action(QStringLiteral("view.toggle-settings"))->shortcut();
    fromRust.panel->setShortcut(QStringLiteral("view.toggle-settings"), saveKey);
    check("settings.conflict.refused",
          fromRust.commands.action(QStringLiteral("view.toggle-settings"))->shortcut()
              == panelBefore,
          fromRust.commands.action(QStringLiteral("view.toggle-settings"))->shortcut().toString());
    check("settings.conflict.refused-names-owner",
          fromRust.panel->statusText().contains(QStringLiteral("file.save")),
          fromRust.panel->statusText());

    // A conflict that ALREADY exists (a hand-edited / legacy store) is still
    // detected and shown in red: the guard is on edits, the report is on state.
    fromRust.commands.store()->setShortcut(QStringLiteral("view.toggle-settings"), saveKey);
    fromRust.commands.refreshShortcuts();
    fromRust.panel->reload();
    check("settings.conflict.flush", fromRust.panel->flush());
    const QString conflictText = fromRust.panel->conflicts().join(QStringLiteral(" | "));
    check("settings.conflict.detected", fromRust.panel->conflictCount() >= 1, conflictText);
    check("settings.conflict.names",
          conflictText.contains(QStringLiteral("file.save"))
              && conflictText.contains(QStringLiteral("view.toggle-settings"))
              && conflictText.contains(QStringLiteral("Ctrl+S")),
          conflictText);
    check("settings.conflict.visible",
          fromRust.panel->conflictLabel()->text().contains(QStringLiteral("Conflicting")),
          fromRust.panel->conflictLabel()->text());
    // Clean the legacy duplicate again so the later checks start from a known state.
    fromRust.commands.store()->clearShortcut(QStringLiteral("view.toggle-settings"));
    fromRust.commands.refreshShortcuts();

    // Bad configuration: the file no longer parses. The panel must show the store's
    // diagnostics and keep a usable theme (defaults where needed), never crash.
    check("settings.badconfig.write", writeText(settingsPath, QByteArray("theme = \n[unclosed\n")));
    fromRust.panel->reload();
    check("settings.badconfig.flush", fromRust.panel->flush());
    const QString diagnostics = fromRust.panel->diagnostics().join(QLatin1Char('\n'));
    check("settings.badconfig.warning", diagnostics.contains(QStringLiteral("invalid TOML")),
          diagnostics.left(140));
    check("settings.badconfig.visible",
          fromRust.panel->diagnosticsView()->toPlainText().contains(QStringLiteral("invalid TOML")));
    check("settings.badconfig.theme-valid", !fromRust.themes.currentTheme().isEmpty(),
          fromRust.themes.currentTheme());

    // Recovery: the UI writes a valid file again and the diagnostics clear.
    fromRust.panel->setTheme(chosen);
    check("settings.badconfig.recover-flush", fromRust.panel->flush());
    fromRust.panel->reload();
    check("settings.badconfig.recovered",
          fromRust.panel->flush() && fromRust.panel->diagnostics().isEmpty(),
          fromRust.panel->diagnostics().join(QLatin1Char('|')));
    check("settings.badconfig.file-valid",
          readText(settingsPath).contains(QStringLiteral("theme = \"%1\"").arg(chosen)));
}

// The GUI thread must never parse the store: a large file is handed to the Rust
// job and the event loop keeps ticking while it is processed.
void checkNonBlocking()
{
    if (!SettingsClient::ffiAvailable()) {
        check("settings.nonblocking.start-returns-fast", false,
              "lane built without the vd-ffi settings ABI");
        return;
    }
    constexpr int kEntries = 120000;
    QTemporaryDir temp;
    check("settings.nonblocking.temp-dir", temp.isValid());
    if (!temp.isValid()) {
        return;
    }
    const QString path = temp.path() + QStringLiteral("/big-settings.toml");
    QByteArray body;
    body.reserve(kEntries * 18);
    for (int index = 0; index < kEntries; ++index) {
        body.append("k");
        body.append(QByteArray::number(index));
        body.append(" = \"v\"\n");
    }
    check("settings.nonblocking.write-fixture", writeText(path, body));

    SettingsClient client(path);
    bool delivered = false;
    int entries = -1;
    QElapsedTimer start;
    start.start();
    client.load([&delivered, &entries](vedit::workspace::settings::Snapshot snapshot) {
        delivered = snapshot.completed;
        entries = snapshot.entries.size();
    });
    const qint64 startMs = start.elapsed();
    check("settings.nonblocking.start-returns-fast", startMs < 50,
          QStringLiteral("start took %1 ms for a %2 KB store").arg(startMs).arg(body.size() / 1024));

    int ticks = 0;
    QTimer ticker;
    ticker.setInterval(20);
    QObject::connect(&ticker, &QTimer::timeout, [&ticks]() { ++ticks; });
    ticker.start();
    QElapsedTimer window;
    window.start();
    while (window.elapsed() < 150) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ticker.stop();
    check("settings.nonblocking.event-loop-ticks", ticks >= 2,
          QStringLiteral("ticks=%1 poles=%2")
              .arg(ticks)
              .arg(static_cast<unsigned long long>(client.pollRounds())));
    check("settings.nonblocking.flush", client.flush());
    check("settings.nonblocking.result", delivered && entries == kEntries,
          QStringLiteral("entries=%1 delivered=%2").arg(entries).arg(delivered ? 1 : 0));
}

// --- item 10.2: the three Preferences pages --------------------------------

const char *const kSecret = "sk-test-DO-NOT-LEAK-123456";

ToolValues sampleTools()
{
    namespace keys = vedit::workspace::settings::keys;
    ToolValues tools;
    tools.lspBackend = QString::fromLatin1(keys::kLspBackendSlang);
    tools.lintEngine = QString::fromLatin1(keys::kLintEngineVerible);
    tools.veribleLs = QStringLiteral("/opt/verible/bin/verible-verilog-ls");
    tools.slang = QStringLiteral("/opt/slang/bin/slang");
    tools.slangServer = QStringLiteral("/opt/slang/bin/slang-server");
    tools.veribleFormat = QStringLiteral("/opt/verible/bin/verible-verilog-format");
    tools.veribleRules = QStringLiteral("/proj/.rules.verible_lint");
    tools.includeDirs = QStringLiteral("rtl\nrtl/include");
    tools.defines = QStringLiteral("WIDTH=8\nSIM");
    return tools;
}

FimValues sampleFim()
{
    namespace keys = vedit::workspace::settings::keys;
    FimValues fim;
    fim.enabled = true;
    fim.protocol = QString::fromLatin1(keys::kFimProtocolLlamaCpp);
    fim.endpoint = QStringLiteral("http://127.0.0.1:8080");
    fim.model = QStringLiteral("qwen");
    fim.timeoutMs = 2500;
    fim.maxTokens = 128;
    fim.ghostText = true;
    return fim;
}

bool sameTools(const ToolValues &left, const ToolValues &right)
{
    return left.lspBackend == right.lspBackend && left.lintEngine == right.lintEngine
        && left.veribleLs == right.veribleLs && left.slang == right.slang
        && left.slangServer == right.slangServer && left.veribleFormat == right.veribleFormat
        && left.veribleRules == right.veribleRules && left.includeDirs == right.includeDirs
        && left.defines == right.defines;
}

bool sameFim(const FimValues &left, const FimValues &right)
{
    return left.enabled == right.enabled && left.protocol == right.protocol
        && left.endpoint == right.endpoint && left.model == right.model
        && left.timeoutMs == right.timeoutMs && left.maxTokens == right.maxTokens
        && left.ghostText == right.ghostText;
}

// The process umask, so a failing run says what it ran under. The key writer
// must NOT depend on it: it creates the file 0600 at creation.
QString ambientUmask()
{
#if defined(Q_OS_UNIX)
    const mode_t current = ::umask(0);
    ::umask(current);
    return QString::number(static_cast<int>(current), 8).rightJustified(3, QLatin1Char('0'));
#else
    return QStringLiteral("n/a");
#endif
}

// Every directory entry whose name starts with the key file name: the writer must
// leave exactly one, and it must not be a temporary file.
QStringList keyFilesIn(const QString &directory)
{
    QStringList names;
    for (const QString &name : QDir(directory).entryList(QDir::Files | QDir::Hidden)) {
        if (name.startsWith(QStringLiteral("fim_api_key"))) {
            names.append(name);
        }
    }
    return names;
}

bool loosePermissions(const QString &path)
{
    const QFileDevice::Permissions loose =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    return (QFile::permissions(path) & loose) != 0;
}

// Exactly owner read+write and nothing else. Qt reports the owner bits twice
// (ReadOwner and the legacy ReadUser flag), so the assertion is written as
// "owner rw present, every other class and every exec bit absent" instead of a
// single magic constant.
bool exactlyOwnerReadWrite(const QString &path)
{
    const QFileDevice::Permissions mode = QFile::permissions(path);
    const QFileDevice::Permissions ownerReadWrite =
        QFileDevice::ReadUser | QFileDevice::WriteUser;
    const QFileDevice::Permissions anythingElse =
        QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ReadGroup
        | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther
        | QFileDevice::WriteOther | QFileDevice::ExeOther;
    return (mode & ownerReadWrite) == ownerReadWrite && (mode & anythingElse) == 0;
}

void checkPages(const QString &directory)
{
    namespace keys = vedit::workspace::settings::keys;
    const QString settingsPath = directory + QStringLiteral("/pages-settings.toml");
    const QString shortcutPath = directory + QStringLiteral("/pages-shortcuts.json");
    if (!SettingsClient::ffiAvailable()) {
        check("settings.pages.ffi.available", false, "lane built without the vd-ffi settings ABI");
        return;
    }
    check("settings.pages.ffi.available", true);

    Harness harness;
    check("settings.pages.loaded", buildHarness(&harness, settingsPath, shortcutPath));
    SettingsPanel *panel = harness.panel.get();

    // AC-10.2.1: Preferences carries the three pages in a QTabWidget.
    check("settings.pages.tabs-container",
          panel->tabs() != nullptr && panel->tabs()->count() == 6,
          panel->tabs() != nullptr ? QStringLiteral("count=%1").arg(panel->tabs()->count())
                                   : QStringLiteral("<no tab widget>"));
    // AC-4.7: the titles are asserted through the translation keys (and the
    // panel's own class context), so the same three coverage points hold when a
    // translator is installed.
    const auto panelText = [](const char *source) {
        return QCoreApplication::translate("vedit::workspace::settings::SettingsPanel", source);
    };
    check("settings.pages.tabs-titles",
          panel->tabs() != nullptr && panel->tabs()->tabText(0) == panelText("Shortcuts")
              && panel->tabs()->tabText(1) == panelText("Tools")
              && panel->tabs()->tabText(2) == panelText("FIM")
              && panel->tabs()->tabText(3) == panelText("Appearance")
              && panel->tabs()->tabText(4) == panelText("Lint")
              && panel->tabs()->tabText(5) == panelText("Format"));
    check("settings.pages.tabs-object-names",
          panel->shortcutsPage() != nullptr && panel->toolsPage() != nullptr
              && panel->fimPage() != nullptr
              && panel->shortcutsPage()->objectName() == QStringLiteral("settings-page-shortcuts")
              && panel->toolsPage()->objectName() == QStringLiteral("settings-page-tools")
              && panel->fimPage()->objectName() == QStringLiteral("settings-page-fim")
              && panel->appearancePage() != nullptr && panel->lintPage() != nullptr
              && panel->formatPage() != nullptr
              && panel->appearancePage()->objectName() == QStringLiteral("settings-page-appearance")
              && panel->lintPage()->objectName() == QStringLiteral("settings-page-lint")
              && panel->formatPage()->objectName() == QStringLiteral("settings-page-format"));

    // AC-10.2.3: every field of the tools page exists.
    check("settings.pages.tools-form", panel->toolsForm() != nullptr);
    check("settings.pages.tools-fields",
          panel->lspBackendCombo() != nullptr && panel->lspBackendCombo()->count() == 2
              && panel->lintEngineCombo() != nullptr && panel->lintEngineCombo()->count() == 2
              && panel->veribleLsEdit() != nullptr && panel->slangEdit() != nullptr
              && panel->slangServerEdit() != nullptr && panel->veribleFormatEdit() != nullptr
              && panel->veribleRulesEdit() != nullptr && panel->includeDirsEdit() != nullptr
              && panel->definesEdit() != nullptr);

    // AC-10.2.4/10.2.5: every field of the FIM page exists, and the key field is
    // write-only.
    check("settings.pages.fim-fields",
          panel->fimEnabledCheck() != nullptr && panel->fimProtocolCombo() != nullptr
              && panel->fimEndpointEdit() != nullptr && panel->fimModelEdit() != nullptr
              && panel->fimTimeoutSpin() != nullptr && panel->fimApiKeyEdit() != nullptr
              && panel->fimGhostCheck() != nullptr);
    check("settings.pages.fim-key-write-only",
          panel->fimApiKeyEdit()->echoMode() == QLineEdit::Password
              && panel->fimApiKeyEdit()->text().isEmpty());

    // AC-1.1 / AC-2.5 / AC-10.2.3: the choices and paths are stored, and a fresh
    // panel (a restart) reads them back.
    const ToolValues tools = sampleTools();
    panel->setToolValues(tools);
    check("settings.pages.tools-flush", panel->flush());
    const QString toolsStored = readText(settingsPath);
    // The store is a flat TOML table, so a dotted key is written QUOTED: a bare
    // `tools.lsp_backend` would be a nested table to any TOML reader.
    check("settings.pages.tools-stored",
          toolsStored.contains(QStringLiteral("\"tools.lsp_backend\" = \"slang\""))
              && toolsStored.contains(QStringLiteral("\"tools.lint_engine\" = \"verible\""))
              && toolsStored.contains(QStringLiteral("\"tools.verible_ls\""))
              && toolsStored.contains(QStringLiteral("\"tools.verible_rules\""))
              && toolsStored.contains(QStringLiteral("\"tools.include_dirs\""))
              && toolsStored.contains(QStringLiteral("\"tools.defines\"")),
          toolsStored.left(160));
    Harness restarted;
    check("settings.pages.tools-restart-load", buildHarness(&restarted, settingsPath, shortcutPath + QStringLiteral(".restart")));
    check("settings.pages.tools-restart", sameTools(restarted.panel->toolValues(), tools),
          restarted.panel->toolValues().includeDirs);
    check("settings.pages.backend-selected",
          restarted.panel->lspBackendCombo()->currentData().toString()
              == QString::fromLatin1(keys::kLspBackendSlang));
    check("settings.pages.include-dirs-widget",
          restarted.panel->includeDirsEdit()->toPlainText() == tools.includeDirs);

    // AC-10.2.4: the FIM values persist and reach the next session.
    const FimValues fim = sampleFim();
    restarted.panel->setFimValues(fim);
    check("settings.pages.fim-flush", restarted.panel->flush());
    const QString fimStored = readText(settingsPath);
    check("settings.pages.fim-stored",
          fimStored.contains(QStringLiteral("\"fim.enabled\" = \"true\""))
              && fimStored.contains(QStringLiteral("\"fim.protocol\" = \"llama_cpp\""))
              && fimStored.contains(
                  QStringLiteral("\"fim.endpoint\" = \"http://127.0.0.1:8080\""))
              && fimStored.contains(QStringLiteral("\"fim.timeout_ms\" = \"2500\""))
              && fimStored.contains(QStringLiteral("\"fim.ghost_text\" = \"true\"")),
          fimStored.left(160));
    Harness fimRestart;
    check("settings.pages.fim-restart-load", buildHarness(&fimRestart, settingsPath, shortcutPath + QStringLiteral(".fim")));
    check("settings.pages.fim-restart", sameFim(fimRestart.panel->fimValues(), fim));

    // AC-10.2.5: the key is written owner-only, never to settings.toml, never
    // echoed back. FimValues has no key field, so the only programmatic way in is
    // the explicit writer (the same one a user typing in the field goes through).
    check("settings.pages.key-store",
          fimRestart.panel->storeFimApiKey(QString::fromLatin1(kSecret)));
    check("settings.pages.key-flush", fimRestart.panel->flush());
    const QString keyPath = fimRestart.panel->fimKeyFilePath();
    check("settings.pages.key-path", keyPath == directory + QStringLiteral("/fim_api_key"), keyPath);
    const QString afterKey = readText(settingsPath);
    check("settings.pages.key-not-in-store",
          !afterKey.contains(QString::fromLatin1(kSecret))
              && !afterKey.contains(QStringLiteral("fim.api_key")),
          afterKey.left(200));
    check("settings.pages.key-file-written", QFileInfo::exists(keyPath));
    check("settings.pages.key-file-0600", !loosePermissions(keyPath),
          QStringLiteral("mode-loose=%1").arg(loosePermissions(keyPath) ? 1 : 0));
    // Exactly 0600 - not "0600 after a chmod". The acceptance harness also runs
    // this binary under `umask 000`, where a writer that relied on the umask
    // would leave 0666 and fail here.
    check("settings.pages.key-file-mode-exact", exactlyOwnerReadWrite(keyPath),
          QStringLiteral("mode=%1 umask=%2")
              .arg(QString::number(static_cast<int>(QFile::permissions(keyPath)), 8),
                   ambientUmask()));
    check("settings.pages.key-no-temp-debris",
          keyFilesIn(directory) == QStringList{QStringLiteral("fim_api_key")},
          keyFilesIn(directory).join(QLatin1Char(',')));
    check("settings.pages.key-file-content", readText(keyPath) == QString::fromLatin1(kSecret));
    check("settings.pages.key-no-echo", fimRestart.panel->fimApiKeyEdit()->text().isEmpty());
    check("settings.pages.key-source", fimRestart.panel->fimKeySource() == QStringLiteral("user-file"),
          fimRestart.panel->fimKeySource());
    check("settings.pages.key-stored", fimRestart.panel->fimKeyStored());
    check("settings.pages.key-label",
          fimRestart.panel->fimKeySourceLabel()->text().contains(keyPath));
    Harness keyed;
    check("settings.pages.key-restart-load", buildHarness(&keyed, settingsPath, shortcutPath + QStringLiteral(".keyed")));
    check("settings.pages.key-restart-source", keyed.panel->fimKeySource() == QStringLiteral("user-file"));
    check("settings.pages.key-restart-no-echo", keyed.panel->fimApiKeyEdit()->text().isEmpty());
    // A pre-existing world-readable key file must be REPLACED atomically, not
    // reused or chmod-ed in place.
    check("settings.pages.key-loose.write",
          writeText(keyPath, QByteArrayLiteral("old-key"))
              && QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                    | QFileDevice::ReadGroup
                                                    | QFileDevice::ReadOther));
    check("settings.pages.key-loose.store",
          fimRestart.panel->storeFimApiKey(QString::fromLatin1(kSecret)));
    check("settings.pages.key-loose.replaced-0600",
          exactlyOwnerReadWrite(keyPath) && readText(keyPath) == QString::fromLatin1(kSecret),
          QStringLiteral("mode=%1")
              .arg(QString::number(static_cast<int>(QFile::permissions(keyPath)), 8)));
    check("settings.pages.key-loose.no-debris",
          keyFilesIn(directory) == QStringList{QStringLiteral("fim_api_key")},
          keyFilesIn(directory).join(QLatin1Char(',')));
    // Stale temps from an earlier crash are swept by the next save; a temp whose
    // pid is still alive belongs to another instance and must survive.
    const QString staleTemp = directory + QStringLiteral("/fim_api_key.tmp.2147483647.0");
    const QString liveTemp = directory
        + QStringLiteral("/fim_api_key.tmp.%1.0").arg(QCoreApplication::applicationPid());
    check("settings.pages.key-sweep.fixtures",
          writeText(staleTemp, QByteArrayLiteral("stale-secret"))
              && writeText(liveTemp, QByteArrayLiteral("live-secret")));
    check("settings.pages.key-sweep.store",
          fimRestart.panel->storeFimApiKey(QString::fromLatin1(kSecret)));
    check("settings.pages.key-sweep.stale-removed", !QFileInfo::exists(staleTemp));
    check("settings.pages.key-sweep.live-kept", QFileInfo::exists(liveTemp));
    // Do not leave the test fixture behind.
    check("settings.pages.key-sweep.cleanup", QFile::remove(liveTemp));

    // A page load/revert must never fill the write-only field: no secret can be
    // echoed into the UI by any value struct.
    fimRestart.panel->fimApiKeyEdit()->setText(QString::fromLatin1(kSecret));
    fimRestart.panel->setFimValues(fim);
    check("settings.pages.key-field-never-filled",
          fimRestart.panel->fimApiKeyEdit()->text().isEmpty());

    keyed.panel->clearFimKey();
    check("settings.pages.key-cleared",
          !QFileInfo::exists(keyPath) && keyed.panel->fimKeySource() == QStringLiteral("none"));

    // A plaintext key in the store (an older build or a hand edit) is ignored,
    // reported, and scrubbed by the next save.
    // Two hand-edit spellings, both safe: the QUOTED form reaches the flat store
    // and is scrubbed by this lane; the BARE form is a nested table to TOML and
    // never enters the store at all (the Rust loader reports it).
    check("settings.pages.plain.flush-others", fimRestart.panel->flush() && keyed.panel->flush());
    const QString plainPath = directory + QStringLiteral("/plain-settings.toml");
    QString withPlaintext = readText(settingsPath);
    withPlaintext += QStringLiteral("\"fim.api_key\" = \"plaintext-secret\"\n");
    check("settings.pages.plain.write", writeText(plainPath, withPlaintext.toUtf8()));
    Harness plain;
    check("settings.pages.plain.load", buildHarness(&plain, plainPath, shortcutPath + QStringLiteral(".plain")));
    check("settings.pages.plain.ignored",
          plain.panel->fimKeySource() == QStringLiteral("none") && !plain.panel->fimKeyStored(),
          plain.panel->fimKeySource());
    check("settings.pages.plain.diagnostic",
          plain.panel->diagnostics().join(QLatin1Char('|')).contains(QStringLiteral("fim.api_key")),
          plain.panel->diagnostics().join(QLatin1Char('|')));
    plain.panel->setToolValues(plain.panel->toolValues());
    check("settings.pages.plain.scrubbed",
          plain.panel->flush() && !readText(plainPath).contains(QStringLiteral("plaintext-secret")));
    check("settings.pages.plain.flush-after", plain.panel->flush());

    const QString nestedPath = directory + QStringLiteral("/nested-settings.toml");
    QString withNested = readText(plainPath);
    withNested += QStringLiteral("fim.api_key = \"nested-secret\"\n");
    check("settings.pages.nested.write", writeText(nestedPath, withNested.toUtf8()));
    Harness nested;
    check("settings.pages.nested.load", buildHarness(&nested, nestedPath, shortcutPath + QStringLiteral(".nested")));
    check("settings.pages.nested.rejected",
          !nested.panel->fimKeyStored()
              && nested.panel->diagnostics().join(QLatin1Char('|')).contains(QStringLiteral("fim")),
          nested.panel->diagnostics().join(QLatin1Char('|')));
    nested.panel->setToolValues(nested.panel->toolValues());
    check("settings.pages.nested.scrubbed",
          nested.panel->flush() && !readText(nestedPath).contains(QStringLiteral("nested-secret")));

    // Deferred preview (the dialog's mode): Cancel writes nothing, Apply does.
    const QString dialogPath = directory + QStringLiteral("/dialog-settings.toml");
    Harness dialogHarness;
    check("settings.pages.dialog-harness",
          buildHarness(&dialogHarness, dialogPath, shortcutPath + QStringLiteral(".dialog")));
    {
        SettingsDialog dialog(&dialogHarness.themes, &dialogHarness.commands, dialogPath, nullptr);
        check("settings.pages.dialog-tabs",
              dialog.panel() != nullptr && dialog.panel()->tabs() != nullptr
                  && dialog.panel()->tabs()->count() == 6);
        check("settings.pages.dialog-deferred", dialog.panel()->deferredPersistence());
        check("settings.pages.dialog-load", dialog.panel()->flush());
        dialog.panel()->setFimValues(sampleFim());
        // Simulate the user typing into the write-only field; there is no struct
        // field that could carry the secret.
        dialog.panel()->fimApiKeyEdit()->setText(QString::fromLatin1(kSecret));
        check("settings.pages.dialog-preview", dialog.hasPendingChanges());
        check("settings.pages.dialog-preview-no-key-file",
              !QFileInfo::exists(dialog.panel()->fimKeyFilePath()));
        check("settings.pages.dialog-preview-no-store",
              !readText(dialogPath).contains(QString::fromLatin1(kSecret))
                  && !readText(dialogPath).contains(QStringLiteral("fim.endpoint")));
        dialog.reject();
        check("settings.pages.dialog-cancel-no-key-file",
              !QFileInfo::exists(dialog.panel()->fimKeyFilePath()));
        check("settings.pages.dialog-cancel-no-key-echo",
              dialog.panel()->fimApiKeyEdit()->text().isEmpty());
    }
    {
        SettingsDialog dialog(&dialogHarness.themes, &dialogHarness.commands, dialogPath, nullptr);
        check("settings.pages.dialog-apply-load", dialog.panel()->flush());
        dialog.panel()->setFimValues(sampleFim());
        dialog.panel()->fimApiKeyEdit()->setText(QString::fromLatin1(kSecret));
        dialog.applyPending();
        check("settings.pages.dialog-apply-flush", dialog.panel()->flush());
        check("settings.pages.dialog-apply-key-file",
              QFileInfo::exists(dialog.panel()->fimKeyFilePath()));
        check("settings.pages.dialog-apply-store",
              readText(dialogPath).contains(QStringLiteral("fim.endpoint"))
                  && !readText(dialogPath).contains(QString::fromLatin1(kSecret)));
    }
}

// The contract asks for a record that the typed key never lands in
// settings.toml. This writes exactly that record (and removes the key file
// afterwards, so no secret-shaped file stays in the evidence tree).
// --- UX batch 2 / A4: appearance, format and lint ---------------------------

namespace a4 = vedit::workspace::settings;

bool setKnobValue(SettingsPanel *panel, const char *key, const QString &value)
{
    QWidget *widget = panel->formatKnobWidget(QString::fromLatin1(key));
    if (widget == nullptr) {
        return false;
    }
    if (auto *spin = qobject_cast<QSpinBox *>(widget)) {
        spin->setValue(value.toInt());
        return true;
    }
    if (auto *combo = qobject_cast<QComboBox *>(widget)) {
        const int index = combo->findData(value);
        if (index < 0) {
            return false;
        }
        combo->setCurrentIndex(index);
        return true;
    }
    return false;
}

// One value per format knob, and the argv the fully-populated page must produce.
a4::FormatValues fullFormatValues()
{
    a4::FormatValues values;
    for (const a4::FormatKnobSpec &knob : a4::kFormatKnobs) {
        QString value;
        switch (knob.kind) {
        case a4::FormatKnobKind::Integer:
            value = QString::fromLatin1(knob.flag) == QStringLiteral("column_limit")
                ? QStringLiteral("120")
                : QStringLiteral("4");
            break;
        case a4::FormatKnobKind::Boolean:
            value = QStringLiteral("true");
            break;
        case a4::FormatKnobKind::LineTerminator:
            value = QStringLiteral("CRLF");
            break;
        case a4::FormatKnobKind::Alignment:
            value = QStringLiteral("flush-left");
            break;
        case a4::FormatKnobKind::Indentation:
            value = QStringLiteral("indent");
            break;
        }
        values.insert(QString::fromLatin1(knob.key), value);
    }
    return values;
}

QStringList expectedFullFormatArguments()
{
    QStringList expected;
    const a4::FormatValues values = fullFormatValues();
    for (const a4::FormatKnobSpec &knob : a4::kFormatKnobs) {
        expected.append(QStringLiteral("--%1=%2")
                            .arg(QString::fromLatin1(knob.flag),
                                 values.value(QString::fromLatin1(knob.key))));
    }
    return expected;
}

void checkAppearancePage(const QString &directory)
{
    using a4::SettingsClient;
    const QString settingsPath = directory + QStringLiteral("/a4-appearance.toml");
    const QString shortcutPath = directory + QStringLiteral("/a4-appearance-shortcuts.json");
    if (!SettingsClient::ffiAvailable()) {
        check("settings.a4.appearance.ffi", false, "no vd-ffi settings ABI");
        return;
    }
    const int startupPointSize = QApplication::font().pointSize();

    Harness harness;
    check("settings.a4.appearance.loaded", buildHarness(&harness, settingsPath, shortcutPath));
    SettingsPanel *panel = harness.panel.get();
    check("settings.a4.appearance.fields",
          panel->fontPointSizeSpin() != nullptr && panel->scalePercentCombo() != nullptr);
    check("settings.a4.appearance.scale-choices",
          panel->scalePercentCombo()->count() == 4
              && panel->scalePercentCombo()->itemData(0).toInt() == 100
              && panel->scalePercentCombo()->itemData(1).toInt() == 125
              && panel->scalePercentCombo()->itemData(2).toInt() == 150
              && panel->scalePercentCombo()->itemData(3).toInt() == 200,
          QString::number(panel->scalePercentCombo()->count()));

    // --- acceptance ⑤: the font changes QApplication::font() immediately ----
    check("settings.a4.font.applied", panel->applyFontPointSize(17) == 17,
          QString::number(QApplication::font().pointSize()));
    check("settings.a4.font.qapplication", QApplication::font().pointSize() == 17,
          QString::number(QApplication::font().pointSize()));
    panel->fontPointSizeSpin()->setValue(13);
    check("settings.a4.font.widget", QApplication::font().pointSize() == 13,
          QString::number(QApplication::font().pointSize()));
    check("settings.a4.font.flush", panel->flush());
    check("settings.a4.font.stored",
          readText(settingsPath).contains(QStringLiteral("\"ui.font_point_size\" = \"13\"")));
    Harness fontRestart;
    check("settings.a4.font.restart-load",
          buildHarness(&fontRestart, settingsPath, shortcutPath + QStringLiteral(".font")));
    check("settings.a4.font.restart-value", fontRestart.panel->fontPointSizeSpin()->value() == 13,
          QString::number(fontRestart.panel->fontPointSizeSpin()->value()));
    check("settings.a4.font.restart-applied", QApplication::font().pointSize() == 13,
          QString::number(QApplication::font().pointSize()));
    fontRestart.panel->fontPointSizeSpin()->setValue(0);
    check("settings.a4.font.system", QApplication::font().pointSize() == startupPointSize,
          QStringLiteral("now=%1 startup=%2")
              .arg(QApplication::font().pointSize())
              .arg(startupPointSize));
    check("settings.a4.font.system-not-stored",
          fontRestart.panel->flush()
              && !readText(settingsPath).contains(QStringLiteral("ui.font_point_size")));

    // --- the scale is stored here and applied by the NEXT start -------------
    panel->scalePercentCombo()->setCurrentIndex(panel->scalePercentCombo()->findData(150));
    check("settings.a4.scale.flush", panel->flush());
    check("settings.a4.scale.stored",
          readText(settingsPath).contains(QStringLiteral("\"ui.scale_percent\" = \"150\"")),
          readText(settingsPath).left(200));
    check("settings.a4.scale.hint",
          panel->scaleHintLabel()->text().contains(QStringLiteral("QT_SCALE_FACTOR=1.5")),
          panel->scaleHintLabel()->text());
    check("settings.a4.scale.read", a4::ui_scale::readValues(settingsPath).scalePercent == 150);
    Harness scaleRestart;
    check("settings.a4.scale.restart-load",
          buildHarness(&scaleRestart, settingsPath, shortcutPath + QStringLiteral(".scale")));
    check("settings.a4.scale.restart-value",
          scaleRestart.panel->scalePercentCombo()->currentData().toInt() == 150);
    scaleRestart.panel->scalePercentCombo()->setCurrentIndex(
        scaleRestart.panel->scalePercentCombo()->findData(100));
    check("settings.a4.scale.default-cleared",
          scaleRestart.panel->flush()
              && !readText(settingsPath).contains(QStringLiteral("ui.scale_percent")));
}

void checkFormatPage(const QString &directory)
{
    using a4::SettingsClient;
    const QString settingsPath = directory + QStringLiteral("/a4-format.toml");
    const QString shortcutPath = directory + QStringLiteral("/a4-format-shortcuts.json");
    if (!SettingsClient::ffiAvailable()) {
        check("settings.a4.format.ffi", false, "no vd-ffi settings ABI");
        return;
    }
    Harness harness;
    check("settings.a4.format.loaded", buildHarness(&harness, settingsPath, shortcutPath));
    SettingsPanel *panel = harness.panel.get();

    // --- acceptance ② (default half): an untouched page sends NO flag -------
    check("settings.a4.format.default-values", a4::formatValuesAreDefault(panel->formatValues()));
    check("settings.a4.format.default-argv",
          a4::formatArguments(panel->formatValues()).isEmpty(),
          a4::formatArguments(panel->formatValues()).join(QLatin1Char(' ')));
    int wired = 0;
    for (const a4::FormatKnobSpec &knob : a4::kFormatKnobs) {
        if (panel->formatKnobWidget(QString::fromLatin1(knob.key)) != nullptr) {
            ++wired;
        }
    }
    check("settings.a4.format.knobs", wired == static_cast<int>(a4::kFormatKnobCount),
          QStringLiteral("%1/%2").arg(wired).arg(a4::kFormatKnobCount));
    check("settings.a4.format.advanced-folded",
          panel->formatAdvancedContainer() != nullptr
              && !panel->formatAdvancedContainer()->isVisible()
              && panel->formatAdvancedButton() != nullptr
              && !panel->formatAdvancedButton()->isChecked());

    // --- acceptance ② (mapping half): one widget value, exactly one flag ----
    QStringList unwired;
    const a4::FormatValues chosen = fullFormatValues();
    for (const a4::FormatKnobSpec &knob : a4::kFormatKnobs) {
        const QString key = QString::fromLatin1(knob.key);
        if (!setKnobValue(panel, knob.key, chosen.value(key))) {
            unwired.append(key);
        }
    }
    const QStringList actual = a4::formatArguments(panel->formatValues());
    const QStringList expected = expectedFullFormatArguments();
    check("settings.a4.format.argv-one-to-one", actual == expected,
          actual.join(QLatin1Char(' ')));
    check("settings.a4.format.argv-count",
          actual.size() == static_cast<int>(a4::kFormatKnobCount),
          QString::number(actual.size()));
    check("settings.a4.format.widgets", unwired.isEmpty(), unwired.join(QLatin1Char(',')));
    check("settings.a4.format.flags",
          actual.contains(QStringLiteral("--column_limit=120"))
              && actual.contains(QStringLiteral("--indentation_spaces=4"))
              && actual.contains(QStringLiteral("--line_terminator=CRLF"))
              && actual.contains(QStringLiteral("--try_wrap_long_lines=true"))
              && actual.contains(QStringLiteral("--port_declarations_indentation=indent")));

    // --- acceptance ①: round trip and idempotent writes --------------------
    check("settings.a4.format.flush", panel->flush());
    Harness restarted;
    check("settings.a4.format.restart-load",
          buildHarness(&restarted, settingsPath, shortcutPath + QStringLiteral(".restart")));
    check("settings.a4.format.restart-values", restarted.panel->formatValues() == chosen);
    check("settings.a4.format.restart-argv",
          a4::formatArguments(restarted.panel->formatValues()) == expected);
    const QString first = readText(settingsPath);
    check("settings.a4.format.single-key",
          first.count(QStringLiteral("\"format.column_limit\"")) == 1
              && first.count(QStringLiteral("\"format.named_port_alignment\"")) == 1);
    restarted.panel->setFormatValues(restarted.panel->formatValues());
    check("settings.a4.format.idempotent-flush", restarted.panel->flush());
    check("settings.a4.format.idempotent", readText(settingsPath) == first);

    // --- "Restore defaults" really clears every key ------------------------
    restarted.panel->restoreFormatDefaults();
    check("settings.a4.format.restored-flush", restarted.panel->flush());
    check("settings.a4.format.restored",
          a4::formatValuesAreDefault(restarted.panel->formatValues())
              && !readText(settingsPath).contains(QStringLiteral("format.")));
    check("settings.a4.format.restored-argv",
          a4::formatArguments(restarted.panel->formatValues()).isEmpty());

    // --- the holder `formatVerilog` reads is the page's value --------------
    restarted.panel->setFormatValues(chosen);
    restarted.panel->publishFormatAndLint();
    check("settings.a4.format.holder", a4::currentFormatValues() == chosen);
    check("settings.a4.format.holder-argv",
          a4::formatArguments(a4::currentFormatValues()) == expected);
    restarted.panel->restoreFormatDefaults();
    restarted.panel->publishFormatAndLint();
    check("settings.a4.format.holder-cleared",
          a4::formatArguments(a4::currentFormatValues()).isEmpty());

#if defined(VEDIT_HAVE_DIAGNOSTICS_FORMAT)
    // --- acceptance ② (transport half): the flags really reach the process ---
    // A fake formatter echoes its argv on stdout, so this covers the REAL
    // `formatVerilog` call (the function the Format Document command runs), not
    // just the pure builder above.
    const QString fakeFormatter = directory + QStringLiteral("/a4-fake-format.sh");
    check("settings.a4.format.fake-write",
          writeText(fakeFormatter,
                    QByteArray("#!/bin/sh\nfor arg in \"$@\"; do printf '%s\\n' \"$arg\"; done\n")));
    check("settings.a4.format.fake-executable",
          QFile::setPermissions(fakeFormatter, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                    | QFileDevice::ExeOwner));
    a4::setCurrentFormatValues(chosen);
    QString formatted;
    QString formatError;
    const bool formattedOk = vedit::workspace::diagnostics::formatVerilog(
        fakeFormatter, QStringLiteral("module m;\nendmodule\n"), &formatted, &formatError);
    QStringList received = formatted.split(QLatin1Char('\n'));
    received.removeAll(QString());
    check("settings.a4.format.transport-runs", formattedOk, formatError);
    check("settings.a4.format.transport-argv", received == expected + QStringList{QStringLiteral("-")},
          received.join(QLatin1Char(' ')));
    // The default page sends exactly the stdin marker and nothing else.
    a4::setCurrentFormatValues(a4::defaultFormatValues());
    QString defaultFormatted;
    const bool defaultOk = vedit::workspace::diagnostics::formatVerilog(
        fakeFormatter, QStringLiteral("module m;\nendmodule\n"), &defaultFormatted, &formatError);
    QStringList defaultReceived = defaultFormatted.split(QLatin1Char('\n'));
    defaultReceived.removeAll(QString());
    check("settings.a4.format.transport-default",
          defaultOk && defaultReceived == QStringList{QStringLiteral("-")},
          defaultReceived.join(QLatin1Char(' ')));
    a4::setCurrentFormatValues(chosen);
#endif
}

void checkLintPage(const QString &directory)
{
    namespace keys = vedit::workspace::settings::keys;
    using a4::SettingsClient;
    const QString settingsPath = directory + QStringLiteral("/a4-lint.toml");
    const QString shortcutPath = directory + QStringLiteral("/a4-lint-shortcuts.json");
    if (!SettingsClient::ffiAvailable()) {
        check("settings.a4.lint.ffi", false, "no vd-ffi settings ABI");
        return;
    }
    Harness harness;
    check("settings.a4.lint.loaded", buildHarness(&harness, settingsPath, shortcutPath));
    SettingsPanel *panel = harness.panel.get();

    const a4::LintKnob allKnobs[] = {
        a4::LintKnob::Ruleset,     a4::LintKnob::Rules,        a4::LintKnob::RulesConfig,
        a4::LintKnob::WaiverFiles, a4::LintKnob::Autofix,      a4::LintKnob::SlangWarnings,
        a4::LintKnob::SlangErrorLimit,
    };
    int wired = 0;
    for (const a4::LintKnob knob : allKnobs) {
        if (panel->lintKnobWidget(knob) != nullptr) {
            ++wired;
        }
    }
    check("settings.a4.lint.knobs", wired == 7, QString::number(wired));

    // --- acceptance ④: an engine that has no such concept greys it OUT -----
    check("settings.a4.lint.default-engine", panel->lintBackend() == QStringLiteral("slang"),
          panel->lintBackend());
    check("settings.a4.lint.slang.ruleset-disabled",
          !panel->lintKnobEnabled(a4::LintKnob::Ruleset)
              && !panel->lintKnobEnabled(a4::LintKnob::Rules)
              && !panel->lintKnobEnabled(a4::LintKnob::RulesConfig)
              && !panel->lintKnobEnabled(a4::LintKnob::WaiverFiles)
              && !panel->lintKnobEnabled(a4::LintKnob::Autofix));
    check("settings.a4.lint.slang.ruleset-reason",
          panel->lintKnobReason(a4::LintKnob::Ruleset).contains(QStringLiteral("slang has no rule")),
          panel->lintKnobReason(a4::LintKnob::Ruleset));
    check("settings.a4.lint.slang.config-reason",
          panel->lintKnobReason(a4::LintKnob::RulesConfig)
              .contains(QStringLiteral(".rules.verible_lint")),
          panel->lintKnobReason(a4::LintKnob::RulesConfig));
    check("settings.a4.lint.slang.waiver-reason",
          panel->lintKnobReason(a4::LintKnob::WaiverFiles).contains(QStringLiteral("waiver")),
          panel->lintKnobReason(a4::LintKnob::WaiverFiles));
    check("settings.a4.lint.slang.autofix-reason",
          panel->lintKnobReason(a4::LintKnob::Autofix)
              .contains(QStringLiteral("automatic fixing")),
          panel->lintKnobReason(a4::LintKnob::Autofix));
    check("settings.a4.lint.slang.warnings-enabled",
          panel->lintKnobEnabled(a4::LintKnob::SlangWarnings)
              && panel->lintKnobEnabled(a4::LintKnob::SlangErrorLimit));
    check("settings.a4.lint.state.backend-unsupported",
          a4::lintKnobState(a4::LintBackend::Slang, a4::LintKnob::Autofix).kind
                  == a4::LintKnobStateKind::BackendUnsupported
              && !a4::lintKnobState(a4::LintBackend::Slang, a4::LintKnob::Autofix).enabled());
    // U6: the five knobs the lint job carries are no longer "pending".
    check("settings.a4.lint.state.wired",
          a4::lintKnobState(a4::LintBackend::Slang, a4::LintKnob::SlangWarnings).kind
                      == a4::LintKnobStateKind::Supported
              && a4::lintKnobState(a4::LintBackend::Slang, a4::LintKnob::SlangErrorLimit).kind
                      == a4::LintKnobStateKind::Supported
              && a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Ruleset).kind
                      == a4::LintKnobStateKind::Supported
              && a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Rules).kind
                      == a4::LintKnobStateKind::Supported
              && a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::WaiverFiles).kind
                      == a4::LintKnobStateKind::Supported);
    // ... but `--autofix` has no field in VdLintBatchOptions, so it stays
    // pending (R1: never silently sent as a no-op).
    check("settings.a4.lint.state.autofix-pending",
          a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix).kind
                      == a4::LintKnobStateKind::TransportPending
              && a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix).pending()
              && a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix).enabled()
              && a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix)
                     .reason.contains(QStringLiteral("transport pending")),
          a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix).reason);
    check("settings.a4.lint.state.supported",
          a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::RulesConfig).kind
                  == a4::LintKnobStateKind::Supported
              && a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::RulesConfig)
                     .reason.isEmpty());
#if defined(VEDIT_LINT_OPTIONS_HAVE_ABI)
    // U6: the engine's grey-out list IS the C ABI's own no-op table (it used to
    // be a hardcoded copy); this pins the two together.
    const auto abiUnsupported = [](int engine) {
        QStringList groups;
        char *raw = vd_lint_unsupported_knobs(engine);
        if (raw != nullptr) {
            groups = QString::fromUtf8(raw).split(QLatin1Char(','), Qt::SkipEmptyParts);
            vd_string_free(raw);
        }
        return groups;
    };
    check("settings.a4.lint.state.abi-table",
          a4::lintUnsupportedKnobGroups(a4::LintBackend::Slang) == abiUnsupported(0)
              && a4::lintUnsupportedKnobGroups(a4::LintBackend::Verible) == abiUnsupported(1),
          a4::lintUnsupportedKnobGroups(a4::LintBackend::Slang).join(QLatin1Char(','))
              + QStringLiteral(" || ")
              + a4::lintUnsupportedKnobGroups(a4::LintBackend::Verible)
                    .join(QLatin1Char(',')));
#endif

    // --- the slang set, stored through the page ----------------------------
    a4::LintValues slangValues = panel->lintValues();
    slangValues.slang.warnings = QStringLiteral("everything\nno-unused-variable");
    slangValues.slang.errorLimit = QStringLiteral("25");
    panel->setLintValues(slangValues);
    check("settings.a4.lint.slang-flush", panel->flush());
    check("settings.a4.lint.slang-stored",
          readText(settingsPath).contains(QStringLiteral("\"lint.slang.error_limit\" = \"25\""))
              && readText(settingsPath).contains(QStringLiteral("\"lint.slang.warnings\"")));

    // --- the verible set, after switching the engine -----------------------
    ToolValues tools = panel->toolValues();
    tools.lintEngine = QString::fromLatin1(keys::kLintEngineVerible);
    panel->setToolValues(tools);
    check("settings.a4.lint.verible-flush", panel->flush());
    check("settings.a4.lint.verible-engine", panel->lintBackend() == QStringLiteral("verible"),
          panel->lintBackend());
    check("settings.a4.lint.verible.ruleset-enabled",
          panel->lintKnobEnabled(a4::LintKnob::Ruleset));
    // U6: --ruleset is applied now, so the knob reports itself as supported
    // (this assertion used to require the "transport pending" reason).
    check("settings.a4.lint.verible.ruleset-wired",
          panel->lintKnobReason(a4::LintKnob::Ruleset).isEmpty(),
          panel->lintKnobReason(a4::LintKnob::Ruleset));
    check("settings.a4.lint.verible.slang-disabled",
          !panel->lintKnobEnabled(a4::LintKnob::SlangWarnings)
              && !panel->lintKnobEnabled(a4::LintKnob::SlangErrorLimit));
    check("settings.a4.lint.verible.slang-reason",
          panel->lintKnobReason(a4::LintKnob::SlangWarnings)
              .contains(QStringLiteral("verible-verilog-lint has no -W")),
          panel->lintKnobReason(a4::LintKnob::SlangWarnings));
    check("settings.a4.lint.verible.config-supported",
          panel->lintKnobReason(a4::LintKnob::RulesConfig).isEmpty());
    check("settings.a4.lint.verible.status-label",
          panel->lintVeribleStatus() != nullptr
              && panel->lintVeribleStatus()->text().contains(QStringLiteral("Active engine")),
          panel->lintVeribleStatus() != nullptr ? panel->lintVeribleStatus()->text() : QString());
    check("settings.a4.lint.verible.slang-status-label",
          panel->lintSlangStatus() != nullptr
              && panel->lintSlangStatus()->text().contains(QStringLiteral("Not the selected engine")),
          panel->lintSlangStatus() != nullptr ? panel->lintSlangStatus()->text() : QString());

    a4::LintValues veribleValues = panel->lintValues();
    veribleValues.verible.ruleset = QString::fromLatin1(keys::kRulesetAll);
    veribleValues.verible.rules = QStringLiteral("line-length,no-tabs");
    veribleValues.verible.rulesConfig = QStringLiteral("/proj/.rules.verible_lint");
    veribleValues.verible.waiverFiles = QStringLiteral("/p/a.waiver\n/p/b.waiver");
    veribleValues.verible.autofix = QString::fromLatin1(keys::kAutofixPatch);
    panel->setLintValues(veribleValues);
    check("settings.a4.lint.verible-flush2", panel->flush());

    // --- acceptance ③: both configurations live side by side ---------------
    const QString store = readText(settingsPath);
    check("settings.a4.lint.two-sets",
          store.contains(QStringLiteral("\"lint.verible.ruleset\" = \"all\""))
              && store.contains(QStringLiteral("\"lint.verible.autofix\" = \"patch\""))
              && store.contains(QStringLiteral("\"lint.verible.rules_config\""))
              && store.contains(QStringLiteral("\"lint.verible.waiver_files\""))
              && store.contains(QStringLiteral("\"lint.slang.error_limit\" = \"25\""))
              && store.contains(QStringLiteral("\"lint.slang.warnings\"")),
          store.left(400));
    tools.lintEngine = QString::fromLatin1(keys::kLintEngineSlang);
    panel->setToolValues(tools);
    check("settings.a4.lint.switch-flush", panel->flush());
    const a4::LintValues switched = panel->lintValues();
    check("settings.a4.lint.switch-back-slang",
          switched.slang.errorLimit == QStringLiteral("25")
              && switched.slang.warnings == QStringLiteral("everything\nno-unused-variable"));
    check("settings.a4.lint.switch-back-verible",
          switched.verible.rules == QStringLiteral("line-length,no-tabs")
              && switched.verible.autofix == QString::fromLatin1(keys::kAutofixPatch));
    check("settings.a4.lint.no-crosswrite", lintSection(readText(settingsPath)) == lintSection(store),
          firstDifference(lintSection(store), lintSection(readText(settingsPath))));

    Harness restarted;
    check("settings.a4.lint.restart-load",
          buildHarness(&restarted, settingsPath, shortcutPath + QStringLiteral(".restart")));
    check("settings.a4.lint.restart-values",
          restarted.panel->lintValues().verible.rules == QStringLiteral("line-length,no-tabs")
              && restarted.panel->lintValues().slang.errorLimit == QStringLiteral("25"));
    const QString lintFirst = readText(settingsPath);
    restarted.panel->setLintValues(restarted.panel->lintValues());
    check("settings.a4.lint.idempotent-flush", restarted.panel->flush());
    check("settings.a4.lint.idempotent", readText(settingsPath) == lintFirst);

    // --- the ONE composition seam (rules file precedence) ------------------
    a4::LintValues composed = restarted.panel->lintValues();
    composed.verible.rulesConfig = QStringLiteral("/page/.rules");
    check("settings.a4.lint.options.page-wins",
          a4::buildLintJobOptions(composed, a4::LintBackend::Verible,
                                  QStringLiteral("/legacy/.rules"),
                                  QStringLiteral("/discovered/.rules"))
                  .rulesPath
              == QStringLiteral("/page/.rules"));
    composed.verible.rulesConfig.clear();
    check("settings.a4.lint.options.legacy-second",
          a4::buildLintJobOptions(composed, a4::LintBackend::Verible,
                                  QStringLiteral("/legacy/.rules"),
                                  QStringLiteral("/discovered/.rules"))
                  .rulesPath
              == QStringLiteral("/legacy/.rules"));
    check("settings.a4.lint.options.discovered-third",
          a4::buildLintJobOptions(composed, a4::LintBackend::Verible, QString(),
                                  QStringLiteral("/discovered/.rules"))
                  .rulesPath
              == QStringLiteral("/discovered/.rules"));
    check("settings.a4.lint.options.none",
          a4::buildLintJobOptions(composed, a4::LintBackend::Verible, QString(), QString())
              .rulesPath.isEmpty());
    check("settings.a4.lint.options.slang-never",
          a4::buildLintJobOptions(restarted.panel->lintValues(), a4::LintBackend::Slang,
                                  QStringLiteral("/legacy/.rules"),
                                  QStringLiteral("/discovered/.rules"))
              .rulesPath.isEmpty());

    // --- U6: every WIRED knob reaches the batch argv ----------------------
    // The mapping is the product's own, not a test-local copy: panel values ->
    // buildLintJobOptions -> fillLintBatchOptions -> the REAL
    // `vd_lint_batch_command_line` echo of the same VdLintBatchOptions struct
    // the project lint backend hands to vd_deeplint_batch_start.
#if defined(VEDIT_LINT_OPTIONS_HAVE_ABI)
    const auto batchArgv = [](const a4::LintValues &values, a4::LintBackend backend,
                              const QString &legacyRules = QString(),
                              const QString &discoveredRules = QString()) {
        const a4::LintJobOptions job =
            a4::buildLintJobOptions(values, backend, legacyRules, discoveredRules);
        VdLintBatchOptions abi{};
        a4::LintBatchAbiStorage storage;
        a4::fillLintBatchOptions(job, &abi, &storage);
        char *line = vd_lint_batch_command_line(&abi, "f.sv");
        QString text = line != nullptr ? QString::fromUtf8(line) : QString();
        if (line != nullptr) {
            vd_string_free(line);
        }
        // The echo appends the engine's knobs note after " | "; the note itself
        // names flags (e.g. "--ruleset= ... are sent only when set"), so the
        // assertions below read the argv part only.
        const int note = text.indexOf(QStringLiteral(" | "));
        if (note >= 0) {
            text.truncate(note);
        }
        return text;
    };

    // (1) --ruleset
    a4::LintValues knob;
    knob.verible.ruleset = QString::fromLatin1(keys::kRulesetAll);
    const QString rulesetArgv = batchArgv(knob, a4::LintBackend::Verible);
    check("settings.a4.lint.argv.ruleset", rulesetArgv.contains(QStringLiteral("--ruleset=all")),
          rulesetArgv);

    // (2) --rules
    knob = a4::LintValues{};
    knob.verible.rules = QStringLiteral("line-length,no-tabs");
    const QString rulesArgv = batchArgv(knob, a4::LintBackend::Verible);
    check("settings.a4.lint.argv.rules",
          rulesArgv.contains(QStringLiteral("--rules=line-length,no-tabs")), rulesArgv);

    // (3) --waiver_files (one path per line -> ONE comma-joined argv entry)
    knob = a4::LintValues{};
    knob.verible.waiverFiles = QStringLiteral("/p/a.waiver\n/p/b.waiver");
    const QString waiverArgv = batchArgv(knob, a4::LintBackend::Verible);
    check("settings.a4.lint.argv.waiver-files",
          waiverArgv.contains(QStringLiteral("--waiver_files=/p/a.waiver,/p/b.waiver")),
          waiverArgv);

    // (4) --autofix: NO ABI field -> the value must NOT reach the argv (R1),
    // and the knob must still report itself as pending.
    knob = a4::LintValues{};
    knob.verible.autofix = QString::fromLatin1(keys::kAutofixPatch);
    const QString autofixArgv = batchArgv(knob, a4::LintBackend::Verible);
    check("settings.a4.lint.argv.autofix-not-sent",
          !autofixArgv.contains(QStringLiteral("--autofix"))
              && a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix).pending(),
          autofixArgv);

    // (5) -W (the page stores bare values; each becomes its own token)
    knob = a4::LintValues{};
    knob.slang.warnings = QStringLiteral("everything\nno-unused-variable");
    const QString warningsArgv = batchArgv(knob, a4::LintBackend::Slang);
    check("settings.a4.lint.argv.slang-warnings",
          warningsArgv.contains(QStringLiteral("-Weverything -Wno-unused-variable")),
          warningsArgv);

    // (6) --error-limit
    knob = a4::LintValues{};
    knob.slang.errorLimit = QStringLiteral("25");
    const QString limitArgv = batchArgv(knob, a4::LintBackend::Slang);
    check("settings.a4.lint.argv.slang-error-limit",
          limitArgv.contains(QStringLiteral("--error-limit=25")), limitArgv);

    // Defaults send NO flag: an untouched page is verible's/slang's own default.
    const QString defaultVerible = batchArgv(a4::LintValues{}, a4::LintBackend::Verible);
    const QString defaultSlang = batchArgv(a4::LintValues{}, a4::LintBackend::Slang);
    check("settings.a4.lint.argv.defaults-no-flag",
          !defaultVerible.contains(QStringLiteral("--ruleset="))
              && !defaultVerible.contains(QStringLiteral("--rules="))
              && !defaultVerible.contains(QStringLiteral("--rules_config="))
              && !defaultVerible.contains(QStringLiteral("--waiver_files="))
              && !defaultVerible.contains(QStringLiteral("--autofix"))
              && !defaultSlang.contains(QStringLiteral("--error-limit="))
              // the engine's own built-in -Weverything is not an extra knob
              && defaultSlang.count(QStringLiteral("-W")) == 1,
          defaultVerible + QStringLiteral(" || ") + defaultSlang);

    // The two configurations never leak into each other's argv: a verible pass
    // carries verible flags only, a slang pass slang flags only.
    a4::LintValues both;
    both.verible.ruleset = QString::fromLatin1(keys::kRulesetAll);
    both.verible.rules = QStringLiteral("line-length");
    both.verible.rulesConfig = QStringLiteral("/page/.rules");
    both.verible.waiverFiles = QStringLiteral("/p/a.waiver");
    both.verible.autofix = QString::fromLatin1(keys::kAutofixPatch);
    both.slang.warnings = QStringLiteral("no-unused-variable");
    both.slang.errorLimit = QStringLiteral("25");
    const QString veribleBoth = batchArgv(both, a4::LintBackend::Verible);
    const QString slangBoth = batchArgv(both, a4::LintBackend::Slang);
    check("settings.a4.lint.argv.two-sets",
          veribleBoth.contains(QStringLiteral("--ruleset=all"))
              && veribleBoth.contains(QStringLiteral("--rules=line-length"))
              && veribleBoth.contains(QStringLiteral("--rules_config=/page/.rules"))
              && veribleBoth.contains(QStringLiteral("--waiver_files=/p/a.waiver"))
              && !veribleBoth.contains(QStringLiteral("--error-limit="))
              && !veribleBoth.contains(QStringLiteral("-Wno-unused-variable"))
              && slangBoth.contains(QStringLiteral("-Wno-unused-variable"))
              && slangBoth.contains(QStringLiteral("--error-limit=25"))
              && !slangBoth.contains(QStringLiteral("--ruleset="))
              && !slangBoth.contains(QStringLiteral("--rules="))
              && !slangBoth.contains(QStringLiteral("--rules_config="))
              && !slangBoth.contains(QStringLiteral("--waiver_files=")),
          veribleBoth + QStringLiteral(" || ") + slangBoth);

    // The discovered `.rules.verible_lint` is the FALLBACK: hand-typed wins,
    // then the legacy Tools path, then discovery, then no flag.
    const QString discoveredArgv =
        batchArgv(a4::LintValues{}, a4::LintBackend::Verible, QString(),
                  QStringLiteral("/proj/.rules.verible_lint"));
    check("settings.a4.lint.argv.discovered-rules-path",
          discoveredArgv.contains(QStringLiteral("--rules_config=/proj/.rules.verible_lint")),
          discoveredArgv);
    const QString handTypedArgv =
        batchArgv(both, a4::LintBackend::Verible, QStringLiteral("/legacy/.rules"),
                  QStringLiteral("/proj/.rules.verible_lint"));
    check("settings.a4.lint.argv.hand-typed-wins",
          handTypedArgv.contains(QStringLiteral("--rules_config=/page/.rules"))
              && !handTypedArgv.contains(QStringLiteral("/legacy/.rules"))
              && !handTypedArgv.contains(QStringLiteral("/proj/.rules.verible_lint")),
          handTypedArgv);
#endif

    restarted.panel->publishFormatAndLint();
    check("settings.a4.lint.holder",
          a4::currentLintValues().verible.autofix == QString::fromLatin1(keys::kAutofixPatch)
              && a4::currentLintValues().slang.errorLimit == QStringLiteral("25"));
}

void checkLegacyStore(const QString &directory)
{
    using a4::SettingsClient;
    if (!SettingsClient::ffiAvailable()) {
        check("settings.a4.legacy.ffi", false, "no vd-ffi settings ABI");
        return;
    }
    const QString settingsPath = directory + QStringLiteral("/a4-legacy.toml");
    check("settings.a4.legacy.write",
          writeText(settingsPath, QByteArray("\"theme\" = \"\"\n"
                                             "\"tools.lint_engine\" = \"verible\"\n"
                                             "\"tools.include_dirs\" = \"rtl\"\n")));
    Harness harness;
    check("settings.a4.legacy.loaded",
          buildHarness(&harness, settingsPath,
                       directory + QStringLiteral("/a4-legacy-keys.json")));
    SettingsPanel *panel = harness.panel.get();
    check("settings.a4.legacy.defaults",
          a4::formatValuesAreDefault(panel->formatValues())
              && panel->appearanceValues().scalePercent == 100
              && panel->appearanceValues().fontPointSize == 0
              && panel->lintValues().slang.errorLimit.isEmpty()
              && panel->lintValues().verible.rules.isEmpty());
    check("settings.a4.legacy.no-diagnostics", panel->diagnostics().isEmpty(),
          panel->diagnostics().join(QLatin1Char('|')));
    panel->setAppearanceValues(SettingsPanel::AppearanceValues{0, 100});
    check("settings.a4.legacy.flush", panel->flush());
    const QString after = readText(settingsPath);
    check("settings.a4.legacy.preserved",
          after.contains(QStringLiteral("\"tools.lint_engine\" = \"verible\""))
              && after.contains(QStringLiteral("\"tools.include_dirs\" = \"rtl\"")),
          after.left(200));
    check("settings.a4.legacy.no-new-keys",
          !after.contains(QStringLiteral("format.")) && !after.contains(QStringLiteral("lint."))
              && !after.contains(QStringLiteral("ui.font_point_size")));
}

// Acceptance ⑥: the value the NEXT start puts into QT_SCALE_FACTOR, asserted
// through the real product binary (not a copy of its logic).
void checkScaleRestartPath(const QString &directory)
{
    namespace ui = a4::ui_scale;
    qunsetenv("QT_SCALE_FACTOR");
    check("settings.a4.scale.env-100",
          ui::applyScaleEnvironment(100).isEmpty() && !qEnvironmentVariableIsSet("QT_SCALE_FACTOR"));
    check("settings.a4.scale.env-125",
          ui::applyScaleEnvironment(125) == QStringLiteral("1.25")
              && qgetenv("QT_SCALE_FACTOR") == QByteArray("1.25"));
    check("settings.a4.scale.env-150",
          ui::applyScaleEnvironment(150) == QStringLiteral("1.5")
              && qgetenv("QT_SCALE_FACTOR") == QByteArray("1.5"));
    check("settings.a4.scale.env-200",
          ui::applyScaleEnvironment(200) == QStringLiteral("2")
              && qgetenv("QT_SCALE_FACTOR") == QByteArray("2"));
    check("settings.a4.scale.env-invalid",
          ui::applyScaleEnvironment(1337) == QStringLiteral("2")
              && qgetenv("QT_SCALE_FACTOR") == QByteArray("2"));
    qunsetenv("QT_SCALE_FACTOR");
    check("settings.a4.scale.factors",
          ui::scaleFactor(100).isEmpty() && ui::scaleFactor(125) == QStringLiteral("1.25")
              && ui::scaleFactor(150) == QStringLiteral("1.5")
              && ui::scaleFactor(200) == QStringLiteral("2"));

    const QString quotedPath = directory + QStringLiteral("/a4-read-quoted.toml");
    check("settings.a4.read.write",
          writeText(quotedPath, QByteArray("\"ui.scale_percent\" = \"150\"\n"
                                           "\"ui.font_point_size\" = \"11\"\n"
                                           "\"tools.lint_engine\" = \"verible\"\n")));
    const ui::UiValues quoted = ui::readValues(quotedPath);
    check("settings.a4.read.quoted", quoted.scalePercent == 150 && quoted.fontPointSize == 11,
          QStringLiteral("%1/%2").arg(quoted.scalePercent).arg(quoted.fontPointSize));
    const QString barePath = directory + QStringLiteral("/a4-read-bare.toml");
    check("settings.a4.read.bare-write",
          writeText(barePath, QByteArray("# hand-edited\nui.scale_percent = \"200\"\n")));
    check("settings.a4.read.bare", ui::readValues(barePath).scalePercent == 200);
    const QString degradedPath = directory + QStringLiteral("/a4-read-degraded.toml");
    check("settings.a4.read.degraded-write",
          writeText(degradedPath, QByteArray("\"ui.scale_percent\" = \"1337\"\n"
                                             "\"ui.font_point_size\" = \"999\"\n")));
    const ui::UiValues degraded = ui::readValues(degradedPath);
    check("settings.a4.read.degrades", degraded.scalePercent == 100 && degraded.fontPointSize == 0,
          QStringLiteral("%1/%2").arg(degraded.scalePercent).arg(degraded.fontPointSize));
    check("settings.a4.read.missing",
          ui::readValues(directory + QStringLiteral("/does-not-exist.toml")).scalePercent == 100);

    const QString binary = QString::fromLatin1(
#if defined(VEDIT_APP_BINARY_PATH)
        VEDIT_APP_BINARY_PATH
#else
        ""
#endif
    );
    if (binary.isEmpty() || !QFileInfo(binary).isExecutable()) {
        std::printf("SKIP settings.a4.scale.product-binary reason=no-vedit-app-in-this-build\n");
        std::fflush(stdout);
        return;
    }
    check("settings.a4.scale.product-exists", QFileInfo(binary).isExecutable(), binary);
    const QString stateDir = directory + QStringLiteral("/a4-scale-state");
    check("settings.a4.scale.state-dir", QDir().mkpath(stateDir));

    const auto runProduct = [&binary, &stateDir](const QByteArray &storeText, QString *output,
                                                 int *exitCode) {
        if (!storeText.isNull()) {
            QFile file(stateDir + QStringLiteral("/settings.toml"));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return false;
            }
            file.write(storeText);
            file.close();
        }
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("QT_SCALE_FACTOR"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        QProcess process;
        process.setProcessEnvironment(env);
        // `--ui-scale-echo` is the narrow seam: it prints the line and exits, so
        // this acceptance row does not depend on the whole shell self-check (other
        // lanes' in-flight state) and runs in milliseconds.
        process.start(binary, {QStringLiteral("--ui-scale-echo"), QStringLiteral("--state-dir"),
                               stateDir});
        if (!process.waitForFinished(180000)) {
            process.kill();
            return false;
        }
        if (output != nullptr) {
            *output = QString::fromUtf8(process.readAllStandardOutput());
        }
        if (exitCode != nullptr) {
            *exitCode = process.exitCode();
        }
        return true;
    };

    QString scaledOutput;
    int scaledExit = -1;
    const bool scaledRan =
        runProduct(QByteArray("\"ui.scale_percent\" = \"150\"\n"), &scaledOutput, &scaledExit);
    check("settings.a4.scale.product-runs", scaledRan,
          QStringLiteral("exit=%1").arg(scaledExit));
    check("settings.a4.scale.product-exit", scaledExit == 0, QString::number(scaledExit));
    check("settings.a4.scale.product-line",
          scaledOutput.contains(
              QStringLiteral("UI-SCALE scale_percent=150 qt_scale_factor=1.5")),
          scaledOutput.left(200));

    QString defaultOutput;
    int defaultExit = -1;
    const bool defaultRan = runProduct(QByteArray("\"tools.lint_engine\" = \"slang\"\n"),
                                       &defaultOutput, &defaultExit);
    check("settings.a4.scale.product-runs-default", defaultRan,
          QStringLiteral("exit=%1").arg(defaultExit));
    check("settings.a4.scale.product-default-line",
          defaultOutput.contains(
              QStringLiteral("UI-SCALE scale_percent=100 qt_scale_factor=<unset>")),
          defaultOutput.left(200));

    // Plain start: NO --state-dir. The store then lives at Qt's AppConfigLocation,
    // which the startup read has to resolve BEFORE the QApplication exists.
    const QString configHome = directory + QStringLiteral("/a4-xdg-config");
    const QString defaultStoreDir = configHome + QStringLiteral("/VEdit/vedit-app");
    check("settings.a4.scale.default-dir", QDir().mkpath(defaultStoreDir));
    check("settings.a4.scale.default-write",
          writeText(defaultStoreDir + QStringLiteral("/settings.toml"),
                    QByteArray("\"ui.scale_percent\" = \"200\"\n")));
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("QT_SCALE_FACTOR"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), configHome);
        QProcess plain;
        plain.setProcessEnvironment(env);
        plain.start(binary, {QStringLiteral("--ui-scale-echo")});
        const bool finished = plain.waitForFinished(120000);
        const QString output = QString::fromUtf8(plain.readAllStandardOutput());
        check("settings.a4.scale.default-runs", finished && plain.exitCode() == 0,
              QStringLiteral("exit=%1").arg(plain.exitCode()));
        check("settings.a4.scale.default-line",
              output.contains(QStringLiteral("scale_percent=200 qt_scale_factor=2")),
              output.left(220));
        check("settings.a4.scale.default-path",
              output.contains(QStringLiteral("settings=%1/settings.toml").arg(defaultStoreDir)),
              output.left(320));
    }
    // ... and with no settings file at all the scale stays at 100 / unset.
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("QT_SCALE_FACTOR"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("XDG_CONFIG_HOME"),
                   directory + QStringLiteral("/a4-empty-config"));
        QProcess plain;
        plain.setProcessEnvironment(env);
        plain.start(binary, {QStringLiteral("--ui-scale-echo")});
        const bool finished = plain.waitForFinished(120000);
        const QString output = QString::fromUtf8(plain.readAllStandardOutput());
        check("settings.a4.scale.default-empty",
              finished && plain.exitCode() == 0
                  && output.contains(QStringLiteral("scale_percent=100 qt_scale_factor=<unset>")),
              output.left(220));
    }

    QString fontOutput;
    int fontExit = -1;
    const bool fontRan =
        runProduct(QByteArray("\"ui.font_point_size\" = \"19\"\n"), &fontOutput, &fontExit);
    check("settings.a4.font.product-runs", fontRan, QStringLiteral("exit=%1").arg(fontExit));
    check("settings.a4.font.product-line",
          fontOutput.contains(QStringLiteral("scale_percent=100 qt_scale_factor=<unset> "
                                             "font_point_size=19")),
          fontOutput.left(200));
}


void captureFimKeyEvidence(const QString &directory)
{
    if (!SettingsClient::ffiAvailable()) {
        check("settings.evidence.ffi.available", false, "lane built without the vd-ffi settings ABI");
        return;
    }
    check("settings.evidence.mkdir", QDir().mkpath(directory));
    const QString settingsPath = directory + QStringLiteral("/fim-evidence-settings.toml");
    Harness harness;
    check("settings.evidence.harness",
          buildHarness(&harness, settingsPath, directory + QStringLiteral("/fim-evidence-shortcuts.json")));
    harness.panel->setFimValues(sampleFim());
    check("settings.evidence.store", harness.panel->storeFimApiKey(QString::fromLatin1(kSecret)));
    check("settings.evidence.flush", harness.panel->flush());
    const QString keyPath = harness.panel->fimKeyFilePath();
    const QString store = readText(settingsPath);
    check("settings.evidence.secret-not-in-store",
          !store.contains(QString::fromLatin1(kSecret))
              && !store.contains(QStringLiteral("fim.api_key")));
    QFile report(directory + QStringLiteral("/fim-key-evidence.txt"));
    if (report.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream out(&report);
        out << "settings.toml:             " << settingsPath << "\n";
        out << "key file:                  " << keyPath << "\n";
        out << "key source reported:       " << harness.panel->fimKeySource() << "\n";
        out << "key stored:                " << (harness.panel->fimKeyStored() ? "yes" : "no") << "\n";
        out << "key file grants group/other access: "
            << (loosePermissions(keyPath) ? "YES" : "no") << "\n";
        out << "key echoed in the UI field: "
            << (harness.panel->fimApiKeyEdit()->text().isEmpty() ? "no" : "YES") << "\n";
        out << "secret present in settings.toml:   "
            << (store.contains(QString::fromLatin1(kSecret)) ? "YES" : "no") << "\n";
        out << "fim.api_key present in settings.toml: "
            << (store.contains(QStringLiteral("fim.api_key")) ? "YES" : "no") << "\n";
        out << "\n--- settings.toml (verbatim) ---\n" << store << "\n";
        out << "--- key file ---\n" << QFileInfo(keyPath).size() << " bytes (content not recorded)\n";
        out.flush();
        report.close();
    }
    // Do not leave a secret-shaped file in the evidence tree.
    harness.panel->clearFimKey();
    check("settings.evidence.key-file-removed", !QFileInfo::exists(keyPath));
}

void captureScreenshots(const QString &directory)
{
    if (!SettingsClient::ffiAvailable()) {
        check("settings.screenshots.ffi.available", false, "lane built without the vd-ffi settings ABI");
        return;
    }
    check("settings.screenshots.mkdir", QDir().mkpath(directory));
    const QString settingsPath = directory + QStringLiteral("/screenshot-settings.toml");
    Harness harness;
    check("settings.screenshots.harness",
          buildHarness(&harness, settingsPath, directory + QStringLiteral("/screenshot-shortcuts.json")));
    SettingsDialog dialog(&harness.themes, &harness.commands, settingsPath, nullptr);
    check("settings.screenshots.load", dialog.panel()->flush());
    dialog.panel()->setToolValues(sampleTools());
    dialog.panel()->setFimValues(sampleFim());
    // UX batch 2 / A4: the Format evidence image shows real values AND the
    // expanded advanced section, so every knob the page exposes is visible.
    dialog.panel()->setFormatValues(fullFormatValues());
    dialog.panel()->setAppearanceValues(SettingsPanel::AppearanceValues{13, 150});
    if (dialog.panel()->formatAdvancedButton() != nullptr) {
        dialog.panel()->formatAdvancedButton()->setChecked(true);
    }
    dialog.show();
    QCoreApplication::processEvents();
    const QString names[6] = {
        QStringLiteral("01-shortcuts.png"), QStringLiteral("02-tools.png"),
        QStringLiteral("03-fim.png"),       QStringLiteral("04-appearance.png"),
        QStringLiteral("05-lint.png"),      QStringLiteral("06-format.png")};
    for (int tab = 0; tab < 6; ++tab) {
        dialog.panel()->tabs()->setCurrentIndex(tab);
        QCoreApplication::processEvents();
        const QPixmap shot = dialog.grab();
        const QString path = directory + QLatin1Char('/') + names[tab];
        check("settings.screenshots.saved", !shot.isNull() && shot.save(path, "PNG"), path);
        std::printf("SCREENSHOT %s %dx%d\n", qPrintable(path), shot.width(), shot.height());
        std::fflush(stdout);
    }
    dialog.panel()->revertPending();
    dialog.close();
}

} // namespace

int main(int argc, char **argv)
{
    bool selfcheck = false;
    QString screenshotDir;
    QString scaleRestartDir;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--selfcheck") == 0) {
            selfcheck = true;
        } else if (std::strcmp(argv[index], "--screenshot") == 0 && index + 1 < argc) {
            screenshotDir = QString::fromLocal8Bit(argv[++index]);
        } else if (std::strcmp(argv[index], "--scale-restart") == 0 && index + 1 < argc) {
            // UX batch 2 / A4, acceptance ⑥: ONLY the restart path, so the
            // dedicated ctest row names exactly what it asserts.
            scaleRestartDir = QString::fromLocal8Bit(argv[++index]);
        }
    }
    if (!scaleRestartDir.isEmpty()) {
        QApplication scaleApp(argc, argv);
        if (!QDir().mkpath(scaleRestartDir)) {
            std::printf("SETTINGS-SELFTEST ok=0 reason=no-scale-restart-dir\n");
            return 1;
        }
        std::printf("SETTINGS-SCALE-RESTART start dir=%s\n", qPrintable(scaleRestartDir));
        checkScaleRestartPath(scaleRestartDir);
        std::printf("SETTINGS-SCALE-RESTART ok=%d checks=%d failures=%d\n",
                    g_failures == 0 ? 1 : 0, g_checks, g_failures);
        std::fflush(stdout);
        return g_failures == 0 ? 0 : 1;
    }
    if (!selfcheck) {
        std::printf("usage: %s --selfcheck [--screenshot <dir>]\n", argv[0]);
        return 2;
    }
    QApplication app(argc, argv);
    // The key-file checks observe the file, so an ambient environment key (which
    // wins by design) has to go. This is a test process; it is not the product.
    if (qEnvironmentVariableIsSet("VEDIT_FIM_API_KEY")) {
        std::printf("NOTE unsetting VEDIT_FIM_API_KEY for the key-file checks\n");
        qunsetenv("VEDIT_FIM_API_KEY");
    }
    std::printf("SETTINGS-SELFTEST start\n");

    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("SETTINGS-SELFTEST ok=0 reason=no-temp-dir\n");
        return 1;
    }
    checkPanelAndPersistence(temp.path() + QStringLiteral("/settings.toml"),
                             temp.path() + QStringLiteral("/shortcuts.json"));
    checkNonBlocking();
    checkPages(temp.path());
    checkAppearancePage(temp.path());
    checkFormatPage(temp.path());
    checkLintPage(temp.path());
    checkLegacyStore(temp.path());
    checkScaleRestartPath(temp.path());
    if (!screenshotDir.isEmpty()) {
        captureScreenshots(screenshotDir);
        captureFimKeyEvidence(screenshotDir);
    }

    std::printf("SETTINGS-SELFTEST ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
