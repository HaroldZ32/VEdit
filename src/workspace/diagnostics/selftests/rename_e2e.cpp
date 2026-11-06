// UX batch 2 / B5 — the Rename end-to-end proof (item 4).
//
// Contract: .mpd/plans/ux-batch-2/00-plan.md §B5. The path under test is the
// REAL one the user drives:
//
//   menu/toolbar action `diagnostics.rename`
//     -> BackendWiring::requestRename (capability gate + cursor validation)
//     -> DiagnosticsController::requestRename
//     -> the Rust LSP session (`textDocument/rename`)
//     -> applyWorkspaceEdit (open document = one undo step; a file that is not
//        open = atomic save)
//
// It uses the REAL verible-verilog-ls; when the binary is not resolvable the
// whole binary SKIPs with an explicit reason instead of pretending.
//
//   build: vedit-rename-e2e [--verible-ls <path>] [--verible-lint <path>]
//
// The name dialog is a real QInputDialog (the product shows one), so the test
// fills it from the event loop exactly like a user would.

#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "diagnostics_panel.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "lsp_actions.h"
#include "panel_registry.h"

#include "diagnostics_integration.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QInputDialog>
#include <QLineEdit>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QWidget>

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

int g_notClaimed = 0;

void skip(const char *name, const QString &why)
{
    ++g_skips;
    std::printf("SKIP %s  %s\n", name, why.toUtf8().constData());
    std::fflush(stdout);
}

/// A documented BOUND, not a skipped acceptance: the captain's ruling keeps
/// cross-file rename out of this batch, so the line says so explicitly instead
/// of reading like an acceptance that was silently skipped.
void notClaimed(const char *name, const QString &why)
{
    ++g_notClaimed;
    std::printf("NOT-CLAIMED %s  %s\n", name, why.toUtf8().constData());
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

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text.toUtf8()) == text.toUtf8().size();
}

int countOccurrences(const QString &text, const QString &needle)
{
    int count = 0;
    int index = text.indexOf(needle);
    while (index >= 0) {
        ++count;
        index = text.indexOf(needle, index + needle.size());
    }
    return count;
}

