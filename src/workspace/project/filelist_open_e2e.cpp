// G1-a end-to-end: opening a `.f` must NEVER import it into the project.
//
// The contract (`.mpd/plans/filelist-file-set-and-duplicate-module.md` §10 and
// AGENT.md "Project mode is filelist-gated") says the project file set is only
// ever widened by an EXPLICIT import. The user hit the opposite: merely OPENING
// a long-forgotten `.f` silently turned it into the project filelist and
// polluted the file set for good.
//
// This binary drives the REAL ApplicationShell, the REAL project lane
// (`installProject`) and the REAL Rust parser, and asserts:
//
//   1. no filelist yet + "Open Only" (the modal's DEFAULT answer) -> the document
//      opens, `filelists()` / `files()` stay empty, the root does not move;
//   2. no filelist yet + "Import" -> the explicit answer, identical to
//      Project ▸ Import Filelist: the root becomes the filelist's directory and
//      the list is imported;
//   3. a project that ALREADY has a filelist -> opening a `.f` leaves
//      `filelists()` and `files()` bit-identical, and the question is not even
//      asked (the injected answer says "Import" and must not leak through);
//   4. the explicit import action still imports.
//
// Check 3 is the regression lock: it FAILS on the old "open == import" code.
//
// Headless: run with QT_QPA_PLATFORM=offscreen (the ctest entry sets it).

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "session_store.h"
#include "project_integration.h"
#include "project_model.h"
#include "project_panel.h"

#include "runtime_init.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <functional>

namespace project = vedit::workspace::project;

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

bool writeFile(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    return file.write(bytes) == bytes.size();
}

bool waitForCompletion(vedit::workspace::project::ProjectModel &model, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (model.isPending() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    return !model.isPending();
}

bool anyContains(const QStringList &values, const QString &needle)
{
    for (const QString &value : values) {
        if (value.contains(needle)) {
            return true;
        }
    }
    return false;
}

QString describe(const QStringList &values)
{
    return QStringLiteral("[%1]").arg(values.join(QLatin1Char(',')));
}

} // namespace

