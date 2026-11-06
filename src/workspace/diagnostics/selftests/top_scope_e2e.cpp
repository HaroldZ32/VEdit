// RM-17b (T41) — the top scope, END TO END (no unit-level shortcut).
//
// RM-17 promises: with a named top, the lint pass covers only the files
// reachable from it, every file outside it keeps the visible hollow
// "not analysed" state (never green, never "no mark"), and Auto restores the
// whole set. The writer named its own gap: that promise was compile-verified but
// no ctest drove it through the product seams.
//
// This binary drives the REAL seams:
//   * the diagnostics lane is installed on a real ApplicationShell (so the
//     top-scope sink / `BackendWiring::applyTopScope` is the product one),
//   * the scope is published through src/workspace/top_scope.h exactly as the
//     module tree publishes it (`publishTopScope`, never the controller setter),
//   * the lint request is observed at the backend boundary (a deterministic
//     fake ProjectLintBackend records `request.files`), and the verdicts come
//     from the product's DiagnosticsController,
//   * the project set comes from the real Rust expansion of a fixture directory
//     (`top.sv` names A which instantiates B; `c.sv` is reachable from neither;
//     `sources.f` is the project's filelist).
//
// Three scenarios, each on its own shell (the sink is process-global, so shells
// are sequential):
//   1. FRESH SCOPE (the writer's order): the top is published before any pass
//      covered the files. Asserts the pass request excludes C, C is
//      NotAnalyzed, A/B get real verdicts, the aggregate line names the top,
//      the project list still shows C, and Auto restores C.
//   2. REALISTIC ORDER (the user's): the project-open automatic pass covers the
//      whole set, THEN the user names a top. Asserts the control (no top: C
//      analysed), the pass request excludes C - and what the dots actually show
//      for C afterwards. This is the precedence question the writer never drove.
//   3. DEFINITIONS-ONLY FILE: a pure `package` (no instance body) is outside the
//      reachable set by the writer's documented rule
//      (crates/vd-deeplint/src/catalog.rs `reachable_source_files`: only the
//      `source_file` of an elaborated `InstanceBody` counts), so the consumer
//      must treat it like any other excluded file.
//
// Run offscreen. Exit code 0 = every assertion passed.

#include "diagnostics_controller.h"
#include "diagnostics_integration.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "project_integration.h"
#include "project_model.h"
#include "project_panel.h"
#include "top_scope.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QListWidget>
#include <QStringList>
#include <QTemporaryDir>

#include <csignal>
#include <cstdio>
#include <cstdlib>
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
}

bool waitUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(text.toUtf8());
    file.close();
    return true;
}

using vedit::workspace::diagnostics::DiagnosticEntry;

/// Display-scale rows: 1 = error, 2 = warning (the project pass's CLI rows use
/// 0/1; the fake is the pass backend, so it speaks the CLI scale: 0 error,
/// 1 warning).
DiagnosticEntry row(const QString &file, int severity, const QString &code)
{
    DiagnosticEntry entry;
    entry.file = file;
    entry.line = 1;
    entry.column = 1;
    entry.severity = severity;
    entry.code = code;
    entry.message = code;
    entry.source = QStringLiteral("fake-lint");
    return entry;
}

/// The deterministic project-pass backend: one row per file the fixture wants a
/// verdict for, every other linted file CLEAN. `filesSeen` is the LAST request
/// the product built - the lint-scope boundary this test asserts on.
class FakeProjectLint final : public vedit::workspace::diagnostics::ProjectLintBackend {
public:
    QVector<DiagnosticEntry> rows;
    QStringList filesSeen;
    int startCount = 0;
    bool reachable = true;
    quint64 job_ = 0;
    vedit::workspace::diagnostics::LintPassSnapshot snapshot;

    bool available() const override { return reachable; }
    QString toolName() const override { return QStringLiteral("fake-lint"); }

