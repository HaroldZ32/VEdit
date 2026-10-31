// Standalone driver for the workspace file tree panel (task T21).
//
//   vedit-filetree-demo --prepare <root> [--big-count N]
//       Create the fixture the screenshots and the manual run use: a small
//       Verilog-ish project, an empty folder, a folder used for the
//       permission-denied state, and a large folder of N files.
//
//   vedit-filetree-demo --screenshots <out> --project <dir> --empty <dir>
//                       --denied <dir> --big <dir> [--missing <dir>]
//       Drive the panel through every A2a interaction on the real filesystem,
//       write PNGs into <out> and print what happened (rows, disk contents,
//       laziness counters, empty-state text). Symbols are all ASCII so the
//       pinned image needs no CJK font.

#include "file_kind.h"
#include "file_tree_model.h"
#include "file_tree_widget.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QStringList>
#include <QTreeView>

#include <cstdio>

using vedit::workspace::FileKind;
using vedit::workspace::FileTreeModel;
using vedit::workspace::FileTreeWidget;
using vedit::workspace::fileKindForPath;
using vedit::workspace::fileKindId;
using vedit::workspace::fileKindLabel;

namespace {

void pump(int milliseconds = 40)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

bool makeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (!contents.isEmpty()) {
        file.write(contents);
    }
    return true;
}

