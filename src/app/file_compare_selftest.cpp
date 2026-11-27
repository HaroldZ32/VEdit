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

#include <KTextEditor/Attribute>
#include <KTextEditor/Document>
#include <KTextEditor/View>
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
#include <QSignalSpy>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTest>
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

QWidget *comparisonPage(app::ApplicationShell &shell)
{
    for (auto *page : shell.findChildren<QWidget *>(QStringLiteral("file-comparison"))) {
        if (page->isVisibleTo(&shell)) {
            return page;
        }
    }
    return nullptr;
}

KTextEditor::View *sideView(QWidget *comparison, const char *name)
{
    return comparison ? comparison->findChild<KTextEditor::View *>(QString::fromLatin1(name)) : nullptr;
}

bool closeActiveTab(app::ApplicationShell &shell)
{
    auto *pane = shell.editorArea()->activePane();
    return pane->closeTab(pane->activeTabIndex());
}

QPushButton *button(QWidget *comparison, const char *name)
{
    return comparison->findChild<QPushButton *>(QString::fromLatin1(name));
}

QString status(QWidget *comparison)
{
    return comparison->findChild<QLabel *>(QStringLiteral("compare-status"))->text();
}

bool diffReady(QWidget *comparison)
{
    return comparison && until([&] { return status(comparison) != QStringLiteral("Comparing…"); })
        && (status(comparison).startsWith(QStringLiteral("Difference")) || status(comparison) == QStringLiteral("No differences"));
}

bool shows(QWidget *comparison, const QString &text)
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

