// 10,000-file palette benchmark for acceptance A2b.
//
// Measures two paths, both inside the pinned Qt6 image:
//   * core filter - FuzzyMatcher::filter over the shared WorkspaceFileIndex;
//   * palette GUI - CommandPalette::setQueryText over the same index, which is
//     exactly the keystroke path (filter + result list rebuild).
//
// The candidate list either comes from a real 10k-file tree on disk
// (--fixture <dir>: created once, then scanned through WorkspaceFileIndex, with
// the cold/cached scan times recorded) or from the deterministic in-memory list
// in synthetic_paths.h.
//
// Output: human-readable BENCH lines on stdout and, with --json, a
// machine-readable record. Exit code: 0 PASS, 3 budget violated, 2 bad input.
#include "command_palette.h"
#include "file_index.h"
#include "fuzzy_matcher.h"
#include "synthetic_paths.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

using namespace vedit::workspace;

struct Stats {
    double medianMs = 0.0;
    double p95Ms = 0.0;
    double maxMs = 0.0;
};

double percentileAt(const QVector<double> &sorted, double fraction)
{
    if (sorted.isEmpty()) {
        return 0.0;
    }
    const int last = static_cast<int>(sorted.size()) - 1;
    int index = static_cast<int>(std::ceil(fraction * sorted.size())) - 1;
    index = std::clamp(index, 0, last);
    return sorted.at(index);
}

Stats statsOf(QVector<double> samples)
{
    Stats stats;
    if (samples.isEmpty()) {
        return stats;
    }
    std::sort(samples.begin(), samples.end());
    stats.medianMs = percentileAt(samples, 0.5);
    stats.p95Ms = percentileAt(samples, 0.95);
    stats.maxMs = samples.last();
    return stats;
}

QJsonObject statsJson(const Stats &stats)
{
    QJsonObject object;
    object.insert(QStringLiteral("median_ms"), stats.medianMs);
    object.insert(QStringLiteral("p95_ms"), stats.p95Ms);
    object.insert(QStringLiteral("max_ms"), stats.maxMs);
    return object;
}

