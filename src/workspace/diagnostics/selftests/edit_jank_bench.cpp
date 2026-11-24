// Repeatable per-keystroke cost benchmark for the diagnostics view pipeline
// (batch 20261110d, card T9 "大工程编辑卡顿").
//
// The product's typing path is: a document edit -> ActiveFileWatcher's
// textChanged -> DiagnosticsController::markFileEdited() (drops the file's
// upper-layer rows) and then the periodic consumers: the 400 ms live publisher
// (status map + display rows + counts), the 300 ms DiagnosticsPanel refresh and
// the 500 ms file-tree/project-panel status poll. This bench drives the REAL
// controller and the REAL panel with a deterministic fake pass backend, and
// measures that work for one file edited `--keys` times.
//
// It is deliberately HEADLESS (offscreen QApplication): the point is the
// per-keystroke CPU cost of the view pipeline, not the toolchain or the LSP.
// The keystroke cadence is a fixed MODEL (one key every --key-ms simulated
// milliseconds) so the 400/300/500 ms consumers fire a deterministic number of
// times. Timing is real wall-clock time spent in product code; only the cadence
// is virtual, which keeps two runs comparable.
//
// Output is one machine-readable BENCH line per measured scenario plus human
// context lines. Counts are exact (deterministic), so a regression is visible
// even on a noisy machine.
//
//   QT_QPA_PLATFORM=offscreen ./vedit-edit-jank-bench --files 1000 --keys 200
//
// This file is benchmark code, not product code: it lives in selftests/ beside
// the lane (the same convention as lint_orchestration_probe.cpp).

#include "diagnostics_controller.h"
#include "diagnostics_panel.h"
#include "diagnostics_types.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace vedit::workspace::diagnostics;

namespace {

struct Options {
    int files = 1000;
    int keys = 200;
    int lines = 1000;
    int keyMs = 25;          // simulated typing cadence
    bool panelVisible = false; // the dock's visibility (fix 4: invisible = no work)
    bool current = false;      // use the per-file publisher (post-fix API)
    bool assertBounds = false; // deterministic T9 bounds (the ctest run)
    bool warmup = false;       // publish the pass's one-time map before timing
};

/// A fake project pass that delivers one CLI row per file (and three rows for
/// the file the bench types into), so every file carries an upper-layer verdict.
class BenchLintBackend final : public ProjectLintBackend {
public:
    BenchLintBackend(QString tool, QString editedFile, int rowsForEdited)
        : tool_(std::move(tool))
        , editedFile_(std::move(editedFile))
        , rowsForEdited_(rowsForEdited)
    {
    }

    bool available() const override { return true; }
    QString toolName() const override { return tool_; }

    quint64 start(const LintPassRequest &request, QString *error) override
    {
        files_ = request.files;
        phase_ = LintPhase::Running;
        done_ = 0;
        job_ = ++nextJob_;
        if (error != nullptr) {
            error->clear();
        }
        return job_;
    }

    LintPassSnapshot progress(quint64 job) const override
    {
        (void)job;
        LintPassSnapshot snapshot;
        snapshot.tool = tool_;
        snapshot.total = static_cast<int>(files_.size());
        if (phase_ == LintPhase::Running) {
            done_ = snapshot.total;
            phase_ = LintPhase::Finished;
        }
        snapshot.done = done_;
        snapshot.phase = phase_;
        return snapshot;
    }

    LspPolled poll(quint64 job) override
    {
        (void)job;
        if (phase_ == LintPhase::Running || phase_ == LintPhase::Queued) {
            return LspPolled::pendingResult();
        }
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        for (const QString &file : files_) {
            const int rows = file == editedFile_ ? rowsForEdited_ : 1;
            for (int row = 0; row < rows; ++row) {
                DiagnosticEntry entry;
                entry.file = file;
                entry.line = row + 1;
                entry.column = 1;
                // C ABI scale: 0 = error, 1 = warning.
                entry.severity = (row % 2 == 0) ? 0 : 1;
                entry.message = QStringLiteral("bench finding %1").arg(row);
                entry.source = QStringLiteral("cli:%1").arg(tool_);
                polled.diagnostics.append(entry);
            }
        }
        return polled;
    }

