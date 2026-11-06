// T8 evidence probe: the project-wide lint orchestration, headless.
//
// It is also the lane's ctest entry (`vedit-lint-orchestration-probe`): a fake
// `ProjectLintBackend` replaces the Rust batch service, so cancellation,
// failure reporting, the C8 open-file snapshot, the LSP/CLI merge and the
// 1k-file interactivity are proven without a toolchain.
//
// UX batch 2 / U8b: the probe lives in the lane beside the code it exercises (it
// used to be compiled out of `evidence/w4-lint/`, which made a LIVING test look
// like a closed evidence artefact). Its output is still recorded under
// evidence/; only the source location changed.
//
//   QT_QPA_PLATFORM=offscreen ./vedit-lint-orchestration-probe
//
// Every line is `CHECK <name> PASS|FAIL`; the last line is the machine summary.
#include "diagnostics_controller.h"
#include "diagnostics_types.h"
#include "lint_progress.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QString>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <memory>

using namespace vedit::workspace::diagnostics;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

void processEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

QStringList numberedFiles(int count)
{
    QStringList files;
    files.reserve(count);
    for (int index = 0; index < count; ++index) {
        files.append(QStringLiteral("/tmp/lint-probe/file%1.sv").arg(index));
    }
    return files;
}

DiagnosticEntry entry(const QString &file, int line, int severity, const QString &message)
{
    DiagnosticEntry row;
    row.file = file;
    row.line = line;
    row.severity = severity;
    row.message = message;
    return row;
}

/// A controllable stand-in for `vd_deeplint_batch_*`.
class FakeLintBackend final : public ProjectLintBackend {
public:
    explicit FakeLintBackend(QString tool, bool available = true)
        : tool_(std::move(tool))
        , available_(available)
    {
    }

    bool available() const override { return available_; }
    QString toolName() const override { return tool_; }

    quint64 start(const LintPassRequest &request, QString *error) override
    {
        if (!available_) {
            if (error != nullptr) {
                *error = QStringLiteral("%1: the lint tool could not be resolved").arg(tool_);
            }
            return 0;
        }
        files_ = request.files;
        includeDirs_ = request.includeDirs;
        defines_ = request.defines;
        phase_ = spawnFailure_ ? LintPhase::Failed : LintPhase::Running;
        done_ = 0;
        job_ = ++nextJob_;
        if (error != nullptr) {
            error->clear();
        }
        return job_;
    }

    LintPassSnapshot progress(quint64 job) const override
    {
        LintPassSnapshot snapshot;
        snapshot.tool = tool_;
        snapshot.total = static_cast<int>(files_.size());
        snapshot.done = done_;
        snapshot.phase = phase_;
        if (done_ < files_.size()) {
            snapshot.current = files_.at(done_);
        }
        if (phase_ == LintPhase::Running && !files_.isEmpty()) {
            // One poll advances a few files; enough for a 1k-file loop.
            done_ = std::min(done_ + step_, static_cast<int>(files_.size()));
            snapshot.done = done_;
            if (done_ >= static_cast<int>(files_.size())) {
                phase_ = LintPhase::Finished;
                snapshot.phase = LintPhase::Finished;
            }
        }
        if (phase_ == LintPhase::Failed) {
            snapshot.stage = QStringLiteral("spawn");
            snapshot.message =
                QStringLiteral("tool=%1 stage=spawn: the lint child could not be started").arg(tool_);
        }
        (void)job;
        return snapshot;
    }

    LspPolled poll(quint64 job) override
    {
        (void)job;
        if (phase_ == LintPhase::Running || phase_ == LintPhase::Queued) {
            return LspPolled::pendingResult();
        }
        if (phase_ == LintPhase::Failed) {
            return LspPolled::failure(
                QStringLiteral("tool=%1 stage=spawn: the lint child could not be started").arg(tool_));
        }
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        if (phase_ == LintPhase::Finished) {
            // One CLI row per file for the first few files (enough to merge).
            const int limit = std::min(static_cast<int>(files_.size()), 3);
            for (int index = 0; index < limit; ++index) {
                DiagnosticEntry row;
                row.file = files_.at(index);
                row.line = index;
                row.severity = 0; // C ABI scale: error
                row.message = QStringLiteral("undeclared identifier `x`");
                row.source = QStringLiteral("cli:%1").arg(tool_);
                polled.diagnostics.append(row);
            }
        }
        return polled;
    }

