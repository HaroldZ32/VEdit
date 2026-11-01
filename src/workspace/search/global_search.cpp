#include "global_search.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include <atomic>
#include <utility>

#if defined(VEDIT_SEARCH_HAVE_FFI)
#include "vd_ffi.h"
#endif

namespace vedit::workspace::search {

namespace {

constexpr int kBatchSize = 200;

#if defined(VEDIT_SEARCH_HAVE_FFI)
QString serviceError(VdService *service)
{
    VdError error{};
    if (service == nullptr || vd_service_error(service, &error) != VD_STATUS_READY) {
        return QStringLiteral("the Rust search service did not report a reason");
    }
    return QString::fromUtf8(error.message);
}
#endif

} // namespace

GlobalSearch::GlobalSearch(QObject *parent)
    : QObject(parent)
    , cancelRequested_(std::make_shared<std::atomic_bool>(false))
{
}

GlobalSearch::~GlobalSearch()
{
    // Lifetime (verdict ws-search M2): the worker only reads the per-scan shared
    // cancellation token; the destructor waits until the worker has returned
    // before any member is destroyed. The Rust job itself is owned by its
    // service (Arc), so destroying that service after the worker returns cannot
    // free memory the worker still reads (vd-ffi service.rs lifetime note).
    cancel();
    if (thread_ != nullptr) {
        thread_->wait();
        QObject::disconnect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
        QCoreApplication::removePostedEvents(thread_);
        delete thread_;
        thread_ = nullptr;
    }
    QCoreApplication::removePostedEvents(this);
}

bool GlobalSearch::start(const SearchOptions &options)
{
    if (thread_ != nullptr) {
        cancel();
        if (!thread_->wait(5000)) {
            // Refuse to run two scans on the same object; the caller can retry.
            return false;
        }
        QObject::disconnect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
        QCoreApplication::removePostedEvents(thread_);
        delete thread_;
        thread_ = nullptr;
    }

    auto cancelTag = std::make_shared<std::atomic_bool>(false);
    cancelRequested_ = cancelTag;

    running_.store(true);
    ranOnWorkerThread_.store(false);
    const quint64 generation = ++generation_;

    QThread *thread = QThread::create(
        [this, options, generation, cancelTag]() { runScan(options, generation, cancelTag); });
    thread->setObjectName(QStringLiteral("vedit-global-search-ffi"));
    thread_ = thread;
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return true;
}

void GlobalSearch::cancel()
{
    if (cancelRequested_ != nullptr) {
        cancelRequested_->store(true);
    }
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

void GlobalSearch::runScan(const SearchOptions &options, quint64 generation,
                           const std::shared_ptr<std::atomic_bool> &cancelTag)
{
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        ranOnWorkerThread_.store(true);
    }

    QElapsedTimer clock;
    clock.start();
    SearchSummary summary;

    const QFileInfo rootInfo(options.rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        summary.state = SearchState::Failed;
        summary.error = QStringLiteral("search root does not exist or is not a directory: %1")
                            .arg(options.rootPath);
        summary.elapsedMs = clock.elapsed();
    } else if (!rootInfo.isReadable()) {
        summary.state = SearchState::Failed;
        summary.error = QStringLiteral("search root is not readable: %1").arg(options.rootPath);
        summary.elapsedMs = clock.elapsed();
    } else if (options.query.isEmpty()) {
        summary.state = SearchState::Failed;
        summary.error = QStringLiteral("empty query");
        summary.elapsedMs = clock.elapsed();
    } else {
#if defined(VEDIT_SEARCH_HAVE_FFI)
        VdService *service = vd_service_create();
        if (service == nullptr) {
            summary.state = SearchState::Failed;
            summary.error = QStringLiteral("cannot create the Rust search service");
        } else {
            const QByteArray rootPath = options.rootPath.toUtf8();
            const QByteArray query = options.query.toUtf8();
            const quint64 job = vd_search_tree_query(
                service, rootPath.constData(), query.constData(),
                static_cast<size_t>(options.maxResults),
                static_cast<quint64>(options.maxFileBytes),
                static_cast<quint64>(options.maxDurationMs), options.caseSensitive ? 1 : 0,
                options.verilogOnly ? 1 : 0);
            if (job == 0) {
                summary.state = SearchState::Failed;
                summary.error = serviceError(service);
            } else {
                bool cancelled = false;
                for (;;) {
                    VdSearchResults results{};
                    VdSearchSummary ffiSummary{};
                    const qint32 status = vd_search_tree_poll(service, job, &results, &ffiSummary);
                    if (status == VD_STATUS_PENDING) {
                        if (cancelTag->load(std::memory_order_relaxed)) {
                            cancelled = true;
                            break;
                        }
                        QThread::msleep(5);
                        continue;
                    }
                    if (status == VD_STATUS_READY) {
                        QList<SearchHit> hits;
                        hits.reserve(static_cast<int>(results.count));
                        for (size_t index = 0; index < results.count; ++index) {
                            const VdSearchHit &item = results.items[index];
                            SearchHit hit;
                            hit.relativePath = QString::fromUtf8(item.path);
                            hit.filePath = QDir(options.rootPath).absoluteFilePath(hit.relativePath);
                            hit.line = static_cast<int>(item.line);
                            hit.column = static_cast<int>(item.column);
                            hit.text = QString::fromUtf8(item.preview);
                            hits.append(hit);
                        }
                        vd_search_results_free(&results);
                        summary.state = SearchState::Done;
                        summary.filesScanned = static_cast<int>(ffiSummary.files_scanned);
                        summary.filesMatched = static_cast<int>(ffiSummary.files_matched);
                        summary.hitCount = static_cast<int>(ffiSummary.hit_count);
                        summary.skippedBinary = static_cast<int>(ffiSummary.skipped_binary);
                        summary.skippedTooLarge = static_cast<int>(ffiSummary.skipped_too_large);
                        summary.skippedUnreadable = static_cast<int>(ffiSummary.skipped_unreadable);
                        summary.truncated = ffiSummary.truncated != 0;
                        summary.timedOut = ffiSummary.timed_out != 0;
                        summary.elapsedMs = static_cast<qint64>(ffiSummary.elapsed_ms);
                        // The Rust service returns one complete result; the
                        // panel still receives it in bounded batches.
                        for (int offset = 0; offset < hits.size(); offset += kBatchSize) {
                            QList<SearchHit> batch = hits.mid(offset, kBatchSize);
                            QMetaObject::invokeMethod(
                                this,
                                [this, generation, batch = std::move(batch)]() {
                                    if (generation != generation_) {
                                        return;
                                    }
                                    emit hitsReady(batch);
                                },
                                Qt::QueuedConnection);
                        }
                    } else {
                        summary.state = SearchState::Failed;
                        summary.error = serviceError(service);
                    }
                    break;
                }
                if (cancelled) {
                    summary.state = SearchState::Cancelled;
                    summary.elapsedMs = clock.elapsed();
                }
            }
            vd_service_destroy(service);
        }
#else
        summary.state = SearchState::Failed;
        summary.error = QStringLiteral(
            "the Rust search service is not linked in this build; the Qt side does not "
            "implement a second search backend");
#endif
    }

    if (summary.elapsedMs == 0 && summary.state != SearchState::Done) {
        summary.elapsedMs = clock.elapsed();
    }

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
