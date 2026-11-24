#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "ktexteditor_provider.h"
#include "panel_registry.h"
#include "runtime_init.h"
#include "vcs_history.h"
#include "vcs_integration.h"
#include "vcs_panel.h"

#include <KTextEditor/Document>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace {
using namespace vedit;
using namespace vedit::workspace::vcs;

int failures = 0;

bool check(const char *label, bool result)
{
    std::printf("CHECK %s %s\n", label, result ? "PASS" : "FAIL");
    failures += !result;
    return result;
}

bool until(const auto &predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 5000) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    return predicate();
}

bool run(const QString &tool, const QString &cwd, const QStringList &args)
{
    QProcess process;
    process.setWorkingDirectory(cwd);
    process.start(tool, args);
    const bool done = process.waitForFinished(5000) && process.exitCode() == 0;
    if (!done) { qWarning("fixture: %s", process.readAllStandardError().constData()); }
    return done;
}

bool put(const QString &path, const QByteArray &text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}

QAction *action(VcsPanel &panel, const char *id)
{
    auto *button = panel.findChild<QPushButton *>(QStringLiteral("vcs-svn-actions"));
    auto *menu = button->menu();
    QMetaObject::invokeMethod(menu, "aboutToShow");
    return menu->findChild<QAction *>(QString::fromLatin1(id));
}

bool trigger(VcsPanel &panel, const char *id)
{
    auto *entry = action(panel, id);
    if (!entry || !entry->isEnabled()) { return false; }
    entry->trigger();
    return true;
}

void answerInput(const QString &response, bool multiline)
{
    QTimer::singleShot(0, qApp, [response, multiline] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        if (!dialog) { return; }
        if (multiline) {
            if (auto *field = dialog->findChild<QPlainTextEdit *>()) { field->setPlainText(response); }
        } else {
            if (auto *field = dialog->findChild<QLineEdit *>()) { field->setText(response); }
        }
        dialog->accept();
    });
}

QDialog *visibleDialog(const QWidget &parent, const QString &title)
{
    for (auto *dialog : parent.findChildren<QDialog *>()) {
        if (dialog->isVisible() && dialog->windowTitle() == title) { return dialog; }
    }
    return nullptr;
}

bool comparisonReady(const app::ApplicationShell &shell)
{
    const auto *dialog = shell.findChild<QDialog *>(QStringLiteral("file-comparison"));
    const auto *status = dialog ? dialog->findChild<QLabel *>(QStringLiteral("compare-status")) : nullptr;
    return status && (status->text().startsWith(QStringLiteral("Difference")) || status->text() == QStringLiteral("No differences"));
}

void capture(QWidget &widget, const char *name)
{
    const auto directory = qEnvironmentVariable("VEDIT_SVN_UI_CAPTURE");
    if (!directory.isEmpty()) { check(name, widget.grab().save(directory + u'/' + QString::fromLatin1(name) + QStringLiteral(".png"))); }
}

