// RM-16 — the file-dialog filters are DATA and must agree with the parser.
//
// The user reported that the file-type filter named “Verilog/SV/Filelist” hid
// `.f` files while a separate “Filelists” filter showed them, and that the
// import-filelist dialog did not show filelists at all. This binary takes the
// product's filter tables as data and asserts:
//
//   * every filter whose NAME claims filelists actually offers the filelist
//     globs — and offers EXACTLY the set the project recognises (no guessed
//     `.flist`),
//   * the import-filelist dialog's SELECTED filter is the filelist one,
//   * with that selected filter, a REAL QFileSystemModel (the engine the Qt file
//     dialog uses) shows `lib.f` and hides `a.sv` in a directory that has both.
//
// The recognised set is `f` (case-insensitive) per
// crates/vd-config/src/project.rs; see src/common/file_dialog_filters.h.

#include "app_shell.h" // the PRODUCT open-file filter (single testable function)
#include "file_dialog_filters.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>
#include <functional>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : "  ", detail.isEmpty() ? "" : detail.toUtf8().constData());
}

/// The glob list inside one name-filter entry: the text in the LAST parentheses.
QStringList globsOf(const QString &filter)
{
    const int open = filter.indexOf(QLatin1Char('('));
    const int close = filter.lastIndexOf(QLatin1Char(')'));
    if (open < 0 || close <= open) {
        return {};
    }
    return filter.mid(open + 1, close - open - 1)
        .split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QString displayNameOf(const QString &filter)
{
    return filter.section(QLatin1Char('('), 0, 0).trimmed();
}

/// The filter entries of a whole dialog table (the product strings are joined
/// with ';;', the same separator Qt uses).
QStringList entriesOf(const QString &joined)
{
    return joined.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
}

/// Does this filter's DISPLAY NAME claim to cover filelists? (English source and
/// the zh spellings the catalogue uses; the test normally runs untranslated.)
bool claimsFilelists(const QString &filter)
{
    const QString name = displayNameOf(filter).toLower();
    return name.contains(QStringLiteral("filelist")) || name.contains(QStringLiteral("文件清单"))
        || name.contains(QStringLiteral("文件列表"));
}

/// The filelist-looking globs of a filter (`*.f` / `*.F`, i.e. the suffix is
/// `f` case-insensitively), sorted for a set comparison.
QStringList filelistGlobsOf(const QString &filter)
{
    QStringList globs;
    for (const QString &glob : globsOf(filter)) {
        const int dot = glob.lastIndexOf(QLatin1Char('.'));
        if (dot < 0 || dot + 1 >= glob.size()) {
            continue;
        }
        if (glob.mid(dot + 1).compare(QLatin1String("f"), Qt::CaseInsensitive) == 0) {
            globs.append(glob);
        }
    }
    globs.sort();
    return globs;
}

QStringList expectedFilelistGlobs()
{
    QStringList globs = vedit::common::filelistGlobs();
    globs.sort();
    return globs;
}

/// The globs a filelist-claiming entry may carry BESIDES the recognised
/// filelist globs: the historical HDL source types. Anything else is a guess.
QStringList unrecognisedGlobsOf(const QString &filter)
{
    const QStringList sourceSuffixes = {QStringLiteral("v"), QStringLiteral("sv"),
                                        QStringLiteral("svh"), QStringLiteral("vh")};
    const QStringList filelist = expectedFilelistGlobs();
    QStringList bad;
    for (const QString &glob : globsOf(filter)) {
        if (filelist.contains(glob)) {
            continue;
        }
        const int dot = glob.lastIndexOf(QLatin1Char('.'));
        if (dot >= 0 && sourceSuffixes.contains(glob.mid(dot + 1).toLower())) {
            continue;
        }
        bad.append(glob);
    }
    return bad;
}

QStringList namesUnder(QFileSystemModel &model, const QModelIndex &root)
{
    QStringList names;
    for (int row = 0; row < model.rowCount(root); ++row) {
        names.append(model.index(row, 0, root).data(Qt::DisplayRole).toString());
    }
    return names;
}

/// Pump the event loop until `done` or the budget ends (the file-system model
/// loads directories asynchronously).
bool waitFor(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        QApplication::processEvents();
    }
    return done();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // -----------------------------------------------------------------------
    // The ONE source of truth mirrors the parser's fact.
    // -----------------------------------------------------------------------
    check("shared.extensions-are-f",
          vedit::common::filelistExtensions() == QStringList{QStringLiteral("f")},
          vedit::common::filelistExtensions().join(QLatin1Char(',')));
    check("shared.globs-f-and-F",
          expectedFilelistGlobs() == (QStringList{QStringLiteral("*.F"), QStringLiteral("*.f")}),
          vedit::common::filelistGlobs().join(QLatin1Char(',')));
    check("shared.is-filelist-lower", vedit::common::isFilelistPath(QStringLiteral("top.f")));
    check("shared.is-filelist-upper", vedit::common::isFilelistPath(QStringLiteral("TOP.F")));
    check("shared.not-filelist-flist",
          !vedit::common::isFilelistPath(QStringLiteral("top.flist")));
    check("shared.not-filelist-source", !vedit::common::isFilelistPath(QStringLiteral("a.sv")));

    // -----------------------------------------------------------------------
    // (A) The PRODUCT open-file table: every filelist-claiming entry offers
    // EXACTLY the recognised filelist set. (This fails before the fix: both
    // filelist-claiming entries carried the guessed `*.flist`.)
    // -----------------------------------------------------------------------
    const QStringList openTable = entriesOf(vedit::app::openFileDialogFilter());
    check("open.table-has-entries", openTable.size() >= 3,
          QStringLiteral("entries=%1").arg(openTable.size()));
    int claiming = 0;
    int claimIndex = 0;
    for (const QString &filter : openTable) {
        if (!claimsFilelists(filter)) {
            continue;
        }
        ++claiming;
        const QByteArray name =
            QStringLiteral("open.entry%1.filelist-globs-exact").arg(claimIndex).toUtf8();
        const QStringList filelistGlobs = filelistGlobsOf(filter);
        check(name.constData(), filelistGlobs == expectedFilelistGlobs(),
              QStringLiteral("entry='%1' filelist-globs='%2' expected='%3'")
                  .arg(displayNameOf(filter))
                  .arg(filelistGlobs.join(QLatin1Char(',')))
                  .arg(expectedFilelistGlobs().join(QLatin1Char(','))));
        ++claimIndex;
    }
    check("open.has-filelist-claiming-entry", claiming >= 1,
          QStringLiteral("claiming=%1").arg(claiming));
    // The FIRST entry is the dialog default; it must offer filelists.
    if (!openTable.isEmpty()) {
        check("open.default-entry-offers-filelists",
              filelistGlobsOf(openTable.first()) == expectedFilelistGlobs(),
              QStringLiteral("default='%1'").arg(displayNameOf(openTable.first())));
    }
    // No entry anywhere may advertise the guessed `.flist`.
    for (int index = 0; index < openTable.size(); ++index) {
        const QByteArray name =
            QStringLiteral("open.entry%1.no-guessed-flist").arg(index).toUtf8();
        check(name.constData(), !openTable.at(index).contains(QStringLiteral("*.flist")),
              openTable.at(index));
    }
    // A filelist-claiming entry may carry the shared filelist globs and the
    // historical SOURCE globs only: any other glob is an unverified guess.
    for (int index = 0; index < openTable.size(); ++index) {
        if (!claimsFilelists(openTable.at(index))) {
            continue;
        }
        const QByteArray name =
            QStringLiteral("open.entry%1.no-unrecognised-globs").arg(index).toUtf8();
        check(name.constData(), unrecognisedGlobsOf(openTable.at(index)).isEmpty(),
              QStringLiteral("entry='%1' guessed='%2'")
                  .arg(displayNameOf(openTable.at(index)))
                  .arg(unrecognisedGlobsOf(openTable.at(index)).join(QLatin1Char(','))));
    }

    // -----------------------------------------------------------------------
    // (B) The import-filelist dialog: the filelist filter is the SELECTED one,
    // and its globs are exactly the recognised set.
    // -----------------------------------------------------------------------
    const QStringList importTable = vedit::common::importFilelistDialogFilters();
    check("import.table-first-is-filelist",
          !importTable.isEmpty() && importTable.first() == vedit::common::importFilelistFilter(),
          importTable.value(0));
    QFileDialog importDialog;
    importDialog.setOption(QFileDialog::DontUseNativeDialog);
    vedit::common::configureImportFilelistDialog(importDialog);
    check("import.selected-filter-is-filelist",
          importDialog.selectedNameFilter() == vedit::common::importFilelistFilter(),
          QStringLiteral("selected='%1' expected='%2'")
              .arg(importDialog.selectedNameFilter(), vedit::common::importFilelistFilter()));
    check("import.selected-offers-exact-filelist-globs",
          filelistGlobsOf(importDialog.selectedNameFilter()) == expectedFilelistGlobs(),
          QStringLiteral("selected='%1' globs='%2'")
              .arg(importDialog.selectedNameFilter())
              .arg(filelistGlobsOf(importDialog.selectedNameFilter()).join(QLatin1Char(','))));
    check("import.selected-offers-f-and-F",
          importDialog.selectedNameFilter().contains(QStringLiteral("*.f"))
              && importDialog.selectedNameFilter().contains(QStringLiteral("*.F")),
          importDialog.selectedNameFilter());

    // -----------------------------------------------------------------------
    // (B, real proof) With the import dialog's DEFAULT filter, the Qt file
    // engine shows `lib.f` and hides `a.sv` in a directory holding both.
    // -----------------------------------------------------------------------
    QTemporaryDir dir;
    check("fixture.tempdir", dir.isValid());
    const QString svPath = dir.filePath(QStringLiteral("a.sv"));
    const QString fPath = dir.filePath(QStringLiteral("lib.f"));
    const QString notesPath = dir.filePath(QStringLiteral("notes.txt"));
    {
        QFile sv(svPath);
        check("fixture.a-sv", sv.open(QIODevice::WriteOnly));
        QFile list(fPath);
        check("fixture.lib-f", list.open(QIODevice::WriteOnly));
        if (list.isOpen()) {
            list.write("a.sv\n");
        }
        // A file NO filter claims: it is visible only when the filter is NOT in
        // effect, so it distinguishes "the filter was applied" from "everything
        // happened to match".
        QFile notes(notesPath);
        check("fixture.notes", notes.open(QIODevice::WriteOnly));
    }
    QFileSystemModel model;
    model.setRootPath(dir.path());
    // QFileSystemModel DISABLES non-matching entries by default (they stay
    // visible, greyed). A file dialog HIDES them: mirror that engine setting.
    model.setNameFilterDisables(false);
    model.setNameFilters(globsOf(importDialog.selectedNameFilter()));
    const QModelIndex rootIndex = model.index(dir.path());
    check("dialog.same-engine-as-the-file-dialog",
          rootIndex.isValid(), dir.path());
    waitFor([&model, &rootIndex]() { return model.rowCount(rootIndex) > 0; }, 3000);
    const QStringList underFilelistFilter = namesUnder(model, rootIndex);
    std::printf("OBSERVED import-default-filter names='%s'\n",
                qPrintable(underFilelistFilter.join(QLatin1Char(','))));
    check("dialog.default-filter-shows-lib.f",
          underFilelistFilter.contains(QStringLiteral("lib.f")),
          underFilelistFilter.join(QLatin1Char(',')));
    check("dialog.default-filter-hides-a.sv",
          !underFilelistFilter.contains(QStringLiteral("a.sv")),
          underFilelistFilter.join(QLatin1Char(',')));
    model.setNameFilters(QStringList{QStringLiteral("*")});
    waitFor([&model, &rootIndex]() {
        const QStringList names = namesUnder(model, rootIndex);
        return names.contains(QStringLiteral("lib.f")) && names.contains(QStringLiteral("a.sv"));
    }, 3000);
    const QStringList underAllFiles = namesUnder(model, rootIndex);
    check("dialog.all-files-shows-both",
          underAllFiles.contains(QStringLiteral("lib.f"))
              && underAllFiles.contains(QStringLiteral("a.sv")),
          underAllFiles.join(QLatin1Char(',')));

    // -----------------------------------------------------------------------
    // (A, real proof) EVERY open-file filter whose NAME claims filelists must
    // SHOW `lib.f` when its globs drive the same real file engine. This is the
    // exact visibility the user missed (a source-oriented entry that claims
    // filelists but filters them out).
    // -----------------------------------------------------------------------
    int engineIndex = 0;
    for (const QString &filter : openTable) {
        if (!claimsFilelists(filter)) {
            continue;
        }
        QFileSystemModel perFilter;
        perFilter.setNameFilterDisables(false);
        perFilter.setRootPath(dir.path());
        perFilter.setNameFilters(globsOf(filter));
        const QModelIndex perRoot = perFilter.index(dir.path());
        waitFor([&perFilter, &perRoot]() { return perFilter.rowCount(perRoot) > 0; }, 2000);
        const QStringList names = namesUnder(perFilter, perRoot);
        const QByteArray name =
            QStringLiteral("open.engine%1.filelist-visible").arg(engineIndex).toUtf8();
        std::printf("OBSERVED open-filter-engine index=%d filter='%s' names='%s'\n",
                    engineIndex, qPrintable(displayNameOf(filter)),
                    qPrintable(names.join(QLatin1Char(','))));
        check(name.constData(), names.contains(QStringLiteral("lib.f")),
              QStringLiteral("filter='%1' names='%2'")
                  .arg(displayNameOf(filter))
                  .arg(names.join(QLatin1Char(','))));
        ++engineIndex;
    }
    check("open.engine-proved-at-least-one", engineIndex >= 1,
          QStringLiteral("proved=%1").arg(engineIndex));

    // -----------------------------------------------------------------------
    // RM-16b: the SELECTED filter must be IN EFFECT AT CREATION - no user
    // interaction, no second click on the type control. Read the dialog's own
    // file-system model (the view Qt renders) and the model's name filters.
    // -----------------------------------------------------------------------
    const auto sortedGlobs = [](QStringList globs) {
        globs.sort();
        return globs;
    };
    // (a) The import dialog, built exactly as the product builds it (directory
    // in the constructor, filters + explicit selection afterwards).
    {
        QFileDialog creationDialog(nullptr,
                                   QCoreApplication::translate("VEdit", "Import Filelist"),
                                   dir.path());
        creationDialog.setOption(QFileDialog::DontUseNativeDialog);
        vedit::common::configureImportFilelistDialog(creationDialog);
        auto *model = creationDialog.findChild<QFileSystemModel *>();
        check("creation.import.model-present", model != nullptr);
        check("creation.import.selected-is-filelist",
              creationDialog.selectedNameFilter() == vedit::common::importFilelistFilter());
        check("creation.import.filters-in-effect-before-show",
              model != nullptr
                  && sortedGlobs(model->nameFilters())
                         == sortedGlobs(vedit::common::filelistGlobs()),
              model != nullptr ? model->nameFilters().join(QLatin1Char(','))
                               : QStringLiteral("<no model>"));
        creationDialog.show();
        if (model != nullptr) {
            const QModelIndex root = model->index(dir.path());
            waitFor([model, &root]() { return model->rowCount(root) > 0; }, 4000);
            const QStringList visible = namesUnder(*model, root);
            std::printf("OBSERVED creation-import visible='%s'\n",
                        qPrintable(visible.join(QLatin1Char(','))));
            check("creation.import.shows-lib.f",
                  visible.contains(QStringLiteral("lib.f")),
                  visible.join(QLatin1Char(',')));
            check("creation.import.hides-a.sv",
                  !visible.contains(QStringLiteral("a.sv")),
                  visible.join(QLatin1Char(',')));
            check("creation.import.hides-unclaimed",
                  !visible.contains(QStringLiteral("notes.txt")),
                  visible.join(QLatin1Char(',')));
        }
        creationDialog.reject();
    }
    // (b) The File -> Open dialog through the product helper: the historical
    // first entry (sources + filelists) is selected and in effect at creation.
    {
        QFileDialog creationDialog(nullptr, QCoreApplication::translate("VEdit", "Open File"),
                                   dir.path());
        creationDialog.setOption(QFileDialog::DontUseNativeDialog);
        vedit::common::configureOpenFileDialog(creationDialog);
        auto *model = creationDialog.findChild<QFileSystemModel *>();
        const QString firstFilter = vedit::common::openFileDialogFilters().value(0);
        check("creation.open.default-is-first", creationDialog.selectedNameFilter() == firstFilter,
              creationDialog.selectedNameFilter());
        check("creation.open.filters-in-effect-before-show",
              model != nullptr
                  && sortedGlobs(model->nameFilters())
                         == sortedGlobs(vedit::common::filterGlobs(firstFilter)),
              model != nullptr ? model->nameFilters().join(QLatin1Char(','))
                               : QStringLiteral("<no model>"));
        creationDialog.show();
        if (model != nullptr) {
            const QModelIndex root = model->index(dir.path());
            waitFor([model, &root]() { return model->rowCount(root) > 0; }, 4000);
            const QStringList visible = namesUnder(*model, root);
            std::printf("OBSERVED creation-open-default visible='%s'\n",
                        qPrintable(visible.join(QLatin1Char(','))));
            check("creation.open.shows-source", visible.contains(QStringLiteral("a.sv")),
                  visible.join(QLatin1Char(',')));
            check("creation.open.shows-filelist", visible.contains(QStringLiteral("lib.f")),
                  visible.join(QLatin1Char(',')));
            check("creation.open.hides-unclaimed",
                  !visible.contains(QStringLiteral("notes.txt")),
                  visible.join(QLatin1Char(',')));
        }
        creationDialog.reject();
    }
    // (c) The File -> Open dialog with the FILELIST entry selected: the filelist
    // type filters the view at creation (the card's "filelist entry selected ⇒
    // lib.f visible, a.sv hidden" case).
    {
        QFileDialog creationDialog(nullptr, QCoreApplication::translate("VEdit", "Open File"),
                                   dir.path());
        creationDialog.setOption(QFileDialog::DontUseNativeDialog);
        creationDialog.setNameFilters(vedit::common::openFileDialogFilters());
        const QString filelistEntry = vedit::common::openFileDialogFilters().value(2);
        creationDialog.selectNameFilter(filelistEntry);
        vedit::common::applySelectedFilterToModel(creationDialog);
        auto *model = creationDialog.findChild<QFileSystemModel *>();
        check("creation.open-filelist.filters-in-effect-before-show",
              model != nullptr
                  && sortedGlobs(model->nameFilters())
                         == sortedGlobs(vedit::common::filelistGlobs()),
              model != nullptr ? model->nameFilters().join(QLatin1Char(','))
                               : QStringLiteral("<no model>"));
        creationDialog.show();
        if (model != nullptr) {
            const QModelIndex root = model->index(dir.path());
            waitFor([model, &root]() { return model->rowCount(root) > 0; }, 4000);
            const QStringList visible = namesUnder(*model, root);
            std::printf("OBSERVED creation-open-filelist visible='%s'\n",
                        qPrintable(visible.join(QLatin1Char(','))));
            check("creation.open-filelist.shows-lib.f",
                  visible.contains(QStringLiteral("lib.f")), visible.join(QLatin1Char(',')));
            check("creation.open-filelist.hides-source",
                  !visible.contains(QStringLiteral("a.sv")), visible.join(QLatin1Char(',')));
        }
        creationDialog.reject();
    }

    std::printf("VE-FILE-DIALOG-FILTERS-SELFTEST checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