void close(app::ApplicationShell &shell, QWidget *comparison)
{
    const QPointer<QWidget> page(comparison);
    QTimer::singleShot(0, [] {
        if (auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            if (auto *discard = prompt->findChild<QPushButton *>(QStringLiteral("unsaved-discard"))) {
                discard->click();
            }
        }
    });
    check("tab.closed", closeActiveTab(shell) && until([&] { return page.isNull(); }));
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
    return !timedOut && chosen == files.size() && comparisonPage(shell);
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
            auto *comparison = comparisonPage(shell);
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
                    prompt->reject();
                }
            });
            closeProbe.start(10);
            check((testName + ".closes-without-discard").constData(), closeActiveTab(shell)
                && until([&] { return !comparisonPage(shell); }) && !prompted);
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
    auto *comparison = comparisonPage(shell);
    if (!check("local.diff-ready", diffReady(comparison))) {
        if (comparison) {
            qWarning("comparison: %s", qPrintable(status(comparison)));
            close(shell, comparison);
        }
        return;
    }
    auto *leftView = sideView(comparison, "compare-left-editor");
    auto *rightView = sideView(comparison, "compare-right-editor");
    if (!check("local.in-page-native-shared-editors", !qobject_cast<QDialog *>(comparison)
            && shell.editorArea()->activePane()->currentFilePath().isEmpty()
            && leftView && rightView && leftView->document() == document)) {
        close(shell, comparison);
        return;
    }
    check("local.native-diff-ranges", leftView->property("compare-diff-range-count").toInt() >= 2
          && rightView->property("compare-diff-range-count").toInt() >= 2);
    app::showFileComparison(shell, {left, {}, {}, false}, {right, {}, {}, false});
    check("local.reuses-comparison-tab", comparisonPage(shell) == comparison);
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

    const app::CompareInput endingSnapshot{{}, QStringLiteral("Repository snapshot"),
        QByteArray("unsaved \xe4\xb8\xad\xe6\x96\x87\nhead\nleft\nanchor\nnew end\n"), false};
    app::showFileComparison(shell, {left, {}, {}, false}, endingSnapshot);
    comparison = comparisonPage(shell);
    if (check("local.readonly-snapshot", diffReady(comparison) && !button(comparison, "compare-apply-right")->isEnabled())) {
        button(comparison, "compare-apply-left")->click();
        check("local.apply-eof-to-open-buffer", diffReady(comparison) && document->text().endsWith(QStringLiteral("new end\n")));
        check("local.ordinary-save-shared-buffer", shell.openPaths({left}) && shell.saveActiveDocument());
        check("local.save-crlf-and-final-newline", get(left).contains("\r\nnew end\r\n") && !document->isModified());
        app::showFileComparison(shell, {left, {}, {}, false}, endingSnapshot);
        check("local.shared-save-reactivates-same-comparison", comparisonPage(shell) == comparison);
        close(shell, comparison);
    }

    app::showFileComparison(shell, {left, {}, {}, false}, {{}, QStringLiteral("Invalid UTF-8"), QByteArray::fromHex("c3"), false});
    comparison = comparisonPage(shell);
    if (check("local.invalid-encoding", comparison && status(comparison).contains(QStringLiteral("unsupported encoding")))) {
        close(shell, comparison);
    }

    const QString empty = directory + QStringLiteral("/empty.txt");
    if (!check("local.empty-fixture", put(empty, {}))) {
        return;
    }
    app::showFileComparison(shell, {empty, {}, {}, false}, {{}, QStringLiteral("Snapshot"), "one\n", false});
    comparison = comparisonPage(shell);
    if (!check("local.empty-file-vs-insert", diffReady(comparison))) {
        return;
    }
    button(comparison, "compare-apply-left")->click();
    check("local.pure-insert-alignment", diffReady(comparison) && status(comparison) == QStringLiteral("No differences"));
    check("local.external-change-fixture", put(empty, "outside\n"));
    button(comparison, "compare-save-left")->click();
    check("local.external-change-refuses-save", get(empty) == "outside\n" && status(comparison).contains(QStringLiteral("changed outside")));
    QTimer::singleShot(0, [] {
        if (auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            prompt->reject();
        }
    });
    check("local.cancel-close-preserves-dirty-page", !closeActiveTab(shell) && comparisonPage(shell) == comparison
        && sideView(comparison, "compare-left-editor")->document()->text() == QStringLiteral("one\n"));
    if (QApplication::platformName() == QStringLiteral("offscreen")) {
        button(comparison, "compare-undo")->click();
        check("local.headless-undo-clears-close-block", diffReady(comparison)
            && !sideView(comparison, "compare-left-editor")->document()->isModified());
    }
    close(shell, comparison);
    check("local.close-after-conflict-preserves-disk", get(empty) == "outside\n");

    app::showFileComparison(shell, {empty, {}, {}, false}, {{}, QStringLiteral("Empty snapshot"), {}, false});
    comparison = comparisonPage(shell);
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
    comparison = comparisonPage(shell);
    if (check("local.readonly-disk-blocks-patch", diffReady(comparison) && !button(comparison, "compare-apply-left")->isEnabled())) {
        close(shell, comparison);
    }

    const QString beforeDiscard = document->text();
    app::showFileComparison(shell, {left, {}, {}, false}, {{}, QStringLiteral("Snapshot"), "different\n", false});
    comparison = comparisonPage(shell);
    if (diffReady(comparison)) {
        button(comparison, "compare-apply-left")->click();
        const bool modified = diffReady(comparison) && document->isModified() && document->text() != beforeDiscard;
        const QString edited = document->text();
        close(shell, comparison);
        check("local.close-retains-live-editor-buffer", modified && document->text() == edited && document->isModified());
        auto *ordinary = shell.editorArea()->activePane()->editorWidget();
        check("local.ordinary-undo-retains-history", ordinary
              && shell.editorArea()->triggerActiveEditorAction({QStringLiteral("edit_undo"), QStringLiteral("undo")},
                  QStringLiteral("undo"))
              && document->text() == beforeDiscard);
        document->setModified(false);
    }
}

QWidget *focusText(KTextEditor::View *view)
{
    view->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    return view->focusProxy() ? view->focusProxy() : view;
}

KTextEditor::Attribute::Ptr syntaxAttribute(KTextEditor::View *view, const QString &token)
{
    const auto column = view->document()->line(0).indexOf(token);
    for (const auto &block : view->lineAttributes(0)) {
        if (block.start <= column && column < block.start + block.length) {
            return block.attribute;
        }
    }
    return {};
}

