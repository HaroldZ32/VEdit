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

class QLabel;
class QListWidget;
class QPushButton;

namespace vedit::workspace::project {

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
    bool interactive_ = true;
};

} // namespace vedit::workspace::project
