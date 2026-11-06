// RM-4 probe/recheck: the interface panel's port activation against the REAL
// product seam, including the user's REAL sequence (restore project A, then
// File > Open Folder B, then jump).
//
// This is deliberately NOT a recording target. The chain exercised here is the
// product's own:
//
//   InterfacePanel::activatePortRow()       (double-click / Ctrl+double-click)
//     -> jump::jumpTo()                       (src/workspace/jump)
//     -> search::EditorJumpTarget::jumpTo()   (the ONE product target, installed
//        by installSearch())
//     -> EditorPane::openFile()               (the ONE open seam)
//     -> EditCoreProvider::openFile()         (KTextEditor)
//     -> View::setCursorPosition()
//
// and every check reads the cursor back from the KTextEditor view, so a jump
// that "succeeds" without landing on the identifier is a FAILURE here.
//
// Modes
//   rm4-jump-probe project <B>            jump in a project opened at startup
//   rm4-jump-probe seed <A>               open A, open a file, SAVE THE SESSION
//   rm4-jump-probe switch <B> <A>         start on A (restored), Open Folder B, jump
//
// Build: evidence/ux-batch-2/rm4-jump/probe/build.sh
// Run:   QT_QPA_PLATFORM=offscreen rm4-jump-probe ...
//
// Exit 0 = every check passed. For each gesture the probe prints the
// PASSED / RECEIVED / RESOLVED triple, because that triple is the artifact
// this defect needs (RM-4: "编辑器拒绝了跳转到 <file>:<line> 的请求").
#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "integration.h"
#include "jump/jump_registry.h"
#include "kernel/ktexteditor_provider.h"
#include "module_tree/interface_panel.h"
#include "module_tree/module_tree_integration.h"
#include "module_tree/module_tree_panel.h"
#include "panel_registry.h"
#include "search/search_integration.h"
#include "search_jump_target.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTableWidget>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>

#include <cstdio>
#include <functional>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("ok   %s%s\n", name,
                    detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    } else {
        ++g_failures;
        std::printf("FAIL %s%s\n", name,
                    detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    }
    std::fflush(stdout);
}

bool spinUntil(const std::function<bool()> &done, int timeoutMs)
{
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(20);
    bool ok = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (done()) {
            ok = true;
            loop.quit();
        }
    });
    poll.start();
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return ok && done();
}

/// One 1-based source position, read from the FIXTURE TEXT (never from the
/// catalog answer), so the assertion cannot be circular.
struct Expected {
    QString file;
    int line = 0;
    int column = 0;
    QString token; ///< the identifier that must sit UNDER the cursor
};


/// A fixture may keep its two files at the top level or in `rtl/`; the catalog
/// and the ground truth must use the same one.
QString fixtureFile(const QString &root, const QString &name)
{
    const QString top = root + QLatin1Char('/') + name;
    if (QFileInfo(top).isFile()) {
        return top;
    }
    const QString nested = root + QStringLiteral("/rtl/") + name;
    return QFileInfo(nested).isFile() ? nested : top;
}

/// The first line carrying `needle`; the column of `token` inside that line.
bool locate(const QString &path, const QString &needle, const QString &token, Expected *out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&file);
    int lineNumber = 0;
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        ++lineNumber;
        const int needleAt = line.indexOf(needle);
        if (needleAt < 0) {
            continue;
        }
        const int tokenAt = line.indexOf(token, needleAt);
        if (tokenAt < 0) {
            return false;
        }
        out->file = QFileInfo(path).absoluteFilePath();
        out->line = lineNumber;
        out->column = tokenAt + 1;
        out->token = token;
        return true;
    }
    return false;
}

/// The identifier that starts at 1-based `column` on 1-based `line`, or an empty
/// string when that position is not an identifier start.
QString identifierAt(const QString &path, int line, int column)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream stream(&file);
    QString text;
    for (int current = 1; current <= line; ++current) {
        if (stream.atEnd()) {
            return QString();
        }
        text = stream.readLine();
    }
    const int index = column - 1;
    if (index < 0 || index >= text.size()) {
        return QString();
    }
    const QChar first = text.at(index);
    if (!(first.isLetter() || first == QLatin1Char('_'))) {
        return QString();
    }
    int end = index;
    while (end < text.size()
           && (text.at(end).isLetterOrNumber() || text.at(end) == QLatin1Char('_'))) {
        ++end;
    }
    return text.mid(index, end - index);
}