bool syntaxUnderDiff(KTextEditor::View *view, const QString &moduleName)
{
    const auto keyword = syntaxAttribute(view, QStringLiteral("module"));
    const auto module = syntaxAttribute(view, moduleName);
    const auto comment = syntaxAttribute(view, QStringLiteral("//"));
    return keyword && module && comment && module->name().endsWith(QStringLiteral("Module Definition"))
        && comment->name().contains(QStringLiteral("Comment"))
        && keyword->name() != comment->name() && module->foreground() != comment->foreground()
        && view->property("compare-diff-range-count").toInt() > 0;
}

void nativeEditing(app::ApplicationShell &shell, const QString &directory)
{
    const QString left = directory + QStringLiteral("/native-left.sv");
    const QString right = directory + QStringLiteral("/native-right.sv");
    QByteArray leftText = "module left_design; // working left\n  logic old_signal;\n";
    QByteArray rightText = "module right_design; // working right\n  logic new_signal;\n  logic extra_signal;\n";
    for (int line = 0; line < 160; ++line) {
        const QByteArray shared = "  wire common_" + QByteArray::number(line) + ";\n";
        leftText += shared;
        rightText += shared;
    }
    leftText += "endmodule\n";
    rightText += "endmodule\n";
    if (!check("native.fixtures", put(left, leftText) && put(right, rightText))) {
        return;
    }
    app::showFileComparison(shell, {left, {}, {}, false}, {right, {}, {}, false});
    auto *page = comparisonPage(shell);
    auto *leftView = sideView(page, "compare-left-editor");
    auto *rightView = sideView(page, "compare-right-editor");
    if (!check("native.both-editors-ready", diffReady(page) && leftView && rightView)) {
        return;
    }
    auto *leftDoc = leftView->document();
    auto *rightDoc = rightView->document();
    check("native.syntax-and-diff-coexist", leftDoc->highlightingMode() == QStringLiteral("SystemVerilog")
        && rightDoc->highlightingMode() == QStringLiteral("SystemVerilog")
        && syntaxUnderDiff(leftView, QStringLiteral("left_design"))
        && syntaxUnderDiff(rightView, QStringLiteral("right_design")));
    if (const auto output = qEnvironmentVariable("VEDIT_COMPARE_SCREENSHOT"); !output.isEmpty()) {
        check("native.capture", shell.grab().save(output));
    }
    for (auto *bar : leftView->findChildren<QScrollBar *>()) {
        if (bar->orientation() == Qt::Vertical && bar->maximum() >= 70) {
            bar->setValue(70);
            break;
        }
    }
    check("native.scroll-aligns-unchanged-lines", leftView->firstDisplayedLine() == 70
        && rightView->firstDisplayedLine() == 71);
    shell.activateWindow();
    check("native.keyboard-window-active", until([&] { return shell.isActiveWindow(); }));
    leftView->setCursorPosition({1, 2});
    QWidget *input = focusText(leftView);
    check("native.keyboard-editor-focused", input->hasFocus());
    QSignalSpy undoCommand(shell.commands()->action(QStringLiteral("edit.undo")), &QAction::triggered);
    QSignalSpy saveCommand(shell.commands()->action(QStringLiteral("file.save")), &QAction::triggered);
    QTest::keyClick(input, Qt::Key_X);
    check("native.typing-stays-in-buffer", leftDoc->text().contains(QStringLiteral("xlogic old_signal;"))
        && leftDoc->isModified() && get(left) == leftText);
    check("native.edit-invalidates-hunk-actions", status(page) == QStringLiteral("Comparing…")
        && !button(page, "compare-apply-right")->isEnabled());
    check("native.recomputed-after-keyboard-edit", diffReady(page));
    QTest::keyClick(input, Qt::Key_Z, Qt::ControlModifier);
    check("native.ctrl-z-routes-shell-command", undoCommand.size() == 1);
    check("native.ctrl-z-restores-baseline", until([&] { return leftDoc->text().toUtf8() == leftText; })
        && !leftDoc->isModified());
    QTest::keyClicks(input, QStringLiteral("wire saved_live; "));
    QTest::keyClick(input, Qt::Key_S, Qt::ControlModifier);
    check("native.ctrl-s-routes-shell-command", saveCommand.size() == 1);
    check("native.ctrl-s-persists-typed-edit", until([&] { return get(left).contains("wire saved_live;"); })
        && !leftDoc->isModified() && get(right) == rightText);

    check("native.ordinary-tab-open-shares-comparison", shell.openPaths({right})
        && editor::documentFor(shell.editorArea()->activePane()->editorWidget()) == rightDoc);
    app::showFileComparison(shell, {left, {}, {}, false}, {right, {}, {}, false});
    if (!check("native.opening-pair-reactivates-page", comparisonPage(shell) == page)) {
        return;
    }
    close(shell, page);

    const QByteArray snapshot = "module historic; // historic snapshot\n  wire from_revision;\nendmodule\n";
    app::showFileComparison(shell, {left, {}, {}, false},
        {{}, QStringLiteral("Revision r17"), snapshot, false, QStringLiteral("rtl/module.sv")});
    page = comparisonPage(shell);
    rightView = sideView(page, "compare-right-editor");
    if (!check("native.historical-syntax", diffReady(page) && rightView
        && rightView->document()->highlightingMode() == QStringLiteral("SystemVerilog")
        && syntaxUnderDiff(rightView, QStringLiteral("historic"))
        && !rightView->document()->isReadWrite())) {
        if (page) { close(shell, page); }
        return;
    }
    auto *snapshotDoc = rightView->document();
    const QString before = snapshotDoc->text();
    QTest::keyClicks(focusText(rightView), QStringLiteral("blocked"));
    check("native.historical-typing-blocked", snapshotDoc->text() == before
        && !snapshotDoc->isModified() && !button(page, "compare-apply-right")->isEnabled());
    close(shell, page);
}

