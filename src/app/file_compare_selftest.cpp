#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "file_compare.h"
#include "ktexteditor_provider.h"
#include "panel_registry.h"
#include "runtime_init.h"
#include "vcs_history.h"
#include "vcs_integration.h"
#include "vcs_panel.h"

#include <KTextEditor/Document>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <QUrl>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using namespace vedit;
using namespace vedit::workspace::vcs;

int failures = 0;

bool check(const char *name, bool passed)
{
    std::printf("CHECK %s %s\n", name, passed ? "PASS" : "FAIL");
    failures += !passed;
    return passed;
}

bool until(const auto &ready)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 6000) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    return ready();
}

bool put(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray get(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QDialog *dialog(app::ApplicationShell &shell)
{
    return shell.findChild<QDialog *>(QStringLiteral("file-comparison"));
}

QPushButton *button(QDialog *comparison, const char *name)
{
    return comparison->findChild<QPushButton *>(QString::fromLatin1(name));
}

QString status(QDialog *comparison)
{
    return comparison->findChild<QLabel *>(QStringLiteral("compare-status"))->text();
}

bool diffReady(QDialog *comparison)
{
    return comparison && until([&] { return status(comparison) != QStringLiteral("Comparing…"); })
        && (status(comparison).startsWith(QStringLiteral("Difference")) || status(comparison) == QStringLiteral("No differences"));
}

bool shows(QDialog *comparison, const QString &text)
{
    if (!comparison) {
        return false;
    }
    for (auto *label : comparison->findChildren<QLabel *>()) {
        if (label->text().contains(text)) {
            return true;
        }
    }
    return false;
}

void close(app::ApplicationShell &shell, QDialog *comparison)
{
    comparison->accept();
    until([&] { return !dialog(shell); });
}

bool compareThroughFileMenu(app::ApplicationShell &shell, const QStringList &files)
{
    auto *menu = shell.findChild<QMenu *>(QStringLiteral("menu-file"));
    auto *action = shell.commands()->action(QStringLiteral("file.compare"));
    if (!menu || !action || !menu->actions().contains(action)) {
        return false;
    }
    int chosen = 0;
    QTimer selectFiles;
    QObject::connect(&selectFiles, &QTimer::timeout, &shell, [&] {
        if (auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            chooser && chosen < files.size()) {
            auto *name = chooser->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
            if (!name) {
                return;
            }
            name->setText(files.at(chosen));
            QMetaObject::invokeMethod(chooser, "accept");
            if (chooser->result() == QDialog::Accepted) {
                ++chosen;
            }
        }
    });
    bool timedOut = false;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &shell, [&] {
        timedOut = true;
        if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            modal->reject();
        }
    });
    selectFiles.start(10);
    deadline.start(3000);
    action->trigger();
    return !timedOut && chosen == files.size() && dialog(shell);
}

bool sameFileState(const struct stat &before, const struct stat &after)
{
    return before.st_dev == after.st_dev && before.st_ino == after.st_ino
        && before.st_nlink == after.st_nlink && before.st_size == after.st_size
        && before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec;
}

bool fileState(const QString &path, struct stat &state)
{
    return ::stat(QFile::encodeName(path).constData(), &state) == 0;
}

