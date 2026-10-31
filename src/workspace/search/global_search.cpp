#include "global_search.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include <atomic>
#include <functional>
#include <utility>

namespace vedit::workspace::search {

namespace {

constexpr int kBatchSize = 64;
constexpr int kMaxDepth = 64;
constexpr int kMaxSnippetChars = 400;
constexpr int kStopCheckMask = 0xFF; // check cancel/budget every 256 lines
constexpr qint64 kFlushIntervalMs = 40;

// One scan. Lives entirely on the worker thread; all state it exposes goes out
// through the batch sink and the returned SearchSummary.
class Scanner final
{
public:
    using BatchSink = std::function<void(QList<SearchHit>)>;

    Scanner(const SearchOptions &options, const std::atomic_bool &cancel, BatchSink sink)
        : options_(options)
        , cancel_(cancel)
        , sink_(std::move(sink))
    {
        if (options_.excludedDirNames.isEmpty()) {
            options_.excludedDirNames = defaultExcludedDirNames();
        }
        if (options_.maxResults <= 0) {
            options_.maxResults = 2000;
        }
        if (options_.maxFileBytes <= 0) {
            options_.maxFileBytes = 8LL * 1024 * 1024;
        }
        if (options_.maxDurationMs <= 0) {
            options_.maxDurationMs = 15000;
        }
    }

    SearchSummary run()
    {
        clock_.start();
        const QFileInfo root(options_.rootPath);
        if (!root.exists() || !root.isDir()) {
            return failed(QStringLiteral("search root does not exist or is not a directory: %1")
                              .arg(options_.rootPath));
        }
        if (!root.isReadable()) {
            return failed(QStringLiteral("search root is not readable: %1").arg(root.absoluteFilePath()));
        }
        if (options_.query.isEmpty()) {
            return failed(QStringLiteral("empty query"));
        }
        rootPath_ = root.absoluteFilePath();
        walk(rootPath_, 0);
        flush(true);
        summary_.state = cancelled() ? SearchState::Cancelled : SearchState::Done;
        summary_.elapsedMs = clock_.elapsed();
        return summary_;
    }

private:
    SearchSummary failed(const QString &error)
    {
        summary_.state = SearchState::Failed;
        summary_.error = error;
        summary_.elapsedMs = clock_.elapsed();
        return summary_;
    }

    bool cancelled() const { return cancel_.load(std::memory_order_relaxed); }

    // Cancellation, result limit and time budget share one decision point so a
    // stopped scan can always explain why it stopped.
    bool stopRequested()
    {
        if (cancelled()) {
            return true;
        }
        if (summary_.truncated) {
            return true;
        }
        if (clock_.elapsed() >= options_.maxDurationMs) {
            summary_.timedOut = true;
            return true;
        }
        return false;
    }

    void walk(const QString &absoluteDirectory, int depth)
    {
        if (depth > kMaxDepth || stopRequested()) {
            return;
        }
        const QDir dir(absoluteDirectory);
        // Readability is NOT a listing filter: an unreadable entry must be
        // counted (observable), not silently disappear from the results.
        const QFileInfoList entries = dir.entryInfoList(
            QDir::Filters(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::DirsFirst),
            QDir::SortFlags(QDir::Name | QDir::IgnoreCase));
        for (const QFileInfo &entry : entries) {
            if (stopRequested()) {
                return;
            }
            if (entry.isSymLink()) {
                continue; // never follow links: cycles and escaping the root
            }
            if (entry.isDir()) {
                if (options_.excludedDirNames.contains(entry.fileName())) {
                    continue; // .git / build / target / node_modules ...
                }
                if (!entry.isReadable()) {
                    ++summary_.skippedUnreadable;
                    continue;
                }
                walk(entry.absoluteFilePath(), depth + 1);
                continue;
            }
            if (!entry.isFile()) {
                continue;
            }
            scanFile(entry);
        }
    }

    void scanFile(const QFileInfo &info)
    {
        if (stopRequested()) {
            return;
        }
        if (options_.verilogOnly && !isVerilogFileName(info.fileName())) {
            return;
        }
        if (info.size() > options_.maxFileBytes) {
            ++summary_.skippedTooLarge;
            return;
        }
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            ++summary_.skippedUnreadable;
            return;
        }
        const QByteArray bytes = file.readAll();
        file.close();
        if (bytes.contains('\0')) {
            ++summary_.skippedBinary;
            return;
        }
        ++summary_.filesScanned;

