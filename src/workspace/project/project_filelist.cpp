#include "project_filelist.h"

#include <QFileInfo>

namespace vedit::workspace::project {

OpenedFilelistPlan planOpenedFilelist(const QString &path, bool hasProject)
{
    OpenedFilelistPlan plan;
    if (path.trimmed().isEmpty()) {
        return plan;
    }
    const QFileInfo info(path);
    const QString suffix = info.suffix();
    // `.f` and `.F` are the classic spellings, `.flist` the long one; the
    // comparison is case-insensitive, exactly like vd-config's own
    // `has_f_extension` (crates/vd-config/src/project.rs).
    if (suffix.compare(QStringLiteral("f"), Qt::CaseInsensitive) != 0
        && suffix.compare(QStringLiteral("flist"), Qt::CaseInsensitive) != 0) {
        return plan;
    }
    plan.isFilelist = true;
    plan.absolutePath = info.absoluteFilePath();
    if (!hasProject) {
        plan.needsRoot = true;
        plan.root = info.absolutePath();
    }
    return plan;
}

} // namespace vedit::workspace::project
