// batch-20261115 lane B (T14 repair) — the REAL catalog -> closure path and the
// project-gate counters, driven through the PRODUCT wiring in one shell.
//
// Contract (§5), each group named by the finding it closes:
//   AC-3.1  (F1): `vedit.moduleTree.closure` produced by a REAL slang catalog
//            says top.sv -> mid.sv and NEVER the grandchild (one level only).
//   AC-2.2  (F4): importing a filelist runs EXACTLY ONE catalog (and no
//            whole-project lint pass), counted on the module-tree holder.
//   AC-2.3b (F5): setting a top re-runs the catalog with `--top` (the answered
//            catalog reports that top and the tree is re-rooted), and choosing
//            the SAME top again re-runs it too (never a stateChanged-only no-op).
//   AC-2.1/2.5 (F8): with a folder but NO imported filelist the top control is
//            disabled, the panel says why, no catalog runs, and the diagnostics
//            file-set poller builds no project file set.
//   AC-7.3  (F9): an unknown top falls back to Auto and the summary line SAYS SO.
//
// The three lanes share `vedit.project.inputs`, so they are installed together
// (project -> module tree -> diagnostics), exactly like src/workspace/integration.cpp.
// The catalog is the real Rust service spawning the real `slang`; without a built
// slang the real-catalog groups SKIP with the honest reason (see the SKIP text),
// while the filelist-gate group still runs (it needs no tool).
#include "catalog_client.h"
#include "module_catalog.h"
#include "module_hierarchy.h"
#include "module_tree_integration.h"
#include "module_tree_panel.h"

#include "project_integration.h"
#include "project_model.h"

#include "diagnostics_controller.h"
#include "diagnostics_integration.h"

#include "runtime_init.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "panel_registry.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantMap>

#include <cstdio>
#include <functional>
#include <memory>

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
    std::printf("CHECK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : "  ", detail.isEmpty() ? "" : detail.toUtf8().constData());
    std::fflush(stdout);
}

void skip(const char *name, const QString &why)
{
    ++g_checks;
    ++g_skips;
    std::printf("CHECK %s SKIP (%s)\n", name, why.toUtf8().constData());
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
    QCoreApplication::processEvents();
    return done();
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    return file.write(text.toUtf8()) == text.toUtf8().size();
}

QString oneLine(const QString &text)
{
    QString copy = text;
    copy.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return copy.simplified();
}

/// Choose a top the way a user does: type it into the panel's editable combo and
/// finish the edit (the panel's ONE change handler then calls the holder).
void chooseTop(QComboBox *combo, const QString &name)
{
    if (combo == nullptr) {
        return;
    }
    combo->setEditText(name);
    if (combo->lineEdit() != nullptr) {
        emit combo->lineEdit()->returnPressed();
    }
    QCoreApplication::processEvents();
}

QStringList stringListOf(const QVariant &value)
{
    return value.toStringList();
}

QStringList rootsOf(const vedit::workspace::module_tree::ModuleTreePanel *panel)
{
    QStringList names;
    if (panel == nullptr) {
        return names;
    }
    for (const auto &root : panel->roots()) {
        names.append(root.moduleName);
    }
    return names;
}

} // namespace