void invalidComparison(app::ApplicationShell &shell, const QString &directory)
{
    const QString valid = directory + QStringLiteral("/readable.txt");
    const QByteArray validBytes = "readable\n";
    struct stat validBefore {};
    if (!check("invalid.valid-peer-fixture", put(valid, validBytes) && fileState(valid, validBefore))) {
        return;
    }
    const QString binary = directory + QStringLiteral("/contains-nul.bin");
    const QString alias = directory + QStringLiteral("/same-binary-hard-link.bin");
    const QString utf8 = directory + QStringLiteral("/invalid-utf8.txt");
    const QString oversized = directory + QStringLiteral("/oversized.txt");
    const QString manyLines = directory + QStringLiteral("/too-many-lines.txt");
    const QString missing = directory + QStringLiteral("/missing.txt");
    const QByteArray binaryBytes("before\0after\n", 13);
    const QByteArray invalidUtf8 = QByteArray::fromHex("76616c696420c3");
    const QByteArray oversizedBytes(8 * 1024 * 1024 + 1, 'x');
    const QByteArray manyLinesBytes(100000, '\n');
    struct stat aliasBefore {};
    if (!check("invalid.binary-is-writable-hardlink", put(binary, binaryBytes) && QFileInfo(binary).isWritable()
          && ::link(QFile::encodeName(binary).constData(), QFile::encodeName(alias).constData()) == 0
          && fileState(alias, aliasBefore) && aliasBefore.st_nlink == 2
          && put(utf8, invalidUtf8) && put(oversized, oversizedBytes) && put(manyLines, manyLinesBytes))) {
        return;
    }
    struct InvalidInput {
        const char *name;
        QString path;
        QByteArray bytes;
        QString error;
        bool exists = true;
    };
    const QString unsupported = QCoreApplication::translate("FileComparison", "%1 is binary or uses an unsupported encoding. Comparison requires UTF-8 text.");
    const QList<InvalidInput> invalids{
        {"nul-hardlink", binary, binaryBytes, unsupported.arg(binary)},
        {"invalid-utf8", utf8, invalidUtf8, unsupported.arg(utf8)},
        {"size-limit", oversized, oversizedBytes, QCoreApplication::translate("FileComparison", "%1 exceeds the 8 MiB text comparison limit.").arg(oversized)},
        {"line-limit", manyLines, manyLinesBytes, QCoreApplication::translate("FileComparison", "%1 exceeds the text comparison limit (8 MiB or 100,000 lines).").arg(manyLines)},
        {"read-failure", missing, {}, QCoreApplication::translate("FileComparison", "Cannot read %1.").arg(missing), false},
    };
    for (const auto &invalid : invalids) {
        struct stat before {};
        if (invalid.exists && !fileState(invalid.path, before)) {
            check("invalid.file-stat", false);
            continue;
        }
        for (const bool invalidOnRight : {true, false}) {
            if (invalid.path == binary && invalidOnRight) {
                check("invalid.nul-hardlink.file-menu", compareThroughFileMenu(shell, {valid, binary}));
            } else {
                const app::CompareInput readable{valid, {}, {}, false};
                const app::CompareInput unreadable{invalid.path, {}, {}, false};
                app::showFileComparison(shell, invalidOnRight ? readable : unreadable,
                                       invalidOnRight ? unreadable : readable);
            }
            auto *comparison = dialog(shell);
            const QByteArray testName = QByteArray("invalid.") + invalid.name + (invalidOnRight ? ".right" : ".left");
            if (!check((testName + ".diagnostic").constData(), comparison && status(comparison).contains(invalid.error))) {
                if (comparison) {
                    close(shell, comparison);
                }
                continue;
            }
            auto *saveLeft = button(comparison, "compare-save-left");
            auto *saveRight = button(comparison, "compare-save-right");
            check((testName + ".clean-and-immovable").constData(), !shows(comparison, QStringLiteral("  *"))
                && !saveLeft->isEnabled() && !saveRight->isEnabled()
                && !button(comparison, "compare-apply-left")->isEnabled()
                && !button(comparison, "compare-apply-right")->isEnabled()
                && !button(comparison, "compare-undo")->isEnabled());
            saveLeft->click();
            saveRight->click();
            bool prompted = false;
            QTimer closeProbe;
            QObject::connect(&closeProbe, &QTimer::timeout, comparison, [&] {
                if (auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    prompted = true;
                    prompt->button(QMessageBox::Discard)->click();
                }
            });
            closeProbe.start(10);
            comparison->close();
            check((testName + ".closes-without-discard").constData(), until([&] { return !dialog(shell); }) && !prompted);
            struct stat validAfter {}, after {}, aliasAfter {};
            const bool invalidUntouched = invalid.exists
                ? fileState(invalid.path, after) && sameFileState(before, after) && get(invalid.path) == invalid.bytes
                : !QFileInfo::exists(invalid.path);
            const bool hardLinkIntact = invalid.path != binary
                || (fileState(alias, aliasAfter) && sameFileState(aliasBefore, aliasAfter)
                    && aliasAfter.st_ino == after.st_ino && get(alias) == binaryBytes);
            check((testName + ".bytes-inode-mtime-links").constData(), invalidUntouched && hardLinkIntact
                && fileState(valid, validAfter) && sameFileState(validBefore, validAfter) && get(valid) == validBytes);
        }
    }
}