QTreeWidgetItem *findTreeItem(QTreeWidget *tree, const QString &label)
{
    const auto walk = [&label](QTreeWidgetItem *item, const auto &self) -> QTreeWidgetItem * {
        if (item->text(0) == label) {
            return item;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            if (QTreeWidgetItem *found = self(item->child(index), self)) {
                return found;
            }
        }
        return nullptr;
    };
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (QTreeWidgetItem *found = walk(tree->topLevelItem(index), walk)) {
            return found;
        }
    }
    return nullptr;
}

void printTreeLabels(QTreeWidget *tree)
{
    std::printf("     tree rows:");
    const std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
        std::printf(" %s", item->text(0).toStdString().c_str());
        for (int index = 0; index < item->childCount(); ++index) {
            walk(item->child(index));
        }
    };
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        walk(tree->topLevelItem(index));
    }
    std::printf("\n");
    std::fflush(stdout);
}

int rowForPort(QTableWidget *table, const QString &portName)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem *item = table->item(row, 0);
        if (item != nullptr && item->text() == portName) {
            return row;
        }
    }
    return -1;
}

/// A real double click on the table viewport (the events a user sends): press,
/// double-click, release. `control` is the modifier the user holds.
bool doubleClickPortRow(QTableWidget *table, int row, bool control)
{
    if (table == nullptr || !table->isVisible() || !table->viewport()->isVisible()) {
        return false;
    }
    const QTableWidgetItem *item = table->item(row, 0);
    if (item == nullptr) {
        return false;
    }
    table->scrollToItem(item);
    QApplication::processEvents();
    const QRect rect = table->visualItemRect(item);
    if (rect.isEmpty()) {
        return false;
    }
    const Qt::KeyboardModifiers modifiers = control ? Qt::ControlModifier : Qt::NoModifier;
    QTest::mousePress(table->viewport(), Qt::LeftButton, modifiers, rect.center());
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, modifiers, rect.center());
    QTest::mouseRelease(table->viewport(), Qt::LeftButton, modifiers, rect.center());
    QApplication::processEvents();
    return true;
}


