#include "filetree_panel.h"

#include "file_tree_widget.h"

namespace vedit::workspace {

QWidget *makeFileTreePanel(const QString &projectRoot, QWidget *parent)
{
    auto *panel = new FileTreeWidget(parent);
    if (!projectRoot.isEmpty()) {
        panel->openDirectory(projectRoot);
    }
    return panel;
}

} // namespace vedit::workspace
