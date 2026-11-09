#pragma once

// FIX 2: opening a `.f` / `.F` filelist imports it as the project.
//
// (`.flist` is not a filelist extension: only `f`, case-insensitive, decided by
// the parser — crates/vd-config/src/project.rs. The set has one definition,
// src/common/file_dialog_filters.h.)
//
// The app layer only says "this document was opened" (one lane-agnostic
// ApplicationShell::fileOpened signal). THIS lane decides what an extension
// means, which keeps the direction src/app -> no project lane intact. The
// decision is a pure function here (no src/app, no Qt widgets) so a lane-only
// build can test it:
//
//   * no project open  -> the filelist's own directory becomes the root first
//     (RootSource::Filelist in crates/vd-config/src/project.rs), then import;
//   * project open     -> import into the CURRENT root; re-opening the root
//     would clear the filelists already imported.
//
// The parse is never re-implemented: ProjectModel::importFilelists() drives the
// Rust parser (crates/vd-config/src/filelist.rs) and its diagnostics.
#include <QString>

namespace vedit::workspace::project {

// What the project lane should do with an opened document path.
struct OpenedFilelistPlan {
    // The document is a filelist (extension .f/.F, case-insensitive).
    bool isFilelist = false;
    // No project is open, so `root` must be set before importing.
    bool needsRoot = false;
    // The filelist's directory (only meaningful when needsRoot).
    QString root;
    // The absolute spelling of the document (the import key).
    QString absolutePath;
};

// Decides from the document path and whether a project is currently open.
OpenedFilelistPlan planOpenedFilelist(const QString &path, bool hasProject);

} // namespace vedit::workspace::project