void exercise(const QString &svn, const QString &root, const QString &url)
{
    const auto working = root + QStringLiteral("/wc");
    const auto first = working + QStringLiteral("/a space.sv");
    const auto second = working + QStringLiteral("/b.sv");
    app::ShellOptions options;
    options.stateDir = root + QStringLiteral("/state");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    app::ApplicationShell shell(options);
    shell.setEditorProvider(editor::makeEditCoreProvider());
    workspace::installVcs(shell, {});
    shell.startSession();
    shell.setProjectRoot(working);
    shell.resize(1220, 760);
    shell.show();
    shell.panels()->showPanel(QStringLiteral("vcs"), &shell);
    auto *panel = shell.findChild<VcsPanel *>();
    auto *history = shell.findChild<VcsHistoryPanel *>();
    auto *tabs = shell.findChild<QTabWidget *>(QStringLiteral("vcs-tabs"));
    if (!check("svn.ui.ready", panel && history && tabs && until([&] { return panel->fileCount() == 2; }))) { return; }
    auto *changes = panel->findChild<QListWidget *>(QStringLiteral("vcs-changes"));
    check("svn.ui.history-deferred", history->commitCount() == 0);
    panel->setConfirmProvider([](const QString &, const QString &) { return true; });
    const auto done = [&] { return until([&] { return !panel->writeInFlight(); }) && panel->lastWriteOk(); };
    panel->selectFile("a space.sv");
    answerInput(QStringLiteral("UI group"), false);
    check("svn.ui.assign-changelist", trigger(*panel, "svn-changelist") && done() && until([&] {
        return panel->fileText(0).contains(QStringLiteral("UI group")) || panel->fileText(1).contains(QStringLiteral("UI group"));
    }));
    check("svn.ui.open-file", shell.openPaths({first}));
    auto *document = editor::documentFor(shell.editorArea()->activePane()->editorWidget());
    check("svn.ui.editor-gutter", document && until([&] { return (document->mark(0) & KTextEditor::Document::markType27) != 0; }));
    capture(shell, "working-tree-and-gutter");
    check("svn.ui.patch-changelist", trigger(*panel, "svn-patch-changelist") && until([&] {
        const auto *patch = visibleDialog(*panel, QStringLiteral("SVN Patch"));
        if (!patch) { return false; }
        const QString text = patch->findChild<QPlainTextEdit *>()->toPlainText();
        return text.contains(QStringLiteral("selected_ui")) && !text.contains(QStringLiteral("unselected_ui"));
    }));
    if (auto *patch = visibleDialog(*panel, QStringLiteral("SVN Patch"))) {
        capture(*patch, "changelist-patch");
        patch->close();
    }
    answerInput(QStringLiteral("UI selected commit"), true);
    check("svn.ui.commit-selected", trigger(*panel, "svn-commit-selected") && done() && until([&] {
        return changes->count() == 1 && changes->item(0)->text().contains(QStringLiteral("b.sv"));
    }));
    check("svn.ui.gutter-clears-on-commit", document && until([&] { return (document->mark(0) & KTextEditor::Document::markType27) == 0; }));
    panel->selectFile("b.sv");
    bool pickerFilled = false;
    QTimer::singleShot(0, qApp, [&] {
        auto *commit = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        if (!commit) { return; }
        auto *previous = commit->findChild<QPushButton *>(QStringLiteral("svn-previous-message"));
        if (!previous) { commit->reject(); return; }
        auto *poll = new QTimer(commit);
        QObject::connect(poll, &QTimer::timeout, commit, [poll, commit, &pickerFilled] {
            auto *active = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (active && active != commit) {
                active->accept();
            } else if (commit->textValue() == QStringLiteral("UI selected commit")) {
                poll->stop();
                pickerFilled = true;
                commit->reject();
            }
        });
        poll->start(10);
        QTimer::singleShot(4000, commit, &QDialog::reject);
        previous->click();
    });
    check("svn.ui.previous-message-picker", trigger(*panel, "svn-commit-selected") && pickerFilled);

    tabs->setCurrentWidget(history);
    check("svn.ui.repository-log", until([&] { return history->commitCount() == 2; }));
    history->search(QStringLiteral("UI selected"));
    check("svn.ui.text-search", until([&] { return history->commitCount() == 1; }) && history->commitText(0, 3).contains(QStringLiteral("UI selected commit")));
    history->search(QStringLiteral("r1"));
    check("svn.ui.revision-search", until([&] { return history->commitHashes() == QStringList{QStringLiteral("1")}; }));
    capture(shell, "repository-history");
    history->search(QString());
    history->openFileHistory(QStringLiteral("a space.sv"));
    check("svn.ui.file-log", until([&] { return history->commitHashes() == QStringList({QStringLiteral("2"), QStringLiteral("1")}); }));
    capture(shell, "file-history");
    emit history->svnBaseComparisonRequested(QStringLiteral("a space.sv"), 1, 2);
    check("svn.ui.revision-to-base", until([&] {
        return comparisonReady(shell);
    }));
    if (auto *comparison = shell.findChild<QDialog *>(QStringLiteral("file-comparison"))) { comparison->close(); }
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    auto *repositories = history->findChild<QComboBox *>(QStringLiteral("svn-saved-repositories"));
    auto *addRepository = history->findChild<QPushButton *>(QStringLiteral("svn-repository-add"));
    auto *removeRepository = history->findChild<QPushButton *>(QStringLiteral("svn-repository-remove"));
    answerInput(url, false);
    addRepository->click();
    check("svn.ui.add-saved-repository", until([&] { return repositories->currentData().toString() == url && history->commitCount() == 2; }));
    QFile saved(root + QStringLiteral("/state/svn-repositories.json"));
    check("svn.ui.persist-saved-repository", saved.open(QIODevice::ReadOnly) && saved.readAll().contains(url.toUtf8()));
    history->selectCommit(QStringLiteral("2"));
    check("svn.ui.remote-detail", until([&] { return history->detailText().contains(QStringLiteral("/a space.sv")); }));
    emit history->svnRemoteFileRequested(url, QStringLiteral("/a space.sv"), 2, false, false);
    check("svn.ui.open-remote-revision", until([&] {
        const auto *snapshot = visibleDialog(shell, QStringLiteral("/a space.sv at r2"));
        return snapshot && snapshot->findChild<QPlainTextEdit *>()->toPlainText().contains(QStringLiteral("selected_ui"));
    }));
    if (auto *snapshot = visibleDialog(shell, QStringLiteral("/a space.sv at r2"))) {
        capture(*snapshot, "remote-revision"); snapshot->close();
    }
    emit history->svnRemoteFileRequested(url, QStringLiteral("/a space.sv"), 2, false, true);
    check("svn.ui.remote-shared-comparison", until([&] { return comparisonReady(shell); }));
    if (auto *comparison = shell.findChild<QDialog *>(QStringLiteral("file-comparison"))) { comparison->close(); }
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    removeRepository->click();
    check("svn.ui.remove-saved-repository", repositories->count() == 1);

    tabs->setCurrentWidget(panel);
    const auto peer = root + QStringLiteral("/peer");
    check("svn.ui.remote-fixture", run(svn, root, {QStringLiteral("checkout"), QStringLiteral("-q"), url, peer})
          && put(peer + QStringLiteral("/a space.sv"), "module remote_change; endmodule\n")
          && run(svn, peer, {QStringLiteral("commit"), QStringLiteral("-qm"), QStringLiteral("Remote edit")}));
    check("svn.ui.remote-refresh", trigger(*panel, "svn-remote-refresh") && until([&] {
        return visibleDialog(*panel, QStringLiteral("Incoming SVN Changes")) != nullptr;
    }) && panel->headerText().contains(QStringLiteral("1 incoming")));
    if (auto *incoming = visibleDialog(*panel, QStringLiteral("Incoming SVN Changes"))) {
        capture(*incoming, "incoming-changes");
        auto *entries = incoming->findChild<QListWidget *>();
        entries->setCurrentRow(0);
        for (auto *button : incoming->findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("Compare With HEAD")) { button->click(); break; }
        }
        check("svn.ui.incoming-shared-comparison", until([&] {
            return comparisonReady(shell);
        }));
        if (auto *comparison = shell.findChild<QDialog *>(QStringLiteral("file-comparison"))) { comparison->close(); }
        incoming->close();
    }
    auto *checkout = shell.commands()->action(QStringLiteral("file.svn-checkout"));
    if (checkout) {
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) { return; }
            dialog->findChild<QLineEdit *>(QStringLiteral("svn-checkout-url"))->setText(url);
            dialog->findChild<QLineEdit *>(QStringLiteral("svn-checkout-parent"))->setText(root);
            dialog->findChild<QLineEdit *>(QStringLiteral("svn-checkout-name"))->setText(QStringLiteral("new wc"));
            capture(*dialog, "checkout-dialog");
            dialog->accept();
        });
        checkout->trigger();
    }
    check("svn.ui.file-menu-checkout", checkout && until([&] { return shell.projectRoot() == root + QStringLiteral("/new wc"); }));
    check("svn.ui.unselected-edit-on-disk", QFile::exists(second));
}

} // namespace

