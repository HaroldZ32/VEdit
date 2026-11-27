// batch-20261115 lane B — realtime LINT depth (AC-3) and file-switch cost
// (AC-5.1/AC-5.2), driven through the PRODUCT wiring in the real shell.
//
// Contract (§5): AC-3.1 "the realtime analysis file set is the current file plus
// the definition files of the modules it DIRECTLY instantiates (one level)",
// AC-3.2 "the realtime surfaces show the current file's diagnostics only",
// AC-3.4 "the closure is bounded and a truncated set says so", AC-5.1 "switching
// between already-open documents starts no new verible LSP session" and AC-5.2
// "the per-switch UI-thread cost is p95 < 50 ms (offscreen, without waiting for
// LSP readiness)". A61.3 fixes the session set: the union of the open documents'
// level-1 closures, never the whole project.
//
// The module closure is the property the module-tree lane publishes
// (`vedit.moduleTree.closure`), so this test SETS that property instead of
// requiring slang: the seam under test is the diagnostics lane's consumption of
// it, not the catalog itself.
//
// KTextEditor is required (the annotations/tab seams are editor widgets); the
// test SKIPs with a printed reason when the build has no editor seam instead of
// pretending.
#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "diagnostics_panel.h"

#include "runtime_init.h"

#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "panel_registry.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QThread>
#include <QVariantMap>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skips = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
}

void skip(const char *name, const QString &reason)
{
    ++g_checks;
    ++g_skips;
    std::printf("CHECK %s SKIP (%s)\n", name, reason.toUtf8().constData());
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    return predicate();
}

/// p95 of `samples` (nearest-rank). The list is sorted in place.
qint64 p95(std::vector<qint64> &samples)
{
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const size_t rank = static_cast<size_t>((samples.size() * 95 + 99) / 100);
    const size_t index = std::min(samples.size() - 1, rank == 0 ? 0 : rank - 1);
    return samples[index];
}

} // namespace

