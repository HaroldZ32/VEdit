#pragma once

// Registration entry point for the shell's panel seam (PanelRegistry id
// "file-tree"). It lives in the workspace lane's own directory so the panel's
// wiring stays inside the lane's write scope: the integration layer only has to
// call this factory, and the lane can change the widget's internals freely.

#include <QString>

class QWidget;

namespace vedit::workspace {

// `projectRoot` is the folder the shell was started with (may be empty, which
// leaves the panel in its "no folder open" empty state).
QWidget *makeFileTreePanel(const QString &projectRoot = QString(), QWidget *parent = nullptr);

} // namespace vedit::workspace