        const QString relative = QDir(rootPath_).relativeFilePath(info.absoluteFilePath());
        const Qt::CaseSensitivity sensitivity =
            options_.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        const int total = bytes.size();
        int lineNumber = 1;
        int start = 0;
        while (start <= total) {
            if ((lineNumber & kStopCheckMask) == 0 && stopRequested()) {
                break;
            }
            if (summary_.truncated) {
                break;
            }
            int end = bytes.indexOf('\n', start);
            if (end < 0) {
                end = total;
            }
            QByteArray raw = bytes.mid(start, end - start);
            if (raw.endsWith('\r')) {
                raw.chop(1);
            }
            if (!raw.isEmpty()) {
                const QString text = QString::fromUtf8(raw);
                const int column = text.indexOf(options_.query, 0, sensitivity);
                if (column >= 0) {
                    recordHit(info.absoluteFilePath(), relative, lineNumber, column + 1, text);
                }
            }
            if (end >= total) {
                break;
            }
            start = end + 1;
            ++lineNumber;
        }
        flush(false);
    }

    void recordHit(const QString &filePath, const QString &relativePath, int line, int column,
                   const QString &text)
    {
        if (summary_.hitCount >= options_.maxResults) {
            summary_.truncated = true;
            return;
        }
        SearchHit hit;
        hit.filePath = filePath;
        hit.relativePath = relativePath;
        hit.line = line;
        hit.column = column;
        hit.text = text.size() > kMaxSnippetChars
                       ? text.left(kMaxSnippetChars) + QChar(0x2026)
                       : text;
        pending_.append(hit);
        ++summary_.hitCount;
        // Hits are produced file by file, so a change of path is a new file.
        if (lastMatchedFile_ != filePath) {
            lastMatchedFile_ = filePath;
            ++summary_.filesMatched;
        }
    }

    void flush(bool force)
    {
        if (pending_.isEmpty()) {
            return;
        }
        const qint64 now = clock_.elapsed();
        if (!force && pending_.size() < kBatchSize && now - lastFlushMs_ < kFlushIntervalMs) {
            return;
        }
        sink_(std::move(pending_));
        pending_.clear();
        lastFlushMs_ = now;
    }

    SearchOptions options_;
    const std::atomic_bool &cancel_;
    BatchSink sink_;
    QElapsedTimer clock_;
    SearchSummary summary_;
    QList<SearchHit> pending_;
    QString rootPath_;
    QString lastMatchedFile_;
    qint64 lastFlushMs_ = 0;
};

} // namespace

GlobalSearch::GlobalSearch(QObject *parent)
    : QObject(parent)
{
}

GlobalSearch::~GlobalSearch()
{
    cancel();
    if (thread_ != nullptr) {
        thread_->wait(10000);
    }
    // The worker posts its last results through invokeMethod(); once the thread
    // is gone those events would otherwise touch a half-destroyed object.
    QCoreApplication::removePostedEvents(this);
}

bool GlobalSearch::start(const SearchOptions &options)
{
    cancelRequested_.store(true);
    if (thread_ != nullptr) {
        if (!thread_->wait(5000)) {
            // Refuse to run two scans on the same object; the caller can retry.
            return false;
        }
        delete thread_;
        thread_ = nullptr;
    }

    running_.store(true);
    cancelRequested_.store(false);
    ranOnWorkerThread_.store(false);
    const quint64 generation = ++generation_;

    QThread *thread = QThread::create([this, options, generation]() { runScan(options, generation); });
    thread->setObjectName(QStringLiteral("vedit-global-search"));
    thread_ = thread;
    // The QThread object lives in this (GUI) thread; deleteLater runs here.
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return true;
}

void GlobalSearch::cancel()
{
    cancelRequested_.store(true);
}

bool GlobalSearch::isRunning() const
{
    return running_.load();
}

SearchSummary GlobalSearch::summary() const
{
    return lastSummary_;
}

bool GlobalSearch::ranOnWorkerThread() const
{
    return ranOnWorkerThread_.load();
}

void GlobalSearch::runScan(const SearchOptions &options, quint64 generation)
{
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        ranOnWorkerThread_.store(true);
    }

    Scanner scanner(options, cancelRequested_, [this, generation](QList<SearchHit> batch) {
        QMetaObject::invokeMethod(
            this,
            [this, generation, batch = std::move(batch)]() {
                if (generation != generation_) {
                    return; // superseded by a newer search
                }
                emit hitsReady(batch);
            },
            Qt::QueuedConnection);
    });

    const SearchSummary summary = scanner.run();

    QMetaObject::invokeMethod(
        this,
        [this, generation, summary]() {
            if (generation != generation_) {
                return;
            }
            lastSummary_ = summary;
            running_.store(false);
            emit finished(summary);
        },
        Qt::QueuedConnection);
}

} // namespace vedit::workspace::search
