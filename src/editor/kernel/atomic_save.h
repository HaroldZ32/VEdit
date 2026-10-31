#pragma once

// Atomic save: write a sibling temporary file, fsync it, rename it over the
// target, then fsync the directory (probe constraint 3 / A1e).
//
// Order (same filesystem, so the rename is atomic):
//   open(<target>.vedit-tmp-*) -> write all -> fsync(file) -> close
//   -> rename(tmp, target) -> open(dir) -> fsync(dir)
//
// - Any failure BEFORE the rename leaves the target untouched and removes the
//   temporary file.
// - `fsync(dir)` returning EINVAL/ENOTSUP is a warning, not a failure: the
//   rename already happened and the new bytes are in place (NFS/FUSE return
//   that errno). Any other errno is reported as a save failure.
// - The temporary file keeps the mode of the existing target (0600 for a new
//   file) so a save never widens or narrows permissions unexpectedly.
// - Tests inject failures with VEDIT_ATOMIC_FAULT=write|fsync|rename|fsync-dir
//   (never set in production).
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace vedit::editor {

struct AtomicSaveResult {
    bool ok = false;
    QString error;
    QStringList diagnostics; // non-fatal notes (e.g. a tolerated directory fsync errno)
};

AtomicSaveResult atomicSaveText(const QString &targetPath, const QByteArray &contents);

} // namespace vedit::editor