    quint64 start(const vedit::workspace::diagnostics::LintPassRequest &request,
                  QString *) override
    {
        ++startCount;
        filesSeen = request.files;
        ++job_;
        snapshot = vedit::workspace::diagnostics::LintPassSnapshot{};
        snapshot.phase = vedit::workspace::diagnostics::LintPhase::Finished;
        snapshot.total = request.files.size();
        snapshot.done = request.files.size();
        snapshot.tool = toolName();
        return job_;
    }
    vedit::workspace::diagnostics::LintPassSnapshot progress(quint64) const override
    {
        return snapshot;
    }
    vedit::workspace::diagnostics::LspPolled poll(quint64) override
    {
        vedit::workspace::diagnostics::LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = vedit::workspace::diagnostics::ResponseKind::Diagnostics;
        polled.diagnostics = rows;
        return polled;
    }
    void cancel(quint64) override {}
    void release(quint64) override {}
};

struct Harness {
    // Declared BEFORE the shell: members die in reverse order, so the shell (and
    // its annotation-bridge timer, which dereferences the provider through the
    // editor area) dies first. A local provider inside makeHarness dangled and
    // crashed the 300 ms bridge timer.
    std::unique_ptr<vedit::app::EditorProvider> provider;
    std::unique_ptr<vedit::app::ApplicationShell> shell;
    vedit::workspace::diagnostics::DiagnosticsController *controller = nullptr;
    vedit::workspace::project::ProjectModel *projectModel = nullptr;
    vedit::workspace::project::ProjectPanel *projectPanel = nullptr;
    FakeProjectLint *fake = nullptr;
    bool ok = false;
};

Harness makeHarness(const QString &stateDir, const QString &root, bool fakeAvailable,
                    const QStringList &rowFiles)
{
    Harness harness;
    QDir().mkpath(stateDir);
    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.projectRoot = root;
    harness.provider = vedit::editor::makeEditCoreProvider();
    harness.shell = std::make_unique<vedit::app::ApplicationShell>(options);
    if (harness.provider != nullptr) {
        harness.shell->editorArea()->setProvider(harness.provider.get());
    }
    harness.shell->editorArea()->ensurePane();

    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = root;
    integration.stateDir = stateDir;
    vedit::workspace::installProject(*harness.shell, integration);
    vedit::workspace::installDiagnostics(*harness.shell, integration);

    harness.controller = vedit::workspace::diagnosticsControllerForShell(*harness.shell);
    harness.projectModel = vedit::workspace::projectModelForShell(*harness.shell);
    harness.projectPanel =
        harness.shell->findChild<vedit::workspace::project::ProjectPanel *>();
    if (harness.controller == nullptr || harness.projectModel == nullptr
        || harness.projectPanel == nullptr) {
        std::printf("OBSERVED harness controller=%d model=%d panel=%d\n",
                    harness.controller != nullptr ? 1 : 0,
                    harness.projectModel != nullptr ? 1 : 0,
                    harness.projectPanel != nullptr ? 1 : 0);
        return harness;
    }

    // Install the deterministic backend BEFORE the first event-loop turn: a
    // project open starts the automatic pass as soon as the set is adopted.
    auto owned = std::make_unique<FakeProjectLint>();
    owned->reachable = fakeAvailable;
    for (const QString &file : rowFiles) {
        if (file == rowFiles.value(0)) {
            owned->rows.append(row(file, 0, QStringLiteral("fake-error")));
        } else {
            owned->rows.append(row(file, 1, QStringLiteral("fake-warning")));
        }
    }
    harness.fake = owned.get();
    harness.controller->setProjectLintBackend(std::move(owned));

    harness.shell->resize(1280, 860);
    harness.shell->show();
    QCoreApplication::processEvents();
    harness.ok = true;
    return harness;
}

bool waitForProjectFiles(Harness &harness, const QStringList &files, int timeoutMs = 20000)
{
    return waitUntil(
        [&harness, &files]() {
            const QStringList adopted = harness.controller->projectFiles();
            for (const QString &file : files) {
                if (!adopted.contains(QDir::cleanPath(file))) {
                    return false;
                }
            }
            return true;
        },
        timeoutMs);
}