    void cancel(quint64 job) override
    {
        (void)job;
        if (phase_ == LintPhase::Running) {
            phase_ = LintPhase::Cancelled;
        }
    }

    void release(quint64 job) override
    {
        (void)job;
        released_ += 1;
    }

    void setStep(int step) { step_ = step; }
    // A-15: the user installs the linter between the failed first run and Re-run.
    void setAvailable(bool available) { available_ = available; }
    void setSpawnFailure(bool fail) { spawnFailure_ = fail; }
    const QStringList &receivedFiles() const { return files_; }
    const QStringList &receivedIncludeDirs() const { return includeDirs_; }
    const QStringList &receivedDefines() const { return defines_; }
    int releasedCount() const { return released_; }

private:
    QString tool_;
    bool available_ = true;
    bool spawnFailure_ = false;
    int step_ = 5;
    quint64 nextJob_ = 0;
    quint64 job_ = 0;
    mutable int done_ = 0;
    mutable LintPhase phase_ = LintPhase::Idle;
    mutable QStringList files_;
    QStringList includeDirs_;
    QStringList defines_;
    mutable int released_ = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // 1. Display-level merge: one row per problem, origin Lsp/Cli/Both.
    {
        const QString file = QStringLiteral("/tmp/lint-probe/a.sv");
        QVector<DiagnosticEntry> lsp{entry(file, 4, 1, QStringLiteral("undeclared  identifier `x`"))};
        QVector<DiagnosticEntry> cli{
            entry(file, 4, 0, QStringLiteral("  undeclared identifier `x` ")), // same -> Both
            entry(file, 9, 1, QStringLiteral("unused signal")),               // CLI only
        };
        const QVector<DisplayDiagnostic> merged = mergeDiagnostics(lsp, cli);
        int both = 0;
        int lspOnly = 0;
        int cliOnly = 0;
        for (const DisplayDiagnostic &row : merged) {
            both += row.origin == DiagnosticOrigin::Both ? 1 : 0;
            lspOnly += row.origin == DiagnosticOrigin::Lsp ? 1 : 0;
            cliOnly += row.origin == DiagnosticOrigin::Cli ? 1 : 0;
        }
        std::printf("MERGE rows=%d both=%d lsp=%d cli=%d\n", static_cast<int>(merged.size()), both,
                    lspOnly, cliOnly);
        check("merge.one-row-for-lsp-and-cli", merged.size() == 2 && both == 1);
        check("merge.cli-only-marked", cliOnly == 1);
        check("merge.cli-severity-converted",
              merged.at(1).entry.severity == 2 // VD warning(1) -> LSP warning(2)
                  || merged.at(0).entry.severity == 2);
    }

    // 2. C8: the pass snapshots the open files and skips them.
    {
        DiagnosticsController controller(nullptr, nullptr);
        auto fake = std::make_unique<FakeLintBackend>(QStringLiteral("fake-slang"));
        FakeLintBackend *backend = fake.get();
        controller.setProjectLintBackend(std::move(fake));
        controller.setOpenFiles({QStringLiteral("/tmp/lint-probe/file1.sv"),
                                 QStringLiteral("/tmp/lint-probe/file2.sv")});
        LintPassRequest request;
        request.files = numberedFiles(5);
        request.includeDirs = {QStringLiteral("/tmp/inc")};
        request.defines = {QStringLiteral("WIDTH=8")};
        check("c8.start", controller.startProjectPass(request));
        check("c8.skipped-open-files", controller.projectPassSkipped().size() == 2);
        check("c8.pass-file-count", controller.projectPassFileCount() == 3);
        check("c8.backend-received-pass-set", backend->receivedFiles().size() == 3);
        check("c8.include-dirs-forwarded", backend->receivedIncludeDirs().size() == 1);
        check("c8.defines-forwarded", backend->receivedDefines().size() == 1);
        // An open during the pass does not change the running set.
        controller.setOpenFiles({QStringLiteral("/tmp/lint-probe/file3.sv")});
        check("c8.new-open-does-not-shrink-pass", backend->receivedFiles().size() == 3);
        for (int tick = 0; tick < 200 && controller.projectPassRunning(); ++tick) {
            controller.refresh();
            processEvents();
        }
        check("c8.finishes", !controller.projectPassRunning());
        check("c8.cli-diagnostics", controller.cliDiagnostics().size() == 3);
        check("c8.merged-display", controller.displayDiagnostics().size() == 3);
    }

