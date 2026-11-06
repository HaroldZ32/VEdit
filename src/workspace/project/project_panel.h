#pragma once

// The project panel widget (workspace lane, task T6).
//
// It renders exactly what `ProjectModel` holds and never parses a filelist
// itself (C1/C4): the root and member count, the members, the imported `.f`
// filelists (removable), the include dirs, the defines and the diagnostics of
// every bad `.f` entry (AC-2.2).
//
// Double click is the jump seam (AC-7.4/AC-7.7): a project file row opens that
// file through the process-wide `jump::IJumpTarget` registry, exactly like every
// other file-bearing panel, so this widget grows no file-opening code.
//
// The panel is inserted into the shell by `installProject()` as its own dock
// (`dock-project`). It is deliberately not registered in the shell's
// `PanelRegistry`, so the View menu has no project toggle yet; that is the
// declared cost of keeping this lane out of `src/app` (see README.md).
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

namespace vedit::workspace::project {

class FileStatusListDelegate;
class ProjectModel;

class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(ProjectModel *model, QWidget *parent = nullptr);

    ProjectModel *model() const;

    // Import path used by the button and by tests: validates through the model
    // and reports the failure in the status label (no modal dialog).
    bool importFilelists(const QStringList &paths);
    // Removes one imported `.f` (no dialog).
    bool removeFilelist(const QString &path);
    // Jumps to the project file at `row` (what a double click does, AC-7.4):
    // routed through the process-wide IJumpTarget registry.
    void activateFile(int row);

    // RM-9: the per-file status the Files list paints, in the ONE vocabulary of
    // src/common/file_status.h (0 unknown / 1 error / 2 warning / 3 clean /
    // 4 not analysed yet or excluded, drawn as a hollow grey ring). The default
    // source reads the diagnostics lane's published map from the TOP-LEVEL
    // window's shell properties, so this lane gains no link dependency and
    // project_integration.cpp stays untouched; a test injects a fake source.
    // The status is asked at PAINT time for the visible rows only, so a
    // 1176-file list is never walked per repaint.
    void setFileStatusSource(std::function<int(const QString &)> status,
                             std::function<quint64()> revision);
    /// What the Files list paints for `row` right now (read-only; tests and the
    /// evidence screenshot assert on it).
    int fileStatusAt(int row) const;

    // ADDENDUM A-9: the project variable table as shown, and the batch
    // collection. `promptForMissingVariables()` opens ONE dialog with a row per
    // unresolved variable of the last expansion; on accept the values are
    // written into the project table and the project re-expands. Cancel keeps
    // the import result (affected lines stay listed with file:line).
    QStringList displayedVariables() const;
    void setInteractiveDialogs(bool enabled);
    bool interactiveDialogs() const;
    bool promptForMissingVariables();
    bool editVariables();

    // RM-3: the ONE write path from the variables dialog to the model. The
    // dialog's table REPLACES the project table (that is what makes a deleted
    // variable actually disappear), is persisted per root, and re-expands.
    // Public so the regression drives add/delete without a modal dialog.
    bool applyVariableTable(const QStringList &values);
    // The dialog's row order: the known names, then the ones the project still
    // needs. The dialog always shows the whole table, so a replace is safe.
    QStringList variableRowNames() const;

    // Read-only views of what the widget actually shows; the headless
    // self-check asserts on these instead of trusting the model alone.
    QString summaryText() const;
    int fileCount() const;
    QStringList displayedFiles() const;
    QStringList displayedFilelists() const;
    QStringList displayedIncludeDirs() const;
    QStringList displayedDefines() const;
    QStringList displayedDiagnostics() const;
    // One-line failure/success note (empty when the last action succeeded).
    QString statusText() const;

public slots:
    void refresh();

private:
    void updateUi();
    void setStatus(const QString &text, bool error);
    void onImportRequested();
    void onRemoveRequested();
    void onFileActivated(int row);
    void onFilelistActivated(int row);
    void onEditVariablesRequested();
    void jumpToPath(const QString &path);

    ProjectModel *model_ = nullptr;
    QLabel *summary_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QPushButton *importButton_ = nullptr;
    QPushButton *removeButton_ = nullptr;
    QLabel *filelistsHeader_ = nullptr;
    QListWidget *filelists_ = nullptr;
    QLabel *filesHeader_ = nullptr;
    QListWidget *fileList_ = nullptr;
    QLabel *includeHeader_ = nullptr;
    QListWidget *includeDirs_ = nullptr;
    QLabel *definesHeader_ = nullptr;
    QListWidget *defines_ = nullptr;
    QLabel *variablesHeader_ = nullptr;
    QListWidget *variables_ = nullptr;
    QPushButton *editVariablesButton_ = nullptr;
    QLabel *diagnosticsHeader_ = nullptr;
    QListWidget *diagnostics_ = nullptr;
    QLabel *emptyHint_ = nullptr;
    QLabel *status_ = nullptr;
    FileStatusListDelegate *statusDelegate_ = nullptr;
    QTimer *statusTimer_ = nullptr;
    std::function<int(const QString &)> statusSource_;
    std::function<quint64()> statusRevisionSource_;
    quint64 statusRevision_ = 0;
    bool interactive_ = true;
};

} // namespace vedit::workspace::project
