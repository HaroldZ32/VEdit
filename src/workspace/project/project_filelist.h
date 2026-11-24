#pragma once

// G1-a: OPENING a `.f` / `.F` filelist NEVER imports it. The project file set is
// widened only by an EXPLICIT import (`Project ▸ Import Filelist…` — the Project
// panel's button — or the "Import" answer to the one question the open seam
// asks). `planOpenedFilelist` therefore describes what an explicit "Import"
// would do; `needsRoot`/`root` are meaningful on that path only.
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
//   * no project open  -> on an explicit "Import", the filelist's own directory
//     becomes the root first (RootSource::Filelist in
//     crates/vd-config/src/project.rs), then the list is imported;
//   * project open     -> on an explicit "Import", the list is imported into the
//     CURRENT root; re-opening the root would clear the filelists already
//     imported;
//   * a project that ALREADY has a filelist imports nothing: the question is not
//     asked and the document only opens as text. The default answer ("Open
//     Only") never imports in the other two cases either.
//
// The parse is never re-implemented: ProjectModel::importFilelists() drives the
// Rust parser (crates/vd-config/src/filelist.rs) and its diagnostics.
#include <QString>

namespace vedit::workspace::project {

// What the project lane should do with an opened document path.
struct OpenedFilelistPlan {
    // The document is a filelist (extension .f/.F, case-insensitive).
    bool isFilelist = false;
    // No project is open: the explicit `Import` answer must set `root` first.
    bool needsRoot = false;
    // The filelist's directory (only meaningful when needsRoot, i.e. on the
    // explicit `Import` path).
    QString root;
    // The absolute spelling of the document (the import key).
    QString absolutePath;
};

// Decides from the document path and whether a project is currently open.
OpenedFilelistPlan planOpenedFilelist(const QString &path, bool hasProject);

} // namespace vedit::workspace::project