void inlineDifferencesAndNavigation(app::ApplicationShell &shell, const QString &directory)
{
    const QString path = directory + QStringLiteral("/inline-left.sv");
    const QString leftText = QStringLiteral("logic out = old_name; // same\n// anchor A\n// mark \U0001F600 end\n"
        "// anchor B\n// mark \U0001F600 end\n// anchor C\n// anchor D\n");
    const QString rightText = QStringLiteral("logic out = new_name; // same\n// anchor A\n// mark \U0001F601 end\n"
        "// anchor B\n// mark \U0001FA00 end\n// anchor C\n// right only\n// anchor D\n");
    const app::CompareInput left{path, {}, {}, false};
    const app::CompareInput right{{}, QStringLiteral("Revision"), rightText.toUtf8(), false, QStringLiteral("inline-right.sv")};
    if (!check("inline.fixture", put(path, leftText.toUtf8()) && shell.openPaths({path}))) {
        return;
    }
    app::showFileComparison(shell, left, right);
    auto *page = comparisonPage(shell);
    auto *leftView = sideView(page, "compare-left-editor");
    auto *rightView = sideView(page, "compare-right-editor");
    if (!check("inline.native-views-ready", diffReady(page) && leftView && rightView)) {
        return;
    }
    const auto range = [](int line, int first, int end) { return QVariant::fromValue(QVariantList{line, first, end}); };
    const auto spansLeft = leftView->property("compare-inline-ranges").toList();
    const auto spansRight = rightView->property("compare-inline-ranges").toList();
    check("inline.range-prefix-suffix", !spansLeft.isEmpty() && !spansRight.isEmpty()
        && spansLeft.first() == range(0, 12, 15) && spansRight.first() == range(0, 12, 15));
    const QVariantList expected{range(0, 12, 15), range(2, 8, 10), range(4, 8, 10)};
    check("inline.unicode-safe", spansLeft == expected && spansRight == expected);
    check("inline.background-only-with-original-native-text", leftView->property("compare-inline-background-only").toBool()
        && rightView->property("compare-inline-background-only").toBool()
        && leftView->document()->text() == leftText && rightView->document()->text() == rightText
        && !leftView->document()->isModified() && !rightView->document()->isModified());
    check("inline.one-sided-line-keeps-block-cue", leftView->property("compare-diff-range-count").toInt() == 3
        && rightView->property("compare-diff-range-count").toInt() == 4);

    shell.activateWindow();
    auto *next = button(page, "compare-next");
    auto *previous = button(page, "compare-previous");
    QSignalSpy nextClicked(next, &QPushButton::clicked);
    QTest::keyClick(focusText(leftView), Qt::Key_PageDown, Qt::AltModifier);
    check("navigation.alt-page-down", nextClicked.size() == 1 && status(page) == QStringLiteral("Difference 2 of 4")
        && leftView->cursorPosition().line() == 2 && rightView->cursorPosition().line() == 2);
    QTest::keyClick(focusText(rightView), Qt::Key_PageUp, Qt::AltModifier);
    check("navigation.alt-page-up", status(page) == QStringLiteral("Difference 1 of 4")
        && leftView->cursorPosition().line() == 0 && rightView->cursorPosition().line() == 0);
    check("navigation.shortcuts-discoverable", next->toolTip().contains(QStringLiteral("Alt"))
        && previous->toolTip().contains(QStringLiteral("Alt")));
    if (check("navigation.ordinary-tab-focused", shell.openPaths({path}))) {
        auto *ordinary = editor::viewFor(shell.editorArea()->activePane()->editorWidget());
        if (ordinary) {
            QTest::keyClick(focusText(ordinary), Qt::Key_PageDown, Qt::AltModifier);
        }
        check("navigation.shortcuts-stay-in-compare", ordinary && nextClicked.size() == 1
            && status(page) == QStringLiteral("Difference 1 of 4"));
    }
    app::showFileComparison(shell, left, right);
    close(shell, page);
}

