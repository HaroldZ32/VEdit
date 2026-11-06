// UX batch 2 / RM-2 — the REAL-MACHINE project-switch defect, in the real shell.
//
// The user switched to another folder in the SAME window and got a pile of stale
// files plus a stale environment. This binary drives the real ApplicationShell
// with the real project lane (installProject), the real file tree
// (installFileTree) and the real diagnostics lane (installDiagnostics), then
// switches A -> B -> A -> B -> A through the NEW top-level entry
// (`file.switch-project`, the same reset path as Open Folder, whose dialog is
// answered by the provider seam). It asserts, on every switch:
//
//   * the project panel's file set and the diagnostics controller's analysis set
//     are THE SAME set, and contain nothing from the project that was left;
//   * the backends' `-I`/`-D` are the NEW root's, never the previous root's
//     (the environment the LSP session is built from);
//   * the persisted project model - what the module catalog reads first - is the
//     new root's set, with no file from the previous project;
//   * the file tree shows the new root and no path of the previous one;
//   * one switch schedules EXACTLY ONE expansion, even though the same root is
//     reported again by every live surface;
//   * an UNSAVED document survives every switch: its tab is still open and its
//     edited buffer is untouched.
//
// The fixture's startup state also carries an IMPORTED filelist on purpose: the
// panel expands it, so the analysis side must see the same file (it used to
// expand with `nullptr, nullptr` and silently dropped imported lists).
//
// Headless: `QT_QPA_PLATFORM=offscreen` (the CMake test registration sets it).

#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "diagnostics_panel.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "file_tree_model.h"
#include "file_tree_widget.h"
#include "filetree_integration.h"
#include "ktexteditor_provider.h"
#include "panel_registry.h"
#include "language_registry.h"
#include "project_integration.h"
#include "project_model.h"
#include "project_state.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMenu>
#include <QMenuBar>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QToolBar>
#include <QTimer>
#include <QVariantMap>

#include <csignal>
#include <cstdio>
#include <functional>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : "  ", detail.isEmpty() ? "" : detail.toUtf8().constData());
    std::fflush(stdout);
}

bool waitUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

bool writeFile(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text.toUtf8()) == text.toUtf8().size();
}

/// One project-state file, in the format ProjectStateStore::load() reads. Used
/// to start the run with a RESTORED project that carries an imported filelist.
bool writePersistedState(const QString &stateDir, const QString &root,
                         const QStringList &filelists, const QStringList &files)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), 2);
    object.insert(QStringLiteral("projectRoot"), root);
    object.insert(QStringLiteral("capturedAtUtc"), QStringLiteral("2026-01-01T00:00:00Z"));
    object.insert(QStringLiteral("filelists"), QJsonArray::fromStringList(filelists));
    object.insert(QStringLiteral("variables"), QJsonArray());
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