int main(int argc, char **argv)
{
    // T134 / AGENT.md §4: the project lane links the Rust service layer (vd_ffi)
    // through vedit_workspace_project, so install the host's process defaults
    // before Qt and any FFI call.
    vedit_runtime_init();
    QApplication app(argc, argv);

    using Choice = project::FilelistOpenChoice;

    QTemporaryDir temp;
    check("e2e.temp-dir", temp.isValid());
    if (!temp.isValid()) {
        return 1;
    }

    // Two independent project directories, each with its own filelist: the
    // accident under test is "a `.f` from somewhere else imports itself".
    const QString rootA = temp.path() + QStringLiteral("/alpha");
    const QString rootB = temp.path() + QStringLiteral("/beta");
    const QString sourceA = rootA + QStringLiteral("/a.sv");
    const QString sourceA2 = rootA + QStringLiteral("/a2.sv");
    const QString sourceB = rootB + QStringLiteral("/b.sv");
    const QString listA = rootA + QStringLiteral("/sources.f");
    const QString listB = rootB + QStringLiteral("/more.F");
    // A `.f` NO earlier step opened or imported: case 3's regression lock must
    // not be masked by an idempotent re-import of a list case 1 already touched.
    const QString listUnseen = rootA + QStringLiteral("/unseen.f");
    check("e2e.fixture", writeFile(sourceA, QStringLiteral("module a; endmodule\n"))
                             && writeFile(sourceA2, QStringLiteral("module a2; endmodule\n"))
                             && writeFile(sourceB, QStringLiteral("module b; endmodule\n"))
                             && writeFile(listA, QStringLiteral("a.sv\n"))
                             && writeFile(listB, QStringLiteral("b.sv\n"))
                             // `unseen.f` contributes a file the project does NOT
                             // have yet, so BOTH lists move if it is imported.
                             && writeFile(listUnseen, QStringLiteral("a.sv\na2.sv\n")));

    vedit::app::ShellOptions shellOptions;
    shellOptions.stateDir = temp.path() + QStringLiteral("/state");
    shellOptions.restoreSession = false;
    shellOptions.showDefaultPanels = false;
    shellOptions.deferSessionRestore = true;
    vedit::app::ApplicationShell shell(shellOptions);
    shell.show();
    QCoreApplication::processEvents();
    vedit::workspace::installProject(shell, vedit::workspace::IntegrationOptions{});
    shell.startSession();
    QCoreApplication::processEvents();

    project::ProjectModel *model = vedit::workspace::projectModelForShell(shell);
    auto *panel = shell.findChild<project::ProjectPanel *>();
    check("e2e.model", model != nullptr);
    check("e2e.panel", panel != nullptr);
    if (model == nullptr || panel == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    check("e2e.starts-empty", shell.projectRoot().isEmpty() && !model->hasProject()
                                  && model->filelists().isEmpty() && model->files().isEmpty());

    const auto activePath = [&shell]() -> QString {
        return shell.editorArea() != nullptr && shell.editorArea()->activePane() != nullptr
                   ? shell.editorArea()->activePane()->currentFilePath()
                   : QString();
    };

    // 0. No injected answer + no interactive dialogs: the lane answers "Open
    //    Only", so a headless/unattended run can never import by opening a file.
    {
        QTemporaryDir localState;
        project::ProjectModel bareModel(localState.path());
        project::ProjectPanel barePanel(&bareModel);
        barePanel.setInteractiveDialogs(false);
        check("e2e.noninteractive-open-only",
              barePanel.confirmFilelistImport(listA) == Choice::OpenOnly);
    }

    // 1. No filelist yet + "Open Only" (the modal's default button): the
    //    document opens, the project does not appear.
    int openOnlyAsked = 0;
    panel->setFilelistOpenConfirmation([&openOnlyAsked](const QString &) {
        ++openOnlyAsked;
        return Choice::OpenOnly;
    });
    QString error;
    check("e2e.open-only-accepted", shell.openPaths({listA}, &error), error);
    // Let ANY expansion the open may have started settle before reading the
    // answer: the file set arrives asynchronously, so a bare read right after
    // the open would pass even on the old auto-importing code.
    check("e2e.open-only-settled", waitForCompletion(*model, 20000));
    check("e2e.open-only-asked-once", openOnlyAsked == 1,
          QStringLiteral("asked=%1").arg(openOnlyAsked));
    check("e2e.open-only-document-opened", activePath() == QFileInfo(listA).absoluteFilePath(),
          activePath());
    check("e2e.open-only-no-filelist", model->filelists().isEmpty(), describe(model->filelists()));
    check("e2e.open-only-no-files", model->files().isEmpty(), describe(model->files()));
    check("e2e.open-only-root-unchanged",
          shell.projectRoot().isEmpty() && model->projectRoot().isEmpty() && !model->hasProject(),
          QStringLiteral("shell-root=[%1]").arg(shell.projectRoot()));
    check("e2e.open-only-hint", anyContains(shell.diagnostics(), QStringLiteral("Import Filelist")),
          shell.diagnostics().join(QStringLiteral(" | ")));

    // 2. No filelist yet + "Import": the explicit answer (Project ▸ Import
    //    Filelist) — the root becomes the filelist's directory, then the import.
    panel->setFilelistOpenConfirmation(
        [](const QString &) { return Choice::Import; });
    check("e2e.import-accepted", shell.openPaths({listB}, &error), error);
    check("e2e.import-finishes", waitForCompletion(*model, 20000));
    check("e2e.import-root", model->projectRoot() == QFileInfo(rootB).absoluteFilePath()
                                 && shell.projectRoot() == QFileInfo(rootB).absoluteFilePath(),
          QStringLiteral("root=[%1]").arg(model->projectRoot()));
    check("e2e.import-filelist", model->filelists() == QStringList{QFileInfo(listB).absoluteFilePath()},
          describe(model->filelists()));
    check("e2e.import-file-set", model->files().contains(QFileInfo(sourceB).absoluteFilePath()),
          describe(model->files()));

    // 3. THE REGRESSION LOCK: a project that already has a filelist. Opening a
    //    `.f` changes neither list, and the question is never asked — the
    //    injected answer says "Import" and must not reach the lane. The `.f`
    //    opened here is `unseen.f`, which no earlier step touched, so the old
    //    auto-import cannot hide behind the importer's own idempotence.
    const QStringList filelistsBefore = model->filelists();
    const QStringList filesBefore = model->files();
    const QString rootBefore = model->projectRoot();
    int existingAsked = 0;
    panel->setFilelistOpenConfirmation([&existingAsked](const QString &) {
        ++existingAsked;
        return Choice::Import;
    });
    check("e2e.existing-open-accepted", shell.openPaths({listUnseen}, &error), error);
    check("e2e.existing-settled", waitForCompletion(*model, 20000));
    check("e2e.existing-no-question", existingAsked == 0,
          QStringLiteral("asked=%1").arg(existingAsked));
    check("e2e.existing-document-opened",
          activePath() == QFileInfo(listUnseen).absoluteFilePath(), activePath());
    check("e2e.existing-filelists-unchanged", model->filelists() == filelistsBefore,
          QStringLiteral("before=%1 after=%2")
              .arg(describe(filelistsBefore), describe(model->filelists())));
    check("e2e.existing-files-unchanged", model->files() == filesBefore,
          QStringLiteral("before=%1 after=%2")
              .arg(describe(filesBefore), describe(model->files())));
    check("e2e.existing-root-unchanged", model->projectRoot() == rootBefore,
          QStringLiteral("root=[%1]").arg(model->projectRoot()));
    check("e2e.existing-hint", anyContains(shell.diagnostics(), QStringLiteral("unseen.f")),
          shell.diagnostics().join(QStringLiteral(" | ")));

    // 4. The EXPLICIT import action is unchanged: the same `.f` imported through
    //    the panel (the Import Filelist button) does land in the project.
    check("e2e.explicit-import", panel->importFilelists({listA}));
    check("e2e.explicit-finishes", waitForCompletion(*model, 20000));
    check("e2e.explicit-filelist-added",
          model->filelists().contains(QFileInfo(listA).absoluteFilePath()),
          describe(model->filelists()));
    check("e2e.explicit-file-set",
          model->files().contains(QFileInfo(sourceA).absoluteFilePath()),
          describe(model->files()));

    check("workspace.beta-include-authored", model->addIncludeDir(QStringLiteral("inc"), &error), error);
    check("workspace.beta-define-authored", model->addDefine(QStringLiteral("BETA=1"), &error), error);
    model->setTop(QStringLiteral("b"));
    check("workspace.beta-settled", waitForCompletion(*model, 20000));
    auto *dirty = qobject_cast<QPlainTextEdit *>(shell.editorArea()->activePane()->editorWidget());
    check("workspace.dirty-editor", dirty != nullptr);
    if (dirty != nullptr) {
        dirty->insertPlainText(QStringLiteral("// unsaved"));
    }
    const auto betaLists = model->filelists();
    const auto betaExpansions = model->expansionCount();
    shell.setProjectRoot(rootA);
    check("workspace.alpha-isolated", model->projectRoot() == rootA && !model->hasFilelist()
          && model->projectIncludeDirs().isEmpty() && model->projectDefines().isEmpty()
          && model->top().isEmpty() && model->expansionCount() == betaExpansions);
    check("workspace.switch-keeps-dirty", dirty != nullptr && shell.editorArea()->activePane()->isModified()
          && dirty->toPlainText().contains(QStringLiteral("// unsaved")));
    check("workspace.alpha-import", model->importFilelists({listA}, &error), error);
    check("workspace.alpha-define", model->addDefine(QStringLiteral("ALPHA=1"), &error), error);
    check("workspace.alpha-settled", waitForCompletion(*model, 20000));
    shell.setProjectRoot(rootB);
    check("workspace.beta-return", waitForCompletion(*model, 20000)
          && model->filelists() == betaLists && model->top() == QStringLiteral("b")
          && model->projectIncludeDirs() == QStringList{QStringLiteral("inc")}
          && model->projectDefines() == QStringList{QStringLiteral("BETA=1")});
    check("workspace.recent-deduplicates", shell.sessionStore()->recentFolders()
          == QStringList{rootB, rootA});

    check("workspace.seed-archive", shell.saveSession(&error), error);
    vedit::app::ShellOptions launch = shellOptions;
    launch.restoreSession = true;
    launch.openStartupFiles = true;
    {
        vedit::app::ApplicationShell standalone(launch);
        vedit::workspace::installProject(standalone, {});
        standalone.startSession();
        auto *restored = vedit::workspace::projectModelForShell(standalone);
        check("workspace.standalone-skips-archived-tabs",
              standalone.editorArea()->activePane()->currentFilePath().isEmpty());
        standalone.setOpenFileDialogProvider([sourceA]() { return sourceA; });
        standalone.commands()->action(QStringLiteral("file.open"))->trigger();
        check("workspace.file-open-standalone", standalone.projectRoot().isEmpty()
              && standalone.editorArea()->activePane()->currentFilePath() == sourceA
              && restored->projectRoot().isEmpty() && !restored->hasFilelist()
              && restored->expansionCount() == 0);
    }
    launch.openStartupFiles = false;
    {
        vedit::app::ApplicationShell reopened(launch);
        vedit::workspace::installProject(reopened, {});
        reopened.startSession();
        auto *restored = vedit::workspace::projectModelForShell(reopened);
        check("workspace.restore-beta", reopened.projectRoot() == rootB
              && restored->filelists() == betaLists && waitForCompletion(*restored, 20000));
        auto *recent = reopened.findChild<QMenu *>(QStringLiteral("menu-recent-folders"));
        check("workspace.recent-menu", recent != nullptr);
        if (recent != nullptr) {
            recent->popup(QPoint(80, 80));
            const auto actions = recent->actions();
            check("workspace.recent-list", actions.size() == 2 && actions.last()->text() == rootA);
            if (actions.size() == 2) {
                actions.last()->trigger();
            }
            recent->hide();
        }
        check("workspace.recent-select-restores-alpha", reopened.projectRoot() == rootA
              && restored->filelists() == QStringList{listA}
              && restored->projectDefines() == QStringList{QStringLiteral("ALPHA=1")}
              && waitForCompletion(*restored, 20000));
        auto *restore = reopened.findChild<QAction *>(QStringLiteral("action-restore-last-folder"));
        check("workspace.restore-toggle-on", restore != nullptr && restore->isChecked());
        if (restore != nullptr) {
            restore->trigger();
        }
        reopened.commands()->action(QStringLiteral("file.close-folder"))->trigger();
        check("workspace.close-folder-clears-model", reopened.projectRoot().isEmpty()
              && restored->projectRoot().isEmpty() && !restored->hasFilelist()
              && !restored->isPending() && reopened.sessionStore()->lastFolder().isEmpty()
              && reopened.sessionStore()->recentFolders() == QStringList{rootA, rootB});
    }
    vedit::app::ApplicationShell restoreDisabled(launch);
    check("workspace.restore-toggle-persisted", !restoreDisabled.sessionStore()->restoresLastFolder());
    restoreDisabled.setProjectRoot(rootB);
    vedit::app::ApplicationShell emptyLaunch(launch);
    check("workspace.restore-disabled-leaves-root-empty", emptyLaunch.projectRoot().isEmpty());
    launch.projectRoot = rootA;
    launch.openStartupFiles = true;
    vedit::app::ApplicationShell explicitLaunch(launch);
    check("workspace.explicit-folder-with-file", explicitLaunch.projectRoot() == rootA);

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
