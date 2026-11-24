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
    //
    // batch-20261113 T4: `subscribeDirectoryOpened` walks the WHOLE object tree,
    // so most sources cannot possibly have the signal. Qt's string connect
    // prints "QObject::connect: No such signal <class>::directoryOpened(QString)"
    // for each of them (197 per product run, from ApplicationShell / QAction /
    // QMainWindowLayout / QWidget). Ask the meta-object first: this query is
    // silent, so the positive path (FileTreeWidget, FakeDirectoryOpener) still
    // binds and a missing signal is simply a no-op.
    if (source->metaObject()->indexOfSignal("directoryOpened(QString)") < 0) {
        return false;
    }
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