/// A real double click on a module-tree row (the events a user sends).
bool doubleClickTreeRow(QTreeWidget *tree, QTreeWidgetItem *item)
{
    if (tree == nullptr || item == nullptr || !tree->isVisible() || !tree->viewport()->isVisible()) {
        return false;
    }
    tree->scrollToItem(item);
    QApplication::processEvents();
    const QRect rect = tree->visualItemRect(item);
    if (rect.isEmpty()) {
        return false;
    }
    QTest::mousePress(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    QTest::mouseRelease(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    QApplication::processEvents();
    return true;
}

struct Harness {
    vedit::app::ApplicationShell *shell = nullptr;
    vedit::workspace::module_tree::ModuleTreePanel *treePanel = nullptr;
    vedit::workspace::module_tree::InterfacePanel *interfacePanel = nullptr;
    vedit::workspace::search::EditorJumpTarget *target = nullptr;
    vedit::app::EditorPane *pane = nullptr;
};


/// The MODULE TREE gesture (the user reported it failing at the same moment): a
/// real double click on the module row must land on the module DECLARATION.
void runModuleTreeGesture(Harness *h, const QString &moduleName, const Expected &declaration)
{
    QTreeWidgetItem *row = findTreeItem(h->treePanel->tree(), moduleName);
    check("rm4.module-tree.row", row != nullptr, moduleName);
    if (row == nullptr) {
        return;
    }
    const bool delivered = doubleClickTreeRow(h->treePanel->tree(), row);
    check("rm4.module-tree.gesture-delivered", delivered);
    const vedit::workspace::jump::JumpResult passed =
        vedit::workspace::jump::Registry::instance().lastResult();
    KTextEditor::View *view = vedit::editor::viewFor(h->pane->editorWidget());
    const KTextEditor::Cursor cursor =
        view != nullptr ? view->cursorPosition() : KTextEditor::Cursor::invalid();
    std::printf("     TREE PASSED   : %s:%d:%d (status=%s)\n", qPrintable(passed.filePath),
                passed.line, passed.column,
                vedit::workspace::jump::jumpStatusName(passed.status));
    std::printf("     TREE RESOLVED : %s cursor=%d:%d\n", qPrintable(h->pane->currentFilePath()),
                view != nullptr ? cursor.line() + 1 : -1,
                view != nullptr ? cursor.column() + 1 : -1);
    std::printf("     TREE PANEL    : %s\n", qPrintable(h->treePanel->statusText()));
    const QString landed =
        view != nullptr ? identifierAt(declaration.file, cursor.line() + 1, cursor.column() + 1)
                        : QString();
    check("rm4.module-tree.opened-declaration-file",
          QFileInfo(h->pane->currentFilePath()).canonicalFilePath()
              == QFileInfo(declaration.file).canonicalFilePath(),
          h->pane->currentFilePath());
    check("rm4.module-tree.cursor-on-definition",
          view != nullptr && cursor.line() + 1 == declaration.line && !landed.isEmpty()
              && landed == declaration.token,
          QStringLiteral("cursor=%1:%2 identifier='%3' expected line=%4")
              .arg(view != nullptr ? cursor.line() + 1 : -1)
              .arg(view != nullptr ? cursor.column() + 1 : -1)
              .arg(landed)
              .arg(declaration.line));
}

/// Runs the two gestures for the focused module's `portName` and asserts where
/// the editor really ended up.
void runGestures(Harness *h, const QString &portName, const Expected &declaration,
                 const Expected &instantiation)
{
    const int row = rowForPort(h->interfacePanel->table(), portName);
    check("rm4.port-row", row >= 0, portName);

    KTextEditor::View *view = vedit::editor::viewFor(h->pane->editorWidget());
    std::printf("     PANE BEFORE: file='%s' view=%s\n", qPrintable(h->pane->currentFilePath()),
                view != nullptr ? "KTextEditor" : "<none>");
    std::printf("     OPEN TABS  : %s\n",
                h->pane->openFilePaths().join(QLatin1String(" | ")).toStdString().c_str());

    struct Gesture {
        const char *name;
        bool control;
        Expected expected;
    };
    const Gesture gestures[] = {
        {"rm4.double-click.port-declaration", false, declaration},
        {"rm4.ctrl-double-click.instantiation", true, instantiation},
    };

    for (const Gesture &gesture : gestures) {
        std::printf("\n== %s (control=%d, expected %s:%d:%d token=%s)\n", gesture.name,
                    gesture.control ? 1 : 0, qPrintable(QFileInfo(gesture.expected.file).fileName()),
                    gesture.expected.line, gesture.expected.column,
                    qPrintable(gesture.expected.token));
        const bool delivered = doubleClickPortRow(h->interfacePanel->table(), row, gesture.control);
        check((QString::fromLatin1(gesture.name) + QStringLiteral(".gesture-delivered")).toLatin1(),
              delivered);

        const vedit::workspace::jump::JumpResult passed =
            vedit::workspace::jump::Registry::instance().lastResult();
        std::printf("     PASSED   : %s:%d:%d (status=%s)\n", qPrintable(passed.filePath),
                    passed.line, passed.column,
                    vedit::workspace::jump::jumpStatusName(passed.status));
        std::printf("     RECEIVED : %s:%d:%d\n", qPrintable(h->target->lastFilePath()),
                    h->target->lastLine(), h->target->lastColumn());
        std::printf("     SEAM-ERR : %s\n",
                    h->target->lastError().isEmpty() ? "<none>"
                                                     : qPrintable(h->target->lastError()));

        KTextEditor::View *currentView = vedit::editor::viewFor(h->pane->editorWidget());
        const QString resolvedFile = h->pane->currentFilePath();
        const KTextEditor::Cursor cursor =
            currentView != nullptr ? currentView->cursorPosition() : KTextEditor::Cursor::invalid();
        std::printf("     RESOLVED : %s cursor=%d:%d\n", qPrintable(resolvedFile),
                    currentView != nullptr ? cursor.line() + 1 : -1,
                    currentView != nullptr ? cursor.column() + 1 : -1);
        std::printf("     PANEL    : %s\n", qPrintable(h->interfacePanel->statusText()));

        const bool sameFile =
            !resolvedFile.isEmpty()
            && QFileInfo(resolvedFile).canonicalFilePath()
                   == QFileInfo(gesture.expected.file).canonicalFilePath();
        check((QString::fromLatin1(gesture.name) + QStringLiteral(".opened-file")).toLatin1(),
              sameFile,
              QStringLiteral("resolved=%1 expected=%2")
                  .arg(QFileInfo(resolvedFile).fileName(),
                       QFileInfo(gesture.expected.file).fileName()));
        const bool sameLine = currentView != nullptr && cursor.line() + 1 == gesture.expected.line;
        check((QString::fromLatin1(gesture.name) + QStringLiteral(".cursor-line")).toLatin1(),
              sameLine,
              QStringLiteral("observed=%1 expected=%2")
                  .arg(currentView != nullptr ? cursor.line() + 1 : -1)
                  .arg(gesture.expected.line));
        const QString landed =
            currentView != nullptr
                ? identifierAt(gesture.expected.file, cursor.line() + 1, cursor.column() + 1)
                : QString();
        check((QString::fromLatin1(gesture.name) + QStringLiteral(".cursor-on-identifier")).toLatin1(),
              !landed.isEmpty() && landed == gesture.expected.token,
              QStringLiteral("identifier-under-cursor='%1' expected='%2' column=%3")
                  .arg(landed, gesture.expected.token)
                  .arg(currentView != nullptr ? cursor.column() + 1 : -1));
        check((QString::fromLatin1(gesture.name) + QStringLiteral(".seam-delivered")).toLatin1(),
              passed.status == vedit::workspace::jump::JumpStatus::Delivered
                  && h->target->lastError().isEmpty(),
              h->target->lastError());
    }
}


/// The two SEAM guarantees the RM-4 fix adds, both driven through the REAL
/// product target (no fake): a relative catalog path is resolved under the
/// workspace root, and a genuine refusal carries WHAT and WHY into the panel's
/// own status line instead of the bare "the editor rejected the jump to X".
void runSeamChecks(Harness *h, const QString &projectRoot)
{
    // (C) A RELATIVE path: meaningless without a base, so the workspace root is
    // the base - never the process working directory.
    const QString relative = QStringLiteral("child.sv");
    const bool delivered = h->target->jumpTo(relative, 1, 34);
    const QString opened = h->pane->currentFilePath();
    std::printf("     RELATIVE   : requested='%s' delivered=%d opened='%s' error='%s'\n",
                qPrintable(relative), delivered ? 1 : 0, qPrintable(opened),
                qPrintable(h->target->lastError()));
    check("rm4.seam.relative-path.delivered", delivered, h->target->lastError());
    check("rm4.seam.relative-path.under-workspace-root",
          !opened.isEmpty()
              && QFileInfo(opened).canonicalFilePath()
                     == QFileInfo(fixtureFile(projectRoot, QStringLiteral("child.sv")))
                            .canonicalFilePath(),
          opened);


    // (A2) The same guarantee through the PANEL: a catalog whose `file` is
    // relative (a legitimate hand-off - the panel forwards the catalog verbatim)
    // must open under the workspace root, not under the process cwd.
    vedit::workspace::module_tree::ModuleCatalog relativeCatalog;
    relativeCatalog.valid = true;
    vedit::workspace::module_tree::ModuleDefinition relativeModule;
    relativeModule.name = QStringLiteral("rel");
    relativeModule.file = QStringLiteral("child.sv");
    relativeModule.line = 0;
    relativeModule.column = 0;
    vedit::workspace::module_tree::ModulePort relativePort;
    relativePort.name = QStringLiteral("clk");
    relativePort.file = QStringLiteral("child.sv");
    relativePort.line = 0;
    relativePort.column = 0;
    relativeModule.ports.append(relativePort);
    relativeCatalog.modules.append(relativeModule);
    h->interfacePanel->setTreeSelection(QStringLiteral("rel"));
    h->interfacePanel->setCatalog(relativeCatalog);
    const bool relativeActivated = h->interfacePanel->activatePortRow(0, false);
    check("rm4.seam.panel.relative-catalog-path-delivered", relativeActivated,
          h->interfacePanel->statusText());
    check("rm4.seam.panel.relative-catalog-path-opened",
          QFileInfo(h->pane->currentFilePath()).canonicalFilePath()
              == QFileInfo(fixtureFile(projectRoot, QStringLiteral("child.sv"))).canonicalFilePath(),
          h->pane->currentFilePath());

    // (A3) THE PATH RULE: an ABSOLUTE path OUTSIDE the workspace opens as given
    // (a filelist may name files above the opened folder), and a missing
    // absolute path refuses WITH the rule that was applied.
    {
        const QString outsideDir = QDir::tempPath() + QStringLiteral("/rm4b-outside");
        QDir().mkpath(outsideDir);
        const QString outside = outsideDir + QStringLiteral("/outside.sv");
        QFile file(outside);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write("module outside_mod;\nendmodule\n");
            file.close();
        }
        const bool outsideDelivered = h->target->jumpTo(outside, 1, 8);
        std::printf("     OUTSIDE    : requested='%s' delivered=%d opened='%s' error='%s'\n",
                    qPrintable(outside), outsideDelivered ? 1 : 0,
                    qPrintable(h->pane->currentFilePath()), qPrintable(h->target->lastError()));
        check("rm4.seam.outside-workspace.absolute-path-delivered", outsideDelivered,
              h->target->lastError());
        check("rm4.seam.outside-workspace.opens-as-given",
              QFileInfo(h->pane->currentFilePath()).canonicalFilePath()
                  == QFileInfo(outside).canonicalFilePath(),
              h->pane->currentFilePath());

        const QString missing = outsideDir + QStringLiteral("/absent.sv");
        const bool missingDelivered = h->target->jumpTo(missing, 1, 1);
        const QString missingError = h->target->lastError();
        std::printf("     MISSING    : requested='%s' delivered=%d error='%s'\n",
                    qPrintable(missing), missingDelivered ? 1 : 0, qPrintable(missingError));
        check("rm4.seam.missing-absolute.refused", !missingDelivered);
        check("rm4.seam.missing-absolute.names-the-path",
              missingError.contains(missing), missingError);
        check("rm4.seam.missing-absolute.names-the-rule",
              missingError.contains(QStringLiteral("absolute path opened as given")),
              missingError);
    }

    // (B) A GENUINE refusal (the catalog names a file that is not on disk) must
    // still be a refusal - and it must SAY what failed and why.
    vedit::workspace::module_tree::ModuleCatalog ghost;
    ghost.valid = true;
    vedit::workspace::module_tree::ModuleDefinition module;
    module.name = QStringLiteral("ghost");
    module.file = QStringLiteral("/nonexistent-rm4/ghost.sv");
    module.line = 0;
    module.column = 0;
    vedit::workspace::module_tree::ModulePort port;
    port.name = QStringLiteral("p");
    port.file = module.file;
    port.line = 0;
    port.column = 0;
    module.ports.append(port);
    ghost.modules.append(module);
    h->interfacePanel->setTreeSelection(QStringLiteral("ghost"));
    h->interfacePanel->setCatalog(ghost);
    // NO processEvents() here ON PURPOSE: the holder's caret (250 ms) and
    // project-model (1 s) timers push the REAL catalog back, so the ghost answer
    // must be activated in the same synchronous turn that rendered it. Both
    // setCatalog() and activatePortRow() are synchronous.
    std::printf("     GHOST FOCUS: '%s' rows=%d\n",
                qPrintable(h->interfacePanel->focusedModuleName()),
                h->interfacePanel->table()->rowCount());
    const bool activated = h->interfacePanel->activatePortRow(0, false);
    const vedit::workspace::jump::JumpResult result =
        vedit::workspace::jump::Registry::instance().lastResult();
    const QString status = h->interfacePanel->statusText();
    std::printf("     GHOST      : activated=%d status=%s\n", activated ? 1 : 0,
                vedit::workspace::jump::jumpStatusName(result.status));
    std::printf("     PANEL      : %s\n", qPrintable(status));
    std::printf("     REASON     : %s\n", qPrintable(result.reason));
    check("rm4.seam.refusal.still-a-refusal",
          !activated && result.status == vedit::workspace::jump::JumpStatus::Rejected);
    check("rm4.seam.refusal.reason-names-what-failed",
          result.reason.contains(QStringLiteral("ghost.sv")), result.reason);
    check("rm4.seam.refusal.panel-carries-the-reason",
          !result.reason.isEmpty() && status.contains(result.reason), status);
    // The NEW diagnostic must be complete on its own: the panel line has to name
    // the full path the editor was asked for, the workspace root it was built
    // for, and the seam's reason - so the next real-machine failure names its
    // own cause without a debugger.
    check("rm4.seam.refusal.message-names-the-full-path",
          status.contains(QStringLiteral("/nonexistent-rm4/ghost.sv")), status);
    check("rm4.seam.refusal.message-names-the-workspace-root",
          status.contains(QStringLiteral("workspace root")) && status.contains(projectRoot),
          status);
    check("rm4.seam.refusal.message-names-the-reason",
          status.contains(QStringLiteral("not a regular file")), status);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-rm4-jump-probe"));

    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString primary =
        argc > 2 ? QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath() : QString();
    const QString secondary =
        argc > 3 ? QFileInfo(QString::fromLocal8Bit(argv[3])).absoluteFilePath() : QString();
    const bool keepState = argc > 4 && qstrcmp(argv[4], "keep-state") == 0;
    const bool switchMode = mode == QLatin1String("switch");
    const bool seedMode = mode == QLatin1String("seed");
    const bool seamMode = mode == QLatin1String("seam");
    const bool folderMode = mode == QLatin1String("folder");
    const bool multiDirMode = mode == QLatin1String("multidir");
    if (!seedMode && !switchMode && !seamMode && !folderMode && !multiDirMode
        && mode != QLatin1String("project")) {
        std::printf("FATAL: mode must be project | seed | switch | seam | folder | multidir\n");
        return 2;
    }
    if (!QFileInfo(primary).isDir()) {
        std::printf("FATAL: primary project directory is missing\n");
        return 2;
    }
    if ((seedMode || switchMode) && !QFileInfo(secondary).isDir()) {
        std::printf("FATAL: mode %s needs a SECOND project directory\n", qPrintable(mode));
        return 2;
    }

    // The state dir lives INSIDE the build tree so it survives across probe runs
    // (each container run gets a fresh /tmp). `seed` + `switch` use the SAME one
    // on purpose: that is how the session archive and the project lane's
    // `project-model.json` reach the second process, exactly like a real restart.
    const QString stateDir = QCoreApplication::applicationDirPath() + QStringLiteral("/state");
    if (!keepState && !seedMode) {
        QDir(stateDir).removeRecursively();
    }
    QDir().mkpath(stateDir);

    // `seed` starts on SECONDARY (the project the user had restored); `switch`
    // starts there too and then opens PRIMARY.
    // `folder` starts with NO project (a bare launch) and then opens PRIMARY
    // through the same public seam File > Open Folder uses.
    const QString startRoot = (seedMode || switchMode) ? secondary
                                                       : (folderMode ? QString() : primary);
    std::printf("mode=%s startRoot=%s\n", qPrintable(mode), qPrintable(startRoot));
    if (switchMode) {
        std::printf("switchTarget=%s\n", qPrintable(primary));
    }
    std::printf("stateDir=%s\n", qPrintable(stateDir));
    std::printf("cwd=%s shellRoot(initial)=%s\n", qPrintable(QDir::currentPath()),
                startRoot.isEmpty() ? "<none>" : qPrintable(startRoot));

    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = startRoot;
    options.restoreSession = true; // the product default (main.cpp)
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    vedit::app::ApplicationShell shell(options);
#if defined(VEDIT_HAVE_EDIT_CORE)
    // The product editor (main.cpp does exactly this before installPanels).
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
#else
    std::printf("FATAL: built without VEDIT_HAVE_EDIT_CORE: no KTextEditor seam\n");
    return 2;
#endif
    shell.startSession();

    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = startRoot;
    integration.stateDir = stateDir;
    // THE PRODUCT ENTRY POINT (main.cpp calls exactly this): every lane installs,
    // including the search lane that registers the ONE real jump target, the
    // project lane that persists `project-model.json` (which the catalog client
    // prefers over its own expansion) and the module tree's two docks. Installing
    // a subset would hide a cross-lane interaction, so the probe never does.
    vedit::workspace::installPanels(shell, integration);

    shell.resize(1400, 900);
    shell.show();
    QApplication::processEvents();

    Harness harness;
    harness.shell = &shell;
    harness.target = shell.findChild<vedit::workspace::search::EditorJumpTarget *>();
    check("rm4.jump-target-is-the-product-target",
          harness.target != nullptr && vedit::workspace::jump::hasTarget(),
          harness.target != nullptr ? QStringLiteral("EditorJumpTarget installed")
                                    : QStringLiteral("no EditorJumpTarget child of the shell"));
    if (harness.target == nullptr) {
        return 1;
    }

    QDockWidget *treeDock = shell.panels() != nullptr
                                ? shell.panels()->dock(QStringLiteral("module-tree"))
                                : nullptr;
    harness.treePanel = treeDock != nullptr
                            ? qobject_cast<vedit::workspace::module_tree::ModuleTreePanel *>(
                                  treeDock->widget())
                            : nullptr;
    check("rm4.module-tree-dock", harness.treePanel != nullptr);
    if (harness.treePanel == nullptr) {
        return 1;
    }
    QDockWidget *interfaceDock = shell.panels()->dock(QStringLiteral("interface-panel"));
    harness.interfacePanel =
        interfaceDock != nullptr
            ? qobject_cast<vedit::workspace::module_tree::InterfacePanel *>(interfaceDock->widget())
            : nullptr;
    check("rm4.interface-dock", harness.interfacePanel != nullptr);
    if (harness.interfacePanel == nullptr) {
        return 1;
    }
    harness.pane = shell.editorArea() != nullptr ? shell.editorArea()->activePane() : nullptr;
    check("rm4.active-pane", harness.pane != nullptr);
    if (harness.pane == nullptr) {
        return 1;
    }

    // A bare launch (folder mode) has no catalog yet ON PURPOSE: it is the
    // "start with no project, then File > Open Folder" shape, so the catalog is
    // waited for after the root change instead.
    bool painted = folderMode;
    if (!folderMode) {
        painted = spinUntil([&]() { return harness.treePanel->tree()->topLevelItemCount() > 0; },
                            180000);
        check("rm4.catalog-painted", painted, harness.treePanel->statusText());
        if (!painted) {
            return 1;
        }
        printTreeLabels(harness.treePanel->tree());
    }

    if (seamMode) {
        runSeamChecks(&harness, primary);
        std::printf("\nchecks=%d failures=%d\n", g_checks, g_failures);
        return g_failures == 0 ? 0 : 1;
    }

    if (seedMode) {
        // Persist the SESSION with one real document open, like a user who had
        // project A open when they closed the app.
        const QStringList candidates =
            QDir(secondary).entryList({QStringLiteral("*.sv")}, QDir::Files);
        if (!candidates.isEmpty()) {
            QString openError;
            const bool opened = harness.pane->openFile(
                secondary + QLatin1Char('/') + candidates.first(), &openError);
            check("rm4.seed.document-open", opened, openError);
        }
        QString saveError;
        check("rm4.seed.session-saved", shell.saveSession(&saveError), saveError);
        std::printf("checks=%d failures=%d\n", g_checks, g_failures);
        return g_failures == 0 ? 0 : 1;
    }

    if (folderMode) {
        // A bare launch (no project) then File > Open Folder B.
        std::printf("\n== folder: no project -> '%s' (cwd='%s')\n", qPrintable(primary),
                    qPrintable(QDir::currentPath()));
        shell.setProjectRoot(primary);
        QApplication::processEvents();
        const bool opened =
            spinUntil([&]() {
                return findTreeItem(harness.treePanel->tree(), QStringLiteral("child")) != nullptr;
            },
            180000);
        check("rm4.folder.catalog-is-the-opened-project", opened,
              QStringLiteral("root=%1 status=%2").arg(shell.projectRoot(),
                                                      harness.treePanel->statusText()));
        printTreeLabels(harness.treePanel->tree());
    }

    if (switchMode) {
        // THE USER'S SEQUENCE: the session restored project A; now File > Open
        // Folder B (the dialog itself only validates and calls setProjectRoot).
        std::printf("\n== switch: project root '%s' -> '%s' (cwd='%s')\n",
                    qPrintable(shell.projectRoot()), qPrintable(primary),
                    qPrintable(QDir::currentPath()));
        shell.setProjectRoot(primary);
        QApplication::processEvents();
        const bool switched =
            spinUntil([&]() {
                return findTreeItem(harness.treePanel->tree(), QStringLiteral("child")) != nullptr
                       && findTreeItem(harness.treePanel->tree(), QStringLiteral("alpha")) == nullptr;
            },
            180000);
        check("rm4.switch.catalog-is-the-new-project", switched,
              QStringLiteral("root=%1 status=%2").arg(shell.projectRoot(),
                                                      harness.treePanel->statusText()));
        printTreeLabels(harness.treePanel->tree());
    }

    // Focus rule (a) through the REAL wiring: selecting the module row in the
    // module tree makes the interface panel follow it.
    QTreeWidgetItem *childRow = findTreeItem(harness.treePanel->tree(), QStringLiteral("child"));
    check("rm4.module-row", childRow != nullptr);
    if (childRow == nullptr) {
        return 1;
    }
    harness.treePanel->tree()->setCurrentItem(childRow);
    const bool focused = spinUntil(
        [&]() {
            return harness.interfacePanel->focusedModuleName() == QLatin1String("child")
                   && harness.interfacePanel->table()->rowCount() > 0;
        },
        10000);
    check("rm4.interface-focus", focused,
          QStringLiteral("focused=%1 rows=%2")
              .arg(harness.interfacePanel->focusedModuleName())
              .arg(harness.interfacePanel->table()->rowCount()));
    if (!focused) {
        return 1;
    }
    for (int row = 0; row < harness.interfacePanel->table()->rowCount(); ++row) {
        const QTableWidgetItem *item = harness.interfacePanel->table()->item(row, 0);
        std::printf("     port row %d = %s\n", row,
                    item != nullptr ? item->text().toStdString().c_str() : "<null>");
    }

    // Ground truth from the fixture text, never from the catalog answer.
    Expected declaration;
    Expected instantiation;
    const bool haveDeclaration =
        locate(fixtureFile(primary, QStringLiteral("child.sv")), QStringLiteral("input"),
               QStringLiteral("clk"), &declaration);
    const bool haveInstantiation =
        locate(fixtureFile(primary, QStringLiteral("top.sv")), QStringLiteral("u_child"),
               QStringLiteral("child"), &instantiation);
    check("rm4.ground-truth.port-declaration", haveDeclaration,
          QStringLiteral("%1:%2:%3")
              .arg(QFileInfo(declaration.file).fileName())
              .arg(declaration.line)
              .arg(declaration.column));
    check("rm4.ground-truth.instantiation", haveInstantiation,
          QStringLiteral("%1:%2:%3")
              .arg(QFileInfo(instantiation.file).fileName())
              .arg(instantiation.line)
              .arg(instantiation.column));
    // Ctrl+double-click lands on the INSTANCE NAME (`u_child`), which is an
    // identifier of the instantiation statement; the check states where the ABI
    // really points instead of pretending it points at the module name.
    instantiation.token = QStringLiteral("u_child");

    runGestures(&harness, QStringLiteral("clk"), declaration, instantiation);

    // The user reported BOTH panels failing at the same moment, so both gestures
    // are driven here: the interface panel above, the module tree now.
    Expected moduleDeclaration;
    const bool haveModuleDeclaration =
        locate(fixtureFile(primary, QStringLiteral("child.sv")), QStringLiteral("module"),
               QStringLiteral("child"), &moduleDeclaration);
    check("rm4.ground-truth.module-declaration", haveModuleDeclaration,
          QStringLiteral("%1:%2:%3")
              .arg(QFileInfo(moduleDeclaration.file).fileName())
              .arg(moduleDeclaration.line)
              .arg(moduleDeclaration.column));
    runModuleTreeGesture(&harness, QStringLiteral("child"), moduleDeclaration);

    if (multiDirMode) {
        // TWO PANELS, TWO DIRECTORIES: the same tree, whose modules live in
        // `rtl/` and `tb/`, must reach each module's REAL file. This is the
        // two-panel agreement check for the RM-4 path bug.
        Expected tbDeclaration;
        const bool haveTb = locate(primary + QStringLiteral("/tb/tb.sv"),
                                   QStringLiteral("module"), QStringLiteral("tb"),
                                   &tbDeclaration);
        check("rm4.ground-truth.tb-declaration", haveTb,
              QStringLiteral("%1:%2:%3")
                  .arg(QFileInfo(tbDeclaration.file).fileName())
                  .arg(tbDeclaration.line)
                  .arg(tbDeclaration.column));
        runModuleTreeGesture(&harness, QStringLiteral("tb"), tbDeclaration);
    }

    std::printf("\nchecks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