QJsonObject readPersistedState(const QString &stateDir, bool *ok)
{
    QFile file(QDir(stateDir).filePath(QStringLiteral("project-model.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok != nullptr) {
            *ok = false;
        }
        return QJsonObject{};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (ok != nullptr) {
        *ok = document.isObject();
    }
    return document.object();
}

QStringList jsonStrings(const QJsonObject &object, const char *key)
{
    QStringList out;
    for (const QJsonValue &value : object.value(QString::fromLatin1(key)).toArray()) {
        if (value.isString()) {
            out.append(value.toString());
        }
    }
    return out;
}

bool insideRoot(const QString &path, const QString &root)
{
    if (root.isEmpty()) {
        return false;
    }
    if (path == root) {
        return true;
    }
    return path.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'));
}

/// The first path of `set` that lives UNDER `root`; EMPTY when none does.
/// (A project's file set may legitimately contain files outside its root - a
/// filelist may list them - so "the previous project is gone" means "no path
/// under the previous root", not "every path is inside the new root".)
QString firstUnder(const QStringList &paths, const QString &root)
{
    for (const QString &path : paths) {
        if (insideRoot(path, root)) {
            return path;
        }
    }
    return QString();
}

QString shown(const QString &value)
{
    return value.isEmpty() ? QStringLiteral("<none>") : value;
}

/// RM-11 (T24): the lint boundary. A pass that answers immediately and records
/// the EXACT request the engine was handed - the proof that a `.f` manifest or a
/// note never reaches the parser.
class FakeLintBackend final : public vedit::workspace::diagnostics::ProjectLintBackend {
public:
    QStringList filesSeen;
    int startCount = 0;

    bool available() const override { return true; }
    QString toolName() const override { return QStringLiteral("fake-lint"); }

    quint64 start(const vedit::workspace::diagnostics::LintPassRequest &request,
                  QString *) override
    {
        ++startCount;
        filesSeen = request.files;
        job_ = static_cast<quint64>(startCount);
        snapshot_ = vedit::workspace::diagnostics::LintPassSnapshot{};
        snapshot_.total = request.files.size();
        snapshot_.done = request.files.size();
        snapshot_.tool = QStringLiteral("fake-lint");
        snapshot_.phase = vedit::workspace::diagnostics::LintPhase::Finished;
        return job_;
    }
    vedit::workspace::diagnostics::LintPassSnapshot progress(quint64) const override
    {
        return snapshot_;
    }
    vedit::workspace::diagnostics::LspPolled poll(quint64) override
    {
        vedit::workspace::diagnostics::LspPolled polled;
        polled.pending = false; // `LspPolled::pending` defaults to true
        polled.ready = true;
        polled.kind = vedit::workspace::diagnostics::ResponseKind::Diagnostics;
        return polled;
    }
    void cancel(quint64) override {}
    void release(quint64) override {}

private:
    quint64 job_ = 0;
    vedit::workspace::diagnostics::LintPassSnapshot snapshot_;
};

} // namespace

int main(int argc, char **argv)
{
    // AGENT.md §4 ("Rust is statically linked into a C++ host"): Rust's runtime
    // init does not run here, and this binary drives the Rust service through
    // the same lanes the product does. A lane selftest that spawns LSP/lint
    // children and links Rust must install the SIGPIPE default itself or a
    // write to a dead child kills the TEST (the product does the same in
    // src/app/main.cpp).
    std::signal(SIGPIPE, SIG_IGN);
    QApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("rm2.tempdir", false);
        return 1;
    }

    // --- fixture ----------------------------------------------------------
    const QString stateDir = dir.filePath(QStringLiteral("state"));
    const QString rootA = dir.filePath(QStringLiteral("project-a"));
    const QString rootB = dir.filePath(QStringLiteral("project-b"));
    const QString sharedDir = dir.filePath(QStringLiteral("shared"));
    QDir().mkpath(stateDir);
    QDir().mkpath(rootA);
    QDir().mkpath(rootB);
    QDir().mkpath(sharedDir);

    const QString aSource = QDir(rootA).filePath(QStringLiteral("a.sv"));
    const QString bSource = QDir(rootB).filePath(QStringLiteral("b.sv"));
    const QString outsideSource = QDir(sharedDir).filePath(QStringLiteral("outside.sv"));
    const QString listedSource = QDir(sharedDir).filePath(QStringLiteral("listed.sv"));
    // RM-11: a manifest that pulls in real sources, a note and a build script.
    // The manifest is the project's INPUT: it must never be analysed, linted or
    // catalogued as a source, and neither must the two non-HDL files.
    const QString manifest = QDir(rootA).filePath(QStringLiteral("sources.f"));
    const QString noteFile = QDir(rootA).filePath(QStringLiteral("notes.md"));
    const QString scriptFile = QDir(rootA).filePath(QStringLiteral("run.tcl"));
    // NOT inside project-a: a root-level `.f` is auto-discovered by the scan,
    // so only an imported list from elsewhere proves the analysis side honours
    // the panel's imported-list input.
    const QString importedList = dir.filePath(QStringLiteral("lists/a-imported.f"));
    const bool fixture =
        writeFile(aSource, QStringLiteral("module a;\n  logic x;\nendmodule\n"))
        && writeFile(bSource, QStringLiteral("module b;\n  logic y;\nendmodule\n"))
        && writeFile(outsideSource, QStringLiteral("module outside;\nendmodule\n"))
        && writeFile(listedSource, QStringLiteral("module listed;\nendmodule\n"))
        && writeFile(noteFile, QStringLiteral("# notes\nnot HDL\n"))
        && writeFile(scriptFile, QStringLiteral("puts \"not HDL\"\n"))
        // An imported `.f` whose only entry is OUTSIDE the opened folder: only a
        // consumer that expands with the panel's imported lists can see it.
        && writeFile(importedList, QStringLiteral("../shared/outside.sv\n"))
        && writeFile(manifest,
                     QStringLiteral("a.sv\n../shared/listed.sv\nnotes.md\nrun.tcl\n"))
        && writeFile(QDir(rootA).filePath(QStringLiteral("vedit.toml")),
                     QStringLiteral("[project]\ninclude_dirs = [\"a_inc\"]\ndefines = "
                                    "[\"A_ONLY=1\"]\n"))
        && writeFile(QDir(rootB).filePath(QStringLiteral("vedit.toml")),
                     QStringLiteral("[project]\ninclude_dirs = [\"b_inc\"]\ndefines = "
                                    "[\"B_ONLY=1\"]\n"))
        // The restored model of the LAST session: root A with its imported list.
        && writePersistedState(stateDir, rootA, {importedList},
                               {aSource, outsideSource});
    check("rm2.fixture", fixture);
    if (!fixture) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }

    const QString incA = QDir(rootA).filePath(QStringLiteral("a_inc"));
    const QString incB = QDir(rootB).filePath(QStringLiteral("b_inc"));

    // --- the real shell + the three real lanes ----------------------------
    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.projectRoot = rootA;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    std::unique_ptr<vedit::app::ApplicationShell> shell(
        new vedit::app::ApplicationShell(options));
    if (provider != nullptr) {
        shell->editorArea()->setProvider(provider.get());
    }
    shell->editorArea()->ensurePane();

    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = rootA;
    integration.stateDir = stateDir;
    // Same order as src/workspace/integration.cpp: file tree, then project (it
    // binds the tree's directoryOpened by name), then diagnostics.
    vedit::workspace::installFileTree(*shell, integration);
    vedit::workspace::installProject(*shell, integration);
    vedit::workspace::installDiagnostics(*shell, integration);

    // Create the docks the assertions read (a hidden placeholder is not a tree).
    shell->panels()->showPanel(QStringLiteral("file-tree"), shell.get());
    shell->panels()->showPanel(QStringLiteral("outline"), shell.get());
    shell->resize(1200, 800);
    shell->show();
    QCoreApplication::processEvents();

    auto *panel = shell->findChild<vedit::workspace::diagnostics::DiagnosticsPanel *>();
    auto *controller = panel != nullptr ? panel->controller() : nullptr;
    auto *project = vedit::workspace::projectModelForShell(*shell);
    auto *tree = shell->findChild<vedit::workspace::FileTreeWidget *>();
    vedit::app::EditorPane *pane =
        shell->editorArea() != nullptr ? shell->editorArea()->activePane() : nullptr;
    check("rm2.panel", panel != nullptr);
    check("rm2.controller", controller != nullptr);
    check("rm2.project-model", project != nullptr);
    check("rm2.file-tree", tree != nullptr);
    check("rm2.pane", pane != nullptr);
    if (panel == nullptr || controller == nullptr || project == nullptr || tree == nullptr
        || pane == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    if (tree->rootPath() != rootA) {
        tree->openDirectory(rootA);
    }

    // The NEW top-level entry the user asked for: it must exist as a File
    // command AND as a toolbar button, and both lead to the same reset path.
    QAction *switchAction = shell->commands() != nullptr
        ? shell->commands()->action(QStringLiteral("file.switch-project"))
        : nullptr;
    auto *toolBar = shell->mainToolBar();
    const bool onToolBar = toolBar != nullptr && switchAction != nullptr
        && toolBar->actions().contains(switchAction);
    check("rm2.switch-action", switchAction != nullptr,
          QStringLiteral("text='%1'").arg(switchAction != nullptr ? switchAction->text() : ""));
    check("rm2.switch-action-on-toolbar", onToolBar);
    if (switchAction == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    const bool fileMenuHasIt = switchAction->text().contains(QStringLiteral("Project"));
    check("rm2.switch-action-labelled", fileMenuHasIt, switchAction->text());

    // Answer the folder dialog of the ONE reset path from the test.
    QString nextRoot = rootB;
    shell->setOpenFolderDialogProvider([&nextRoot]() { return nextRoot; });

    // --- helpers over the observable surfaces ------------------------------
    const auto diagnosticsProject = [&shell]() {
        return shell->property("vedit.diagnostics.project").toMap();
    };
    const auto projectInputs = [&shell]() {
        return shell->property("vedit.project.inputs").toMap();
    };
    const auto projectEnvironment = [&shell]() {
        return shell->property("vedit.diagnostics.projectEnv").toMap();
    };
    const auto diagnosticsSettled = [&controller, &diagnosticsProject](const QString &root) {
        const QVariantMap state = diagnosticsProject();
        return state.value(QStringLiteral("root")).toString() == root
            && !state.value(QStringLiteral("pending")).toBool()
            && !controller->fileSet().isEmpty();
    };
    const auto projectSettled = [&project](const QString &root) {
        return project->projectRoot() == root && !project->isPending() && !project->files().isEmpty();
    };
    const auto persistedMatches = [&controller, &stateDir](const QString &root) {
        bool ok = false;
        const QJsonObject object = readPersistedState(stateDir, &ok);
        return ok && object.value(QStringLiteral("projectRoot")).toString() == root
            && jsonStrings(object, "files").size() == controller->fileSet().size();
    };

    // --- startup: the RESTORED project's imported list reaches BOTH sides --
    const bool startSettled =
        waitUntil([&]() {
            return diagnosticsSettled(rootA) && projectSettled(rootA)
                && persistedMatches(rootA);
        }, 15000);
    const QStringList startSet = controller->fileSet();
    std::printf("RM2-START root='%s' analysis-files=%d panel-files=%d imported='%s'\n",
                qPrintable(rootA), static_cast<int>(startSet.size()),
                static_cast<int>(project->files().size()), qPrintable(importedList));
    for (const QString &path : startSet) {
        std::printf("RM2-START-FILE '%s'\n", qPrintable(path));
    }
    check("rm2.start-settled", startSettled);
    // RM-11: the language registry decides membership. The manifest and the
    // non-HDL files are in the folder but have no language, so they are not
    // sources; the sources the manifest lists ARE.
    check("rm2.start-manifest-not-a-source", !startSet.contains(manifest),
          QStringLiteral("manifest-in-set=%1").arg(startSet.contains(manifest) ? 1 : 0));
    check("rm2.start-non-hdl-not-sources",
          !startSet.contains(noteFile) && !startSet.contains(scriptFile));
    check("rm2.start-manifest-sources-present",
          startSet.contains(aSource) && startSet.contains(listedSource),
          QStringLiteral("listed=%1").arg(startSet.contains(listedSource) ? 1 : 0));
    check("rm2.start-registry-source-extensions",
          vedit::workspace::project::LanguageRegistry::instance()
                  .extensionsWith(vedit::workspace::project::Capability::Source)
              == QStringList{QStringLiteral("sv"), QStringLiteral("svh"), QStringLiteral("v"),
                             QStringLiteral("vh")},
          vedit::workspace::project::LanguageRegistry::instance()
              .extensionsWith(vedit::workspace::project::Capability::Source)
              .join(QLatin1Char(',')));
    check("rm2.start-analysis-has-imported-entry",
          startSet.contains(outsideSource),
          QStringLiteral("contains('%1')=%2")
              .arg(outsideSource)
              .arg(startSet.contains(outsideSource) ? 1 : 0));
    check("rm2.start-panel-and-analysis-agree",
          startSet.size() == project->files().size(),
          QStringLiteral("analysis=%1 panel=%2")
              .arg(startSet.size()).arg(project->files().size()));
    const QVariantMap startEnv = projectEnvironment();
    check("rm2.start-env-is-project-a",
          startEnv.value(QStringLiteral("includeDirs")).toStringList().contains(incA)
              && startEnv.value(QStringLiteral("defines")).toStringList().contains(
                  QStringLiteral("A_ONLY=1")),
          QStringLiteral("root='%1' dirs=%2 defines=%3")
              .arg(startEnv.value(QStringLiteral("root")).toString())
              .arg(startEnv.value(QStringLiteral("includeDirs")).toStringList().join(QLatin1Char(',')))
              .arg(startEnv.value(QStringLiteral("defines")).toStringList().join(QLatin1Char(','))));

    // --- the unsaved document that must survive every switch --------------
    QString openError;
    const bool opened = pane->openFile(aSource, &openError);
    check("rm2.unsaved.open", opened, openError);
    if (opened) {
        pane->setActiveTab(pane->tabIndexForPath(aSource));
        QCoreApplication::processEvents();
        auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget());
        if (view != nullptr && view->document() != nullptr) {
            view->document()->insertText(KTextEditor::Cursor(1, 2),
                                         QStringLiteral("unsaved-marker"));
        }
        const auto *document = view != nullptr ? view->document() : nullptr;
        check("rm2.unsaved.modified",
              document != nullptr && document->isModified()
                  && document->text().contains(QStringLiteral("unsaved-marker")));
    }

    // --- RM-11: a manifest and a non-HDL file carry NO diagnostics ---------
    // Opening them is what a user does; the analysis consumer must answer from
    // the registry ("no analyzer for this file type") and never hand them to a
    // Verilog server. The state is published where the UI reads it.
    const auto availability = [&shell]() {
        return shell->property("vedit.diagnostics.analysisAvailability").toMap();
    };
    const auto rowsFor = [&controller](const QString &path) {
        int rows = 0;
        for (const auto &entry : controller->diagnostics()) {
            if (entry.file == path) {
                ++rows;
            }
        }
        return rows;
    };
    for (const QString &path : {manifest, noteFile, scriptFile}) {
        QString openNonHdlError;
        const bool openedNonHdl = pane->openFile(path, &openNonHdlError);
        QCoreApplication::processEvents();
        const bool stateSettled = waitUntil([&]() {
            const QVariantMap state = availability();
            return state.value(QStringLiteral("path")).toString() == path
                && !state.value(QStringLiteral("available")).toBool();
        }, 5000);
        const QVariantMap state = availability();
        std::printf("RM11-NON-SOURCE path='%s' opened=%d available=%d reason='%s' rows=%d\n",
                    qPrintable(path), openedNonHdl ? 1 : 0,
                    state.value(QStringLiteral("available")).toBool() ? 1 : 0,
                    qPrintable(state.value(QStringLiteral("reason")).toString()),
                    rowsFor(path));
        check(QStringLiteral("rm11.not-a-source.%1")
                  .arg(QFileInfo(path).fileName())
                  .toUtf8()
                  .constData(),
              openedNonHdl && stateSettled
                  && !state.value(QStringLiteral("available")).toBool()
                  && !state.value(QStringLiteral("reason")).toString().isEmpty()
                  && rowsFor(path) == 0,
              QStringLiteral("available=%1 reason='%2'")
                  .arg(state.value(QStringLiteral("available")).toBool() ? 1 : 0)
                  .arg(state.value(QStringLiteral("reason")).toString()));
        check(QStringLiteral("rm11.not-a-source.rows.%1")
                  .arg(QFileInfo(path).fileName())
                  .toUtf8()
                  .constData(),
              rowsFor(path) == 0);
    }
    // The registry is DATA: registering a language is all it takes for its
    // files to become analysable. No consumer is touched.
    vedit::workspace::project::LanguageSpec fakeLanguage;
    fakeLanguage.id = QStringLiteral("fakelang");
    fakeLanguage.displayName = QStringLiteral("Fake Language");
    fakeLanguage.extensions = {QStringLiteral("fkh")};
    fakeLanguage.capabilities = {
        vedit::workspace::project::Capability::Source,
        vedit::workspace::project::Capability::Diagnostics,
        vedit::workspace::project::Capability::Lint,
        vedit::workspace::project::Capability::Catalog,
    };
    auto &registry = vedit::workspace::project::LanguageRegistry::instance();
    const QString probePath = QDir(rootA).filePath(QStringLiteral("probe.fkh"));
    check("rm11.fake-language-unknown-before",
          !registry.supports(probePath, vedit::workspace::project::Capability::Diagnostics));
    registry.registerLanguage(fakeLanguage);
    check("rm11.fake-language-registered",
          registry.supports(probePath, vedit::workspace::project::Capability::Diagnostics));
    writeFile(probePath, QStringLiteral("fake language content\n"));
    QString openProbeError;
    const bool openedProbe = pane->openFile(probePath, &openProbeError);
    QCoreApplication::processEvents();
    const bool probeAvailable = waitUntil([&]() {
        const QVariantMap state = availability();
        return state.value(QStringLiteral("path")).toString() == probePath
            && state.value(QStringLiteral("available")).toBool();
    }, 5000);
    check("rm11.fake-language-enters-analysis", openedProbe && probeAvailable,
          QStringLiteral("opened=%1 available=%2")
              .arg(openedProbe ? 1 : 0)
              .arg(probeAvailable ? 1 : 0));

    // =====================================================================
    // RM-11 (T24): the LINT boundary and the catalog boundary
    // =====================================================================
    // The one predicate answers per consumer. A `.f` manifest, a note and a build
    // script have no language, so no consumer can be asked to parse them; the
    // registered HDL files answer true.
    {
        const auto &registry = vedit::workspace::project::LanguageRegistry::instance();
        check("t24.predicate.lint",
              registry.supports(aSource, vedit::workspace::project::Capability::Lint)
                  && !registry.supports(manifest, vedit::workspace::project::Capability::Lint)
                  && !registry.supports(noteFile, vedit::workspace::project::Capability::Lint)
                  && !registry.supports(scriptFile, vedit::workspace::project::Capability::Lint));
        check("t24.predicate.catalog",
              registry.supports(listedSource, vedit::workspace::project::Capability::Catalog)
                  && !registry.supports(manifest,
                                        vedit::workspace::project::Capability::Catalog)
                  && !registry.supports(noteFile,
                                        vedit::workspace::project::Capability::Catalog)
                  && !registry.supports(scriptFile,
                                        vedit::workspace::project::Capability::Catalog));
    }

    auto fakeLintOwned = std::make_unique<FakeLintBackend>();
    FakeLintBackend *fakeLint = fakeLintOwned.get();
    controller->setProjectLintBackend(std::move(fakeLintOwned));
    {
        // A POLLUTED request: it carries the manifest, the note and the build
        // script next to the real sources, as if a caller had bypassed the
        // builders. The controller must exclude them before the engine and
        // record why.
        vedit::workspace::diagnostics::LintPassRequest polluted;
        polluted.files = {manifest, noteFile, scriptFile, aSource, listedSource};
        if (controller->projectPassRunning()) {
            controller->cancelProjectPass();
        }
        const bool started = controller->startProjectPass(polluted);
        // The product polls the pass from its own timer; the harness pumps the
        // same `refresh()` the panel/wiring call, so the pass is deterministic
        // without depending on a 300 ms timer inside this event loop.
        const bool settled = waitUntil([controller]() {
            controller->refresh();
            return !controller->projectPassRunning();
        }, 10000);
        const QStringList excluded = controller->projectPassNonSource();
        std::printf("T24-LINT-PASS started=%d settled=%d skipped-open='%s'\n",
                    started ? 1 : 0, settled ? 1 : 0,
                    qPrintable(controller->projectPassSkipped().join(QLatin1Char(','))));
        std::printf("T24-LINT started=%d engine-files=%d excluded=%d files='%s' excluded-files='%s'\n",
                    started ? 1 : 0, static_cast<int>(fakeLint->filesSeen.size()),
                    static_cast<int>(excluded.size()),
                    qPrintable(fakeLint->filesSeen.join(QLatin1Char(','))),
                    qPrintable(excluded.join(QLatin1Char(','))));
        check("t24.lint.boundary-started", started && settled);
        check("t24.lint.engine-never-sees-non-hdl",
              !fakeLint->filesSeen.contains(manifest)
                  && !fakeLint->filesSeen.contains(noteFile)
                  && !fakeLint->filesSeen.contains(scriptFile),
              fakeLint->filesSeen.join(QLatin1Char(',')));
        // An OPEN MODIFIED buffer is excluded from the CLI pass by the C8 open-
        // file snapshot (the language server lints unsaved edits), so `a.sv` may
        // legitimately be in `projectPassSkipped()` instead of the engine; the
        // closed source must reach the engine.
        check("t24.lint.sources-reach-engine",
              fakeLint->filesSeen.contains(listedSource)
                  && (fakeLint->filesSeen.contains(aSource)
                      || controller->projectPassSkipped().contains(aSource)),
              fakeLint->filesSeen.join(QLatin1Char(',')));
        check("t24.lint.excluded-recorded",
              excluded.contains(manifest) && excluded.contains(noteFile)
                  && excluded.contains(scriptFile),
              excluded.join(QLatin1Char(',')));
    }
    {
        // A request with ONLY non-HDL files: nothing starts and the recorded
        // reason says why - the outcome is exclusion, never a parse error.
        vedit::workspace::diagnostics::LintPassRequest nonHdlOnly;
        nonHdlOnly.files = {manifest, noteFile, scriptFile};
        if (controller->projectPassRunning()) {
            controller->cancelProjectPass();
        }
        QString nonHdlError;
        const bool started = controller->startProjectPass(nonHdlOnly, &nonHdlError);
        const vedit::workspace::diagnostics::LintPassSnapshot snapshot = controller->projectPass();
        std::printf("T24-LINT-NON-HDL-ONLY started=%d running=%d message='%s' error='%s'\n",
                    started ? 1 : 0, controller->projectPassRunning() ? 1 : 0,
                    qPrintable(snapshot.message), qPrintable(nonHdlError));
        check("t24.lint.non-hdl-only-refused", !started);
        check("t24.lint.reason-recorded",
              snapshot.message.contains(QStringLiteral("no lint analyzer"))
                  && controller->projectPassNonSource().size() == 3,
              snapshot.message);
    }

    // --- the switches ------------------------------------------------------
    struct SwitchStep {
        const char *name;
        QString target;
        QString previous;
        QString targetSource;
        QString previousSource;
        QString targetInc;
        QString previousInc;
        QString targetDefine;
        QString previousDefine;
        bool expectImportedEntry;
    };
    const SwitchStep steps[] = {
        {"A-to-B", rootB, rootA, bSource, aSource, incB, incA, QStringLiteral("B_ONLY=1"),
         QStringLiteral("A_ONLY=1"), false},
        {"B-to-A", rootA, rootB, aSource, bSource, incA, incB, QStringLiteral("A_ONLY=1"),
         QStringLiteral("B_ONLY=1"), false},
        {"A-to-B-again", rootB, rootA, bSource, aSource, incB, incA, QStringLiteral("B_ONLY=1"),
         QStringLiteral("A_ONLY=1"), false},
        {"B-to-A-again", rootA, rootB, aSource, bSource, incA, incB, QStringLiteral("A_ONLY=1"),
         QStringLiteral("B_ONLY=1"), false},
    };

    quint64 expectedExpansions = project->expansionCount();
    for (const SwitchStep &step : steps) {
        const auto name = [&step](const char *suffix) {
            return QStringLiteral("rm2.%1.%2").arg(QLatin1String(step.name), QLatin1String(suffix));
        };
        const quint64 before = project->expansionCount();
        nextRoot = step.target;
        switchAction->trigger(); // the REAL top entry -> openProjectFolderDialog -> setProjectRoot
        // Every live surface reports the same switch; the model must not
        // schedule a second expansion for it. This is the duplicate the user's
        // machine produced from more than one widget.
        project->openRoot(step.target);
        ++expectedExpansions;
        const bool settled =
            waitUntil([&]() {
                return diagnosticsSettled(step.target) && projectSettled(step.target)
                    && persistedMatches(step.target) && tree->rootPath() == step.target;
            }, 15000);
        QCoreApplication::processEvents();

        const QStringList analysis = controller->fileSet();
        const QVariantMap diag = diagnosticsProject();
        const QVariantMap inputs = projectInputs();
        const QVariantMap env = projectEnvironment();
        bool persistedOk = false;
        const QJsonObject persisted = readPersistedState(stateDir, &persistedOk);
        const QStringList persistedFiles = jsonStrings(persisted, "files");
        const QStringList envDirs = env.value(QStringLiteral("includeDirs")).toStringList();
        const QStringList envDefines = env.value(QStringLiteral("defines")).toStringList();
        const quint64 expansions = project->expansionCount();

        std::printf("RM2-SWITCH %s to='%s' from='%s' expansions=%llu settled=%d\n",
                    step.name, qPrintable(step.target), qPrintable(step.previous),
                    static_cast<unsigned long long>(expansions), settled ? 1 : 0);
        std::printf("RM2-SET root='%s' analysis=%d panel=%d persisted=%d\n",
                    qPrintable(diag.value(QStringLiteral("root")).toString()),
                    static_cast<int>(analysis.size()),
                    static_cast<int>(project->files().size()),
                    static_cast<int>(persistedFiles.size()));
        for (const QString &path : analysis) {
            std::printf("RM2-SET-FILE '%s'\n", qPrintable(path));
        }
        std::printf("RM2-ENV root='%s' include-dirs='%s' defines='%s'\n",
                    qPrintable(env.value(QStringLiteral("root")).toString()),
                    qPrintable(envDirs.join(QLatin1Char(','))),
                    qPrintable(envDefines.join(QLatin1Char(','))));

        check(name("settled").toUtf8().constData(), settled);
        check(name("diagnostics-root").toUtf8().constData(),
              diag.value(QStringLiteral("root")).toString() == step.target);
        check(name("project-root").toUtf8().constData(),
              inputs.value(QStringLiteral("root")).toString() == step.target
                  && project->projectRoot() == step.target);
        check(name("one-expansion").toUtf8().constData(), expansions == expectedExpansions,
              QStringLiteral("before=%1 after=%2 expected=%3")
                  .arg(before)
                  .arg(expansions)
                  .arg(expectedExpansions));
        // (iii) the analysis set IS the panel's set: same root, same count, and
        // the same imported-list behaviour (none after a switch: cleared).
        check(name("analysis-no-previous-file").toUtf8().constData(),
              !analysis.contains(step.previousSource)
                  && firstUnder(analysis, step.previous).isEmpty(),
              QStringLiteral("previous='%1' still-under-previous='%2'")
                  .arg(step.previousSource, shown(firstUnder(analysis, step.previous))));
        check(name("analysis-has-target-file").toUtf8().constData(),
              analysis.contains(step.targetSource));
        check(name("analysis-equals-panel").toUtf8().constData(),
              analysis.size() == project->files().size() && analysis.size() ==
                  diag.value(QStringLiteral("files")).toInt(),
              QStringLiteral("analysis=%1 panel=%2 property=%3")
                  .arg(analysis.size())
                  .arg(project->files().size())
                  .arg(diag.value(QStringLiteral("files")).toInt()));
        check(name("imported-list-cleared").toUtf8().constData(),
              analysis.contains(outsideSource) == step.expectImportedEntry,
              QStringLiteral("outside-in-set=%1")
                  .arg(analysis.contains(outsideSource) ? 1 : 0));
        // (i) the environment the next LSP session is built from.
        check(name("env-root").toUtf8().constData(),
              env.value(QStringLiteral("root")).toString() == step.target);
        // The values really went through the backend seam (selectBackend),
        // which is the ONE writer of the session's argv.
        check(name("env-pinned-through-backend").toUtf8().constData(),
              env.value(QStringLiteral("pinned")).toBool());
        check(name("env-target-include-dirs").toUtf8().constData(),
              envDirs.contains(step.targetInc),
              QStringLiteral("dirs='%1'").arg(envDirs.join(QLatin1Char(','))));
        check(name("env-no-previous-include-dirs").toUtf8().constData(),
              !envDirs.contains(step.previousInc),
              QStringLiteral("dirs='%1'").arg(envDirs.join(QLatin1Char(','))));
        check(name("env-target-defines").toUtf8().constData(),
              envDefines.contains(step.targetDefine),
              QStringLiteral("defines='%1'").arg(envDefines.join(QLatin1Char(','))));
        check(name("env-no-previous-defines").toUtf8().constData(),
              !envDefines.contains(step.previousDefine),
              QStringLiteral("defines='%1'").arg(envDefines.join(QLatin1Char(','))));
        // The panel's inputs are the new root's (a switch never carries the
        // previous project's imported lists).
        check(name("inputs-filelists-cleared").toUtf8().constData(),
              inputs.value(QStringLiteral("filelists")).toStringList().isEmpty());
        check(name("inputs-settings-path").toUtf8().constData(),
              inputs.value(QStringLiteral("settingsPath")).toString()
                  == QDir(stateDir).filePath(QStringLiteral("settings.toml")));
        // (ii) the persisted model - what the module catalog reads first - is
        // the new root's set, cleaned.
        check(name("persisted-root").toUtf8().constData(),
              persistedOk
                  && persisted.value(QStringLiteral("projectRoot")).toString() == step.target);
        check(name("persisted-no-previous-file").toUtf8().constData(),
              !persistedFiles.contains(step.previousSource)
                  && firstUnder(persistedFiles, step.previous).isEmpty(),
              QStringLiteral("still-under-previous='%1'")
                  .arg(shown(firstUnder(persistedFiles, step.previous))));
        check(name("persisted-count-matches-analysis").toUtf8().constData(),
              persistedFiles.size() == analysis.size(),
              QStringLiteral("persisted=%1 analysis=%2")
                  .arg(persistedFiles.size()).arg(analysis.size()));
        // The file tree re-rooted: the new root's file resolves, the previous
        // project's does not.
        check(name("tree-root").toUtf8().constData(), tree->rootPath() == step.target);
        check(name("tree-has-target-file").toUtf8().constData(),
              tree->model() != nullptr
                  && tree->model()->indexForPath(step.targetSource, true).isValid());
        check(name("tree-no-previous-file").toUtf8().constData(),
              tree->model() != nullptr
                  && !tree->model()->indexForPath(step.previousSource, true).isValid());
        // The unsaved document survived (nothing closes a tab on a switch).
        const int tab = pane->tabIndexForPath(aSource);
        const bool tabStillOpen = tab >= 0 && pane->openFilePaths().contains(aSource);
        QCoreApplication::processEvents();
        pane->setActiveTab(tab);
        QCoreApplication::processEvents();
        auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget());
        KTextEditor::Document *document = view != nullptr ? view->document() : nullptr;
        const bool bufferIntact = document != nullptr && document->isModified()
            && document->text().contains(QStringLiteral("unsaved-marker"));
        check(name("unsaved-tab-survives").toUtf8().constData(), tabStillOpen);
        check(name("unsaved-buffer-survives").toUtf8().constData(), bufferIntact);
    }

    // --- the File menu itself carries the entry ---------------------------
    QAction *fileMenuSwitch = nullptr;
    if (QMenuBar *bar = shell->menuBar()) {
        for (QAction *menuAction : bar->actions()) {
            if (QMenu *menu = menuAction->menu()) {
                if (menu->objectName() == QStringLiteral("menu-file")
                    && menu->actions().contains(switchAction)) {
                    fileMenuSwitch = switchAction;
                }
            }
        }
    }
    check("rm2.switch-action-in-file-menu", fileMenuSwitch != nullptr);

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