    // 3. Cancel: true stop, no later diagnostics, clean rerun.
    {
        DiagnosticsController controller(nullptr, nullptr);
        auto fake = std::make_unique<FakeLintBackend>(QStringLiteral("fake-slang"));
        FakeLintBackend *backend = fake.get();
        backend->setStep(5);
        controller.setProjectLintBackend(std::move(fake));
        LintPassRequest request;
        request.files = numberedFiles(100);
        check("cancel.start", controller.startProjectPass(request));
        for (int tick = 0; tick < 3; ++tick) {
            controller.refresh();
            processEvents();
        }
        const LintPassSnapshot before = controller.projectPass();
        std::printf("CANCEL before phase=%d done=%d/%d running=%d\n", static_cast<int>(before.phase),
                    before.done, before.total, controller.projectPassRunning() ? 1 : 0);
        check("cancel.running-before", controller.projectPassRunning());
        controller.cancelProjectPass();
        const LintPassSnapshot after = controller.projectPass();
        std::printf("CANCEL after phase=%d running=%d message=«%s» diagnostics=%d\n",
                    static_cast<int>(after.phase), controller.projectPassRunning() ? 1 : 0,
                    qPrintable(after.message),
                    static_cast<int>(controller.cliDiagnostics().size()));
        check("cancel.phase", after.phase == LintPhase::Cancelled);
        check("cancel.flag-cleared", !controller.projectPassRunning());
        // U2c made the two lint status strings translatable with ENGLISH source
        // text; with no QTranslator installed the message IS the source. The fact
        // is unchanged: a CANCEL message that names the done/total counts (the old
        // text was «已取消 — 20/100 文件»).
        check("cancel.message-counts",
              after.message.contains(QStringLiteral("cancelled"))
                  && after.message.contains(QStringLiteral("20/100")));
        check("cancel.no-diagnostics-after",
              controller.cliDiagnostics().isEmpty()); // nothing arrived after the cancel
        for (int tick = 0; tick < 20; ++tick) {
            controller.refresh();
            processEvents();
        }
        check("cancel.still-nothing-after-ticks", controller.cliDiagnostics().isEmpty());
        check("cancel.released", backend->releasedCount() == 1);
        check("cancel.rerun-starts", controller.rerunProjectPass());
        for (int tick = 0; tick < 400 && controller.projectPassRunning(); ++tick) {
            controller.refresh();
            processEvents();
        }
        check("cancel.rerun-finishes", controller.projectPass().phase == LintPhase::Finished);
        check("cancel.rerun-delivers", controller.cliDiagnostics().size() == 3);
    }

    // 4. Failure names the tool and the stage, and a rerun stays possible.
    {
        DiagnosticsController controller(nullptr, nullptr);
        controller.setProjectLintBackend(
            std::make_unique<FakeLintBackend>(QStringLiteral("fake-verible"), false));
        LintPassRequest request;
        request.files = numberedFiles(4);
        QString error;
        check("fail.unavailable-refused", !controller.startProjectPass(request, &error));
        std::printf("FAIL unavailable tool=%s stage=%s message=«%s»\n",
                    qPrintable(controller.projectPass().tool),
                    qPrintable(controller.projectPass().stage),
                    qPrintable(controller.projectPass().message));
        check("fail.tool-named", controller.projectPass().tool == QStringLiteral("fake-verible"));
        check("fail.stage-named", controller.projectPass().stage == QStringLiteral("resolve"));

        DiagnosticsController spawn(nullptr, nullptr);
        auto fake = std::make_unique<FakeLintBackend>(QStringLiteral("fake-slang"));
        fake->setSpawnFailure(true);
        spawn.setProjectLintBackend(std::move(fake));
        check("fail.spawn-started", spawn.startProjectPass(request));
        for (int tick = 0; tick < 50; ++tick) {
            spawn.refresh();
            processEvents();
        }
        std::printf("FAIL spawn phase=%d tool=%s stage=%s message=«%s»\n",
                    static_cast<int>(spawn.projectPass().phase),
                    qPrintable(spawn.projectPass().tool), qPrintable(spawn.projectPass().stage),
                    qPrintable(spawn.projectPass().message));
        check("fail.spawn-phase", spawn.projectPass().phase == LintPhase::Failed);
        check("fail.spawn-tool-and-stage",
              spawn.projectPass().message.contains(QStringLiteral("fake-slang"))
                  && spawn.projectPass().message.contains(QStringLiteral("stage=spawn")));
    }

