// AC-5.4 (batch-20261115, lane C): the 10k-line OPEN benchmark.
//
// The acceptance is a comparison: this exact binary and command are run BEFORE
// the breadcrumb deferral (baseline, recorded in
// evidence/batch-20261115/editor/README.md) and again after it; the post-change
// p95 must stay <= baseline * 1.1. The in-binary check is only the absolute
// floor the native-fold lane already uses (p95 <= 2000 ms), so a machine without
// the pinned toolchain fails honestly instead of silently passing.
//
//   vedit-editor-open-bench
//
// The measured path is the PRODUCT open path: EditorPane::openFile() ->
// EditCoreProvider::openFile() -> document load + highlighting + the
// breadcrumb seed. The seed copies the whole document (measured 192 ms at 10k
// lines, evidence/large-file/REPORT.md), which is what the deferral targets.
#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"

#if defined(VEDIT_OPEN_BENCH_HAVE_EDIT_CORE)
#include "ktexteditor_provider.h"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const QString &name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", qPrintable(name), ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", qPrintable(detail));
    std::fflush(stdout);
}

void pump()
{
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
    }
}

// 40 lines per module, the same synthetic shape the native-fold lane uses, so
// the 10k sample is comparable with evidence/large-file/REPORT.md.
QString sample(int modules)
{
    QString text;
    for (int index = 0; index < modules; ++index) {
        const QString n = QString::number(index);
        text += QStringLiteral("module m%1 #(parameter W = 8) (\n").arg(n);
        text += QStringLiteral("  input  logic clk,\n");
        text += QStringLiteral("  input  logic rst_n,\n");
        text += QStringLiteral("  output logic [W-1:0] q\n");
        text += QStringLiteral(");\n");
        text += QStringLiteral("  logic [W-1:0] state, nxt;\n");
        text += QStringLiteral("  always_ff @(posedge clk) begin\n");
        text += QStringLiteral("    state <= nxt;\n");
        text += QStringLiteral("  end\n");
        for (int line = 8; line < 39; ++line) {
            text += QStringLiteral("  // filler %1 %2\n").arg(line).arg(n);
        }
        text += QStringLiteral("endmodule\n");
    }
    return text;
}

bool writeFile(const QString &path, const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

qint64 percentile(std::vector<qint64> samples, double fraction)
{
    if (samples.empty()) {
        return -1;
    }
    std::sort(samples.begin(), samples.end());
    // Nearest-rank: p95 of 7 samples is the largest one, the conservative bound.
    const auto rank = static_cast<std::size_t>(std::ceil(fraction * samples.size()));
    const std::size_t index = rank == 0 ? 0 : std::min(samples.size() - 1, rank - 1);
    return samples[index];
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-editor-open-bench"));

#if !defined(VEDIT_OPEN_BENCH_HAVE_EDIT_CORE)
    // Without the editor lane this benchmark would measure the demo placeholder,
    // which says nothing about the product open path: say SKIP, never PASS.
    std::printf("OPEN-BENCH SKIP no edit-core provider (VEDIT_OPEN_BENCH_HAVE_EDIT_CORE)\n");
    return 77;
#else
    QTemporaryDir temp;
    check(QStringLiteral("open-bench.temp-dir"), temp.isValid());
    if (!temp.isValid()) {
        return 1;
    }

    vedit::app::ShellOptions options;
    options.stateDir = temp.path() + QStringLiteral("/state");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    vedit::app::ApplicationShell shell(options);
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    shell.resize(1280, 820);
    shell.show();
    pump();

    vedit::app::EditorPane *pane = shell.editorArea()->ensurePane();
    check(QStringLiteral("open-bench.pane"), pane != nullptr);
    if (pane == nullptr) {
        return 1;
    }

    // Warm up the provider (KTextEditor editor instance, syntax repository, the
    // pane's first widgets) so the measured samples are the steady state.
    const QString warmPath = temp.path() + QStringLiteral("/warmup.sv");
    check(QStringLiteral("open-bench.warmup-file"), writeFile(warmPath, sample(4)));
    QString error;
    check(QStringLiteral("open-bench.warmup-open"), pane->openFile(warmPath, &error), error);
    pump();

    const QString warmBigPath = temp.path() + QStringLiteral("/warmup-10k.sv");
    check(QStringLiteral("open-bench.warmup-10k-file"), writeFile(warmBigPath, sample(256)));
    QElapsedTimer warmTimer;
    warmTimer.start();
    const bool warmBigOpened = pane->openFile(warmBigPath, &error);
    const qint64 warmBigMs = warmTimer.elapsed();
    std::printf("OPEN-BENCH warmup-10k ms=%lld opened=%d\n", static_cast<long long>(warmBigMs),
                warmBigOpened ? 1 : 0);
    check(QStringLiteral("open-bench.warmup-10k-open"), warmBigOpened, error);
    pump();

    constexpr int kSampleCount = 7;
    std::vector<qint64> samples;
    samples.reserve(kSampleCount);
    bool allOpened = true;
    for (int index = 0; index < kSampleCount; ++index) {
        const QString path = temp.path() + QStringLiteral("/open-%1.sv").arg(index);
        if (!writeFile(path, sample(256))) {
            allOpened = false;
            continue;
        }
        QElapsedTimer timer;
        timer.start();
        const bool opened = pane->openFile(path, &error);
        const qint64 elapsed = timer.elapsed();
        samples.push_back(elapsed);
        allOpened = allOpened && opened;
        // The measured window ends where openFile() returned: the deferred
        // breadcrumb scan (post-fix) is allowed to run here, outside the sample.
        pump();
    }

    check(QStringLiteral("open-bench.all-opened"), allOpened);
    check(QStringLiteral("open-bench.samples"), samples.size() == kSampleCount,
          QString::number(samples.size()));

    const qint64 p50 = percentile(samples, 0.50);
    const qint64 p95 = percentile(samples, 0.95);
    const qint64 minimum = percentile(samples, 0.0);
    const qint64 maximum = percentile(samples, 1.0);
    std::printf("OPEN-BENCH open-10k lines=%d samples=%d p50=%lld p95=%lld min=%lld max=%lld\n",
                10240, static_cast<int>(samples.size()), static_cast<long long>(p50),
                static_cast<long long>(p95), static_cast<long long>(minimum),
                static_cast<long long>(maximum));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        std::printf("OPEN-BENCH sample[%d] ms=%lld\n", static_cast<int>(i),
                    static_cast<long long>(samples[i]));
    }
    // AC-5.4: the deferral must not DROP the seed. One event-loop turn after the
    // open (the pump() above) the debounce timer runs the SAME full scan and the
    // pane's breadcrumb bar shows the module path, exactly as before the change.
    const QString seeded = pane->breadcrumbText();
    check(QStringLiteral("open-bench.deferred-seed"), !seeded.isEmpty(), seeded);

    // Absolute floor only: the batch acceptance compares with the recorded
    // baseline (a ratio cannot live in a test that also runs on a slower host).
    check(QStringLiteral("open-bench.p95-under-2s"), p95 > 0 && p95 <= 2000,
          QString::number(p95));

    std::printf("OPEN-BENCH %s checks=%d failures=%d\n", g_failures == 0 ? "PASS" : "FAIL",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
#endif
}