/// Trigger the product's explicit project lint action and wait for its terminal
/// state. Returns true when a NEW pass started and finished.
bool runExplicitPass(Harness &harness, int timeoutMs = 20000)
{
    QAction *action = harness.shell->commands() != nullptr
                          ? harness.shell->commands()->action(QStringLiteral("project.lint"))
                          : nullptr;
    if (action == nullptr) {
        return false;
    }
    const int before = harness.fake->startCount;
    action->trigger();
    if (!waitUntil([&harness, before]() { return harness.fake->startCount != before; }, 8000)) {
        return false;
    }
    return waitUntil([&harness]() { return !harness.controller->projectPassRunning(); }, timeoutMs);
}

int statusOf(Harness &harness, const QString &file)
{
    return harness.controller->fileStatuses().value(QDir::cleanPath(file), -1);
}

QStringList projectPanelRows(Harness &harness)
{
    QStringList rows;
    auto *list = harness.projectPanel->findChild<QListWidget *>(
        QStringLiteral("project-file-list"));
    if (list == nullptr) {
        return rows;
    }
    for (int index = 0; index < list->count(); ++index) {
        rows.append(list->item(index)->text());
    }
    return rows;
}

bool projectPanelShows(Harness &harness, const QString &fileName)
{
    const QStringList rows = projectPanelRows(harness);
    for (const QString &text : rows) {
        if (text == fileName || text.endsWith(QLatin1Char('/') + fileName)) {
            return true;
        }
    }
    return false;
}

