// Lane B acceptance test for batch-20261116 (AC M1.1/M1.2/M1.3/M1.5/M1.7).
//
// The module tree's failure semantics are only observable against a REAL job:
// the sentence, the error CLASS the UI branches on, whether a second `slang` was
// started, and whether the scope/closure the diagnostics lane consumes survived.
// So this test drives the PRODUCT shell with
//
//   * a real `slang` (passed by CMake, same binary as the catalog acceptance
//     test) for the success preconditions, and
//   * a fake `slang` that records every start and then sleeps far past the
//     child's budget — the only way to prove "a failure does not re-run itself"
//     without waiting the real 120 s, and the only way to prove concurrency
//     without guessing.
//
// Everything is asserted from the outside: the shell's published properties, the
// panel the user reads, and the fake tool's own log.
#include "catalog_client.h"
#include "module_catalog.h"
#include "module_tree_integration.h"
#include "module_tree_panel.h"
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
#include "project_state.h"
#endif

#include "app_shell.h"
#include "integration.h"
#include "panel_registry.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QProcess>
#include <QSaveFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>

#include <cstdio>
#include <functional>
#include <memory>

namespace {

using vedit::workspace::module_tree::CatalogClient;

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    // A printed FAIL must ALSO fail the process: without this the summary said
    // "0 failure(s)" and the binary exited 0 while a check was red (found while
    // adding the G1-f assertions - the lane's result was decorative).
    if (!condition) {
        ++g_failures;
    }
    std::printf("%s %s%s\n", condition ? "ok  " : "FAIL", name,
                detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    std::fflush(stdout);
}

/// Pump the event loop until `predicate` holds or `timeoutMs` expires. Returns
/// whether it held (never sleeps blindly).
bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    return predicate();
}

bool writeText(const QString &path, const QString &text, bool executable = false)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    file.close();
    if (executable) {
        return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                   | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther
                                   | QFile::ExeOther);
    }
    return true;
}

/// A fake `slang`: appends `<ns>` to `log`, sleeps `seconds`, exits 1 without any
/// AST JSON. Every start is recorded BEFORE the sleep, so the log is a complete
/// timeline of the children that ran.
QString makeFakeSlang(const QString &directory, const QString &log, int seconds)
{
    const QString script = QDir(directory).absoluteFilePath(QStringLiteral("fake-slang.sh"));
    const QString body = QStringLiteral("#!/bin/sh\n"
                                        "date +%s%N >> '%1'\n"
                                        "sleep %2\n"
                                        "exit 1\n")
                             .arg(log)
                             .arg(seconds);
    if (!writeText(script, body, /*executable=*/true)) {
        return QString();
    }
    return script;
}

QList<qint64> startTimestamps(const QString &log)
{
    QList<qint64> stamps;
    QFile file(log);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return stamps;
    }
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        bool ok = false;
        const qint64 value = line.toLongLong(&ok);
        if (ok) {
            stamps.append(value);
        }
    }
    return stamps;
}

/// The module-tree holder of one shell (it is the lane's single durable object).
QObject *holderOf(vedit::app::ApplicationShell *shell)
{
    return shell->findChild<QObject *>(QStringLiteral("module-tree-holder"));
}

int moduleTreeRows(vedit::app::ApplicationShell *shell)
{
    auto *panel = shell->findChild<vedit::workspace::module_tree::ModuleTreePanel *>();
    return panel != nullptr && panel->tree() != nullptr ? panel->tree()->topLevelItemCount() : 0;
}

QString panelStatus(vedit::app::ApplicationShell *shell)
{
    auto *panel = shell->findChild<vedit::workspace::module_tree::ModuleTreePanel *>();
    return panel != nullptr ? panel->statusText() : QString();
}

vedit::workspace::module_tree::ModuleTreePanel *treePanelOf(vedit::app::ApplicationShell *shell)
{
    return shell->findChild<vedit::workspace::module_tree::ModuleTreePanel *>();
}