void saveAsComparison(app::ApplicationShell &shell, const QString &directory)
{
    const QString a = directory + QStringLiteral("/save-as-a.sv");
    const QString b = directory + QStringLiteral("/save-as-b.sv");
    const QString c = directory + QStringLiteral("/save-as-c.sv");
    const QByteArray original = "module source_a;\r\nendmodule\r\n";
    const app::CompareInput left{a, {}, {}, false};
    const app::CompareInput right{c, {}, {}, false};
    if (!check("save-as.fixture", put(a, original) && put(c, "module other;\nendmodule\n") && shell.openPaths({a}))) {
        return;
    }
    auto *pane = shell.editorArea()->activePane();
    auto *aDoc = editor::documentFor(pane->editorWidget());
    app::showFileComparison(shell, left, right);
    auto *page = comparisonPage(shell);
    if (!check("save-as.compare-shares-a", diffReady(page) && aDoc && sideView(page, "compare-left-editor")->document() == aDoc)) {
        return;
    }
    if (!check("save-as.return-to-a", shell.openPaths({a}))) {
        return;
    }
    QString error;
    check("save-as.refuses-other-compared-destination", !pane->saveAsFile(c, &error)
        && get(c) == "module other;\nendmodule\n" && pane->currentFilePath() == a);
    check("save-as.refuses-disappeared-compared-destination", QFile::remove(c) && !pane->saveAsFile(c, &error)
        && !QFileInfo::exists(c) && put(c, "module other;\nendmodule\n") && pane->currentFilePath() == a);
    const QString unreadable = directory + QStringLiteral("/save-as-write-only.sv");
    check("save-as.does-not-write-unreadable-target", put(unreadable, "unchanged\n")
        && QFile::setPermissions(unreadable, QFileDevice::WriteOwner)
        && !pane->saveAsFile(unreadable, &error) && pane->currentFilePath() == a
        && QFile::setPermissions(unreadable, QFileDevice::WriteOwner | QFileDevice::ReadOwner)
        && get(unreadable) == "unchanged\n");
    if (!check("save-as.detaches-b-model", pane->saveAsFile(b, &error) && pane->currentFilePath() == b
        && editor::documentFor(pane->editorWidget()) != aDoc && get(b) == original && get(a) == original)) {
        qWarning("Save As: %s", qPrintable(error));
        return;
    }
    auto *bDoc = editor::documentFor(pane->editorWidget());
    if (!check("save-as.b-typing-does-not-edit-a", bDoc && bDoc->insertText({0, 0}, QStringLiteral("// B only\n"))
        && bDoc->isModified() && !aDoc->isModified() && !aDoc->text().contains(QStringLiteral("B only")))) {
        return;
    }
    if (!check("save-as.reopens-a-as-original-model", shell.openPaths({a}) && editor::documentFor(pane->editorWidget()) == aDoc)) {
        return;
    }
    app::showFileComparison(shell, left, right);
    check("save-as.reuses-comparison-with-original-model", comparisonPage(shell) == page);
    check("save-as.a-can-edit-independently", aDoc->insertText({0, 0}, QStringLiteral("// A only\n"))
        && aDoc->isModified() && !bDoc->text().contains(QStringLiteral("A only")));
    button(page, "compare-save-left")->click();
    check("save-as.saving-a-preserves-b-dirty-state-and-disk", !aDoc->isModified() && bDoc->isModified()
        && get(a) == "// A only\r\n" + original && get(b) == original);
    close(shell, page);
    check("save-as.saves-b-only-via-its-tab", shell.openPaths({b}) && editor::documentFor(pane->editorWidget()) == bDoc
        && shell.saveActiveDocument() && get(b).startsWith("// B only\n") && get(a) == "// A only\r\n" + original);
}

