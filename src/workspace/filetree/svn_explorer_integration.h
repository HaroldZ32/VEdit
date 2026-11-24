#pragma once

#include "file_tree_widget.h"

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

FileTreeWidget::ContextMenuExtension svnExplorerMenu(vedit::app::ApplicationShell &shell);

} // namespace vedit::workspace