QStringList diskNames(const QString &directory)
{
    QDir dir(directory);
    QStringList names = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::NoSort);
    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList treeNames(FileTreeModel *model, const QString &path)
{
    QStringList names;
    const QModelIndex index = model->indexForPath(path, true);
    if (!index.isValid()) {
        return names;
    }
    model->ensureFullyLoaded(index);
    const int rows = model->rowCount(index);
    for (int row = 0; row < rows; ++row) {
        names << model->index(row, 0, index).data(Qt::DisplayRole).toString();
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

void printListing(const char *tag, const QString &path, const QStringList &names)
{
    std::printf("%s path=%s entries=%d [%s]\n", tag, qPrintable(path), static_cast<int>(names.size()),
                qPrintable(names.join(QLatin1Char(','))));
}

int prepare(const QString &root, int bigCount)
{
    QDir rootDir(root);
    if (rootDir.exists() && !rootDir.removeRecursively()) {
        std::printf("PREPARE-FAIL cannot clear %s\n", qPrintable(root));
        return 1;
    }
    if (!rootDir.mkpath(QStringLiteral("."))) {
        std::printf("PREPARE-FAIL cannot create %s\n", qPrintable(root));
        return 1;
    }

    const QString project = root + QStringLiteral("/project");
    const auto mkdirs = [&root](const QStringList &relative) {
        for (const QString &path : relative) {
            if (!QDir().mkpath(root + QLatin1Char('/') + path)) {
                return false;
            }
        }
        return true;
    };
    if (!mkdirs({QStringLiteral("project/rtl/sub"), QStringLiteral("project/tb"),
                 QStringLiteral("project/docs"), QStringLiteral("empty"),
                 QStringLiteral("denied"), QStringLiteral("big")})) {
        std::printf("PREPARE-FAIL cannot create the fixture tree\n");
        return 1;
    }

    const auto write = [](const QString &path, const QByteArray &contents) {
        if (!makeFile(path, contents)) {
            std::printf("PREPARE-FAIL cannot write %s\n", qPrintable(path));
            return false;
        }
        return true;
    };
    const bool written =
        write(project + QStringLiteral("/rtl/top.v"),
              "module top;\n  // top level\nendmodule\n")
        && write(project + QStringLiteral("/rtl/alu.sv"),
                 "module alu(input logic [7:0] a, output logic [7:0] y);\nendmodule\n")
        && write(project + QStringLiteral("/rtl/defs.vh"), "`define WIDTH 8\n")
        && write(project + QStringLiteral("/rtl/sub/counter.v"),
                 "module counter;\nendmodule\n")
        && write(project + QStringLiteral("/tb/top_tb.sv"),
                 "module top_tb;\nendmodule\n")
        && write(project + QStringLiteral("/docs/notes.md"), "# Notes\n")
        && write(project + QStringLiteral("/filelist.f"),
                 "rtl/top.v\nrtl/alu.sv\n+incdir+rtl\n")
        && write(project + QStringLiteral("/README.md"), "# demo project\n");
    if (!written) {
        return 1;
    }

    for (int i = 0; i < bigCount; ++i) {
        const QString name = QStringLiteral("module_%1.v").arg(i, 5, 10, QLatin1Char('0'));
        if (!makeFile(root + QStringLiteral("/big/") + name, QByteArray())) {
            std::printf("PREPARE-FAIL cannot create big/%s\n", qPrintable(name));
            return 1;
        }
    }

    std::printf("PREPARE ok=1 root=%s project_files=7 big_files=%d\n", qPrintable(root), bigCount);
    return 0;
}

bool saveScreenshot(FileTreeWidget &widget, const QString &directory, const QString &name)
{
    const QString path = directory + QLatin1Char('/') + name;
    const QImage image = widget.grab().toImage();
    QImageWriter writer(path);
    const bool ok = !image.isNull() && writer.write(image);
    std::printf("SCREENSHOT ok=%d %s size=%dx%d%s%s\n", ok ? 1 : 0, qPrintable(path), image.width(),
                image.height(), ok ? "" : " error=", ok ? "" : qPrintable(writer.errorString()));
    return ok;
}

int runScreenshots(const QString &out, const QString &project, const QString &empty,
                   const QString &denied, const QString &big, const QString &missing)
{
    if (!QDir().mkpath(out)) {
        std::printf("SCREENSHOT-FAIL cannot create %s\n", qPrintable(out));
        return 1;
    }

    FileTreeWidget widget;
    widget.setInteractiveDialogs(false);
    widget.resize(560, 720);
    QObject::connect(&widget, &FileTreeWidget::openFileRequested, [](const QString &path) {
        std::printf("OPEN-REQUEST path=%s\n", qPrintable(path));
    });
    widget.show();
    pump(80);

    bool ok = true;
    FileTreeModel *model = widget.model();
    QString error;

    // 1. Open a folder, expand two levels, select a .v file (status shows the
    //    kind, which is the ".v is Verilog in the tree, V only in the
    //    highlighter" check).
    widget.openDirectory(project);
    pump(60);
    const QModelIndex rtlIndex = model->indexForPath(project + QStringLiteral("/rtl"), true);
    if (rtlIndex.isValid()) {
        widget.view()->setExpanded(rtlIndex, true);
        model->ensureLoaded(rtlIndex);
    }
    const QModelIndex subIndex = model->indexForPath(project + QStringLiteral("/rtl/sub"), true);
    if (subIndex.isValid()) {
        widget.view()->setExpanded(subIndex, true);
        model->ensureLoaded(subIndex);
    }
    widget.view()->expandAll();
    widget.selectPath(project + QStringLiteral("/rtl/top.v"));
    pump(80);
    ok = saveScreenshot(widget, out, QStringLiteral("10-file-tree-open.png")) && ok;
    std::printf("SCENARIO open root=%s rows=%d status=\"%s\"\n", qPrintable(widget.rootPath()),
                model->rowCount(model->index(0, 0, QModelIndex())), qPrintable(widget.statusText()));
    std::printf("KIND top.v=%s(%s) alu.sv=%s(%s) filelist.f=%s(%s)\n",
                qPrintable(fileKindId(fileKindForPath(QStringLiteral("top.v"), false))),
                qPrintable(fileKindLabel(fileKindForPath(QStringLiteral("top.v"), false))),
                qPrintable(fileKindId(fileKindForPath(QStringLiteral("alu.sv"), false))),
                qPrintable(fileKindLabel(fileKindForPath(QStringLiteral("alu.sv"), false))),
                qPrintable(fileKindId(fileKindForPath(QStringLiteral("filelist.f"), false))),
                qPrintable(fileKindLabel(fileKindForPath(QStringLiteral("filelist.f"), false))));

    // 2. Double-click behaviour through the panel's activation hook: a file
    //    raises openFileRequested, a folder expands.
    const QModelIndex fileIndex = model->indexForPath(project + QStringLiteral("/rtl/alu.sv"), true);
    if (fileIndex.isValid()) {
        widget.activateIndex(fileIndex);
    }
    widget.activateIndex(rtlIndex);
    pump(30);

    // 3. New / rename / delete on the real filesystem; after each step the
    //    on-disk listing and the model's rows are printed side by side.
    const QString rtlPath = project + QStringLiteral("/rtl");
    ok = widget.createFile(rtlPath, QStringLiteral("new_module.v"), &error) && ok;
    pump(60);
    ok = saveScreenshot(widget, out, QStringLiteral("11-created-file.png")) && ok;
    printListing("DISK-CREATE", rtlPath, diskNames(rtlPath));
    printListing("TREE-CREATE", rtlPath, treeNames(model, rtlPath));
    std::printf("OP create ok=%d status=\"%s\"\n", diskNames(rtlPath).contains(QStringLiteral("new_module.v")) ? 1 : 0,
                qPrintable(widget.statusText()));

    ok = widget.renameEntry(rtlPath + QStringLiteral("/new_module.v"),
                            QStringLiteral("renamed_module.v"), &error) && ok;
    pump(60);
    ok = saveScreenshot(widget, out, QStringLiteral("12-renamed-file.png")) && ok;
    printListing("DISK-RENAME", rtlPath, diskNames(rtlPath));
    printListing("TREE-RENAME", rtlPath, treeNames(model, rtlPath));
    std::printf("OP rename old_gone=%d new_present=%d status=\"%s\"\n",
                QFileInfo::exists(rtlPath + QStringLiteral("/new_module.v")) ? 0 : 1,
                QFileInfo::exists(rtlPath + QStringLiteral("/renamed_module.v")) ? 1 : 0,
                qPrintable(widget.statusText()));

    ok = widget.deleteEntry(rtlPath + QStringLiteral("/renamed_module.v"), false, &error) && ok;
    pump(60);
    ok = saveScreenshot(widget, out, QStringLiteral("13-deleted-file.png")) && ok;
    printListing("DISK-DELETE", rtlPath, diskNames(rtlPath));
    printListing("TREE-DELETE", rtlPath, treeNames(model, rtlPath));
    std::printf("OP delete gone_from_disk=%d status=\"%s\"\n",
                QFileInfo::exists(rtlPath + QStringLiteral("/renamed_module.v")) ? 0 : 1,
                qPrintable(widget.statusText()));

    // 4. Empty folder state.
    widget.openDirectory(empty);
    pump(60);
    ok = saveScreenshot(widget, out, QStringLiteral("14-empty-folder.png")) && ok;
    std::printf("EMPTY-STATE kind=empty text=\"%s\" visible=%d\n",
                qPrintable(widget.emptyStateText()), widget.emptyStateVisible() ? 1 : 0);

    // 5. Permission denied state (the fixture is chmod 000; as root the OS
    //    still lets us in, which is reported honestly instead of pretending).
    widget.openDirectory(denied);
    pump(60);
    ok = saveScreenshot(widget, out, QStringLiteral("15-permission-denied.png")) && ok;
    std::printf("EMPTY-STATE kind=denied text=\"%s\" visible=%d\n",
                qPrintable(widget.emptyStateText()), widget.emptyStateVisible() ? 1 : 0);

    // 6. Large folder: open must not materialise 10k rows (lazy batches).
    QElapsedTimer timer;
    timer.start();
    widget.openDirectory(big);
    const qint64 openMs = timer.elapsed();
    pump(120);
    ok = saveScreenshot(widget, out, QStringLiteral("16-large-folder-lazy.png")) && ok;
    const QModelIndex bigRoot = model->index(0, 0, QModelIndex());
    std::printf("LAZY open_ms=%lld rows_loaded=%d can_fetch_more=%d scanned_dirs=%d\n",
                static_cast<long long>(openMs), model->loadedRows(),
                model->canFetchMore(bigRoot) ? 1 : 0, model->scannedDirectories());
    model->ensureFullyLoaded(bigRoot);
    std::printf("LAZY fully_loaded rows=%d total_ms=%lld scanned_dirs=%d\n",
                model->rowCount(bigRoot), static_cast<long long>(timer.elapsed()),
                model->scannedDirectories());

    // 7. Missing folder state.
    if (!missing.isEmpty()) {
        widget.openDirectory(missing);
        pump(60);
        ok = saveScreenshot(widget, out, QStringLiteral("17-missing-folder.png")) && ok;
        std::printf("EMPTY-STATE kind=missing text=\"%s\" visible=%d\n",
                    qPrintable(widget.emptyStateText()), widget.emptyStateVisible() ? 1 : 0);
    }

    std::printf("FILETREE-DEMO ok=%d dir=%s\n", ok ? 1 : 0, qPrintable(out));
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-filetree-demo"));

    QString prepareRoot;
    QString screenshots;
    QString project;
    QString empty;
    QString denied;
    QString big;
    QString missing;
    int bigCount = 10000;

    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        const auto value = [&args, &i]() { return i + 1 < args.size() ? args.at(++i) : QString(); };
        if (arg == QLatin1String("--prepare")) {
            prepareRoot = value();
        } else if (arg == QLatin1String("--big-count")) {
            bigCount = value().toInt();
        } else if (arg == QLatin1String("--screenshots")) {
            screenshots = value();
        } else if (arg == QLatin1String("--project")) {
            project = value();
        } else if (arg == QLatin1String("--empty")) {
            empty = value();
        } else if (arg == QLatin1String("--denied")) {
            denied = value();
        } else if (arg == QLatin1String("--big")) {
            big = value();
        } else if (arg == QLatin1String("--missing")) {
            missing = value();
        } else {
            std::printf("unknown argument: %s\n", qPrintable(arg));
            return 2;
        }
    }

    if (!prepareRoot.isEmpty()) {
        return prepare(prepareRoot, bigCount);
    }
    if (!screenshots.isEmpty() && !project.isEmpty()) {
        return runScreenshots(screenshots, project, empty, denied, big, missing);
    }

    std::printf("usage:\n"
                "  vedit-filetree-demo --prepare <root> [--big-count N]\n"
                "  vedit-filetree-demo --screenshots <out> --project <dir> --empty <dir>"
                " --denied <dir> --big <dir> [--missing <dir>]\n");
    return 2;
}