int main(int argc, char **argv)
{
    // T134 / AGENT.md §4: installDiagnostics links the Rust service layer, whose
    // runtime startup does not run inside this C++ host. Must be first.
    vedit_runtime_init();
    QApplication app(argc, argv);

    const QString slang = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const bool haveSlang = !slang.isEmpty() && QFileInfo(slang).isExecutable();
    if (haveSlang) {
        qputenv("VEDIT_SLANG", slang.toLocal8Bit());
    }
    // The real-catalog groups need a slang child; the filelist gate below does
    // not. The SKIP text says exactly which coverage is missing, never that the
    // injected-property path (realtime-depth-switch) or the tree-root selftest
    // has covered it - neither observes a real catalog -> closure.
    const QString noSlangReason = QStringLiteral(
        "no built slang (argv='%1'): the real catalog->closure content is NOT observed in this "
        "run; the synthetic closure rule is covered by vedit-module-tree-selftest "
        "(checkModuleClosure) and vedit-module-tree-catalog-test covers real catalog parsing "
        "but NOT the closure map")
                                      .arg(slang);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("gate.tempdir", false);
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    const QString project = dir.filePath(QStringLiteral("project"));
    QDir().mkpath(project);
    // top -> mid -> leaf. The closure must answer ONE level: top.sv -> mid.sv and
    // never leaf.sv, while mid.sv itself -> leaf.sv (so the negative check cannot
    // pass by publishing an empty map).
    const QString topPath = QDir(project).absoluteFilePath(QStringLiteral("top.sv"));
    const QString midPath = QDir(project).absoluteFilePath(QStringLiteral("mid.sv"));
    const QString leafPath = QDir(project).absoluteFilePath(QStringLiteral("leaf.sv"));
    // G1-a: a file NO `.f` names, defining the SAME module as mid.sv - the real
    // user's "已确认没有任何文件 include 它，它只出现在一个没人引用的 .f 里".
    // Opening it must never put it into the realtime compile set.
    const QString outsidePath = QDir(project).absoluteFilePath(QStringLiteral("outside.sv"));
    const QString sourcesPath = QDir(project).absoluteFilePath(QStringLiteral("sources.f"));
    const bool fixture =
        writeFile(topPath, QStringLiteral("module closure_top;\n  closure_mid u_mid ();\nendmodule\n"))
        && writeFile(midPath,
                     QStringLiteral("module closure_mid;\n  closure_leaf u_leaf ();\nendmodule\n"))
        && writeFile(leafPath, QStringLiteral("module closure_leaf;\nendmodule\n"))
        && writeFile(outsidePath,
                     QStringLiteral("module closure_mid;\n  logic b1 b2;\nendmodule\n"))
        && writeFile(sourcesPath, QStringLiteral("top.sv\nmid.sv\nleaf.sv\n"));
    check("gate.fixture", fixture);
    if (!fixture) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }

    vedit::app::ShellOptions options;
    options.stateDir = dir.filePath(QStringLiteral("state"));
    options.projectRoot = project;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    std::unique_ptr<vedit::app::ApplicationShell> shell(
        new vedit::app::ApplicationShell(options));
    if (provider != nullptr) {
        shell->editorArea()->setProvider(provider.get());
    }
    shell->editorArea()->ensurePane();

    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = project;
    integration.stateDir = dir.filePath(QStringLiteral("state"));
    // The product's own order (src/workspace/integration.cpp).
    vedit::workspace::installProject(*shell, integration);
    vedit::workspace::installModuleTree(*shell, integration);
    vedit::workspace::installDiagnostics(*shell, integration);
    shell->resize(1280, 860);
    shell->show();
    QCoreApplication::processEvents();

    auto *treePanel = shell->findChild<vedit::workspace::module_tree::ModuleTreePanel *>();
    QObject *holder = shell->findChild<QObject *>(QStringLiteral("module-tree-holder"));
    auto *model = vedit::workspace::projectModelForShell(*shell);
    auto *client = shell->findChild<vedit::workspace::module_tree::CatalogClient *>();
    auto *controller = vedit::workspace::diagnosticsControllerForShell(*shell);
    check("gate.panel", treePanel != nullptr);
    check("gate.holder", holder != nullptr);
    check("gate.project-model", model != nullptr);
    check("gate.factory", client != nullptr);
    check("gate.diagnostics-controller", controller != nullptr);
    if (treePanel == nullptr || holder == nullptr || model == nullptr || client == nullptr
        || controller == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    auto *topCombo = treePanel->findChild<QComboBox *>(QStringLiteral("module-tree-top"));
    check("gate.top-control", topCombo != nullptr);

    // ---------------------------------------------------------------------
    // F8 (AC-2.1/AC-2.5): a folder WITHOUT an imported filelist is not a project
    // ---------------------------------------------------------------------
    // Let the install-time singleShot refresh and the diagnostics poller settle.
    waitUntil([&shell]() {
        return shell->property("vedit.diagnostics.projectFiles").isValid();
    }, 5000);
    QThread::msleep(300);
    QCoreApplication::processEvents();
    const int gateRuns = holder->property("catalogRuns").toInt();
    check("gate.no-filelist-no-catalog", gateRuns == 0,
          QStringLiteral("catalogRuns=%1").arg(gateRuns));
    check("gate.project-gate-closed", holder->property("projectGate").toBool() == false,
          QStringLiteral("projectGate=%1").arg(holder->property("projectGate").toBool()));
    check("gate.top-disabled", topCombo != nullptr && !topCombo->isEnabled(),
          QStringLiteral("enabled=%1").arg(topCombo != nullptr && topCombo->isEnabled()));
    check("gate.no-filelist-reason",
          treePanel->statusText().contains(QStringLiteral("No filelist")),
          treePanel->statusText());
    const QVariantMap published = shell->property("vedit.project.inputs").toMap();
    check("gate.published-hasFilelist-false",
          published.value(QStringLiteral("hasFilelist")).toBool() == false,
          QStringLiteral("hasFilelist=%1")
              .arg(published.value(QStringLiteral("hasFilelist")).toBool()));
    // AC-2.5 poller gate: no project file set is built from a folder scan, and the
    // property is PUBLISHED empty (a never-set property would make this vacuous).
    const QVariant polledFiles = shell->property("vedit.diagnostics.projectFiles");
    check("gate.poller-no-file-set",
          polledFiles.isValid() && stringListOf(polledFiles).isEmpty(),
          QStringLiteral("valid=%1 files=%2")
              .arg(polledFiles.isValid())
              .arg(stringListOf(polledFiles).join(QLatin1Char(','))));
    check("gate.no-project-pass",
          controller->projectPassSerial() == 0 && !controller->projectPassRunning(),
          QStringLiteral("serial=%1 running=%2")
              .arg(static_cast<qulonglong>(controller->projectPassSerial()))
              .arg(controller->projectPassRunning()));

    // ---------------------------------------------------------------------
    // R3 (AC-3.3): with NO catalog the realtime analysis stays SINGLE-FILE
    // ---------------------------------------------------------------------
    // The module closure is the only thing that widens the analysis set, and the
    // gate above proved no catalog ran - so opening a source file must analyse
    // exactly that file. Asserted on the widget's own published set, with the
    // closure map required to be empty in the same frame (otherwise "one file"
    // could be a coincidence of an empty closure for this file only).
    vedit::app::EditorPane *pane = shell->editorArea() != nullptr
                                       ? shell->editorArea()->activePane()
                                       : nullptr;
    check("single.pane", pane != nullptr);
    if (pane != nullptr) {
        QString singleOpenError;
        check("single.open-active", pane->openFile(topPath, &singleOpenError), singleOpenError);
        const bool singlePublished = waitUntil([&shell]() {
            return !shell->property("vedit.diagnostics.realtimeFileSet").toStringList().isEmpty();
        }, 10000);
        const QStringList singleSet =
            shell->property("vedit.diagnostics.realtimeFileSet").toStringList();
        check("single.no-catalog-closure-empty",
              shell->property("vedit.moduleTree.closure").toMap().isEmpty(),
              QStringLiteral("closure-keys=%1")
                  .arg(shell->property("vedit.moduleTree.closure").toMap().size()));
        check("single.realtime-set-is-one-file",
              singlePublished && singleSet == QStringList{QDir::cleanPath(topPath)},
              QStringLiteral("published=%1 set=[%2]")
                  .arg(singlePublished)
                  .arg(singleSet.join(QLatin1Char(','))));
        check("single.realtime-sets-count",
              shell->property("vedit.diagnostics.realtimeSets")
                      .toMap()
                      .value(QStringLiteral("files"))
                      .toInt()
                  == 1,
              QStringLiteral("files=%1")
                  .arg(shell->property("vedit.diagnostics.realtimeSets")
                           .toMap()
                           .value(QStringLiteral("files"))
                           .toInt()));
        // Back to the gate state for the import below: closing the only document
        // keeps the rest of the run about the catalog, not about the analysis.
        pane->closeTab(pane->tabIndexForPath(topPath));
        QCoreApplication::processEvents();
    }

    // ---------------------------------------------------------------------
    // F4 (AC-2.2) + F1 (AC-3.1): ONE catalog per import, real closure content
    // ---------------------------------------------------------------------
    const quint64 passSerialBeforeImport = controller->projectPassSerial();
    QString importError;
    const bool imported = model->importFilelists({sourcesPath}, &importError);
    check("import.accepted", imported, importError);

    if (!haveSlang) {
        skip("import.one-catalog-run", noSlangReason);
        skip("import.catalog-settles", noSlangReason);
        skip("import.no-lint-pass", noSlangReason);
        skip("closure.real-direct-child", noSlangReason);
        skip("closure.real-no-grandchild", noSlangReason);
        skip("closure.real-mid-to-leaf", noSlangReason);
        skip("closure.real-leaf-empty", noSlangReason);
        skip("closure.real-modules-published", noSlangReason);
        skip("top.reruns-catalog", noSlangReason);
        skip("top.same-value-reruns", noSlangReason);
        skip("top.invalid-falls-back", noSlangReason);
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return g_failures == 0 ? 0 : 1;
    }

    const bool answered = waitUntil(
        [&holder, &treePanel]() {
            return holder->property("catalogRuns").toInt() >= 1 && treePanel->catalog().valid;
        },
        180000);
    const int runsAfterImport = holder->property("catalogRuns").toInt();
    const auto catalog = treePanel->catalog();
    check("import.one-catalog-run", answered && runsAfterImport == 1,
          QStringLiteral("answered=%1 runs=%2 error='%3'")
              .arg(answered)
              .arg(runsAfterImport)
              .arg(oneLine(catalog.error)));
    check("import.catalog-file-set", catalog.fileSetSize == 3,
          QStringLiteral("fileSet=%1 modules=%2")
              .arg(catalog.fileSetSize)
              .arg(catalog.modules.size()));
    // One follow-up is OWED only for a model change; a settled model must not
    // start another run. 2.5 s covers the 1 s model watch plus slack.
    QThread::msleep(2500);
    QCoreApplication::processEvents();
    check("import.catalog-settles",
          holder->property("catalogRuns").toInt() == runsAfterImport,
          QStringLiteral("runs=%1 -> %2")
              .arg(runsAfterImport)
              .arg(holder->property("catalogRuns").toInt()));
    // AC-2.4 (amended by A61.1): importing a filelist starts NO project lint pass.
    check("import.no-lint-pass",
          controller->projectPassSerial() == passSerialBeforeImport
              && !controller->projectPassRunning(),
          QStringLiteral("serial=%1 -> %2 running=%3")
              .arg(static_cast<qulonglong>(passSerialBeforeImport))
              .arg(static_cast<qulonglong>(controller->projectPassSerial()))
              .arg(controller->projectPassRunning()));

    // AC-3.1: the closure the REAL catalog produced. The consumer keys whole
    // documents with `QDir::cleanPath`, so the map must be keyed the same way.
    const QVariantMap closure = shell->property("vedit.moduleTree.closure").toMap();
    const QString topKey = QDir::cleanPath(topPath);
    const QString midKey = QDir::cleanPath(midPath);
    const QString leafKey = QDir::cleanPath(leafPath);
    const QStringList topChildren = stringListOf(closure.value(topKey));
    const QStringList midChildren = stringListOf(closure.value(midKey));
    check("closure.real-direct-child", topChildren.contains(midKey),
          QStringLiteral("top -> [%1]").arg(topChildren.join(QLatin1Char(','))));
    check("closure.real-no-grandchild", !topChildren.contains(leafKey),
          QStringLiteral("top -> [%1]").arg(topChildren.join(QLatin1Char(','))));
    check("closure.real-mid-to-leaf", midChildren.contains(leafKey),
          QStringLiteral("mid -> [%1]").arg(midChildren.join(QLatin1Char(','))));
    check("closure.real-leaf-empty",
          closure.contains(leafKey) && stringListOf(closure.value(leafKey)).isEmpty(),
          QStringLiteral("leaf -> [%1]")
              .arg(stringListOf(closure.value(leafKey)).join(QLatin1Char(','))));
    check("closure.real-modules-published",
          shell->property("vedit.moduleTree.closureModules").toInt()
              == static_cast<int>(catalog.modules.size()),
          QStringLiteral("published=%1 modules=%2")
              .arg(shell->property("vedit.moduleTree.closureModules").toInt())
              .arg(catalog.modules.size()));

    // ---------------------------------------------------------------------
    // G1-d (review, batch-20261119): the realtime compile set is DERIVED from
    // THIS catalog, not from the project `.f`
    // ---------------------------------------------------------------------
    // The closure the module tree published must be exactly the one this REAL
    // catalog produces - same keys, same definition-file lists. Every following
    // assertion reads this catalog, so an injected/empty map cannot pass it.
    const QHash<QString, QStringList> derivedClosure =
        vedit::workspace::module_tree::buildModuleClosure(catalog);
    const auto normalizedPaths = [](const QStringList &paths) {
        QStringList out;
        for (const QString &path : paths) {
            out.append(QDir::cleanPath(path));
        }
        out.sort();
        return out;
    };
    QHash<QString, QStringList> derivedNormalized;
    for (auto entry = derivedClosure.constBegin(); entry != derivedClosure.constEnd(); ++entry) {
        derivedNormalized.insert(QDir::cleanPath(entry.key()), normalizedPaths(entry.value()));
    }
    QHash<QString, QStringList> publishedNormalized;
    for (auto entry = closure.constBegin(); entry != closure.constEnd(); ++entry) {
        publishedNormalized.insert(QDir::cleanPath(entry.key()),
                                   normalizedPaths(entry.value().toStringList()));
    }
    check("g1d.closure-is-the-real-catalog", publishedNormalized == derivedNormalized,
          QStringLiteral("published=%1 derived=%2")
              .arg(publishedNormalized.size())
              .arg(derivedNormalized.size()));

    // Every file the closure names is the DEFINITION FILE of a module in this
    // catalog. A file that only appears in a filelist (or in an injected map)
    // is not a catalog definition and must not be reachable this way.
    QSet<QString> definitionFiles;
    for (const vedit::workspace::module_tree::ModuleDefinition &module : catalog.modules) {
        const QString file = QDir::cleanPath(module.file);
        if (!file.isEmpty()) {
            definitionFiles.insert(file);
        }
    }
    QStringList closureFiles;
    for (const QStringList &children : derivedNormalized) {
        closureFiles.append(children);
    }
    QStringList foreignFiles;
    for (const QString &file : closureFiles) {
        if (!definitionFiles.contains(file)) {
            foreignFiles.append(file);
        }
    }
    check("g1d.closure-files-are-catalog-definitions",
          !closureFiles.isEmpty() && foreignFiles.isEmpty(),
          QStringLiteral("files=%1 foreign=[%2]")
              .arg(closureFiles.size())
              .arg(foreignFiles.join(QLatin1Char(','))));

    // The realtime set is `realtimeFileSetFor(current)` published by the
    // diagnostics lane: the current document plus the ONE-LEVEL closure. It must
    // be a subset of {current} ∪ closure[current]; an extra file (a grandchild, a
    // filelist entry) is exactly the G1-d defect. The project's `.f` must never
    // be compiled, and no `-f` argument may ride along with the set.
    if (pane != nullptr) {
        QString openError;
        check("g1d.realtime.open-current", pane->openFile(topPath, &openError), openError);
        const bool publishedAfterCatalog = waitUntil(
            [&shell, &midKey]() {
                return shell->property("vedit.diagnostics.realtimeFileSet")
                    .toStringList()
                    .contains(midKey);
            },
            15000);
        const QStringList realtimeSet =
            shell->property("vedit.diagnostics.realtimeFileSet").toStringList();
        const QStringList sessionSet =
            shell->property("vedit.diagnostics.sessionFileSet").toStringList();
        QSet<QString> allowed;
        allowed.insert(QDir::cleanPath(topKey));
        for (const QString &child : derivedNormalized.value(QDir::cleanPath(topKey))) {
            allowed.insert(child);
        }
        QStringList allowedList;
        for (const QString &file : allowed) {
            allowedList.append(file);
        }
        allowedList.sort();
        const QStringList realtimeNormalized = normalizedPaths(realtimeSet);
        QStringList outside;
        for (const QString &file : realtimeNormalized) {
            if (!allowed.contains(file)) {
                outside.append(file);
            }
        }
        check("g1d.realtime-set-is-derived-from-closure",
              publishedAfterCatalog && !realtimeNormalized.isEmpty() && outside.isEmpty(),
              QStringLiteral("set=[%1] allowed=[%2] outside=[%3]")
                  .arg(realtimeNormalized.join(QLatin1Char(',')))
                  .arg(allowedList.join(QLatin1Char(',')))
                  .arg(outside.join(QLatin1Char(','))));
        check("g1d.realtime-set-direct-child-only",
              realtimeNormalized.contains(midKey) && !realtimeNormalized.contains(leafKey),
              QStringLiteral("set=[%1]").arg(realtimeNormalized.join(QLatin1Char(','))));
        // The session set is what the slang build context compiles (its file
        // lines); with one open document it is the same derived set.
        const QStringList sessionNormalized = normalizedPaths(sessionSet);
        QStringList sessionOutside;
        for (const QString &file : sessionNormalized) {
            if (!allowed.contains(file)) {
                sessionOutside.append(file);
            }
        }
        check("g1d.session-set-is-derived-from-closure",
              !sessionNormalized.isEmpty() && sessionOutside.isEmpty(),
              QStringLiteral("session=[%1] outside=[%2]")
                  .arg(sessionNormalized.join(QLatin1Char(',')))
                  .arg(sessionOutside.join(QLatin1Char(','))));
        const QString filelistPath = QDir::cleanPath(sourcesPath);
        QStringList nestedFilelists;
        for (const QString &file : realtimeNormalized + sessionNormalized) {
            if (file == filelistPath || file.startsWith(QStringLiteral("-f"))) {
                nestedFilelists.append(file);
            }
        }
        check("g1d.no-project-filelist-in-the-compile-set", nestedFilelists.isEmpty(),
              QStringLiteral("offenders=[%1]")
                  .arg(nestedFilelists.join(QLatin1Char(','))));
        // The generated slang build FILE itself (include dirs + defines + the
        // session set) is written by the Rust session worker into a private
        // directory; this e2e has no handle on it, and the default backend here
        // is verible, which never receives one. Its content is locked by
        // `crates/vd-ffi/tests/build_context_no_filelist.rs`, which reads the
        // generated file back and asserts neither `-f` nor the project `.f` path
        // appears (run by `cargo test -p vd-ffi`). This e2e asserts the boundary
        // that file is generated from: the session set above.
        skip("g1d.build-context-file",
             "the generated slang build file is owned by the Rust session worker (no handle "
             "here; verible is the default backend); content locked by "
             "crates/vd-ffi/tests/build_context_no_filelist.rs");
    } else {
        skip("g1d.realtime-set", "no editor pane in this shell");
    }

    // ---------------------------------------------------------------------
    // G1-a (batch-20261120): a file NOT in the filelist never enters the realtime
    // compile set, not even while it is open
    // ---------------------------------------------------------------------
    // The reported defect: `realtimeFileSetFor()` started with `{path}` and the
    // session unioned EVERY open tab, so a file the user merely OPENED entered
    // the realtime compile set; slang then reported / navigated to it while the
    // module tree (fed by the project file set) stayed correct. `outside.sv`
    // re-defines `closure_mid`, the exact name clash of the user's report.
    if (pane != nullptr) {
        // The golden set is the imported filelist's expansion (D1: no directory
        // scan), so outside.sv is outside it by construction. Wait for the lane to
        // adopt that exact set before asserting anything about membership.
        const bool goldenAdopted = waitUntil(
            [&shell]() {
                return shell->property("vedit.diagnostics.realtimeSets")
                           .toMap()
                           .value(QStringLiteral("goldenFileCount"))
                           .toInt()
                       == 3;
            },
            15000);
        check("g1a.golden-set-adopted", goldenAdopted,
              QStringLiteral("goldenFileCount=%1 projectFiles=%2")
                  .arg(shell->property("vedit.diagnostics.realtimeSets")
                           .toMap()
                           .value(QStringLiteral("goldenFileCount"))
                           .toInt())
                  .arg(stringListOf(shell->property("vedit.diagnostics.projectFiles")).size()));
        const QStringList goldenFiles =
            stringListOf(shell->property("vedit.diagnostics.projectFiles"));
        check("g1a.golden-set-is-the-filelist-not-the-directory",
              goldenFiles.size() == 3 && !goldenFiles.contains(outsidePath),
              QStringLiteral("files=[%1]")
                  .arg(goldenFiles.join(QLatin1Char(','))));
        // G1-a "expand once": the golden set this lane adopted came from the
        // PROJECT lane's published expansion, not from a second expansion of its
        // own. The product order installs the project lane first here, so this
        // shell HAS a publisher and the diagnostics lane must have read it.
        const QString fileSetSource =
            shell->property("vedit.diagnostics.projectFilesState")
                .toMap()
                .value(QStringLiteral("source"))
                .toString();
        check("g1a.file-set-comes-from-the-project-lane",
              fileSetSource == QStringLiteral("project-lane"),
              QStringLiteral("source='%1' published=%2 golden=%3")
                  .arg(fileSetSource)
                  .arg(stringListOf(shell->property("vedit.project.files")).size())
                  .arg(goldenFiles.size()));

        QString outsideOpenError;
        check("g1a.open-outside", pane->openFile(outsidePath, &outsideOpenError),
              outsideOpenError);
        pane->setActiveTab(pane->tabIndexForPath(outsidePath));
        const QString outsideKey = QDir::cleanPath(outsidePath);
        const bool outsidePublished = waitUntil(
            [&shell, &outsideKey]() {
                return stringListOf(shell->property("vedit.diagnostics.realtimeFileSet"))
                    == QStringList{outsideKey};
            },
            15000);
        const QStringList outsideRealtime =
            stringListOf(shell->property("vedit.diagnostics.realtimeFileSet"));
        // The outside file is analysed ALONE: the user keeps single-file
        // diagnostics, and neither the project's files nor its own closure (it has
        // no catalog entry) ride along.
        check("g1a.outside-realtime-set-is-the-file-alone",
              outsidePublished && outsideRealtime == QStringList{outsideKey},
              QStringLiteral("set=[%1]")
                  .arg(outsideRealtime.join(QLatin1Char(','))));

        // The SESSION set is what the Rust worker compiles; outside.sv must be
        // absent from it while the still-open golden documents stay covered.
        const QStringList sessionAfterOutside =
            stringListOf(shell->property("vedit.diagnostics.sessionFileSet"));
        bool sessionHasOutside = false;
        for (const QString &file : sessionAfterOutside) {
            if (QDir::cleanPath(file) == outsideKey) {
                sessionHasOutside = true;
            }
        }
        check("g1a.session-excludes-the-outside-file", !sessionHasOutside,
              QStringLiteral("session=[%1]")
                  .arg(sessionAfterOutside.join(QLatin1Char(','))));
        const QStringList sessionNormalizedAfterOutside = [&sessionAfterOutside]() {
            QStringList out;
            for (const QString &file : sessionAfterOutside) {
                out.append(QDir::cleanPath(file));
            }
            out.sort();
            return out;
        }();
        check("g1a.session-keeps-the-golden-documents",
              sessionNormalizedAfterOutside.contains(topKey)
                  && sessionNormalizedAfterOutside.contains(midKey),
              QStringLiteral("session=[%1]")
                  .arg(sessionNormalizedAfterOutside.join(QLatin1Char(','))));
        QStringList controllerSession;
        for (const QString &file : controller->realtimeSessionFiles()) {
            controllerSession.append(QDir::cleanPath(file));
        }
        controllerSession.sort();
        check("g1a.session-matches-controller",
              controllerSession == sessionNormalizedAfterOutside,
              QStringLiteral("controller=[%1] published=[%2]")
                  .arg(controllerSession.join(QLatin1Char(',')))
                  .arg(sessionNormalizedAfterOutside.join(QLatin1Char(','))));
        const QVariantMap setsAfterOutside =
            shell->property("vedit.diagnostics.realtimeSets").toMap();
        check("g1a.realtime-sets-publish-outside-count",
              setsAfterOutside.value(QStringLiteral("outsideFileCount")).toInt() == 1,
              QStringLiteral("outsideFileCount=%1")
                  .arg(setsAfterOutside.value(QStringLiteral("outsideFileCount")).toInt()));
        check("g1a.realtime-sets-publish-golden-count",
              setsAfterOutside.value(QStringLiteral("goldenFileCount")).toInt() == 3,
              QStringLiteral("goldenFileCount=%1")
                  .arg(setsAfterOutside.value(QStringLiteral("goldenFileCount")).toInt()));

        // END TO END (1/2): while the outside file is the ACTIVE document the
        // compile input is `{outside}` ALONE.
        //
        // This reads `fileSetReport().files` - the list `vd_lsp_session_start_v2`
        // receives, i.e. what slang parses - and NOT the published
        // `vedit.diagnostics.realtimeFileSet` property. That distinction is the
        // F1 review finding: the property already said `{outside}` while the REAL
        // set was `{outside} ∪ session` (the controller prepended the active
        // document), so `outside.sv` and `mid.sv` - which BOTH declare
        // `closure_mid` - sat in one build context and slang could pick its own
        // winner. A property-only assertion cannot see that state; this one can.
        const bool compileIsAlone = waitUntil(
            [controller, &outsideKey]() {
                const QStringList files = controller->fileSetReport().files;
                return files.size() == 1 && QDir::cleanPath(files.first()) == outsideKey;
            },
            15000);
        QStringList compileOutside;
        for (const QString &file : controller->fileSetReport().files) {
            compileOutside.append(QDir::cleanPath(file));
        }
        compileOutside.sort();
        check("g1a.compile-set-is-the-outside-file-alone",
              compileIsAlone && compileOutside == QStringList{outsideKey}
                  && !compileOutside.contains(midKey),
              QStringLiteral("compile=[%1]").arg(compileOutside.join(QLatin1Char(','))));

        // Back on a GOLDEN document the analysis set is exactly what it was
        // before the outside tab existed: {top} ∪ its one-level closure.
        pane->setActiveTab(pane->tabIndexForPath(topPath));
        const bool insideStillDerived = waitUntil(
            [&shell, &midKey]() {
                return stringListOf(shell->property("vedit.diagnostics.realtimeFileSet"))
                    .contains(midKey);
            },
            15000);
        QStringList insideNormalized;
        for (const QString &file :
             stringListOf(shell->property("vedit.diagnostics.realtimeFileSet"))) {
            insideNormalized.append(QDir::cleanPath(file));
        }
        insideNormalized.sort();
        QStringList expectedInside{topKey, midKey};
        expectedInside.sort();
        check("g1a.inside-set-unchanged-after-outside-open",
              insideStillDerived && insideNormalized == expectedInside,
              QStringLiteral("set=[%1] expected=[%2]")
                  .arg(insideNormalized.join(QLatin1Char(',')))
                  .arg(expectedInside.join(QLatin1Char(','))));
        check("g1a.outside-count-kept-while-open",
              shell->property("vedit.diagnostics.realtimeSets")
                      .toMap()
                      .value(QStringLiteral("outsideFileCount"))
                      .toInt()
                  == 1,
              QStringLiteral("outsideFileCount=%1")
                  .arg(shell->property("vedit.diagnostics.realtimeSets")
                           .toMap()
                           .value(QStringLiteral("outsideFileCount"))
                           .toInt()));

        // END TO END (2/2): with a project file active again the session is
        // rebuilt from the golden set only - the outside file is STILL OPEN and
        // still absent from the compile input, so only the golden rule keeps it
        // out of slang's parse.
        const bool compileSettled = waitUntil(
            [controller, &topKey, &outsideKey]() {
                bool hasTop = false;
                bool hasOutside = false;
                for (const QString &file : controller->fileSetReport().files) {
                    const QString key = QDir::cleanPath(file);
                    if (key == topKey) {
                        hasTop = true;
                    }
                    if (key == outsideKey) {
                        hasOutside = true;
                    }
                }
                return hasTop && !hasOutside;
            },
            15000);
        QStringList compileSet;
        for (const QString &file : controller->fileSetReport().files) {
            compileSet.append(QDir::cleanPath(file));
        }
        check("g1a.compile-set-excludes-the-outside-file", compileSettled,
              QStringLiteral("compile=[%1]").arg(compileSet.join(QLatin1Char(','))));

        // The rest of the run is about the catalog: close the outside tab.
        pane->closeTab(pane->tabIndexForPath(outsidePath));
        QCoreApplication::processEvents();
    } else {
        skip("g1a.outside-file", QStringLiteral("no editor pane in this shell"));
    }

    // ---------------------------------------------------------------------
    // F5 (AC-2.3b): a top re-runs the catalog with `--top`, same value included
    // ---------------------------------------------------------------------
    check("top.combo-enabled", topCombo != nullptr && topCombo->isEnabled());
    chooseTop(topCombo, QStringLiteral("closure_mid"));
    const bool scoped = waitUntil(
        [&holder, &treePanel]() {
            const auto current = treePanel->catalog();
            return current.valid && current.top == QStringLiteral("closure_mid")
                && holder->property("catalogRuns").toInt() >= 2;
        },
        180000);
    const int runsAfterTop = holder->property("catalogRuns").toInt();
    check("top.reruns-catalog", scoped && runsAfterTop == 2,
          QStringLiteral("scoped=%1 runs=%2 top='%3'")
              .arg(scoped)
              .arg(runsAfterTop)
              .arg(treePanel->catalog().top));
    check("top.tree-root-is-top", rootsOf(treePanel) == QStringList{QStringLiteral("closure_mid")},
          QStringLiteral("roots=[%1]").arg(rootsOf(treePanel).join(QLatin1Char(','))));
    check("top.client-top", client->top() == QStringLiteral("closure_mid"), client->top());

    // AC-7.2: choosing the SAME top again must re-run too (a stateChanged-only
    // no-op would leave a tree that never actually ran that top).
    const int runsBeforeSame = holder->property("catalogRuns").toInt();
    chooseTop(topCombo, QStringLiteral("closure_mid"));
    const bool rerun = waitUntil(
        [&holder, &treePanel, runsBeforeSame]() {
            return holder->property("catalogRuns").toInt() > runsBeforeSame
                && treePanel->catalog().valid
                && treePanel->catalog().top == QStringLiteral("closure_mid");
        },
        180000);
    check("top.same-value-reruns", rerun,
          QStringLiteral("runs=%1 -> %2")
              .arg(runsBeforeSame)
              .arg(holder->property("catalogRuns").toInt()));

    // ---------------------------------------------------------------------
    // F9 (AC-7.3): an unknown top falls back to Auto AND the summary says why
    // ---------------------------------------------------------------------
    const int runsBeforeInvalid = holder->property("catalogRuns").toInt();
    chooseTop(topCombo, QStringLiteral("NO_SUCH_MODULE"));
    const bool autoAgain = waitUntil(
        [&holder, &treePanel, runsBeforeInvalid]() {
            const auto current = treePanel->catalog();
            return current.valid && current.top.isEmpty()
                && holder->property("catalogRuns").toInt() > runsBeforeInvalid;
        },
        180000);
    check("top.invalid-falls-back", autoAgain,
          QStringLiteral("top='%1' runs=%2")
              .arg(treePanel->catalog().top)
              .arg(holder->property("catalogRuns").toInt()));
    const QString summary = treePanel->summaryText();
    check("top.invalid-note",
          summary.contains(QStringLiteral("NO_SUCH_MODULE"))
              && summary.contains(QStringLiteral("not in the module catalog")),
          summary);
    check("top.invalid-roots-are-auto",
          rootsOf(treePanel) == QStringList{QStringLiteral("closure_top")},
          QStringLiteral("roots=[%1]").arg(rootsOf(treePanel).join(QLatin1Char(','))));

    // ---------------------------------------------------------------------
    // G1-c (batch-20261120): the files whose definitions ALL lost the duplicate
    // pick leave the compiler input
    // ---------------------------------------------------------------------
    // Contract §10 G1-c: there is ONE duplicate-definition authority (this
    // catalog), so slang's own duplicate rule must never become a second one.
    // Two files declare `dup_shared`; the file-set order (and, deliberately, the
    // alphabetical order) picks `dup_a_winner`. `dup_b_loser` declares NOTHING
    // else, so the whole file is shadowed - and the project lint pass must not
    // hand it to slang. `dup_c_unique` ALSO loses `dup_shared` but declares a
    // unique module, so it must be KEPT: excluding it would drop `dup_only_here`
    // from the design.
    const QString dupWinnerPath = QDir(project).absoluteFilePath(QStringLiteral("dup_a_winner.sv"));
    const QString dupLoserPath = QDir(project).absoluteFilePath(QStringLiteral("dup_b_loser.sv"));
    const QString dupUniquePath = QDir(project).absoluteFilePath(QStringLiteral("dup_c_unique.sv"));
    const QString dupsPath = QDir(project).absoluteFilePath(QStringLiteral("dups.f"));
    const bool dupFixture =
        writeFile(dupWinnerPath, QStringLiteral("module dup_shared;\n  logic w1;\nendmodule\n"))
        && writeFile(dupLoserPath, QStringLiteral("module dup_shared;\n  logic l1 l2;\nendmodule\n"))
        && writeFile(dupUniquePath,
                     QStringLiteral("module dup_shared;\n  logic u1;\nendmodule\n"
                                    "module dup_only_here;\nendmodule\n"))
        && writeFile(dupsPath,
                     QStringLiteral("dup_a_winner.sv\ndup_b_loser.sv\ndup_c_unique.sv\n"));
    check("g1c.fixture", dupFixture);
    if (!dupFixture) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    const int runsBeforeDup = holder->property("catalogRuns").toInt();
    QString dupImportError;
    check("g1c.import", model->importFilelists({dupsPath}, &dupImportError), dupImportError);
    const bool dupCatalogAnswered = waitUntil(
        [&holder, &treePanel, runsBeforeDup]() {
            const auto current = treePanel->catalog();
            return holder->property("catalogRuns").toInt() > runsBeforeDup && current.valid
                && !current.shadowedFiles.isEmpty();
        },
        180000);
    // The assertions read THIS catalog, so an injected list cannot pass them.
    const auto dupCatalog = treePanel->catalog();
    const auto normalizedList = [](const QStringList &paths) {
        QStringList out;
        for (const QString &path : paths) {
            out.append(QDir::cleanPath(path));
        }
        out.sort();
        return out;
    };
    const QString dupWinnerKey = QDir::cleanPath(dupWinnerPath);
    const QString dupLoserKey = QDir::cleanPath(dupLoserPath);
    const QString dupUniqueKey = QDir::cleanPath(dupUniquePath);
    const QStringList catalogShadowed = normalizedList(dupCatalog.shadowedFiles);
    check("g1c.catalog-publishes-the-loser",
          dupCatalogAnswered && catalogShadowed == QStringList{dupLoserKey},
          QStringLiteral("shadowed=[%1] error='%2'")
              .arg(catalogShadowed.join(QLatin1Char(',')))
              .arg(oneLine(dupCatalog.error)));
    check("g1c.catalog-keeps-every-other-file",
          !catalogShadowed.contains(dupWinnerKey) && !catalogShadowed.contains(dupUniqueKey),
          QStringLiteral("shadowed=[%1]").arg(catalogShadowed.join(QLatin1Char(','))));
    // The module tree is the ONE authority; it publishes its answer on the shell
    // seam every other lane reads (`vedit.moduleTree.shadowedFiles`).
    const QStringList publishedShadowed =
        normalizedList(shell->property("vedit.moduleTree.shadowedFiles").toStringList());
    check("g1c.shell-publishes-the-shadowed-list", publishedShadowed == QStringList{dupLoserKey},
          QStringLiteral("published=[%1]").arg(publishedShadowed.join(QLatin1Char(','))));
    // The user is TOLD: the service's sentence follows the duplicate warning, in
    // the same ` · ⚠ ` shape (`summaryText()` is the full text, R2-d).
    const QString dupSummary = treePanel->summaryText();
    check("g1c.summary-names-the-exclusion",
          dupSummary.contains(QStringLiteral("excluded from the compiler input"))
              && dupSummary.contains(QStringLiteral("dup_b_loser.sv")),
          oneLine(dupSummary));

    // The project lint pass (the bottom dock's `project.lint`) is the consumer
    // that hands files to slang: its requested set must not contain the loser.
    const bool projectSetAdopted = waitUntil(
        [&shell]() {
            return stringListOf(shell->property("vedit.diagnostics.projectFiles")).size() == 6;
        },
        20000);
    const QStringList projectSet =
        stringListOf(shell->property("vedit.diagnostics.projectFiles"));
    check("g1c.project-set-adopted", projectSetAdopted && projectSet.size() == 6,
          QStringLiteral("files=%1 [%2]")
              .arg(projectSet.size())
              .arg(normalizedList(projectSet).join(QLatin1Char(','))));
    QAction *lintProjectAction = shell->commands() != nullptr
        ? shell->commands()->action(QStringLiteral("project.lint"))
        : nullptr;
    check("g1c.project-lint-action", lintProjectAction != nullptr);
    if (lintProjectAction != nullptr) {
        lintProjectAction->trigger();
        QCoreApplication::processEvents();
        // `lastProjectPassRequest()` is what the controller recorded as the
        // pass's compiler input (after the registry/scope filters).
        const QStringList requested = normalizedList(controller->lastProjectPassRequest().files);
        check("g1c.project-lint-drops-the-loser", !requested.contains(dupLoserKey),
              QStringLiteral("requested=[%1]").arg(requested.join(QLatin1Char(','))));
        check("g1c.project-lint-keeps-the-other-two",
              requested.contains(dupWinnerKey) && requested.contains(dupUniqueKey),
              QStringLiteral("requested=[%1]").arg(requested.join(QLatin1Char(','))));
        check("g1c.project-lint-count-is-file-set-minus-shadowed",
              requested.size() == projectSet.size() - 1,
              QStringLiteral("requested=%1 projectSet=%2")
                  .arg(requested.size())
                  .arg(projectSet.size()));
    }

    // G1-c, the other side of the same rule: OPENING the losing file must still
    // analyse it (single-file diagnostics), and must not pull the winner in. The
    // realtime set is `{active} ∪ closure[active]`, and a shadowed file defines no
    // ADOPTED module, so it has no closure entry - the `keep` argument of
    // `dropShadowedFiles` is what keeps the user's own document analysed.
    if (pane != nullptr) {
        QString loserOpenError;
        const bool loserOpened = pane->openFile(dupLoserPath, &loserOpenError);
        pane->setActiveTab(pane->tabIndexForPath(dupLoserPath));
        const bool loserAnalysed = waitUntil(
            [&shell, &dupLoserKey]() {
                return stringListOf(shell->property("vedit.diagnostics.realtimeFileSet"))
                    == QStringList{dupLoserKey};
            },
            15000);
        const QStringList loserSet =
            normalizedList(shell->property("vedit.diagnostics.realtimeFileSet").toStringList());
        check("g1c.opened-loser-is-analysed-alone",
              loserOpened && loserAnalysed && loserSet == QStringList{dupLoserKey},
              QStringLiteral("opened=%1 set=[%2] error='%3'")
                  .arg(loserOpened)
                  .arg(loserSet.join(QLatin1Char(',')))
                  .arg(loserOpenError));
        const QStringList sessionAfterLoser =
            normalizedList(shell->property("vedit.diagnostics.sessionFileSet").toStringList());
        check("g1c.session-keeps-the-loser-out",
              !sessionAfterLoser.contains(dupLoserKey)
                  && sessionAfterLoser.contains(QDir::cleanPath(topPath)),
              QStringLiteral("session=[%1]").arg(sessionAfterLoser.join(QLatin1Char(','))));
        // …and the REAL compiler input for that document is `{loser}` ALONE: the
        // winner (`dup_a_winner.sv`), the other project files and the loser's own
        // closure must all be absent, or slang sees two `dup_shared` definitions
        // and picks its own winner again (G1-c). `fileSetReport().files` is the
        // set the session was started over; the published properties above are
        // not enough (that was the F1 finding).
        const bool loserCompileAlone = waitUntil(
            [controller, &dupLoserKey]() {
                const QStringList files = controller->fileSetReport().files;
                return files.size() == 1 && QDir::cleanPath(files.first()) == dupLoserKey;
            },
            15000);
        const QStringList loserCompile = normalizedList(controller->fileSetReport().files);
        check("g1c.compile-set-is-the-loser-alone",
              loserCompileAlone && loserCompile == QStringList{dupLoserKey}
                  && !loserCompile.contains(dupWinnerKey),
              QStringLiteral("compile=[%1]").arg(loserCompile.join(QLatin1Char(','))));
    }

    // ---------------------------------------------------------------------
    // G1-c (review finding 3): the "catalog arrives LATE" tick
    // ---------------------------------------------------------------------
    // The block above waits for the catalog BEFORE opening the loser. The user's
    // report is the opposite ordering: the document is ALREADY open when a
    // catalog run publishes its shadowed list. `ActiveFileWatcher::shadowedSetChanged()`
    // is that tick; without it the tab keeps sharing the build context with the
    // winner until the user happens to switch tabs ("jump in and it comes back").
    //
    // To make the window deterministic, the next catalog is held open by a
    // wrapper that sleeps and then execs the real slang: both late duplicates are
    // opened while the golden set already contains them and
    // `vedit.moduleTree.shadowedFiles` still does not, so the state under test is
    // guaranteed rather than raced. When the catalog finally publishes, the
    // still-open loser must leave the compile set on the NEXT analysis.
    {
        const QString lateWinnerPath = QDir(project).absoluteFilePath("late_a_winner.sv");
        const QString lateLoserPath = QDir(project).absoluteFilePath("late_b_loser.sv");
        const QString lateListPath = QDir(project).absoluteFilePath("late.f");
        const bool lateFixture =
            writeFile(lateWinnerPath, QStringLiteral("module late_shared;\n  logic w1;\nendmodule\n"))
            && writeFile(lateLoserPath, QStringLiteral("module late_shared;\n  logic l1 l2;\nendmodule\n"))
            && writeFile(lateListPath, QStringLiteral("late_a_winner.sv\nlate_b_loser.sv\n"));
        check("g1c.late-fixture", lateFixture);
        const QString realSlang = qEnvironmentVariable("VEDIT_SLANG");
        const QString wrapperPath = dir.filePath(QStringLiteral("slow-slang.sh"));
        const bool wrapperOk = lateFixture && !realSlang.isEmpty()
            && writeFile(wrapperPath,
                         QStringLiteral("#!/bin/sh\n"
                                        "# G1-c late-catalog window: hold the catalog open.\n"
                                        "sleep 6\n"
                                        "exec \"%1\" \"$@\"\n")
                             .arg(realSlang))
            && QFile::setPermissions(wrapperPath,
                                     QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                         | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                         | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                         | QFileDevice::ExeOther);
        check("g1c.late-catalog-window", wrapperOk,
              QStringLiteral("wrapper='%1' slang='%2'").arg(wrapperPath, realSlang));
        if (!wrapperOk) {
            skip("g1c.late-opened-before-the-catalog",
                 QStringLiteral("cannot hold the catalog open (wrapper not written)"));
            skip("g1c.late-catalog-drops-the-open-loser",
                 QStringLiteral("cannot hold the catalog open (wrapper not written)"));
            skip("g1c.late-catalog-session-keeps-the-loser-out",
                 QStringLiteral("cannot hold the catalog open (wrapper not written)"));
        } else {
            qputenv("VEDIT_SLANG", wrapperPath.toLocal8Bit());
            const QString lateWinnerKey = QDir::cleanPath(lateWinnerPath);
            const QString lateLoserKey = QDir::cleanPath(lateLoserPath);
            QString lateImportError;
            check("g1c.late-import",
                  model->importFilelists({lateListPath}, &lateImportError), lateImportError);
            // The diagnostics lane adopts the project lane's answer on its watch
            // tick (<= 800 ms); the catalog child is still sleeping.
            const bool goldenHasLate = waitUntil(
                [&shell, &lateWinnerKey, &lateLoserKey]() {
                    const QStringList golden =
                        stringListOf(shell->property("vedit.diagnostics.projectFiles"));
                    return golden.contains(lateWinnerKey) && golden.contains(lateLoserKey);
                },
                20000);
            check("g1c.late-golden-adopted", goldenHasLate,
                  QStringLiteral("golden=%1")
                      .arg(normalizedList(shell->property("vedit.diagnostics.projectFiles")
                                              .toStringList())
                               .join(QLatin1Char(','))));
            // Open BOTH duplicates while the catalog has not answered for them.
            // Every earlier tab is closed first: the session is the union of the
            // GOLDEN open documents' closures, so a leftover tab would widen the
            // compile set and make "exactly the two late files" ambiguous.
            QString lateWinnerOpenError;
            QString lateLoserOpenError;
            const bool opened = pane != nullptr
                && ([pane]() {
                       for (int index = pane->tabCount() - 1; index >= 0; --index) {
                           pane->closeTab(index);
                       }
                       QCoreApplication::processEvents();
                       // The last close leaves one fresh EMPTY tab (the pane
                       // always shows one); it has no path, so it never joins
                       // the golden session.
                       return true;
                   })()
                && pane->openFile(lateWinnerPath, &lateWinnerOpenError)
                && pane->openFile(lateLoserPath, &lateLoserOpenError);
            if (pane != nullptr) {
                pane->setActiveTab(pane->tabIndexForPath(lateLoserPath));
            }
            const bool bothInCompileSet = waitUntil(
                [controller, &lateWinnerKey, &lateLoserKey]() {
                    QStringList files;
                    for (const QString &file : controller->fileSetReport().files) {
                        files.append(QDir::cleanPath(file));
                    }
                    files.sort();
                    QStringList expected{lateWinnerKey, lateLoserKey};
                    expected.sort();
                    return files == expected;
                },
                15000);
            const QStringList lateCompileBefore =
                normalizedList(controller->fileSetReport().files);
            const QStringList shadowedBefore =
                normalizedList(shell->property("vedit.moduleTree.shadowedFiles").toStringList());
            check("g1c.late-opened-before-the-catalog",
                  opened && bothInCompileSet && !shadowedBefore.contains(lateLoserKey),
                  QStringLiteral("opened=%1 compile=[%2] shadowed=[%3]")
                      .arg(opened ? 1 : 0)
                      .arg(lateCompileBefore.join(QLatin1Char(',')))
                      .arg(shadowedBefore.join(QLatin1Char(','))));
            // The delayed catalog finishes and publishes the loser as shadowed.
            const bool lateShadowed = waitUntil(
                [&shell, &lateLoserKey]() {
                    return stringListOf(shell->property("vedit.moduleTree.shadowedFiles"))
                        .contains(lateLoserKey);
                },
                240000);
            check("g1c.late-catalog-published-the-loser", lateShadowed,
                  QStringLiteral("shadowed=%1")
                      .arg(normalizedList(shell->property("vedit.moduleTree.shadowedFiles")
                                              .toStringList())
                               .join(QLatin1Char(','))));
            const bool lateDropped = waitUntil(
                [controller, &lateLoserKey]() {
                    const QStringList files = controller->fileSetReport().files;
                    return files.size() == 1 && QDir::cleanPath(files.first()) == lateLoserKey;
                },
                20000);
            const QStringList lateCompileAfter =
                normalizedList(controller->fileSetReport().files);
            check("g1c.late-catalog-drops-the-open-loser",
                  lateShadowed && lateDropped
                      && lateCompileAfter == QStringList{lateLoserKey}
                      && !lateCompileAfter.contains(lateWinnerKey),
                  QStringLiteral("compile=[%1]")
                      .arg(lateCompileAfter.join(QLatin1Char(','))));
            const QStringList lateSessionAfter =
                normalizedList(shell->property("vedit.diagnostics.sessionFileSet").toStringList());
            check("g1c.late-catalog-session-keeps-the-loser-out",
                  !lateSessionAfter.contains(lateLoserKey)
                      && lateSessionAfter.contains(lateWinnerKey),
                  QStringLiteral("session=[%1]").arg(lateSessionAfter.join(QLatin1Char(','))));
            qputenv("VEDIT_SLANG", realSlang.toLocal8Bit());
        }
    }

    std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