void publishScope(const QString &top, const QStringList &analysed,
                  const QStringList &excluded)
{
    vedit::workspace::publishTopScope(top, analysed, excluded);
}

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGPIPE, SIG_IGN); // AGENT.md §4: the host links Rust
    QApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("rm17b.tempdir", false);
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    const QString root = dir.filePath(QStringLiteral("project"));
    QDir().mkpath(root);
    const QString topPath = QDir(root).filePath(QStringLiteral("top.sv"));
    const QString bPath = QDir(root).filePath(QStringLiteral("b.sv"));
    const QString cPath = QDir(root).filePath(QStringLiteral("c.sv"));
    const QString pkgPath = QDir(root).filePath(QStringLiteral("pkg.sv"));
    const QString listPath = QDir(root).filePath(QStringLiteral("sources.f"));

    // A instantiates B; C is reachable from neither; the `.f` filelist is the
    // project's own manifest (the fixture requirement).
    const bool fixture =
        writeFile(topPath, QStringLiteral("module A;\n  B u_b ();\nendmodule\n"))
        && writeFile(bPath, QStringLiteral("module B;\nendmodule\n"))
        && writeFile(cPath, QStringLiteral("module C;\nendmodule\n"))
        && writeFile(listPath, QStringLiteral("top.sv\nb.sv\nc.sv\n"));
    check("rm17b.fixture", fixture);
    if (!fixture) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }

    // The scope the module tree derives for this fixture: A's root instance and
    // B's body are the reachable files; C is project membership minus reachable.
    const QStringList analysed{topPath, bPath};
    const QStringList excluded{cPath};

    // =====================================================================
    // Scenario 1 - FRESH SCOPE (the writer's order): the top is published
    // before any pass covered the files.
    // =====================================================================
    {
        Harness harness = makeHarness(dir.filePath(QStringLiteral("state1")), root,
                                      /*fakeAvailable=*/false, {topPath, cPath});
        check("rm17b.s1.harness", harness.ok);
        if (!harness.ok) {
            std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
            return 1;
        }
        check("rm17b.s1.project-set",
              waitForProjectFiles(harness, {topPath, bPath, cPath}),
              QStringLiteral("files=%1").arg(harness.controller->projectFiles().size()));
        // No pass may have covered anything yet: the fake was unavailable, so
        // the automatic open pass could not start.
        check("rm17b.s1.no-coverage-before-scope",
              !harness.controller->hasUpperLayer(topPath)
                  && !harness.controller->hasUpperLayer(cPath),
              QStringLiteral("upper-top=%1 upper-c=%2")
                  .arg(harness.controller->hasUpperLayer(topPath) ? 1 : 0)
                  .arg(harness.controller->hasUpperLayer(cPath) ? 1 : 0));

        publishScope(QStringLiteral("A"), analysed, excluded);
        check("rm17b.s1.shell-top", harness.shell->property("vedit.diagnostics.topScope").toString()
                                        == QStringLiteral("A"),
              harness.shell->property("vedit.diagnostics.topScope").toString());
        check("rm17b.s1.shell-excluded-count",
              harness.shell->property("vedit.diagnostics.topScopeExcluded").toInt() == 1,
              QString::number(harness.shell->property("vedit.diagnostics.topScopeExcluded").toInt()));
        bool saysOutside = false;
        for (const QString &line : harness.shell->diagnostics()) {
            if (line.contains(QStringLiteral("1 file(s) outside the top A are not analysed"))) {
                saysOutside = true;
            }
        }
        check("rm17b.s1.aggregate-line", saysOutside,
              harness.shell->diagnostics().join(QStringLiteral(" | ")));
        check("rm17b.s1.excluded-mark", statusOf(harness, cPath) == 4,
              QStringLiteral("c=%1").arg(statusOf(harness, cPath)));
        check("rm17b.s1.excluded-never-green", statusOf(harness, cPath) != 3,
              QStringLiteral("c=%1").arg(statusOf(harness, cPath)));

        // Make the backend resolvable now that the scope is in place (no
        // backend swap: the same fake keeps its rows, only availability flips).
        harness.fake->reachable = true;

        check("rm17b.s1.pass-runs", runExplicitPass(harness));
        check("rm17b.s1.request-analysed-only",
              harness.fake->filesSeen.contains(QDir::cleanPath(topPath))
                  && harness.fake->filesSeen.contains(QDir::cleanPath(bPath)),
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s1.request-excludes-outside",
              !harness.fake->filesSeen.contains(QDir::cleanPath(cPath)),
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s1.request-no-foreign-files",
              harness.fake->filesSeen.size() == 2,
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s1.a-verdict", statusOf(harness, topPath) == 1,
              QStringLiteral("a=%1").arg(statusOf(harness, topPath)));
        check("rm17b.s1.b-clean", statusOf(harness, bPath) == 3,
              QStringLiteral("b=%1").arg(statusOf(harness, bPath)));
        check("rm17b.s1.outside-still-notanalysed", statusOf(harness, cPath) == 4,
              QStringLiteral("c=%1").arg(statusOf(harness, cPath)));
        std::printf("OBSERVED rm17b.s1.after-scoped-pass filesSeen='%s' upper='%s' "
                    "c-status-map=%d c-status-direct=%d c-covered=%d c-merged-rows=%lld "
                    "passFiles=%d\n",
                    qPrintable(harness.fake->filesSeen.join(QLatin1Char(','))),
                    qPrintable(harness.controller->upperLayerFiles().join(QLatin1Char(','))),
                    statusOf(harness, cPath),
                    static_cast<int>(harness.controller->statusForFile(cPath)),
                    harness.controller->hasUpperLayer(cPath) ? 1 : 0,
                    static_cast<long long>(
                        harness.controller->mergedDiagnosticsForFile(QDir::cleanPath(cPath)).size()),
                    harness.controller->projectPassFileCount());

        // Membership is unchanged: the Project panel's Files list still shows C.
        check("rm17b.s1.project-model-keeps-c",
              harness.projectModel->files().contains(QDir::cleanPath(cPath)),
              QStringLiteral("files=%1").arg(harness.projectModel->files().size()));
        check("rm17b.s1.project-list-shows-c", projectPanelShows(harness, QStringLiteral("c.sv")),
              projectPanelRows(harness).join(QLatin1Char(',')));

        // Return to Auto: the whole set is analysable again and the marker is
        // cleared (C gets its REAL fake verdict, not the hollow state).
        publishScope(QString(), QStringList{}, QStringList{});
        check("rm17b.s1.auto-shell-top",
              harness.shell->property("vedit.diagnostics.topScope").toString().isEmpty(),
              harness.shell->property("vedit.diagnostics.topScope").toString());
        check("rm17b.s1.auto-shell-excluded",
              harness.shell->property("vedit.diagnostics.topScopeExcluded").toInt() == 0,
              QString::number(harness.shell->property("vedit.diagnostics.topScopeExcluded").toInt()));
        check("rm17b.s1.auto-pass-runs", runExplicitPass(harness));
        check("rm17b.s1.auto-request-includes-c",
              harness.fake->filesSeen.contains(QDir::cleanPath(cPath)),
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s1.auto-c-analysed-again", statusOf(harness, cPath) == 2,
              QStringLiteral("c=%1").arg(statusOf(harness, cPath)));
        check("rm17b.s1.auto-c-not-notanalysed", statusOf(harness, cPath) != 4,
              QStringLiteral("c=%1").arg(statusOf(harness, cPath)));
    }

    // =====================================================================
    // Scenario 2 - REALISTIC ORDER: the project-open automatic pass covers the
    // whole set, THEN the user names a top (what actually happens in the app).
    // =====================================================================
    {
        Harness harness = makeHarness(dir.filePath(QStringLiteral("state2")), root,
                                      /*fakeAvailable=*/true, {topPath, cPath});
        check("rm17b.s2.harness", harness.ok);
        if (!harness.ok) {
            std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
            return 1;
        }
        const bool adopted = waitForProjectFiles(harness, {topPath, bPath, cPath});
        check("rm17b.s2.project-set", adopted);
        // CONTROL: with no top set the on-open pass covers C (it is analysed).
        const bool controlPass = waitUntil(
            [&harness, &cPath]() {
                return harness.fake->filesSeen.contains(QDir::cleanPath(cPath))
                       && !harness.controller->projectPassRunning();
            },
            20000);
        check("rm17b.s2.control-pass-ran", controlPass,
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s2.control-request-includes-c",
              harness.fake->filesSeen.contains(QDir::cleanPath(cPath)),
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s2.control-c-analysed", statusOf(harness, cPath) == 2,
              QStringLiteral("c=%1").arg(statusOf(harness, cPath)));
        check("rm17b.s2.control-covered", harness.controller->hasUpperLayer(cPath),
              QStringLiteral("covered=%1").arg(harness.controller->hasUpperLayer(cPath) ? 1 : 0));

        // The user now names the top (the module tree would publish this).
        publishScope(QStringLiteral("A"), analysed, excluded);
        check("rm17b.s2.shell-top", harness.shell->property("vedit.diagnostics.topScope").toString()
                                       == QStringLiteral("A"),
              harness.shell->property("vedit.diagnostics.topScope").toString());
        bool saysOutside = false;
        for (const QString &line : harness.shell->diagnostics()) {
            if (line.contains(QStringLiteral("1 file(s) outside the top A are not analysed"))) {
                saysOutside = true;
            }
        }
        check("rm17b.s2.aggregate-line", saysOutside,
              harness.shell->diagnostics().join(QStringLiteral(" | ")));
        check("rm17b.s2.excluded-is-hollow", statusOf(harness, cPath) == 4,
              QStringLiteral("c=%1 covered=%2")
                  .arg(statusOf(harness, cPath))
                  .arg(harness.controller->hasUpperLayer(cPath) ? 1 : 0));
        check("rm17b.s2.excluded-never-green", statusOf(harness, cPath) != 3,
              QStringLiteral("c=%1").arg(statusOf(harness, cPath)));

        // The lint really skips it from now on...
        check("rm17b.s2.next-pass-runs", runExplicitPass(harness));
        check("rm17b.s2.request-excludes-outside",
              !harness.fake->filesSeen.contains(QDir::cleanPath(cPath)),
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s2.request-analysed-only",
              harness.fake->filesSeen.size() == 2
                  && harness.fake->filesSeen.contains(QDir::cleanPath(topPath))
                  && harness.fake->filesSeen.contains(QDir::cleanPath(bPath)),
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s2.a-verdict", statusOf(harness, topPath) == 1,
              QStringLiteral("a=%1").arg(statusOf(harness, topPath)));
        check("rm17b.s2.b-clean", statusOf(harness, bPath) == 3,
              QStringLiteral("b=%1").arg(statusOf(harness, bPath)));
        std::printf("OBSERVED rm17b.s2.after-pass c-status=%d covered=%d\n",
                    statusOf(harness, cPath), harness.controller->hasUpperLayer(cPath) ? 1 : 0);
        check("rm17b.s2.still-hollow-after-pass", statusOf(harness, cPath) == 4,
              QStringLiteral("c=%1 covered=%2")
                  .arg(statusOf(harness, cPath))
                  .arg(harness.controller->hasUpperLayer(cPath) ? 1 : 0));

        // Return to Auto: C is analysed again (its real verdict returns) - the
        // other half of the contract, asserted independently of the defect above.
        publishScope(QString(), QStringList{}, QStringList{});
        check("rm17b.s2.auto-pass-runs", runExplicitPass(harness));
        check("rm17b.s2.auto-request-includes-c",
              harness.fake->filesSeen.contains(QDir::cleanPath(cPath)),
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s2.auto-c-analysed-again", statusOf(harness, cPath) == 2,
              QStringLiteral("c=%1").arg(statusOf(harness, cPath)));
    }

    // =====================================================================
    // Scenario 3 - DEFINITIONS-ONLY FILE (pure package). The writer's rule
    // (crates/vd-deeplint/src/catalog.rs: only an elaborated `InstanceBody`'s
    // `source_file` is reachable) puts a file that only declares a package
    // outside the reachable set. The consumer must treat it like C: hollow, and
    // absent from the lint request.
    // =====================================================================
    {
        const QString state3 = dir.filePath(QStringLiteral("state3"));
        check("rm17b.s3.package-fixture",
              writeFile(pkgPath, QStringLiteral("package P;\n  localparam int W = 1;\nendpackage\n"))
                  && writeFile(listPath,
                               QStringLiteral("top.sv\nb.sv\nc.sv\npkg.sv\n")));
        Harness harness = makeHarness(state3, root, /*fakeAvailable=*/false, {topPath, cPath});
        check("rm17b.s3.harness", harness.ok);
        if (!harness.ok) {
            std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
            return 1;
        }
        check("rm17b.s3.project-set",
              waitForProjectFiles(harness, {topPath, bPath, cPath, pkgPath}),
              QStringLiteral("files=%1").arg(harness.controller->projectFiles().size()));
        publishScope(QStringLiteral("A"), analysed, QStringList{cPath, pkgPath});
        check("rm17b.s3.aggregate-counts-package",
              harness.shell->property("vedit.diagnostics.topScopeExcluded").toInt() == 2,
              QString::number(harness.shell->property("vedit.diagnostics.topScopeExcluded").toInt()));
        bool saysOutside = false;
        for (const QString &line : harness.shell->diagnostics()) {
            if (line.contains(QStringLiteral("2 file(s) outside the top A are not analysed"))) {
                saysOutside = true;
            }
        }
        check("rm17b.s3.aggregate-line", saysOutside,
              harness.shell->diagnostics().join(QStringLiteral(" | ")));
        check("rm17b.s3.package-hollow", statusOf(harness, pkgPath) == 4,
              QStringLiteral("pkg=%1").arg(statusOf(harness, pkgPath)));
        harness.fake->reachable = true;
        check("rm17b.s3.pass-runs", runExplicitPass(harness));
        check("rm17b.s3.request-excludes-package",
              !harness.fake->filesSeen.contains(QDir::cleanPath(pkgPath)),
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s3.request-analysed-only",
              harness.fake->filesSeen.size() == 2,
              harness.fake->filesSeen.join(QLatin1Char(',')));
        check("rm17b.s3.a-verdict", statusOf(harness, topPath) == 1,
              QStringLiteral("a=%1").arg(statusOf(harness, topPath)));
        check("rm17b.s3.package-still-hollow", statusOf(harness, pkgPath) == 4,
              QStringLiteral("pkg=%1").arg(statusOf(harness, pkgPath)));
    }

    std::printf("VE-RM17B-TOP-SCOPE-E2E checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