QString createFixture(const QString &root, int files)
{
    const QString markerPath = root + QStringLiteral("/.vedit-palette-bench");
    const QByteArray expected = QByteArray::number(files);
    QFile marker(markerPath);
    if (marker.open(QIODevice::ReadOnly)) {
        const QByteArray found = marker.readAll().trimmed();
        marker.close();
        if (found == expected) {
            return QString(); // already created by an earlier run
        }
    }

    QElapsedTimer timer;
    timer.start();
    const QStringList paths = makeSyntheticPaths(files);
    for (const QString &relative : paths) {
        const QString path = root + u'/' + relative;
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            return QStringLiteral("cannot create directory for %1").arg(path);
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return QStringLiteral("cannot create %1").arg(path);
        }
        file.write(QByteArray("palette bench fixture\n"));
    }
    QFile out(markerPath);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(expected);
    }
    std::printf("BENCH FIXTURE created files=%d ms=%lld\n", files,
                static_cast<long long>(timer.elapsed()));
    return QString();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-workspace-palette-bench"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("10k-file Ctrl+P palette benchmark (VEdit T22 / A2b)."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("files"), QStringLiteral("Candidate file count."),
                      QStringLiteral("count"), QStringLiteral("10000")});
    parser.addOption({QStringLiteral("iterations"), QStringLiteral("Samples per query."),
                      QStringLiteral("count"), QStringLiteral("200")});
    parser.addOption({QStringLiteral("limit"), QStringLiteral("Visible results per query."),
                      QStringLiteral("count"), QStringLiteral("100")});
    parser.addOption({QStringLiteral("fixture"),
                      QStringLiteral("Create/scan a real file tree under this directory."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("json"), QStringLiteral("Write machine-readable results here."),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("assert-p95-ms"),
                      QStringLiteral("Fail when the overall palette p95 exceeds this budget."),
                      QStringLiteral("ms"), QStringLiteral("200")});
    parser.process(app);

    const int files = parser.value(QStringLiteral("files")).toInt();
    const int iterations = parser.value(QStringLiteral("iterations")).toInt();
    const int limit = parser.value(QStringLiteral("limit")).toInt();
    const double budgetMs = parser.value(QStringLiteral("assert-p95-ms")).toDouble();
    if (files <= 0 || iterations <= 0 || limit <= 0) {
        std::printf("BENCH FAIL invalid --files/--iterations/--limit\n");
        return 2;
    }

    QString source = QStringLiteral("synthetic");
    WorkspaceFileIndex index;
    QJsonObject scanJson;
    bool hasScan = false;

    if (parser.isSet(QStringLiteral("fixture"))) {
        const QString fixture = parser.value(QStringLiteral("fixture"));
        const QString error = createFixture(fixture, files);
        if (!error.isEmpty()) {
            std::printf("BENCH FAIL %s\n", qPrintable(error));
            return 2;
        }
        index.setRoot(fixture);
        QElapsedTimer timer;
        timer.start();
        QString scanError;
        if (!index.scan(&scanError)) {
            std::printf("BENCH FAIL %s\n", qPrintable(scanError));
            return 2;
        }
        const qint64 coldMs = timer.elapsed();
        timer.restart();
        index.scan(); // cached: must not walk the disk again
        const qint64 cachedMs = timer.elapsed();
        if (index.size() != files) {
            std::printf("BENCH FAIL indexed files=%d expected=%d\n", index.size(), files);
            return 2;
        }
        source = QStringLiteral("disk");
        hasScan = true;
        scanJson.insert(QStringLiteral("root"), fixture);
        scanJson.insert(QStringLiteral("files"), index.size());
        scanJson.insert(QStringLiteral("cold_ms"), static_cast<double>(coldMs));
        scanJson.insert(QStringLiteral("cached_ms"), static_cast<double>(cachedMs));
        scanJson.insert(QStringLiteral("scan_count"), index.scanCount());
        std::printf("BENCH SCAN files=%d cold_ms=%lld cached_ms=%lld scan_count=%d\n",
                    index.size(), static_cast<long long>(coldMs),
                    static_cast<long long>(cachedMs), index.scanCount());
    } else {
        index.setFiles(makeSyntheticPaths(files));
        std::printf("BENCH FILES synthetic=%d\n", index.size());
    }

    const QStringList queries{QStringLiteral("m"),      QStringLiteral("ma"),
                              QStringLiteral("mai"),    QStringLiteral("main"),
                              QStringLiteral("cmd"),    QStringLiteral("pal"),
                              QStringLiteral("src"),    QStringLiteral("widget"),
                              QStringLiteral("file_index"), QStringLiteral("zzzz")};

    // Warm-up (first-touch caches, allocator growth) before any sample.
    for (const QString &query : queries) {
        FuzzyMatcher::filter(query, index.displayPaths(), index.basenames(), limit);
    }

    QHash<QString, QVector<double>> coreSamples;
    QVector<double> coreAll;
    coreAll.reserve(static_cast<qsizetype>(queries.size()) * iterations);
    QElapsedTimer timer;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        for (const QString &query : queries) {
            timer.start();
            const auto results =
                FuzzyMatcher::filter(query, index.displayPaths(), index.basenames(), limit);
            const double ms = timer.nsecsElapsed() / 1000000.0;
            coreSamples[query].append(ms);
            coreAll.append(ms);
            if (iteration == 0 && query != QStringLiteral("zzzz") && results.isEmpty()) {
                std::printf("BENCH FAIL query=%s returned no matches\n", qPrintable(query));
                return 2;
            }
        }
    }

    CommandPalette palette;
    palette.setMaxResults(limit);
    palette.setFileIndex(&index);
    palette.openPalette();

    QHash<QString, QVector<double>> guiSamples;
    QVector<double> guiAll;
    guiAll.reserve(coreAll.size());
    for (int iteration = 0; iteration < iterations; ++iteration) {
        for (const QString &query : queries) {
            palette.setQueryText(query);
            const double ms = static_cast<double>(palette.lastFilterMicros()) / 1000.0;
            guiSamples[query].append(ms);
            guiAll.append(ms);
        }
    }
    if (index.scanCount() > 1) {
        std::printf("BENCH FAIL palette rescanned the disk (%d scans)\n", index.scanCount());
        return 2;
    }

    const Stats coreStats = statsOf(coreAll);
    const Stats guiStats = statsOf(guiAll);
    for (const QString &query : queries) {
        const Stats core = statsOf(coreSamples.value(query));
        const Stats gui = statsOf(guiSamples.value(query));
        std::printf("BENCH CORE q=%s median_ms=%.3f p95_ms=%.3f max_ms=%.3f\n", qPrintable(query),
                    core.medianMs, core.p95Ms, core.maxMs);
        std::printf("BENCH GUI  q=%s median_ms=%.3f p95_ms=%.3f max_ms=%.3f\n", qPrintable(query),
                    gui.medianMs, gui.p95Ms, gui.maxMs);
    }
    std::printf("BENCH CORE overall median_ms=%.3f p95_ms=%.3f max_ms=%.3f\n", coreStats.medianMs,
                coreStats.p95Ms, coreStats.maxMs);
    std::printf("BENCH GUI  overall median_ms=%.3f p95_ms=%.3f max_ms=%.3f\n", guiStats.medianMs,
                guiStats.p95Ms, guiStats.maxMs);

    const double overallP95 = std::max(coreStats.p95Ms, guiStats.p95Ms);
    const bool pass = overallP95 <= budgetMs;
    const QString verdict = pass ? QStringLiteral("PASS") : QStringLiteral("FAIL");
    std::printf("BENCH VERDICT %s p95_ms=%.3f budget_ms=%.3f files=%d source=%s\n",
                qPrintable(verdict), overallP95, budgetMs, index.size(), qPrintable(source));

    if (parser.isSet(QStringLiteral("json"))) {
        QJsonObject root;
        root.insert(QStringLiteral("schema"), 1);
        root.insert(QStringLiteral("source"), source);
        root.insert(QStringLiteral("files"), index.size());
        root.insert(QStringLiteral("iterations"), iterations);
        root.insert(QStringLiteral("limit"), limit);
        root.insert(QStringLiteral("budget_ms"), budgetMs);
        root.insert(QStringLiteral("verdict"), verdict);

        QJsonObject corePerQuery;
        QJsonObject guiPerQuery;
        for (const QString &query : queries) {
            corePerQuery.insert(query, statsJson(statsOf(coreSamples.value(query))));
            guiPerQuery.insert(query, statsJson(statsOf(guiSamples.value(query))));
        }
        QJsonObject core;
        core.insert(QStringLiteral("overall"), statsJson(coreStats));
        core.insert(QStringLiteral("per_query"), corePerQuery);
        root.insert(QStringLiteral("core_filter"), core);

        QJsonObject gui;
        gui.insert(QStringLiteral("overall"), statsJson(guiStats));
        gui.insert(QStringLiteral("per_query"), guiPerQuery);
        root.insert(QStringLiteral("palette_gui"), gui);
        if (hasScan) {
            root.insert(QStringLiteral("disk_scan"), scanJson);
        }
        root.insert(QStringLiteral("overall_p95_ms"), overallP95);

        const QString path = parser.value(QStringLiteral("json"));
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            std::printf("BENCH FAIL cannot write %s\n", qPrintable(path));
            return 2;
        }
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        std::printf("BENCH JSON %s\n", qPrintable(path));
    }

    return pass ? 0 : 3;
}