int main(int argc, char **argv)
{
    vedit_runtime_init();
    QApplication app(argc, argv);
    const auto svn = qEnvironmentVariable("VEDIT_SVN_BIN");
    const auto admin = qEnvironmentVariable("VEDIT_SVNADMIN");
    if (svn.isEmpty() || admin.isEmpty()) { std::puts("SVN-WORKBENCH-SKIP configure VEDIT_SVN_BIN and VEDIT_SVNADMIN"); return 77; }
    qputenv("VEDIT_SVN", svn.toUtf8());
    QTemporaryDir fixture;
    const QString root = fixture.path();
    const auto url = QUrl::fromLocalFile(fixture.filePath(QStringLiteral("repo"))).toString(QUrl::FullyEncoded);
    const auto working = fixture.filePath(QStringLiteral("wc"));
    const auto a = working + QStringLiteral("/a space.sv");
    const auto b = working + QStringLiteral("/b.sv");
    if (check("svn.ui.fixture", fixture.isValid() && run(admin, root, {QStringLiteral("create"), QStringLiteral("repo")})
              && run(svn, root, {QStringLiteral("checkout"), QStringLiteral("-q"), url, working})
              && put(a, "module base_a; endmodule\n") && put(b, "module base_b; endmodule\n")
              && run(svn, working, {QStringLiteral("add"), QStringLiteral("a space.sv"), QStringLiteral("b.sv")})
              && run(svn, working, {QStringLiteral("commit"), QStringLiteral("-qm"), QStringLiteral("Initial files")})
              && put(a, "module selected_ui; endmodule\n") && put(b, "module unselected_ui; endmodule\n"))) {
        exercise(svn, root, url);
    }
    std::printf("SVN-WORKBENCH failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
