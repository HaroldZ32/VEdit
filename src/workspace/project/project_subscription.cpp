#include "project_subscription.h"

#include "project_model.h"

namespace vedit::workspace::project {

bool bindDirectoryOpened(QObject *source, ProjectModel *model)
{
    if (source == nullptr || model == nullptr) {
        return false;
    }
    // Name-based on purpose: the project lane must not include (or link) the
    // file tree lane, and a panel that does not have the signal must stay a
    // silent no-op instead of a build break.
    return QObject::connect(source, SIGNAL(directoryOpened(QString)), model,
                            SLOT(openRoot(QString)));
}

int subscribeDirectoryOpened(QObject *root, ProjectModel *model)
{
    if (root == nullptr || model == nullptr) {
        return 0;
    }
    int connected = bindDirectoryOpened(root, model) ? 1 : 0;
    const QObjectList children = root->children();
    for (QObject *child : children) {
        connected += subscribeDirectoryOpened(child, model);
    }
    return connected;
}

} // namespace vedit::workspace::project
