#pragma once

// The project-root input seam (AC-2.1).
//
// The file tree panel announces the folder the user opened as
// `directoryOpened(QString)`. The project lane must react to it WITHOUT linking
// the file tree lane (every lane also builds standalone), so the binding is a
// name-based connection: every object under the shell that exposes that signal
// is connected to `ProjectModel::openRoot`. A shell without the file tree
// simply connects nothing — the project still works with `--project`.
#include <QObject>

namespace vedit::workspace::project {

class ProjectModel;

// Connects one object; true when it has the `directoryOpened(QString)` signal.
bool bindDirectoryOpened(QObject *source, ProjectModel *model);

// Connects `root` and every descendant that exposes the signal. Returns how many
// connections were made (0 when no panel announces directory opens).
int subscribeDirectoryOpened(QObject *root, ProjectModel *model);

} // namespace vedit::workspace::project
