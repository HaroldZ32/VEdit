// AC-T13.2–13.5 — the rename preview / atomic apply / cancel probe (Xvfb+xcb).
//
// This probe drives the REAL product path, not a fake seam:
//
//   ApplicationShell (real shell, real theme) + the real KTextEditor provider
//     -> the real diagnostics lane publishes the project file set (the SAME
//        property the file tree consumes)
//     -> installRename registers `rename.symbol` in the real CommandRegistry
//     -> the command's trigger reads the identifier under the cursor from the
//        real KTextEditor view and opens the real preview dialog
//     -> Preview / Apply / Cancel run the REAL Rust jobs through the frozen
//        C ABI; every assertion below reads a RENDERED widget text or a file's
//        SHA-256, never private state.
//
// It is deliberately NOT the LSP `diagnostics.rename` e2e (`vedit-rename-e2e`):
// that one covers textDocument/rename over the language server; this one covers
// the engineering-wide slang-AST rename and its atomic apply.
//
//   build: vedit-rename-ui-probe --fixture <tests/fixtures/rename> --mode <mode>
//
// Modes (one scenario per run, so each evidence log quotes ONE observed path):
//   preview, ambiguous, unsupported, noop, illegal, conflict,
//   apply, failure, cancel-preview, cancel-apply
//
// The probe requires an X server: QT_QPA_PLATFORM=xcb under xvfb-run. Any other
// platform SKIPs with exit 77 and the observed platform name — never a silent
// pass. `failure` additionally requires a non-root euid, because it makes one
// target directory read-only for the apply; as root that would not block the
// write, so the mode SKIPs instead of claiming a rollback it did not produce.
#include "rename_integration.h"
#include "rename_preview.h"

#include "runtime_init.h"

#include "app_shell.h"
#include "command_registry.h"
#include "diagnostics_integration.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>

#include <unistd.h>

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
    std::fflush(stdout);
}

void observed(const QString &line)
{
    std::printf("OBSERVED %s\n", line.toUtf8().constData());
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

QString sha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("<unreadable>");
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

QMap<QString, QString> treeHashes(const QString &root)
{
    QMap<QString, QString> hashes;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        hashes.insert(QDir(root).relativeFilePath(path), sha256(path));
    }
    return hashes;
}

QString changedFiles(const QMap<QString, QString> &before, const QMap<QString, QString> &after)
{
    QStringList changed;
    for (auto it = before.begin(); it != before.end(); ++it) {
        if (!after.contains(it.key()) || after.value(it.key()) != it.value()) {
            changed.append(it.key());
        }
    }
    for (auto it = after.begin(); it != after.end(); ++it) {
        if (!before.contains(it.key())) {
            changed.append(it.key());
        }
    }
    changed.sort();
    return changed.join(QLatin1Char(','));
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

bool copyTree(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    const QDir sourceRoot(from);
    while (it.hasNext()) {
        const QString source = it.next();
        const QFileInfo info(source);
        const QString target = QDir(to).filePath(sourceRoot.relativeFilePath(source));
        if (info.isDir()) {
            if (!QDir().mkpath(target)) {
                return false;
            }
            continue;
        }
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            return false;
        }
        if (!QFile::copy(source, target)) {
            return false;
        }
    }
    return true;
}

