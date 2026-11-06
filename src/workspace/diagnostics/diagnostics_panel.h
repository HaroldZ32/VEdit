#pragma once

// Diagnostics panel: the dock content the shell factory builds. It reads the
// controller's outline and diagnostics; it never starts or polls a backend
// itself.
//
// T9 (AC-6.1..6.4, AC-7.1/7.2):
//   * diagnostics are listed in severity groups - Errors and Warnings, each with
//     its own count and the editor's own palette (diagnosticsSeverityColor), plus
//     an Information group for everything that is neither;
//   * rows are sorted by file:line and double-click jumps through the one
//     application-wide jump registry (vedit::workspace::jump::jumpTo);
//   * outline rows double-click through the SAME registry, to the symbol's
//     file:line:column;
//   * no diagnostics => an explicit empty state that says what to do next.

#include <QColor>
#include <QString>
#include <QWidget>

class QLabel;
class QModelIndex;
class QTimer;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;

namespace vedit::workspace::diagnostics {

class DiagnosticsController;
class OutlineModel;

class DiagnosticsPanel final : public QWidget {
    Q_OBJECT
public:
    /// Item-data role holding a diagnostic row's severity; group headers carry
    /// -1. The self-check reads it to prove a severity never lands in the wrong
    /// group (AC-6.2).
    static constexpr int IssueSeverityRole = Qt::UserRole + 3;

    explicit DiagnosticsPanel(DiagnosticsController *controller, QWidget *parent = nullptr);

    OutlineModel *model() const { return model_; }
    QString statusText() const;
    /// Number of rows in the outline table (0 when there is nothing to show).
    int outlineRowCount() const;
    /// Diagnostic leaf rows across all groups (group headers are not counted).
    int issueRowCount() const;
    /// Per-severity counts: errors = severity 1, warnings = severity 2,
    /// information = everything else (0/3/4).
    int errorCount() const;
    int warningCount() const;
    int informationCount() const;
    /// "Errors: 1 · Warnings: 2 · Information: 0" - always computed, even zero.
    QString countsText() const;
    /// The palette shared with the editor's inline squiggle (AC-6.2).
    QColor errorColor() const;
    QColor warningColor() const;
    /// Severity -> group: 1 errors, 2 warnings, 3 information/other, 0 none.
    static int groupForSeverity(int severity);

    /// The one-line empty-state reason shown below the outline (empty when hidden).
    QString emptyStateText() const;
    bool emptyStateVisible() const;
    /// The diagnostics section's empty state (AC-6.4 / AC-10.3.3): what to do
    /// next when there is nothing to list (empty when hidden).
    QString diagnosticsEmptyStateText() const;
    bool diagnosticsEmptyStateVisible() const;

    /// The controller this panel renders (the self-test reads its entry counts).
    DiagnosticsController *controller() const;

    /// The widgets a check drives (mouse double click / geometry); the panel
    /// owns them.
    QWidget *outlineWidget() const;
    QWidget *issuesWidget() const;
    QWidget *countsWidget() const;

    /// Copy the controller's outline + state + diagnostics into the widgets.
    void refreshFromController();

private:
    /// T118: the panel owns the polling loop, so an asynchronous analysis result
    /// always reaches the widgets without the user touching anything.
    void poll();
    void rebuildDiagnostics();
    void jumpToOutline(const QModelIndex &index);
    void jumpToIssue(QTreeWidgetItem *item);
    /// 0-based line/column -> the 1-based position of the jump contract.
    void jumpTo(const QString &filePath, int zeroBasedLine, int zeroBasedColumn);

    DiagnosticsController *controller_ = nullptr;
    QTimer *pollTimer_ = nullptr;
    QTreeWidget *issues_ = nullptr;
    QLabel *counts_ = nullptr;
    QLabel *diagnosticsEmpty_ = nullptr;
    OutlineModel *model_ = nullptr;
    QTreeView *view_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *emptyState_ = nullptr;
    int issueRowCount_ = 0;
    int errorCount_ = 0;
    int warningCount_ = 0;
    int informationCount_ = 0;
    QString countsText_;
};

/// Factory used by `installDiagnostics` for the shell's "outline" panel id.
QWidget *createOutlinePanel(DiagnosticsController *controller);

} // namespace vedit::workspace::diagnostics