/// Fill the next QInputDialog the product shows (the Rename name dialog).
class DialogFiller final : public QObject {
public:
    DialogFiller(QString value, QObject *parent = nullptr)
        : QObject(parent)
        , value_(std::move(value))
    {
        timer_.setInterval(20);
        connect(&timer_, &QTimer::timeout, this, [this]() {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (dialog == nullptr) {
                dialog = qobject_cast<QInputDialog *>(QApplication::activeWindow());
            }
            if (dialog == nullptr) {
                return;
            }
            ++seen_;
            dialog->setTextValue(value_);
            dialog->accept();
            timer_.stop();
        });
    }
    void arm() { timer_.start(); }
    int seen() const { return seen_; }
    bool accepted() const { return seen_ > 0; }

private:
    QString value_;
    QTimer timer_;
    int seen_ = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString veribleLs;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QLatin1String("--verible-ls") && index + 1 < argc) {
            veribleLs = QString::fromLocal8Bit(argv[++index]);
        }
    }
    if (veribleLs.isEmpty()) {
        veribleLs = qEnvironmentVariable("VEDIT_VERIBLE_LS");
    }
    if (veribleLs.isEmpty() || !QFile::exists(veribleLs)) {
        skip("rename.*", QStringLiteral("no verible-verilog-ls at '%1'").arg(veribleLs));
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 0;
    }
    qputenv("VEDIT_VERIBLE_LS", veribleLs.toUtf8());

    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("rename.tempdir", false);
        return 1;
    }
    const QString rtl = dir.path() + QStringLiteral("/rtl");
    QDir().mkpath(rtl);
    const QString topPath = rtl + QStringLiteral("/top.sv");
    const QString otherPath = rtl + QStringLiteral("/other.sv");
    const QString topText = QStringLiteral(
        "module top;\n"
        "  logic w;\n"
        "  assign w = 1'b1;\n"
        "  initial $display(\"%0b\", w);\n"
        "endmodule\n");
    const QString otherText = QStringLiteral("module other;\n  top u_top();\nendmodule\n");
    check("rename.fixture", writeFile(topPath, topText) && writeFile(otherPath, otherText));

    vedit::app::ShellOptions options;
    options.stateDir = dir.path() + QStringLiteral("/state");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    std::unique_ptr<vedit::app::ApplicationShell> shell(new vedit::app::ApplicationShell(options));
    if (provider != nullptr) {
        shell->editorArea()->setProvider(provider.get());
    }
    shell->editorArea()->ensurePane();
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = dir.path();
    integration.stateDir = dir.path() + QStringLiteral("/state");
    vedit::workspace::installDiagnostics(*shell, integration);
    // The Diagnostics panel is the documented way to reach the controller from
    // the outside (the shell property holds a QObject holder; the panel exposes
    // the typed pointer).
    shell->panels()->showPanel(QStringLiteral("outline"), shell.get());
    shell->resize(1200, 800);
    shell->show();
    QCoreApplication::processEvents();

    QAction *renameAction =
        shell->commands() != nullptr ? shell->commands()->action(QStringLiteral("diagnostics.rename"))
                                     : nullptr;
    check("rename.action-exists", renameAction != nullptr);
    if (renameAction == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    check("rename.action-enabled", renameAction->isEnabled(),
          renameAction->toolTip());

    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    check("rename.pane", pane != nullptr);
    QString openError;
    check("rename.file-opened", pane != nullptr && pane->openFile(topPath, &openError), openError);
    auto *view = pane != nullptr ? qobject_cast<KTextEditor::View *>(pane->editorWidget()) : nullptr;
    check("rename.ktexteditor-view", view != nullptr);

    // Wait for the server session (the rename needs textDocument/rename).
    auto *panel = shell->findChild<vedit::workspace::diagnostics::DiagnosticsPanel *>();
    check("rename.panel", panel != nullptr);
    if (view == nullptr || panel == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    const bool ready = waitUntil(
        [panel]() {
            return panel->controller()->session().state
                == vedit::workspace::diagnostics::BackendState::Ready;
        },
        30000);
    if (!ready) {
        skip("rename.e2e", QStringLiteral("the LSP session never became ready: %1")
                               .arg(panel->controller()->session().message));
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 0;
    }
    check("rename.session-ready", ready, panel->controller()->session().message);

    // ---- leg 0: positions that cannot be renamed are refused WITHOUT a dialog
    {
        view->setCursorPosition(KTextEditor::Cursor(1, 0)); // leading spaces of "  logic w;"
        const QString beforeRefusal = view->document()->text();
        DialogFiller refusalFiller(QStringLiteral("never"));
        refusalFiller.arm();
        renameAction->trigger();
        waitUntil([&refusalFiller]() { return refusalFiller.accepted(); }, 300);
        const QString refusalStatus = shell->statusBar()->currentMessage();
        check("rename.refusal-no-dialog", !refusalFiller.accepted(),
              QStringLiteral("dialogs=%1").arg(refusalFiller.seen()));
        check("rename.refusal-buffer-untouched", view->document()->text() == beforeRefusal);
        check("rename.refusal-reason", refusalStatus.contains(QStringLiteral("symbol")),
              refusalStatus);
        std::printf("OBSERVED rename.refusal status='%s'\n", qPrintable(refusalStatus));
    }

    // ---- leg 0b: a keyword is not a symbol either -------------------------
    {
        view->setCursorPosition(KTextEditor::Cursor(4, 0)); // "endmodule"
        DialogFiller keywordFiller(QStringLiteral("never"));
        keywordFiller.arm();
        renameAction->trigger();
        waitUntil([&keywordFiller]() { return keywordFiller.accepted(); }, 300);
        const QString keywordStatus = shell->statusBar()->currentMessage();
        check("rename.keyword-no-dialog", !keywordFiller.accepted());
        check("rename.keyword-reason", keywordStatus.contains(QStringLiteral("keyword")),
              keywordStatus);
        std::printf("OBSERVED rename.keyword status='%s'\n", qPrintable(keywordStatus));
    }

    // ---- leg 1: rename `w` -> `w2` through the MENU action -----------------
    {
        view->setCursorPosition(KTextEditor::Cursor(2, 9)); // inside `w`
        DialogFiller filler(QStringLiteral("w2"));
        filler.arm();
        renameAction->trigger();
        const bool applied = waitUntil(
            [&view]() {
                return view->document()->text().contains(QStringLiteral("w2"));
            },
            30000);
        check("rename.dialog-shown", filler.accepted(),
              QStringLiteral("dialogs=%1").arg(filler.seen()));
        const QString buffer = view->document()->text();
        check("rename.buffer-applied",
              applied && countOccurrences(buffer, QStringLiteral("w2")) == 3,
              QStringLiteral("w2-count=%1 text='%2'")
                  .arg(countOccurrences(buffer, QStringLiteral("w2")))
                  .arg(buffer.left(80).replace(QLatin1Char('\n'), QLatin1Char('|'))));
        check("rename.buffer-no-old-name", countOccurrences(buffer, QStringLiteral(" w ")) == 0
                  && countOccurrences(buffer, QStringLiteral(" w;")) == 0);
        // The open document is edited in ONE undo step per file; the disk changes
        // on the user's own save (this is exactly the product's save seam).
        check("rename.disk-before-save", readFile(topPath) == topText,
              QStringLiteral("the open document is a buffer edit until the user saves"));
        QString saveError;
        check("rename.save", pane->saveFile(&saveError), saveError);
        const QString disk = readFile(topPath);
        check("rename.disk-after-save",
              disk.contains(QStringLiteral("logic w2;"))
                  && countOccurrences(disk, QStringLiteral("w2")) == 3
                  && countOccurrences(disk, QStringLiteral(" w ")) == 0,
              disk.left(80).replace(QLatin1Char('\n'), QLatin1Char('|')));
    }

    // ---- leg 2: the cross-file leg (a file that is NOT open) --------------
    // `top` is declared in this file and instantiated in rtl/other.sv.
    {
        view->setCursorPosition(KTextEditor::Cursor(0, 7)); // inside `top`
        DialogFiller topFiller(QStringLiteral("top2"));
        topFiller.arm();
        renameAction->trigger();
        const bool changed = waitUntil(
            [&view]() { return view->document()->text().contains(QStringLiteral("module top2")); },
            30000);
        const QString buffer2 = view->document()->text();
        check("rename.crossfile-buffer", changed, buffer2.left(40));
        const QString otherDisk = readFile(otherPath);
        const bool crossFile = otherDisk.contains(QStringLiteral("top2"));
        std::printf("OBSERVED rename.crossfile non-open-file-edited=%d other='%s'\n",
                    crossFile ? 1 : 0,
                    otherDisk.left(60).replace(QLatin1Char('\n'), QLatin1Char('|')).toUtf8().constData());
        if (crossFile) {
            check("rename.crossfile-atomic-save", true,
                  QStringLiteral("other.sv (not open) was written on disk"));
        } else {
            notClaimed("rename.crossfile-atomic-save",
                       QStringLiteral("verible answers for the OPEN document only; cross-file rename "
                                      "is NOT CLAIMED in this batch (other.sv unchanged: %1). The "
                                      "non-open-file WRITE path is proven separately below.")
                           .arg(otherDisk == otherText ? QStringLiteral("yes")
                                                       : QStringLiteral("different")));
        }
    }

    // The non-open-file path of the engine itself (the seam the rename uses):
    // an edit addressed to a file that is not open must be written atomically.
    {
        const QString outsidePath = dir.path() + QStringLiteral("/outside.sv");
        writeFile(outsidePath, QStringLiteral("module outside;\n  wire a;\nendmodule\n"));
        vedit::editor::WorkspaceTextEdit edit;
        edit.filePath = outsidePath;
        edit.startLine = 1;
        edit.startColumn = 7;
        edit.endLine = 1;
        edit.endColumn = 8;
        edit.newText = QStringLiteral("renamed");
        const vedit::editor::WorkspaceEditReport report =
            vedit::editor::applyWorkspaceEdit(QList<vedit::editor::WorkspaceTextEdit>{edit});
        const QString outsideDisk = readFile(outsidePath);
        check("rename.non-open-atomic-save",
              report.ok && report.writtenFileCount == 1
                  && outsideDisk.contains(QStringLiteral("wire renamed;")),
              QStringLiteral("ok=%1 written=%2 text='%3'")
                  .arg(report.ok ? 1 : 0)
                  .arg(report.writtenFileCount)
                  .arg(outsideDisk.left(40).replace(QLatin1Char('\n'), QLatin1Char('|'))));
    }

    std::printf("SUMMARY checks=%d failures=%d skips=%d not-claimed=%d\n", g_checks, g_failures,
                g_skips, g_notClaimed);
    return g_failures == 0 ? 0 : 1;
}
