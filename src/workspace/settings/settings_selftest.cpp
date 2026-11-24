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
#if defined(VEDIT_HAVE_PROJECT_LANE)
#include "project_model.h"
#endif
#if defined(VEDIT_HAVE_DIAGNOSTICS_FORMAT)
#include "toolchain.h" // the REAL formatVerilog the product command uses
#endif

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QObject>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
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
using vedit::workspace::settings::EnvImportPage;
using vedit::workspace::settings::SettingsClient;
using vedit::workspace::settings::SettingsDialog;
using vedit::workspace::settings::SettingsPanel;
using vedit::workspace::settings::ToolValues;
#if defined(VEDIT_HAVE_PROJECT_LANE)
using vedit::workspace::project::ProjectModel;
#endif

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

// batch-20261113 T5 Pass B: a REAL local HTTP endpoint for the Detect button.
// Everything stays on 127.0.0.1 with an ephemeral port (the contract forbids the
// network); it records the paths it was asked for and answers every GET with the
// status/body the test selects, then closes the connection.
class FakeFimEndpoint : public QObject
{
public:
    explicit FakeFimEndpoint(QObject *parent = nullptr)
        : server_(new QTcpServer(parent))
    {
        QObject::connect(server_, &QTcpServer::newConnection, server_, [this]() {
            while (QTcpSocket *socket = server_->nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [this, socket]() { onReadyRead(socket); });
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QObject::deleteLater);
            }
        });
    }

    bool listen() { return server_->listen(QHostAddress::LocalHost, 0); }
    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(server_->serverPort());
    }
    void setStatus(int status) { status_ = status; }
    void setBody(const QByteArray &body) { body_ = body; }
    QStringList requestedPaths() const { return requestedPaths_; }

private:
    void onReadyRead(QTcpSocket *socket)
    {
        buffers_[socket] += socket->readAll();
        const int headerEnd = buffers_[socket].indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }
        const QByteArray request = buffers_.take(socket);
        const QByteArray requestLine = request.left(request.indexOf("\r\n"));
        const QList<QByteArray> parts = requestLine.split(' ');
        if (parts.size() >= 2) {
            requestedPaths_.append(QString::fromUtf8(parts.at(1)));
        }
        QByteArray reason = "OK";
        if (status_ == 401) {
            reason = "Unauthorized";
        } else if (status_ != 200) {
            reason = "Error";
        }
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status_) + " " + reason + "\r\n";
        response += "Content-Type: application/json\r\n";
        response += "Content-Length: " + QByteArray::number(body_.size()) + "\r\n";
        response += "Connection: close\r\n\r\n";
        response += body_;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QTcpServer *server_ = nullptr;
    QHash<QTcpSocket *, QByteArray> buffers_;
    QStringList requestedPaths_;
    int status_ = 200;
    QByteArray body_ = QByteArrayLiteral("{\"data\":[]}");
};

bool comboHasItem(const QComboBox *combo, const QString &text)
{
    return combo != nullptr && combo->findText(text) >= 0;
}

// Pumps the GUI event loop until the Detect probe's completion slot ran (the
// button is re-enabled by it) or the timeout expires.
bool waitForFimDetect(SettingsPanel *panel, int timeoutMs)
{
    if (panel == nullptr) {
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QPushButton *button = panel->fimDetectButton();
        if (button != nullptr && button->isEnabled()) {
            return true;
        }
        QThread::msleep(1);
    }
    return false;
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

    // batch-20261113 T5 PASS A: the NON-secret model cache a previous detect
    // wrote beside settings.toml. It is what the editable Model combo opens with;
    // it must never replace the stored `fim.model`.
    {
        QJsonArray models;
        models.append(QStringLiteral("deepseek-flash"));
        models.append(QStringLiteral("deepseek-v4-pro"));
        QJsonObject sidecar;
        sidecar.insert(QStringLiteral("models"), models);
        check("fim.models-sidecar.fixture-written",
              writeText(directory + QStringLiteral("/fim_models.json"),
                        QJsonDocument(sidecar).toJson(QJsonDocument::Compact)));
    }

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
              && panel->fimProviderCombo() != nullptr
              && panel->fimEndpointEdit() != nullptr && panel->fimModelCombo() != nullptr
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

    // batch-20261113 T5 PASS A — the provider preset writes the provider's
    // endpoint AND protocol. DeepSeek's assertion is the EXACT completion URL on
    // purpose: a bare host still looks plausible but makes the transport request
    // `/beta/v1/completions`, so "starts with api.deepseek.com" is not enough.
    if (QComboBox *provider = fimRestart.panel->fimProviderCombo()) {
        const int deepseekIndex = provider->findData(QStringLiteral("deepseek"));
        check("fim.provider-preset.deepseek-present", deepseekIndex >= 0);
        if (deepseekIndex >= 0) {
            provider->setCurrentIndex(deepseekIndex);
            const FimValues preset = fimRestart.panel->fimValues();
            check("fim.provider-preset.deepseek-writes-the-full-completion-url",
                  preset.endpoint == QStringLiteral("https://api.deepseek.com/beta/completions")
                      && preset.protocol == QString::fromLatin1(keys::kFimProtocolOpenAi),
                  preset.endpoint + QStringLiteral(" ") + preset.protocol);
        }
    } else {
        check("fim.provider-preset.provider-combo", false);
    }
    // The Model combo opens with the sidecar's list and STILL selects the stored
    // `fim.model` (the cache is a convenience, never the source of truth).
    check("fim.models-sidecar.loads-into-the-combo",
          fimRestart.panel->fimModelCombo() != nullptr
              && fimRestart.panel->fimModelCombo()->findText(QStringLiteral("deepseek-flash")) >= 0
              && fimRestart.panel->fimModelCombo()->findText(QStringLiteral("deepseek-v4-pro")) >= 0);
    check("fim.models-sidecar.keeps-the-stored-model",
          fimRestart.panel->fimModelCombo() != nullptr
              && fimRestart.panel->fimModelCombo()->currentText() == fim.model,
          fimRestart.panel->fimModelCombo() != nullptr
              ? fimRestart.panel->fimModelCombo()->currentText()
              : QStringLiteral("<no combo>"));
    // A corrupt cache is HARMLESS: the page still opens, the malformed content is
    // ignored, and the stored model is still present/selected. The harness is
    // built in its OWN statement: a `check(...)` argument list is evaluated in an
    // unspecified order, so dereferencing a not-yet-built panel from the detail
    // argument would be a null dereference.
    {
        // Settle the store write the preset above triggered BEFORE opening a
        // second client on the same file (every other harness in this test is
        // built after a flush for exactly this reason).
        check("fim.provider-preset.flushed", fimRestart.panel->flush());
        const QString sidecarPath = directory + QStringLiteral("/fim_models.json");
        check("fim.models-sidecar.malformed-fixture-written",
              writeText(sidecarPath, QByteArrayLiteral("{ this is not json, models: [oops")));
        Harness malformedCache;
        const bool malformedBuilt =
            buildHarness(&malformedCache, settingsPath, shortcutPath + QStringLiteral(".corrupt"));
        const QString malformedModel = malformedCache.panel != nullptr
                && malformedCache.panel->fimModelCombo() != nullptr
            ? malformedCache.panel->fimModelCombo()->currentText()
            : QStringLiteral("<no combo>");
        check("fim.models-sidecar.malformed-is-harmless",
              malformedBuilt && malformedModel == fim.model, malformedModel);

        // A well-formed object whose "models" array mixes types: only the string
        // entries are taken, the rest is ignored, and the page still opens.
        check("fim.models-sidecar.non-string-fixture-written",
              writeText(sidecarPath,
                        QByteArrayLiteral("{\"models\": [\"only-string\", 7, null]}")));
        Harness mixedCache;
        const bool mixedBuilt =
            buildHarness(&mixedCache, settingsPath, shortcutPath + QStringLiteral(".mixed"));
        const bool onlyStringTaken = mixedCache.panel != nullptr
                && mixedCache.panel->fimModelCombo() != nullptr
            ? mixedCache.panel->fimModelCombo()->findText(QStringLiteral("only-string")) >= 0
                && mixedCache.panel->fimModelCombo()->findText(QStringLiteral("7")) < 0
                && mixedCache.panel->fimModelCombo()->currentText() == fim.model
            : false;
        const QString mixedDetail = mixedCache.panel != nullptr
                && mixedCache.panel->fimModelCombo() != nullptr
            ? mixedCache.panel->fimModelCombo()->currentText()
            : QStringLiteral("<no combo>");
        check("fim.models-sidecar.non-string-entries-are-harmless",
              mixedBuilt && onlyStringTaken, mixedDetail);
    }

    // batch-20261113 T5 Pass B: the manual Detect button, driven exactly like a
    // user click (QPushButton::click) against a REAL local HTTP server on
    // 127.0.0.1 with an ephemeral port — nothing leaves the machine. The probe is
    // synchronous, so `fim.detect.runs-off-the-gui-thread` is the load-bearing
    // assertion: it compares the thread that called vd_fim_detect with this one.
    {
        FakeFimEndpoint fakeEndpoint;
        const bool serverUp = fakeEndpoint.listen();
        check("fim.detect.fake-server", serverUp, fakeEndpoint.baseUrl());
        QPushButton *detect = fimRestart.panel->fimDetectButton();
        QLabel *detectStatus = fimRestart.panel->fimDetectStatusLabel();
        check("fim.detect.widgets", detect != nullptr && detectStatus != nullptr);
        if (serverUp && detect != nullptr && detectStatus != nullptr) {
            // ---- success: models filled, endpoint normalized to completion_url
            fakeEndpoint.setBody(
                QByteArrayLiteral("{\"data\":[{\"id\":\"alpha\"},{\"id\":\"beta\"}]}"));
            // The probe reads the WIDGETS (the page is a deferred preview), never
            // the pending `entries_`.
            fimRestart.panel->fimEndpointEdit()->setText(fakeEndpoint.baseUrl());
            fimRestart.panel->fimModelCombo()->setCurrentText(QStringLiteral("user-model"));
            const Qt::HANDLE guiThread = QThread::currentThreadId();
            detect->click();
            // startFimDetect disables the button synchronously, before the worker
            // can report back through the queued signal.
            check("fim.detect.disabled-while-running", !detect->isEnabled());
            // Compute the wait BEFORE reading the label: a check() argument list
            // has an unspecified evaluation order, and this detail must describe
            // the state AFTER the probe settled, not the "Detecting…" moment.
            const bool successFinished = waitForFimDetect(fimRestart.panel.get(), 15000);
            check("fim.detect.finishes", successFinished, detectStatus->text());
            const Qt::HANDLE probeThread = fimRestart.panel->fimDetectCallThreadId();
            check("fim.detect.runs-off-the-gui-thread",
                  probeThread != nullptr && probeThread != guiThread,
                  QStringLiteral("probe-thread=0x%1 gui-thread=0x%2")
                      .arg(reinterpret_cast<quintptr>(probeThread), 0, 16)
                      .arg(reinterpret_cast<quintptr>(guiThread), 0, 16));
            check("fim.detect.success-fills-model-combo",
                  comboHasItem(fimRestart.panel->fimModelCombo(), QStringLiteral("alpha"))
                      && comboHasItem(fimRestart.panel->fimModelCombo(), QStringLiteral("beta"))
                      && fimRestart.panel->fimModelCombo()->currentText()
                          == QStringLiteral("user-model"),
                  fimRestart.panel->fimModelCombo()->currentText());
            check("fim.detect.success-writes-the-normalized-endpoint",
                  fimRestart.panel->fimEndpointEdit()->text()
                      == fakeEndpoint.baseUrl() + QStringLiteral("/v1/completions"),
                  fimRestart.panel->fimEndpointEdit()->text());
            check("fim.detect.status-succeeded",
                  detectStatus->text().contains(QStringLiteral("Detect succeeded")),
                  detectStatus->text());
            check("fim.detect.writes-the-sidecar",
                  readText(directory + QStringLiteral("/fim_models.json"))
                      .contains(QStringLiteral("alpha")));
            check("fim.detect.requests-reached-the-server",
                  !fakeEndpoint.requestedPaths().isEmpty(),
                  fakeEndpoint.requestedPaths().join(QLatin1Char(',')));

            // ---- failure: a 401 is a REAL failure (ok=false JSON with reason),
            // not the -1 argument path. Endpoint/model must stay as typed.
            fakeEndpoint.setStatus(401);
            fakeEndpoint.setBody(QByteArrayLiteral("{\"error\":\"unauthorized\"}"));
            fimRestart.panel->fimEndpointEdit()->setText(fakeEndpoint.baseUrl());
            fimRestart.panel->fimModelCombo()->setCurrentText(QStringLiteral("keep-me"));
            detect->click();
            const bool failureFinished = waitForFimDetect(fimRestart.panel.get(), 15000);
            check("fim.detect.failure-finishes", failureFinished, detectStatus->text());
            check("fim.detect.failure-keeps-the-users-input",
                  fimRestart.panel->fimEndpointEdit()->text() == fakeEndpoint.baseUrl()
                      && fimRestart.panel->fimModelCombo()->currentText()
                          == QStringLiteral("keep-me"),
                  fimRestart.panel->fimEndpointEdit()->text());
            check("fim.detect.failure-shows-the-reason",
                  detectStatus->text().contains(QStringLiteral("FIM HTTP 401")),
                  detectStatus->text());

            // ---- an empty endpoint is a JSON failure ("FIM 端点无效"), not -1 and
            // not a request: report it and keep the user's input.
            fimRestart.panel->fimEndpointEdit()->clear();
            fimRestart.panel->fimModelCombo()->setCurrentText(QStringLiteral("keep-empty"));
            detect->click();
            const bool emptyFinished = waitForFimDetect(fimRestart.panel.get(), 15000);
            check("fim.detect.empty-endpoint-finishes", emptyFinished, detectStatus->text());
            check("fim.detect.empty-endpoint-is-a-json-failure",
                  detectStatus->text().contains(QStringLiteral("Detect failed"))
                      && fimRestart.panel->fimEndpointEdit()->text().isEmpty()
                      && fimRestart.panel->fimModelCombo()->currentText()
                          == QStringLiteral("keep-empty"),
                  detectStatus->text());
        }
    }

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
        // Batch 20261110c appended the Environment page: the count grows from
        // 6 to 7 and the NEW check below pins that the extra tab IS that page
        // (the assertion is strengthened, never relaxed).
        check("settings.pages.dialog-tabs",
              dialog.panel() != nullptr && dialog.panel()->tabs() != nullptr
                  && dialog.panel()->tabs()->count() == 7);
        check("settings.pages.dialog-env-tab",
              dialog.panel() != nullptr && dialog.panel()->tabs() != nullptr
                  && dialog.environmentPage() != nullptr
                  && dialog.panel()->tabs()->widget(6) == dialog.environmentPage());
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

// --- batch-20261113 B1 (§12): FIM store ownership --------------------------
//
// The Preferences page must never SEED the FIM defaults into a store the user
// never configured: if it did, `fim.enabled = false` took over the request path
// and silently disabled a FIM configuration that lives in vedit.toml. Every case
// below drives the PRODUCT path (SettingsDialog -> deferred preview -> Apply ->
// SettingsPanel::commitPending) and reads the file the Rust store actually wrote;
// only the *fixture* (the pre-existing "old" store) is written by hand.
QStringList fimKeysInStore(const QString &text)
{
    QStringList keys;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (!line.startsWith(QStringLiteral("\"fim."))) {
            continue;
        }
        const int separator = line.indexOf(QStringLiteral("\" ="));
        if (separator > 0) {
            keys.append(line.mid(1, separator - 1));
        }
    }
    keys.sort();
    return keys;
}