    // 4b. A-15: a first-run tool-missing failure is RE-RUNNABLE. The request is
    //     recorded before the tool is resolved, so "install the linter, click
    //     Re-run" starts a real pass instead of refusing with "no project lint
    //     pass has run yet" (the silent dead button the T8 review found).
    {
        DiagnosticsController controller(nullptr, nullptr);
        auto fake = std::make_unique<FakeLintBackend>(QStringLiteral("fake-verible"), false);
        FakeLintBackend *backend = fake.get();
        controller.setProjectLintBackend(std::move(fake));
        LintPassRequest request;
        request.files = numberedFiles(4);
        QString error;
        check("rerun.fail-first", !controller.startProjectPass(request, &error));
        std::printf("A15 first run: stage=%s message=«%s» rerunAvailable=%d request_files=%d\n",
                    qPrintable(controller.projectPass().stage), qPrintable(error),
                    controller.rerunAvailable() ? 1 : 0,
                    static_cast<int>(controller.lastProjectPassRequest().files.size()));
        check("rerun.fail-first-stage", controller.projectPass().stage == QStringLiteral("resolve"));
        check("rerun.request-recorded", controller.rerunAvailable());
        check("rerun.request-kept",
              controller.lastProjectPassRequest().files.size() == request.files.size());

        // The user installs the linter and clicks Re-run. A small step keeps the
        // second pass observable for a tick (the default step would finish it
        // inside the first poll and hide the "it started" fact).
        backend->setAvailable(true);
        backend->setStep(1);
        QString rerunError;
        check("rerun.after-failure-starts", controller.rerunProjectPass(&rerunError));
        std::printf("A15 rerun attempt: error=«%s»\n", qPrintable(rerunError));
        for (int tick = 0; tick < 400 && controller.projectPassRunning(); ++tick) {
            controller.refresh();
            processEvents();
        }
        std::printf("A15 rerun after failure: phase=%d diagnostics=%d\n",
                    static_cast<int>(controller.projectPass().phase),
                    static_cast<int>(controller.cliDiagnostics().size()));
        check("rerun.after-failure-finishes",
              controller.projectPass().phase == LintPhase::Finished);
        check("rerun.after-failure-delivers", controller.cliDiagnostics().size() == 3);

        // The strip offers the button for this state, and clicking it runs.
        LintProgressBar strip;
        strip.setPollInterval(5);
        strip.setController(&controller);
        strip.refreshFromController();
        check("rerun.strip-enabled-after-failure", strip.rerunEnabled());
        check("rerun.strip-hint-empty", strip.rerunHint().isEmpty());
        strip.requestRerun();
        check("rerun.strip-starts", strip.running());
        for (int tick = 0; tick < 400 && strip.running(); ++tick) {
            strip.refreshFromController();
            processEvents();
        }
        check("rerun.strip-finishes", !strip.running());
    }

    // 4c. A-15: with NO recorded request the button is disabled and explains
    //     itself — never an enabled control that does nothing.
    {
        DiagnosticsController controller(nullptr, nullptr);
        controller.setProjectLintBackend(
            std::make_unique<FakeLintBackend>(QStringLiteral("fake-slang")));
        LintProgressBar strip;
        strip.setPollInterval(5);
        strip.setController(&controller);
        LintPassRequest empty; // no project files: nothing to re-run
        QString error;
        check("rerun.empty-request-refused", !controller.startProjectPass(empty, &error));
        strip.refreshFromController();
        std::printf("A15 empty request: message=«%s» rerunEnabled=%d hint=«%s»\n",
                    qPrintable(strip.statusText()), strip.rerunEnabled() ? 1 : 0,
                    qPrintable(strip.rerunHint()));
        check("rerun.empty-request-not-available", !controller.rerunAvailable());
        check("rerun.empty-request-button-disabled", !strip.rerunEnabled());
        check("rerun.empty-request-explained", !strip.rerunHint().isEmpty());
    }

