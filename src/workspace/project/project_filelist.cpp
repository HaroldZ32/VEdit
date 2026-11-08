#include "project_filelist.h"

#include "file_dialog_filters.h" // src/common: the ONE filelist extension set (RM-16)

#include <QFileInfo>

namespace vedit::workspace::project {

OpenedFilelistPlan planOpenedFilelist(const QString &path, bool hasProject)
{
    OpenedFilelistPlan plan;
    if (path.trimmed().isEmpty()) {
        return plan;
    }
    // RM-16: the filelist extension set has ONE definition
    // (src/common/file_dialog_filters.h), the same one the file dialogs offer;
    // it mirrors vd-config's own predicate (crates/vd-config/src/project.rs),
    // which accepts the `f` extension only, case-insensitively. The long
    // `.flist` spelling is NOT a filelist.
    if (!vedit::common::isFilelistPath(path)) {
        return plan;
    }
    const QFileInfo info(path);
    plan.isFilelist = true;
    plan.absolutePath = info.absoluteFilePath();
    if (!hasProject) {
        plan.needsRoot = true;
        plan.root = info.absolutePath();
    }
    return plan;
}

} // namespace vedit::workspace::project
