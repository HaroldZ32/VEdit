#include "search_types.h"

#include <QFileInfo>
#include <QStringLiteral>

namespace vedit::workspace::search {

QStringList defaultExcludedDirNames()
{
    static const QStringList names = {
        QStringLiteral(".git"),        QStringLiteral(".svn"),      QStringLiteral(".hg"),
        QStringLiteral("build"),       QStringLiteral("target"),    QStringLiteral("node_modules"),
        QStringLiteral(".cache"),      QStringLiteral(".idea"),     QStringLiteral(".vscode"),
        QStringLiteral("CMakeFiles"),  QStringLiteral(".ccache"),   QStringLiteral(".venv"),
    };
    return names;
}

bool isVerilogFileName(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return suffix == QStringLiteral("v") || suffix == QStringLiteral("vh") ||
           suffix == QStringLiteral("sv") || suffix == QStringLiteral("svh");
}

QString searchStateName(SearchState state)
{
    switch (state) {
    case SearchState::Idle:
        return QStringLiteral("idle");
    case SearchState::Running:
        return QStringLiteral("running");
    case SearchState::Done:
        return QStringLiteral("done");
    case SearchState::Cancelled:
        return QStringLiteral("cancelled");
    case SearchState::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

} // namespace vedit::workspace::search
