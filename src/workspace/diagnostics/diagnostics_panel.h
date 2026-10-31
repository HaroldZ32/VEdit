#pragma once

// Outline panel: the dock content the shell factory builds. It reads the
// controller's outline and state; it never starts or polls a backend itself.

#include <QWidget>

class QLabel;
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

    /// Copy the controller's outline + state into the widgets.
    void refreshFromController();

private:
    DiagnosticsController *controller_ = nullptr;
    OutlineModel *model_ = nullptr;
    QTreeView *view_ = nullptr;
    QLabel *status_ = nullptr;
};

/// Factory used by `installDiagnostics` for the shell's "outline" panel id.
QWidget *createOutlinePanel(DiagnosticsController *controller);

} // namespace vedit::workspace::diagnostics
