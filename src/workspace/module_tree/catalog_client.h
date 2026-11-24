#pragma once

// Async driver of the module catalog service (UX batch 2, item 1/2).
//
// Same shape as every other service consumer in this product: the client reads
// the project's effective file set (the project model's persisted
// `project-model.json`, falling back to a `vd_project_expand_v2` job when that
// file does not exist yet), starts a `vd_catalog_start` job and polls it from
// one QTimer. slang NEVER runs on the GUI thread - `vd_catalog_start` only
// enqueues the work and returns a job id; the poll is a non-blocking status
// read. One run is in flight at a time; `start()` cancels the previous one.
//
// The data source follows the project lane's rule: the project root of the
// persisted project model is the single source of truth for the root and for
// the whole file set (never the file that happens to be open).
#include "module_catalog.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

namespace vedit::workspace::module_tree {

class CatalogClient : public QObject {
    Q_OBJECT
public:
    explicit CatalogClient(QObject *parent = nullptr);
    ~CatalogClient() override;

    /// The project-model file that supplies root/files/-I/-D.
    void setStateDirectory(const QString &stateDirectory);
    /// Explicit slang binary; empty lets the Rust service resolve it
    /// (bundled > local checkout > PATH > env, T7/T82).
    void setSlangBinary(const QString &binary);
    /// RM-17: the top-level module the next run elaborates from ("" = Auto).
    void setTop(const QString &top);
    QString top() const;
    /// The project file set the last run was started with (absolute).
    QStringList lastFileSet() const;

    /// Starts (or restarts) the chain: project file set -> module catalog.
    void start(const QString &projectRoot);
    /// Stops the running work. A running catalog is CANCELLED (`vd_catalog_cancel`
    /// kills the `slang` child) and then released — releasing alone left the
    /// child elaborating for minutes into memory nobody reads (AC M1.4).
    void cancel();

    /// AC M1.5: files above this count are not catalogued by an automatic run.
    /// `0` disables the gate (tests, a caller that knows better).
    void setLargeProjectThreshold(int sources);
    /// Permission for exactly ONE run to exceed the threshold (the user pressed
    /// Refresh or named a top). Consumed by the next run, whether it starts or is
    /// withheld.
    void setAllowLargeCatalog(bool allow);
    /// Wall-clock budget for the `slang` child; 0 keeps the service default
    /// (120 s). The test seam for the timeout rules in AC M1.1/M1.2.
    void setTimeoutMs(int milliseconds);

    void startFromFileSet(const QString &root, const QStringList &files,
                          const QStringList &includeDirs, const QStringList &defines);
    bool isRunning() const;
    QString projectRoot() const;
    /// What the last completed run actually covered (project source of truth).
    QString sourceDescription() const;

    void setOnReady(std::function<void(const ModuleCatalog &)> callback);
    /// A failed run: the sentence AND its class (AC M1.1 — only `UnknownTop` may
    /// drop the user's top).
    void setOnFailed(std::function<void(const QString &, CatalogFailureKind)> callback);
    /// AC M1.5: the run was withheld because the file set is large and no top was
    /// named. The panel shows `largeProjectHint(fileCount)`.
    void setOnLargeProject(std::function<void(int fileCount)> callback);
    void setOnProgress(std::function<void(const QString &)> callback);

private:
    void beginProjectFallback();
    void beginCatalog(const QStringList &files, const QStringList &includeDirs,
                      const QStringList &defines, const QString &root, int fileCount,
                      const QString &source);
    void poll();
    void finish(const ModuleCatalog &catalog);
    void fail(const QString &message, CatalogFailureKind kind);
    QString serviceError(int *code = nullptr) const;
    void resetJobs();

    QTimer *timer_ = nullptr;
    void *service_ = nullptr; ///< VdService*, kept as void* so the header has no C ABI type
    quint64 job_ = 0;
    int ticks_ = 0;
    bool catalogPhase_ = false;
    QString projectRoot_;
    QString top_;
    QStringList lastFileSet_;
    QString stateDirectory_;
    QString slangBinary_;
    int fileCount_ = 0;
    int timeoutMs_ = 0;
    int largeProjectThreshold_ = kLargeProjectFileThreshold;
    bool allowLargeCatalog_ = false;
    QString source_;
    std::function<void(const ModuleCatalog &)> onReady_;
    std::function<void(const QString &, CatalogFailureKind)> onFailed_;
    std::function<void(int)> onLargeProject_;
    std::function<void(const QString &)> onProgress_;
};

} // namespace vedit::workspace::module_tree
