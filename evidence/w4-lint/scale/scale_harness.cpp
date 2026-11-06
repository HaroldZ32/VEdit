// A-12: measures the REAL project lint pass (Rust batch service + a real tool)
// at scale, through the same DiagnosticsController the product uses.
//
//   scale_harness --root <fixture> --slang <path> [--limit N] [--mode full|overscale]
//                 [--cancel-after-ms N] [--tick-ms N]
//
// The polling loop is the GUI tick: refresh() + processEvents() is exactly what
// the product's QTimer does, so `worst_tick_ms` is the longest stall the UI
// thread would see during the pass.
#include "diagnostics_controller.h"

// A-12 / T44 addition: the ELABORATION measurement goes through the shipped C
// ABI (`vd_hier_start` -> `vd_hier_poll`), i.e. the exact path the module tree
// will call, not a private shortcut.
#include "vd_ffi.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

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

/// Run one hierarchical elaboration through the C ABI and print the AC-3.5
/// numbers: wall time, the child's peak RSS, the elaborated instance count and
/// the deepest hierarchical path actually produced.
int runElaborate(const QString &root, const QString &slang, const QString &top, int timeoutMs,
                 int tickMs, const QStringList &files)
{
    std::vector<std::string> fileStorage;
    std::vector<char *> filePointers;
    for (const QString &file : files) {
        fileStorage.push_back(file.toStdString());
    }
    for (std::string &path : fileStorage) {
        filePointers.push_back(path.data());
    }
    VdStringList fileList{};
    fileList.count = filePointers.size();
    fileList.items = filePointers.data();

    std::vector<std::string> includeStorage{root.toStdString() + "/inc"};
    std::vector<char *> includePointers{includeStorage[0].data()};
    VdStringList includeList{};
    includeList.count = includePointers.size();
    includeList.items = includePointers.data();

    std::vector<std::string> defineStorage{"SCALE=1"};
    std::vector<char *> definePointers{defineStorage[0].data()};
    VdStringList defineList{};
    defineList.count = definePointers.size();
    defineList.items = definePointers.data();

    const std::string slangUtf8 = slang.toStdString();
    const std::string topUtf8 = top.toStdString();
    VdHierOptions options{};
    options.slang_binary = slang.isEmpty() ? nullptr : slangUtf8.c_str();
    options.top = top.isEmpty() ? nullptr : topUtf8.c_str();
    options.include_dirs = includeList;
    options.defines = defineList;
    options.timeout_ms = timeoutMs > 0 ? static_cast<uint64_t>(timeoutMs) : 0;
    options.max_instances = 0; // 0 = the shipped default (10 000)
    options.max_files = 0;     // 0 = the shipped default (1 000)

    struct VdService *service = vd_service_create();
    if (service == nullptr) {
        std::printf("SCALE-ERROR no service handle\n");
        return 2;
    }
    QElapsedTimer wall;
    wall.start();
    const uint64_t job = vd_hier_start(service, &fileList, &options);
    if (job == 0) {
        VdError error{};
        vd_service_error(service, &error);
        std::printf("SCALE-ERROR hier start: %s\n", error.message);
        vd_service_destroy(service);
        return 2;
    }
    VdHierarchy hierarchy{};
    int32_t status = VD_STATUS_PENDING;
    int polls = 0;
    qint64 worstTick = 0;
    while (status == VD_STATUS_PENDING) {
        status = vd_hier_poll(service, job, &hierarchy);
        if (status != VD_STATUS_PENDING) {
            break;
        }
        QElapsedTimer tick;
        tick.start();
        processEvents();
        worstTick = std::max(worstTick, tick.elapsed());
        ++polls;
        QThread::msleep(static_cast<unsigned long>(std::max(0, tickMs)));
        if (polls > 200000) {
            std::printf("SCALE-ERROR elaborate poll loop did not terminate\n");
            vd_service_destroy(service);
            return 3;
        }
    }
    const qint64 wallMs = wall.elapsed();
    if (status != VD_STATUS_READY) {
        VdError error{};
        vd_service_error(service, &error);
        std::printf("ELABORATE-FAILED status=%d wall_ms=%lld polls=%d error=«%s»\n", status,
                    static_cast<long long>(wallMs), polls, error.message);
        vd_service_destroy(service);
        return 3;
    }

    int maxDepth = 0;
    int deepest = 0;
    for (size_t index = 0; index < hierarchy.instances.count; ++index) {
        const char *path = hierarchy.instances.items[index].path;
        if (path == nullptr) {
            continue;
        }
        int depth = 1;
        for (const char *cursor = path; *cursor != '\0'; ++cursor) {
            if (*cursor == '.') {
                ++depth;
            }
        }
        if (depth > maxDepth) {
            maxDepth = depth;
            deepest = static_cast<int>(index);
        }
    }
    const char *deepestPath = hierarchy.instances.count > 0 && deepest >= 0
        ? hierarchy.instances.items[deepest].path
        : "";
    std::printf(
        "ELABORATE top=«%s» tool=«%s» files=%d instances=%zu max_depth=%d deepest=«%s» "
        "polls=%d wall_ms=%lld worst_tick_ms=%lld elapsed_ms=%llu peak_rss_kb=%llu "
        "warning=«%s»\n",
        hierarchy.top != nullptr ? hierarchy.top : "", hierarchy.tool != nullptr ? hierarchy.tool : "",
        static_cast<int>(files.size()), hierarchy.instances.count, maxDepth,
        deepestPath != nullptr ? deepestPath : "", polls, static_cast<long long>(wallMs),
        static_cast<long long>(worstTick), static_cast<unsigned long long>(hierarchy.elapsed_ms),
        static_cast<unsigned long long>(hierarchy.peak_rss_kb),
        hierarchy.warning != nullptr ? hierarchy.warning : "");
    check("elaborate.instances", hierarchy.instances.count > 0);
    check("elaborate.depth-at-least-2", maxDepth >= 2);
    check("elaborate.wall-under-30s", wallMs <= 30000);
    check("elaborate.tick-under-100ms", worstTick <= 100);
    vd_hierarchy_free(&hierarchy);
    vd_service_destroy(service);
    std::printf("ELABORATE-HARNESS ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString root;
    QString slang;
    QString mode = QStringLiteral("full");
    int limit = 0;
    int cancelAfterMs = -1;
    int tickMs = 10;
    int timeoutMs = 0;
    QString top;
    const QStringList args = QCoreApplication::arguments();
    for (int index = 1; index < args.size(); ++index) {
        const QString &arg = args.at(index);
        const QString next = index + 1 < args.size() ? args.at(index + 1) : QString();
        if (arg == QStringLiteral("--root")) {
            root = next;
            ++index;
        } else if (arg == QStringLiteral("--slang")) {
            slang = next;
            ++index;
        } else if (arg == QStringLiteral("--mode")) {
            mode = next;
            ++index;
        } else if (arg == QStringLiteral("--limit")) {
            limit = next.toInt();
            ++index;
        } else if (arg == QStringLiteral("--cancel-after-ms")) {
            cancelAfterMs = next.toInt();
            ++index;
        } else if (arg == QStringLiteral("--tick-ms")) {
            tickMs = next.toInt();
            ++index;
        } else if (arg == QStringLiteral("--timeout-ms")) {
            timeoutMs = next.toInt();
            ++index;
        } else if (arg == QStringLiteral("--top")) {
            top = next;
            ++index;
        }
    }
    if (root.isEmpty()) {
        std::printf("SCALE-ERROR missing --root\n");
        return 2;
    }

    QStringList files;
    QDirIterator iterator(root + QStringLiteral("/rtl"), {QStringLiteral("*.sv"), QStringLiteral("*.v")},
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        files.append(iterator.next());
    }
    files.sort();
    if (limit > 0 && files.size() > limit) {
        files = files.mid(0, limit);
    }
    if (files.isEmpty()) {
        std::printf("SCALE-ERROR no sources under %s/rtl\n", qPrintable(root));
        return 2;
    }

    if (mode == QStringLiteral("elaborate")) {
        return runElaborate(root, slang, top, timeoutMs, tickMs, files);
    }

    DiagnosticsController controller(nullptr, nullptr);
    controller.setProjectLintBackend(createFfiProjectLintBackend());
    LintPassRequest request;
    request.files = files;
    request.engine = 0; // slang
    request.slangPath = slang;
    request.includeDirs = {root + QStringLiteral("/inc")};
    request.defines = {QStringLiteral("SCALE=1")};

    QString error;
    QElapsedTimer wall;
    wall.start();
    if (!controller.startProjectPass(request, &error)) {
        std::printf("SCALE-ERROR start: %s\n", qPrintable(error));
        return 2;
    }
    const LintPassSnapshot started = controller.projectPass();
    const bool overscaleWarning = started.message.contains(QStringLiteral("large pass"));
    std::printf("START files=%d phase=%d tool=%s message=«%s»\n", static_cast<int>(files.size()),
                static_cast<int>(started.phase), qPrintable(started.tool),
                qPrintable(started.message));

    qint64 worstTick = 0;
    int ticks = 0;
    int lastDone = 0;
    bool monotonic = true;
    int progressSamples = 0;
    int maxPercent = 0;
    qint64 cancelLatencyMs = -1;
    bool cancelled = false;
    while (controller.projectPassRunning()) {
        if (cancelAfterMs >= 0 && wall.elapsed() >= cancelAfterMs) {
            QElapsedTimer cancelTimer;
            cancelTimer.start();
            controller.cancelProjectPass();
            cancelLatencyMs = cancelTimer.elapsed();
            cancelled = true;
            break;
        }
        QElapsedTimer tick;
        tick.start();
        controller.refresh();
        processEvents();
        worstTick = std::max(worstTick, tick.elapsed());
        ++ticks;
        const LintPassSnapshot snapshot = controller.projectPass();
        if (snapshot.done < lastDone) {
            monotonic = false;
        }
        lastDone = snapshot.done;
        if (snapshot.total > 0) {
            ++progressSamples;
            maxPercent = std::max(maxPercent, snapshot.percent());
        }
        QThread::msleep(static_cast<unsigned long>(std::max(0, tickMs)));
        if (ticks > 200000) {
            std::printf("SCALE-ERROR poll loop did not terminate\n");
            return 3;
        }
    }
    const LintPassSnapshot finalSnapshot = controller.projectPass();
    std::printf(
        "RESULT files=%d ticks=%d wall_ms=%lld worst_tick_ms=%lld phase=%d done=%d total=%d "
        "percent=%d monotonic=%d progress_samples=%d cli_diagnostics=%d overscale_warn=%d "
        "message=«%s»\n",
        static_cast<int>(files.size()), ticks, static_cast<long long>(wall.elapsed()),
        static_cast<long long>(worstTick), static_cast<int>(finalSnapshot.phase),
        finalSnapshot.done, finalSnapshot.total, finalSnapshot.percent(), monotonic ? 1 : 0,
        progressSamples, static_cast<int>(controller.cliDiagnostics().size()),
        overscaleWarning ? 1 : 0, qPrintable(finalSnapshot.message));

    check("ui.tick-under-100ms", worstTick <= 100);
    check("progress.monotonic", monotonic);
    if (cancelled) {
        std::printf("CANCEL latency_ms=%lld phase=%d running=%d message=«%s»\n",
                    static_cast<long long>(cancelLatencyMs),
                    static_cast<int>(finalSnapshot.phase), controller.projectPassRunning() ? 1 : 0,
                    qPrintable(finalSnapshot.message));
        check("cancel.phase", finalSnapshot.phase == LintPhase::Cancelled);
        check("cancel.flag-cleared", !controller.projectPassRunning());
        check("cancel.bounded-latency", cancelLatencyMs >= 0 && cancelLatencyMs <= 100);
        const int before = static_cast<int>(controller.cliDiagnostics().size());
        for (int poll = 0; poll < 50; ++poll) {
            controller.refresh();
            processEvents();
        }
        std::printf("AFTER-CANCEL cli_before=%d cli_after=%d running=%d\n", before,
                    static_cast<int>(controller.cliDiagnostics().size()),
                    controller.projectPassRunning() ? 1 : 0);
        check("cancel.no-late-diagnostics",
              controller.cliDiagnostics().size() == before);
        check("overscale.warning", mode != QStringLiteral("overscale") || overscaleWarning);
    } else {
        check("pass.finished", finalSnapshot.phase == LintPhase::Finished);
        check("progress.reached-total",
              finalSnapshot.done == static_cast<int>(files.size())
                  && finalSnapshot.total == static_cast<int>(files.size()));
        check("progress.samples", progressSamples > 0);
        std::printf("FINAL cli_diagnostics=%d message=«%s»\n",
                    static_cast<int>(controller.cliDiagnostics().size()),
                    qPrintable(finalSnapshot.message));
    }

    std::printf("LINT-SCALE-HARNESS ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