int main(int argc, char **argv)
{
    // T134 / AGENT.md §4: installDiagnostics links the Rust service layer, whose
    // runtime startup does not run inside this C++ host. Must be first.
    vedit_runtime_init();
    QApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("switch.tempdir", false);
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    // A instantiates SUB; B is independent. The closure handed to the lane says
    // exactly that, which is what AC-3.5 asserts through the published set.
    const QString topPath = dir.filePath(QStringLiteral("top.sv"));
    const QString otherPath = dir.filePath(QStringLiteral("other.sv"));
    const QString subPath = dir.filePath(QStringLiteral("sub.sv"));
    // BOTH top.sv and sub.sv carry a syntax error: the active file's rows must
    // appear (proving the panel really analysed something) and the submodule's
    // must not (AC-3.2), so the assertion cannot pass by showing nothing.
    //
    // G1-a (batch-20261120): the mid-level group and the path-spelling group below
    // exercise the SESSION and closure machinery, and G1-a allows a session to
    // cover only files of the project file set. Their files are therefore written
    // HERE and named by the `sources.f` imported below - they are project members,
    // exactly as the product requires for a file to join the compile set. No
    // assertion of those groups is changed; each group still writes its own file
    // (idempotently) and still asserts the same facts.
    const bool fixture =
        writeFile(topPath, QStringLiteral("module top;\n  logic a b;\n  sub u_sub ();\nendmodule\n"))
        && writeFile(otherPath, QStringLiteral("module other;\nendmodule\n"))
        && writeFile(subPath, QStringLiteral("module sub;\n  logic c d;\nendmodule\n"))
        && writeFile(dir.filePath(QStringLiteral("leaf.sv")),
                     QStringLiteral("module leaf;\n  logic l m;\nendmodule\n"))
        && writeFile(dir.filePath(QStringLiteral("sibling.sv")),
                     QStringLiteral("module sibling;\n  logic s t;\nendmodule\n"))
        && QDir().mkpath(dir.filePath(QStringLiteral("canonical")))
        && writeFile(dir.filePath(QStringLiteral("canonical/parent.sv")),
                     QStringLiteral("module parent;\nendmodule\n"))
        && writeFile(dir.filePath(QStringLiteral("canonical/child.sv")),
                     QStringLiteral("module child;\nendmodule\n"));
    check("switch.fixture", fixture);
    if (!fixture) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }

    vedit::app::ShellOptions options;
    options.stateDir = dir.filePath(QStringLiteral("state"));
    options.restoreSession = false;
    options.showDefaultPanels = false;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    if (provider == nullptr) {
        skip("switch.editor-seam", QStringLiteral("this build has no editor seam"));
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 0;
    }
    std::unique_ptr<vedit::app::ApplicationShell> shell(
        new vedit::app::ApplicationShell(options));
    shell->editorArea()->setProvider(provider.get());
    shell->editorArea()->ensurePane();

    // B1 regression control: start in single-file mode (no imported filelist) so
    // the project poller does NOT adopt the fixture during the initial analysis.
    // The B1 scenario below flips `hasFilelist`/`filelists` later - exactly the
    // product path of importing a `.f` list - and asserts the session comes back.
    {
        QVariantMap projectInputs;
        projectInputs.insert(QStringLiteral("root"), dir.path());
        projectInputs.insert(QStringLiteral("hasFilelist"), false);
        projectInputs.insert(QStringLiteral("settingsPath"),
                             dir.filePath(QStringLiteral("state/settings.toml")));
        shell->setProperty("vedit.project.inputs", projectInputs);
    }

    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = dir.path();
    integration.stateDir = dir.filePath(QStringLiteral("state"));
    vedit::workspace::installDiagnostics(*shell, integration);
    shell->panels()->showPanel(QStringLiteral("outline"), shell.get());
    shell->resize(1200, 800);
    shell->show();
    QCoreApplication::processEvents();

    auto *controller = vedit::workspace::diagnosticsControllerForShell(*shell);
    auto *pane = shell->editorArea() != nullptr ? shell->editorArea()->activePane() : nullptr;
    check("switch.controller", controller != nullptr);
    check("switch.pane", pane != nullptr);
    if (controller == nullptr || pane == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }

    // ---- batch-20261116 L1.1: a PROJECT file set is not the realtime session --
    // The reported defect happened in exactly this order: the project file set
    // arrives (setFileSet) and only then the editor analyses the current file.
    // Under the old code the project set became `files_`, the session opened over
    // all of it and every tick requested diagnostics for the whole project.
    QStringList projectSet;
    for (int index = 0; index < 400; ++index) {
        projectSet.append(dir.filePath(
            QStringLiteral("proj/file%1.sv").arg(index, 3, 10, QLatin1Char('0'))));
    }
    projectSet << topPath << otherPath << subPath;
    const quint64 startsBeforeProjectSet = controller->lspStartCount();
    controller->setFileSet(projectSet);
    check("switch.project-set-recorded", controller->projectFiles().size() == projectSet.size(),
          QString::number(controller->projectFiles().size()));
    check("switch.project-set-starts-no-session",
          controller->lspStartCount() == startsBeforeProjectSet,
          QStringLiteral("before=%1 after=%2")
              .arg(static_cast<qulonglong>(startsBeforeProjectSet))
              .arg(static_cast<qulonglong>(controller->lspStartCount())));
    check("switch.project-set-issues-no-request",
          controller->lastDiagnosticsRequestFiles().isEmpty()
              && controller->diagnosticsRequestCount() == 0,
          QString::number(controller->diagnosticsRequestCount()));

    // The module-tree lane's published closure: top.sv directly instantiates a
    // module defined in sub.sv; other.sv has no children. AC-3.1's one level.
    QVariantMap closure;
    closure.insert(topPath, QStringList{subPath});
    closure.insert(otherPath, QStringList{});
    closure.insert(subPath, QStringList{});
    shell->setProperty("vedit.moduleTree.closure", closure);

    QString openError;
    check("switch.open-top", pane->openFile(topPath, &openError), openError);
    check("switch.open-other", pane->openFile(otherPath, &openError), openError);
    check("switch.open-sub", pane->openFile(subPath, &openError), openError);
    check("switch.tabs", pane->tabCount() == 3, QString::number(pane->tabCount()));

    // ---- AC-3.1 / AC-3.5: the analysis set of the active file --------------
    pane->setActiveTab(pane->tabIndexForPath(topPath));
    QCoreApplication::processEvents();
    const bool published = waitUntil(
        [&shell, &subPath]() {
            return shell->property("vedit.diagnostics.realtimeFileSet")
                       .toStringList()
                       .contains(subPath);
        },
        8000);
    const QStringList realtimeSet =
        shell->property("vedit.diagnostics.realtimeFileSet").toStringList();
    check("switch.realtime-set-has-submodule", published,
          QStringLiteral("set=%1").arg(realtimeSet.join(QLatin1Char(','))));
    check("switch.realtime-set-has-current", realtimeSet.contains(topPath),
          realtimeSet.join(QLatin1Char(',')));
    check("switch.realtime-set-is-one-level", !realtimeSet.contains(otherPath),
          realtimeSet.join(QLatin1Char(',')));

    // ---- A61.3: the session set is the UNION of the open closures ----------
    const QStringList sessionSet =
        shell->property("vedit.diagnostics.sessionFileSet").toStringList();
    check("switch.session-set-is-union",
          sessionSet.contains(topPath) && sessionSet.contains(otherPath)
              && sessionSet.contains(subPath),
          QStringLiteral("session=%1").arg(sessionSet.join(QLatin1Char(','))));
    check("switch.session-set-matches-controller",
          controller->realtimeSessionFiles() == sessionSet,
          QStringLiteral("controller=%1")
              .arg(controller->realtimeSessionFiles().join(QLatin1Char(','))));

    // ---- L1.1: the diagnostics request set is the realtime set --------------
    // Wait (bounded) for the first batch; assert every requested file belongs to
    // the realtime session and that the 400-file project set never appears.
    const bool requestLanded = waitUntil(
        [controller]() { return !controller->lastDiagnosticsRequestFiles().isEmpty(); }, 15000);
    const QStringList requestSet = controller->lastDiagnosticsRequestFiles();
    bool requestOutsideSession = false;
    for (const QString &file : requestSet) {
        if (!controller->realtimeSessionFiles().contains(file)) {
            requestOutsideSession = true;
            std::printf("OBSERVED switch.request-outside-session file='%s'\n", qPrintable(file));
        }
    }
    check("switch.diagnostics-request-set-bounded",
          requestLanded && !requestOutsideSession
              && requestSet.size() <= controller->realtimeSessionFiles().size(),
          QStringLiteral("requested=%1 session=%2")
              .arg(requestSet.size())
              .arg(controller->realtimeSessionFiles().size()));
    check("switch.diagnostics-request-set-not-the-project-set",
          requestSet.size() <= 128 && requestSet.size() < projectSet.size(),
          QStringLiteral("requested=%1 project=%2").arg(requestSet.size()).arg(projectSet.size()));
    // Drop the synthetic project set again: the B1 scene below imports a REAL
    // `.f` list, and its "adoption arrived" check must not be satisfied by the
    // 400 synthetic paths this case injected.
    controller->setFileSet(QStringList{});

    // ---- AC-5.1: switching between OPEN documents starts no new session ----
    const quint64 startsAfterFirst = controller->lspStartCount();
    const quint64 stopsAfterFirst = controller->lspStopCount();
    check("switch.first-session-single", startsAfterFirst <= 1,
          QStringLiteral("starts=%1").arg(static_cast<qulonglong>(startsAfterFirst)));

    std::vector<qint64> samples;
    samples.reserve(10);
    int topIndex = pane->tabIndexForPath(topPath);
    int otherIndex = pane->tabIndexForPath(otherPath);
    for (int round = 0; round < 10; ++round) {
        const int index = (round % 2 == 0) ? otherIndex : topIndex;
        const QString path = (round % 2 == 0) ? otherPath : topPath;
        QElapsedTimer timer;
        timer.start();
        pane->setActiveTab(index);
        // The product's own switch cost: the analysis the watcher runs for the
        // newly active buffer (no LSP-ready wait is included - `analyzeFile` is
        // synchronous only up to starting/refreshing the session).
        controller->analyzeFile(path, path == topPath
                                            ? QStringLiteral("module top;\n  logic a b;\n  sub u_sub ();\nendmodule\n")
                                            : QStringLiteral("module other;\nendmodule\n"),
                                false);
        controller->refresh();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        samples.push_back(timer.elapsed());
    }
    const qint64 switchP95 = p95(samples);
    std::printf("OBSERVED switch.p95 ms=%lld samples=%d\n",
                static_cast<long long>(switchP95), static_cast<int>(samples.size()));
    check("switch.p95-under-50ms", switchP95 < 50,
          QStringLiteral("p95=%1ms").arg(switchP95));
    check("switch.no-new-session", controller->lspStartCount() == startsAfterFirst,
          QStringLiteral("before=%1 after=%2")
              .arg(static_cast<qulonglong>(startsAfterFirst))
              .arg(static_cast<qulonglong>(controller->lspStartCount())));
    check("switch.no-session-stop", controller->lspStopCount() == stopsAfterFirst,
          QStringLiteral("before=%1 after=%2")
              .arg(static_cast<qulonglong>(stopsAfterFirst))
              .arg(static_cast<qulonglong>(controller->lspStopCount())));

    // ---- AC-3.2: the realtime view shows the ACTIVE file only --------------
    // The panel is the Outline list surface. Its rows are grouped in a
    // QTreeWidget, and each row's tooltip carries the absolute file, so a leak
    // from a submodule file is visible without depending on the toolchain having
    // produced any row at all.
    // A dock's content is built lazily and a closed/re-inserted dock leaves an
    // orphan panel behind, so read the LIVE one: the instance that actually
    // polled (highest rebuild count), not whichever came first in the tree.
    vedit::workspace::diagnostics::DiagnosticsPanel *panel = nullptr;
    for (auto *candidate :
         shell->findChildren<vedit::workspace::diagnostics::DiagnosticsPanel *>()) {
        if (panel == nullptr || candidate->rebuildCount() > panel->rebuildCount()) {
            panel = candidate;
        }
    }
    check("switch.display-panel", panel != nullptr);
    if (panel != nullptr) {
        auto *issues = qobject_cast<QTreeWidget *>(panel->issuesWidget());
        check("switch.display-issues-widget", issues != nullptr);
        const auto collect = [issues]() {
            QStringList tooltips;
            if (issues == nullptr) {
                return tooltips;
            }
            for (int group = 0; group < issues->topLevelItemCount(); ++group) {
                QTreeWidgetItem *header = issues->topLevelItem(group);
                for (int row = 0; row < header->childCount(); ++row) {
                    tooltips.append(header->child(row)->toolTip(0));
                }
            }
            return tooltips;
        };
        // The panel rebuilds on its own 300 ms timer, so wait for the active
        // file's own row to LAND before judging what else is there.
        waitUntil([&collect, &topPath]() {
            for (const QString &tip : collect()) {
                if (tip.startsWith(topPath + QLatin1Char(':'))) {
                    return true;
                }
            }
            return false;
        }, 8000);
        const QStringList tooltips = collect();
        // The dock's panel is built lazily and is NOT polled in this offscreen
        // harness (rebuildCount stays at its construction value), so the panel
        // rows are recorded as an OBSERVATION and the ASSERTION reads the exact
        // view the panel is built from: `lowerRowsForFile(displayFile)` - the
        // realtime layer of the active document only (AC-3.2 as AC-4.3 splits it).
        const QVector<vedit::workspace::diagnostics::DiagnosticEntry> realtimeRows =
            controller->lowerRowsForFile(controller->realtimeDisplayFile());
        bool onlyRealtimeFile = !realtimeRows.isEmpty();
        for (const auto &entry : realtimeRows) {
            if (entry.file != topPath) {
                onlyRealtimeFile = false;
                std::printf("OBSERVED switch.realtime-leak file='%s'\n", qPrintable(entry.file));
            }
        }
        check("switch.realtime-rows-current-file-only", onlyRealtimeFile,
              QStringLiteral("rows=%1 display=%2")
                  .arg(realtimeRows.size())
                  .arg(controller->realtimeDisplayFile()));
        // L1.2: the editor's squiggle/gutter bridge reads exactly this call
        // (AnnotationBridge -> mergedDiagnosticsForFile(active path)); proving it
        // non-empty is the editor half of "the current file's diagnostics appear".
        check("switch.editor-annotation-source",
              !controller->mergedDiagnosticsForFile(topPath).isEmpty(),
              QStringLiteral("merged=%1")
                  .arg(controller->mergedDiagnosticsForFile(topPath).size()));
        std::printf("OBSERVED switch.display-probe file='%s' for-file=%d merged=%d panel-rows=%d "
                    "rebuilds=%d pre-settle-rows=%d active-tab='%s'\n",
                    qPrintable(controller->realtimeDisplayFile()),
                    static_cast<int>(controller->mergedDiagnosticsForFile(topPath).size()),
                    static_cast<int>(controller->mergedDiagnostics().size()),
                    panel->issueRowCount(), panel->rebuildCount(),
                    static_cast<int>(tooltips.size()),
                    qPrintable(pane->currentFilePath()));
        // F3 (Review B, High): the DISPLAY surface is what the user sees, so the
        // assertion is on the widget. Wait (bounded) for the panel to settle -
        // its row count must equal the realtime rows of the active document -
        // and then require every row to belong to the current file.
        const int realtimeCount = static_cast<int>(realtimeRows.size());
        const bool panelSettled = waitUntil(
            [panel, realtimeCount]() { return panel->issueRowCount() == realtimeCount; }, 8000);
        check("switch.display-panel-settles", panelSettled,
              QStringLiteral("panel=%1 realtime=%2")
                  .arg(panel->issueRowCount())
                  .arg(realtimeCount));
        // R2 (Review B, Nit): judge the rows the widget holds AFTER it settled.
        // The tooltips collected before the settle are a possibly stale frame -
        // with the panel already correct they agree, but a leak that is fixed one
        // poll later would have slipped through the pre-settle read.
        const QStringList settledTooltips = collect();
        bool onlyActiveFile = !settledTooltips.isEmpty();
        for (const QString &tip : settledTooltips) {
            if (!tip.startsWith(topPath + QLatin1Char(':'))) {
                onlyActiveFile = false;
                std::printf("OBSERVED switch.display-leak row='%s'\n", qPrintable(tip));
            }
        }
        // Exactly the active document's realtime rows: the count alone could be
        // satisfied by a stale frame, and the file prefix alone by an empty list.
        check("switch.display-widget-rows-current-file",
              onlyActiveFile && settledTooltips.size() == realtimeCount,
              QStringLiteral("rows=%1 realtime=%2 display=%3")
                  .arg(settledTooltips.size())
                  .arg(realtimeCount)
                  .arg(controller->realtimeDisplayFile()));
    }

    // ---- AC-3.4: the closure is bounded and the bound is published ---------
    const QVariantMap sets = shell->property("vedit.diagnostics.realtimeSets").toMap();
    check("switch.closure-cap-published",
          sets.value(QStringLiteral("closureCap")).toInt() == 64,
          QString::number(sets.value(QStringLiteral("closureCap")).toInt()));
    check("switch.session-cap-published",
          sets.value(QStringLiteral("sessionCap")).toInt() == 128,
          QString::number(sets.value(QStringLiteral("sessionCap")).toInt()));

    // AC-3.1 on the REAL catalog path: the closure above was injected through
    // the published property, so this case proves the CONSUMER only. The
    // producer side lives where the catalog does - never here - and the SKIP
    // names the case that owns it instead of implying this one covers it:
    //   * `vedit-module-closure-e2e` installs project + module-tree + diagnostics
    //     and asserts the closure a real slang catalog publishes;
    //   * `vedit-module-tree-selftest` (checkModuleClosure) covers the synthetic
    //     one-level rule with no tool.
    // Without a built slang NEITHER of those observes the real path.
    if (qEnvironmentVariableIsEmpty("VEDIT_SLANG")) {
        skip("switch.real-catalog-closure",
             QStringLiteral("no VEDIT_SLANG in this environment: the real catalog -> closure "
                            "path is NOT observed in this run (it would be covered by "
                            "vedit-module-closure-e2e with a built slang); this case proves only "
                            "that the lane consumes the injected map, and the synthetic rule is "
                            "covered by vedit-module-tree-selftest (checkModuleClosure)"));
    } else {
        skip("switch.real-catalog-closure",
             QStringLiteral("this lane installs only diagnostics, so it has no module-tree "
                            "holder and cannot produce a catalog closure; the REAL path is "
                            "asserted by vedit-module-closure-e2e (project + module-tree lanes, "
                            "real slang) and the synthetic rule by vedit-module-tree-selftest "
                            "(checkModuleClosure)"));
    }

    // =====================================================================
    // batch-20261116 B1 (Reviewer Blocker): a project adoption and a settings
    // apply MUST restart the realtime session. Both real paths stop the LSP
    // server through FfiLspSessionBackend::selectBackend() (unconditional
    // stop()) while `setFileSet()` deliberately starts nothing; without an
    // explicit restart the controller reuses a dead session and the realtime
    // LINT stays off until the app restarts. This block drives the PRODUCT
    // wiring twice and then proves a continued edit still produces diagnostics.
    // =====================================================================
    const auto errorRows = [controller, &topPath]() {
        int errors = 0;
        for (const auto &entry : controller->lowerRowsForFile(topPath)) {
            if (entry.severity == 1) {
                ++errors;
            }
        }
        return errors;
    };
    const auto setActiveText = [pane](const QString &text) {
        auto *view = dynamic_cast<KTextEditor::View *>(pane->editorWidget());
        if (view == nullptr || view->document() == nullptr) {
            return false;
        }
        view->document()->setText(text);
        QCoreApplication::processEvents();
        return true;
    };

    check("restart.baseline-ready",
          waitUntil([controller]() {
              return controller->session().state
                  == vedit::workspace::diagnostics::BackendState::Ready;
          }, 15000),
          controller->session().message);

    // A clean buffer first, so the post-adoption error is NEW, not the fixture's.
    const quint64 analysesBeforeClean = controller->analysisCount();
    check("restart.clean-edit", setActiveText(QStringLiteral(
              "module top;\n  sub u_sub ();\nendmodule\n")));
    // Not a vacuous pass: the dirty-buffer path clears the old rows at once, so
    // the wait requires a NEW analysis to have run AND to have settled clean.
    check("restart.clean-reaches-backend",
          waitUntil([controller, &errorRows, analysesBeforeClean]() {
              return controller->analysisCount() > analysesBeforeClean
                  && controller->state() != vedit::workspace::diagnostics::BackendState::Reparsing
                  && errorRows() == 0;
          }, 20000),
          QStringLiteral("analyses=%1 state=%2 errors=%3")
              .arg(static_cast<qulonglong>(controller->analysisCount()))
              .arg(static_cast<int>(controller->state()))
              .arg(errorRows()));

    // ---- (1) PROJECT ADOPTION (import a `.f` list; re-pins -I/-D) ----------
    const QString sourcesPath = dir.filePath(QStringLiteral("sources.f"));
    check("restart.sources-written",
          writeFile(sourcesPath,
                    QStringLiteral("top.sv\nother.sv\nsub.sv\nleaf.sv\nsibling.sv\n"
                                   "canonical/parent.sv\ncanonical/child.sv\n")));
    const quint64 startsBeforeAdoption = controller->lspStartCount();
    QVariantMap adoptedInputs;
    adoptedInputs.insert(QStringLiteral("root"), dir.path());
    adoptedInputs.insert(QStringLiteral("hasFilelist"), true);
    adoptedInputs.insert(QStringLiteral("filelists"), QStringList{sourcesPath});
    adoptedInputs.insert(QStringLiteral("settingsPath"),
                         dir.filePath(QStringLiteral("state/settings.toml")));
    shell->setProperty("vedit.project.inputs", adoptedInputs);
    const auto adoptedProject = [controller, &topPath, &otherPath, &subPath]() {
        const QStringList files = controller->projectFiles();
        return files.contains(topPath) && files.contains(otherPath) && files.contains(subPath)
            && files.size() <= 8; // the fixture, not the synthetic 400-file set
    };
    const bool adoptionArrived = waitUntil(adoptedProject, 20000);
    const QStringList adoptedFiles = controller->projectFiles();
    check("restart.adoption-arrived", adoptionArrived,
          QStringLiteral("projectFiles=%1").arg(adoptedFiles.size()));
    const bool adoptionRestarted = waitUntil([controller, startsBeforeAdoption]() {
        return controller->session().state
                   == vedit::workspace::diagnostics::BackendState::Ready
            && controller->lspStartCount() > startsBeforeAdoption;
    }, 20000);
    check("restart.adoption-started-session", adoptionRestarted,
          QStringLiteral("starts=%1 -> %2 state=%3")
              .arg(static_cast<qulonglong>(startsBeforeAdoption))
              .arg(static_cast<qulonglong>(controller->lspStartCount()))
              .arg(static_cast<int>(controller->session().state)));
    const QStringList adoptionRequests = controller->lastDiagnosticsRequestFiles();
    bool adoptionRequestsBounded = !adoptionRequests.isEmpty();
    for (const QString &file : adoptionRequests) {
        if (!controller->realtimeSessionFiles().contains(file)) {
            adoptionRequestsBounded = false;
        }
    }
    check("restart.adoption-request-set-realtime", adoptionRequestsBounded,
          QStringLiteral("requested=%1 realtime=%2")
              .arg(adoptionRequests.size())
              .arg(controller->realtimeSessionFiles().size()));

    // Continue editing AFTER the adoption: the fresh session must answer.
    check("restart.adoption-edit", setActiveText(QStringLiteral(
              "module top;\n  logic a b;\n  sub u_sub ();\nendmodule\n")));
    const bool adoptionDiagnostic =
        waitUntil([&errorRows]() { return errorRows() > 0; }, 20000);
    check("restart.adoption-new-diagnostic", adoptionDiagnostic,
          QStringLiteral("errors=%1").arg(errorRows()));

    // ---- (2) SETTINGS APPLY (state_.apply -> selectBackend stop) ------------
    const QString settingsPath = dir.filePath(QStringLiteral("state/settings.toml"));
    QDir().mkpath(dir.filePath(QStringLiteral("state")));
    const quint64 startsBeforeSettings = controller->lspStartCount();
    check("restart.settings-written",
          writeFile(settingsPath, QStringLiteral("\"tools.lsp_backend\" = \"verible\"\n")));
    const bool settingsApplied = waitUntil([&shell]() {
        const QVariantMap switchState =
            shell->property("vedit.diagnostics.switch").toMap();
        return switchState.value(QStringLiteral("ok")).toBool();
    }, 20000);
    check("restart.settings-applied", settingsApplied);
    const bool settingsRestarted = waitUntil([controller, startsBeforeSettings]() {
        return controller->session().state
                   == vedit::workspace::diagnostics::BackendState::Ready
            && controller->lspStartCount() > startsBeforeSettings;
    }, 20000);
    check("restart.settings-started-session", settingsRestarted,
          QStringLiteral("starts=%1 -> %2 state=%3")
              .arg(static_cast<qulonglong>(startsBeforeSettings))
              .arg(static_cast<qulonglong>(controller->lspStartCount()))
              .arg(static_cast<int>(controller->session().state)));

    // The restartAnalysis_ path re-analysed the CURRENT buffer; then one more
    // edit must land a NEW diagnostic (a different token, so the check cannot
    // pass on a stale row).
    check("restart.settings-edit", setActiveText(QStringLiteral(
              "module top;\n  logic c d;\n  sub u_sub ();\nendmodule\n")));
    const bool newDiagnostic = waitUntil(
        [controller, &topPath]() {
            for (const auto &entry : controller->lowerRowsForFile(topPath)) {
                if (entry.severity == 1 && entry.message.contains(QStringLiteral("\"d\""))) {
                    return true;
                }
            }
            return false;
        },
        20000);
    QStringList errorMessages;
    for (const auto &entry : controller->lowerRowsForFile(topPath)) {
        if (entry.severity == 1) {
            errorMessages.append(entry.message);
        }
    }
    check("restart.settings-new-diagnostic", newDiagnostic,
          errorMessages.join(QStringLiteral(" | ")));

    // ---- AC-5 (batch-20261118): the REQUEST scope is the ACTIVE closure -----
    // The user's defect: with a TOP tab open, the server was asked for
    // "TOP + its level" instead of the active file's one-level closure. Here the
    // active file is a MID-level module (sub instantiates a module defined in
    // leaf), which the old e2e never activated.
    {
        const QString leafPath = dir.filePath(QStringLiteral("leaf.sv"));
        const QString siblingPath = dir.filePath(QStringLiteral("sibling.sv"));
        check("midlevel.fixture",
              writeFile(leafPath, QStringLiteral("module leaf;\n  logic l m;\nendmodule\n"))
                  && writeFile(siblingPath,
                               QStringLiteral("module sibling;\n  logic s t;\nendmodule\n")));
        check("midlevel.open-leaf", pane->openFile(leafPath, &openError), openError);
        // TOP has TWO children: sub (activated below) and sibling. sibling is
        // therefore inside TOP's one-level closure and the session union, but it
        // must never enter the request/analysis set of the active file. The old
        // fixture could not detect that regression: its `other.sv` was an
        // unrelated tab, not a sibling of TOP.
        QVariantMap midClosure = shell->property("vedit.moduleTree.closure").toMap();
        midClosure.insert(topPath, QStringList{subPath, siblingPath});
        midClosure.insert(subPath, QStringList{leafPath});
        shell->setProperty("vedit.moduleTree.closure", midClosure);
        pane->setActiveTab(pane->tabIndexForPath(subPath));
        QCoreApplication::processEvents();
        const bool midPublished = waitUntil(
            [&shell, &leafPath]() {
                return shell->property("vedit.diagnostics.realtimeFileSet")
                    .toStringList()
                    .contains(leafPath);
            },
            8000);
        const QStringList midSet =
            shell->property("vedit.diagnostics.realtimeFileSet").toStringList();
        check("midlevel.realtime-set-has-leaf", midPublished,
              QStringLiteral("set=%1").arg(midSet.join(QLatin1Char(','))));
        check("midlevel.realtime-set-has-active", midSet.contains(subPath),
              midSet.join(QLatin1Char(',')));
        QStringList expectedMidSet{subPath, leafPath};
        expectedMidSet.sort();
        QStringList sortedMidSet = midSet;
        sortedMidSet.sort();
        check("midlevel.realtime-set-exact", sortedMidSet == expectedMidSet,
              midSet.join(QLatin1Char(',')));
        check("midlevel.realtime-set-excludes-top", !midSet.contains(topPath),
              midSet.join(QLatin1Char(',')));
        check("midlevel.realtime-set-excludes-unrelated",
              !midSet.contains(otherPath), midSet.join(QLatin1Char(',')));
        check("midlevel.realtime-set-excludes-sibling",
              !midSet.contains(siblingPath), midSet.join(QLatin1Char(',')));
        // Positive control: sibling IS known to the session (it is in TOP's
        // open-tab closure), so the negative checks above are not vacuous.
        check("midlevel.session-covers-tops-other-child",
              controller->realtimeSessionFiles().contains(siblingPath),
              QStringLiteral("session=%1")
                  .arg(controller->realtimeSessionFiles().join(QLatin1Char(','))));
        const bool midRequested = waitUntil(
            [controller, &leafPath]() {
                return controller->lastDiagnosticsRequestFiles().contains(leafPath);
            },
            15000);
        const QStringList midRequestSet = controller->lastDiagnosticsRequestFiles();
        QStringList sortedRequestSet = midRequestSet;
        sortedRequestSet.sort();
        check("midlevel.request-set-exact", sortedRequestSet == expectedMidSet,
              midRequestSet.join(QLatin1Char(',')));
        check("midlevel.request-set-has-leaf", midRequested,
              QStringLiteral("requested=%1")
                  .arg(midRequestSet.join(QLatin1Char(','))));
        check("midlevel.request-set-scoped-to-the-active-closure",
              !midRequestSet.contains(topPath) && !midRequestSet.contains(otherPath)
                  && !midRequestSet.contains(siblingPath),
              QStringLiteral("requested=%1")
                  .arg(midRequestSet.join(QLatin1Char(','))));
        check("midlevel.request-set-excludes-sibling",
              !midRequestSet.contains(siblingPath),
              QStringLiteral("requested=%1")
                  .arg(midRequestSet.join(QLatin1Char(','))));
        // The SESSION may still cover the union open documents (reuse); only
        // the request/analysis scope is the active closure.
        check("midlevel.session-keeps-the-open-documents",
              controller->realtimeSessionFiles().contains(topPath)
                  && controller->realtimeSessionFiles().contains(otherPath),
              QStringLiteral("session=%1")
                  .arg(controller->realtimeSessionFiles().join(QLatin1Char(','))));
    }

    // ---- AC-5 path-spelling fallback (canonicalDirectoryVariant) ------------
    // A project opened through a symlinked parent makes the editor and the
    // module catalog disagree on the prefix only; the closure lookup must fall
    // back to the canonical directory spelling instead of degrading to a single
    // file.
    {
        const QString realDir = dir.filePath(QStringLiteral("canonical"));
        QDir().mkpath(realDir);
        const QString realParent = realDir + QStringLiteral("/parent.sv");
        const QString realChild = realDir + QStringLiteral("/child.sv");
        check("pathspelling.fixture",
              writeFile(realParent, QStringLiteral("module parent;\nendmodule\n"))
                  && writeFile(realChild, QStringLiteral("module child;\nendmodule\n")));
        const QString linkedDir = dir.filePath(QStringLiteral("linked"));
        check("pathspelling.symlink", QFile::link(realDir, linkedDir));
        const QString linkedParent = linkedDir + QStringLiteral("/parent.sv");
        QVariantMap spellingClosure = shell->property("vedit.moduleTree.closure").toMap();
        // The catalog's spelling is the canonical one; the editor opens the
        // symlinked spelling.
        spellingClosure.insert(realParent, QStringList{realChild});
        shell->setProperty("vedit.moduleTree.closure", spellingClosure);
        check("pathspelling.open", pane->openFile(linkedParent, &openError), openError);
        pane->setActiveTab(pane->tabIndexForPath(linkedParent));
        QCoreApplication::processEvents();
        const bool sawChild = waitUntil(
            [&shell, &realChild]() {
                return shell->property("vedit.diagnostics.realtimeFileSet")
                    .toStringList()
                    .contains(realChild);
            },
            8000);
        const QStringList spellingSet =
            shell->property("vedit.diagnostics.realtimeFileSet").toStringList();
        check("pathspelling.canonical-fallback", sawChild,
              QStringLiteral("set=%1").arg(spellingSet.join(QLatin1Char(','))));
    }

    std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
