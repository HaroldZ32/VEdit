#pragma once

// The project panel's state machine: restores the persisted project, asks the
// Rust service to expand it, and publishes the result to the panel.
//
// Threading: everything here runs on the GUI thread. The expansion itself runs
// on the Rust worker thread (C2); `pollTimer_` polls it every few milliseconds
// and never blocks the event loop.
//
// Restart behaviour (AC-S1.3): `restoreFromDisk()` loads `<state dir>/project-model.json`
// so the project and its members are listed immediately on the next start, even
// before (or without) the service answering, and a refresh replaces them with
// the live expansion.
#include "project_service.h"
#include "project_state.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace vedit::workspace::project {

class ProjectModel : public QObject {
    Q_OBJECT
public:
    explicit ProjectModel(QString stateDirectory, QObject *parent = nullptr);
    ~ProjectModel() override;

    // Loads the persisted project (root + members). Returns true when one was
    // restored; `error` receives why not (empty for "nothing saved yet").
    bool restoreFromDisk(QString *error = nullptr);

    // Opens `projectRoot` (switching projects clears the previous members) and
    // starts an asynchronous expansion.
    void open(const QString &projectRoot);

    // Re-expands the current root; no-op with a failed signal when none is open.
    void refresh();

    bool hasProject() const;
    bool isPending() const;
    bool restoredFromDisk() const;
    QString projectRoot() const;

    // Absolute paths exactly as the Rust service returned them.
    QStringList files() const;
    // The same paths relative to the project root when they are inside it.
    QStringList displayFiles() const;
    QStringList includeDirs() const;
    QStringList defines() const;
    // Service diagnostics plus any state-store/service failure, phrased for the UI.
    QStringList diagnostics() const;

    // One line the panel shows first, e.g.
    // `Project: verilog-project — 7 files (live)`.
    QString summary() const;
    QString statePath() const;
    QString lastError() const;

signals:
    void stateChanged();
    // Emitted when an expansion attempt settles: `ok` false carries the reason.
    void refreshFinished(bool ok, const QString &error);

private:
    void startExpansion();
    void pollOnce();
    void adopt(const ExpansionPayload &payload);
    void recordFailure(const QString &error);

    ProjectService service_;
    ProjectStateStore store_;
    QTimer pollTimer_;
    QElapsedTimer deadline_;
    ProjectState state_;
    QString lastError_;
    QString stateError_;
    quint64 job_ = 0;
    bool pending_ = false;
    bool restored_ = false;
};

} // namespace vedit::workspace::project
