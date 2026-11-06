// Product-seam self-test for T118: "the user opens a file and edits one line".
//
// It builds the REAL ApplicationShell, installs the diagnostics lane exactly like
// vedit-app does (no --project: the double-click case), opens a file in the active
// pane through the documented document seam, and then edits the BUFFER only.
//
// Asserted (headless, offscreen):
//   * opening a file starts an analysis on its own (no "Reparse Project" click);
//   * the panel shows "analyzed <file> ... diagnostic(s)" without any manual call;
//   * a buffer-only edit (deleted comma, not saved) produces a syntax diagnostic;
//   * the file on disk is untouched, which is what proves the BUFFER was linted.
//
// Requires the real verible-verilog-ls (bundled/local/PATH/env); it SKIPs with an
// explicit reason instead of pretending when the toolchain is absent.
#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "diagnostics_panel.h"

#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "diagnostics_annotations.h"
#include "ktexteditor_provider.h"
#include "panel_registry.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QImage>
#include <QPixmap>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QThread>
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
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
    std::fflush(stdout);
}

void skip(const char *name, const QString &why)
{
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
        QThread::msleep(10);
    }
    return true;
}

void setBufferText(QWidget *editor, const QString &text)
{
    if (auto *view = qobject_cast<KTextEditor::View *>(editor)) {
        if (view->document() != nullptr) {
            view->document()->setText(text);
        }
        return;
    }
    if (auto *plain = qobject_cast<QPlainTextEdit *>(editor)) {
        plain->setPlainText(text);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        skip("seam.temp-dir", QCoreApplication::translate("VEdit", "no temporary directory"));
        std::printf("VE-DIAGNOSTICS-PRODUCT-SEAM checks=%d failures=%d skipped=%d\n", g_checks,
                    g_failures, g_skips);
        return g_failures == 0 ? 0 : 1;
    }
    const QString file = dir.path() + QStringLiteral("/edited.sv");
    const QString goodText =
        QStringLiteral("module edited;\n  logic a, b;\n  always_comb begin\n    a = b;\n"
                       "  end\nendmodule\n");
    const QString brokenText =
        QStringLiteral("module edited;\n  logic a b;\n  always_comb begin\n    a = b;\n"
                       "  end\nendmodule\n");
    {
        QFile onDisk(file);
        check("seam.file-written", onDisk.open(QIODevice::WriteOnly)
                                      && onDisk.write(goodText.toUtf8()) == goodText.toUtf8().size());
    }

    vedit::app::ShellOptions options;
    options.stateDir = dir.path() + QStringLiteral("/state");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    // The PRODUCT editor (KTextEditor): inline annotations only exist on a real
    // KTextEditor::View. The provider outlives the shell (declared first).
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    check("seam.edit-core-provider", provider != nullptr);
    std::unique_ptr<vedit::app::ApplicationShell> shell(new vedit::app::ApplicationShell(options));
    if (provider != nullptr) {
        shell->editorArea()->setProvider(provider.get());
    }
    shell->editorArea()->ensurePane();
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = QString(); // the double-click case: no project at all
    integration.stateDir = dir.path() + QStringLiteral("/state");
    vedit::workspace::installDiagnostics(*shell, integration);
    shell->panels()->showPanel(QStringLiteral("outline"), shell.get());
    shell->resize(1280, 860);
    shell->show();
    QCoreApplication::processEvents();

    auto *panel = shell->findChild<vedit::workspace::diagnostics::DiagnosticsPanel *>();
    check("seam.panel-installed", panel != nullptr);

    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    check("seam.active-pane", pane != nullptr);
    if (panel == nullptr || pane == nullptr) {
        std::printf("VE-DIAGNOSTICS-PRODUCT-SEAM checks=%d failures=%d skipped=%d\n", g_checks,
                    g_failures, g_skips);
        return g_failures == 0 ? 0 : 1;
    }

    // "The user opens the file" - the same document seam the shell uses.
    QString openError;
    check("seam.file-opened", pane->openFile(file, &openError), openError);
    const bool analyzedAfterOpen = waitUntil(
        [&panel]() { return panel->statusText().contains(QStringLiteral("analyzed")); }, 30000);
    if (!analyzedAfterOpen
        && panel->statusText().contains(QCoreApplication::translate("VEdit", "Diagnostics unavailable"))) {
        skip("seam.analysis-after-open", panel->statusText());
        skip("seam.panel-shows-buffer-source", QStringLiteral("no toolchain"));
        skip("seam.buffer-edit-diagnosed", QStringLiteral("no toolchain"));
        skip("seam.disk-untouched", QStringLiteral("no toolchain"));
        std::printf("VE-DIAGNOSTICS-PRODUCT-SEAM checks=%d failures=%d skipped=%d\n", g_checks,
                    g_failures, g_skips);
        return g_failures == 0 ? 0 : 1;
    }
    check("seam.analysis-after-open", analyzedAfterOpen, panel->statusText());
    std::printf("OBSERVED seam.after-open status='%s'\n",
                panel->statusText().toUtf8().constData());

    // "The user deletes one comma" - in the BUFFER only.
    QWidget *editor = pane->editorWidget();
    check("seam.editor-widget", editor != nullptr);
    if (editor != nullptr) {
        setBufferText(editor, brokenText);
        const bool diagnosed = waitUntil(
            [&panel]() {
                return panel->statusText().contains(QStringLiteral("not saved"))
                       && !panel->statusText().contains(QCoreApplication::translate("VEdit", ": 0 diagnostic(s)"));
            },
            30000);
        std::printf("OBSERVED seam.after-edit status='%s'\n",
                    panel->statusText().toUtf8().constData());
        std::printf("OBSERVED seam.controller state=%d session='%s' diagnostics=%lld analyses=%llu\n",
                    static_cast<int>(panel->controller()->state()),
                    qPrintable(panel->controller()->session().message),
                    static_cast<long long>(panel->controller()->diagnostics().size()),
                    static_cast<unsigned long long>(panel->controller()->analysisCount()));
        check("seam.panel-shows-buffer-source",
              panel->statusText().contains(QStringLiteral("not saved")), panel->statusText());
        check("seam.buffer-edit-diagnosed", diagnosed, panel->statusText());
        QFile onDisk(file);
        QString disk;
        if (onDisk.open(QIODevice::ReadOnly)) {
            disk = QString::fromUtf8(onDisk.readAll());
        }
        check("seam.disk-untouched", disk == goodText,
              QStringLiteral("disk=%1 bytes").arg(disk.size()));
        check("seam.panel-lists-the-issue", panel->issueRowCount() > 0,
              QString::number(panel->issueRowCount()));

        // T131: the same diagnostics must be INLINE in the editor (annotation border).
        auto *view = qobject_cast<KTextEditor::View *>(editor);
        check("seam.ktexteditor-view", view != nullptr);
        if (view != nullptr) {
            auto *annotations = vedit::editor::DiagnosticsAnnotations::forView(view);
            check("seam.annotations-installed", annotations != nullptr);
            if (annotations != nullptr) {
                // The bridge pushes on its own 300 ms tick: give it a moment.
                waitUntil([annotations]() { return annotations->annotatedLineCount() > 0; }, 5000);
            }
            if (annotations != nullptr) {
                const int lines = annotations->annotatedLineCount();
                const QString tooltip = annotations->tooltipForLine(1); // "logic a b;" is line 1
                std::printf("OBSERVED seam.annotation_lines=%d tooltip='%s'\n", lines,
                            tooltip.toUtf8().constData());
                check("seam.annotation-on-error-line", lines >= 1 && !tooltip.isEmpty(), tooltip);
                check("seam.annotation-tooltip-has-syntax",
                      tooltip.contains(QStringLiteral("syntax"), Qt::CaseInsensitive), tooltip);
            }
        }

        // Pixel evidence: the window with the inline marker AND the diagnostics
        // panel visible in one frame.
        const QString shotPath = qEnvironmentVariable("VEDIT_SEAM_SCREENSHOT");
        if (!shotPath.isEmpty()) {
            const QPixmap shot = shell->grab();
            check("seam.screenshot-saved", !shot.isNull() && shot.save(shotPath),
                  QStringLiteral("%1 (%2x%3)").arg(shotPath).arg(shot.width()).arg(shot.height()));
        }
    }

    // ---- reverse control: without ActiveFileWatcher the same open does NOT
    // analyse anything (so step 1 of the acceptance is not vacuous).
    {
        qputenv("VEDIT_NO_ACTIVE_FILE_WATCHER", "1");
        vedit::app::ShellOptions options2;
        options2.stateDir = dir.path() + QStringLiteral("/state2");
        options2.restoreSession = false;
        options2.showDefaultPanels = false;
        vedit::app::DemoEditorProvider provider2;
        std::unique_ptr<vedit::app::ApplicationShell> shell2(
            new vedit::app::ApplicationShell(options2));
        shell2->editorArea()->setProvider(&provider2);
        shell2->editorArea()->ensurePane();
        vedit::workspace::IntegrationOptions integration2;
        integration2.stateDir = dir.path() + QStringLiteral("/state2");
        vedit::workspace::installDiagnostics(*shell2, integration2);
        shell2->panels()->showPanel(QStringLiteral("outline"), shell2.get());
        shell2->show();
        QCoreApplication::processEvents();
        auto *panel2 = shell2->findChild<vedit::workspace::diagnostics::DiagnosticsPanel *>();
        auto *pane2 = shell2->editorArea()->activePane();
        if (panel2 != nullptr && pane2 != nullptr) {
            pane2->openFile(file);
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 4000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(10);
            }
            std::printf("OBSERVED seam.reverse-control status='%s'\n",
                        panel2->statusText().toUtf8().constData());
            check("seam.reverse-control-no-analysis",
                  !panel2->statusText().contains(QStringLiteral("analyzed")),
                  panel2->statusText());
        }
        qunsetenv("VEDIT_NO_ACTIVE_FILE_WATCHER");
    }

    std::printf("VE-DIAGNOSTICS-PRODUCT-SEAM checks=%d failures=%d skipped=%d\n", g_checks,
                g_failures, g_skips);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
