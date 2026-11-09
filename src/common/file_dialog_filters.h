#pragma once

// RM-16: ONE source of truth for the file-dialog filter tables and for the
// filelist extension set.
//
// WHY THIS HEADER EXISTS
// The product's dialogs used to spell the filelist globs themselves
// (`*.f *.F *.flist` in the open dialog and in the import dialog), while the
// project code that actually decides "is this a filelist manifest?" accepts the
// `f` extension only, case-insensitively:
//
//   * crates/vd-config/src/project.rs  — the discovery predicate:
//       `.is_some_and(|extension| extension.eq_ignore_ascii_case("f"))`
//   * crates/vd-config/src/filelist.rs — the parser is CONTENT-based (any path
//       handed to it is parsed; imported lists are read by content, which is why
//       "All files" stays available for an unusual spelling).
//
// So `.flist` was a UI GUESS that no producer/consumer of filelists recognises:
// a filter must not advertise it. Conversely `.F` must be listed even though the
// parser is case-insensitive, because QFileSystemModel matches globs
// case-SENSITIVELY on Linux — that is the one place the upper-case spelling is
// decided, here.
//
// The extensions are NOT sources: RM-11 keeps `.f` out of the analysis/lint
// file sets. This header is about DIALOG FILTERS only; do not reuse it to decide
// what gets parsed.

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QString>
#include <QStringList>

namespace vedit::common {

/// The filelist manifest extensions the project RECOGNISES (lower case). The
/// single definition; crates/vd-config/src/project.rs is the fact it mirrors.
inline QStringList filelistExtensions()
{
    return QStringList{QStringLiteral("f")};
}

/// The globs a file picker must offer for a filelist. Derived from the
/// recognised extensions; the upper-case spelling is listed because glob
/// matching on a case-sensitive filesystem is not the parser's
/// case-insensitive comparison.
inline QStringList filelistGlobs()
{
    return QStringList{QStringLiteral("*.f"), QStringLiteral("*.F")};
}

/// The space-separated glob pattern used inside a filter string.
inline QString filelistGlobPattern()
{
    return filelistGlobs().join(QLatin1Char(' '));
}

/// True for a path the project would recognise as a filelist manifest
/// (extension `f`, case-insensitive). Mirrors project.rs; kept here so no dialog
/// re-derives it by hand.
inline bool isFilelistPath(const QString &path)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot == path.size() - 1) {
        return false;
    }
    return path.mid(dot + 1).compare(QStringLiteral("f"), Qt::CaseInsensitive) == 0;
}

/// The literal table of the File -> Open dialog, in display order; the FIRST
/// entry is the one the dialog selects by default.
inline QStringList openFileDialogFilters()
{
    const QString globs = filelistGlobPattern();
    return {
        QCoreApplication::translate(
            "VEdit", "Verilog/SystemVerilog/filelist (%1 *.v *.sv *.svh *.vh)").arg(globs),
        QCoreApplication::translate("VEdit", "Verilog/SystemVerilog (*.v *.sv *.svh *.vh)"),
        QCoreApplication::translate("VEdit", "Filelists (%1)").arg(globs),
        QCoreApplication::translate("VEdit", "All Files (*)"),
    };
}

/// The import-filelist dialog's filelist entry. It is ALSO the first entry, and
/// configureImportFilelistDialog() selects it explicitly, so a dialog opened
/// specifically to import a filelist can never hide filelists.
inline QString importFilelistFilter()
{
    return QCoreApplication::translate("VEdit", "Verilog filelists (%1)")
        .arg(filelistGlobPattern());
}

/// The import-filelist dialog's whole table (filelist entry first).
inline QStringList importFilelistDialogFilters()
{
    return {importFilelistFilter(), QCoreApplication::translate("VEdit", "All files (*)")};
}