/// The user's Refresh gesture, through the REAL button (not the probe hook).
bool clickRefresh(vedit::app::ApplicationShell *shell)
{
    auto *panel = treePanelOf(shell);
    if (panel == nullptr) {
        return false;
    }
    auto *button = panel->findChild<QToolButton *>(QStringLiteral("module-tree-refresh"));
    if (button == nullptr) {
        return false;
    }
    button->click();
    return true;
}

/// One shell with the module-tree lane installed, exactly as the product does it.
struct Harness {
    std::unique_ptr<vedit::app::ApplicationShell> shell;
};

bool startShell(Harness &harness, const QString &stateDir, const QString &root)
{
    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = root;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    harness.shell = std::make_unique<vedit::app::ApplicationShell>(options);
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = root;
    integration.stateDir = stateDir;
    vedit::workspace::installModuleTree(*harness.shell, integration);
    harness.shell->resize(1280, 860);
    harness.shell->show();
    QCoreApplication::processEvents();
    return holderOf(harness.shell.get()) != nullptr;
}

#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
/// A persisted project model: the project lane's own file, so the client reads the
/// file set the panel would read (no expansion needed, which is what makes the
/// file COUNT controllable — the large-project rule depends on it).
bool writeProjectModel(const QString &stateDir, const QString &root, const QStringList &files,
                       const QString &top)
{
    vedit::workspace::project::ProjectState state;
    state.root = root;
    state.files = files;
    state.top = top;
    state.capturedAt = vedit::workspace::project::ProjectState::nowUtc();
    return vedit::workspace::project::ProjectStateStore(stateDir).save(state, nullptr);
}
#endif

/// One `project-model.json` in an EXPLICIT older format version, written by hand:
/// `ProjectStateStore::save` always writes the current version, and the whole
/// point of G1-e is what a file from an OLDER build may and may not authorise.
bool writeProjectModelVersion(const QString &stateDir, const QString &root,
                              const QStringList &files, int version)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), version);
    object.insert(QStringLiteral("projectRoot"), root);
    object.insert(QStringLiteral("capturedAtUtc"), QStringLiteral("2026-11-01T00:00:00Z"));
    object.insert(QStringLiteral("filelists"), QJsonArray());
    object.insert(QStringLiteral("variables"), QJsonArray());
    object.insert(QStringLiteral("variablesByRoot"), QJsonObject());
    object.insert(QStringLiteral("top"), QString());
    object.insert(QStringLiteral("topByRoot"), QJsonObject());
    object.insert(QStringLiteral("files"), QJsonArray::fromStringList(files));
    object.insert(QStringLiteral("includeDirs"), QJsonArray());
    object.insert(QStringLiteral("defines"), QJsonArray());
    object.insert(QStringLiteral("diagnostics"), QJsonArray());
    QSaveFile file(QDir(stateDir).filePath(QStringLiteral("project-model.json")));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
}

