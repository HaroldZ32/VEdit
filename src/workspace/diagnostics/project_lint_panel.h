#pragma once

// batch-20261115 AC-4.2..4.5: the PROJECT LINT panel's CONTENT.
//
// The dock itself belongs to the editor lane (C1): `ApplicationShell` creates a
// bottom dock whose content host is `projectLintHost()`. This lane installs the
// content into that host - it never creates a dock - so the panel is persistent
// through `saveState/restoreState` without this lane knowing anything about
// docks.
//
// The panel is the ONLY surface for the whole-project pass (AC-4.3): the Outline
// dock next to the editor keeps the realtime (lower) diagnostics of the active
// document. Rows are grouped per file so a project-sized result stays readable,
// and a double click jumps through the one application-wide jump registry.
#include "diagnostics_types.h"

#include <QString>
#include <QVariant>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace::diagnostics {

class DiagnosticsController;

/// batch-20261116 L2.2: the ONE user-visible line for the project file-set
/// poller's state map (`vedit.diagnostics.projectFilesState`). The dock and the
/// regression both read this, so the persistent reason can never disagree with
/// the state that produced it. Empty for an unknown/absent map.
QString projectFileSetStateText(const QVariantMap &state);

class ProjectLintPanel final : public QWidget {
    Q_OBJECT
public:
    ProjectLintPanel(DiagnosticsController *controller, vedit::app::ApplicationShell *shell,
                     QWidget *parent = nullptr);

    /// Rows currently listed (files are group headers, not counted).
    int issueRowCount() const { return issueRowCount_; }
    /// Files with at least one row.
    int fileRowCount() const { return fileRowCount_; }
    /// The explicit run's entry point - the same `project.lint` action the menu
    /// uses, so one command and one request builder serve both paths.
    QPushButton *runButton() const { return run_; }
    /// The one-line state: counts plus the pass phase / progress.
    QString summaryText() const;

    QTreeWidget *tree() const { return tree_; }

private:
    void rebuild();
    void jumpToItem(QTreeWidgetItem *item);

    DiagnosticsController *controller_ = nullptr;
    vedit::app::ApplicationShell *shell_ = nullptr;
    QLabel *summary_ = nullptr;
    QLabel *empty_ = nullptr;
    QPushButton *run_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QTimer *timer_ = nullptr;
    quint64 lastSerial_ = 0;
    QString lastSignature_;
    int issueRowCount_ = 0;
    int fileRowCount_ = 0;
};

} // namespace vedit::workspace::diagnostics
