#pragma once

// RM-17: the ONE derived scope, two consumers.
//
// The module tree runs the catalog. With a named TOP that run also yields the
// file set reachable from the top (the Rust `ModuleCatalog.reachable_files`,
// derived from the SAME elaboration that produced the tree). The lint pass and
// the status dots must use THAT set, not a second derivation — otherwise the
// tree could say "12 files outside the top" while the lint analysed 11 or 13.
//
// The two lanes are installed by `src/workspace/integration.cpp` into one
// process but are separate libraries with no link edge between them (adding one
// would create a cycle: the integration target owns both). This header is the
// deliberate seam, header-only so neither lane gains a dependency:
//   * the diagnostics lane installs the sink at install time;
//   * the module tree lane calls `publishTopScope()` whenever a catalog finishes
//     (and with an empty top when the user returns to Auto, which clears the
//     scope and clears the "not analysed" reasons this seam set).
#include <QString>
#include <QStringList>

#include <functional>

namespace vedit::workspace {

/// `top` empty = Auto (no exclusion); `excluded` = project files that are in
/// the project set but not reachable from `top`.
using TopScopeSink =
    std::function<void(const QString &top, const QStringList &analysed,
                       const QStringList &excluded)>;

/// The sink slot. Empty until the diagnostics lane is installed.
inline TopScopeSink &topScopeSink()
{
    static TopScopeSink sink;
    return sink;
}

/// Publish one derived scope. No-op when the diagnostics lane is absent (the
/// module-tree selftests run without it).
inline void publishTopScope(const QString &top, const QStringList &analysed,
                            const QStringList &excluded)
{
    if (topScopeSink()) {
        topScopeSink()(top, analysed, excluded);
    }
}

} // namespace vedit::workspace
