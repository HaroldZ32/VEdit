// Active-file watcher size-cap self-test (BLOCKER fix, batch 20261110d).
//
// The real-machine defect: the 400 ms ActiveFileWatcher copied the WHOLE active
// document on every tick (`document()->text()`), kept another full copy in
// `lastText_`, and on the first tick read the file again (`readAll()` +
// `QString::fromUtf8`). With no size cap, a several-hundred-MB file threw
// `std::bad_alloc` -> `terminate` inside the Qt event loop while the user was
// idle.
//
// This test drives the REAL shell + KTextEditor document + diagnostics lane and
// asserts, from the watcher's own published counters (`vedit.diagnostics.activeWatch`):
//   * a small file is still copied (once) and handed to the analysis - the
//     steady state the fix must not break;
//   * an oversized document (generated at runtime, never committed) is copied
//     ZERO times, never analysed, and produces exactly ONE named
//     "analysis disabled: file too large" line plus the availability state;
//   * several 400 ms ticks later the counters are unchanged (idle cost is O(1));
//   * `VEDIT_NO_ACTIVE_FILE_WATCHER=1` still leaves the watcher unpublished and
//     idle (the reverse control the project already relies on).
//
// The measured copy the OLD tick made is printed as OBSERVED: it is taken by the
// test itself (`document()->text()`), never by the watcher.
#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "diagnostics_panel.h"

#include "runtime_init.h"

#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "panel_registry.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QStatusBar>
#include <QVariantMap>
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

/// Drive the event loop for `ms` so the 400 ms watcher tick fires several times.
void settle(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
}

QVariantMap watchStats(vedit::app::ApplicationShell *shell)
{
    return shell->property("vedit.diagnostics.activeWatch").toMap();
}

QVariantMap availability(vedit::app::ApplicationShell *shell)
{
    return shell->property("vedit.diagnostics.analysisAvailability").toMap();
}

int linesMatching(vedit::app::ApplicationShell *shell, const QString &needle)
{
    int matches = 0;
    for (const QString &line : shell->diagnostics()) {
        if (line.contains(needle)) {
            ++matches;
        }
    }
    return matches;
}

/// Write `bytes` of ASCII filler generated at runtime (the test never commits a
/// large fixture; the "source" need not be valid HDL because the cap is checked
/// before any analysis).
bool writeGeneratedFile(const QString &path, qint64 bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray chunk = QByteArrayLiteral(
        "// generated filler for the active-file watcher cap self-test\n");
    qint64 written = 0;
    while (written < bytes) {
        const qint64 remaining = bytes - written;
        const QByteArray slice = remaining >= chunk.size()
                                     ? chunk
                                     : chunk.left(static_cast<qsizetype>(remaining));
        const qint64 n = file.write(slice);
        if (n <= 0) {
            return false;
        }
        written += n;
    }
    return file.flush() && file.size() == bytes;
}

} // namespace