void localComparison(app::ApplicationShell &shell, const QString &directory)
{
    const QString left = directory + QStringLiteral("/left @ 中文.txt");
    const QString right = directory + QStringLiteral("/right.txt");
    const QByteArray leftDisk = "head\r\nleft\r\nanchor\r\nold\r\nend";
    const QByteArray rightDisk = "head\nright\nextra\nanchor\nend\n";
    if (!check("local.fixture", put(left, leftDisk) && put(right, rightDisk) && shell.openPaths({left}))) {
        return;
    }
    check("local.file-menu-command", shell.commands()->action(QStringLiteral("file.compare")) != nullptr);
    auto *document = editor::documentFor(shell.editorArea()->activePane()->editorWidget());
    if (!check("local.ktexteditor-buffer", document && document->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("unsaved 中文\n")))) {
        return;
    }
    const QString before = document->text();
    app::showFileComparison(shell, {left, {}, {}, false}, {right, {}, {}, false});
    auto *comparison = dialog(shell);
    if (!check("local.diff-ready", diffReady(comparison))) {
        if (comparison) {
            qWarning("comparison: %s", qPrintable(status(comparison)));
            close(shell, comparison);
        }
        return;
    }
    const auto *rows = comparison->findChild<QTableView *>()->model();
    bool aligned = false;
    for (int row = 0; row < rows->rowCount(); ++row) {
        aligned |= rows->index(row, 0).data().toString().contains(QStringLiteral("anchor"))
            && rows->index(row, 2).data().toString().contains(QStringLiteral("anchor"));
    }
    check("local.aligned-rows", aligned && rows->rowCount() >= 7);
    if (const auto output = qEnvironmentVariable("VEDIT_COMPARE_SCREENSHOT"); !output.isEmpty()) {
        check("local.capture", comparison->grab().save(output));
    }
    button(comparison, "compare-next")->click();
    button(comparison, "compare-apply-left")->click();
    check("local.apply-left-stays-in-buffer", diffReady(comparison) && document->isModified()
          && document->text().contains(QStringLiteral("right\nextra")) && get(left) == leftDisk);
    button(comparison, "compare-undo")->click();
    check("local.undo-retains-prior-unsaved-buffer", diffReady(comparison) && document->isModified() && document->text() == before);
    button(comparison, "compare-previous")->click();
    button(comparison, "compare-apply-right")->click();
    check("local.apply-right-stays-in-buffer", diffReady(comparison) && get(right) == rightDisk);
    button(comparison, "compare-save-right")->click();
    check("local.save-selected-only", get(left) == leftDisk && get(right).startsWith("unsaved ") && get(right).endsWith(rightDisk));
    close(shell, comparison);

    app::showFileComparison(shell, {left, {}, {}, false}, {{}, QStringLiteral("Repository snapshot"), QByteArray("unsaved \xe4\xb8\xad\xe6\x96\x87\nhead\nleft\nanchor\nnew end\n"), false});
    comparison = dialog(shell);
    if (check("local.readonly-snapshot", diffReady(comparison) && !button(comparison, "compare-apply-right")->isEnabled())) {
        button(comparison, "compare-apply-left")->click();
        check("local.apply-eof-to-open-buffer", diffReady(comparison) && document->text().endsWith(QStringLiteral("new end\n")));
        button(comparison, "compare-save-left")->click();
        check("local.save-crlf-and-final-newline", get(left).contains("\r\nnew end\r\n") && !document->isModified());
        close(shell, comparison);
    }

    app::showFileComparison(shell, {left, {}, {}, false}, {{}, QStringLiteral("Invalid UTF-8"), QByteArray::fromHex("c3"), false});
    comparison = dialog(shell);
    if (check("local.invalid-encoding", comparison && status(comparison).contains(QStringLiteral("unsupported encoding")))) {
        close(shell, comparison);
    }

    const QString empty = directory + QStringLiteral("/empty.txt");
    if (!check("local.empty-fixture", put(empty, {}))) {
        return;
    }
    app::showFileComparison(shell, {empty, {}, {}, false}, {{}, QStringLiteral("Snapshot"), "one\n", false});
    comparison = dialog(shell);
    if (!check("local.empty-file-vs-insert", diffReady(comparison))) {
        return;
    }
    button(comparison, "compare-apply-left")->click();
    check("local.pure-insert-alignment", diffReady(comparison) && status(comparison) == QStringLiteral("No differences"));
    check("local.external-change-fixture", put(empty, "outside\n"));
    button(comparison, "compare-save-left")->click();
    check("local.external-change-refuses-save", get(empty) == "outside\n" && status(comparison).contains(QStringLiteral("changed outside")));
    close(shell, comparison);

    app::showFileComparison(shell, {empty, {}, {}, false}, {{}, QStringLiteral("Empty snapshot"), {}, false});
    comparison = dialog(shell);
    if (!check("local.pure-delete-ready", diffReady(comparison))) {
        return;
    }
    button(comparison, "compare-apply-left")->click();
    if (diffReady(comparison)) {
        button(comparison, "compare-save-left")->click();
    }
    check("local.pure-delete-saves-empty", get(empty).isEmpty() && status(comparison).startsWith(QStringLiteral("Saved")));
    close(shell, comparison);

    check("local.readonly-fixture", QFile::setPermissions(empty, QFileDevice::ReadOwner));
    app::showFileComparison(shell, {empty, {}, {}, false}, {{}, QStringLiteral("Snapshot"), "one\n", false});
    comparison = dialog(shell);
    if (check("local.readonly-disk-blocks-patch", diffReady(comparison) && !button(comparison, "compare-apply-left")->isEnabled())) {
        close(shell, comparison);
    }

    const QString beforeDiscard = document->text();
    app::showFileComparison(shell, {left, {}, {}, false}, {{}, QStringLiteral("Snapshot"), "different\n", false});
    comparison = dialog(shell);
    if (diffReady(comparison)) {
        button(comparison, "compare-apply-left")->click();
        const bool modified = diffReady(comparison) && document->isModified() && document->text() != beforeDiscard;
        QTimer::singleShot(0, [] {
            auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (prompt) {
                prompt->button(QMessageBox::Discard)->click();
            }
        });
        QMetaObject::invokeMethod(comparison, "reject");
        check("local.discard-restores-editor-buffer", modified && document->text() == beforeDiscard && !document->isModified());
        until([&] { return !dialog(shell); });
    }
}

