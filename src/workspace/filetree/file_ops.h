#pragma once

// Filesystem operations behind the file tree's New / Rename / Delete actions
// (lane: workspace, T21; acceptance A2a: "after create/rename/delete the disk
// and the tree agree, and failures are diagnosable").
//
// The rules this module enforces, so that a *failed* operation can never leave
// a phantom row behind:
//   * every operation either succeeds on disk or returns ok=false with a
//     one-line `error` the panel can show verbatim;
//   * nothing is truncated, overwritten or replaced: creation uses NewOnly and
//     rename refuses an existing target;
//   * the caller re-reads the affected directory from disk afterwards, so the
//     model is never the source of truth for the tree's contents.

#include <QString>

namespace vedit::workspace {

// The operations live in their own namespace so the panel's members can keep
// the natural names (createFile/renameEntry) without shadowing them.
namespace fileops {

struct FileOpResult {
    bool ok = false;
    QString path;   // absolute path of the entry the operation addressed/created
    QString error;  // one-line, user-facing diagnostic; empty when ok
    QString detail; // low-level cause (QFile/QDir/system error string)

    explicit operator bool() const { return ok; }

    static FileOpResult success(const QString &path);
    static FileOpResult failure(const QString &path, const QString &error,
                                const QString &detail = QString());
};

// A name a single entry may carry: not empty, not "."/"..", no path separator,
// no NUL, no more than 255 bytes. On failure `error` carries the reason.
bool isPlainEntryName(const QString &name, QString *error);

FileOpResult createFile(const QString &directory, const QString &name);
FileOpResult createDirectory(const QString &directory, const QString &name);
FileOpResult renameEntry(const QString &path, const QString &newName);
FileOpResult removeEntry(const QString &path, bool recursive);

} // namespace fileops
} // namespace vedit::workspace
