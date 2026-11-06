#pragma once

// T8: the window-bottom project-lint progress strip.
//
// It owns no lint logic: it renders `DiagnosticsController::projectPass()` and
// calls `controller->refresh()` from its own QTimer (job + poll, C4), so the
// GUI thread never blocks. shell-integration mounts the widget (this file only
// provides the widget and its wires); a build without the shell still gets the
// widget, hidden until a pass exists.
//
// Cancel is a REAL cancel (AC-2.8): the controller kills the child, clears the
// running flag and a rerun starts from scratch. The strip disappears on cancel
// and shows `cancelled — N/M file(s)` (the zh catalogue renders
// `已取消 — N/M 文件`; or the failure tool+stage) until the next pass.
#include "diagnostics_types.h"

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace vedit::workspace::diagnostics {

class DiagnosticsController;

class LintProgressBar : public QWidget {
    Q_OBJECT
public:
    explicit LintProgressBar(QWidget *parent = nullptr);

    // Bind the controller (nullptr unbinds and hides the strip). The widget
    // does not own the controller.
    void setController(DiagnosticsController *controller);
    DiagnosticsController *controller() const { return controller_; }

    // Poll interval of the widget's own timer (tests use 5 ms).
    void setPollInterval(int milliseconds);
    int pollInterval() const;

    // Render the controller's current snapshot NOW (the timer calls this).
    void refreshFromController();

    // Read-only views for the headless self-check / evidence.
    QString statusText() const;
    QString progressText() const;
    bool running() const;
    bool cancelVisible() const;
    bool rerunEnabled() const;
    /// Why Re-run is unavailable (empty when it is available). A-15: the strip
    /// must explain a disabled button rather than let it click into nothing.
    QString rerunHint() const;
    bool stripVisible() const;
    int percent() const;

signals:
    void cancelRequested();
    void rerunRequested();

public slots:
    // Buttons/tests: cancel the running pass (no-op when idle).
    void requestCancel();
    // Buttons/tests: rerun the last request (AC-2.8).
    void requestRerun();

private:
    void applySnapshot(const LintPassSnapshot &snapshot);

    DiagnosticsController *controller_ = nullptr;
    QTimer *timer_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QPushButton *rerun_ = nullptr;
    LintPassSnapshot snapshot_;
    bool everStarted_ = false;
};

} // namespace vedit::workspace::diagnostics