bool command(const QString &program, const QStringList &args)
{
    QProcess process;
    process.start(program, args);
    const bool ok = process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!ok) {
        qWarning("%s: %s", qPrintable(program), process.readAllStandardError().constData());
    }
    return ok;
}

void svnComparison(const QString &directory)
{
    const QString svn = qEnvironmentVariable("VEDIT_SVN_BIN");
    const QString admin = qEnvironmentVariable("VEDIT_SVNADMIN");
    if (svn.isEmpty() || admin.isEmpty()) {
        std::puts("CHECK svn.fixture SKIP set VEDIT_SVN_BIN and VEDIT_SVNADMIN");
        return;
    }
    qputenv("VEDIT_SVN", QFile::encodeName(svn));
    const QString repository = directory + QStringLiteral("/repository");
    const QString wc = directory + QStringLiteral("/working-copy");
    const QString original = QStringLiteral("original @ 中文.txt");
    const QString renamed = QStringLiteral("renamed @ 中文.txt");
    const QString oldPath = wc + QLatin1Char('/') + original;
    const QString path = wc + QLatin1Char('/') + renamed;
    const QString marker = wc + QStringLiteral("/unrelated");
    const auto commit = [&](const QString &message) { return command(svn, {QStringLiteral("commit"), QStringLiteral("-m"), message, wc}); };
    const auto add = [&](const QString &file) { return command(svn, {QStringLiteral("add"), QStringLiteral("--"), file + QLatin1Char('@')}); };
    if (!check("svn.fixture", command(admin, {QStringLiteral("create"), repository})
        && command(svn, {QStringLiteral("checkout"), QUrl::fromLocalFile(repository).toString(), wc})
        && put(oldPath, "first\r\nanchor\r\n") && add(oldPath) && commit(QStringLiteral("file first added"))
        && put(marker, "unrelated") && add(marker) && commit(QStringLiteral("other file only"))
        && put(oldPath, "third\r\nanchor\r\n") && commit(QStringLiteral("file changed"))
        && command(svn, {QStringLiteral("move"), QStringLiteral("--"), oldPath + QLatin1Char('@'), path})
        && commit(QStringLiteral("file renamed")) && put(path, "working\r\nanchor\r\n"))) {
        return;
    }

    app::ShellOptions options;
    options.stateDir = directory + QStringLiteral("/svn-state");
    options.projectRoot = wc;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    app::ApplicationShell shell(options);
    workspace::installVcs(shell, {});
    shell.panels()->showPanel(QStringLiteral("vcs"), &shell);
    shell.show();
    auto *history = shell.findChild<VcsHistoryPanel *>();
    auto *panel = shell.findChild<VcsPanel *>();
    if (!check("svn.panel.reachable", history && panel && until([&] { return panel->backend() == Backend::Subversion; }))) {
        return;
    }
    history->openFileHistory(renamed);
    if (!check("svn.file-history-skips-unrelated-and-follows-copy", until([&] {
            return history->commitHashes() == QStringList{QStringLiteral("4"), QStringLiteral("3"), QStringLiteral("1")};
        }))) {
        qWarning("svn history: %s, error: %s", qPrintable(history->commitHashes().join(QLatin1Char(','))), qPrintable(history->errorText()));
        return;
    }
    auto *rows = history->findChild<QTreeWidget *>(QStringLiteral("vcs-history-commits"));
    auto *compare = history->findChild<QPushButton *>(QStringLiteral("vcs-history-action-compare"));
    const auto select = [&](int row) {
        rows->clearSelection();
        rows->setCurrentItem(rows->topLevelItem(row));
    };
    const auto awaitComparison = [&]() {
        until([&] { return dialog(shell); });
        auto *item = dialog(shell);
        return diffReady(item) ? item : nullptr;
    };
    select(1);
    compare->click();
    history->findChild<QLineEdit *>(QStringLiteral("vcs-history-path"))->setText(original);
    check("svn.stale-file-selection-discards-result", !until([&] { return dialog(shell); }));
    history->findChild<QLineEdit *>(QStringLiteral("vcs-history-path"))->setText(renamed);
    compare->click();
    auto *comparison = awaitComparison();
    if (check("svn.previous-actual-revision", shows(comparison, QStringLiteral("r1")) && shows(comparison, QStringLiteral("r3"))
            && !button(comparison, "compare-apply-left")->isEnabled() && !button(comparison, "compare-apply-right")->isEnabled())) {
        if (const auto output = qEnvironmentVariable("VEDIT_COMPARE_SVN_SCREENSHOT"); !output.isEmpty()) {
            check("svn.capture", comparison->grab().save(output));
        }
        close(shell, comparison);
    }
    select(0);
    rows->topLevelItem(2)->setSelected(true);
    compare->click();
    comparison = awaitComparison();
    if (check("svn.two-selected-revisions", shows(comparison, QStringLiteral("r1")) && shows(comparison, QStringLiteral("r4")))) {
        close(shell, comparison);
    }
    QTimer::singleShot(0, [] {
        if (auto *input = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
            input->setTextValue(QStringLiteral("r2, r1"));
            input->accept();
        }
    });
    history->findChild<QPushButton *>(QStringLiteral("svn-history-compare-revisions"))->click();
    comparison = awaitComparison();
    if (check("svn.arbitrary-revisions-can-include-unrelated-commit", shows(comparison, QStringLiteral("r2"))
              && shows(comparison, QStringLiteral("r1")) && status(comparison) == QStringLiteral("No differences"))) {
        close(shell, comparison);
    }
    select(2);
    compare->click();
    comparison = awaitComparison();
    if (check("svn.first-add-has-absent-side", shows(comparison, QStringLiteral("r0")) && shows(comparison, QStringLiteral("Absent")))) {
        close(shell, comparison);
    }
    select(0);
    history->findChild<QPushButton *>(QStringLiteral("svn-history-compare-working"))->click();
    comparison = awaitComparison();
    if (check("svn.selected-revision-to-working-copy", shows(comparison, QStringLiteral("r4")) && shows(comparison, QStringLiteral("Working Copy")))) {
        const QByteArray before = get(path);
        button(comparison, "compare-apply-right")->click();
        check("svn.restore-hunk-excludes-disk-until-save", diffReady(comparison) && get(path) == before);
        button(comparison, "compare-undo")->click();
        check("svn.restore-undo", diffReady(comparison) && button(comparison, "compare-apply-right")->isEnabled());
        button(comparison, "compare-apply-right")->click();
        if (diffReady(comparison)) {
            button(comparison, "compare-save-right")->click();
        }
        check("svn.restore-explicit-save", get(path) == "third\r\nanchor\r\n");
        close(shell, comparison);
    }
    put(path, "working\r\nanchor\r\n");
    panel->refresh();
    if (check("svn.working-tree-selection", until([&] {
            panel->selectFile(renamed.toStdString());
            return panel->selectedPath() == renamed;
        }))) {
        auto *menu = panel->findChild<QPushButton *>(QStringLiteral("vcs-svn-compare"))->menu();
        for (const auto *choice : {"BASE", "HEAD"}) {
            for (auto *action : menu->actions()) {
                if (action->text() == QString::fromLatin1(choice)) {
                    action->trigger();
                    comparison = awaitComparison();
                    if (check(choice, shows(comparison, QString::fromLatin1(choice)) && shows(comparison, QStringLiteral("Working Copy")))) {
                        close(shell, comparison);
                    }
                    break;
                }
            }
        }
    }
    history->openFileHistory(original);
    const bool deletedHistory = until([&] {
        return !history->loadingStateVisible()
            && history->commitHashes() == QStringList{QStringLiteral("4"), QStringLiteral("3"), QStringLiteral("1")};
    });
    if (!deletedHistory) {
        qWarning("deleted history: rows='%s', error='%s', loading=%d", qPrintable(history->commitHashes().join(QLatin1Char(','))),
                 qPrintable(history->errorText()), history->loadingStateVisible());
    }
    if (check("svn.deleted-file-history", deletedHistory)) {
        select(0);
        compare->click();
        comparison = awaitComparison();
        if (!check("svn.deleted-revision-is-absent", shows(comparison, QStringLiteral("r4")) && shows(comparison, QStringLiteral("Absent")))) {
            qWarning("deleted comparison action enabled=%d, shell status='%s'", compare->isEnabled(), qPrintable(shell.statusBar()->currentMessage()));
            if (comparison) {
                for (const auto *label : comparison->findChildren<QLabel *>()) {
                    qWarning("comparison label: %s", qPrintable(label->text()));
                }
            }
        }
        if (comparison) {
            close(shell, comparison);
        }
        compare->click();
        check("svn.root-switch-fixture", shell.projectRoot() == wc);
        shell.setProjectRoot(directory);
        check("svn.stale-root-discards-result", !until([&] { return dialog(shell); }));
    }
}
bool activeDiffDestroyed = false;

void teardownMessageHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    if (message.startsWith(QStringLiteral("QProcess: Destroyed while process"))) {
        activeDiffDestroyed = true;
    }
    std::fprintf(stderr, "%s\n", qPrintable(message));
}

void activeDiffTeardown(const QString &directory, bool quitApplication)
{
    const auto originalHandler = qInstallMessageHandler(teardownMessageHandler);
    const auto restoreHandler = qScopeGuard([&] { qInstallMessageHandler(originalHandler); });
    const QString realDiff = QStandardPaths::findExecutable(QStringLiteral("diff"));
    const QString wrapper = directory + QStringLiteral("/diff");
    const QString started = directory + QStringLiteral("/diff-started");
    const QByteArray script = "#!/bin/bash\nprintf started > \"$VEDIT_TEST_DIFF_STARTED\"\n"
                              "read -r -t 30 gate\nexec \"$VEDIT_TEST_REAL_DIFF\" \"$@\"\n";
    if (!check("teardown.delayed-diff-wrapper", !realDiff.isEmpty() && put(wrapper, script)
        && QFile::setPermissions(wrapper, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))) {
        return;
    }
    const QByteArray path = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(directory) + ':' + path);
    qputenv("VEDIT_TEST_DIFF_STARTED", QFile::encodeName(started));
    qputenv("VEDIT_TEST_REAL_DIFF", QFile::encodeName(realDiff));
    const auto restorePath = qScopeGuard([&] {
        qputenv("PATH", path);
        qunsetenv("VEDIT_TEST_DIFF_STARTED");
        qunsetenv("VEDIT_TEST_REAL_DIFF");
    });
    qint64 pid = 0;
    QStringList inputs;
    bool opened = false;
    {
        app::ShellOptions options;
        options.stateDir = directory + QStringLiteral("/teardown-state");
        options.restoreSession = false;
        options.showDefaultPanels = false;
        app::ApplicationShell shell(options);
        shell.show();
        app::showFileComparison(shell, {{}, QStringLiteral("left"), "before\n", false},
                                {{}, QStringLiteral("right"), "after\n", false});
        auto *comparison = dialog(shell);
        auto *process = comparison ? comparison->findChild<QProcess *>() : nullptr;
        if (!check("teardown.child-is-running", process && until([&] {
            return process->state() == QProcess::Running && QFileInfo::exists(started);
        }))) {
            return;
        }
        pid = process->processId();
        inputs = process->arguments().last(2);
        opened = check("teardown.diff-inputs-exist", pid > 0 && inputs.size() == 2
            && QFileInfo::exists(inputs.front()) && QFileInfo::exists(inputs.back()));
        std::fflush(stdout);
        if (quitApplication) {
            QTimer::singleShot(0, [] { QCoreApplication::exit(); });
            check("teardown.application-event-loop-exited", QCoreApplication::exec() == 0);
            check("teardown.child-still-active-after-event-loop", process->state() == QProcess::Running);
        } else {
            comparison->close();
            check("teardown.dialog-destroyed", until([&] { return !dialog(shell); }));
        }
    }
    check("teardown.process-stopped-before-destruction", !activeDiffDestroyed);
    if (opened) {
        check("teardown.child-killed-and-reaped", ::kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH);
        check("teardown.temp-inputs-removed", !QFileInfo::exists(inputs.front()) && !QFileInfo::exists(inputs.back())
            && !QFileInfo::exists(QFileInfo(inputs.front()).absolutePath()));
    }
}
} // namespace

