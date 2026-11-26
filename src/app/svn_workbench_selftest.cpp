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
#include <KTextEditor/View>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFontMetrics>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
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

QWidget *comparisonPage(const app::ApplicationShell &shell)
{
    return shell.findChild<QWidget *>(QStringLiteral("file-comparison"));
}

bool comparisonReady(const app::ApplicationShell &shell)
{
    const auto *page = comparisonPage(shell);
    const auto *status = page ? page->findChild<QLabel *>(QStringLiteral("compare-status")) : nullptr;
    return status && (status->text().startsWith(QStringLiteral("Difference")) || status->text() == QStringLiteral("No differences"));
}

KTextEditor::Document *comparisonDocument(const app::ApplicationShell &shell, const QString &name)
{
    auto *page = comparisonPage(shell);
    auto *view = page ? page->findChild<KTextEditor::View *>(name) : nullptr;
    return view ? view->document() : nullptr;
}

void closeComparison(app::ApplicationShell &shell)
{
    if (comparisonPage(shell)) {
        auto *pane = shell.editorArea()->activePane();
        const int index = pane->activeTabIndex();
        if (pane->isAuxiliaryTab(index)) { pane->closeTab(index); }
    }
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void capture(QWidget &widget, const char *name)
{
    const auto directory = qEnvironmentVariable("VEDIT_SVN_UI_CAPTURE");
    if (!directory.isEmpty()) { check(name, widget.grab().save(directory + u'/' + QString::fromLatin1(name) + QStringLiteral(".png"))); }
}

bool historyButtonFits(const VcsHistoryPanel &history, const QPushButton &button)
{
    QStyleOptionButton option;
    option.initFrom(&button);
    option.text = button.text();
    const QRect contents = button.style()->subElementRect(QStyle::SE_PushButtonContents, &option, &button);
    const int textWidth = button.fontMetrics().horizontalAdvance(button.text());
    std::printf("SVN-HISTORY width=%d button=%d contents=%d text=%d %s\n", history.width(), button.width(),
                contents.width(), textWidth, qPrintable(button.text()));
    return button.isVisible() && button.isEnabled() && contents.width() >= textWidth
        && history.rect().contains(QRect(button.mapTo(&history, QPoint()), button.size()));
}

void checkHistoryCompareLayout(app::ApplicationShell &shell, VcsHistoryPanel &history, bool chinese = false)
{
    auto *commits = history.findChild<QTreeWidget *>(QStringLiteral("vcs-history-commits"));
    const auto *previous = history.findChild<QPushButton *>(QStringLiteral("vcs-history-action-compare"));
    const auto *working = history.findChild<QPushButton *>(QStringLiteral("svn-history-compare-working"));
    auto *dock = shell.panels()->dock(QStringLiteral("vcs"));
    if (!check("svn.ui.history-layout-controls", commits && previous && working && dock && commits->topLevelItemCount() > 0)) { return; }
    const int originalWidth = dock->width();
    commits->setCurrentItem(commits->topLevelItem(commits->topLevelItemCount() - 1));
    const QString previousLabel = chinese ? QStringLiteral("与上次修订比较") : QStringLiteral("Compare With Previous Revision");
    const QString workingLabel = chinese ? QStringLiteral("与工作副本比较") : QStringLiteral("Compare With Working Copy");
    for (const bool narrow : {false, true}) {
        if (narrow) { shell.resizeDocks({dock}, {500}, Qt::Horizontal); }
        QApplication::processEvents();
        const bool previousFits = historyButtonFits(history, *previous);
        const bool workingFits = historyButtonFits(history, *working);
        check("svn.ui.history-compare-previous-fits", previousFits && previous->text() == previousLabel);
        check("svn.ui.history-compare-working-fits", workingFits && working->text() == workingLabel);
        capture(shell, narrow ? "editor-file-history-narrow" : "editor-file-history");
    }
    shell.resizeDocks({dock}, {originalWidth}, Qt::Horizontal);
}

void exerciseActiveEditor(app::ApplicationShell &shell, VcsPanel &panel, VcsHistoryPanel &history,
                          const QString &root, const QString &working, const QString &peer)
{
    const auto file = QStringLiteral("a space.sv");
    const auto activePath = working + u'/' + file;
    const auto plainPath = root + QStringLiteral("/outside.sv");
    auto *historyAction = shell.commands()->action(QStringLiteral("file.svn-history"));
    auto *baseAction = shell.commands()->action(QStringLiteral("file.svn-compare-base"));
    QMenu *fileMenu = nullptr;
    for (auto *menu : shell.menuBar()->findChildren<QMenu *>()) {
        if (menu->actions().contains(historyAction) && menu->actions().contains(baseAction)) { fileMenu = menu; }
    }
    if (!check("svn.ui.editor-file-menu", fileMenu != nullptr)) { return; }

    shell.setProjectRoot(root);
    auto *selector = panel.findChild<QComboBox *>(QStringLiteral("svn-working-copy-selector"));
    if (!check("svn.ui.editor-copy-discovered", selector && until([&] { return selector->findData(peer) >= 0; }))) { return; }
    const int peerIndex = selector->findData(peer);
    selector->setCurrentIndex(peerIndex);
    emit selector->activated(peerIndex);
    if (!check("svn.ui.editor-different-copy-selected", QString::fromStdString(panel.root()) == peer)) { return; }
    auto *pane = shell.editorArea()->activePane();
    if (!check("svn.ui.editor-fixture", put(plainPath, "module outside; endmodule\n") && pane
              && pane->openFile(plainPath) && pane->openFile(activePath))) { return; }
    const int activeIndex = pane->tabIndexForPath(activePath);
    const int plainIndex = pane->tabIndexForPath(plainPath);
    if (!check("svn.ui.editor-file-tabs", activeIndex >= 0 && plainIndex >= 0)) { return; }
    pane->setActiveTab(activeIndex);
    auto *buffer = editor::documentFor(pane->editorWidget());
    if (!check("svn.ui.editor-unsaved-buffer", buffer && buffer->setText(QStringLiteral("module editor_unsaved; endmodule\n")) && buffer->isModified())) { return; }

    if (qEnvironmentVariableIsSet("VEDIT_SVN_UI_CAPTURE")) {
        fileMenu->popup(shell.menuBar()->mapToGlobal(QPoint(0, shell.menuBar()->height())));
        QApplication::processEvents();
        capture(*fileMenu, "editor-svn-file-menu");
        fileMenu->close();
    }

    const auto left = QStringLiteral("compare-left-editor");
    const auto right = QStringLiteral("compare-right-editor");
    const auto hasText = [&](const QString &side, const QString &text) {
        const auto *doc = comparisonDocument(shell, side);
        return doc && doc->text().contains(text);
    };
    baseAction->trigger();
    check("svn.ui.editor-base", until([&] { return comparisonReady(shell); })
          && hasText(left, QStringLiteral("selected_ui")) && comparisonDocument(shell, right) == buffer
          && hasText(right, QStringLiteral("editor_unsaved")) && !comparisonDocument(shell, left)->isReadWrite());
    if (auto *page = comparisonPage(shell)) { capture(*page, "editor-buffer-to-base"); }
    closeComparison(shell);
    pane->setActiveTab(activeIndex);

    historyAction->trigger();
    const auto *path = history.findChild<QLineEdit *>(QStringLiteral("vcs-history-path"));
    check("svn.ui.editor-history", until([&] { return history.commitHashes() == QStringList({QStringLiteral("3"), QStringLiteral("2"), QStringLiteral("1")}); })
          && path && path->text() == file && QString::fromStdString(history.lastRequest().root) == working
          && QString::fromStdString(panel.root()) == peer);
    auto *commits = history.findChild<QTreeWidget *>(QStringLiteral("vcs-history-commits"));
    auto *compareWorking = history.findChild<QPushButton *>(QStringLiteral("svn-history-compare-working"));
    auto *compareSelected = history.findChild<QPushButton *>(QStringLiteral("vcs-history-action-compare"));
    if (!check("svn.ui.editor-history-controls", compareWorking && compareSelected && commits && commits->topLevelItemCount() == 3)) { return; }
    checkHistoryCompareLayout(shell, history);
    commits->setCurrentItem(commits->topLevelItem(2));
    check("svn.ui.editor-compare-working-enabled", compareWorking->isEnabled());
    compareWorking->click();
    check("svn.ui.editor-revision-to-buffer", until([&] { return comparisonReady(shell); })
          && hasText(left, QStringLiteral("base_a")) && comparisonDocument(shell, right) == buffer
          && hasText(right, QStringLiteral("editor_unsaved")));
    closeComparison(shell);
    pane->setActiveTab(activeIndex);

    commits->topLevelItem(1)->setSelected(true);
    compareSelected->click();
    check("svn.ui.editor-revision-pair", until([&] { return comparisonReady(shell); })
          && hasText(left, QStringLiteral("base_a")) && hasText(right, QStringLiteral("selected_ui"))
          && !comparisonDocument(shell, left)->isReadWrite() && !comparisonDocument(shell, right)->isReadWrite());
    if (auto *page = comparisonPage(shell)) { capture(*page, "editor-revision-pair"); }
    closeComparison(shell);
    pane->setActiveTab(activeIndex);

    baseAction->trigger();
    const QString loading = shell.statusBar()->currentMessage();
    pane->setActiveTab(plainIndex);
    check("svn.ui.editor-non-svn-actions", !historyAction->isEnabled() && !baseAction->isEnabled()
          && !compareWorking->isEnabled() && !compareSelected->isEnabled());
    pane->setActiveTab(activeIndex);
    check("svn.ui.editor-reject-stale-base", !loading.isEmpty() && until([&] {
        return shell.statusBar()->currentMessage() != loading;
    }) && !comparisonPage(shell));

    QFile disk(activePath);
    check("svn.ui.editor-buffer-preserved", buffer->isModified() && disk.open(QIODevice::ReadOnly)
          && disk.readAll() == QByteArray("module selected_ui; endmodule\n"));
}

void exercise(const QString &svn, const QString &root, const QString &url, bool chinese)
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
    if (chinese) {
        auto *fileHistory = shell.commands()->action(QStringLiteral("file.svn-history"));
        if (check("svn.ui.zh-history-action", shell.openPaths({first}) && fileHistory && fileHistory->isEnabled()
                  && fileHistory->text() == QStringLiteral("当前文件：SVN 历史"))) {
            fileHistory->trigger();
            if (check("svn.ui.zh-file-history", until([&] { return history->commitCount() == 1; }))) {
                checkHistoryCompareLayout(shell, *history, true);
            }
        }
        return;
    }
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
    closeComparison(shell);

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
    closeComparison(shell);
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
        closeComparison(shell);
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
    exerciseActiveEditor(shell, *panel, *history, root, working, peer);
}

} // namespace

int main(int argc, char **argv)
{
    vedit_runtime_init();
    QApplication app(argc, argv);
    QTranslator chineseCatalog;
    const QString catalogs = qEnvironmentVariable("VEDIT_SVN_ZH_CATALOGS");
    const bool chinese = !catalogs.isEmpty();
    if (chinese && !check("svn.ui.zh-catalog", chineseCatalog.load(QStringLiteral("vedit_zh_CN"), catalogs)
                          && app.installTranslator(&chineseCatalog))) {
        return 1;
    }
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
        exercise(svn, root, url, chinese);
    }
    std::printf("SVN-WORKBENCH failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
