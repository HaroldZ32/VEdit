#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <utility>

namespace vedit::workspace::diagnostics {

// Instance identifier positions in zero-based lines and UTF-16 columns.
using LiteralModuleReferences = QHash<QString, QSet<std::pair<int, int>>>;
LiteralModuleReferences literalModuleReferences(const QString &text);

} // namespace vedit::workspace::diagnostics
