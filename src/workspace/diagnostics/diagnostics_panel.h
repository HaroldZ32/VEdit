#pragma once

// Outline panel: the dock content the shell factory builds. It reads the
// controller's outline and state; it never starts or polls a backend itself.

#include <QWidget>

class QLabel;
class QTimer;
class QTreeView;

namespace vedit::workspace::diagnostics {

class DiagnosticsController;
class OutlineModel;

class DiagnosticsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPanel(DiagnosticsController *controller, QWidget *parent = nullptr);

    OutlineModel *model() const { return model_; }
    QString statusText() const;
    /// Number of rows in the outline table (0 when there is nothing to show).
    int outlineRowCount() const;
    /// The one-line empty-state reason shown below the outline (empty when hidden).
    QString emptyStateText() const;
    bool emptyStateVisible() const;
    /// The controller this panel renders (the self-test reads its entry counts).
    DiagnosticsController *controller() const;

    /// Copy the controller's outline + state into the widgets.
    void refreshFromController();

private:
    /// T118: the panel owns the polling loop, so an asynchronous analysis result
    /// always reaches the widgets without the user touching anything.
    void poll();

    DiagnosticsController *controller_ = nullptr;
    QTimer *pollTimer_ = nullptr;
    OutlineModel *model_ = nullptr;
    QTreeView *view_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *emptyState_ = nullptr;
};

/// Factory used by `installDiagnostics` for the shell's "outline" panel id.
QWidget *createOutlinePanel(DiagnosticsController *controller);

} // namespace vedit::workspace::diagnostics