    // 5. 1k files: the GUI tick stays responsive; over-scale warns and cancels.
    {
        DiagnosticsController controller(nullptr, nullptr);
        auto fake = std::make_unique<FakeLintBackend>(QStringLiteral("fake-slang"));
        fake->setStep(37);
        controller.setProjectLintBackend(std::move(fake));
        LintPassRequest request;
        request.files = numberedFiles(1000);
        QElapsedTimer passTimer;
        passTimer.start();
        check("scale.start-1k", controller.startProjectPass(request));
        qint64 worstTick = 0;
        int ticks = 0;
        while (controller.projectPassRunning() && ticks < 5000) {
            QElapsedTimer tick;
            tick.start();
            controller.refresh();
            processEvents();
            worstTick = std::max(worstTick, tick.elapsed());
            ++ticks;
        }
        std::printf("SCALE files=1000 ticks=%d worst_tick_ms=%lld total_ms=%lld done=%d\n", ticks,
                    static_cast<long long>(worstTick), static_cast<long long>(passTimer.elapsed()),
                    controller.projectPass().done);
        check("scale.finishes-1k", !controller.projectPassRunning());
        check("scale.ui-tick-under-50ms", worstTick < 50);
        check("scale.progress-reached-total", controller.projectPass().done == 1000);

        DiagnosticsController large(nullptr, nullptr);
        large.setProjectLintBackend(std::make_unique<FakeLintBackend>(QStringLiteral("fake-slang")));
        LintPassRequest big;
        big.files = numberedFiles(1500);
        check("scale.large-starts", large.startProjectPass(big));
        large.refresh();
        std::printf("SCALE large-message=«%s»\n", qPrintable(large.projectPass().message));
        check("scale.large-warns", large.projectPass().message.contains(QStringLiteral("large pass")));
        large.cancelProjectPass();
        check("scale.large-cancels", large.projectPass().phase == LintPhase::Cancelled);
    }

    // 6. The bottom strip renders the pass and its buttons do the right things.
    {
        DiagnosticsController controller(nullptr, nullptr);
        auto fake = std::make_unique<FakeLintBackend>(QStringLiteral("fake-slang"));
        fake->setStep(25);
        controller.setProjectLintBackend(std::move(fake));
        LintProgressBar strip;
        strip.setPollInterval(5);
        strip.setController(&controller);
        check("widget.hidden-before-pass", !strip.stripVisible());
        LintPassRequest request;
        request.files = numberedFiles(100);
        check("widget.start", controller.startProjectPass(request));
        strip.refreshFromController();
        check("widget.running", strip.running());
        check("widget.cancel-visible", strip.cancelVisible());
        check("widget.progress-song", strip.progressText().contains(QLatin1Char('/')));
        strip.requestCancel();
        // Same fact as before (the strip renders a CANCEL message, not "idle" and
        // not a finished line), now checked against the English source string; the
        // "/100" keeps the total-count part that the old phrase carried.
        check("widget.cancel-message",
              strip.statusText().contains(QStringLiteral("cancelled"))
                  && strip.statusText().contains(QStringLiteral("/100")));
        check("widget.rerun-enabled", strip.rerunEnabled());
        strip.requestRerun();
        check("widget.rerun-running", strip.running());
        for (int tick = 0; tick < 400 && strip.running(); ++tick) {
            strip.refreshFromController();
            processEvents();
        }
        check("widget.finished", strip.percent() == 100);
        // The FINISHED line, from the English source: it must still say the files
        // were scanned and report the CLI diagnostics count (old text:
        // «%1/%2 文件已扫描 — %3 条 CLI 诊断»).
        check("widget.finished-text",
              strip.statusText().contains(QStringLiteral("file(s) scanned"))
                  && strip.statusText().contains(QStringLiteral("CLI diagnostics")));
    }

    std::printf("LINT-ORCHESTRATION-PROBE ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