QString storedFimValue(const QString &text, const QString &key)
{
    const QString prefix = QStringLiteral("\"%1\" = \"").arg(key);
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (!line.startsWith(prefix)) {
            continue;
        }
        const int start = prefix.size();
        const int end = line.lastIndexOf(QLatin1Char('"'));
        if (end >= start) {
            return line.mid(start, end - start);
        }
    }
    return {};
}

void checkFimStoreOwnership(const QString &directory)
{
    namespace keys = vedit::workspace::settings::keys;
    if (!SettingsClient::ffiAvailable()) {
        check("fim.ownership.ffi", false, "lane built without the vd-ffi settings ABI");
        return;
    }
    const QString shortcutBase = directory + QStringLiteral("/fim-ownership-shortcuts");

    // (a) empty store: open the dialog, never touch the FIM page, press Apply.
    const QString emptyPath = directory + QStringLiteral("/fim-ownership-empty.toml");
    QFile::remove(emptyPath);
    {
        Harness harness;
        check("fim.ownership.empty-harness",
              buildHarness(&harness, emptyPath, shortcutBase + QStringLiteral(".empty")));
        SettingsDialog dialog(&harness.themes, &harness.commands, emptyPath, nullptr);
        check("fim.ownership.empty-load", dialog.panel() != nullptr && dialog.panel()->flush());
        dialog.applyPending();
        check("fim.ownership.empty-apply-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString emptyStore = readText(emptyPath);
    check("fim.ownership.empty-store-has-no-fim-keys", fimKeysInStore(emptyStore).isEmpty(),
          fimKeysInStore(emptyStore).join(QLatin1Char(',')));

    // (a2) the dialog's OK button routes to the same commitPending; prove that
    // entry point on a fresh empty store too.
    const QString okPath = directory + QStringLiteral("/fim-ownership-ok.toml");
    QFile::remove(okPath);
    {
        Harness harness;
        check("fim.ownership.ok-harness",
              buildHarness(&harness, okPath, shortcutBase + QStringLiteral(".ok")));
        SettingsDialog dialog(&harness.themes, &harness.commands, okPath, nullptr);
        check("fim.ownership.ok-load", dialog.panel() != nullptr && dialog.panel()->flush());
        dialog.accept(); // OK: persist + close
        check("fim.ownership.ok-flush", dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString okStore = readText(okPath);
    check("fim.ownership.ok-store-has-no-fim-keys", fimKeysInStore(okStore).isEmpty(),
          fimKeysInStore(okStore).join(QLatin1Char(',')));

    // (b) same flow, but the user switches the Provider once: that IS "the user
    // edited the FIM page", so the page writes the ownership marker and owns the
    // fields the provider preset really expresses (endpoint + protocol); the
    // other fim.* fields stay unexpressed and keep inheriting vedit.toml.
    const QString editedPath = directory + QStringLiteral("/fim-ownership-edited.toml");
    QFile::remove(editedPath);
    {
        Harness harness;
        check("fim.ownership.edited-harness",
              buildHarness(&harness, editedPath, shortcutBase + QStringLiteral(".edited")));
        SettingsDialog dialog(&harness.themes, &harness.commands, editedPath, nullptr);
        check("fim.ownership.edited-load", dialog.panel() != nullptr && dialog.panel()->flush());
        QComboBox *provider =
            dialog.panel() != nullptr ? dialog.panel()->fimProviderCombo() : nullptr;
        const int deepseek =
            provider != nullptr ? provider->findData(QStringLiteral("deepseek")) : -1;
        check("fim.ownership.provider-preset-present", deepseek >= 0);
        if (deepseek >= 0) {
            // Exactly the currentIndexChanged signal a real click emits.
            provider->setCurrentIndex(deepseek);
        }
        dialog.applyPending();
        check("fim.ownership.edited-apply-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString editedStore = readText(editedPath);
    check("fim.ownership.edited-writes-marker",
          storedFimValue(editedStore, QStringLiteral("fim.ui_owned")) == QStringLiteral("true"),
          fimKeysInStore(editedStore).join(QLatin1Char(',')));
    check("fim.ownership.edited-writes-endpoint",
          storedFimValue(editedStore, QStringLiteral("fim.endpoint"))
              == QStringLiteral("https://api.deepseek.com/beta/completions"),
          storedFimValue(editedStore, QStringLiteral("fim.endpoint")));
    check("fim.ownership.edited-writes-protocol",
          storedFimValue(editedStore, QStringLiteral("fim.protocol"))
              == QString::fromLatin1(keys::kFimProtocolOpenAi),
          storedFimValue(editedStore, QStringLiteral("fim.protocol")));

    // (c) poisoned legacy store: an OLD build wrote `fim.enabled=false` without
    // the ownership marker. Apply must neither add the marker nor any other fim.*
    // key, and must not delete the key that was already there.
    const QString poisonedPath = directory + QStringLiteral("/fim-ownership-poisoned.toml");
    check("fim.ownership.poisoned-fixture",
          writeText(poisonedPath, QByteArray("\"fim.enabled\" = \"false\"\n")));
    {
        Harness harness;
        check("fim.ownership.poisoned-harness",
              buildHarness(&harness, poisonedPath, shortcutBase + QStringLiteral(".poisoned")));
        SettingsDialog dialog(&harness.themes, &harness.commands, poisonedPath, nullptr);
        check("fim.ownership.poisoned-load", dialog.panel() != nullptr && dialog.panel()->flush());
        dialog.applyPending();
        check("fim.ownership.poisoned-apply-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString poisonedStore = readText(poisonedPath);
    check("fim.ownership.poisoned-no-marker",
          storedFimValue(poisonedStore, QStringLiteral("fim.ui_owned")).isEmpty());
    check("fim.ownership.poisoned-keeps-the-existing-key",
          storedFimValue(poisonedStore, QStringLiteral("fim.enabled")) == QStringLiteral("false"),
          storedFimValue(poisonedStore, QStringLiteral("fim.enabled")));
    check("fim.ownership.poisoned-writes-no-other-fim-keys",
          fimKeysInStore(poisonedStore) == QStringList{QStringLiteral("fim.enabled")},
          fimKeysInStore(poisonedStore).join(QLatin1Char(',')));

    // (d) THE BACKFILL TRAP (§12 B1 requirement 3): every control is filled
    // programmatically from a store whose values ALL differ from the defaults, so
    // applyFimValues() really does toggle/set every widget. None of that is a user
    // edit: Apply must not create the marker, and every stored value must survive.
    const QString backfillPath = directory + QStringLiteral("/fim-ownership-backfill.toml");
    QByteArray backfillFixture;
    backfillFixture.append("\"fim.enabled\" = \"true\"\n");
    backfillFixture.append("\"fim.protocol\" = \"llama_cpp\"\n");
    backfillFixture.append("\"fim.endpoint\" = \"http://127.0.0.1:8080\"\n");
    backfillFixture.append("\"fim.model\" = \"qwen\"\n");
    backfillFixture.append("\"fim.timeout_ms\" = \"2500\"\n");
    backfillFixture.append("\"fim.max_tokens\" = \"128\"\n");
    backfillFixture.append("\"fim.ghost_text\" = \"true\"\n");
    check("fim.ownership.backfill-fixture", writeText(backfillPath, backfillFixture));
    {
        Harness harness;
        check("fim.ownership.backfill-harness",
              buildHarness(&harness, backfillPath, shortcutBase + QStringLiteral(".backfill")));
        SettingsDialog dialog(&harness.themes, &harness.commands, backfillPath, nullptr);
        check("fim.ownership.backfill-load", dialog.panel() != nullptr && dialog.panel()->flush());
        // Prove the backfill really ran: a negative check on a page that was never
        // filled proves nothing.
        const FimValues shown =
            dialog.panel() != nullptr ? dialog.panel()->fimValues() : FimValues{};
        check("fim.ownership.backfill-filled-the-widgets",
              shown.enabled && shown.ghostText
                  && shown.protocol == QString::fromLatin1(keys::kFimProtocolLlamaCpp)
                  && shown.endpoint == QStringLiteral("http://127.0.0.1:8080")
                  && shown.model == QStringLiteral("qwen") && shown.timeoutMs == 2500
                  && shown.maxTokens == 128);
        dialog.applyPending();
        check("fim.ownership.backfill-apply-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString backfillStore = readText(backfillPath);
    check("fim.ownership.backfill-no-marker",
          storedFimValue(backfillStore, QStringLiteral("fim.ui_owned")).isEmpty(),
          storedFimValue(backfillStore, QStringLiteral("fim.ui_owned")));
    check("fim.ownership.backfill-keeps-every-value",
          storedFimValue(backfillStore, QStringLiteral("fim.enabled")) == QStringLiteral("true")
              && storedFimValue(backfillStore, QStringLiteral("fim.protocol"))
                  == QStringLiteral("llama_cpp")
              && storedFimValue(backfillStore, QStringLiteral("fim.endpoint"))
                  == QStringLiteral("http://127.0.0.1:8080")
              && storedFimValue(backfillStore, QStringLiteral("fim.model"))
                  == QStringLiteral("qwen")
              && storedFimValue(backfillStore, QStringLiteral("fim.timeout_ms"))
                  == QStringLiteral("2500")
              && storedFimValue(backfillStore, QStringLiteral("fim.max_tokens"))
                  == QStringLiteral("128")
              && storedFimValue(backfillStore, QStringLiteral("fim.ghost_text"))
                  == QStringLiteral("true"),
          fimKeysInStore(backfillStore).join(QLatin1Char(',')));

    // (e) NON-FIRST promotion is non-destructive too (batch-20261113 F2, H1). The
    // store already declares ownership, but that only says the FIM page is
    // ALLOWED to own the block — it does not make every page value an expressed
    // one. A store that owns FIM but carries no `fim.enabled` (exactly what a
    // first promotion that never expressed Enabled leaves behind) must not gain
    // `fim.enabled=false` plus the page's timeout / max_tokens defaults just
    // because the user pressed Apply without touching the FIM page: the Rust side
    // inherits those fields from vedit.toml, and a seeded false would override it.
    //
    // This REPLACES the old `fim.ownership.owned-writes-the-page-values` check,
    // which demanded exactly that whole block and therefore locked the H1 defect
    // in as the expected behaviour.
    const QString ownedPath = directory + QStringLiteral("/fim-ownership-owned.toml");
    check("fim.ownership.owned-fixture",
          writeText(ownedPath, QByteArray("\"fim.ui_owned\" = \"true\"\n")));
    {
        Harness harness;
        check("fim.ownership.owned-harness",
              buildHarness(&harness, ownedPath, shortcutBase + QStringLiteral(".owned")));
        SettingsDialog dialog(&harness.themes, &harness.commands, ownedPath, nullptr);
        check("fim.ownership.owned-load", dialog.panel() != nullptr && dialog.panel()->flush());
        dialog.applyPending();
        check("fim.ownership.owned-apply-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString ownedStore = readText(ownedPath);
    check("fim.ownership.owned-marker-written",
          storedFimValue(ownedStore, QStringLiteral("fim.ui_owned")) == QStringLiteral("true"),
          fimKeysInStore(ownedStore).join(QLatin1Char(',')));
    check("fim.ownership.owned-writes-no-unexpressed-fields",
          fimKeysInStore(ownedStore) == QStringList{QStringLiteral("fim.ui_owned")},
          fimKeysInStore(ownedStore).join(QLatin1Char(',')));

    // (e2) H1's exact shape through the OK button: the store a first Detect
    // promotion leaves behind (marker + endpoint/protocol/model, and NO
    // `fim.enabled`, no timeout, no max_tokens). Open the dialog, press OK with
    // the FIM page never touched: nothing about the block may change. Before the
    // fix this wrote `fim.enabled=false`, `fim.timeout_ms=5000`, `fim.max_tokens=64`
    // and `fim.ghost_text=false` — a silent "off" for a vedit.toml user.
    const QString ownedOkPath = directory + QStringLiteral("/fim-ownership-owned-ok.toml");
    QByteArray ownedOkFixture;
    ownedOkFixture.append("\"fim.ui_owned\" = \"true\"\n");
    ownedOkFixture.append("\"fim.endpoint\" = \"https://api.deepseek.com/beta/completions\"\n");
    ownedOkFixture.append("\"fim.protocol\" = \"open_ai\"\n");
    ownedOkFixture.append("\"fim.model\" = \"deepseek-flash\"\n");
    check("fim.ownership.owned-ok-fixture", writeText(ownedOkPath, ownedOkFixture));
    {
        Harness harness;
        check("fim.ownership.owned-ok-harness",
              buildHarness(&harness, ownedOkPath, shortcutBase + QStringLiteral(".owned-ok")));
        SettingsDialog dialog(&harness.themes, &harness.commands, ownedOkPath, nullptr);
        check("fim.ownership.owned-ok-load",
              dialog.panel() != nullptr && dialog.panel()->flush());
        dialog.accept(); // OK -> commitPending, with no FIM edit in this session
        check("fim.ownership.owned-ok-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString ownedOkStore = readText(ownedOkPath);
    check("fim.ownership.owned-ok-no-fim-enabled",
          storedFimValue(ownedOkStore, QStringLiteral("fim.enabled")).isEmpty(),
          storedFimValue(ownedOkStore, QStringLiteral("fim.enabled")));
    check("fim.ownership.owned-ok-no-page-defaults",
          storedFimValue(ownedOkStore, QStringLiteral("fim.timeout_ms")).isEmpty()
              && storedFimValue(ownedOkStore, QStringLiteral("fim.max_tokens")).isEmpty()
              && storedFimValue(ownedOkStore, QStringLiteral("fim.ghost_text")).isEmpty(),
          fimKeysInStore(ownedOkStore).join(QLatin1Char(',')));
    check("fim.ownership.owned-ok-keeps-the-existing-keys",
          fimKeysInStore(ownedOkStore)
              == QStringList{QStringLiteral("fim.endpoint"), QStringLiteral("fim.model"),
                             QStringLiteral("fim.protocol"), QStringLiteral("fim.ui_owned")},
          fimKeysInStore(ownedOkStore).join(QLatin1Char(',')));

    // (e3) the other half of the frozen rule: a NON-FIRST promotion still writes
    // what the user DID express — and only that. Max tokens is edited; the
    // existing enabled / endpoint keys must survive untouched and no
    // protocol / timeout / ghost_text key may be seeded from the page.
    const QString partialPath = directory + QStringLiteral("/fim-ownership-partial.toml");
    QByteArray partialFixture;
    partialFixture.append("\"fim.ui_owned\" = \"true\"\n");
    partialFixture.append("\"fim.enabled\" = \"true\"\n");
    partialFixture.append("\"fim.endpoint\" = \"http://127.0.0.1:9000\"\n");
    check("fim.ownership.partial-fixture", writeText(partialPath, partialFixture));
    {
        Harness harness;
        check("fim.ownership.partial-harness",
              buildHarness(&harness, partialPath, shortcutBase + QStringLiteral(".partial")));
        SettingsDialog dialog(&harness.themes, &harness.commands, partialPath, nullptr);
        check("fim.ownership.partial-load",
              dialog.panel() != nullptr && dialog.panel()->flush());
        if (QSpinBox *tokens = dialog.panel() != nullptr
                                   ? dialog.panel()->fimMaxTokensSpin()
                                   : nullptr) {
            tokens->setValue(384); // the real user edit: valueChanged -> expressed
            check("fim.ownership.partial-edit", true);
        } else {
            check("fim.ownership.partial-edit", false);
        }
        dialog.applyPending();
        check("fim.ownership.partial-apply-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString partialStore = readText(partialPath);
    check("fim.ownership.owned-writes-only-the-expressed-field",
          storedFimValue(partialStore, QStringLiteral("fim.max_tokens")) == QStringLiteral("384")
              && storedFimValue(partialStore, QStringLiteral("fim.enabled"))
                  == QStringLiteral("true")
              && storedFimValue(partialStore, QStringLiteral("fim.endpoint"))
                  == QStringLiteral("http://127.0.0.1:9000")
              && fimKeysInStore(partialStore)
                  == QStringList{QStringLiteral("fim.enabled"), QStringLiteral("fim.endpoint"),
                                 QStringLiteral("fim.max_tokens"), QStringLiteral("fim.ui_owned")},
          fimKeysInStore(partialStore).join(QLatin1Char(',')));

    // (f1) M2 — Reload must clear the per-field mask. The user edits Timeout, then
    // presses Reload (the store never carried `fim.timeout_ms`), then Apply: the
    // discarded edit must not resurrect as the page's 5000 ms default, which is an
    // unexpressed field being written. Before the fix `fim.timeout_ms=5000` landed
    // in the store even though the store never had it and the edit was discarded.
    const QString reloadPath = directory + QStringLiteral("/fim-ownership-reload.toml");
    QByteArray reloadFixture;
    reloadFixture.append("\"fim.ui_owned\" = \"true\"\n");
    reloadFixture.append("\"fim.enabled\" = \"true\"\n");
    check("fim.ownership.reload-fixture", writeText(reloadPath, reloadFixture));
    {
        Harness harness;
        check("fim.ownership.reload-harness",
              buildHarness(&harness, reloadPath, shortcutBase + QStringLiteral(".reload")));
        SettingsDialog dialog(&harness.themes, &harness.commands, reloadPath, nullptr);
        check("fim.ownership.reload-load", dialog.panel() != nullptr && dialog.panel()->flush());
        if (QSpinBox *timeout = dialog.panel() != nullptr
                                    ? dialog.panel()->fimTimeoutSpin()
                                    : nullptr) {
            timeout->setValue(3500); // expresses TimeoutMs in this session
            check("fim.ownership.reload-edit", true);
        } else {
            check("fim.ownership.reload-edit", false);
        }
        if (dialog.panel() != nullptr) {
            dialog.panel()->reload(); // the Reload button's slot
        }
        check("fim.ownership.reload-flush", dialog.panel() != nullptr && dialog.panel()->flush());
        dialog.applyPending();
        check("fim.ownership.reload-apply-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString reloadStore = readText(reloadPath);
    check("fim.ownership.reload-mask-cleared",
          fimKeysInStore(reloadStore)
              == QStringList{QStringLiteral("fim.enabled"), QStringLiteral("fim.ui_owned")},
          fimKeysInStore(reloadStore).join(QLatin1Char(',')));
    check("fim.ownership.reload-writes-no-default-timeout",
          storedFimValue(reloadStore, QStringLiteral("fim.timeout_ms")).isEmpty(),
          storedFimValue(reloadStore, QStringLiteral("fim.timeout_ms")));

    // (f2) M2 — Reload must re-capture whether the STORE already carries the
    // marker. The file gains `fim.ui_owned` on disk between load and reload; after
    // the reload the panel must treat the store as the owner (NON-first
    // promotion), so the first-promotion branch's deletion of the pre-existing,
    // never-expressed `fim.enabled` must not run.
    const QString recapturePath = directory + QStringLiteral("/fim-ownership-recapture.toml");
    check("fim.ownership.recapture-fixture",
          writeText(recapturePath, QByteArray("\"fim.enabled\" = \"false\"\n")));
    {
        Harness harness;
        check("fim.ownership.recapture-harness",
              buildHarness(&harness, recapturePath, shortcutBase + QStringLiteral(".recapture")));
        SettingsDialog dialog(&harness.themes, &harness.commands, recapturePath, nullptr);
        check("fim.ownership.recapture-load",
              dialog.panel() != nullptr && dialog.panel()->flush());
        check("fim.ownership.recapture-store-edit",
              writeText(recapturePath, QByteArray("\"fim.enabled\" = \"false\"\n"
                                                  "\"fim.ui_owned\" = \"true\"\n")));
        if (dialog.panel() != nullptr) {
            dialog.panel()->reload();
        }
        check("fim.ownership.recapture-reload-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
        if (QSpinBox *tokens = dialog.panel() != nullptr
                                   ? dialog.panel()->fimMaxTokensSpin()
                                   : nullptr) {
            tokens->setValue(384); // expresses MaxTokens after the reload
            check("fim.ownership.recapture-edit", true);
        } else {
            check("fim.ownership.recapture-edit", false);
        }
        dialog.applyPending();
        check("fim.ownership.recapture-apply-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString recaptureStore = readText(recapturePath);
    check("fim.ownership.recapture-keeps-the-unexpressed-key",
          storedFimValue(recaptureStore, QStringLiteral("fim.enabled")) == QStringLiteral("false"),
          fimKeysInStore(recaptureStore).join(QLatin1Char(',')));
}

// --- batch-20261113 F1 (§13): Detect must not hand the store the WHOLE block --
//
// A legacy store (written by an older build's Apply) carries `fim.enabled=false`
// and an endpoint but NO ownership marker. The frozen rule is that ownership is
// promoted NON-destructively: the first promotion writes the marker plus only the
// fields the user expressed in this session (edited, or reported by a successful
// Detect) and DELETES the pre-existing `fim.*` keys that had no authority yet.
// Before the fix, one Detect click wrote the page's whole block — marker AND
// `fim.enabled=false` — so the store took over the request path and silently
// disabled a `[fim]`-in-vedit.toml user, the B1 defect one click away.
void checkFimDetectPromotion(const QString &directory)
{
    namespace keys = vedit::workspace::settings::keys;
    if (!SettingsClient::ffiAvailable()) {
        check("fim.detect-promotion.ffi", false, "lane built without the vd-ffi settings ABI");
        return;
    }
    const QString shortcutBase = directory + QStringLiteral("/fim-detect-promotion-shortcuts");

    // (C1/C2) the legacy store, and a REAL local server the Detect click reaches.
    FakeFimEndpoint fakeEndpoint;
    const bool serverUp = fakeEndpoint.listen();
    check("fim.detect-promotion.server", serverUp, fakeEndpoint.baseUrl());
    if (!serverUp) {
        return;
    }
    fakeEndpoint.setBody(QByteArrayLiteral("{\"data\":[{\"id\":\"alpha\"},{\"id\":\"beta\"}]}"));
    const QString legacyPath = directory + QStringLiteral("/fim-detect-legacy.toml");
    QByteArray legacyFixture;
    legacyFixture.append("\"fim.enabled\" = \"false\"\n");
    legacyFixture.append("\"fim.endpoint\" = \"" + fakeEndpoint.baseUrl().toUtf8() + "\"\n");
    legacyFixture.append("\"fim.timeout_ms\" = \"1234\"\n");
    legacyFixture.append("\"fim.max_tokens\" = \"99\"\n");
    check("fim.detect-promotion.legacy-fixture", writeText(legacyPath, legacyFixture));
    {
        Harness harness;
        check("fim.detect-promotion.legacy-harness",
              buildHarness(&harness, legacyPath, shortcutBase + QStringLiteral(".legacy")));
        SettingsDialog dialog(&harness.themes, &harness.commands, legacyPath, nullptr);
        check("fim.detect-promotion.legacy-load",
              dialog.panel() != nullptr && dialog.panel()->flush());
        // Prove the page really shows the legacy values (a page that was never
        // filled would make the negative checks below prove nothing).
        const FimValues shown =
            dialog.panel() != nullptr ? dialog.panel()->fimValues() : FimValues{};
        check("fim.detect-promotion.legacy-page-filled",
              !shown.enabled && shown.endpoint == fakeEndpoint.baseUrl() && shown.timeoutMs == 1234,
              QStringLiteral("enabled=%1 endpoint=[%2] timeout=%3")
                  .arg(shown.enabled ? 1 : 0)
                  .arg(shown.endpoint)
                  .arg(shown.timeoutMs));
        QPushButton *detect =
            dialog.panel() != nullptr ? dialog.panel()->fimDetectButton() : nullptr;
        check("fim.detect-promotion.detect-button", detect != nullptr);
        if (detect == nullptr) {
            return;
        }
        // The ONLY user action: one Detect click. The FIM page is never edited.
        detect->click();
        const bool finished = waitForFimDetect(dialog.panel(), 15000);
        check("fim.detect-promotion.detect-succeeded", finished,
              dialog.panel() != nullptr && dialog.panel()->fimDetectStatusLabel() != nullptr
                  ? dialog.panel()->fimDetectStatusLabel()->text()
                  : QString());
        dialog.applyPending();
        check("fim.detect-promotion.legacy-apply-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString legacyStore = readText(legacyPath);
    check("fim.detect-promotion.legacy-marker",
          storedFimValue(legacyStore, QStringLiteral("fim.ui_owned")) == QStringLiteral("true"),
          fimKeysInStore(legacyStore).join(QLatin1Char(',')));
    // C1 RED before the fix (`fim.enabled=false` survived into the promoted
    // store); C2 GREEN after it: the key was neither edited nor detected, so the
    // first promotion deletes it.
    check("fim.detect-promotion.legacy-no-fim-enabled",
          !legacyStore.contains(QStringLiteral("\"fim.enabled\"")),
          storedFimValue(legacyStore, QStringLiteral("fim.enabled")));
    check("fim.detect-promotion.legacy-detected-fields",
          storedFimValue(legacyStore, QStringLiteral("fim.endpoint"))
                  == fakeEndpoint.baseUrl() + QStringLiteral("/v1/completions")
              && storedFimValue(legacyStore, QStringLiteral("fim.protocol"))
                  == QString::fromLatin1(keys::kFimProtocolOpenAi)
              && storedFimValue(legacyStore, QStringLiteral("fim.model"))
                  == QStringLiteral("alpha"),
          QStringLiteral("endpoint=[%1] protocol=[%2] model=[%3]")
              .arg(storedFimValue(legacyStore, QStringLiteral("fim.endpoint")),
                   storedFimValue(legacyStore, QStringLiteral("fim.protocol")),
                   storedFimValue(legacyStore, QStringLiteral("fim.model"))));
    check("fim.detect-promotion.legacy-drops-unexpressed",
          fimKeysInStore(legacyStore)
              == QStringList{QStringLiteral("fim.endpoint"), QStringLiteral("fim.model"),
                             QStringLiteral("fim.protocol"), QStringLiteral("fim.ui_owned")},
          fimKeysInStore(legacyStore).join(QLatin1Char(',')));

    // (C3a) empty store, FIM page never touched, Apply: still not one fim.* key.
    const QString emptyPath = directory + QStringLiteral("/fim-detect-empty.toml");
    QFile::remove(emptyPath);
    {
        Harness harness;
        check("fim.detect-promotion.empty-harness",
              buildHarness(&harness, emptyPath, shortcutBase + QStringLiteral(".empty")));
        SettingsDialog dialog(&harness.themes, &harness.commands, emptyPath, nullptr);
        check("fim.detect-promotion.empty-load",
              dialog.panel() != nullptr && dialog.panel()->flush());
        dialog.applyPending();
        check("fim.detect-promotion.empty-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString emptyStore = readText(emptyPath);
    check("fim.detect-promotion.empty-store-still-empty",
          fimKeysInStore(emptyStore).isEmpty(),
          fimKeysInStore(emptyStore).join(QLatin1Char(',')));

    // (C3b) empty store + ONE Provider switch + Apply: the marker plus the two
    // fields the preset wrote — and NOT `fim.enabled` (nothing expressed it).
    const QString presetPath = directory + QStringLiteral("/fim-detect-preset.toml");
    QFile::remove(presetPath);
    {
        Harness harness;
        check("fim.detect-promotion.preset-harness",
              buildHarness(&harness, presetPath, shortcutBase + QStringLiteral(".preset")));
        SettingsDialog dialog(&harness.themes, &harness.commands, presetPath, nullptr);
        check("fim.detect-promotion.preset-load",
              dialog.panel() != nullptr && dialog.panel()->flush());
        QComboBox *provider =
            dialog.panel() != nullptr ? dialog.panel()->fimProviderCombo() : nullptr;
        const int deepseek =
            provider != nullptr ? provider->findData(QStringLiteral("deepseek")) : -1;
        check("fim.detect-promotion.preset-present", deepseek >= 0);
        if (deepseek >= 0) {
            provider->setCurrentIndex(deepseek);
        }
        dialog.applyPending();
        check("fim.detect-promotion.preset-flush",
              dialog.panel() != nullptr && dialog.panel()->flush());
    }
    const QString presetStore = readText(presetPath);
    check("fim.detect-promotion.preset-no-fim-enabled",
          !presetStore.contains(QStringLiteral("\"fim.enabled\"")),
          storedFimValue(presetStore, QStringLiteral("fim.enabled")));
    check("fim.detect-promotion.preset-keys",
          fimKeysInStore(presetStore)
              == QStringList{QStringLiteral("fim.endpoint"), QStringLiteral("fim.protocol"),
                             QStringLiteral("fim.ui_owned")},
          fimKeysInStore(presetStore).join(QLatin1Char(',')));
    check("fim.detect-promotion.preset-values",
          storedFimValue(presetStore, QStringLiteral("fim.endpoint"))
                  == QStringLiteral("https://api.deepseek.com/beta/completions")
              && storedFimValue(presetStore, QStringLiteral("fim.protocol"))
                  == QString::fromLatin1(keys::kFimProtocolOpenAi),
          storedFimValue(presetStore, QStringLiteral("fim.endpoint")));
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
    // Batch 20261110b (authorised contract change): `--autofix` is WIRED.
    // `VdLintBatchOptions` carries `verible_autofix` / `autofix_output_file`, so
    // the knob is supported and the value is transported (the old
    // `settings.a4.lint.state.autofix-pending` assertion pinned "stored but not
    // sent"; the old->new basis and logs are in
    // `evidence/batch-20261110b/lint-engine/AUTHORIZATION.md`).
    check("settings.a4.lint.state.autofix-transported",
          a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix).kind
                      == a4::LintKnobStateKind::Supported
              && a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix).enabled()
              && !a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix).pending()
              && a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix)
                     .reason.isEmpty()
              && a4::lintAutofixCode(QString::fromLatin1(keys::kAutofixPatch)) == 1
              && a4::lintAutofixCode(QString::fromLatin1(keys::kAutofixGenerateWaiver)) == 5,
          a4::lintKnobState(a4::LintBackend::Verible, a4::LintKnob::Autofix).reason);

    // AC-1.4 / AC-T8.2: the two interactive modes are NOT pretend-supported:
    // they carry the no-TTY reason, while every non-interactive value stays
    // available (the engine still transports a stored interactive value).
    const a4::LintAutofixValue patchInteractive =
        a4::lintAutofixValue(QString::fromLatin1(keys::kAutofixPatchInteractive));
    const a4::LintAutofixValue inplaceInteractive =
        a4::lintAutofixValue(QString::fromLatin1(keys::kAutofixInplaceInteractive));
    check("settings.a4.lint.state.autofix-interactive-unavailable",
          !patchInteractive.available && !inplaceInteractive.available
              && patchInteractive.reason.contains(QStringLiteral("TTY"))
              && inplaceInteractive.reason.contains(QStringLiteral("TTY"))
              && patchInteractive.code == 2 && inplaceInteractive.code == 4
              && a4::lintAutofixValue(QString::fromLatin1(keys::kAutofixPatch)).available
              && a4::lintAutofixValue(QString::fromLatin1(keys::kAutofixInplace)).available
              && a4::lintAutofixValue(QString::fromLatin1(keys::kAutofixGenerateWaiver)).available,
          patchInteractive.reason);
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

    // (4) --autofix: the ABI carries it now, so the stored value reaches the
    // real argv (batch 20261110b; this replaces the old "not sent" assertion).
    knob = a4::LintValues{};
    knob.verible.autofix = QString::fromLatin1(keys::kAutofixPatch);
    const QString autofixArgv = batchArgv(knob, a4::LintBackend::Verible);
    check("settings.a4.lint.argv.autofix",
          autofixArgv.contains(QStringLiteral("--autofix=patch")), autofixArgv);

    // (4b) --autofix_output_file and slang's own --waiver-file are filled by
    // the same one mapping (fillLintBatchOptions), not by a second path.
    a4::LintJobOptions fixJob =
        a4::buildLintJobOptions(knob, a4::LintBackend::Verible, QString(), QString());
    fixJob.autofixOutputFile = QStringLiteral("/tmp/vedit-fixes.patch");
    VdLintBatchOptions fixAbi{};
    a4::LintBatchAbiStorage fixStorage;
    a4::fillLintBatchOptions(fixJob, &fixAbi, &fixStorage);
    char *fixLine = vd_lint_batch_command_line(&fixAbi, "f.sv");
    QString fixArgv = fixLine != nullptr ? QString::fromUtf8(fixLine) : QString();
    if (fixLine != nullptr) {
        vd_string_free(fixLine);
    }
    const int fixNote = fixArgv.indexOf(QStringLiteral(" | "));
    if (fixNote >= 0) {
        fixArgv.truncate(fixNote);
    }
    check("settings.a4.lint.argv.autofix-output-file",
          fixArgv.contains(QStringLiteral("--autofix_output_file=/tmp/vedit-fixes.patch")),
          fixArgv);

    const QString waiverPath = directory + QStringLiteral("/slang-waivers.toml");
    const bool waiverWritten = writeText(
        waiverPath,
        QByteArray("[[waivers]]\nfile = \"f.sv\"\ndiagnostic = \"implicit-net\"\n"));
    a4::LintValues slangWaiver;
    slangWaiver.slang.waiverFile = waiverPath;
    const QString slangWaiverArgv = batchArgv(slangWaiver, a4::LintBackend::Slang);
    check("settings.a4.lint.argv.slang-waiver-file",
          waiverWritten
              && slangWaiverArgv.contains(QStringLiteral("--waiver-file ") + waiverPath),
          slangWaiverArgv);

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

// --- batch 20261110c: Preferences → Environment (AC-4.3/4.4, AC-N5) --------

void pumpGuiEvents(int rounds = 20)
{
    for (int round = 0; round < rounds; ++round) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

// Batch 20261110e: the Preferences dialog now scrolls its content (size
// discipline, batch 20261110d), so a control below the fold is NOT under the
// screen point it maps to until the viewport is scrolled — `widgetAt` returns
// null there. A real user scrolls the wheel before clicking; the probe does the
// same: every QScrollArea between the widget and the dialog is scrolled just
// enough to bring the widget's rect inside its viewport, and the layout is
// pumped before the hit test computes the GLOBAL point. A widget that still
// cannot be brought into view is left where it is — the hit test then fails and
// the probe reports it, rather than silently clicking a wrong control.
void scrollTargetIntoView(QWidget *target)
{
    if (target == nullptr) {
        return;
    }
    constexpr int kMargin = 12;
    for (QWidget *ancestor = target->parentWidget(); ancestor != nullptr;
         ancestor = ancestor->parentWidget()) {
        auto *area = qobject_cast<QScrollArea *>(ancestor);
        if (area == nullptr || !area->viewport()->isAncestorOf(target)) {
            continue;
        }
        QWidget *viewport = area->viewport();
        const QRect rect(target->mapTo(viewport, QPoint(0, 0)), target->size());
        const QRect visible = viewport->rect();
        int delta = 0;
        if (rect.height() >= visible.height() - 2 * kMargin) {
            // Taller than the viewport: its top edge is what a user can reach first.
            delta = rect.top() - (visible.top() + kMargin);
        } else if (rect.top() < visible.top() + kMargin) {
            delta = rect.top() - (visible.top() + kMargin);
        } else if (rect.bottom() > visible.bottom() - kMargin) {
            delta = rect.bottom() - (visible.bottom() - kMargin);
        }
        if (delta != 0) {
            QScrollBar *bar = area->verticalScrollBar();
            bar->setValue(bar->value() + delta);
            pumpGuiEvents();
            std::printf("ENV-UI probe: scrolled %s into view by %d (scroll=%d)\n",
                        target->objectName().isEmpty() ? target->metaObject()->className()
                                                       : qPrintable(target->objectName()),
                        delta, bar->value());
        }
    }
}

// Real hit test + delivery (the xcb pattern of `vedit-fold-gutter-click`): the
// widget under the GLOBAL point is resolved with QApplication::widgetAt and the
// press/release go to THAT widget. `target` is the widget we believe owns the
// point; an ancestor/descendant of it is accepted (a viewport/child), anything
// else fails the check instead of silently clicking the wrong control.
bool clickUnderPoint(QWidget *target, const QPoint &global, const char *label)
{
    if (target == nullptr) {
        std::printf("ENV-UI probe: no target for %s\n", label);
        return false;
    }
    QWidget *under = QApplication::widgetAt(global);
    if (under == nullptr
        || (under != target && !target->isAncestorOf(under) && !under->isAncestorOf(target))) {
        std::printf("ENV-UI probe: no hit for %s at %d,%d under=%s\n", label, global.x(),
                    global.y(),
                    under == nullptr ? "null" : qPrintable(under->objectName()));
        return false;
    }
    const QPoint local = under->mapFromGlobal(global);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(under, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), QPointF(global),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(under, &release);
    pumpGuiEvents();
    return true;
}

bool clickCenter(QWidget *widget, const char *label)
{
    if (widget == nullptr) {
        std::printf("ENV-UI probe: no widget for %s\n", label);
        return false;
    }
    // Scroll the control into the visible part of a scrolling Preferences page
    // BEFORE computing its screen point (batch 20261110d made the content scroll).
    scrollTargetIntoView(widget);
    return clickUnderPoint(widget, widget->mapToGlobal(widget->rect().center()), label);
}

int environmentRowNamed(const EnvImportPage &page, const QString &name)
{
    for (int row = 0; row < page.diffRowCount(); ++row) {
        if (page.diffName(row) == name) {
            return row;
        }
    }
    return -1;
}

int storedRowNamed(const EnvImportPage &page, const QString &name, const QString &root,
                   QString *value)
{
    for (int row = 0; row < page.selectionRowCount(); ++row) {
        if (page.selectionName(row) == name && page.selectionRoot(row) == root) {
            if (value != nullptr) {
                *value = page.selectionValue(row);
            }
            return row;
        }
    }
    return -1;
}

// The fixture the diff checks use: three assignments a shell-only environment
// changes, one secret-shaped name, and one that is identical in the process.
bool writeEnvironmentFixture(const QString &path)
{
    return writeText(path, QByteArray("export VEDIT_ENV_UI_ADDED=from-shell\n"
                                      "export VEDIT_ENV_UI_CHANGED=from-shell\n"
                                      "export VEDIT_ENV_UI_UNCHANGED=unchanged\n"
                                      "export VEDIT_ENV_UI_SECRET_TOKEN=fixture-secret-value\n"));
}

void prepareEnvironmentFixtureProcess()
{
    qputenv("VEDIT_ENV_UI_CHANGED", "from-process");
    qputenv("VEDIT_ENV_UI_UNCHANGED", "unchanged");
    qunsetenv("VEDIT_ENV_UI_ADDED");
    qunsetenv("VEDIT_ENV_UI_SECRET_TOKEN");
}

void checkEnvironmentPage(const QString &directory)
{
    if (!SettingsClient::ffiAvailable()) {
        check("env-import.ffi.available", false,
              "lane built without the vd-ffi environment ABI");
        return;
    }
    check("env-import.ffi.available", true);
    const QString base = directory + QStringLiteral("/env-import");
    check("env-import.mkdir", QDir().mkpath(base));
    const QString settingsPath = base + QStringLiteral("/settings.toml");
    const QString rcPath = base + QStringLiteral("/fixture.rc");
    check("env-import.fixture.rc", writeEnvironmentFixture(rcPath));
    prepareEnvironmentFixtureProcess();

    EnvImportPage page(settingsPath);
    const bool initialLoaded = page.flush();
    check("env-import.initial-load", initialLoaded, page.statusText());
    check("env-import.initial-empty", page.selectionRowCount() == 0,
          QStringLiteral("rows=%1").arg(page.selectionRowCount()));
    page.setProjectRoot(base);
    page.setProjectScope(false);
    page.setShellCode(0);
    page.setSourceFile(rcPath);
    check("env-import.scope.default-project", !page.projectScope());
    const bool captureStarted = page.startCapture();
    check("env-import.capture.start", captureStarted, page.statusText());
    const bool captureFinished = page.flush(60000);
    check("env-import.capture.done", captureFinished, page.captureReport());

    const int addedRow = environmentRowNamed(page, QStringLiteral("VEDIT_ENV_UI_ADDED"));
    const int changedRow = environmentRowNamed(page, QStringLiteral("VEDIT_ENV_UI_CHANGED"));
    const int unchangedRow = environmentRowNamed(page, QStringLiteral("VEDIT_ENV_UI_UNCHANGED"));
    const int secretRow = environmentRowNamed(page, QStringLiteral("VEDIT_ENV_UI_SECRET_TOKEN"));
    check("env-import.diff.added", addedRow >= 0 && page.diffKind(addedRow) == QStringLiteral("added"),
          QStringLiteral("row=%1").arg(addedRow));
    check("env-import.diff.changed",
          changedRow >= 0 && page.diffKind(changedRow) == QStringLiteral("changed"),
          QStringLiteral("row=%1 kind=%2")
              .arg(changedRow)
              .arg(changedRow >= 0 ? page.diffKind(changedRow) : QString()));
    check("env-import.diff.unchanged-absent", unchangedRow < 0,
          QStringLiteral("row=%1").arg(unchangedRow));
    bool noneChecked = true;
    for (int row = 0; row < page.diffRowCount(); ++row) {
        if (page.diffChecked(row)) {
            noneChecked = false;
        }
    }
    check("env-import.diff.default-unchecked", noneChecked,
          QStringLiteral("rows=%1").arg(page.diffRowCount()));
    check("env-import.diff.mask",
          secretRow >= 0 && page.diffDisplayValue(secretRow) == QStringLiteral("<masked>")
              && page.diffCapturedValue(secretRow) == QStringLiteral("fixture-secret-value"),
          QStringLiteral("row=%1").arg(secretRow));
    check("env-import.diff.changed-value",
          changedRow >= 0 && page.diffDisplayValue(changedRow) == QStringLiteral("from-shell")
              && page.diffCapturedValue(changedRow) == QStringLiteral("from-shell"));
    check("env-import.stage.refuse-none", !page.stageImport());

    // Check the two ordinary rows and the secret-shaped one: the ordinary rows
    // are staged, the secret-shaped row is refused (masked, not persisted).
    if (addedRow >= 0) {
        page.setDiffChecked(addedRow, true);
    }
    if (changedRow >= 0) {
        page.setDiffChecked(changedRow, true);
    }
    if (secretRow >= 0) {
        page.setDiffChecked(secretRow, true);
    }
    const bool staged = page.stageImport();
    check("env-import.stage.selected", staged, page.statusText());
    check("env-import.stage.rows", page.selectionRowCount() == 2,
          QStringLiteral("rows=%1").arg(page.selectionRowCount()));
    check("env-import.stage.pending", page.hasPendingChanges());
    check("env-import.stage.no-write", !QFileInfo::exists(page.selectionPath()),
          page.selectionPath());
    QString stagedValue;
    check("env-import.stage.project-layer",
          storedRowNamed(page, QStringLiteral("VEDIT_ENV_UI_ADDED"), base, &stagedValue) >= 0
              && stagedValue == QStringLiteral("from-shell"),
          stagedValue);
    check("env-import.stage.secret-skipped",
          storedRowNamed(page, QStringLiteral("VEDIT_ENV_UI_SECRET_TOKEN"), base, nullptr) < 0);
    // Remove goes through the same draft path the "Remove checked" button uses.
    check("env-import.remove.none-checked", !page.removeSelectedStored());
    page.setSelectionRemoveChecked(0, true);
    const bool removed = page.removeSelectedStored();
    check("env-import.remove.checked", removed, page.statusText());
    check("env-import.remove.rows", page.selectionRowCount() == 1,
          QStringLiteral("rows=%1").arg(page.selectionRowCount()));
    page.discardPending();
    check("env-import.discard",
          !page.hasPendingChanges() && page.selectionRowCount() == 0,
          QStringLiteral("rows=%1").arg(page.selectionRowCount()));

    // Manual NAME=VALUE rows: one project row and one global row with the SAME
    // name, plus validation of the name rule the C ABI store applies.
    if (addedRow >= 0) {
        page.setDiffChecked(addedRow, true);
    }
    page.stageImport();
    check("env-import.add.validate-empty", !page.addVariable(QString(), QStringLiteral("x")));
    check("env-import.add.validate-equals",
          !page.addVariable(QStringLiteral("A=B"), QStringLiteral("x")));
    check("env-import.add.validate-control",
          !page.addVariable(QStringLiteral("A") + QChar(0x1b), QStringLiteral("x")));
    page.setProjectScope(false);
    const bool projectAdded =
        page.addVariable(QStringLiteral("VEDIT_ENV_UI_SCOPE"), QStringLiteral("project-value"));
    check("env-import.add.project", projectAdded, page.statusText());
    page.setProjectScope(true);
    const bool globalAdded =
        page.addVariable(QStringLiteral("VEDIT_ENV_UI_SCOPE"), QStringLiteral("global-value"));
    check("env-import.add.global", globalAdded, page.statusText());
    page.setProjectScope(false);
    check("env-import.effective.project-wins",
          page.effectiveValue(QStringLiteral("VEDIT_ENV_UI_SCOPE"))
              == QStringLiteral("project-value"),
          page.effectiveValue(QStringLiteral("VEDIT_ENV_UI_SCOPE")));
    const bool commitStarted = page.commitPending();
    check("env-import.commit", commitStarted, page.statusText());
    const bool commitFinished = page.flush();
    check("env-import.commit.done", commitFinished, page.statusText());
    check("env-import.commit.no-pending", !page.hasPendingChanges());
    const QString stored = readText(page.selectionPath());
    check("env-import.file.exists", QFileInfo::exists(page.selectionPath()),
          page.selectionPath());
    check("env-import.file.both-layers",
          stored.contains(QStringLiteral("\"root\":\"") + base + QStringLiteral("\""))
              && stored.contains(QStringLiteral("VEDIT_ENV_UI_SCOPE"))
              && stored.contains(QStringLiteral("\"value\":\"global-value\""))
              && stored.contains(QStringLiteral("\"value\":\"project-value\"")),
          stored);
    check("env-import.file.secret-absent",
          !stored.contains(QStringLiteral("fixture-secret-value")));
    check("env-import.file.no-wholesale",
          !stored.contains(QStringLiteral("\"name\":\"VEDIT_ENV_UI_UNCHANGED\""))
              && !stored.contains(QStringLiteral("\"name\":\"PATH\"")));

    // Restart round-trip: a NEW page reads the same file through the C ABI.
    {
        EnvImportPage restarted(settingsPath);
        const bool restartLoaded = restarted.flush();
        check("env-import.restart.load", restartLoaded, restarted.statusText());
        restarted.setProjectRoot(base);
        QString roundTripped;
        check("env-import.restart.project-row",
              storedRowNamed(restarted, QStringLiteral("VEDIT_ENV_UI_ADDED"), base, &roundTripped)
                      >= 0
                  && roundTripped == QStringLiteral("from-shell"),
              roundTripped);
        check("env-import.restart.global-row",
              storedRowNamed(restarted, QStringLiteral("VEDIT_ENV_UI_SCOPE"), QString(), nullptr)
                  >= 0);
        check("env-import.restart.effective",
              restarted.effectiveValue(QStringLiteral("VEDIT_ENV_UI_SCOPE"))
                  == QStringLiteral("project-value"));
        check("env-import.restart.no-pending", !restarted.hasPendingChanges());
    }

    // A hand-broken file degrades to an empty selection plus a warning; it never
    // crashes the page (AC-4.4).
    check("env-import.broken.write",
          writeText(base + QStringLiteral("/environment.json"),
                    QByteArray("{\"version\": 1, \"global\": [ this is not json\n")));
    {
        EnvImportPage broken(settingsPath);
        const bool brokenLoaded = broken.flush();
        check("env-import.broken.no-crash", brokenLoaded, broken.statusText());
        check("env-import.broken.empty", broken.selectionRowCount() == 0,
              QStringLiteral("rows=%1").arg(broken.selectionRowCount()));
        check("env-import.broken.warning", !broken.warnings().isEmpty(),
              broken.warnings().join(QStringLiteral("; ")));
    }

    // --- E2 (batch-20261114): the candidate list is the capture diff filtered by
    // the names the project's filelist actually REFERENCES, never by a name
    // blacklist. "A PATH-like variable the project never mentions" is the user's
    // own example. The referenced set + the layer each name resolved in come from
    // the REAL project model expansion (the same `expansionInputs()` payload the
    // product publishes on `vedit.project.inputs`).
#if defined(VEDIT_HAVE_PROJECT_LANE)
    const QString e2Base = base + QStringLiteral("/e2");
    check("e2.mkdir", QDir().mkpath(e2Base));
    check("e2.fixture.file", writeText(e2Base + QStringLiteral("/file.v"),
                                       QByteArray("module m;\nendmodule\n")));
    // Four referenced names with three different providers, plus one name the
    // filelist never mentions (the "PATH/API key" case). `VEDIT_E2_ENVIRONMENT`
    // is exported in THIS process before the expansion, so its layer is
    // `environment`; `VEDIT_E2_PROJECTED` lives in the project table; the other
    // two are referenced but unresolved (layer `missing`).
    const QString e2Filelist = e2Base + QStringLiteral("/project.f");
    check("e2.fixture.filelist",
          writeText(e2Filelist,
                    QByteArray("$VEDIT_E2_ENVIRONMENT/file.v\n"
                               "$VEDIT_E2_PROJECTED/file.v\n"
                               "$VEDIT_E2_MISSING/file.v\n"
                               "$VEDIT_E2_REFERENCED/file.v\n")));
    qputenv("VEDIT_E2_ENVIRONMENT", QByteArray("."));
    qunsetenv("VEDIT_E2_PROJECTED");
    qunsetenv("VEDIT_E2_MISSING");
    qunsetenv("VEDIT_E2_REFERENCED");
    qunsetenv("VEDIT_E2_UNREFERENCED");
    const QString e2Rc = e2Base + QStringLiteral("/candidates.rc");
    check("e2.fixture.rc",
          writeText(e2Rc, QByteArray("export VEDIT_E2_ENVIRONMENT=from-shell\n"
                                     "export VEDIT_E2_PROJECTED=from-shell\n"
                                     "export VEDIT_E2_MISSING=from-shell\n"
                                     "export VEDIT_E2_REFERENCED=from-shell\n"
                                     "export VEDIT_E2_UNREFERENCED=from-shell\n")));

    // A real expansion of the real `.f`: no fake payload, no hand-written map.
    ProjectModel model(e2Base + QStringLiteral("/state"));
    bool e2Settled = false;
    QObject::connect(&model, &ProjectModel::refreshFinished,
                     [&e2Settled](bool, const QString &) { e2Settled = true; });
    model.open(e2Base);
    model.importFilelists({e2Filelist});
    e2Settled = false;
    model.setVariables({QStringLiteral("VEDIT_E2_PROJECTED=.")});
    const bool e2Expanded = [&]() {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000) {
            if (e2Settled && !model.isPending()
                && model.referencedVariables().contains(QStringLiteral("VEDIT_E2_PROJECTED"))) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(5);
        }
        return false;
    }();
    check("e2.expansion.finished", e2Expanded, model.lastError());
    check("e2.references.names",
          model.referencedVariables()
              == QStringList{QStringLiteral("VEDIT_E2_ENVIRONMENT"),
                             QStringLiteral("VEDIT_E2_MISSING"),
                             QStringLiteral("VEDIT_E2_PROJECTED"),
                             QStringLiteral("VEDIT_E2_REFERENCED")},
          model.referencedVariables().join(QStringLiteral(",")));
    check("e2.references.layers",
          model.referencedVariableLayers()
              == QStringList{QStringLiteral("environment"), QStringLiteral("missing"),
                             QStringLiteral("project"), QStringLiteral("missing")},
          model.referencedVariableLayers().join(QStringLiteral(",")));
    const QVariantMap e2Inputs = model.expansionInputs();
    check("e2.inputs.payload",
          e2Inputs.value(QStringLiteral("referencedVariables")).toStringList()
                  == model.referencedVariables()
              && e2Inputs.value(QStringLiteral("referencedVariableLayers")).toStringList()
                     == model.referencedVariableLayers(),
          QStringLiteral("keys=%1").arg(e2Inputs.keys().join(QStringLiteral(","))));

    // (1) + (2) + (4): the candidate list is the referenced set, with layers, and
    // the unreferenced shell variable never appears.
    {
        EnvImportPage page(settingsPath);
        page.flush();
        page.setProjectInputs(e2Inputs);
        check("e2.project-context", page.hasProjectContext());
        page.setProjectRoot(e2Base);
        page.setShellCode(0);
        page.setSourceFile(e2Rc);
        check("e2.capture.start", page.startCapture(), page.statusText());
        check("e2.capture.done", page.flush(60000), page.captureReport());
        QStringList listed;
        for (int row = 0; row < page.diffRowCount(); ++row) {
            listed.append(page.diffName(row));
        }
        const QString unrefLayerFacts =
            QStringLiteral("rows=%1 names=%2 hidden=%3")
                .arg(page.diffRowCount())
                .arg(listed.join(QStringLiteral(",")))
                .arg(page.hiddenDiffRowCount());
        check("e2.candidates-only-referenced", listed == model.referencedVariables(), unrefLayerFacts);
        check("e2.not-referenced.absent",
              !listed.contains(QStringLiteral("VEDIT_E2_UNREFERENCED"))
                  && page.hiddenDiffRowCount() >= 1,
              unrefLayerFacts);
        const auto layerOf = [&page](const QString &name) {
            const int row = environmentRowNamed(page, name);
            return row < 0 ? QStringLiteral("<no row>") : page.diffLayer(row);
        };
        check("e2.layer.environment", layerOf(QStringLiteral("VEDIT_E2_ENVIRONMENT"))
                                          == QStringLiteral("environment"),
              layerOf(QStringLiteral("VEDIT_E2_ENVIRONMENT")));
        check("e2.layer.project",
              layerOf(QStringLiteral("VEDIT_E2_PROJECTED")) == QStringLiteral("project"),
              layerOf(QStringLiteral("VEDIT_E2_PROJECTED")));
        check("e2.layer.missing",
              layerOf(QStringLiteral("VEDIT_E2_REFERENCED")) == QStringLiteral("missing")
                  && layerOf(QStringLiteral("VEDIT_E2_MISSING")) == QStringLiteral("missing"),
              layerOf(QStringLiteral("VEDIT_E2_REFERENCED")));
        // (2) explicit: the named CONTROL is off by default and reveals exactly
        // the rows the filter folded away.
        check("e2.show-all.default-off",
              page.showAllCandidatesCheck() != nullptr
                  && !page.showAllCandidatesCheck()->isChecked() && !page.showAllCandidates());
        page.setShowAllCandidates(true);
        check("e2.show-all.reveals",
              page.hiddenDiffRowCount() == 0
                  && environmentRowNamed(page, QStringLiteral("VEDIT_E2_UNREFERENCED")) >= 0,
              unrefLayerFacts);
        check("e2.show-all.unreferenced-has-no-layer",
              page.diffLayer(environmentRowNamed(page, QStringLiteral("VEDIT_E2_UNREFERENCED")))
                  .isEmpty());
        page.setShowAllCandidates(false);
        check("e2.show-all.hides-again",
              environmentRowNamed(page, QStringLiteral("VEDIT_E2_UNREFERENCED")) < 0);
    }

    // (3) a project whose filelist references no variable: the list is empty and
    // says why.
    {
        const QString e2Empty = base + QStringLiteral("/e2-empty");
        check("e2.empty.mkdir", QDir().mkpath(e2Empty));
        check("e2.empty.fixture",
              writeText(e2Empty + QStringLiteral("/plain.v"),
                        QByteArray("module plain;\nendmodule\n"))
                  && writeText(e2Empty + QStringLiteral("/plain.f"),
                               QByteArray("plain.v\n")));
        ProjectModel emptyModel(e2Empty + QStringLiteral("/state"));
        bool emptySettled = false;
        QObject::connect(&emptyModel, &ProjectModel::refreshFinished,
                         [&emptySettled](bool, const QString &) { emptySettled = true; });
        emptyModel.open(e2Empty);
        emptyModel.importFilelists({e2Empty + QStringLiteral("/plain.f")});
        const bool emptyExpanded = [&]() {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 20000) {
                if (emptySettled && !emptyModel.isPending()
                    && emptyModel.files().contains(e2Empty + QStringLiteral("/plain.v"))) {
                    return true;
                }
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
            return false;
        }();
        check("e2.empty.expansion", emptyExpanded, emptyModel.lastError());
        check("e2.empty.references", emptyModel.referencedVariables().isEmpty(),
              emptyModel.referencedVariables().join(QStringLiteral(",")));
        EnvImportPage emptyPage(settingsPath);
        emptyPage.flush();
        emptyPage.setProjectInputs(emptyModel.expansionInputs());
        emptyPage.setProjectRoot(e2Empty);
        emptyPage.setShellCode(0);
        emptyPage.setSourceFile(e2Rc);
        check("e2.empty.capture.start", emptyPage.startCapture(), emptyPage.statusText());
        check("e2.empty.capture.done", emptyPage.flush(60000), emptyPage.captureReport());
        check("e2.empty.list", emptyPage.diffRowCount() == 0,
              QStringLiteral("visible=%1 hidden=%2")
                  .arg(emptyPage.diffRowCount())
                  .arg(emptyPage.hiddenDiffRowCount()));
        check("e2.empty.explained",
              emptyPage.candidateHint().contains(QStringLiteral("references no variables")),
              emptyPage.candidateHint());
    }
    qunsetenv("VEDIT_E2_ENVIRONMENT");
#endif
}

// --- batch 20261110c: env masking + the four failure modes (AC-4.8/4.9) ----

// One executable script under the temporary directory (the lane's fake-format
// pattern: write, then grant owner-execute).
bool writeScript(const QString &path, const QByteArray &body)
{
    return writeText(path, QByteArray("#!/bin/sh\n") + body)
        && QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner);
}

// AC-4.8: a value must never enter this process's stdout. Every assertion about a
// secret value is therefore a BOOLEAN; this helper only exists so the negative
// case ("the value is nowhere in the text") reads as one expression.
QString redactionFacts(const QString &text, const QString &value)
{
    return QStringLiteral("bytes=%1 value_present=%2 mask_present=%3")
        .arg(text.size())
        .arg(!value.isEmpty() && text.contains(value) ? 1 : 0)
        .arg(text.contains(QStringLiteral("<masked>")) ? 1 : 0);
}

// The masking + failure-mode half of the Environment page (AC-4.8/4.9). It runs
// against its OWN directory and its own capture fixtures so the AC-4.3/4.4
// checks above keep their exact row counts.
void checkEnvironmentMaskAndFailures(const QString &directory)
{
    if (!SettingsClient::ffiAvailable()) {
        check("env-mask.ffi.available", false,
              "lane built without the vd-ffi environment ABI");
        return;
    }
    check("env-mask.ffi.available", true);
    const QString base = directory + QStringLiteral("/env-mask");
    check("env-mask.mkdir", QDir().mkpath(base));
    const QString settingsPath = base + QStringLiteral("/settings.toml");

    // The AC-4.8 shapes in one rc file: a secret-shaped name, a NON-secret
    // control, and a value that contains BOTH '=' and a real newline.
    const QString rcPath = base + QStringLiteral("/mask.rc");
    check("env-mask.fixture.rc",
          writeText(rcPath, QByteArray("export VEDIT_MASK_API_KEY='fixture-key-material-7c1d'\n"
                                      "export VEDIT_MASK_VISIBLE='visible-control-value'\n"
                                      "export VEDIT_MASK_MULTILINE='one=1\ntwo=2'\n")));
    const QString secretValue = QStringLiteral("fixture-key-material-7c1d");
    const QString visibleValue = QStringLiteral("visible-control-value");
    const QString multilineValue = QStringLiteral("one=1\ntwo=2");
    qunsetenv("VEDIT_MASK_API_KEY");
    qunsetenv("VEDIT_MASK_VISIBLE");
    qunsetenv("VEDIT_MASK_MULTILINE");

    // AC-4.8: the name rule is exactly the Rust one (substring, case-insensitive,
    // deliberately over-inclusive) — asserted here so a UI-only rule cannot drift.
    check("env-mask.matcher.names",
          EnvImportPage::isSecretShapedName(QStringLiteral("GITHUB_TOKEN"))
              && EnvImportPage::isSecretShapedName(QStringLiteral("my_secret"))
              && EnvImportPage::isSecretShapedName(QStringLiteral("DB_PASSWORD"))
              && EnvImportPage::isSecretShapedName(QStringLiteral("API_KEY"))
              && EnvImportPage::isSecretShapedName(QStringLiteral("hotkey_path"))
              && !EnvImportPage::isSecretShapedName(QStringLiteral("PATH"))
              && !EnvImportPage::isSecretShapedName(QStringLiteral("MONTH")));

    EnvImportPage page(settingsPath);
    const bool maskLoaded = page.flush();
    check("env-mask.load", maskLoaded, page.statusText());
    page.setProjectRoot(base);
    page.setProjectScope(false);
    page.setShellCode(0);
    page.setSourceFile(rcPath);
    check("env-mask.reveal.default-off", !page.secretsRevealed());
    check("env-mask.reveal.box-default",
          page.revealSecretsCheck() != nullptr && !page.revealSecretsCheck()->isChecked());
    const bool maskCaptureStarted = page.startCapture();
    check("env-mask.capture.start", maskCaptureStarted, page.statusText());
    const bool maskCaptured = page.flush(60000);
    check("env-mask.capture.done", maskCaptured, page.captureReport());

    const int secretRow = environmentRowNamed(page, QStringLiteral("VEDIT_MASK_API_KEY"));
    const int visibleRow = environmentRowNamed(page, QStringLiteral("VEDIT_MASK_VISIBLE"));
    const int multilineRow = environmentRowNamed(page, QStringLiteral("VEDIT_MASK_MULTILINE"));
    check("env-mask.diff.rows", secretRow >= 0 && visibleRow >= 0 && multilineRow >= 0,
          QStringLiteral("rows=%1").arg(page.diffRowCount()));

    // THE mask criterion, and the reason "no output" would not pass it: the cell
    // the table PAINTS is `<masked>` while the value underneath still EXISTS.
    // Deleting the field would satisfy a bare "value absent" test; it fails here.
    const bool secretCellMasked =
        secretRow >= 0 && page.diffTable() != nullptr
        && page.diffTable()->item(secretRow, 4) != nullptr
        && page.diffTable()->item(secretRow, 4)->text() == QStringLiteral("<masked>");
    check("env-mask.diff.painted-mask", secretCellMasked,
          QStringLiteral("row=%1 cell_is_mask=%2").arg(secretRow).arg(secretCellMasked ? 1 : 0));
    check("env-mask.diff.underlying-value",
          secretRow >= 0 && page.diffCapturedValue(secretRow) == secretValue,
          QStringLiteral("row=%1 raw_present=%2")
              .arg(secretRow)
              .arg(secretRow >= 0 && page.diffCapturedValue(secretRow) == secretValue ? 1 : 0));
    check("env-mask.diff.control-visible",
          visibleRow >= 0 && page.diffDisplayValue(visibleRow) == visibleValue
              && page.diffTable()->item(visibleRow, 4) != nullptr
              && page.diffTable()->item(visibleRow, 4)->text() == visibleValue);
    check("env-mask.diff.equals-newline",
          multilineRow >= 0 && page.diffCapturedValue(multilineRow) == multilineValue
              && page.diffDisplayValue(multilineRow) == QStringLiteral("one=1\\ntwo=2"),
          QStringLiteral("row=%1 raw_len=%2 display_len=%3")
              .arg(multilineRow)
              .arg(multilineRow >= 0 ? page.diffCapturedValue(multilineRow).size() : -1)
              .arg(multilineRow >= 0 ? page.diffDisplayValue(multilineRow).size() : -1));
    check("env-mask.status.no-secret",
          !(page.statusText() + page.captureReport()).contains(secretValue),
          redactionFacts(page.statusText() + page.captureReport(), secretValue));

    // Unrevealed: checking a secret-shaped row stages NOTHING and says why.
    page.setDiffChecked(secretRow, true);
    page.setDiffChecked(visibleRow, true);
    const bool unrevealedStaged = page.stageImport();
    check("env-mask.stage.unrevealed", unrevealedStaged, page.statusText());
    check("env-mask.stage.unrevealed-skipped",
          storedRowNamed(page, QStringLiteral("VEDIT_MASK_API_KEY"), base, nullptr) < 0
              && storedRowNamed(page, QStringLiteral("VEDIT_MASK_VISIBLE"), base, nullptr) >= 0);
    check("env-mask.stage.unrevealed-explained",
          page.statusText().contains(QStringLiteral("secret-shaped")), page.statusText());
    page.discardPending();

    // The EXPLICIT expansion: the painted cell now carries the real value, and the
    // reveal (not the value) is what the user just approved.
    page.setSecretsRevealed(true);
    const bool revealedCellRaw =
        secretRow >= 0 && page.diffTable()->item(secretRow, 4) != nullptr
        && page.diffTable()->item(secretRow, 4)->text() == secretValue;
    check("env-mask.reveal.api", page.secretsRevealed());
    check("env-mask.reveal.box-checked",
          page.revealSecretsCheck() != nullptr && page.revealSecretsCheck()->isChecked());
    check("env-mask.reveal.painted-raw", revealedCellRaw,
          QStringLiteral("row=%1 cell_is_raw=%2").arg(secretRow).arg(revealedCellRaw ? 1 : 0));
    check("env-mask.reveal.display-raw", page.diffDisplayValue(secretRow) == secretValue);

    page.setDiffChecked(secretRow, true);
    const bool revealedStaged = page.stageImport();
    check("env-mask.stage.revealed", revealedStaged, page.statusText());
    QString stagedValue;
    check("env-mask.stage.revealed-row",
          storedRowNamed(page, QStringLiteral("VEDIT_MASK_API_KEY"), base, &stagedValue) >= 0
              && stagedValue == secretValue,
          QStringLiteral("staged=%1")
              .arg(storedRowNamed(page, QStringLiteral("VEDIT_MASK_API_KEY"), base, nullptr) >= 0
                       ? 1
                       : 0));
    const bool commitStarted = page.commitPending();
    check("env-mask.commit.started", commitStarted, page.statusText());
    const bool commitFinished = page.flush();
    check("env-mask.commit.done", commitFinished, page.statusText());
    const QString stored = readText(page.selectionPath());
    check("env-mask.file.value-stored", stored.contains(secretValue),
          redactionFacts(stored, secretValue));
    check("env-mask.file.no-mask-marker",
          !stored.contains(QStringLiteral("<masked>"))
              && !stored.contains(QStringLiteral("reveal")));

    // "Not persisted" = the REVEAL is not persisted: a new page starts masked,
    // while the value it masks is still there underneath.
    {
        EnvImportPage restarted(settingsPath);
        const bool restartedLoaded = restarted.flush();
        check("env-mask.restart.load", restartedLoaded, restarted.statusText());
        QString roundTripped;
        const int row = storedRowNamed(restarted, QStringLiteral("VEDIT_MASK_API_KEY"), base,
                                       &roundTripped);
        const bool restartedCellMasked =
            row >= 0 && restarted.selectionTable() != nullptr
            && restarted.selectionTable()->item(row, 3) != nullptr
            && restarted.selectionTable()->item(row, 3)->text() == QStringLiteral("<masked>");
        check("env-mask.restart.default-masked", !restarted.secretsRevealed());
        check("env-mask.restart.box-unchecked",
              restarted.revealSecretsCheck() != nullptr
                  && !restarted.revealSecretsCheck()->isChecked());
        check("env-mask.restart.painted-mask", restartedCellMasked,
              QStringLiteral("row=%1 cell_is_mask=%2").arg(row).arg(restartedCellMasked ? 1 : 0));
        check("env-mask.restart.value-underneath", row >= 0 && roundTripped == secretValue,
              QStringLiteral("row=%1 raw_present=%2")
                  .arg(row)
                  .arg(row >= 0 && roundTripped == secretValue ? 1 : 0));
    }

    // The manual NAME=VALUE path is a second write path into the same store and
    // obeys the same rule: no secret-shaped value while it is masked.
    page.setSecretsRevealed(false);
    const int rowsBeforeAdd = page.selectionRowCount();
    const bool unrevealedAddRefused =
        !page.addVariable(QStringLiteral("VEDIT_MASK_MANUAL_TOKEN"),
                          QStringLiteral("manual-secret-value"));
    check("env-mask.add.unrevealed-refused", unrevealedAddRefused, page.statusText());
    check("env-mask.add.unrevealed-no-row", page.selectionRowCount() == rowsBeforeAdd);
    page.setSecretsRevealed(true);
    const bool revealedAddAllowed =
        page.addVariable(QStringLiteral("VEDIT_MASK_MANUAL_TOKEN"),
                         QStringLiteral("manual-secret-value"));
    check("env-mask.add.revealed-allowed", revealedAddAllowed, page.statusText());
    page.discardPending(); // the manual row is only needed to prove the gate

    // A shell can echo a secret to stderr; the tail is REDACTED against the
    // capture's own secret-shaped values before it is painted or reported.
    const QString echoRc = base + QStringLiteral("/echo.rc");
    check("env-mask.fixture.echo-rc",
          writeText(echoRc, QByteArray("export VEDIT_MASK_ECHO_TOKEN='fixture-echo-canary-4b8e'\n"
                                      "echo \"token=$VEDIT_MASK_ECHO_TOKEN\" >&2\n")));
    const QString echoCanary = QStringLiteral("fixture-echo-canary-4b8e");
    const QString echoRowName = QStringLiteral("VEDIT_MASK_ECHO_TOKEN");
    page.setShellCode(0);
    page.setShellPath(QString());
    page.setSourceFile(echoRc);
    // A new capture is new data: the explicit reveal must be asked for AGAIN.
    page.setSecretsRevealed(true);
    const bool echoCaptureStarted = page.startCapture();
    check("env-mask.reveal.reset-on-capture",
          echoCaptureStarted && !page.secretsRevealed(),
          QStringLiteral("capture=%1 revealed=%2")
              .arg(echoCaptureStarted ? 1 : 0)
              .arg(page.secretsRevealed() ? 1 : 0));
    check("env-mask.stderr.capture-start", echoCaptureStarted, page.statusText());
    const bool echoCaptured = page.flush(60000);
    check("env-mask.stderr.capture-done", echoCaptured, page.captureReport());
    check("env-mask.stderr.tail-redacted",
          page.captureStderrTail().contains(QStringLiteral("token=<masked>")),
          redactionFacts(page.captureStderrTail(), echoCanary));
    check("env-mask.stderr.value-absent", !page.captureStderrTail().contains(echoCanary),
          redactionFacts(page.captureStderrTail(), echoCanary));
    check("env-mask.stderr.report-absent", !page.captureReport().contains(echoCanary),
          redactionFacts(page.captureReport(), echoCanary));
    const int echoRow = environmentRowNamed(page, echoRowName);
    check("env-mask.stderr.row-masked",
          echoRow >= 0 && page.diffDisplayValue(echoRow) == QStringLiteral("<masked>")
              && page.diffCapturedValue(echoRow) == echoCanary,
          QStringLiteral("row=%1").arg(echoRow));

    // An AMBIENT secret (unchanged in this process, therefore NOT in the diff at
    // all) echoed by the child must be redacted too: a diff-only rule would leak
    // exactly the credentials the user already has in their environment.
    const QString ambientProgram = base + QStringLiteral("/ambient-env.sh");
    check("env-mask.ambient.fixture",
          writeScript(ambientProgram, QByteArray("printf 'VEDIT_MASK_AMBIENT_MARKER=ok\\n'\n"
                                                "echo \"ambient=$VEDIT_MASK_AMBIENT_TOKEN\" >&2\n")));
    const QString ambientCanary = QStringLiteral("ambient-canary-5e2a");
    qputenv("VEDIT_MASK_AMBIENT_TOKEN", ambientCanary.toUtf8());
    page.setShellCode(7); // custom program
    page.setShellPath(ambientProgram);
    page.setSourceFile(QString());
    const bool ambientStarted = page.startCapture();
    check("env-mask.ambient.capture-start", ambientStarted, page.statusText());
    const bool ambientDone = page.flush(60000);
    check("env-mask.ambient.capture-done", ambientDone, page.captureReport());
    check("env-mask.ambient.tail-redacted",
          page.captureStderrTail().contains(QStringLiteral("ambient=<masked>")),
          redactionFacts(page.captureStderrTail(), ambientCanary));
    check("env-mask.ambient.value-absent",
          !page.captureStderrTail().contains(ambientCanary),
          redactionFacts(page.captureStderrTail(), ambientCanary));
    check("env-mask.ambient.row-absent",
          environmentRowNamed(page, QStringLiteral("VEDIT_MASK_AMBIENT_TOKEN")) < 0);
    qunsetenv("VEDIT_MASK_AMBIENT_TOKEN");

    // --- AC-4.9: the four failure classes --------------------------------
    // Baseline for the "no half-apply" claims: the selection file written above.
    const QString fileBefore = readText(page.selectionPath());
    // A staged but uncommitted draft: a failed capture must leave it alone.
    check("env-fail.draft.before",
          page.addVariable(QStringLiteral("VEDIT_MASK_KEEP"), QStringLiteral("keep-value")));
    check("env-fail.draft.pending", page.hasPendingChanges());
    const int draftRows = page.selectionRowCount();

    // (1) rc file MISSING: the shell exits non-zero, the environment is still
    // read, and the exit status + stderr tail stay visible.
    page.setShellCode(0);
    page.setShellPath(QString());
    page.setSourceFile(base + QStringLiteral("/no-such-rc-file"));
    const bool rcMissingStarted = page.startCapture();
    check("env-fail.rc-missing.start", rcMissingStarted, page.statusText());
    const bool rcMissingDone = page.flush(60000);
    check("env-fail.rc-missing.done", rcMissingDone, page.captureReport());
    check("env-fail.rc-missing.exit-status", page.captureExitCode() != 0,
          QStringLiteral("exit=%1").arg(page.captureExitCode()));
    check("env-fail.rc-missing.stderr-tail",
          page.captureStderrTail().contains(QStringLiteral("no-such-rc-file")));
    check("env-fail.rc-missing.user-visible",
          page.statusText().contains(QStringLiteral("exit status")), page.statusText());

    // (2) rc file UNREADABLE: a directory cannot be sourced; same reporting.
    const QString rcDirectory = base + QStringLiteral("/rc-as-directory");
    check("env-fail.rc-unreadable.fixture", QDir().mkpath(rcDirectory));
    page.setSourceFile(rcDirectory);
    const bool rcUnreadableStarted = page.startCapture();
    check("env-fail.rc-unreadable.start", rcUnreadableStarted, page.statusText());
    const bool rcUnreadableDone = page.flush(60000);
    check("env-fail.rc-unreadable.done", rcUnreadableDone, page.captureReport());
    check("env-fail.rc-unreadable.exit-status", page.captureExitCode() != 0,
          QStringLiteral("exit=%1").arg(page.captureExitCode()));
    check("env-fail.rc-unreadable.stderr-tail", !page.captureStderrTail().trimmed().isEmpty(),
          redactionFacts(page.captureStderrTail(), secretValue));
    check("env-fail.rc-unreadable.user-visible",
          page.statusText().contains(QStringLiteral("exit status")), page.statusText());

    // (3) shell MISSING: the job fails with VD_ERR_NOT_FOUND and the sentence
    // names the program that could not start.
    page.setShellPath(QStringLiteral("/nonexistent/vedit-no-such-shell"));
    page.setSourceFile(QString());
    const bool shellMissingStarted = page.startCapture();
    check("env-fail.shell-missing.start", shellMissingStarted, page.statusText());
    const bool shellMissingDone = page.flush(60000);
    check("env-fail.shell-missing.done", shellMissingDone, page.statusText());
    check("env-fail.shell-missing.explained",
          page.statusText().contains(QStringLiteral("could not be started"))
              && page.statusText().contains(QStringLiteral("vedit-no-such-shell")),
          page.statusText());
    check("env-fail.shell-missing.no-rows", page.diffRowCount() == 0,
          QStringLiteral("rows=%1").arg(page.diffRowCount()));
    check("env-fail.shell-missing.no-exit-status", page.captureExitCode() == -1,
          QStringLiteral("exit=%1").arg(page.captureExitCode()));

    // (4) NON-NUL output: a custom program that prints newline-separated
    // NAME=VALUE. The engine accepts it as a fallback WITH a warning, and a value
    // containing '=' survives because the split is at the FIRST '='.
    const QString newlineProgram = base + QStringLiteral("/newline-env.sh");
    check("env-fail.non-nul.fixture",
          writeScript(newlineProgram, QByteArray("printf 'VEDIT_FAIL_NONNUL=value-with=equals\\n'\n"
                                                "printf 'VEDIT_FAIL_SECOND=two\\n'\n")));
    page.setShellCode(7); // custom program
    page.setShellPath(newlineProgram);
    const bool nonNulStarted = page.startCapture();
    check("env-fail.non-nul.start", nonNulStarted, page.statusText());
    const bool nonNulDone = page.flush(60000);
    check("env-fail.non-nul.done", nonNulDone, page.captureReport());
    check("env-fail.non-nul.warning",
          page.warnings().join(QStringLiteral("; ")).contains(QStringLiteral("newline-separated")),
          page.warnings().join(QStringLiteral("; ")));
    check("env-fail.non-nul.equals-value",
          environmentRowNamed(page, QStringLiteral("VEDIT_FAIL_NONNUL")) >= 0
              && page.diffCapturedValue(environmentRowNamed(page,
                                                            QStringLiteral("VEDIT_FAIL_NONNUL")))
                     == QStringLiteral("value-with=equals"));
    check("env-fail.non-nul.exit-status", page.captureExitCode() == 0,
          QStringLiteral("exit=%1").arg(page.captureExitCode()));

    // (5) TIMEOUT: the budget is lowered so the check is fast; the sentence says
    // it timed out and nothing is applied.
    const QString slowProgram = base + QStringLiteral("/slow-env.sh");
    check("env-fail.timeout.fixture",
          writeScript(slowProgram, QByteArray("sleep 20\n"
                                             "printf 'VEDIT_FAIL_SLOW=late\\n'\n")));
    page.setShellCode(7);
    page.setShellPath(slowProgram);
    page.setCaptureTimeoutMs(400);
    const bool timeoutStarted = page.startCapture();
    check("env-fail.timeout.start", timeoutStarted, page.statusText());
    const bool timeoutDone = page.flush(60000);
    check("env-fail.timeout.done", timeoutDone, page.statusText());
    check("env-fail.timeout.explained", page.statusText().contains(QStringLiteral("timed out")),
          page.statusText());
    check("env-fail.timeout.no-rows", page.diffRowCount() == 0,
          QStringLiteral("rows=%1").arg(page.diffRowCount()));

    // (6) CANCEL: the user stops the capture that is in flight.
    page.setCaptureTimeoutMs(15000);
    page.setShellPath(slowProgram);
    const bool cancelStarted = page.startCapture();
    check("env-fail.cancel.start", cancelStarted, page.statusText());
    const bool cancelEnabled =
        page.cancelButton() != nullptr && page.cancelButton()->isEnabled();
    check("env-fail.cancel.button-enabled", cancelEnabled,
          QStringLiteral("enabled=%1").arg(cancelEnabled ? 1 : 0));
    const bool cancellationAsked = page.cancelCapture();
    check("env-fail.cancel.while-running", cancellationAsked, page.statusText());
    const bool cancelDone = page.flush(60000);
    check("env-fail.cancel.done", cancelDone, page.statusText());
    const bool cancelDisabled =
        page.cancelButton() != nullptr && !page.cancelButton()->isEnabled();
    check("env-fail.cancel.button-disabled", cancelDisabled,
          QStringLiteral("enabled=%1").arg(cancelDisabled ? 0 : 1));
    check("env-fail.cancel.explained", page.statusText().contains(QStringLiteral("cancelled")),
          page.statusText());
    check("env-fail.cancel.no-rows", page.diffRowCount() == 0,
          QStringLiteral("rows=%1").arg(page.diffRowCount()));

    // "No half-apply" for every failure above: the store is byte-identical and
    // the staged draft survived.
    check("env-fail.no-half-apply.file", readText(page.selectionPath()) == fileBefore);
    check("env-fail.no-half-apply.draft",
          page.hasPendingChanges() && page.selectionRowCount() == draftRows);
    page.discardPending();

    // (7) csh/tcsh/fish: every family is captured through `env -0`, so the NUL
    // block is authoritative for all three and no newline-fallback warning may
    // appear. The family only exists if the target machine has the shell; missing
    // is reported as an explicit SKIP WITH ITS REASON, never as a silent pass
    // (the newline fallback itself is covered by the custom program in (4)).
    struct FamilyProbe {
        const char *name;
        int code;
        bool fish_syntax;
    };
    const FamilyProbe families[] = {
        {"csh", 2, false}, {"tcsh", 3, false}, {"fish", 4, true}};
    for (const FamilyProbe &family : families) {
        const QString name = QString::fromLatin1(family.name);
        const QString familyProgram = QStandardPaths::findExecutable(name);
        if (familyProgram.isEmpty()) {
            std::printf("SKIP env-fail.%s.capture reason=%s-not-on-PATH "
                        "(the verification image must provide it; see "
                        "evidence/batch-20261114/a50)\n",
                        family.name, family.name);
            std::fflush(stdout);
            continue;
        }
        const QString familyRc = base + QStringLiteral("/family-") + name + QStringLiteral(".rc");
        check(qPrintable(QStringLiteral("env-fail.%1.fixture").arg(name)),
              writeText(familyRc,
                        QByteArray(family.fish_syntax
                                       ? "set -gx VEDIT_FAMILY_MARKER 'family-value=with=equals'\n"
                                       : "setenv VEDIT_FAMILY_MARKER 'family-value=with=equals'\n")));
        page.setShellCode(family.code);
        page.setShellPath(QString());
        page.setSourceFile(familyRc);
        page.setCaptureTimeoutMs(15000);
        const QByteArray startLabel =
            QStringLiteral("env-fail.%1.capture-start").arg(name).toUtf8();
        if (!page.startCapture()) {
            check(startLabel.constData(), false, page.statusText());
            continue;
        }
        check(startLabel.constData(), true);
        const bool familyCaptured = page.flush(60000);
        check(qPrintable(QStringLiteral("env-fail.%1.capture-done").arg(name)),
              familyCaptured, page.captureReport());
        check(qPrintable(QStringLiteral("env-fail.%1.equals-value").arg(name)),
              environmentRowNamed(page, QStringLiteral("VEDIT_FAMILY_MARKER")) >= 0
                  && page.diffCapturedValue(environmentRowNamed(
                         page, QStringLiteral("VEDIT_FAMILY_MARKER")))
                         == QStringLiteral("family-value=with=equals"));
        // A51: csh/tcsh are captured through `env -0` exactly like POSIX and
        // fish, so the NUL block is authoritative for every family and the
        // newline-fallback warning must be ABSENT. (The fallback still exists for
        // a custom program; (4) exercises that path with its warning.)
        check(qPrintable(QStringLiteral("env-fail.%1.nul-path").arg(name)),
              !page.warnings().join(QStringLiteral("; "))
                   .contains(QStringLiteral("newline-separated")),
              page.warnings().join(QStringLiteral("; ")));
    }
}

// The Xvfb + xcb probe: every step is a REAL hit test on a real window (see
// `clickUnderPoint`), not a direct slot call. Without an xcb display the probe
// prints its reason and exits 77 (the ctest SKIP_RETURN_CODE).
int runEnvironmentProbe(const QString &directory)
{
    if (!SettingsClient::ffiAvailable()) {
        std::printf("ENV-UI SKIP reason=no-vd-ffi-environment-abi\n");
        return 77;
    }
    if (QApplication::platformName() != QLatin1String("xcb")) {
        std::printf("ENV-UI SKIP platform=%s (needs xcb + Xvfb)\n",
                    qPrintable(QApplication::platformName()));
        return 77;
    }
    // A rerun must start from an EMPTY selection: a persisted row from the
    // previous probe would make "staged" a no-op (import replaces, it does not
    // duplicate) and hide the transition under test.
    QDir(directory).removeRecursively();
    if (!QDir().mkpath(directory)) {
        std::printf("ENV-UI PROBE ok=0 reason=no-directory\n");
        return 1;
    }
    const QString rcPath = directory + QStringLiteral("/probe.rc");
    check("env-ui.probe.fixture", writeEnvironmentFixture(rcPath));
    prepareEnvironmentFixtureProcess();
    const QString settingsPath = directory + QStringLiteral("/settings.toml");

    Harness harness;
    check("env-ui.probe.harness",
          buildHarness(&harness, settingsPath, directory + QStringLiteral("/shortcuts.json")));
    SettingsDialog dialog(&harness.themes, &harness.commands, settingsPath);
    dialog.show();
    dialog.raise();
    dialog.activateWindow();
    EnvImportPage *page = dialog.environmentPage();
    check("env-ui.probe.page", page != nullptr);
    if (page == nullptr) {
        return 1;
    }
    if (QTabWidget *tabs = dialog.panel() == nullptr ? nullptr : dialog.panel()->tabs();
        tabs != nullptr) {
        tabs->setCurrentWidget(page);
    }
    pumpGuiEvents(40);
    {
        QTabWidget *tabs = dialog.panel() == nullptr ? nullptr : dialog.panel()->tabs();
        std::printf("ENV-UI probe geometry dialog.visible=%d dialog.size=%dx%d tabs=%d current=%d "
                    "pageIndex=%d page.visible=%d page.size=%dx%d button.visible=%d\n",
                    dialog.isVisible() ? 1 : 0, dialog.width(), dialog.height(),
                    tabs == nullptr ? -1 : tabs->count(),
                    tabs == nullptr ? -1 : tabs->currentIndex(),
                    tabs == nullptr ? -2 : tabs->indexOf(page), page->isVisible() ? 1 : 0,
                    page->width(), page->height(),
                    page->captureButton() != nullptr && page->captureButton()->isVisible() ? 1 : 0);
    }
    check("env-ui.probe.page-visible", page->isVisible());
    page->setProjectRoot(directory);
    page->setProjectScope(false);
    page->setSourceFile(rcPath);
    page->setShellCode(0);
    const bool probeLoaded = page->flush();
    check("env-ui.probe.load", probeLoaded, page->statusText());

    check("env-ui.probe.capture.hit", clickCenter(page->captureButton(), "capture"));
    const bool probeCaptured = page->flush(60000);
    check("env-ui.probe.capture.done", probeCaptured, page->captureReport());
    const int addedRow = environmentRowNamed(*page, QStringLiteral("VEDIT_ENV_UI_ADDED"));
    check("env-ui.probe.capture.rows",
          page->diffRowCount() > 0 && addedRow >= 0,
          QStringLiteral("rows=%1").arg(page->diffRowCount()));
    // AC-4.8 through the REAL window: the painted cell is `<masked>` while the
    // value exists underneath, the reveal checkbox (hit-test click) shows it, and
    // a second click masks it again (the reveal is not sticky).
    const int probeSecretRow =
        environmentRowNamed(*page, QStringLiteral("VEDIT_ENV_UI_SECRET_TOKEN"));
    check("env-ui.probe.secret.row", probeSecretRow >= 0);
    if (probeSecretRow >= 0 && page->diffTable() != nullptr) {
        QTableWidget *table = page->diffTable();
        const bool maskedCell = table->item(probeSecretRow, 4) != nullptr
                                && table->item(probeSecretRow, 4)->text()
                                       == QStringLiteral("<masked>");
        check("env-ui.probe.secret.painted-mask", maskedCell,
              QStringLiteral("cell_is_mask=%1").arg(maskedCell ? 1 : 0));
        const bool rawUnderneath =
            page->diffCapturedValue(probeSecretRow) == QStringLiteral("fixture-secret-value");
        check("env-ui.probe.secret.underlying-value", rawUnderneath,
              QStringLiteral("raw_present=%1").arg(rawUnderneath ? 1 : 0));
        check("env-ui.probe.secret.reveal-hit",
              clickCenter(page->revealSecretsCheck(), "reveal-secrets"));
        const bool revealedCell = table->item(probeSecretRow, 4) != nullptr
                                  && table->item(probeSecretRow, 4)->text()
                                         == QStringLiteral("fixture-secret-value");
        check("env-ui.probe.secret.revealed", page->secretsRevealed() && revealedCell,
              QStringLiteral("revealed=%1 cell_is_raw=%2")
                  .arg(page->secretsRevealed() ? 1 : 0)
                  .arg(revealedCell ? 1 : 0));
        check("env-ui.probe.secret.hide-hit",
              clickCenter(page->revealSecretsCheck(), "hide-secrets"));
        const bool remasked = table->item(probeSecretRow, 4) != nullptr
                              && table->item(probeSecretRow, 4)->text()
                                     == QStringLiteral("<masked>");
        check("env-ui.probe.secret.remasked", !page->secretsRevealed() && remasked,
              QStringLiteral("revealed=%1 cell_is_mask=%2")
                  .arg(page->secretsRevealed() ? 1 : 0)
                  .arg(remasked ? 1 : 0));
    }
    if (addedRow >= 0 && page->diffTable() != nullptr) {
        QTableWidget *table = page->diffTable();
        QTableWidgetItem *checkItem = table->item(addedRow, 0);
        table->scrollToItem(checkItem);
        pumpGuiEvents();
        // The table lives inside the scrolling page too: bring its viewport into
        // the dialog's viewport before the indicator point is computed.
        scrollTargetIntoView(table->viewport());
        pumpGuiEvents();
        const QRect cellRect = table->visualItemRect(checkItem);
        QStyleOptionViewItem option;
        option.initFrom(table->viewport());
        option.rect = cellRect;
        option.features = QStyleOptionViewItem::HasCheckIndicator;
        const QRect indicator = table->style()->subElementRect(
            QStyle::SE_ItemViewItemCheckIndicator, &option, table);
        check("env-ui.probe.checkbox.starts-unchecked", !page->diffChecked(addedRow));
        check("env-ui.probe.checkbox.hit",
              clickUnderPoint(table->viewport(),
                              table->viewport()->mapToGlobal(indicator.center()), "diff-check"));
        check("env-ui.probe.checkbox.checked", page->diffChecked(addedRow));
    }
    check("env-ui.probe.import.hit", clickCenter(page->importButton(), "import"));
    const bool probeStaged = page->hasPendingChanges();
    check("env-ui.probe.import.staged", probeStaged, page->statusText());
    check("env-ui.probe.import.no-write", !QFileInfo::exists(page->selectionPath()),
          page->selectionPath());
    QPushButton *apply = dialog.buttons() == nullptr
                             ? nullptr
                             : dialog.buttons()->button(QDialogButtonBox::Apply);
    check("env-ui.probe.apply.hit", clickCenter(apply, "apply"));
    const bool probeStored = page->flush() && !page->hasPendingChanges();
    check("env-ui.probe.apply.stored", probeStored, page->statusText());
    const QString stored = readText(page->selectionPath());
    check("env-ui.probe.file",
          stored.contains(QStringLiteral("\"name\":\"VEDIT_ENV_UI_ADDED\""))
              && stored.contains(QStringLiteral("from-shell")),
          page->selectionPath());
    check("env-ui.probe.no-wholesale",
          !stored.contains(QStringLiteral("\"name\":\"VEDIT_ENV_UI_UNCHANGED\""))
              && !stored.contains(QStringLiteral("\"name\":\"PATH\"")));

    // Restart round-trip through a second dialog on the same file.
    {
        SettingsDialog restarted(&harness.themes, &harness.commands, settingsPath);
        restarted.show();
        pumpGuiEvents(40);
        EnvImportPage *again = restarted.environmentPage();
        check("env-ui.probe.restart.load", again != nullptr && again->flush());
        QString value;
        check("env-ui.probe.restart.row",
              again != nullptr
                  && storedRowNamed(*again, QStringLiteral("VEDIT_ENV_UI_ADDED"), directory, &value)
                         >= 0
                  && value == QStringLiteral("from-shell"),
              value);
        restarted.reject();
        pumpGuiEvents();
    }
    dialog.reject();
    std::printf("ENV-UI PROBE ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0, g_checks,
                g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
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


// Batch 20261110d: the Preferences dialog's SIZE DISCIPLINE. The real-machine
// window measured 1510×1741 — taller than the screen — so the OK/Apply/Cancel
// row sat below the bottom edge and was unreachable. Two invariants:
//   * the dialog is CAPPED to 90% of the screen's available height (never above
//     the preferred 760), and
//   * the CONTENT scrolls while the QDialogButtonBox stays OUTSIDE the scroll
//     area, pinned at the bottom.
// Both reference viewports (1366×768 and 1920×1080) are checked by building the
// dialog at exactly the height the constructor would choose on that screen.
void checkDialogSizeDiscipline(const QString &directory)
{
    const QSize laptop(1366, 768);
    const QSize desktop(1920, 1080);
    check("settings.discipline.cap-1366x768",
          SettingsDialog::cappedHeightFor(laptop) == 691,
          QString::number(SettingsDialog::cappedHeightFor(laptop)));
    check("settings.discipline.cap-1920x1080",
          SettingsDialog::cappedHeightFor(desktop) == SettingsDialog::kPreferredHeight,
          QString::number(SettingsDialog::cappedHeightFor(desktop)));
    check("settings.discipline.cap-invalid",
          SettingsDialog::cappedHeightFor(QSize()) == SettingsDialog::kPreferredHeight);

    const QString settingsPath = directory + QStringLiteral("/discipline-settings.toml");
    Harness harness;
    check("settings.discipline.harness",
          buildHarness(&harness, settingsPath,
                       directory + QStringLiteral("/discipline-shortcuts.json")));

    for (const QSize &available : {laptop, desktop}) {
        const QString tag =
            QStringLiteral("%1x%2").arg(available.width()).arg(available.height());
        const int capped = SettingsDialog::cappedHeightFor(available);
        SettingsDialog dialog(&harness.themes, &harness.commands, settingsPath, nullptr);
        // The offscreen test screen is smaller than either reference viewport:
        // apply the cap the constructor would apply on THAT screen, then size
        // the dialog exactly as it would appear there.
        dialog.setMaximumHeight(std::max(SettingsDialog::kMinimumHeight,
                                         available.height() * 9 / 10));
        dialog.resize(SettingsDialog::kPreferredWidth, capped);
        dialog.show();
        QApplication::processEvents();

        QDialogButtonBox *buttons = dialog.buttons();
        QScrollArea *scroll = dialog.contentScroll();
        check(qPrintable(QStringLiteral("settings.discipline.scroll-") + tag),
              scroll != nullptr && scroll->widget() == dialog.panel());
        check(qPrintable(QStringLiteral("settings.discipline.capped-") + tag), dialog.height() <= capped,
              QStringLiteral("height=%1 cap=%2").arg(dialog.height()).arg(capped));
        check(qPrintable(QStringLiteral("settings.discipline.buttons-visible-") + tag),
              buttons != nullptr && buttons->isVisible());
        // The action row is NOT inside the scrolling content.
        check(qPrintable(QStringLiteral("settings.discipline.buttons-not-in-scroll-") + tag),
              buttons != nullptr && buttons->parentWidget() == &dialog);
        const QRect buttonsRect =
            buttons != nullptr
                ? QRect(buttons->mapTo(&dialog, QPoint(0, 0)), buttons->size())
                : QRect();
        const QRect scrollRect = scroll != nullptr
                                     ? QRect(scroll->mapTo(&dialog, QPoint(0, 0)), scroll->size())
                                     : QRect();
        check(qPrintable(QStringLiteral("settings.discipline.buttons-inside-dialog-") + tag),
              buttons != nullptr && dialog.rect().contains(buttonsRect),
              QStringLiteral("buttons=%1,%2 %3x%4 dialog=%5x%6")
                  .arg(buttonsRect.x())
                  .arg(buttonsRect.y())
                  .arg(buttonsRect.width())
                  .arg(buttonsRect.height())
                  .arg(dialog.width())
                  .arg(dialog.height()));
        check(qPrintable(QStringLiteral("settings.discipline.buttons-below-content-") + tag),
              scroll != nullptr && scrollRect.bottom() < buttonsRect.top(),
              QStringLiteral("scroll-bottom=%1 buttons-top=%2")
                  .arg(scrollRect.bottom())
                  .arg(buttonsRect.top()));
        std::printf("DIALOG-GEOMETRY %s dialog=%dx%d scroll=%d,%d %dx%d buttons=%d,%d %dx%d\n",
                    qPrintable(tag), dialog.width(), dialog.height(), scrollRect.x(),
                    scrollRect.y(), scrollRect.width(), scrollRect.height(), buttonsRect.x(),
                    buttonsRect.y(), buttonsRect.width(), buttonsRect.height());
    }
}

int main(int argc, char **argv)
{
    bool selfcheck = false;
    QString screenshotDir;
    QString scaleRestartDir;
    QString environmentProbeDir;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--selfcheck") == 0) {
            selfcheck = true;
        } else if (std::strcmp(argv[index], "--screenshot") == 0 && index + 1 < argc) {
            screenshotDir = QString::fromLocal8Bit(argv[++index]);
        } else if (std::strcmp(argv[index], "--scale-restart") == 0 && index + 1 < argc) {
            // UX batch 2 / A4, acceptance ⑥: ONLY the restart path, so the
            // dedicated ctest row names exactly what it asserts.
            scaleRestartDir = QString::fromLocal8Bit(argv[++index]);
        } else if (std::strcmp(argv[index], "--env-ui-probe") == 0 && index + 1 < argc) {
            // Batch 20261110c: the Xvfb + xcb hit-test probe for the Environment
            // page; a real QApplication is needed for widgetAt/sendEvent.
            environmentProbeDir = QString::fromLocal8Bit(argv[++index]);
        }
    }
    if (!environmentProbeDir.isEmpty()) {
        QApplication probeApp(argc, argv);
        std::printf("ENV-UI PROBE start dir=%s platform=%s\n",
                    qPrintable(environmentProbeDir), qPrintable(QApplication::platformName()));
        const int result = runEnvironmentProbe(environmentProbeDir);
        std::fflush(stdout);
        return result;
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
        std::printf("usage: %s --selfcheck [--screenshot <dir>] | --scale-restart <dir> | "
                    "--env-ui-probe <dir>\n", argv[0]);
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
    checkFimStoreOwnership(temp.path());
    checkFimDetectPromotion(temp.path());
    checkDialogSizeDiscipline(temp.path());
    checkAppearancePage(temp.path());
    checkFormatPage(temp.path());
    checkLintPage(temp.path());
    checkEnvironmentPage(temp.path());
    checkEnvironmentMaskAndFailures(temp.path());
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
