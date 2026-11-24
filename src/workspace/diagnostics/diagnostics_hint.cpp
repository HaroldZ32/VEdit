#include "diagnostics_hint.h"

#include <QCoreApplication>

namespace vedit::workspace::diagnostics {

namespace {

// Verible's own wording. Both fragments must be present: the row is
// `preprocessing error at "<MACRO>" Error expanding macro identifier, might not
// be defined before.` and neither fragment alone identifies it (a design's own
// message could mention either word).
constexpr char kPreprocessMarker[] = "preprocessing error at";
constexpr char kMacroMarker[] = "might not be defined before";

} // namespace

QString veriblePreprocessHint(const QString &backendName, const QVector<DiagnosticEntry> &rows)
{
    // slang-server expands `include / `define macros, so the hint is false for
    // it - and for any backend that is not the pinned Verible server. The
    // backend that PRODUCED the rows decides, never the requested setting.
    if (backendName != QStringLiteral("verible")) {
        return QString();
    }
    for (const DiagnosticEntry &row : rows) {
        if (row.message.contains(QLatin1String(kPreprocessMarker))
            && row.message.contains(QLatin1String(kMacroMarker))) {
            return QCoreApplication::translate(
                "VEdit",
                "Verible stopped at a macro preprocessing error — the real syntax error (for example a missing semicolon) is hidden. Switch the LSP backend to slang in Preferences → Tools to see it.");
        }
    }
    return QString();
}

} // namespace vedit::workspace::diagnostics