    void cancel(quint64 job) override { (void)job; }
    void release(quint64 job) override { (void)job; }

private:
    QString tool_;
    QString editedFile_;
    int rowsForEdited_ = 1;
    QStringList files_;
    quint64 nextJob_ = 0;
    quint64 job_ = 0;
    mutable int done_ = 0;
    mutable LintPhase phase_ = LintPhase::Idle;
};

/// One publisher observation, mirroring what the product publishes.
struct PublishResult {
    bool full = false;
    int entries = 0;
};

/// The PRE-FIX 400 ms publisher block, byte-for-byte the calls
/// `diagnostics_integration.cpp`'s `refreshLiveState()` makes when the single
/// global view revision moved: rebuild the whole status map, the whole display
/// list, the upper list (sorted) and the merged list (sorted).
PublishResult publishLegacy(DiagnosticsController &controller, quint64 &lastRevision)
{
    PublishResult result;
    const quint64 revision = controller.viewRevision();
    if (revision == lastRevision) {
        return result;
    }
    lastRevision = revision;
    result.full = true;
    const QHash<QString, int> map = controller.fileStatuses();
    result.entries = static_cast<int>(map.size());
    const QVector<DisplayDiagnostic> displayed = controller.displayDiagnostics();
    (void)displayed;
    (void)controller.upperLayerDiagnostics();
    (void)controller.mergedDiagnostics();
    return result;
}

/// The POST-FIX publisher: ONE controller call returns what changed (a full map
/// after a structural change, a per-file delta otherwise) plus the cheap counts.
PublishResult publishCurrent(DiagnosticsController &controller, quint64 &lastView,
                             quint64 &lastFiles)
{
    PublishResult result;
    const LivePublish publish = controller.takeLivePublish();
    if (!publish.full && publish.changedFiles.isEmpty()) {
        return result;
    }
    lastView = publish.viewRevision;
    lastFiles = publish.fileRevision;
    result.full = publish.full;
    result.entries = publish.full ? static_cast<int>(publish.statuses.size())
                                  : static_cast<int>(publish.changedFiles.size());
    return result;
}

struct Scenario {
    int files = 0;
    int keys = 0;
    double totalMs = 0;
    double perKeyUs = 0;
    double p95Us = 0;
    double maxUs = 0;
    double editMs = 0;      // the synchronous per-key controller work
    double backgroundMs = 0; // the periodic consumers
    int fullPublishes = 0;
    int deltaPublishes = 0;
    int statusEntriesPublished = 0;
    int panelRebuilds = 0;
    int panelRefreshes = 0;
    int treeRepaints = 0;
    int mergedRebuilds = 0;
    int displayRebuilds = 0;
};

/// Drive one scenario and print its BENCH line.
Scenario runScenario(const Options &options, const QStringList &files, bool current)
{
    const QString edited = files.first();
    DiagnosticsController controller(nullptr, nullptr);
    auto backend = std::make_unique<BenchLintBackend>(
        QStringLiteral("bench-slang"), edited, 3);
    controller.setProjectLintBackend(std::move(backend));
    controller.setFileSet(files);

    // One finished project pass: the upper layer now covers every file.
    LintPassRequest request;
    request.files = files;
    if (!controller.startProjectPass(request)) {
        std::printf("BENCH-ERROR could not start the fake project pass\n");
        std::exit(2);
    }
    for (int tick = 0; tick < 100 && controller.projectPassRunning(); ++tick) {
        controller.refresh();
    }
    if (controller.projectPassRunning() || controller.cliDiagnostics().isEmpty()) {
        std::printf("BENCH-ERROR the fake project pass did not deliver rows\n");
        std::exit(2);
    }

    auto panel = std::make_unique<DiagnosticsPanel>(&controller);
    // The real dock is shown at startup; reproduce that lifecycle so the
    // "hidden" scenario below is the user HIDING the dock (T9 fix 4), not a
    // widget that was never on screen.
    panel->show();
    QCoreApplication::processEvents();
    if (!options.panelVisible) {
        panel->hide();
        QCoreApplication::processEvents();
    }
    const int panelRebuildsBeforeLoop = panel->rebuildCount();

    Scenario result;
    result.files = static_cast<int>(files.size());
    result.keys = options.keys;

    quint64 lastView = static_cast<quint64>(-1);
    quint64 lastFiles = static_cast<quint64>(-1);
    quint64 lastLegacyRevision = static_cast<quint64>(-1);
    if (options.warmup) {
        // The finished pass's ONE full-map publish is a structural, one-time
        // cost; priming it here makes the timed loop the steady state a user
        // types in (T9 comparison).
        if (current) {
            publishCurrent(controller, lastView, lastFiles);
        } else {
            publishLegacy(controller, lastLegacyRevision);
        }
        panel->poll();
    }
    double nextPublishMs = 400.0;
    double nextPanelMs = 300.0;
    double nextTreeMs = 500.0;
    double virtualMs = 0.0;
    std::vector<double> perKey;
    QElapsedTimer total;
    total.start();

    for (int key = 0; key < options.keys; ++key) {
        QElapsedTimer keyTimer;
        keyTimer.start();
        controller.markFileEdited(edited);
        const double editUs = static_cast<double>(keyTimer.nsecsElapsed()) / 1000.0;
        result.editMs += editUs / 1000.0;
        virtualMs += options.keyMs;

        // The 400 ms live publisher.
        if (virtualMs >= nextPublishMs) {
            nextPublishMs += 400.0;
            QElapsedTimer timer;
            timer.start();
            PublishResult published = current
                ? publishCurrent(controller, lastView, lastFiles)
                : publishLegacy(controller, lastLegacyRevision);
            result.backgroundMs += static_cast<double>(timer.nsecsElapsed()) / 1e6;
            if (published.full) {
                ++result.fullPublishes;
            } else if (published.entries > 0) {
                ++result.deltaPublishes;
            }
            result.statusEntriesPublished += published.entries;
        }

        // The 300 ms DiagnosticsPanel refresh (controller poll + widget copy).
        if (virtualMs >= nextPanelMs) {
            nextPanelMs += 300.0;
            QElapsedTimer timer;
            timer.start();
            panel->poll();
            result.backgroundMs += static_cast<double>(timer.nsecsElapsed()) / 1e6;
            ++result.panelRefreshes;
        }

        // The 500 ms file-tree / project-panel status poll: the published map is
        // re-hashed into the cache and every loaded range repaints. The bench
        // records the published volume (the input to that O(files) work).
        if (virtualMs >= nextTreeMs) {
            nextTreeMs += 500.0;
            ++result.treeRepaints;
        }

        const double keyMs = static_cast<double>(keyTimer.nsecsElapsed()) / 1e6;
        (void)keyMs;
        perKey.push_back(static_cast<double>(keyTimer.nsecsElapsed()) / 1000.0);
    }

    result.totalMs = static_cast<double>(total.nsecsElapsed()) / 1e6;
    std::sort(perKey.begin(), perKey.end());
    if (!perKey.empty()) {
        double sum = 0;
        for (double value : perKey) {
            sum += value;
        }
        result.perKeyUs = sum / static_cast<double>(perKey.size());
        result.p95Us = perKey[static_cast<size_t>(perKey.size() * 95 / 100)];
        result.maxUs = perKey.back();
    }
    // Exact counts, both modes: the controller counts every merged/display
    // rebuild and every publish; the panel counts its own issue-list rebuilds.
    const DiagnosticsWorkStats stats = controller.workStats();
    result.mergedRebuilds = static_cast<int>(stats.mergedRebuilds);
    result.displayRebuilds = static_cast<int>(stats.displayRebuilds);
    // Only the rebuilds the TYPING loop caused; the show/constructor rebuild is a
    // one-time startup cost, not a per-key cost.
    result.panelRebuilds = panel->rebuildCount() - panelRebuildsBeforeLoop;
    if (current) {
        result.fullPublishes = static_cast<int>(stats.fullPublishes);
        result.deltaPublishes = static_cast<int>(stats.deltaPublishes);
        result.statusEntriesPublished = static_cast<int>(stats.statusEntriesPublished);
    }
    std::printf("BENCH mode=%s files=%d keys=%d lines=%d panel=%s total_ms=%.2f "
                "per_key_us=%.2f p95_us=%.2f max_us=%.2f edit_ms=%.2f background_ms=%.2f "
                "full_publishes=%d delta_publishes=%d status_entries=%d panel_refreshes=%d "
                "panel_rebuilds=%d tree_repaints=%d merged_rebuilds=%d display_rebuilds=%d "
                "rows_scanned_on_edit=%llu\n",
                current ? "current" : "legacy", result.files, result.keys, options.lines,
                options.panelVisible ? "visible" : "hidden", result.totalMs, result.perKeyUs,
                result.p95Us, result.maxUs, result.editMs, result.backgroundMs,
                result.fullPublishes, result.deltaPublishes, result.statusEntriesPublished,
                result.panelRefreshes, result.panelRebuilds, result.treeRepaints,
                result.mergedRebuilds, result.displayRebuilds,
                static_cast<unsigned long long>(stats.upperRowsScannedOnEdit));
    if (options.assertBounds) {
        // Deterministic bounds (the wall clock is noisy; these counts are not).
        // They encode the T9 contract: a keystroke repaints ONE file.
        bool ok = true;
        const auto check = [&ok](const char *name, bool condition, long long value) {
            if (!condition) {
                ok = false;
                std::printf("BENCH-ASSERT FAIL %s value=%lld\n", name, value);
            } else {
                std::printf("BENCH-ASSERT PASS %s value=%lld\n", name, value);
            }
        };
        if (current) {
            check("one-structural-publish", result.fullPublishes <= 1, result.fullPublishes);
            // One structural publish (the pass's map) and NOTHING else scales:
            // typing appends at most one status entry total (the edited file),
            // whether the first edit rides the full publish or a later delta.
            check("status-entries-bounded", result.statusEntriesPublished <= result.files + 2,
                  result.statusEntriesPublished - result.files);
            // The rows an edit walks are the EDITED FILE's rows (3), not the
            // project's. 8 is headroom for the first edit's drop + bookkeeping.
            check("edit-scans-file-rows-only",
                  stats.upperRowsScannedOnEdit <= 8,
                  static_cast<long long>(stats.upperRowsScannedOnEdit));
            if (!options.panelVisible) {
                check("hidden-panel-does-not-rebuild", result.panelRebuilds == 0, result.panelRebuilds);
            }
            check("no-merged-rebuild-per-key", result.mergedRebuilds <= 4, result.mergedRebuilds);
            // The incremental counts must equal the full views exactly (the
            // per-file cache is what the status line and the panel read now).
            const int mergedExact = static_cast<int>(controller.mergedDiagnostics().size());
            const int upperExact = static_cast<int>(controller.upperLayerDiagnostics().size());
            check("merged-count-matches-view", controller.mergedDiagnosticCount() == mergedExact,
                  controller.mergedDiagnosticCount() - mergedExact);
            check("upper-count-matches-view",
                  controller.upperLayerDiagnosticCount() == upperExact,
                  controller.upperLayerDiagnosticCount() - upperExact);
        }
        if (!ok) {
            std::printf("BENCH-ASSERT RESULT FAIL\n");
            std::exit(3);
        }
        std::printf("BENCH-ASSERT RESULT PASS\n");
    }
    return result;
}

int parseInt(int argc, char **argv, const char *name, int fallback)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::strcmp(argv[index], name) == 0) {
            return std::atoi(argv[index + 1]);
        }
    }
    return fallback;
}

bool hasFlag(int argc, char **argv, const char *name)
{
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Options options;
    options.files = parseInt(argc, argv, "--files", options.files);
    options.keys = parseInt(argc, argv, "--keys", options.keys);
    options.lines = parseInt(argc, argv, "--lines", options.lines);
    options.keyMs = parseInt(argc, argv, "--key-ms", options.keyMs);
    options.panelVisible = hasFlag(argc, argv, "--panel");
    options.current = hasFlag(argc, argv, "--current");
    options.assertBounds = hasFlag(argc, argv, "--assert");
    options.warmup = hasFlag(argc, argv, "--warmup");

    QStringList files;
    files.reserve(options.files);
    for (int index = 0; index < options.files; ++index) {
        files.append(QStringLiteral("/bench/project/rtl/file%1.sv").arg(index, 5, 10, QLatin1Char('0')));
    }
    std::printf("BENCH-ENV files=%d keys=%d lines=%d key_ms=%d panel=%s\n",
                options.files, options.keys, options.lines, options.keyMs,
                options.panelVisible ? "visible" : "hidden");
    runScenario(options, files, options.current);
    return 0;
}
