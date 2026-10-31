#pragma once

// The project panel widget (workspace lane, Slice 1 / task T6).
//
// It renders exactly what `ProjectModel` holds and never parses a filelist
// itself (C1/C4): the member count, the expanded list, the include dirs and
// defines it summarises, and the diagnostics of every bad `.f` entry.
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

namespace vedit::workspace::project {

class ProjectModel;

class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(ProjectModel *model, QWidget *parent = nullptr);

    ProjectModel *model() const;

    // Read-only views of what the widget actually shows; the headless
    // self-check asserts on these instead of trusting the model alone.
    QString summaryText() const;
    int fileCount() const;
    QStringList displayedFiles() const;
    QStringList displayedDiagnostics() const;

public slots:
    void refresh();

private:
    void updateUi();

    ProjectModel *model_ = nullptr;
    QLabel *summary_ = nullptr;
    QListWidget *fileList_ = nullptr;
    QLabel *diagnosticsHeader_ = nullptr;
    QListWidget *diagnostics_ = nullptr;
    QLabel *emptyHint_ = nullptr;
};

} // namespace vedit::workspace::project