void mixedLineEndings(app::ApplicationShell &shell, const QString &directory)
{
    const QString path = directory + QStringLiteral("/mixed.txt");
    const QByteArray bom = QByteArray::fromHex("efbbbf");
    const QByteArray original = "head\r\nkeep-lf\nold\r\nkeep-crlf\r\ntail";
    const app::CompareInput left{path, {}, {}, false};
    const app::CompareInput right{{}, QStringLiteral("Revision"), "head\nkeep-lf\nnew\nkeep-crlf\nnew-tail\n", false};
    if (!check("mixed.fixture", put(path, bom + original) && shell.openPaths({path}))) {
        return;
    }
    app::showFileComparison(shell, left, right);
    auto *page = comparisonPage(shell);
    if (!check("mixed.comparison-ready", diffReady(page))) {
        if (page) { qWarning("Mixed compare: %s", qPrintable(status(page))); }
        return;
    }
    auto *view = sideView(page, "compare-left-editor");
    button(page, "compare-apply-left")->click();
    if (!check("mixed.applies-hunk-without-writing", diffReady(page) && get(path) == bom + original)) {
        return;
    }
    view->setCursorPosition({1, 7});
    QTest::keyClicks(focusText(view), QStringLiteral("!"));
    button(page, "compare-save-left")->click();
    check("mixed.preserves-bom-lf-crlf-and-no-final-newline",
          get(path) == bom + "head\r\nkeep-lf!\nnew\r\nkeep-crlf\r\ntail");
    if (check("mixed.eof-hunk-ready", until([&] { return button(page, "compare-next")->isEnabled(); }))) {
        button(page, "compare-next")->click();
        button(page, "compare-apply-left")->click();
        check("mixed.eof-hunk-updates-native-buffer", view->document()->text().endsWith(QStringLiteral("new-tail\n")));
        button(page, "compare-save-left")->click();
    }
    const auto saved = get(path);
    if (!check("mixed.eof-hunk-preserves-other-line-endings",
          saved == bom + "head\r\nkeep-lf!\nnew\r\nkeep-crlf\r\nnew-tail\r\n")) {
        qWarning("Mixed EOF status: %s, bytes: %s", qPrintable(status(page)), saved.toHex().constData());
    }
    close(shell, page);

    const QString joinedPath = directory + QStringLiteral("/mixed-join-undo.txt");
    check("mixed.join-undo-fixture", put(joinedPath, "A\r\nB\nC\n") && shell.openPaths({joinedPath}));
    app::showFileComparison(shell, {joinedPath, {}, {}, false},
                           {{}, QStringLiteral("Revision"), "A\nB\nunrelated\n", false});
    page = comparisonPage(shell);
    auto *joinedView = sideView(page, "compare-left-editor");
    if (check("mixed.join-undo-ready", diffReady(page) && joinedView)) {
        shell.activateWindow();
        auto *input = focusText(joinedView);
        joinedView->setCursorPosition({2, 1});
        QTest::keyClicks(input, QStringLiteral("!"));
        joinedView->setCursorPosition({1, 0});
        QTest::keyClick(input, Qt::Key_Backspace);
        check("mixed.joins-crlf-boundary", joinedView->document()->text() == QStringLiteral("AB\nC!\n"));
        QTest::keyClick(input, Qt::Key_Z, Qt::ControlModifier);
        check("mixed.undo-join-retains-other-dirty-edit", joinedView->document()->text() == QStringLiteral("A\nB\nC!\n"));
        button(page, "compare-save-left")->click();
        check("mixed.undo-join-restores-original-crlf", get(joinedPath) == "A\r\nB\nC!\n");
        close(shell, page);
    }
}