int main(int argc, char **argv)
{
    // T134 / AGENT.md §4: the shell links the Rust service layer and the lane may
    // start a real LSP child, so install the host's process defaults first.
    vedit_runtime_init();
    QApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("VE-ACTIVE-WATCHER-CAP checks=0 failures=1 skipped=0 (no temporary directory)\n");
        return 1;
    }

    const qint64 cap = vedit::workspace::kActiveFileWatchByteCap;
    // ---- small file: the analysis path still works -------------------------
    const QString smallFile = dir.path() + QStringLiteral("/small.sv");
    const QByteArray smallText = QByteArrayLiteral(
        "module small;\n  logic a, b;\n  always_comb begin\n    a = b;\n  end\nendmodule\n");
    {
        QFile file(smallFile);
        check("cap.small.written", file.open(QIODevice::WriteOnly)
                                      && file.write(smallText) == smallText.size());
    }

    vedit::app::ShellOptions options;
    options.stateDir = dir.path() + QStringLiteral("/state");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    check("cap.edit-core-provider", provider != nullptr);
    std::unique_ptr<vedit::app::ApplicationShell> shell(
        new vedit::app::ApplicationShell(options));
    if (provider != nullptr) {
        shell->editorArea()->setProvider(provider.get());
    }
    shell->editorArea()->ensurePane();
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = QString(); // the double-click case: no project
    integration.stateDir = dir.path() + QStringLiteral("/state");
    vedit::workspace::installDiagnostics(*shell, integration);
    shell->panels()->showPanel(QStringLiteral("outline"), shell.get());
    shell->resize(1280, 860);
    shell->show();
    QCoreApplication::processEvents();

    auto *panel = shell->findChild<vedit::workspace::diagnostics::DiagnosticsPanel *>();
    check("cap.panel-installed", panel != nullptr);

    // The watcher publishes the cap BEFORE any file is opened, so "the watcher
    // is active and applies this cap" is observable even in a bug report.
    const QVariantMap initialStats = watchStats(shell.get());
    check("cap.watcher-active", initialStats.value(QStringLiteral("capBytes")).toLongLong() == cap,
          QStringLiteral("capBytes=%1").arg(initialStats.value(QStringLiteral("capBytes")).toLongLong()));

    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    check("cap.active-pane", pane != nullptr);
    if (panel == nullptr || pane == nullptr) {
        std::printf("VE-ACTIVE-WATCHER-CAP checks=%d failures=%d skipped=%d\n", g_checks,
                    g_failures, g_skips);
        return g_failures == 0 ? 0 : 1;
    }

    QString smallError;
    const bool smallOpened = pane->openFile(smallFile, &smallError);
    check("cap.small.opened", smallOpened, smallError);
    const bool smallAnalysed = waitUntil(
        [&shell]() {
            return watchStats(shell.get()).value(QStringLiteral("analyzeCalls")).toULongLong() >= 1;
        },
        15000);
    const QVariantMap smallStats = watchStats(shell.get());
    check("cap.small.analysis-requested", smallAnalysed,
          QStringLiteral("analyzeCalls=%1")
              .arg(smallStats.value(QStringLiteral("analyzeCalls")).toULongLong()));
    check("cap.small.buffer-copied",
          smallStats.value(QStringLiteral("bufferReads")).toULongLong() >= 1,
          QStringLiteral("bufferReads=%1")
              .arg(smallStats.value(QStringLiteral("bufferReads")).toULongLong()));
    // The copy is bounded by the small file, i.e. far below the cap (the large
    // file below then proves zero copies rather than "a small one").
    check("cap.small.copy-bounded",
          smallStats.value(QStringLiteral("bufferBytes")).toULongLong() > 0
              && smallStats.value(QStringLiteral("bufferBytes")).toULongLong()
                     <= static_cast<quint64>(cap),
          QStringLiteral("bufferBytes=%1 capBytes=%2")
              .arg(smallStats.value(QStringLiteral("bufferBytes")).toULongLong())
              .arg(cap));
    const QVariantMap smallAvailability = availability(shell.get());
    check("cap.small.availability",
          smallAvailability.value(QStringLiteral("path")).toString() == smallFile
              && smallAvailability.value(QStringLiteral("available")).toBool(),
          QStringLiteral("available=%1")
              .arg(smallAvailability.value(QStringLiteral("available")).toBool() ? 1 : 0));
    check("cap.small.controller-analysis",
          panel->controller()->analyzeFileName() == smallFile
              && panel->controller()->analysisCount() >= 1,
          QStringLiteral("file='%1' analyses=%2")
              .arg(panel->controller()->analyzeFileName())
              .arg(panel->controller()->analysisCount()));
    check("cap.small.no-disabled-line", linesMatching(shell.get(), QStringLiteral("file too large")) == 0);

    // Let any startup/wiring re-analysis of the small file settle BEFORE the
    // baseline, so the large-file leg cannot be blamed for old work.
    settle(1200);
    const QVariantMap baseline = watchStats(shell.get());
    const quint64 smallReads = baseline.value(QStringLiteral("bufferReads")).toULongLong();
    const quint64 smallBytes = baseline.value(QStringLiteral("bufferBytes")).toULongLong();
    const quint64 smallAnalyses = baseline.value(QStringLiteral("analyzeCalls")).toULongLong();
    const quint64 smallControllerAnalyses = panel->controller()->analysisCount();

    // ---- oversized document: zero copies, one named line -------------------
    const qint64 bigTarget = cap + 1024 * 1024; // one MiB past the cap
    const QString bigFile = dir.path() + QStringLiteral("/big.sv");
    const bool bigWritten = writeGeneratedFile(bigFile, bigTarget);
    check("cap.big.generated", bigWritten && QFileInfo(bigFile).size() == bigTarget,
          QStringLiteral("bytes=%1 target=%2").arg(QFileInfo(bigFile).size()).arg(bigTarget));

    QString bigError;
    const bool bigOpened = pane->openFile(bigFile, &bigError);
    check("cap.big.opened", bigOpened, bigError);
    const bool noticed = waitUntil(
        [&shell]() {
            return watchStats(shell.get()).value(QStringLiteral("tooLargeNotices")).toULongLong() >= 1;
        },
        15000);
    check("cap.big.notice-emitted", noticed);
    const QVariantMap rightAfterNotice = watchStats(shell.get());

    // Several 400 ms ticks: the old watcher copied the document on each one.
    settle(2500);

    const QVariantMap bigStats = watchStats(shell.get());
    // The large file was active across the whole settle window: the idle path
    // did no work proportional to it (this is the steady-state claim).
    check("cap.big.steady-state-no-work",
          bigStats.value(QStringLiteral("bufferReads")).toULongLong()
                  == rightAfterNotice.value(QStringLiteral("bufferReads")).toULongLong()
              && bigStats.value(QStringLiteral("bufferBytes")).toULongLong()
                     == rightAfterNotice.value(QStringLiteral("bufferBytes")).toULongLong()
              && bigStats.value(QStringLiteral("analyzeCalls")).toULongLong()
                     == rightAfterNotice.value(QStringLiteral("analyzeCalls")).toULongLong(),
          QStringLiteral("bufferReads %1->%2 analyzeCalls %3->%4")
              .arg(rightAfterNotice.value(QStringLiteral("bufferReads")).toULongLong())
              .arg(bigStats.value(QStringLiteral("bufferReads")).toULongLong())
              .arg(rightAfterNotice.value(QStringLiteral("analyzeCalls")).toULongLong())
              .arg(bigStats.value(QStringLiteral("analyzeCalls")).toULongLong()));
    const QVariantMap bigAvailability = availability(shell.get());
    check("cap.big.no-buffer-copy",
          bigStats.value(QStringLiteral("bufferReads")).toULongLong() == smallReads,
          QStringLiteral("bufferReads=%1 (was %2 before the large file)")
              .arg(bigStats.value(QStringLiteral("bufferReads")).toULongLong())
              .arg(smallReads));
    check("cap.big.no-bytes-copied",
          bigStats.value(QStringLiteral("bufferBytes")).toULongLong() == smallBytes,
          QStringLiteral("bufferBytes=%1")
              .arg(bigStats.value(QStringLiteral("bufferBytes")).toULongLong()));
    check("cap.big.no-analysis-request",
          bigStats.value(QStringLiteral("analyzeCalls")).toULongLong() == smallAnalyses
              && panel->controller()->analysisCount() == smallControllerAnalyses,
          QStringLiteral("watcherCalls=%1 controllerAnalyses=%2")
              .arg(bigStats.value(QStringLiteral("analyzeCalls")).toULongLong())
              .arg(panel->controller()->analysisCount()));
    check("cap.big.one-notice",
          bigStats.value(QStringLiteral("tooLargeNotices")).toULongLong() == 1,
          QStringLiteral("tooLargeNotices=%1 tooLargeSkips=%2")
              .arg(bigStats.value(QStringLiteral("tooLargeNotices")).toULongLong())
              .arg(bigStats.value(QStringLiteral("tooLargeSkips")).toULongLong()));
    check("cap.big.availability",
          bigAvailability.value(QStringLiteral("path")).toString() == bigFile
              && !bigAvailability.value(QStringLiteral("available")).toBool()
              && bigAvailability.value(QStringLiteral("reason")).toString().contains(
                  QStringLiteral("file too large")),
          bigAvailability.value(QStringLiteral("reason")).toString());
    check("cap.big.exactly-one-shell-line",
          linesMatching(shell.get(), QStringLiteral("file too large")) == 1,
          QStringLiteral("lines=%1").arg(linesMatching(shell.get(), QStringLiteral("file too large"))));
    std::printf("OBSERVED cap.big status-bar='%s'\n",
                shell->statusBar() != nullptr
                    ? shell->statusBar()->currentMessage().toUtf8().constData()
                    : "");

    // The copy the OLD watcher made on every tick, measured by the test (the
    // watcher itself made none): one `document()->text()` for a document of N
    // characters is 2N bytes; the old first tick additionally held a second
    // buffer copy and the disk copy (readAll + fromUtf8).
    if (auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget())) {
        if (view->document() != nullptr) {
            const QString measured = view->document()->text();
            const qint64 chars = measured.size();
            std::printf("OBSERVED cap.big document-chars=%lld one-copy-bytes=%lld "
                        "old-first-tick-peak-bytes~=%lld new-watcher-copy-bytes=0\n",
                        static_cast<long long>(chars),
                        static_cast<long long>(chars * 2),
                        static_cast<long long>(chars * 6));
            check("cap.big.document-exceeds-cap", chars > cap,
                  QStringLiteral("documentChars=%1 cap=%2").arg(chars).arg(cap));
        }
    }

    // ---- reverse control: the documented switch still disables the watcher --
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
        if (auto *pane2 = shell2->editorArea()->activePane()) {
            pane2->openFile(smallFile);
        }
        settle(1500);
        check("cap.reverse-control.watcher-disabled",
              !shell2->property("vedit.diagnostics.activeWatch").isValid());
        check("cap.reverse-control.no-disabled-line",
              linesMatching(shell2.get(), QStringLiteral("file too large")) == 0);
        qunsetenv("VEDIT_NO_ACTIVE_FILE_WATCHER");
    }

    std::printf("VE-ACTIVE-WATCHER-CAP checks=%d failures=%d skipped=%d\n", g_checks, g_failures,
                g_skips);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
