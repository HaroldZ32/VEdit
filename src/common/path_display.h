#pragma once

// The ONE path-display rule of the product (user ruling, 2026-11-04):
//
//   * a project root is set AND the path is inside it -> display the path
//     relative to the root, without a leading "./";
//   * otherwise -> display the absolute path.
//
// Every persistent surface that RENDERS a path (the editor tab tooltip and the
// window title, the Files tree tooltip, the project panel lists, the command
// palette and the search results) calls displayPath(); no surface grows its own
// prefix test. One-shot status-bar / panel confirmations and error notes
// ("Opened ...", "Removed ...") deliberately keep the absolute path and are
// outside this rule (docs/USER_GUIDE.md names them). What a surface OPENS is
// untouched: absolute paths stay the open key and the dedup key.
//
// Header-only and QtCore-only on purpose: a workspace lane can include it in a
// standalone build (`cmake -S src/workspace/<lane>`) without linking src/app,
// and src/app does not have to know a lane.
//
// `tests`: the app self-check asserts the three cases (under root, outside root,
// no root) directly, and the project lane asserts them on its displayed lists.
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace vedit::paths {

// The stable spelling of a root: absolute and free of "." / ".." segments, so
// the prefix comparison below cannot fail because of a trailing slash or a
// relative spelling. An empty root stays empty ("no project").
//
// Symlinks are deliberately NOT resolved: the file paths the project service
// returns are spelled under the root the caller passed, so resolving only one
// side of the comparison would turn every under-root file into an absolute one.
inline QString normalizedRoot(const QString &root)
{
    const QString trimmed = root.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

// The display spelling of `path` under `root` (see the rule above). The path is
// normalised the same way as the root, so the comparison is stable for a root
// with a trailing slash and for "." / ".." segments.
inline QString displayPath(const QString &root, const QString &path)
{
    if (path.isEmpty()) {
        return QString();
    }
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString normalized = normalizedRoot(root);
    if (normalized.isEmpty()) {
        return absolute;
    }
    // The root itself is not "inside" the root: showing a bare "." would hide
    // which folder the user actually opened.
    if (absolute == normalized) {
        return absolute;
    }
    const QString prefix = normalized.endsWith(QLatin1Char('/'))
                               ? normalized
                               : normalized + QLatin1Char('/');
    if (absolute.startsWith(prefix)) {
        return absolute.mid(prefix.size());
    }
    // A sibling whose name merely starts with the root's name ("/a/bc" next to
    // "/a/b") is outside: the separator above is what makes that safe.
    return absolute;
}

} // namespace vedit::paths
