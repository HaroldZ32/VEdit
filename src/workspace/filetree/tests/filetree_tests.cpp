// Acceptance tests for the workspace file tree panel (task T21, acceptance A2a).
//
// The suite runs on a real filesystem under the build tree (never /tmp: this
// session isolates /tmp per command, and a previous writer lost evidence that
// way). Every case prints a machine-readable `CHECK <name> ok=<0|1>` line, the
// measurements print `MEASURE <name> ...`, and the process exits non-zero when
// any check fails, so ctest is red on a real defect.

#include "file_kind.h"
#include "file_tree_model.h"
#include "file_tree_widget.h"
#include "filetree_panel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStringList>
#include <QTreeView>
#include <QtTest/QTest>

#include <unistd.h>

#include <cstdio>

using vedit::workspace::FileKind;
using vedit::workspace::FileTreeModel;
using vedit::workspace::FileTreeWidget;
using vedit::workspace::fileKindForPath;
using vedit::workspace::fileKindId;
using vedit::workspace::fileKindLabel;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    if (detail.isEmpty()) {
        std::printf("CHECK %s ok=%d\n", name, ok ? 1 : 0);
    } else {
        std::printf("CHECK %s ok=%d detail=\"%s\"\n", name, ok ? 1 : 0, qPrintable(detail));
    }
}

void measure(const char *name, qint64 value, const char *unit)
{
    std::printf("MEASURE %s %lld%s\n", name, static_cast<long long>(value), unit);
}

void pump(int milliseconds = 40)
{
    QTest::qWait(milliseconds);
}

QString scratchRoot()
{
    return QDir::current().absoluteFilePath(QStringLiteral("filetree-testdata"));
}

bool writeFile(const QString &path, const QByteArray &contents = QByteArray())
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (!contents.isEmpty()) {
        file.write(contents);
    }
    file.close();
    return true;
}

QStringList sortedDiskNames(const QString &directory)
{
    QDir dir(directory);
    QStringList names = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::NoSort);
    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList sortedTreeNames(FileTreeModel *model, const QString &path)
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

bool diskAndTreeAgree(FileTreeModel *model, const QString &directory)
{
    return sortedDiskNames(directory) == sortedTreeNames(model, directory);
}

bool treeContains(FileTreeModel *model, const QString &directory, const QString &name)
{
    return model->indexForPath(QDir(directory).filePath(name), true).isValid();
}

// ---------------------------------------------------------------------------
// file kind classification (the ".v" trap)
// ---------------------------------------------------------------------------

void testFileKinds()
{
    check("kind.v-is-verilog", fileKindForPath(QStringLiteral("top.v"), false) == FileKind::Verilog);
    check("kind.v-label", fileKindLabel(fileKindForPath(QStringLiteral("top.v"), false))
                              == QStringLiteral("Verilog"));
    check("kind.v-id-not-vlang", fileKindId(fileKindForPath(QStringLiteral("top.v"), false))
                                     != QStringLiteral("v"));
    check("kind.vh-is-verilog", fileKindForPath(QStringLiteral("defs.vh"), false) == FileKind::Verilog);
    check("kind.sv-is-systemverilog",
          fileKindForPath(QStringLiteral("alu.sv"), false) == FileKind::SystemVerilog);
    check("kind.svh-is-systemverilog",
          fileKindForPath(QStringLiteral("defs.svh"), false) == FileKind::SystemVerilog);
    check("kind.uppercase-suffix",
          fileKindForPath(QStringLiteral("ALU.SV"), false) == FileKind::SystemVerilog);
    check("kind.f-is-filelist",
          fileKindForPath(QStringLiteral("design.f"), false) == FileKind::FileList);
    check("kind.txt-is-other", fileKindForPath(QStringLiteral("notes.txt"), false) == FileKind::Other);
    check("kind.extensionless-other",
          fileKindForPath(QStringLiteral("Makefile"), false) == FileKind::Other);
    check("kind.directory", fileKindForPath(QStringLiteral("rtl"), true) == FileKind::Directory);
}

// ---------------------------------------------------------------------------
// root / empty states
// ---------------------------------------------------------------------------

