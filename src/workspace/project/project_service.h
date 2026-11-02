#pragma once

// Client of the Rust project service over the C ABI (C2: non-blocking).
//
// The service lives in `crates/vd-ffi/src/project.rs`: `vd_project_parse` starts
// a job on a Rust worker thread and returns its id immediately, and
// `vd_project_poll` reports PENDING / READY / FAILED. This class is the only
// place the project lane touches the C ABI; the rest of the lane sees Qt types.
//
// The Qt side never parses a `.f` file itself (C1/C4): the file list, include
// dirs, defines and diagnostics all come from the service, and the panel only
// renders them.
#include <QString>
#include <QStringList>
#include <QtGlobal>

struct VdService;

namespace vedit::workspace::project {

// One finished expansion, as handed out by the C ABI.
struct ExpansionPayload {
    QStringList files;
    QStringList includeDirs;
    QStringList defines;
    QStringList diagnostics;
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

    // Starts an expansion of `projectRoot`; the call returns without waiting.
    Start start(const QString &projectRoot, quint64 *job);

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