void guardSaveAll(app::ApplicationShell &shell, const QString &directory)
{
    for (bool compareFirst : {false, true}) {
        const QString path = directory + (compareFirst ? QStringLiteral("/compare-first.txt") : QStringLiteral("/ordinary-first.txt"));
        if (!check("save-all.fixture", put(path, "original\n") && (compareFirst || shell.openPaths({path})))) {
            return;
        }
        const app::CompareInput left{path, {}, {}, false};
        const app::CompareInput right{{}, QStringLiteral("Revision"), "historical\n", false};
        app::showFileComparison(shell, left, right);
        auto *page = comparisonPage(shell);
        if (!check("save-all.shared-ready", diffReady(page) && shell.openPaths({path}))) {
            return;
        }
        auto *doc = editor::documentFor(shell.editorArea()->activePane()->editorWidget());
        if (!check("save-all.dirty-with-external-edit", doc && doc == sideView(page, "compare-left-editor")->document()
            && doc->insertText({0, 0}, QStringLiteral("mine\n")) && put(path, "changed on disk\n"))) {
            return;
        }
        shell.commands()->action(QStringLiteral("file.save-all"))->trigger();
        check(compareFirst ? "save-all.compare-first-refuses-external-overwrite" : "save-all.ordinary-first-refuses-external-overwrite",
              get(path) == "changed on disk\n" && doc->isModified() && doc->text().startsWith(QStringLiteral("mine\n")));
        app::showFileComparison(shell, left, right);
        close(shell, page);
        check("save-all.closed-compare-retains-shared-dirty-buffer", shell.openPaths({path})
            && editor::documentFor(shell.editorArea()->activePane()->editorWidget()) == doc && doc->isModified());
        shell.commands()->action(QStringLiteral("file.save-all"))->trigger();
        check("save-all.closed-compare-still-refuses-external-overwrite",
            get(path) == "changed on disk\n" && doc->isModified());
        doc->setModified(false);
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
    shell.setEditorProvider(editor::makeEditCoreProvider());
    workspace::installVcs(shell, {});
    shell.panels()->showPanel(QStringLiteral("vcs"), &shell);
    shell.show();
    auto *history = shell.findChild<VcsHistoryPanel *>(QStringLiteral("vcs-history-panel"));
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
        until([&] { return comparisonPage(shell); });
        auto *item = comparisonPage(shell);
        return diffReady(item) ? item : nullptr;
    };
    select(1);
    compare->click();
    history->findChild<QLineEdit *>(QStringLiteral("vcs-history-path"))->setText(original);
    check("svn.stale-file-selection-discards-result", !until([&] { return comparisonPage(shell); }));
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
        check("svn.stale-root-discards-result", !until([&] { return comparisonPage(shell); }));
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

void activeDiffTeardown(const QString &directory, bool quitApplication, bool testStaleResult)
{
    const auto originalHandler = qInstallMessageHandler(teardownMessageHandler);
    const auto restoreHandler = qScopeGuard([&] { qInstallMessageHandler(originalHandler); });
    const QString realDiff = QStandardPaths::findExecutable(QStringLiteral("diff"));
    const QString wrapper = directory + QStringLiteral("/diff");
    const QString started = directory + QStringLiteral("/diff-started");
    const QByteArray script = "#!/bin/bash\nif [[ ! -e \"$VEDIT_TEST_DIFF_STARTED\" ]]; then\n"
                              "  printf started > \"$VEDIT_TEST_DIFF_STARTED\"\n  read -r -t 30 gate\nfi\n"
                              "exec \"$VEDIT_TEST_REAL_DIFF\" \"$@\"\n";
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
        shell.setEditorProvider(editor::makeEditCoreProvider());
        shell.show();
        app::CompareInput left{{}, QStringLiteral("left"), "before\n", false};
        const app::CompareInput right{{}, QStringLiteral("right"), "after\n", false};
        if (testStaleResult) {
            left.localPath = directory + QStringLiteral("/working.txt");
            if (!check("stale.writable-fixture", put(left.localPath, left.snapshot))) {
                return;
            }
        }
        app::showFileComparison(shell, left, right);
        auto *comparison = comparisonPage(shell);
        auto *process = comparison ? comparison->findChild<QProcess *>() : nullptr;
        if (!check("teardown.child-is-running", process && until([&] {
            return process->state() == QProcess::Running && QFileInfo::exists(started);
        }))) {
            return;
        }
        if (testStaleResult) {
            auto *view = sideView(comparison, "compare-left-editor");
            if (!check("stale.writable-native-view", view && view->document()->isReadWrite())) {
                return;
            }
            bool rejected = false;
            const auto result = QObject::connect(process, &QProcess::finished, comparison, [&](int code) {
                if (code == 1) {
                    rejected = status(comparison) == QStringLiteral("Comparing…")
                        && !button(comparison, "compare-apply-left")->isEnabled();
                }
            });
            check("stale.edit-before-old-result", view->document()->setText(QStringLiteral("after\n")));
            process->write("continue\n");
            process->closeWriteChannel();
            check("stale.first-result-rejected-and-latest-used", diffReady(comparison) && rejected
                && status(comparison) == QStringLiteral("No differences"));
            QObject::disconnect(result);
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
            const QPointer<QWidget> page(comparison);
            check("teardown.tab-destroyed", closeActiveTab(shell) && until([&] { return page.isNull(); }));
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    QTemporaryDir fixture;
    if (!check("comparison.tempdir", fixture.isValid())) {
        return 1;
    }
    const QStringList arguments = application.arguments();
    const bool testStaleResult = arguments.contains(QStringLiteral("--stale-diff"));
    if (arguments.contains(QStringLiteral("--close-active-diff"))
        || arguments.contains(QStringLiteral("--exit-active-diff")) || testStaleResult) {
        activeDiffTeardown(fixture.path(), arguments.contains(QStringLiteral("--exit-active-diff")), testStaleResult);
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
    shell.resize(1180, 720);
    shell.show();
    if (!translations.isEmpty()) {
        auto *action = shell.commands()->action(QStringLiteral("file.compare"));
        check("invalid.zh-menu-and-diagnostic", action && action->text().contains(QStringLiteral("比较"))
              && QCoreApplication::translate("FileComparison", "Cannot read %1.").contains(QStringLiteral("无法读取")));
    }
    invalidComparison(shell, fixture.path());
    if (translations.isEmpty()) {
        localComparison(shell, fixture.path());
        nativeEditing(shell, fixture.path());
        inlineDifferencesAndNavigation(shell, fixture.path());
        saveAsComparison(shell, fixture.path());
        mixedLineEndings(shell, fixture.path());
        guardSaveAll(shell, fixture.path());
        svnComparison(fixture.path());
    }
    std::printf("FILE-COMPARE-SELFTEST failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