void testRootStates(const QString &scratch)
{
    FileTreeWidget widget;
    widget.resize(520, 600);
    widget.show();
    pump(40);

    widget.openDirectory(QString());
    pump(20);
    check("state.no-root", widget.model()->rootState() == FileTreeModel::RootState::NoRoot);
    check("state.no-root.text", widget.emptyStateText().contains(QStringLiteral("No folder open")),
          widget.emptyStateText());
    check("state.no-root.visible", widget.emptyStateVisible());
    check("state.no-root.rows", widget.model()->rowCount(QModelIndex()) == 0);

    widget.openDirectory(scratch + QStringLiteral("/does-not-exist"));
    pump(20);
    check("state.missing", widget.model()->rootState() == FileTreeModel::RootState::Missing);
    check("state.missing.text",
          widget.emptyStateText().contains(QStringLiteral("does not exist")), widget.emptyStateText());

    const QString plainFile = scratch + QStringLiteral("/plain.txt");
    check("state.fixture-file", writeFile(plainFile, "x"));
    widget.openDirectory(plainFile);
    pump(20);
    check("state.not-a-folder", widget.model()->rootState() == FileTreeModel::RootState::NotAFolder);
    check("state.not-a-folder.text",
          widget.emptyStateText().contains(QStringLiteral("not a folder")), widget.emptyStateText());

    const QString emptyDir = scratch + QStringLiteral("/empty");
    QDir().mkpath(emptyDir);
    widget.openDirectory(emptyDir);
    pump(40);
    check("state.empty-folder", widget.model()->rootState() == FileTreeModel::RootState::EmptyFolder);
    check("state.empty-folder.text",
          widget.emptyStateText().contains(QStringLiteral("empty")), widget.emptyStateText());
    check("state.empty-folder.visible", widget.emptyStateVisible());
    check("state.empty-folder.no-rows", widget.model()->rowCount(widget.model()->index(0, 0)) == 0);

    const QString lockedDir = scratch + QStringLiteral("/locked");
    QDir().mkpath(lockedDir);
    QFile::setPermissions(lockedDir, QFile::Permissions());
    const bool isRoot = ::geteuid() == 0;
    if (isRoot) {
        std::printf("CHECK state.permission-denied SKIPPED running-as-root\n");
    } else {
        widget.openDirectory(lockedDir);
        pump(20);
        check("state.permission-denied",
              widget.model()->rootState() == FileTreeModel::RootState::PermissionDenied);
        check("state.permission-denied.text",
              widget.emptyStateText().contains(QStringLiteral("permission denied"), Qt::CaseInsensitive),
              widget.emptyStateText());
        check("state.permission-denied.visible", widget.emptyStateVisible());
    }
    QFile::setPermissions(lockedDir,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

// ---------------------------------------------------------------------------
// tree shape, expand/collapse, double click
// ---------------------------------------------------------------------------

void testTreeAndInteraction(const QString &scratch)
{
    const QString root = scratch + QStringLiteral("/tree");
    QDir().mkpath(root + QStringLiteral("/alpha/deep"));
    QDir().mkpath(root + QStringLiteral("/beta"));
    QDir().mkpath(root + QStringLiteral("/zeta"));
    writeFile(root + QStringLiteral("/alpha/a1.v"));
    writeFile(root + QStringLiteral("/alpha/deep/d1.sv"));
    writeFile(root + QStringLiteral("/beta/b1.f"));
    writeFile(root + QStringLiteral("/top.v"));

    FileTreeWidget widget;
    widget.setInteractiveDialogs(false);
    widget.resize(560, 640);
    widget.show();
    pump(60);
    widget.openDirectory(root);
    pump(60);

    FileTreeModel *model = widget.model();
    const QModelIndex rootIndex = model->index(0, 0, QModelIndex());
    check("tree.root-open", model->rootState() == FileTreeModel::RootState::Ready);
    check("tree.root-expanded", widget.view()->isExpanded(rootIndex));
    check("tree.root.rows", model->rowCount(rootIndex) == 4, QString::number(model->rowCount(rootIndex)));
    QStringList names;
    for (int row = 0; row < model->rowCount(rootIndex); ++row) {
        names << model->index(row, 0, rootIndex).data(Qt::DisplayRole).toString();
    }
    check("tree.folders-first", names == QStringList({QStringLiteral("alpha"), QStringLiteral("beta"),
                                                     QStringLiteral("zeta"), QStringLiteral("top.v")}),
          names.join(QLatin1Char(',')));

    const QModelIndex alphaIndex = model->indexForPath(root + QStringLiteral("/alpha"), true);
    check("tree.lazy.nested-not-read", !model->isPopulated(alphaIndex));
    check("tree.lazy.nested-no-rows", model->rowCount(alphaIndex) == 0);
    check("tree.lazy.one-dir-scanned", model->scannedDirectories() == 1,
          QString::number(model->scannedDirectories()));

    model->ensureLoaded(alphaIndex);
    check("tree.alpha.populated", model->isPopulated(alphaIndex));
    check("tree.alpha.rows", model->rowCount(alphaIndex) == 2,
          QString::number(model->rowCount(alphaIndex)));
    check("tree.alpha.folders-first",
          model->index(0, 0, alphaIndex).data(Qt::DisplayRole).toString() == QStringLiteral("deep"));

    widget.view()->setExpanded(alphaIndex, false);
    check("tree.collapse", !widget.view()->isExpanded(alphaIndex));
    widget.view()->setExpanded(alphaIndex, true);
    check("tree.expand", widget.view()->isExpanded(alphaIndex));

    // Double click on a folder expands it (QTreeView's native behaviour).
    widget.view()->setExpanded(alphaIndex, false);
    pump(20);
    const QRect alphaRect = widget.view()->visualRect(alphaIndex);
    check("tree.dblclick.rect-visible", alphaRect.isValid() && !alphaRect.isEmpty());
    QSignalSpy fileSpy(&widget, &FileTreeWidget::openFileRequested);
    QSignalSpy viewSpy(widget.view(), &QTreeView::doubleClicked);
    QTest::mouseClick(widget.view()->viewport(), Qt::LeftButton, Qt::NoModifier, alphaRect.center());
    QTest::mouseDClick(widget.view()->viewport(), Qt::LeftButton, Qt::NoModifier, alphaRect.center());
    pump(60);
    check("tree.dblclick.view-signal", viewSpy.count() == 1, QString::number(viewSpy.count()));
    check("tree.dblclick.folder-expands", widget.view()->isExpanded(alphaIndex));
    check("tree.dblclick.folder-does-not-open", fileSpy.count() == 0);

    // Double click on a file raises openFileRequested with its path.
    const QModelIndex fileIndex = model->indexForPath(root + QStringLiteral("/top.v"), true);
    check("tree.file-index", fileIndex.isValid());
    const QRect fileRect = widget.view()->visualRect(fileIndex);
    check("tree.dblclick.file-rect", fileRect.isValid() && !fileRect.isEmpty());
    QTest::mouseClick(widget.view()->viewport(), Qt::LeftButton, Qt::NoModifier, fileRect.center());
    QTest::mouseDClick(widget.view()->viewport(), Qt::LeftButton, Qt::NoModifier, fileRect.center());
    pump(60);
    check("tree.dblclick.file-opens", fileSpy.count() == 1, QString::number(fileSpy.count()));
    if (fileSpy.count() == 1) {
        check("tree.dblclick.file-path",
              fileSpy.at(0).at(0).toString() == QDir(root).filePath(QStringLiteral("top.v")),
              fileSpy.at(0).at(0).toString());
    }

    // activateIndex() is the same contract for programmatic callers.
    QSignalSpy activationSpy(&widget, &FileTreeWidget::openFileRequested);
    widget.activateIndex(fileIndex);
    pump(20);
    check("tree.activate.file-opens", activationSpy.count() == 1);

    // Selecting a .v file shows the tree's own classification, not "V".
    widget.selectPath(root + QStringLiteral("/top.v"));
    pump(20);
    check("tree.status.kind", widget.statusText().contains(QStringLiteral("Verilog")),
          widget.statusText());

    check("tree.disk-and-tree-agree", diskAndTreeAgree(model, root));
    check("tree.disk-and-tree-agree.alpha",
          diskAndTreeAgree(model, root + QStringLiteral("/alpha")));
}

// ---------------------------------------------------------------------------
// laziness on a large folder
// ---------------------------------------------------------------------------

void testLaziness(const QString &scratch)
{
    const QString big = scratch + QStringLiteral("/big");
    QDir().mkpath(big);
    const int fileCount = 10000;
    for (int i = 0; i < fileCount; ++i) {
        const QString name = QStringLiteral("module_%1.v").arg(i, 5, 10, QLatin1Char('0'));
        if (!writeFile(big + QLatin1Char('/') + name)) {
            std::printf("CHECK lazy.fixture ok=0 cannot write %s\n", qPrintable(name));
            ++g_checks;
            ++g_failures;
            return;
        }
    }
    QDir().mkpath(big + QStringLiteral("/aaa_child"));
    QDir().mkpath(big + QStringLiteral("/bbb_child"));

    FileTreeWidget widget;
    widget.resize(560, 640);
    widget.show();
    pump(60);

    QElapsedTimer timer;
    timer.start();
    widget.openDirectory(big);
    const qint64 openMs = timer.elapsed();
    pump(120);

    FileTreeModel *model = widget.model();
    const QModelIndex rootIndex = model->index(0, 0, QModelIndex());
    measure("lazy.open", openMs, "ms");
    measure("lazy.rows-after-open", model->loadedRows(), "");
    check("lazy.open-fast", openMs < 500, QStringLiteral("%1 ms").arg(openMs));
    check("lazy.not-all-rows-materialised", model->loadedRows() < fileCount,
          QString::number(model->loadedRows()));
    check("lazy.can-fetch-more", model->canFetchMore(rootIndex));
    check("lazy.only-root-scanned", model->scannedDirectories() == 1,
          QString::number(model->scannedDirectories()));

    const QModelIndex childIndex = model->indexForPath(big + QStringLiteral("/aaa_child"), true);
    check("lazy.child-index", childIndex.isValid());
    check("lazy.child-not-read", !model->isPopulated(childIndex));

    timer.restart();
    model->fetchMore(rootIndex);
    const qint64 batchMs = timer.elapsed();
    measure("lazy.one-batch", batchMs, "ms");
    check("lazy.batch-fast", batchMs < 200, QStringLiteral("%1 ms").arg(batchMs));

    timer.restart();
    model->ensureFullyLoaded(rootIndex);
    const qint64 drainMs = timer.elapsed();
    measure("lazy.full-drain", drainMs, "ms");
    check("lazy.total-rows", model->rowCount(rootIndex) == fileCount + 2,
          QString::number(model->rowCount(rootIndex)));
    check("lazy.disk-and-tree-agree", diskAndTreeAgree(model, big));

    // The view stays responsive while the batches are handed out: at least one
    // event-loop turn runs between the first insert and the last one.
    check("lazy.ui-not-blocked", openMs < 500 && model->loadedRows() > 0);
}

// ---------------------------------------------------------------------------
// new / rename / delete against the real filesystem
// ---------------------------------------------------------------------------

void testOperations(const QString &scratch)
{
    const QString ops = scratch + QStringLiteral("/ops");
    QDir().mkpath(ops);

    FileTreeWidget widget;
    widget.setInteractiveDialogs(false);
    widget.resize(560, 640);
    widget.show();
    pump(40);
    widget.openDirectory(ops);
    pump(40);
    FileTreeModel *model = widget.model();
    QString error;

    check("ops.create-file", widget.createFile(ops, QStringLiteral("alpha.v"), &error), error);
    check("ops.create-file.on-disk", QFileInfo::exists(ops + QStringLiteral("/alpha.v")));
    check("ops.create-file.in-tree", treeContains(model, ops, QStringLiteral("alpha.v")));
    check("ops.create-file.consistent", diskAndTreeAgree(model, ops));
    check("ops.create-file.status", widget.statusText().contains(QStringLiteral("Created file")),
          widget.statusText());

    const QStringList beforeDuplicate = sortedDiskNames(ops);
    check("ops.create-dup-refused",
          !widget.createFile(ops, QStringLiteral("alpha.v"), &error));
    check("ops.create-dup-error", !error.isEmpty(), error);
    check("ops.create-dup.no-disk-change", sortedDiskNames(ops) == beforeDuplicate);
    check("ops.create-dup.tree-count", model->rowCount(model->index(0, 0)) == 1);

    check("ops.invalid.empty-name", !widget.createFile(ops, QString(), &error));
    check("ops.invalid.empty-name.error", !error.isEmpty(), error);
    check("ops.invalid.separator", !widget.createFolder(ops, QStringLiteral("a/b"), &error));
    check("ops.invalid.separator.error",
          error.contains(QStringLiteral("separator")), error);
    check("ops.invalid.dotdot", !widget.createFile(ops, QStringLiteral(".."), &error));
    check("ops.invalid.no-disk-change", sortedDiskNames(ops) == beforeDuplicate);

    check("ops.create-folder", widget.createFolder(ops, QStringLiteral("rtl"), &error), error);
    check("ops.create-folder.on-disk", QFileInfo(ops + QStringLiteral("/rtl")).isDir());
    check("ops.create-folder.in-tree", treeContains(model, ops, QStringLiteral("rtl")));
    check("ops.create-folder.consistent", diskAndTreeAgree(model, ops));

    check("ops.rename", widget.renameEntry(ops + QStringLiteral("/alpha.v"),
                                           QStringLiteral("beta.v"), &error), error);
    check("ops.rename.old-gone", !QFileInfo::exists(ops + QStringLiteral("/alpha.v")));
    check("ops.rename.new-present", QFileInfo::exists(ops + QStringLiteral("/beta.v")));
    check("ops.rename.tree-old-gone", !treeContains(model, ops, QStringLiteral("alpha.v")));
    check("ops.rename.tree-new", treeContains(model, ops, QStringLiteral("beta.v")));
    check("ops.rename.consistent", diskAndTreeAgree(model, ops));

    check("ops.rename-dup-refused", !widget.renameEntry(ops + QStringLiteral("/beta.v"),
                                                       QStringLiteral("rtl"), &error));
    check("ops.rename-dup.error", !error.isEmpty(), error);
    check("ops.rename-dup.no-change", QFileInfo::exists(ops + QStringLiteral("/beta.v")));
    check("ops.rename-dup.consistent", diskAndTreeAgree(model, ops));

    check("ops.delete-file", widget.deleteEntry(ops + QStringLiteral("/beta.v"), false, &error), error);
    check("ops.delete-file.gone", !QFileInfo::exists(ops + QStringLiteral("/beta.v")));
    check("ops.delete-file.tree-gone", !treeContains(model, ops, QStringLiteral("beta.v")));
    check("ops.delete-file.consistent", diskAndTreeAgree(model, ops));

    check("ops.create-nested", widget.createFolder(ops, QStringLiteral("sub"), &error), error);
    check("ops.create-nested-file",
          widget.createFile(ops + QStringLiteral("/sub"), QStringLiteral("inner.v"), &error), error);
    const QModelIndex subIndex = model->indexForPath(ops + QStringLiteral("/sub"), true);
    check("ops.sub-index", subIndex.isValid());
    widget.view()->setExpanded(subIndex, true);
    model->ensureLoaded(subIndex);
    const int rowsBeforeFolderDelete = model->loadedRows();
    check("ops.delete-folder", widget.deleteEntry(ops + QStringLiteral("/sub"), true, &error), error);
    check("ops.delete-folder.gone", !QFileInfo::exists(ops + QStringLiteral("/sub")));
    check("ops.delete-folder.consistent", diskAndTreeAgree(model, ops));
    check("ops.delete-folder.rows-shrunk", model->loadedRows() < rowsBeforeFolderDelete,
          QStringLiteral("%1 -> %2").arg(rowsBeforeFolderDelete).arg(model->loadedRows()));

    check("ops.delete-root-refused", !widget.deleteEntry(ops, true, &error));
    check("ops.delete-root.error",
          error.contains(QStringLiteral("open"), Qt::CaseInsensitive), error);

    check("ops.locked-folder", widget.createFolder(ops, QStringLiteral("locked"), &error), error);
    check("ops.locked-file",
          widget.createFile(ops + QStringLiteral("/locked"), QStringLiteral("keep.v"), &error), error);
    const bool isRoot = ::geteuid() == 0;
    if (!isRoot) {
        QFile::setPermissions(ops + QStringLiteral("/locked"),
                              QFile::ReadOwner | QFile::ExeOwner);
        check("ops.delete-denied-refused", !widget.deleteEntry(
                  ops + QStringLiteral("/locked/keep.v"), false, &error));
        check("ops.delete-denied.error", !error.isEmpty(), error);
        check("ops.delete-denied.still-there",
              QFileInfo::exists(ops + QStringLiteral("/locked/keep.v")));
        check("ops.delete-denied.no-phantom",
              treeContains(model, ops + QStringLiteral("/locked"), QStringLiteral("keep.v")));
    } else {
        std::printf("CHECK ops.delete-denied SKIPPED running-as-root\n");
    }
    QFile::setPermissions(ops + QStringLiteral("/locked"),
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    // The disk stays the source of truth: a file created behind the panel's
    // back is invisible until the folder is re-read, then it appears.
    check("refresh.fixture", writeFile(ops + QStringLiteral("/external.v")));
    check("refresh.hidden-until-refresh", !treeContains(model, ops, QStringLiteral("external.v")));
    model->refreshDirectory(ops);
    check("refresh.appears", treeContains(model, ops, QStringLiteral("external.v")));
    check("refresh.consistent", diskAndTreeAgree(model, ops));

    widget.closeDirectory();
    pump(20);
    check("widget.close.no-root", widget.model()->rootState() == FileTreeModel::RootState::NoRoot);
}

// ---------------------------------------------------------------------------
// panel seam factory
// ---------------------------------------------------------------------------

void testPanelFactory(const QString &scratch)
{
    const QString root = scratch + QStringLiteral("/tree");
    QWidget *panel = vedit::workspace::makeFileTreePanel(root);
    auto *tree = qobject_cast<FileTreeWidget *>(panel);
    check("seam.factory.returns-widget", tree != nullptr);
    if (tree != nullptr) {
        check("seam.factory.root", tree->rootPath() == QDir(root).absolutePath(),
              tree->rootPath());
        check("seam.factory.rows", tree->model()->rowCount(tree->model()->index(0, 0)) == 4,
              QString::number(tree->model()->rowCount(tree->model()->index(0, 0))));
    }
    delete panel;

    QWidget *empty = vedit::workspace::makeFileTreePanel(QString());
    auto *emptyTree = qobject_cast<FileTreeWidget *>(empty);
    check("seam.factory.empty-root", emptyTree != nullptr
              && emptyTree->model()->rootState() == FileTreeModel::RootState::NoRoot);
    delete empty;
}

// ---------------------------------------------------------------------------
// permission-denied behaviour (run as a NON-root user -- see tools/in-container.sh;
// container root bypasses DAC, so the root run prints SKIPPED instead of a lie)
// ---------------------------------------------------------------------------

void testPermissionDenied(const QString &scratch)
{
    const int euid = static_cast<int>(::geteuid());
    std::printf("PERMISSIONS euid=%d\n", euid);
    if (euid == 0) {
        std::printf("CHECK perm.root-state SKIPPED running-as-root\n");
        std::printf("CHECK perm.child-diagnostic SKIPPED running-as-root\n");
        std::printf("CHECK perm.delete-refused SKIPPED running-as-root\n");
        return;
    }

    const QString locked = scratch + QStringLiteral("/perm-locked");
    QDir().mkpath(locked);
    check("perm.fixture-locked", QFile::setPermissions(locked, QFile::Permissions()));

    FileTreeWidget widget;
    widget.resize(520, 600);
    widget.show();
    pump(40);
    widget.openDirectory(locked);
    pump(40);
    check("perm.root-state", widget.model()->rootState() == FileTreeModel::RootState::PermissionDenied);
    check("perm.root-text",
          widget.emptyStateText().contains(QStringLiteral("permission denied"), Qt::CaseInsensitive),
          widget.emptyStateText());
    check("perm.root-visible", widget.emptyStateVisible());
    check("perm.root-no-rows", widget.model()->rowCount(QModelIndex()) == 0);
    QFile::setPermissions(locked, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    // A readable folder with an unreadable child: the child is diagnosed, never
    // silently presented as an empty folder.
    const QString parent = scratch + QStringLiteral("/perm-parent");
    QDir().mkpath(parent + QStringLiteral("/secret"));
    writeFile(parent + QStringLiteral("/open.v"));
    QFile::setPermissions(parent + QStringLiteral("/secret"), QFile::Permissions());

    FileTreeWidget childWidget;
    childWidget.resize(520, 600);
    childWidget.show();
    pump(30);
    QSignalSpy failedSpy(childWidget.model(), &FileTreeModel::loadFailed);
    childWidget.openDirectory(parent);
    pump(40);
    const QModelIndex secret = childWidget.model()->indexForPath(parent + QStringLiteral("/secret"), true);
    check("perm.child-index", secret.isValid());
    childWidget.model()->ensureLoaded(secret);
    pump(20);
    check("perm.child-populated", childWidget.model()->isPopulated(secret));
    check("perm.child-no-rows", childWidget.model()->rowCount(secret) == 0);
    check("perm.child-signalled", failedSpy.count() == 1, QString::number(failedSpy.count()));
    check("perm.child-tooltip",
          secret.data(Qt::ToolTipRole).toString().contains(QStringLiteral("permission denied"),
                                                           Qt::CaseInsensitive),
          secret.data(Qt::ToolTipRole).toString());
    QFile::setPermissions(parent + QStringLiteral("/secret"),
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    // Deleting inside a read-only folder must fail with a diagnostic and leave
    // both the disk and the tree unchanged.
    const QString readonly = scratch + QStringLiteral("/perm-readonly");
    QDir().mkpath(readonly);
    writeFile(readonly + QStringLiteral("/keep.v"));
    FileTreeWidget opWidget;
    opWidget.resize(520, 600);
    opWidget.show();
    pump(30);
    opWidget.openDirectory(readonly);
    pump(40);
    check("perm.fixture-readonly",
          QFile::setPermissions(readonly, QFile::ReadOwner | QFile::ExeOwner));
    QString error;
    check("perm.delete-refused", !opWidget.deleteEntry(readonly + QStringLiteral("/keep.v"),
                                                       false, &error));
    check("perm.delete-error", !error.isEmpty(), error);
    check("perm.delete-status", !opWidget.statusText().isEmpty(), opWidget.statusText());
    check("perm.delete-still-on-disk", QFileInfo::exists(readonly + QStringLiteral("/keep.v")));
    check("perm.delete-no-phantom",
          treeContains(opWidget.model(), readonly, QStringLiteral("keep.v")));
    opWidget.model()->refreshDirectory(readonly);
    check("perm.delete-tree-unchanged", diskAndTreeAgree(opWidget.model(), readonly));
    QFile::setPermissions(readonly, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-filetree-tests"));

    QString scratch = qEnvironmentVariable("VEDIT_FILETREE_SCRATCH");
    scratch = scratch.isEmpty() ? scratchRoot() : QDir(scratch).absolutePath();
    QDir top(scratch);
    if (top.exists()) {
        const QStringList entries =
            top.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
        for (const QString &entry : entries) {
            const QFileInfo info(top.filePath(entry));
            if (info.isDir() && !info.isSymbolicLink()) {
                QDir(info.absoluteFilePath()).removeRecursively();
            } else {
                QFile::remove(info.absoluteFilePath());
            }
        }
    } else if (!QDir().mkpath(scratch)) {
        std::printf("FATAL cannot create the scratch directory %s\n", qPrintable(scratch));
        return 2;
    }
    std::printf("SCRATCH %s\n", qPrintable(scratch));

    const QString suite = qEnvironmentVariable("VEDIT_FILETREE_SUITE");
    if (suite == QLatin1String("permissions")) {
        testPermissionDenied(scratch);
    } else {
        testFileKinds();
        testRootStates(scratch);
        testTreeAndInteraction(scratch);
        testLaziness(scratch);
        testOperations(scratch);
        testPanelFactory(scratch);
    }

    std::printf("FILETREE-TESTS checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
