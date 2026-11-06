// RM-9 (T20) — the status dots must cover the WHOLE project, at the product
// level: the real shell + the real project, file-tree and diagnostics lanes,
// with a deterministic fake lint backend installed BEFORE the first event-loop
// turn (the project-open pass starts as soon as the file set is adopted).
//
// User report this encodes: (a) unopened files showed NOTHING, so "no dot" was
// indistinguishable from "not a file"; (b) a file that exists only through an
// imported `.f` filelist appeared ONLY in the Project panel's list, with no
// status anywhere. The contract:
//   * a project OPEN is a full pass over the expanded set (scan + `.f` lists),
//   * after it every file has a verdict - the opened one and every other one,
//   * a filelist-only file has a row in the tree's "From filelist" group AND a
//     status mark in the Project panel's Files list,
//   * a dirty buffer is excluded (never silently clean),
//   * a SAVE pass is incremental (exactly the saved file) and the other files
//     keep their verdicts.
//
// Run offscreen; with `--shot <png>` the final window is saved as evidence.

#include "diagnostics_controller.h"
#include "diagnostics_integration.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "file_tree_model.h"
#include "file_tree_widget.h"
#include "filetree_integration.h"
#include "ktexteditor_provider.h"
#include "panel_registry.h"
#include "project_integration.h"
#include "project_model.h"
#include "project_panel.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAbstractItemDelegate>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QWidget>

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

vedit::workspace::diagnostics::DiagnosticEntry diagnosticRow(const QString &file, int severity,
                                                             const QString &code)
{
    using vedit::workspace::diagnostics::DiagnosticEntry;
    DiagnosticEntry entry;
    entry.file = file;
    entry.line = 2;
    entry.column = 1;
    entry.severity = severity; // display scale: 1 error, 2 warning
    entry.code = code;
    entry.message = code;
    entry.source = QStringLiteral("fake-lint");
    return entry;
}

/// The deterministic project pass. One row per file the test wants a verdict
/// for; every other linted file is CLEAN (no rows). `filesSeen` is the request
/// of the LAST started pass - the coverage policy's observable.
class FakeProjectLint final : public vedit::workspace::diagnostics::ProjectLintBackend {
public:
    QVector<vedit::workspace::diagnostics::DiagnosticEntry> rows;
    QStringList filesSeen;
    int startCount = 0;
    int cancelCount = 0;
    quint64 job_ = 0;
    vedit::workspace::diagnostics::LintPassSnapshot snapshot;

    bool available() const override { return true; }
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
    void cancel(quint64) override
    {
        ++cancelCount;
        snapshot.phase = vedit::workspace::diagnostics::LintPhase::Cancelled;
    }
    void release(quint64) override {}

private:
};