int main(int argc, char *argv[])
{
    vedit_runtime_init();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    QTemporaryDir fixture;
    if (!check("comparison.tempdir", fixture.isValid())) {
        return 1;
    }
    const QStringList arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--close-active-diff"))
        || arguments.contains(QStringLiteral("--exit-active-diff"))) {
        activeDiffTeardown(fixture.path(), arguments.contains(QStringLiteral("--exit-active-diff")));
        std::printf("FILE-COMPARE-TEARDOWN failures=%d\n", failures);
        return failures == 0 ? 0 : 1;
    }
    app::ShellOptions options;
    options.stateDir = fixture.filePath(QStringLiteral("state"));
    options.restoreSession = false;
    options.showDefaultPanels = false;
    QTranslator productCatalog;
    QTranslator qtCatalog;
    const QString translations = qEnvironmentVariable("VEDIT_COMPARE_ZH_CATALOGS");
    if (!translations.isEmpty()) {
        if (!check("invalid.zh-catalogs", productCatalog.load(QStringLiteral("vedit_zh_CN"), translations)
                && qtCatalog.load(QStringLiteral("qtbase_zh_CN"), translations)
                && application.installTranslator(&productCatalog) && application.installTranslator(&qtCatalog))) {
            return 1;
        }
    }
    app::ApplicationShell shell(options);
    shell.setEditorProvider(editor::makeEditCoreProvider());
    shell.show();
    if (!translations.isEmpty()) {
        auto *action = shell.commands()->action(QStringLiteral("file.compare"));
        check("invalid.zh-menu-and-diagnostic", action && action->text().contains(QStringLiteral("比较"))
              && QCoreApplication::translate("FileComparison", "Cannot read %1.").contains(QStringLiteral("无法读取")));
    }
    invalidComparison(shell, fixture.path());
    if (translations.isEmpty()) {
        localComparison(shell, fixture.path());
        svnComparison(fixture.path());
    }
    std::printf("FILE-COMPARE-SELFTEST failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