QString stateName(vedit::workspace::RenamePreviewState state)
{
    using vedit::workspace::RenamePreviewState;
    switch (state) {
    case RenamePreviewState::Editing: return QStringLiteral("editing");
    case RenamePreviewState::Planning: return QStringLiteral("planning");
    case RenamePreviewState::Ready: return QStringLiteral("ready");
    case RenamePreviewState::Applying: return QStringLiteral("applying");
    case RenamePreviewState::Applied: return QStringLiteral("applied");
    case RenamePreviewState::Failed: return QStringLiteral("failed");
    case RenamePreviewState::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

struct Scenario {
    QString mode;
    QString openFile; // project-relative
    QString symbol;   // the identifier under the cursor
    int lineHint = 0; // 0-based line that carries the declaration
    QString newName;
    bool multiFile = false;
};

Scenario scenarioFor(const QString &mode)
{
    if (mode == QStringLiteral("ambiguous")) {
        return {mode, QStringLiteral("rtl/alpha.sv"), QStringLiteral("cnt"), 10,
                QStringLiteral("cnt_renamed")};
    }
    if (mode == QStringLiteral("unsupported")) {
        return {mode, QStringLiteral("rtl/alpha.sv"), QStringLiteral("WIDTH"), 2,
                QStringLiteral("WIDTH_RENAMED")};
    }
    if (mode == QStringLiteral("noop")) {
        return {mode, QStringLiteral("rtl/alpha.sv"), QStringLiteral("alpha_cnt"), 8,
                QStringLiteral("alpha_cnt")};
    }
    if (mode == QStringLiteral("illegal")) {
        return {mode, QStringLiteral("rtl/alpha.sv"), QStringLiteral("alpha_cnt"), 8,
                QStringLiteral("1not_a_name")};
    }
    if (mode == QStringLiteral("conflict")) {
        return {mode, QStringLiteral("rtl/alpha.sv"), QStringLiteral("alpha_cnt"), 8,
                QStringLiteral("cnt")};
    }
    if (mode == QStringLiteral("apply") || mode == QStringLiteral("failure")
        || mode == QStringLiteral("cancel-apply")) {
        return {mode, QStringLiteral("zbroken/pkg_shared.sv"), QStringLiteral("shared_flag"), 1,
                QStringLiteral("shared_flag_renamed"), true};
    }
    if (mode == QStringLiteral("cancel-preview")) {
        return {mode, QStringLiteral("zbroken/pkg_shared.sv"), QStringLiteral("shared_flag"), 1,
                QStringLiteral("shared_flag_renamed"), true};
    }
    // preview (the default and the ctest scenario)
    return {QStringLiteral("preview"), QStringLiteral("rtl/alpha.sv"), QStringLiteral("alpha_cnt"),
            8, QStringLiteral("alpha_count")};
}

/// A three-file fixture the ENGINE can really plan: a package-level variable
/// referenced UNQUALIFIED through `import pkg::*;` in two modules. A qualified
/// `pkg::name` reference spans the whole path in the AST (the engine refuses it
/// whole), so the import form is the one that exercises a real multi-file plan
/// and a real multi-file rollback.
bool writeMultiFileFixture(const QString &root)
{
    const QString package =
        QStringLiteral("package shared;\n"
                       "  logic shared_flag;\n"
                       "endpackage\n");
    const QString moduleA =
        QStringLiteral("module shared_a;\n"
                       "  import shared::*;\n"
                       "  initial begin\n"
                       "    shared_flag = 1'b1;\n"
                       "  end\n"
                       "endmodule\n");
    const QString moduleB =
        QStringLiteral("module shared_b;\n"
                       "  import shared::*;\n"
                       "  initial begin\n"
                       "    shared_flag = 1'b0;\n"
                       "  end\n"
                       "endmodule\n");
    return writeFile(root + QStringLiteral("/zbroken/pkg_shared.sv"), package)
        && writeFile(root + QStringLiteral("/rtl_apply/a_shared.sv"), moduleA)
        && writeFile(root + QStringLiteral("/rtl_apply/b_shared.sv"), moduleB);
}

bool setCursorOnToken(KTextEditor::View *view, const QString &token, int lineHint)
{
    if (view == nullptr || view->document() == nullptr) {
        return false;
    }
    KTextEditor::Document *document = view->document();
    const QStringList lines = document->text().split(QLatin1Char('\n'));
    QList<int> candidates;
    if (lineHint >= 0 && lineHint < lines.size()) {
        candidates.append(lineHint);
    }
    for (int line = 0; line < lines.size(); ++line) {
        if (line != lineHint) {
            candidates.append(line);
        }
    }
    for (int line : candidates) {
        const int column = lines.at(line).indexOf(token);
        if (column < 0) {
            continue;
        }
        view->setCursorPosition(KTextEditor::Cursor(line, column));
        return true;
    }
    return false;
}

QString joined(const QStringList &lines)
{
    return lines.join(QStringLiteral(" | "));
}

} // namespace

int main(int argc, char **argv)
{
    // T134 / AGENT.md §4: this probe links the Rust service layer and runs the
    // REAL slang-AST jobs through it, so the Rust runtime's `SIGPIPE -> SIG_IGN`
    // default is not installed here unless the host does it first.
    vedit_runtime_init();
    QApplication app(argc, argv);

    QString fixture;
    QString mode = QStringLiteral("preview");
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--fixture") && index + 1 < argc) {
            fixture = QString::fromLocal8Bit(argv[++index]);
        } else if (argument == QStringLiteral("--mode") && index + 1 < argc) {
            mode = QString::fromLocal8Bit(argv[++index]);
        }
    }
    const Scenario scenario = scenarioFor(mode);
    std::printf("PROBE mode=%s platform=%s\n", scenario.mode.toUtf8().constData(),
                QGuiApplication::platformName().toUtf8().constData());
    std::fflush(stdout);

    if (QGuiApplication::platformName() != QStringLiteral("xcb")) {
        std::printf("SKIP probe.platform  the probe requires xcb (observed '%s'); run it under "
                    "xvfb-run with QT_QPA_PLATFORM=xcb\n",
                    QGuiApplication::platformName().toUtf8().constData());
        return 77;
    }
    if (fixture.isEmpty() || !QFileInfo::exists(fixture + QStringLiteral("/rtl/alpha.sv"))) {
        std::printf("FAIL probe.fixture  no --fixture <tests/fixtures/rename> (got '%s')\n",
                    fixture.toUtf8().constData());
        return 2;
    }
    if (scenario.mode == QStringLiteral("failure") && geteuid() == 0) {
        std::printf("SKIP probe.failure-permissions  a read-only directory does not block root; "
                    "run the failure mode as a non-root user\n");
        return 77;
    }

    // A SHORT scratch root: the frozen C ABI caps one reason at 255 bytes, and
    // an ambiguous refusal lists every declaration with its absolute path. A
    // long QTemporaryDir path can push the last declaration past the cap; the
    // cap itself is reported (the dialog appends a note when it is reached).
    const QString base = QStringLiteral("/tmp/rnui-%1").arg(QCoreApplication::applicationPid());
    QDir(base).removeRecursively();
    const bool scratch = QDir().mkpath(base + QStringLiteral("/p"));
    check("probe.tempdir", scratch, base);
    if (!scratch) {
        return 1;
    }
    const QString project = base + QStringLiteral("/p");
    check("probe.fixture-copy", copyTree(fixture, project), fixture);
    // T72: the rename ABI carries the project's own `-I`/`-D` now, so the
    // fixture's `include "defs.vh"` must resolve from `inc/` WITHOUT copying the
    // header next to the including file. That copy worked around the missing ABI
    // input; leaving `rtl/defs.vh` absent is the proof that the workaround is
    // gone and the product passes the project's expansion include dirs.
    check("probe.fixture-include-outside",
          !QFileInfo::exists(project + QStringLiteral("/rtl/defs.vh")),
          QStringLiteral("inc/defs.vh stays in inc/; the lane passes the project's -I"));
    if (scenario.multiFile) {
        check("probe.multi-fixture", writeMultiFileFixture(project));
    }
    const QMap<QString, QString> before = treeHashes(project);
    check("probe.fixture-files", before.size() >= 6, QString::number(before.size()));
    const auto printHashes = [&project](const char *phase) {
        const QMap<QString, QString> hashes = treeHashes(project);
        for (auto it = hashes.begin(); it != hashes.end(); ++it) {
            observed(QStringLiteral("sha256-%1 '%2' %3")
                         .arg(QString::fromLatin1(phase), it.key(), it.value()));
        }
    };
    printHashes("before");

    vedit::app::ShellOptions options;
    options.stateDir = base + QStringLiteral("/state");
    options.projectRoot = project;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    vedit::app::ApplicationShell shell(options);
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    shell.editorArea()->ensurePane();
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = project;
    integration.stateDir = options.stateDir;
    // The REAL project file set publisher (the same property the file tree
    // consumes); the rename lane deliberately owns no second scan.
    vedit::workspace::installDiagnostics(shell, integration);
    vedit::workspace::installRename(shell, integration);
    shell.resize(1280, 800);
    shell.show();
    QCoreApplication::processEvents();

    const bool setArrived = waitUntil(
        [&shell]() {
            return shell.property("vedit.diagnostics.projectFilesRevision").toULongLong() > 0
                && !shell.property("vedit.diagnostics.projectFiles").toStringList().isEmpty();
        },
        120000);
    const QStringList projectFiles =
        shell.property("vedit.diagnostics.projectFiles").toStringList();
    check("probe.project-file-set", setArrived,
          QStringLiteral("files=%1").arg(projectFiles.size()));
    observed(QStringLiteral("project-file-set files=%1").arg(projectFiles.size()));
    if (scenario.multiFile) {
        for (const QString &name : {QStringLiteral("a_shared.sv"), QStringLiteral("b_shared.sv"),
                                    QStringLiteral("pkg_shared.sv")}) {
            bool found = false;
            for (const QString &file : projectFiles) {
                if (file.endsWith(QLatin1Char('/') + name)) {
                    found = true;
                }
            }
            const QByteArray label = (QStringLiteral("probe.file-set.") + name).toUtf8();
            check(label.constData(), found, name);
        }
    }

    vedit::app::EditorPane *pane = shell.editorArea()->activePane();
    QString openError;
    const QString openPath = project + QLatin1Char('/') + scenario.openFile;
    const bool opened = pane != nullptr && pane->openFile(openPath, &openError);
    check("probe.document-opened", opened, openError);
    auto *view = pane != nullptr ? qobject_cast<KTextEditor::View *>(pane->editorWidget())
                                 : nullptr;
    check("probe.ktexteditor-view", view != nullptr);
    if (!opened || view == nullptr) {
        std::printf("SUMMARY mode=%s checks=%d failures=%d\n", scenario.mode.toUtf8().constData(),
                    g_checks, g_failures);
        return 1;
    }
    check("probe.cursor", setCursorOnToken(view, scenario.symbol, scenario.lineHint),
          scenario.symbol);

    QAction *action = shell.commands() != nullptr
        ? shell.commands()->action(QStringLiteral("rename.symbol"))
        : nullptr;
    check("probe.action", action != nullptr);
    // The lane polls the published file set (400 ms); wait for the gate to open
    // before triggering, exactly as a user waits for the menu entry to enable.
    if (action != nullptr) {
        const bool enabled = waitUntil([action]() { return action->isEnabled(); }, 15000);
        check("probe.action-enabled", enabled, action->toolTip());
    }
    if (action == nullptr) {
        std::printf("SUMMARY mode=%s checks=%d failures=%d\n", scenario.mode.toUtf8().constData(),
                    g_checks, g_failures);
        return 1;
    }

    // The GUI thread keeps turning while a job runs: this timer proves the
    // event loop was not blocked by the plan/apply work.
    int ticks = 0;
    QTimer ticker;
    ticker.setInterval(1);
    QObject::connect(&ticker, &QTimer::timeout, [&ticks]() { ++ticks; });
    ticker.start();

    action->trigger();
    auto *dialog = shell.findChild<vedit::workspace::RenamePreviewDialog *>();
    check("probe.dialog", dialog != nullptr);
    if (dialog == nullptr) {
        std::printf("SUMMARY mode=%s checks=%d failures=%d\n", scenario.mode.toUtf8().constData(),
                    g_checks, g_failures);
        return 1;
    }
    check("probe.dialog-visible", waitUntil([dialog]() { return dialog->isVisible(); }, 5000));
    check("probe.dialog-non-modal", !dialog->isModal());
    check("probe.dialog-parent", dialog->parentWidget() == &shell);
    check("probe.dialog-symbol", dialog->symbol() == scenario.symbol, dialog->symbol());
    observed(QStringLiteral("dialog symbol='%1'").arg(dialog->symbol()));

    // AC-N6/R2-1: the unsupported classes are named AT ALL TIMES, before any
    // job, each with its reason — not only after a refusal.
    const QString unsupported = dialog->unsupportedText();
    check("probe.unsupported-parameter", unsupported.contains(QStringLiteral("parameter/constant"))
              && unsupported.contains(QStringLiteral("type-dimension")),
          unsupported.left(120));
    check("probe.unsupported-instance", unsupported.contains(QStringLiteral("instance name")));
    check("probe.unsupported-connection",
          unsupported.contains(QStringLiteral("connection label")));
    check("probe.unsupported-hierarchical",
          unsupported.contains(QStringLiteral("hierarchical reference")));
    check("probe.hint-zero-writes", dialog->hintText().contains(QStringLiteral("write nothing")),
          dialog->hintText());
    for (const QString &line : dialog->unsupportedText().split(QLatin1Char('\n'))) {
        observed(QStringLiteral("unsupported '%1'").arg(line));
    }

    dialog->nameEdit()->setText(scenario.newName);
    dialog->previewButton()->click();

    if (scenario.mode == QStringLiteral("cancel-preview")) {
        check("cancel-preview.progress-visible", dialog->progressVisible());
        check("cancel-preview.cancel-visible", dialog->cancelVisible());
        check("cancel-preview.apply-disabled", !dialog->applyEnabled());
        dialog->cancelButton()->click();
        const bool settled = waitUntil(
            [dialog]() {
                return dialog->state() == vedit::workspace::RenamePreviewState::Cancelled
                    || dialog->state() == vedit::workspace::RenamePreviewState::Failed;
            },
            120000);
        check("cancel-preview.terminal", settled, stateName(dialog->state()));
        check("cancel-preview.state",
              dialog->state() == vedit::workspace::RenamePreviewState::Cancelled,
              stateName(dialog->state()));
        const QString changed = changedFiles(before, treeHashes(project));
        check("cancel-preview.zero-writes", changed.isEmpty(), changed);
        check("cancel-preview.status",
              dialog->statusText().contains(QStringLiteral("nothing was written")),
              dialog->statusText());
        observed(QStringLiteral("cancel-preview state=%1 status='%2'")
                     .arg(stateName(dialog->state()), dialog->statusText()));
        observed(QStringLiteral("cancel-preview changed='%1'").arg(changed));
        printHashes("after");
        ticker.stop();
        std::printf("SUMMARY mode=%s checks=%d failures=%d\n", scenario.mode.toUtf8().constData(),
                    g_checks, g_failures);
        return g_failures == 0 ? 0 : 1;
    }

    const bool settled = waitUntil(
        [dialog]() {
            return dialog->state() == vedit::workspace::RenamePreviewState::Ready
                || dialog->state() == vedit::workspace::RenamePreviewState::Failed
                || dialog->state() == vedit::workspace::RenamePreviewState::Cancelled;
        },
        180000);
    check("plan.terminal", settled, stateName(dialog->state()));
    observed(QStringLiteral("plan state=%1 status='%2'")
                 .arg(stateName(dialog->state()), dialog->statusText()));

    if (scenario.mode == QStringLiteral("ambiguous")) {
        check("ambiguous.refused",
              dialog->state() == vedit::workspace::RenamePreviewState::Failed,
              stateName(dialog->state()));
        const QString status = dialog->statusText();
        check("ambiguous.code", status.startsWith(QStringLiteral("ambiguous:")), status);
        check("ambiguous.all-declarations",
              status.contains(QStringLiteral("alpha.sv:11"))
                  && status.contains(QStringLiteral("gamma.sv:5"))
                  && status.contains(QStringLiteral("alpha_other.sv:5")),
              status);
        check("ambiguous.zero-writes", changedFiles(before, treeHashes(project)).isEmpty(),
              changedFiles(before, treeHashes(project)));
        printHashes("after");
        observed(QStringLiteral("ambiguous status='%1'").arg(status));
    } else if (scenario.mode == QStringLiteral("unsupported")) {
        check("unsupported.refused",
              dialog->state() == vedit::workspace::RenamePreviewState::Failed);
        const QString status = dialog->statusText();
        check("unsupported.code", status.startsWith(QStringLiteral("unsupported:")), status);
        check("unsupported.reason",
              status.contains(QStringLiteral("parameter/constant"))
                  && status.contains(QStringLiteral("type-dimension")),
              status);
        check("unsupported.no-rows", dialog->renderedRows().isEmpty(),
              joined(dialog->renderedRows()));
        check("unsupported.zero-writes", changedFiles(before, treeHashes(project)).isEmpty());
        printHashes("after");
        observed(QStringLiteral("unsupported status='%1'").arg(status));
    } else if (scenario.mode == QStringLiteral("noop")) {
        check("noop.refused",
              dialog->state() == vedit::workspace::RenamePreviewState::Failed);
        check("noop.reason", dialog->statusText().contains(QStringLiteral("no-op")),
              dialog->statusText());
        check("noop.zero-writes", changedFiles(before, treeHashes(project)).isEmpty());
        printHashes("after");
        observed(QStringLiteral("noop status='%1'").arg(dialog->statusText()));
    } else if (scenario.mode == QStringLiteral("illegal")) {
        check("illegal.refused",
              dialog->state() == vedit::workspace::RenamePreviewState::Failed);
        check("illegal.reason",
              dialog->statusText().contains(QStringLiteral("not a SystemVerilog identifier")),
              dialog->statusText());
        check("illegal.zero-writes", changedFiles(before, treeHashes(project)).isEmpty());
        printHashes("after");
        observed(QStringLiteral("illegal status='%1'").arg(dialog->statusText()));
    } else if (scenario.mode == QStringLiteral("conflict")) {
        check("conflict.refused",
              dialog->state() == vedit::workspace::RenamePreviewState::Failed);
        const QString status = dialog->statusText();
        check("conflict.code", status.startsWith(QStringLiteral("conflict:")), status);
        check("conflict.declaration",
              status.contains(QStringLiteral("already declared at"))
                  && status.contains(QStringLiteral("alpha.sv:11")),
              status);
        check("conflict.zero-writes", changedFiles(before, treeHashes(project)).isEmpty());
        printHashes("after");
        observed(QStringLiteral("conflict status='%1'").arg(status));
    } else {
        // The preview / apply / failure paths.
        check("preview.ready",
              dialog->state() == vedit::workspace::RenamePreviewState::Ready,
              stateName(dialog->state()));
        const QStringList files = dialog->renderedFiles();
        const QStringList rows = dialog->renderedRows();
        const QString total = dialog->totalText();
        for (const QString &file : files) {
            observed(QStringLiteral("preview-file '%1'").arg(file));
        }
        for (const QString &row : rows) {
            observed(QStringLiteral("preview-row '%1'").arg(row));
        }
        observed(QStringLiteral("preview-total '%1'").arg(total));

        const bool multi = scenario.multiFile;
        const int expectedFiles = multi ? 3 : 1;
        const int expectedRows = 3;
        check("preview.file-groups", files.size() == expectedFiles, QString::number(files.size()));
        check("preview.row-count", rows.size() == expectedRows, QString::number(rows.size()));
        check("preview.total-shape",
              total.contains(QStringLiteral("TOTAL: 3 replacements in 3 files"))
                  || total.contains(QStringLiteral("TOTAL: 3 replacements in 1 file")),
              total);
        check("preview.total-names-symbol",
              total.contains(scenario.symbol) && total.contains(scenario.newName), total);
        // Per-row file:line + before -> after (AC-T13.2's literal shape).
        bool rowShape = !rows.isEmpty();
        for (const QString &row : rows) {
            rowShape = rowShape && row.contains(QStringLiteral("  ->  "))
                && row.contains(QRegularExpression(QStringLiteral(":[0-9]+  ")))
                && row.contains(scenario.symbol) && row.contains(scenario.newName);
        }
        check("preview.row-shape", rowShape, joined(rows));
        // The file group owns its rows (grouping, not a flat list).
        const bool grouped = dialog->tree()->topLevelItemCount() == expectedFiles
            && [dialog]() {
                   for (int index = 0; index < dialog->tree()->topLevelItemCount(); ++index) {
                       if (dialog->tree()->topLevelItem(index)->childCount() == 0) {
                           return false;
                       }
                   }
                   return true;
               }();
        check("preview.grouped", grouped);
        check("preview.zero-writes", changedFiles(before, treeHashes(project)).isEmpty(),
              changedFiles(before, treeHashes(project)));
        check("preview.apply-enabled", dialog->applyEnabled());
        check("preview.cancel-hidden", !dialog->cancelVisible());
        check("preview.progress-hidden", !dialog->progressVisible());
        check("preview.gui-alive", ticks > 0, QString::number(ticks));

        if (scenario.mode == QStringLiteral("preview")) {
            // The negative controls: no unrelated symbol appears in the plan.
            bool unrelated = false;
            for (const QString &row : rows) {
                unrelated = unrelated || row.contains(QStringLiteral("u_alpha"));
            }
            check("preview.no-unrelated-symbols", !unrelated, joined(rows));
            observed(QStringLiteral("preview mode=preview zero-writes=1"));
            printHashes("after");
            ticker.stop();
            std::printf("SUMMARY mode=%s checks=%d failures=%d\n",
                        scenario.mode.toUtf8().constData(), g_checks, g_failures);
            return g_failures == 0 ? 0 : 1;
        }

        if (scenario.mode == QStringLiteral("failure")) {
            const QString brokenDir = project + QStringLiteral("/zbroken");
            const QFile::Permissions original = QFile::permissions(brokenDir);
            const bool locked = QFile::setPermissions(
                brokenDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner
                               | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                               | QFileDevice::ReadOther | QFileDevice::ExeOther);
            check("failure.read-only-dir", locked, brokenDir);
            dialog->applyButton()->click();
            check("failure.applying",
                  dialog->state() == vedit::workspace::RenamePreviewState::Applying);
            const bool failed = waitUntil(
                [dialog]() {
                    return dialog->state() == vedit::workspace::RenamePreviewState::Failed;
                },
                180000);
            QFile::setPermissions(brokenDir, original);
            const QString status = dialog->statusText();
            check("failure.terminal", failed, stateName(dialog->state()));
            check("failure.names-file", status.contains(QStringLiteral("zbroken/pkg_shared.sv")),
                  status);
            check("failure.rolled-back", status.contains(QStringLiteral("rolled back=1")), status);
            const QString changed = changedFiles(before, treeHashes(project));
            check("failure.zero-writes", changed.isEmpty(), changed);
            observed(QStringLiteral("failure status='%1'").arg(status));
            observed(QStringLiteral("failure changed='%1'").arg(changed));
            printHashes("after");
            ticker.stop();
            std::printf("SUMMARY mode=%s checks=%d failures=%d\n",
                        scenario.mode.toUtf8().constData(), g_checks, g_failures);
            return g_failures == 0 ? 0 : 1;
        }

        // apply and cancel-apply: the ONE confirmation.
        dialog->applyButton()->click();
        check("apply.applying",
              dialog->state() == vedit::workspace::RenamePreviewState::Applying,
              stateName(dialog->state()));
        check("apply.apply-disabled", !dialog->applyEnabled());
        check("apply.progress-visible", dialog->progressVisible());
        check("apply.cancel-visible", dialog->cancelVisible());
        // ONE confirmation: a second click while the job runs starts nothing.
        dialog->applyButton()->click();
        check("apply.one-job", dialog->state() == vedit::workspace::RenamePreviewState::Applying,
              stateName(dialog->state()));

        if (scenario.mode == QStringLiteral("cancel-apply")) {
            dialog->cancelButton()->click();
            const bool cancelled = waitUntil(
                [dialog]() {
                    return dialog->state() == vedit::workspace::RenamePreviewState::Cancelled
                        || dialog->state() == vedit::workspace::RenamePreviewState::Failed;
                },
                180000);
            const QString changed = changedFiles(before, treeHashes(project));
            check("cancel-apply.terminal", cancelled, stateName(dialog->state()));
            check("cancel-apply.state",
                  dialog->state() == vedit::workspace::RenamePreviewState::Cancelled,
                  stateName(dialog->state()));
            check("cancel-apply.zero-writes", changed.isEmpty(), changed);
            check("cancel-apply.rolled-back",
                  dialog->statusText().contains(QStringLiteral("nothing was renamed")),
                  dialog->statusText());
            observed(QStringLiteral("cancel-apply status='%1'").arg(dialog->statusText()));
            observed(QStringLiteral("cancel-apply changed='%1'").arg(changed));
            printHashes("after");
            ticker.stop();
            std::printf("SUMMARY mode=%s checks=%d failures=%d\n",
                        scenario.mode.toUtf8().constData(), g_checks, g_failures);
            return g_failures == 0 ? 0 : 1;
        }

        const bool applied = waitUntil(
            [dialog]() {
                return dialog->state() == vedit::workspace::RenamePreviewState::Applied
                    || dialog->state() == vedit::workspace::RenamePreviewState::Failed;
            },
            180000);
        check("apply.terminal", applied, stateName(dialog->state()));
        check("apply.state", dialog->state() == vedit::workspace::RenamePreviewState::Applied,
              stateName(dialog->state()));
        const QString status = dialog->statusText();
        check("apply.verified", status.contains(QStringLiteral("verified=1")), status);
        check("apply.counts",
              status.contains(QStringLiteral("Applied 3 replacements in 3 files")), status);
        const QRegularExpression oldName(QStringLiteral("\\bshared_flag\\b"));
        int renamed = 0;
        int leftBehind = 0;
        for (const QString &relative : {QStringLiteral("zbroken/pkg_shared.sv"),
                                        QStringLiteral("rtl_apply/a_shared.sv"),
                                        QStringLiteral("rtl_apply/b_shared.sv")}) {
            QFile file(project + QLatin1Char('/') + relative);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString text = QString::fromUtf8(file.readAll());
            if (text.contains(QStringLiteral("shared_flag_renamed"))) {
                ++renamed;
            }
            if (oldName.match(text).hasMatch()) {
                ++leftBehind;
            }
        }
        check("apply.three-files-renamed", renamed == 3, QString::number(renamed));
        check("apply.no-old-name-left", leftBehind == 0, QString::number(leftBehind));
        const QString changed = changedFiles(before, treeHashes(project));
        check("apply.changed-three-files",
              changed == QStringLiteral("rtl_apply/a_shared.sv,rtl_apply/b_shared.sv,"
                                        "zbroken/pkg_shared.sv"),
              changed);
        observed(QStringLiteral("apply status='%1'").arg(status));
        observed(QStringLiteral("apply changed='%1'").arg(changed));
        printHashes("after");
    }

    ticker.stop();
    std::printf("SUMMARY mode=%s checks=%d failures=%d\n", scenario.mode.toUtf8().constData(),
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
