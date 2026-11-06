#pragma once

// R3 (ux-batch-2, BINDING): the save-triggered project lint.
//
// The editor's ONE save seam (EditorPane::saveTab / saveAsFile, forwarded by
// EditorArea::setOnDocumentSaved) reports every SUCCESSFUL write here. This
// class owns the three properties the ruling demands, in one place:
//
//   * debounce  - 700 ms after the last save; a burst of saves is ONE pass,
//   * non-blocking - noteSaved() only restarts a timer, so a save is never
//                    delayed or postponed by the lint,
//   * single-flight - a pass still running when the debounce fires is
//                    CANCELLED and the newest one started; nothing is queued.
//
// An automatic pass is marked on the controller before it starts
// (setNextPassAutomatic), which is how the UI keeps it silent: results go to
// the editor, the panel, the file-tree dots and the status bar, never to a
// modal dialog (item 12/R3).
//
// The class is widget-free and the request/open-file sources are injected, so
// the headless self-test drives exactly the production policy with a fake lint
// backend.

#include "diagnostics_controller.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

namespace vedit::workspace::diagnostics {

class ProjectLintTrigger final : public QObject {
    Q_OBJECT
public:
    using RequestBuilder = std::function<LintPassRequest()>;
    using OpenFilesProvider = std::function<QStringList()>;
    using ResultSink = std::function<void(bool started, const QString &error)>;

    explicit ProjectLintTrigger(DiagnosticsController *controller, QObject *parent = nullptr);

    void setRequestBuilder(RequestBuilder builder);
    void setOpenFilesProvider(OpenFilesProvider provider);
    void setResultSink(ResultSink sink);
    /// The debounce window (default 700 ms, the ruling's value). Tests shorten
    /// it; the production wiring never calls this.
    void setDebounceInterval(int milliseconds);
    int debounceInterval() const;

    /// A document reached disk. Never starts work synchronously.
    void noteSaved(const QString &file);
    /// Fire the pending debounce now (deterministic tests). No-op when idle.
    void flushPending();

    quint64 saveCount() const { return saveCount_; }
    /// Passes this trigger actually STARTED (the acceptance's "one pass").
    quint64 passStarts() const { return passStarts_; }
    /// Running passes cancelled to make room for a newer one (single-flight).
    quint64 cancelledRunning() const { return cancelledRunning_; }
    bool pending() const;
    QString lastSavedFile() const { return lastSavedFile_; }

private:
    void fire();

    DiagnosticsController *controller_ = nullptr;
    QTimer *debounce_ = nullptr;
    RequestBuilder requestBuilder_;
    OpenFilesProvider openFiles_;
    ResultSink sink_;
    quint64 saveCount_ = 0;
    quint64 passStarts_ = 0;
    quint64 cancelledRunning_ = 0;
    QString lastSavedFile_;
};

} // namespace vedit::workspace::diagnostics