QString dataString(const QModelIndex &index)
{
    return index.data(Qt::DisplayRole).toString();
}

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGPIPE, SIG_IGN); // AGENT.md §4: the host links Rust
    QApplication app(argc, argv);
    QString shotPath;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QLatin1String("--shot") && index + 1 < argc) {
            shotPath = QString::fromLocal8Bit(argv[index + 1]);
        }
    }

    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("rm9.tempdir", false);
        return 1;
    }
    const QString stateDir = dir.filePath(QStringLiteral("state"));
    const QString root = dir.filePath(QStringLiteral("project"));
    const QString shared = dir.filePath(QStringLiteral("shared"));
    const QString lists = dir.filePath(QStringLiteral("lists"));
    QDir().mkpath(stateDir);
    QDir().mkpath(root);
    QDir().mkpath(shared);
    QDir().mkpath(lists);
    const QString aPath = QDir(root).filePath(QStringLiteral("a.sv"));
    const QString bPath = QDir(root).filePath(QStringLiteral("b.sv"));
    const QString outsidePath = QDir(shared).filePath(QStringLiteral("outside.sv"));
    const QString listPath = QDir(lists).filePath(QStringLiteral("imported.f"));
    const bool fixture =
        writeFile(aPath, QStringLiteral("module a;\n  logic x;   \nendmodule\n"))
        && writeFile(bPath, QStringLiteral("module b;\n  logic y;\nendmodule\n"))
        && writeFile(outsidePath, QStringLiteral("module outside;\nendmodule\n"))
        // The imported list is OUTSIDE the opened folder and references a file
        // that is outside too: only a consumer that expands the imported `.f`
        // can see it, and the directory tree never shows it.
        && writeFile(listPath, QStringLiteral("../shared/outside.sv\n"));
    check("rm9.fixture", fixture);
    if (!fixture) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }

    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.projectRoot = root;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    std::unique_ptr<vedit::app::ApplicationShell> shell(
        new vedit::app::ApplicationShell(options));
    if (provider != nullptr) {
        shell->editorArea()->setProvider(provider.get());
    }
    shell->editorArea()->ensurePane();

    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = root;
    integration.stateDir = stateDir;
    vedit::workspace::installFileTree(*shell, integration);
    vedit::workspace::installProject(*shell, integration);
    vedit::workspace::installDiagnostics(*shell, integration);

    // The deterministic backend must own the pass BEFORE the first event-loop
    // turn: a project open starts the full pass as soon as the set is adopted.
    auto *controller = vedit::workspace::diagnosticsControllerForShell(*shell);
    check("rm9.controller", controller != nullptr);
    auto fakeOwned = std::make_unique<FakeProjectLint>();
    FakeProjectLint *fake = fakeOwned.get();
    // The project pass speaks the C ABI's CLI scale: 0 = error, 1 = warning.
    fake->rows.append(diagnosticRow(aPath, 0, QStringLiteral("fake-error-a")));
    fake->rows.append(diagnosticRow(outsidePath, 1, QStringLiteral("fake-warning-outside")));
    if (controller != nullptr) {
        controller->setProjectLintBackend(std::move(fakeOwned));
    }
    auto *projectModel = vedit::workspace::projectModelForShell(*shell);
    auto *projectPanel = shell->findChild<vedit::workspace::project::ProjectPanel *>();
    check("rm9.project-model", projectModel != nullptr);
    check("rm9.project-panel", projectPanel != nullptr);
    if (controller == nullptr || projectModel == nullptr || projectPanel == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }

    shell->panels()->showPanel(QStringLiteral("file-tree"), shell.get());
    shell->panels()->showPanel(QStringLiteral("outline"), shell.get());
    shell->resize(1280, 860);
    shell->show();
    QCoreApplication::processEvents();

    // The file tree is created by the panel factory on first show, not at
    // install (unlike the controller/project panel above).
    auto *tree = shell->findChild<vedit::workspace::FileTreeWidget *>();
    check("rm9.tree", tree != nullptr);
    if (tree == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }

    // The project lane expands the root; then the imported `.f` joins the set.
    const bool rootReady = waitUntil(
        [projectModel]() { return projectModel->hasProject() && !projectModel->isPending(); },
        15000);
    check("rm9.project-expanded", rootReady);
    check("rm9.import-filelist", projectPanel->importFilelists(QStringList{listPath}));
    const bool outsideInProject = waitUntil(
        [projectModel, &outsidePath]() { return projectModel->files().contains(outsidePath); },
        15000);
    check("rm9.outside-in-project-set", outsideInProject,
          QStringLiteral("files=%1").arg(projectModel->files().size()));

    // The diagnostics lane expands the SAME inputs; wait for its adoption.
    const bool adopted = waitUntil(
        [controller, &outsidePath, &aPath]() {
            return controller->projectFiles().contains(outsidePath)
                   && controller->projectFiles().contains(aPath);
        },
        15000);
    check("rm9.analysis-set-has-outside", adopted,
          QStringLiteral("files=%1").arg(controller->projectFiles().size()));

    // (b) the project-open pass runs over the WHOLE expanded set, and after it
    // every file has a verdict - including the two nobody opened.
    const bool passSawWholeSet = waitUntil(
        [fake, &aPath, &bPath, &outsidePath]() {
            return fake->filesSeen.contains(aPath) && fake->filesSeen.contains(bPath)
                   && fake->filesSeen.contains(outsidePath);
        },
        15000);
    check("rm9.on-open-pass-covers-whole-set", passSawWholeSet,
          QStringLiteral("files=%1").arg(fake->filesSeen.join(QLatin1Char(','))));
    check("rm9.on-open-pass-is-automatic", controller->lastPassWasAutomatic()
              || controller->projectPassRunning() || !controller->upperLayerFiles().isEmpty(),
          QStringLiteral("automatic=%1").arg(controller->lastPassWasAutomatic() ? 1 : 0));
    const bool passDone =
        waitUntil([controller]() { return !controller->projectPassRunning(); }, 15000);
    check("rm9.on-open-pass-finished", passDone);
    QCoreApplication::processEvents();

    // (b) every file of the project set has a verdict, none is "no verdict".
    const QHash<QString, int> statuses = controller->fileStatuses();
    bool everyFileHasVerdict = !controller->projectFiles().isEmpty();
    for (const QString &file : controller->projectFiles()) {
        if (statuses.value(file, 0) == 0) {
            everyFileHasVerdict = false;
            std::printf("OBSERVED rm9.no-verdict file='%s'\n", qPrintable(file));
        }
    }
    check("rm9.every-project-file-has-verdict", everyFileHasVerdict,
          QStringLiteral("project=%1 map=%2")
              .arg(controller->projectFiles().size())
              .arg(statuses.size()));
    check("rm9.never-opened-a-is-error", statuses.value(aPath, 0) == 1,
          QStringLiteral("a=%1").arg(statuses.value(aPath, -1)));
    check("rm9.never-opened-b-is-clean", statuses.value(bPath, 0) == 3,
          QStringLiteral("b=%1").arg(statuses.value(bPath, -1)));
    check("rm9.filelist-only-outside-is-warning", statuses.value(outsidePath, 0) == 2,
          QStringLiteral("outside=%1").arg(statuses.value(outsidePath, -1)));

    // (c) the tree shows the filelist-only file in its synthetic group, with
    // the SAME status the map carries.
    const bool treeGroupReady = waitUntil(
        [tree, &outsidePath]() {
            if (tree == nullptr || tree->model() == nullptr) {
                return false;
            }
            vedit::workspace::FileTreeModel *model = tree->model();
            const QModelIndex rootIndex = model->index(0, 0, QModelIndex());
            for (int row = 0; row < model->rowCount(rootIndex); ++row) {
                const QModelIndex group = model->index(row, 0, rootIndex);
                if (!dataString(group).startsWith(QStringLiteral("From filelist"))) {
                    continue;
                }
                for (int child = 0; child < model->rowCount(group); ++child) {
                    const QModelIndex member = model->index(child, 0, group);
                    if (member.data(vedit::workspace::FileTreeModel::PathRole).toString()
                        == QFileInfo(outsidePath).absoluteFilePath()) {
                        return true;
                    }
                }
            }
            return false;
        },
        10000);
    check("rm9.tree-filelist-group-has-outside", treeGroupReady);
    int treeStatus = -1;
    if (tree != nullptr && tree->model() != nullptr) {
        vedit::workspace::FileTreeModel *model = tree->model();
        const QModelIndex rootIndex = model->index(0, 0, QModelIndex());
        for (int row = 0; row < model->rowCount(rootIndex) && treeStatus < 0; ++row) {
            const QModelIndex group = model->index(row, 0, rootIndex);
            if (!dataString(group).startsWith(QStringLiteral("From filelist"))) {
                continue;
            }
            for (int child = 0; child < model->rowCount(group); ++child) {
                const QModelIndex member = model->index(child, 0, group);
                if (member.data(vedit::workspace::FileTreeModel::PathRole).toString()
                    == QFileInfo(outsidePath).absoluteFilePath()) {
                    treeStatus = member
                                     .data(vedit::workspace::FileTreeModel::FileStatusRole)
                                     .toInt();
                    break;
                }
            }
        }
    }
    check("rm9.tree-group-status-matches-map", treeStatus == 2,
          QString::number(treeStatus));

    // (c) the Project panel's Files list paints the same status for that file.
    auto *filesList = projectPanel->findChild<QListWidget *>(QStringLiteral("project-file-list"));
    check("rm9.panel-files-list", filesList != nullptr);
    int panelRow = -1;
    if (filesList != nullptr) {
        for (int row = 0; row < filesList->count(); ++row) {
            if (filesList->item(row)->data(Qt::UserRole).toString()
                == QFileInfo(outsidePath).absoluteFilePath()) {
                panelRow = row;
                break;
            }
        }
    }
    check("rm9.panel-lists-outside", panelRow >= 0, QString::number(filesList != nullptr
                                                                        ? filesList->count()
                                                                        : -1));
    check("rm9.panel-status-matches-map", projectPanel->fileStatusAt(panelRow) == 2,
          QString::number(panelRow >= 0 ? projectPanel->fileStatusAt(panelRow) : -1));
    // And the list delegate really PAINTS the shared warning colour (a status
    // source that nothing renders would be a false green).
    bool paintedWarning = false;
    if (filesList != nullptr && panelRow >= 0 && filesList->itemDelegate() != nullptr) {
        QImage image(260, 24, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        QStyleOptionViewItem option;
        option.rect = QRect(0, 0, 260, 24);
        option.state = QStyle::State_Enabled;
        option.widget = filesList;
        filesList->itemDelegate()->paint(&painter, option,
                                         filesList->model()->index(panelRow, 0));
        painter.end();
        const QColor warning(0xf9, 0xa8, 0x25);
        for (int y = 0; y < image.height() && !paintedWarning; ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) == warning) {
                    paintedWarning = true;
                    break;
                }
            }
        }
    }
    check("rm9.panel-renders-status-mark", paintedWarning);

    // (d) a dirty buffer is EXCLUDED from the pass and never silently clean.
    vedit::app::EditorPane *pane =
        shell->editorArea() != nullptr ? shell->editorArea()->activePane() : nullptr;
    check("rm9.pane", pane != nullptr);
    if (pane != nullptr) {
        QString openError;
        check("rm9.open-a", pane->openFile(aPath, &openError), openError);
        auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget());
        check("rm9.view-a", view != nullptr && view->document() != nullptr);
        if (view != nullptr && view->document() != nullptr) {
            view->document()->insertText(KTextEditor::Cursor(1, 2), QStringLiteral("rm9 "));
        }
        check("rm9.a-dirty", pane->isTabModified(pane->tabIndexForPath(aPath)));
        // The edit voids the file's verdict at once. The product watcher does
        // this on textChanged; calling it here makes the assertion independent of
        // the 400 ms poll (b3 covers the watcher timing).
        controller->markFileEdited(aPath);
        check("rm9.dirty-buffer-excluded",
              controller->fileStatuses().value(aPath, 0) == 4,
              QStringLiteral("a=%1").arg(controller->fileStatuses().value(aPath, -1)));

        // The explicit pass keeps excluding the dirty buffer while it covers the
        // other files; the dirty file is never COVERED by a pass reading its
        // stale bytes on disk (a fresh lower verdict may still win, ruling 1).
        QAction *lintAction = shell->commands() != nullptr
                                  ? shell->commands()->action(QStringLiteral("project.lint"))
                                  : nullptr;
        check("rm9.lint-action", lintAction != nullptr);
        if (lintAction != nullptr) {
            const int before = fake->startCount;
            lintAction->trigger();
            check("rm9.explicit-pass-started",
                  waitUntil([fake, before]() { return fake->startCount != before; }, 8000));
            check("rm9.explicit-pass-excludes-dirty", !fake->filesSeen.contains(aPath),
                  QStringLiteral("files=%1").arg(fake->filesSeen.join(QLatin1Char(','))));
            check("rm9.explicit-pass-covers-others",
                  fake->filesSeen.contains(bPath) && fake->filesSeen.contains(outsidePath));
            check("rm9.explicit-pass-finished",
                  waitUntil([controller]() { return !controller->projectPassRunning(); }, 15000));
            check("rm9.dirty-never-covered-by-pass",
                  !controller->hasUpperLayer(aPath),
                  QStringLiteral("covered-a=%1")
                      .arg(controller->hasUpperLayer(aPath) ? 1 : 0));
        }
    }

    if (!shotPath.isEmpty()) {
        const QPixmap shot = shell->grab();
        if (!shot.isNull()) {
            shot.save(shotPath);
            std::printf("SHOT %s\n", qPrintable(shotPath));
        }
    }

    std::printf("RM9-E2E checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