/// G1-e (batch-20261119): a version-5 `project-model.json` must NOT be adopted.
///
/// Up to version 5 the `files` array was whatever the expansion produced - on an
/// `auto_discover` build that is a folder SCAN - and the client used to trust it
/// on "root matches + files non-empty" alone. The version gate drops it, so the
/// client expands the root live and the stale list's file never appears in the
/// effective set (the file it names does not even exist on disk).
void phaseStaleModelNotTrusted(const QString &fixture, const QString &slang)
{
    QTemporaryDir stateDir;
    check("stale-model.temp-dir", stateDir.isValid());
    const QString ghost = QDir(fixture).absoluteFilePath(QStringLiteral("ghost-from-v5.sv"));
    const QString statePath = stateDir.path();
    check("stale-model.written",
          writeProjectModelVersion(statePath, fixture, {ghost}, /*version=*/5), statePath);

    CatalogClient client;
    client.setStateDirectory(statePath);
    client.setSlangBinary(slang);
    client.start(fixture);
    check("stale-model.run-started", client.isRunning());
    check("stale-model.not-adopted",
          !client.sourceDescription().contains(QStringLiteral("project model")),
          client.sourceDescription());
    check("stale-model.expands-live",
          client.sourceDescription().contains(QStringLiteral("vd_project_expand_v2")),
          client.sourceDescription());
    // The live expansion answers with the fixture's REAL files; the ghost only
    // exists inside the stale array, so its presence would prove the file set was
    // adopted. (`lastFileSet()` is set when the expansion answer is turned into a
    // catalog run, i.e. before any `slang` work starts.)
    const bool answered = waitFor([&client]() { return !client.lastFileSet().isEmpty(); }, 120000);
    check("stale-model.expansion-answered", answered,
          QStringLiteral("files=%1 source='%2'")
              .arg(client.lastFileSet().size())
              .arg(client.sourceDescription()));
    check("stale-model.ghost-excluded", !client.lastFileSet().contains(ghost),
          QStringLiteral("files=%1").arg(client.lastFileSet().size()));
    check("stale-model.live-set-non-empty", !client.lastFileSet().isEmpty(),
          QStringLiteral("files=%1").arg(client.lastFileSet().size()));
    client.cancel();
}

/// Phase A: a REAL catalog run (the precondition the failure phases need: a
/// non-empty closure and a non-empty tree).
void phaseRealCatalog(const QString &fixture, const QString &slang, const QString &stateDir)
{
    qputenv("VEDIT_SLANG", slang.toLocal8Bit());
    Harness harness;
    check("real.holder-installed", startShell(harness, stateDir, fixture));
    auto *shell = harness.shell.get();
    const bool ready = waitFor(
        [shell]() { return moduleTreeRows(shell) > 0; }, 180000);
    check("real.catalog-ready", ready,
          QStringLiteral("rows=%1 status='%2'").arg(moduleTreeRows(shell)).arg(panelStatus(shell)));
    const QVariantMap closure = shell->property("vedit.moduleTree.closure").toMap();
    check("real.closure-published", !closure.isEmpty(),
          QStringLiteral("files=%1").arg(closure.size()));
    check("real.scope-module-count-published",
          shell->property("vedit.moduleTree.closureModules").toInt() > 0,
          QString::number(shell->property("vedit.moduleTree.closureModules").toInt()));

    // G1-f (batch-20261119): the closure is re-published on EVERY lifecycle
    // transition, not only when a catalog run succeeds or fails. A root switch
    // clears the catalog, so it must publish the EMPTY closure instead of leaving
    // the previous project's file map (keyed by ABSOLUTE paths) on the shell for
    // the diagnostics lane to pick up.
    auto *client = shell->findChild<CatalogClient *>();
    auto *panel = treePanelOf(shell);
    check("g1f.probe.client", client != nullptr);
    if (client != nullptr && panel != nullptr) {
        // A switch to ANOTHER root is the case the old code left stale: the
        // previous catalog is dropped and a NEW run starts, so nothing rewrote
        // the property and the old project's map stayed published until the new
        // run finished. Assert it synchronously, before any polling can run.
        QTemporaryDir otherRoot;
        QDir().mkpath(otherRoot.path() + QStringLiteral("/rtl"));
        writeText(QDir(otherRoot.path()).absoluteFilePath(QStringLiteral("rtl/other.sv")),
                  QStringLiteral("module other; endmodule\n"));
        shell->setProjectRoot(otherRoot.path());
        QCoreApplication::processEvents();
        check("g1f.root-switch.closure-empty",
              shell->property("vedit.moduleTree.closure").toMap().isEmpty(),
              QStringLiteral("files=%1")
                  .arg(shell->property("vedit.moduleTree.closure").toMap().size()));
        check("g1f.root-switch.closure-modules-zero",
              shell->property("vedit.moduleTree.closureModules").toInt() == 0,
              QString::number(shell->property("vedit.moduleTree.closureModules").toInt()));

        // A CANCEL re-publishes too. The stale state is seeded explicitly (the
        // previous project's map) because that is exactly what the property held
        // before this fix: a transition that never wrote it again.
        check("g1f.cancel.run-started", client->isRunning());
        QVariantMap stale;
        stale.insert(QStringLiteral("/old/project/top.sv"),
                     QStringList{QStringLiteral("/old/project/child.sv")});
        shell->setProperty("vedit.moduleTree.closure", stale);
        shell->setProperty("vedit.moduleTree.closureModules", 9);
        auto *cancel = panel->findChild<QToolButton *>(QStringLiteral("module-tree-cancel"));
        check("g1f.cancel.button", cancel != nullptr && cancel->isEnabled());
        if (cancel != nullptr) {
            cancel->click();
        }
        QCoreApplication::processEvents();
        check("g1f.cancel.closure-republished",
              shell->property("vedit.moduleTree.closure").toMap().isEmpty(),
              QStringLiteral("files=%1")
                  .arg(shell->property("vedit.moduleTree.closure").toMap().size()));
        check("g1f.cancel.closure-modules-zero",
              shell->property("vedit.moduleTree.closureModules").toInt() == 0,
              QString::number(shell->property("vedit.moduleTree.closureModules").toInt()));
    }
}

