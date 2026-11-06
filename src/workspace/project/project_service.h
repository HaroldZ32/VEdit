#pragma once

// Client of the Rust project service over the C ABI (C2: non-blocking).
//
// The service lives in `crates/vd-ffi/src/project.rs`: `vd_project_expand`
// starts a job on a Rust worker thread and returns its id immediately, and
// `vd_project_poll` reports PENDING / READY / FAILED. This class is the only
// place the project lane touches the C ABI; the rest of the lane sees Qt types.
//
// The Qt side never parses a `.f` file and never reads `settings.toml` itself
// (C1/C4): the file list, include dirs, defines and diagnostics all come from
// the service, and the panel only renders them. The imported filelists and the
// settings path are INPUTS of the call; the worker job merges them (AC-2.2,
// AC-2.5).
#include <QString>
#include <QStringList>
#include <QtGlobal>

struct VdService;

namespace vedit::workspace::project {

// One finished expansion, as handed out by the C ABI.
struct ExpansionPayload {
    QStringList files;         // absolute: what the UI opens
    QStringList relativeFiles; // project-relative: what a generated `.f` carries
    QStringList includeDirs;
    QStringList defines;
    QStringList diagnostics;
    QStringList missingVariables; // ADDENDUM A-9: unresolved, unique, sorted
    int skipped = 0;              // lines that produced no entry (never silent)
};

class ProjectService {
public:
    enum class Start {
        Started,
        Rejected,
    };
    enum class Poll {
        Pending,
        Ready,
        Failed,
        Misuse,
    };

    ProjectService();
    ~ProjectService();
    ProjectService(const ProjectService &) = delete;
    ProjectService &operator=(const ProjectService &) = delete;

    // Starts an expansion of `projectRoot` using the imported `.f` files, the
    // project variable table (`NAME=VALUE`, ADDENDUM A-9) and the settings store
    // at `settingsPath` (empty => no tools-page values). The table wins over the
    // process environment. The call returns without waiting; the returned job
    // id is polled below.
    Start start(const QString &projectRoot, const QStringList &filelists,
                const QStringList &variables, const QString &settingsPath, quint64 *job);

    // Polls one job. On READY the payload is filled; on FAILED the reason is
    // available through lastError().
    Poll poll(quint64 job, ExpansionPayload *payload);

    // The reason of the last rejected call or failed job (empty when none).
    QString lastError() const;

    // Asks the service to discard a job that is no longer wanted.
    void cancel(quint64 job);

private:
    VdService *service_ = nullptr;
};

} // namespace vedit::workspace::project
