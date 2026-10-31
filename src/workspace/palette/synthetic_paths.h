#pragma once

// Deterministic synthetic file list used by the palette selftest and the 10k
// benchmark. Keeping it in one header means both measure exactly the same
// candidate set, and the numbers are reproducible without touching the disk.
#include <QString>
#include <QStringList>

namespace vedit::workspace {

inline QStringList makeSyntheticPaths(int count)
{
    static const char *const directories[] = {
        "src/app",       "src/workspace", "src/editor",   "src/syntax",
        "src/tools",     "tests/unit",    "docs",         "tools/scripts",
    };
    static const char *const stems[] = {
        "command_palette", "file_index",    "fuzzy_matcher", "main",
        "widget",          "parser",        "document",      "provider",
        "palette_view",    "settings",      "terminal",      "outline",
        "vcs",             "search",        "tree",          "benchmark",
    };
    static const char *const extensions[] = {".cpp", ".h", ".md", ".v", ".sv", ".txt"};

    constexpr int directoryCount = static_cast<int>(sizeof(directories) / sizeof(directories[0]));
    constexpr int stemCount = static_cast<int>(sizeof(stems) / sizeof(stems[0]));
    constexpr int extensionCount = static_cast<int>(sizeof(extensions) / sizeof(extensions[0]));

    QStringList paths;
    paths.reserve(count);
    for (int index = 0; index < count; ++index) {
        const QString directory = QString::fromLatin1(directories[index % directoryCount]);
        const QString stem = QString::fromLatin1(stems[index % stemCount]);
        const QString extension = QString::fromLatin1(extensions[index % extensionCount]);
        paths.append(QStringLiteral("%1/%2_%3%4")
                         .arg(directory, stem)
                         .arg(index, 5, 10, QLatin1Char('0'))
                         .arg(extension));
    }
    return paths;
}

} // namespace vedit::workspace