/// Phase B: a timeout on a REAL persisted project model.
///
/// AC M1.1  the user's top survives (the combo still shows it, and the persisted
///          model is untouched): only UnknownTop may fall back to Auto.
/// AC M1.2  a failed run does NOT re-run itself: exactly ONE fake `slang` start.
/// AC M1.7  the previous run's scope/closure are cleared on failure.
/// AC M1.3  three Refresh clicks while a run is in flight produce at most two
///          SERIAL runs — never two concurrent children.
void phaseTimeoutAndSingleFlight(const QString &stateDir, const QString &root,
                                 const QString &slangForSuccess)
{
    const QString log = QDir(stateDir).absoluteFilePath(QStringLiteral("fake-slang.log"));
    const QString fake = makeFakeSlang(stateDir, log, /*seconds=*/8);
    check("timeout.fake-slang-created", !fake.isEmpty() && QFileInfo(fake).isExecutable(), fake);

    QStringList files;
    for (int index = 0; index < 12; ++index) {
        files.append(QDir(root).absoluteFilePath(QStringLiteral("src/m%1.sv").arg(index)));
    }
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    const QString rootStateDir = QDir(stateDir).absoluteFilePath(QStringLiteral("state"));
    QDir().mkpath(rootStateDir);
    check("timeout.project-model-written",
          writeProjectModel(rootStateDir, root, files, QStringLiteral("top_a")));
#else
    const QString rootStateDir = stateDir;
#endif
    // The child's budget is the test seam: 500 ms, so the 8 s fake tool is killed
    // by the TIMEOUT path (not by our cancel) and the whole phase stays fast.
    qputenv("VEDIT_CATALOG_TIMEOUT_MS", QByteArrayLiteral("500"));
    qputenv("VEDIT_SLANG", fake.toLocal8Bit());

    Harness harness;
    check("timeout.holder-installed", startShell(harness, rootStateDir, root));
    auto *shell = harness.shell.get();
    auto *panel = treePanelOf(shell);

    // AC M1.6: with NO successful catalog the typed name must be ACCEPTED and sent
    // as `--top` (the old rule silently cleared it). Pressing Enter is the one
    // gesture that commits it - and the single-flight dedupe means the editable
    // combo's two signals still start exactly ONE run.
    auto *combo = panel->findChild<QComboBox *>(QStringLiteral("module-tree-top"));
    check("timeout.top-combo-present", combo != nullptr && combo->isEditable());
    if (combo != nullptr) {
        combo->setEditText(QStringLiteral("top_a"));
        QMetaObject::invokeMethod(combo->lineEdit(), "returnPressed");
    }
    QCoreApplication::processEvents();
    check("timeout.top-visible-before-the-run",
          panel != nullptr && panel->effectiveTop() == QStringLiteral("top_a"),
          panel != nullptr ? panel->effectiveTop() : QString());
    // Two more gestures while that run is in flight: single flight means they are
    // coalesced into ONE follow-up run (never a second concurrent child).
    clickRefresh(shell);
    clickRefresh(shell);
    const bool failed = waitFor(
        [&log]() { return startTimestamps(log).size() >= 1; }, 20000);
    check("timeout.fake-slang-ran", failed, QStringLiteral("log=%1").arg(log));
    // Wait for the first failure's sentence to reach the panel, then give the
    // holder well past the old 1.5 s retry window to prove no automatic re-run.
    waitFor([shell]() { return panelStatus(shell).contains(QStringLiteral("timed out")); }, 20000);
    QThread::msleep(2500);
    QCoreApplication::processEvents();

    const QList<qint64> stamps = startTimestamps(log);
    check("timeout.status-names-the-timeout",
          panelStatus(shell).contains(QStringLiteral("timed out")), panelStatus(shell));
    // AC M1.1: the top is still what the user chose.
    check("timeout.top-preserved", panel->effectiveTop() == QStringLiteral("top_a"),
          panel->effectiveTop());
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    bool stateOk = false;
    const vedit::workspace::project::ProjectState reloaded =
        vedit::workspace::project::ProjectStateStore(rootStateDir).load(&stateOk, nullptr);
    check("timeout.persisted-top-preserved",
          stateOk && reloaded.top == QStringLiteral("top_a"),
          QStringLiteral("persisted='%1'").arg(reloaded.top));
#endif
    // AC M1.7: the failed run publishes an EMPTY scope/closure.
    check("timeout.closure-cleared", shell->property("vedit.moduleTree.closure").toMap().isEmpty(),
          QString::number(shell->property("vedit.moduleTree.closure").toMap().size()));
    check("timeout.closure-module-count-zero",
          shell->property("vedit.moduleTree.closureModules").toInt() == 0,
          QString::number(shell->property("vedit.moduleTree.closureModules").toInt()));
    // AC M1.2/M1.3: three gestures, at most two runs, and the second starts only
    // after the first run's budget expired (it ran at least the 500 ms budget, so
    // a concurrent child would show a delta far below that).
    check("timeout.no-unconditional-retry", stamps.size() <= 2,
          QStringLiteral("starts=%1").arg(stamps.size()));
    bool serial = stamps.size() <= 1;
    if (stamps.size() >= 2) {
        const qint64 deltaMs = (stamps.at(1) - stamps.at(0)) / 1000000;
        serial = deltaMs >= 400;
        check("timeout.runs-are-serial", serial,
              QStringLiteral("delta=%1ms (budget 500ms)").arg(deltaMs));
    } else {
        check("timeout.runs-are-serial", serial, QStringLiteral("starts=1"));
    }
    // The Cancel button is the user's exit and must be offered exactly while a run
    // is in flight (AC M1.4 wires the gesture; the Rust kill is proven by
    // `cargo test -p vd-ffi cancelling_a_running_job_kills_the_tool`).
    check("timeout.cancel-disabled-when-idle", panel != nullptr && !panel->cancelEnabled());

    // A cancelled run stops the child: one gesture, then Cancel.
    QFile::remove(log);
    clickRefresh(shell);
    const bool running = waitFor([&log]() { return !startTimestamps(log).isEmpty(); }, 20000);
    check("cancel.fake-slang-ran", running);
    auto *cancel = panel->findChild<QToolButton *>(QStringLiteral("module-tree-cancel"));
    check("cancel.button-enabled-while-running", cancel != nullptr && cancel->isEnabled());
    if (cancel != nullptr) {
        cancel->click();
    }
    const bool cancelledQuickly = waitFor(
        [panel]() { return panel->statusText().contains(QStringLiteral("cancelled")); }, 5000);
    check("cancel.reports-cancelled", cancelledQuickly, panel->statusText());
    check("cancel.has-started-exactly-one-run", startTimestamps(log).size() == 1,
          QStringLiteral("starts=%1").arg(startTimestamps(log).size()));
    Q_UNUSED(slangForSuccess);
    qunsetenv("VEDIT_CATALOG_TIMEOUT_MS");
}

