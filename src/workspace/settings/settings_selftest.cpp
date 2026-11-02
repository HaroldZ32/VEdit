// Lane validator for the settings panel (Slice 3 / AC-S3.1).
//
//   vedit-workspace-settings-selftest --selfcheck
//
// Drives the real panel against the REAL Rust settings store (through the C ABI,
// no stub): theme switch + persistence, key-binding edit + persistence, the
// conflict hint, the bad-configuration fallback/recovery, a simulated restart
// (fresh ThemeManager/CommandRegistry/panel reading the same file) and a
// non-blocking check that a large store load never runs on the GUI thread.
#include "command_registry.h"
#include "settings_client.h"
#include "settings_panel.h"
#include "theme_manager.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QKeySequence>
#include <QLabel>
#include <QObject>
#include <QPlainTextEdit>
#include <QString>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <thread>

using vedit::app::CommandRegistry;
using vedit::app::CommandSpec;
using vedit::app::JsonShortcutStore;
using vedit::app::ThemeManager;
using vedit::workspace::settings::SettingsClient;
using vedit::workspace::settings::SettingsPanel;

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
    check("settings.keys.rows", panel->keyTable()->rowCount() == 3,
          QStringLiteral("rows=%1").arg(panel->keyTable()->rowCount()));

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

    // Conflict: bind Ctrl+S to a second command and expect the visible hint.
    fromRust.panel->setShortcut(QStringLiteral("view.toggle-settings"),
                                QKeySequence(QStringLiteral("Ctrl+S")));
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

} // namespace

int main(int argc, char **argv)
{
    bool selfcheck = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--selfcheck") == 0) {
            selfcheck = true;
        }
    }
    if (!selfcheck) {
        std::printf("usage: %s --selfcheck\n", argv[0]);
        return 2;
    }
    QApplication app(argc, argv);
    std::printf("SETTINGS-SELFTEST start\n");

    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("SETTINGS-SELFTEST ok=0 reason=no-temp-dir\n");
        return 1;
    }
    checkPanelAndPersistence(temp.path() + QStringLiteral("/settings.toml"),
                             temp.path() + QStringLiteral("/shortcuts.json"));
    checkNonBlocking();

    std::printf("SETTINGS-SELFTEST ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