/// The glob list of one filter entry (the text inside the LAST parentheses).
inline QStringList filterGlobs(const QString &filter)
{
    const int open = filter.indexOf(QLatin1Char('('));
    const int close = filter.lastIndexOf(QLatin1Char(')'));
    if (open < 0 || close <= open) {
        return {};
    }
    return filter.mid(open + 1, close - open - 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

/// RM-16b: make the SELECTED filter the filter the file view ACTUALLY uses at
/// creation - without waiting for the user to open the type combo once more.
///
/// Qt normally applies the selected entry when it is set, and the shipped Qt
/// (6.10.2, no platform-theme plugin in the AppDir) does so in every probe we
/// ran. The user still reported "the default filter only works after one extra
/// click", so the product must not rely on that internal step: fanning the
/// selected entry's globs into the dialog's own file-system model makes the
/// initial state deterministic on every Qt/platform combination, including a
/// native dialog that populates its view before the filter is applied. The
/// dialog's own combo handler keeps the model in sync for later changes.
inline void applySelectedFilterToModel(QFileDialog &dialog)
{
    auto *model = dialog.findChild<QFileSystemModel *>();
    if (model == nullptr) {
        return;
    }
    const QStringList globs = filterGlobs(dialog.selectedNameFilter());
    if (globs.isEmpty()) {
        return;
    }
    // A filter HIDES non-matching entries; the Qt default only greys them.
    model->setNameFilterDisables(false);
    model->setNameFilters(globs);
}

/// Configure a QFileDialog as the import-filelist dialog: multi-select, the
/// FILELIST filter selected by default, and that selection applied to the view
/// BEFORE the dialog is shown. The production panel and the regression call this
/// same function, so "the default is the filelist filter, in effect" is one fact.
inline void configureImportFilelistDialog(QFileDialog &dialog)
{
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilters(importFilelistDialogFilters());
    dialog.selectNameFilter(importFilelistFilter());
    applySelectedFilterToModel(dialog);
}

/// Configure a QFileDialog as the File -> Open dialog: the historical first
/// entry is the default, selected explicitly AND applied to the view before it
/// is shown (same reason as above). The open-file list keeps its order.
inline void configureOpenFileDialog(QFileDialog &dialog)
{
    dialog.setNameFilters(openFileDialogFilters());
    const QStringList filters = openFileDialogFilters();
    if (!filters.isEmpty()) {
        dialog.selectNameFilter(filters.first());
    }
    applySelectedFilterToModel(dialog);
}

// V12: ONE switch that keeps every file picker on Qt's OWN widget dialog.
//
// Why this is not optional: QFileDialog falls back to the platform chooser when
// the running desktop supplies one (portal / GTK platform theme). A native
// dialog is painted by the desktop, so neither this application's stylesheet
// nor its palette reach its context menu — a bug there cannot be fixed in QSS.
// Forcing Qt's own dialog makes the application's styling authoritative and
// makes the menu visible to the menu diagnostics (ApplicationShell event
// filter), which is how the reported black context menu is located.
//
// Qt has no application-wide equivalent of this option, so every creation point
// goes through these helpers instead of spelling the flag again.

/// Force `dialog` to be Qt's widget dialog rather than a native/portal one.
inline void useOwnFileDialog(QFileDialog &dialog)
{
    dialog.setOption(QFileDialog::DontUseNativeDialog);
}

/// getExistingDirectory() with the native fallback disabled.
inline QString chooseExistingDirectory(QWidget *parent, const QString &caption,
                                       const QString &directory)
{
    return QFileDialog::getExistingDirectory(parent, caption, directory,
                                             QFileDialog::ShowDirsOnly
                                                 | QFileDialog::DontUseNativeDialog);
}

/// getSaveFileName() with the native fallback disabled.
inline QString chooseSaveFileName(QWidget *parent, const QString &caption,
                                  const QString &directory, const QString &filter = QString())
{
    return QFileDialog::getSaveFileName(parent, caption, directory, filter, nullptr,
                                        QFileDialog::DontUseNativeDialog);
}

} // namespace vedit::common