/// Phase C: AC M1.5 — a large persisted file set is NOT catalogued automatically,
/// and the panel says what to do instead.
void phaseLargeProjectGate(const QString &stateDir, const QString &root)
{
    QStringList files;
    for (int index = 0; index < 6000; ++index) {
        files.append(QDir(root).absoluteFilePath(QStringLiteral("big/m%1.sv").arg(index)));
    }
    const QString largeStateDir = QDir(stateDir).absoluteFilePath(QStringLiteral("large"));
    QDir().mkpath(largeStateDir);
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    check("large.project-model-written",
          writeProjectModel(largeStateDir, root, files, QString()));
#endif
    const QString log = QDir(stateDir).absoluteFilePath(QStringLiteral("large-slang.log"));
    const QString fake = makeFakeSlang(stateDir, log, /*seconds=1*/ 1);
    qputenv("VEDIT_SLANG", fake.toLocal8Bit());

    Harness harness;
    check("large.holder-installed", startShell(harness, largeStateDir, root));
    auto *shell = harness.shell.get();
    const bool hinted = waitFor(
        [shell]() { return panelStatus(shell).contains(QStringLiteral("Large project")); }, 15000);
    check("large.hint-shown", hinted, panelStatus(shell));
    check("large.no-automatic-slang-run", startTimestamps(log).isEmpty(),
          QStringLiteral("starts=%1").arg(startTimestamps(log).size()));
    check("large.tree-stays-empty", moduleTreeRows(shell) == 0,
          QStringLiteral("rows=%1").arg(moduleTreeRows(shell)));

    // The user's explicit Refresh runs it (that is the whole point of the gate).
    clickRefresh(shell);
    const bool ran = waitFor([&log]() { return !startTimestamps(log).isEmpty(); }, 20000);
    check("large.refresh-runs-it", ran,
          QStringLiteral("starts=%1").arg(startTimestamps(log).size()));
    Q_UNUSED(root);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const QString fixture = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString slang = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (!QFileInfo(fixture).isDir() || !QFileInfo(slang).isExecutable()) {
        std::printf("FAIL lane-b.real — the lane fixture and a built slang are required "
                    "(fixture='%s' slang='%s')\n",
                    fixture.toStdString().c_str(), slang.toStdString().c_str());
        std::printf("FAIL: 0 check(s), 1 failure(s)\n");
        std::fflush(stdout);
        return 1;
    }

    QTemporaryDir workspace;
    if (!workspace.isValid()) {
        std::printf("FAIL: the temporary workspace could not be created\n");
        return 1;
    }
    const QString root = workspace.path();
    QDir().mkpath(QDir(root).absoluteFilePath(QStringLiteral("src")));
    QDir().mkpath(QDir(root).absoluteFilePath(QStringLiteral("big")));
    // The persisted model points at paths the catalog NEVER has to read: the fake
    // tool decides how the run ends, and the real phase uses its own fixture.
    for (int index = 0; index < 12; ++index) {
        writeText(QDir(root).absoluteFilePath(QStringLiteral("src/m%1.sv").arg(index)),
                  QStringLiteral("module m%1; endmodule\n").arg(index));
    }

    phaseRealCatalog(fixture, slang, QDir(root).absoluteFilePath(QStringLiteral("real-state")));
    phaseStaleModelNotTrusted(fixture, slang);
    phaseTimeoutAndSingleFlight(root, root, slang);
    phaseLargeProjectGate(root, root);

    std::printf("%s: %d check(s), %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
