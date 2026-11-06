// T19 lane self-test — session restore on the REAL product shell + REAL
// KTextEditor provider. Two modes:
//
//   session-probe --checks                       headless assertions (offscreen)
//   session-probe --screenshots <dir> [--archive-out <file>]
//                                                + PNG evidence (xcb) + archive sample
//
// It links the product's own static libraries (see build.sh), so what it
// observes is the shipped code, not a copy of it.
#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "panel_registry.h"
#include "session_store.h"

#include "ktexteditor_provider.h"

#include "project_state.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QScreen>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

#include <cstdio>

using namespace vedit::app;

namespace {

int failures = 0;

void check(const char *id, bool ok, const QString &detail = QString())
{
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", id,
                detail.isEmpty() ? "" : " — ", qPrintable(detail));
    if (!ok) {
        ++failures;
    }
}

void pump(int rounds = 8)
{
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    }
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

QString g_shotDir;
QString g_dialogAnswer;
QString g_dialogShotName;

// A QStatusBar draws a temporary message over the normal widget area, so a grab
// taken mid-transition records both texts on top of each other.
bool saveShot(QWidget *widget, const QString &path)
{
    const bool ok = widget != nullptr && widget->grab().save(path);
    std::printf("SHOT %s %s\n", ok ? "OK" : "FAIL", qPrintable(path));
    return ok;
}

void driveModalDialog(int attempt = 0)
{
    QWidget *modal = QApplication::activeModalWidget();
    if (modal == nullptr) {
        if (attempt < 60) {
            QTimer::singleShot(50, [attempt]() { driveModalDialog(attempt + 1); });
        }
        return;
    }
    if (!g_shotDir.isEmpty() && !g_dialogShotName.isEmpty()) {
        saveShot(modal, g_shotDir + QLatin1Char('/') + g_dialogShotName);
    }
    auto *box = qobject_cast<QMessageBox *>(modal);
    if (box == nullptr) {
        modal->close();
        return;
    }
    for (QAbstractButton *button : box->buttons()) {
        if (button->text() == g_dialogAnswer) {
            button->click();
            return;
        }
    }
    box->reject();
}

void armDialogAnswer(const QString &answer, const QString &shotName = QString())
{
    g_dialogAnswer = answer;
    g_dialogShotName = shotName;
    QTimer::singleShot(80, []() { driveModalDialog(); });
}

QByteArray moduleFile(const QString &module, int lines)
{
    QByteArray bytes = "module " + module.toUtf8() + ";\n";
    for (int line = 4; line <= lines; ++line) {
        bytes += QByteArray("  // line ") + QByteArray::number(line) + "\n";
    }
    bytes += "endmodule\n";
    return bytes;
}

// The product sequence (src/app/main.cpp): build the shell with the restore
// deferred, install the real provider, THEN apply the session.
ApplicationShell *makeShell(const QString &stateDir, const QString &projectRoot, bool restore)
{
    ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = projectRoot;
    options.restoreSession = restore;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    auto *shell = new ApplicationShell(options);
    shell->setEditorProvider(vedit::editor::makeEditCoreProvider());
    shell->startSession();
    shell->show();
    pump();
    return shell;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-session-probe"));

    bool screenshots = false;
    bool corruptAPosition = false;
    QString archiveOut;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--screenshots") && i + 1 < argc) {
            screenshots = true;
            g_shotDir = QString::fromLocal8Bit(argv[i + 1]);
        } else if (arg == QLatin1String("--archive-out") && i + 1 < argc) {
            archiveOut = QString::fromLocal8Bit(argv[i + 1]);
        } else if (arg == QLatin1String("--corrupt-a-position")) {
            // T51 fail-first control: smash the HIDDEN tab's archived caret/scroll
            // before the restart. The new A assertions must then go RED; a run that
            // stays green would mean the probe never looked at A.
            corruptAPosition = true;
        }
    }
    if (screenshots && !QDir().mkpath(g_shotDir)) {
        std::printf("FAIL shot-dir\n");
        return 1;
    }

    QTemporaryDir temp;
    check("probe.fixture-dir", temp.isValid());
    if (!temp.isValid()) {
        return 1;
    }
    const QString root = temp.path() + QStringLiteral("/project");
    const QString stateDir = temp.path() + QStringLiteral("/state");
    QDir().mkpath(root);
    const QString fileA = root + QStringLiteral("/alpha.sv");
    const QString fileB = root + QStringLiteral("/bravo.sv");
    const QString fileC = root + QStringLiteral("/charlie.sv");
    check("probe.fixtures",
          writeFile(fileA, moduleFile(QStringLiteral("alpha"), 60))
              && writeFile(fileB, moduleFile(QStringLiteral("bravo"), 80))
              && writeFile(fileC, moduleFile(QStringLiteral("charlie"), 40)));

    // The persisted project model: root, imported filelists and the member file
    // set live in project-model.json (project lane), NOT in the session archive.
    vedit::workspace::project::ProjectState project;
    project.root = root;
    project.filelists = {root + QStringLiteral("/files.f")};
    project.variables = {QStringLiteral("SIM=1")};
    project.files = {fileA, fileB, fileC};
    project.includeDirs = {root + QStringLiteral("/include")};
    project.defines = {QStringLiteral("WIDTH=8")};
    vedit::workspace::project::ProjectStateStore projectStore(stateDir);
    QString projectError;
    check("project-model.save", projectStore.save(project, &projectError), projectError);

    // --- 1) a working session: A+B as tabs of pane 1, C in pane 2 ----------
    ApplicationShell *first = makeShell(stateDir, root, /*restore=*/false);
    check("before.pane-count", first->editorArea()->paneCount() == 1);
    QString openError;
    check("before.open-a", first->openPaths({fileA}, &openError), openError);
    check("before.open-b", first->openPaths({fileB}, &openError), openError);
    EditorPane *paneOne = first->editorArea()->activePane();
    check("before.pane1-two-tabs", paneOne != nullptr && paneOne->tabCount() == 2,
          paneOne != nullptr ? QStringLiteral("tabs=%1").arg(paneOne->tabCount()) : QString());
    EditorPane *paneTwo = first->editorArea()->splitActive(Qt::Horizontal);
    pump();
    check("before.pane2", paneTwo != nullptr && first->editorArea()->paneCount() == 2);
    if (paneTwo != nullptr) {
        check("before.open-c", paneTwo->openFile(fileC, &openError), openError);
    }
    first->panels()->showPanel(QStringLiteral("file-tree"), first);
    first->panels()->showPanel(QStringLiteral("outline"), first);
    // restoreGeometry() clamps to the available screen, so the "before" size must
    // fit this platform: offscreen has 800x600, Xvfb 1500x1000. Bound the wanted
    // size and then assert the EXACT round-trip of whatever was really used (an
    // oversized window would report the platform's clamp, not the archive's).
    const QSize screenBound =
        QGuiApplication::primaryScreen() != nullptr
            ? QGuiApplication::primaryScreen()->availableGeometry().size() - QSize(40, 80)
            : QSize(1000, 700);
    first->resize(QSize(1200, 860).boundedTo(screenBound));
    pump();

    // --- AC-10.1.3.2, the OTHER half: A is the HIDDEN tab after the restart ---
    // Give A a distinctive caret/scroll while it IS the visible tab, so the archive
    // carries it; after the restart A is a background page whose position can only
    // be applied through the deferred path (applyPendingPosition on its Show).
    first->editorArea()->setActivePane(paneOne);
    paneOne->setActiveTab(0);
    pump();
    KTextEditor::View *viewA = vedit::editor::viewFor(paneOne->editorWidget());
    check("before.a-view", viewA != nullptr);
    if (viewA == nullptr) {
        return 1;
    }
    const int caretALine = 12;
    const int caretAColumn = 3;
    viewA->setCursorPosition(KTextEditor::Cursor(caretALine, caretAColumn));
    viewA->setScrollPosition(KTextEditor::Cursor(6, 0));
    pump();
    const int capturedACaretLine = viewA->cursorPosition().line();
    const int capturedACaretColumn = viewA->cursorPosition().column();
    const int capturedAScroll = viewA->firstDisplayedLine(KTextEditor::View::RealLine);
    check("before.a-position", capturedACaretLine == caretALine && capturedACaretColumn == caretAColumn,
          QStringLiteral("caret=%1:%2 scroll=%3")
              .arg(capturedACaretLine)
              .arg(capturedACaretColumn)
              .arg(capturedAScroll));
    if (screenshots) {
        first->statusBar()->clearMessage();
        pump();
        saveShot(first, g_shotDir + QStringLiteral("/19-before-restart-a-active.png"));
    }

    // Activate B in pane 1, put the caret on a known line and scroll to the middle.
    paneOne->setActiveTab(1);
    pump();
    KTextEditor::View *viewB = vedit::editor::viewFor(paneOne->editorWidget());
    check("before.b-view", viewB != nullptr);
    if (viewB == nullptr) {
        return 1;
    }
    const int caretLine = 45;
    const int caretColumn = 5;
    viewB->setCursorPosition(KTextEditor::Cursor(caretLine, caretColumn));
    viewB->setScrollPosition(KTextEditor::Cursor(30, 0));
    pump();
    const int capturedCaretLine = viewB->cursorPosition().line();
    const int capturedCaretColumn = viewB->cursorPosition().column();
    const int capturedScroll = viewB->firstDisplayedLine(KTextEditor::View::RealLine);
    check("before.b-scrolled", capturedScroll > 5,
          QStringLiteral("first-displayed-line=%1").arg(capturedScroll));
    const QSize beforeSize = first->size();
    const QPoint beforePos = first->pos();
    const QString activePaneId = paneOne->paneId();
    std::printf("BEFORE caret=%d:%d scroll=%d size=%dx%d pos=%d,%d active=%s\n", capturedCaretLine,
                capturedCaretColumn, capturedScroll, beforeSize.width(), beforeSize.height(),
                beforePos.x(), beforePos.y(), qPrintable(activePaneId));
    if (screenshots) {
        first->statusBar()->clearMessage();
        pump();
        saveShot(first, g_shotDir + QStringLiteral("/20-before-restart.png"));
    }

    // --- 2) archive on the way out ----------------------------------------
    QString saveError;
    check("before.save-session", first->saveSession(&saveError), saveError);
    const QString sessionFile = stateDir + QStringLiteral("/session.json");
    const QByteArray archiveBytes = readFile(sessionFile);
    check("archive.written", !archiveBytes.isEmpty(), sessionFile);
    if (!archiveOut.isEmpty()) {
        check("archive.sample-copied", writeFile(archiveOut, archiveBytes), archiveOut);
    }
    const QJsonObject archive = QJsonDocument::fromJson(archiveBytes).object();
    check("archive.version", archive.value(QStringLiteral("version")).toInt() == 2,
          QString::number(archive.value(QStringLiteral("version")).toInt()));
    check("archive.project-root",
          archive.value(QStringLiteral("project")).toString() == root,
          archive.value(QStringLiteral("project")).toString());
    check("archive.panels",
          archive.value(QStringLiteral("panels")).toArray().size() == 2);
    const QJsonObject layout = archive.value(QStringLiteral("editorLayout")).toObject();
    check("archive.layout-version", layout.value(QStringLiteral("version")).toInt() == 2);
    check("archive.layout-active", layout.value(QStringLiteral("active")).toString() == activePaneId);
    const QJsonObject layoutRoot = layout.value(QStringLiteral("root")).toObject();
    check("archive.layout-split", layoutRoot.value(QStringLiteral("type")).toString() == QLatin1String("split")
              && layoutRoot.value(QStringLiteral("orientation")).toString() == QLatin1String("horizontal"));
    const QJsonObject firstLeaf = layoutRoot.value(QStringLiteral("first")).toObject();
    const QJsonArray leafDocs = firstLeaf.value(QStringLiteral("documents")).toArray();
    check("archive.leaf-documents", leafDocs.size() == 2,
          QStringLiteral("documents=%1").arg(leafDocs.size()));
    check("archive.leaf-active-tab", firstLeaf.value(QStringLiteral("activeTab")).toInt() == 1,
          QString::number(firstLeaf.value(QStringLiteral("activeTab")).toInt()));
    check("archive.leaf-provider",
          firstLeaf.value(QStringLiteral("provider")).toString() == QLatin1String("ktexteditor"));
    bool archiveHasCaret = false;
    bool archiveHasScroll = false;
    for (const QJsonValue &value : leafDocs) {
        const QJsonObject entry = value.toObject();
        const QJsonObject caret = entry.value(QStringLiteral("caret")).toObject();
        if (caret.value(QStringLiteral("line")).toInt() == capturedCaretLine
            && caret.value(QStringLiteral("column")).toInt() == capturedCaretColumn
            && entry.value(QStringLiteral("scroll")).toInt() == capturedScroll) {
            archiveHasCaret = true;
            archiveHasScroll = true;
        }
    }
    check("archive.caret-and-scroll-match-live-view", archiveHasCaret && archiveHasScroll,
          QStringLiteral("expected caret=%1:%2 scroll=%3")
              .arg(capturedCaretLine)
              .arg(capturedCaretColumn)
              .arg(capturedScroll));
    bool archiveHasAPosition = false;
    for (const QJsonValue &value : leafDocs) {
        const QJsonObject entry = value.toObject();
        const QJsonObject caret = entry.value(QStringLiteral("caret")).toObject();
        if (entry.value(QStringLiteral("path")).toString() == QFileInfo(fileA).absoluteFilePath()
            && caret.value(QStringLiteral("line")).toInt() == capturedACaretLine
            && caret.value(QStringLiteral("column")).toInt() == capturedACaretColumn
            && entry.value(QStringLiteral("scroll")).toInt() == capturedAScroll) {
            archiveHasAPosition = true;
        }
    }
    check("archive.a-caret-and-scroll", archiveHasAPosition,
          QStringLiteral("expected A caret=%1:%2 scroll=%3")
              .arg(capturedACaretLine)
              .arg(capturedACaretColumn)
              .arg(capturedAScroll));
    delete first;
    pump();

    if (corruptAPosition) {
        // T51 fail-first control. A is documents[0] of the first leaf; destroy its
        // caret AND its scroll in the archive on disk, then restart from that
        // archive. Everything else stays valid, so a RED run isolates exactly the
        // new hidden-tab assertions.
        QJsonObject broken = QJsonDocument::fromJson(readFile(sessionFile)).object();
        QJsonObject layoutBroken = broken.value(QStringLiteral("editorLayout")).toObject();
        QJsonObject rootBroken = layoutBroken.value(QStringLiteral("root")).toObject();
        QJsonObject leafBroken = rootBroken.value(QStringLiteral("first")).toObject();
        QJsonArray docsBroken = leafBroken.value(QStringLiteral("documents")).toArray();
        if (!docsBroken.isEmpty()) {
            QJsonObject entry = docsBroken.at(0).toObject();
            entry.insert(QStringLiteral("caret"),
                         QJsonObject{{QStringLiteral("line"), 0}, {QStringLiteral("column"), 0}});
            entry.insert(QStringLiteral("scroll"), 0);
            docsBroken.replace(0, entry);
            leafBroken.insert(QStringLiteral("documents"), docsBroken);
            rootBroken.insert(QStringLiteral("first"), leafBroken);
            layoutBroken.insert(QStringLiteral("root"), rootBroken);
            broken.insert(QStringLiteral("editorLayout"), layoutBroken);
            const bool written = writeFile(sessionFile, QJsonDocument(broken).toJson());
            check("failfirst.archive-corrupted", written);
        } else {
            check("failfirst.archive-corrupted", false);
        }
    }

    // --- 3) restart: a NEW process would see exactly this on disk ----------
    ApplicationShell *second = makeShell(stateDir, root, /*restore=*/true);
    check("after.pane-count", second->editorArea()->paneCount() == 2,
          QStringLiteral("panes=%1").arg(second->editorArea()->paneCount()));
    check("after.window-size", second->size() == beforeSize,
          QStringLiteral("now=%1x%2 want=%3x%4")
              .arg(second->width()).arg(second->height())
              .arg(beforeSize.width()).arg(beforeSize.height()));
    check("after.window-position", (second->pos() - beforePos).manhattanLength() <= 40,
          QStringLiteral("now=%1,%2 want=%3,%4")
              .arg(second->pos().x()).arg(second->pos().y())
              .arg(beforePos.x()).arg(beforePos.y()));
    EditorPane *restoredOne = nullptr;
    EditorPane *restoredTwo = nullptr;
    for (EditorPane *pane : second->editorArea()->panes()) {
        if (pane->paneId() == activePaneId) {
            restoredOne = pane;
        } else {
            restoredTwo = pane;
        }
    }
    check("after.pane-identity", restoredOne != nullptr && restoredTwo != nullptr);
    if (restoredOne == nullptr || restoredTwo == nullptr) {
        return 1;
    }
    check("after.pane1-two-tabs", restoredOne->tabCount() == 2,
          QStringLiteral("tabs=%1").arg(restoredOne->tabCount()));
    check("after.pane1-file-names",
          restoredOne->tabFilePath(0) == QFileInfo(fileA).absoluteFilePath()
              && restoredOne->tabFilePath(1) == QFileInfo(fileB).absoluteFilePath(),
          restoredOne->tabFilePath(0) + QStringLiteral(" | ") + restoredOne->tabFilePath(1));
    check("after.pane1-active-tab-is-b", restoredOne->activeTabIndex() == 1,
          QString::number(restoredOne->activeTabIndex()));
    check("after.pane2-file", restoredTwo->tabCount() == 1
              && restoredTwo->tabFilePath(0) == QFileInfo(fileC).absoluteFilePath(),
          restoredTwo->tabFilePath(0));
    check("after.active-pane", second->editorArea()->activePane() == restoredOne);
    check("after.project-root", second->projectLabel() == QFileInfo(root).fileName(),
          second->projectLabel());
    check("after.panel-file-tree", second->panels()->isVisible(QStringLiteral("file-tree")));
    check("after.panel-outline", second->panels()->isVisible(QStringLiteral("outline")));
    check("after.panel-terminal-hidden",
          !second->panels()->isVisible(QStringLiteral("terminal")));

    KTextEditor::View *restoredBView = vedit::editor::viewFor(restoredOne->editorWidget());
    check("after.b-view", restoredBView != nullptr);
    if (restoredBView != nullptr) {
        check("after.caret-line", restoredBView->cursorPosition().line() == capturedCaretLine,
              QStringLiteral("now=%1 want=%2")
                  .arg(restoredBView->cursorPosition().line())
                  .arg(capturedCaretLine));
        check("after.caret-column", restoredBView->cursorPosition().column() == capturedCaretColumn,
              QStringLiteral("now=%1 want=%2")
                  .arg(restoredBView->cursorPosition().column())
                  .arg(capturedCaretColumn));
        check("after.scroll-line",
              restoredBView->firstDisplayedLine(KTextEditor::View::RealLine) == capturedScroll,
              QStringLiteral("now=%1 want=%2")
                  .arg(restoredBView->firstDisplayedLine(KTextEditor::View::RealLine))
                  .arg(capturedScroll));
    }
    // --- the hidden tab: activate A and read ITS real KTextEditor view --------
    // This is the deferred path: A was a background page at restore time, so its
    // caret/scroll are applied when the page is first shown (applyPendingPosition
    // on the Show event). Reading it here is the only way to tell "restored" from
    // "never looked".
    restoredOne->setActiveTab(0);
    pump();
    check("after.a-file", restoredOne->currentFilePath() == QFileInfo(fileA).absoluteFilePath(),
          restoredOne->currentFilePath());
    KTextEditor::View *restoredAView = vedit::editor::viewFor(restoredOne->editorWidget());
    check("after.a-view", restoredAView != nullptr);
    if (restoredAView != nullptr) {
        check("after.a-caret-line", restoredAView->cursorPosition().line() == capturedACaretLine,
              QStringLiteral("now=%1 want=%2")
                  .arg(restoredAView->cursorPosition().line())
                  .arg(capturedACaretLine));
        check("after.a-caret-column", restoredAView->cursorPosition().column() == capturedACaretColumn,
              QStringLiteral("now=%1 want=%2")
                  .arg(restoredAView->cursorPosition().column())
                  .arg(capturedACaretColumn));
        check("after.a-scroll-line",
              restoredAView->firstDisplayedLine(KTextEditor::View::RealLine) == capturedAScroll,
              QStringLiteral("now=%1 want=%2")
                  .arg(restoredAView->firstDisplayedLine(KTextEditor::View::RealLine))
                  .arg(capturedAScroll));
    }
    if (screenshots) {
        second->statusBar()->clearMessage();
        pump();
        saveShot(second, g_shotDir + QStringLiteral("/21b-after-restart-a-active.png"));
    }
    // Back to B: the dirty-buffer scenario below must dirty the ACTIVE document.
    restoredOne->setActiveTab(1);
    pump();

    // The project FILE SET is the project lane's archive; it must survive too.
    bool projectOk = false;
    const vedit::workspace::project::ProjectState restoredProject = projectStore.load(&projectOk);
    check("after.project-model", projectOk && restoredProject.root == project.root
              && restoredProject.files == project.files
              && restoredProject.filelists == project.filelists
              && restoredProject.includeDirs == project.includeDirs
              && restoredProject.defines == project.defines,
          QStringLiteral("root=%1 files=%2").arg(restoredProject.root).arg(restoredProject.files.size()));
    if (screenshots) {
        second->statusBar()->clearMessage();
        pump();
        saveShot(second, g_shotDir + QStringLiteral("/21-after-restart.png"));
    }

    // --- 4) dirty buffer: the restart guard must never drop it -------------
    KTextEditor::Document *dirtyDocument =
        vedit::editor::documentFor(restoredOne->editorWidget());
    check("dirty.document", dirtyDocument != nullptr);
    if (dirtyDocument != nullptr) {
        const QByteArray onDiskBefore = readFile(fileB);
        dirtyDocument->setText(dirtyDocument->text() + QStringLiteral("// unsaved edit\n"));
        dirtyDocument->setModified(true);
        pump();
        check("dirty.state", restoredOne->isActiveTabModified());
        const QByteArray archiveBefore = readFile(sessionFile);
        if (QApplication::platformName() == QLatin1String("offscreen")) {
            // Headless has nobody to ask: Cancel, no write, window stays.
            check("dirty.guard-cancels", !second->confirmUnsavedDocuments());
            check("dirty.archive-not-written", readFile(sessionFile) == archiveBefore);
            check("dirty.disk-not-written", readFile(fileB) == onDiskBefore);
            check("dirty.window-stays", second->isVisible());
            dirtyDocument->setModified(false);
            pump();
            check("dirty.clean-close-allowed", second->confirmUnsavedDocuments());
        } else {
            // 1) Cancel keeps everything and writes nothing.
            const QByteArray archiveBefore2 = readFile(sessionFile);
            armDialogAnswer(QStringLiteral("Cancel"),
                            screenshots ? QStringLiteral("22-unsaved-restart-prompt.png") : QString());
            check("dirty.guard-cancels", !second->confirmUnsavedDocuments());
            pump();
            check("dirty.archive-not-written", readFile(sessionFile) == archiveBefore2);
            check("dirty.disk-not-written", readFile(fileB) == onDiskBefore);
            // 2) Save writes the buffer, and only then an archive may be written.
            armDialogAnswer(QStringLiteral("Save"));
            check("dirty.guard-save-allows", second->confirmUnsavedDocuments());
            pump();
            check("dirty.save-wrote-disk", readFile(fileB) != onDiskBefore
                      && readFile(fileB).contains("unsaved edit"));
            QString saveAgainError;
            check("dirty.archive-written-after-save", second->saveSession(&saveAgainError),
                  saveAgainError);
        }
    }

    // --- 5) a corrupt archive is a CLEAN START WITH A VISIBLE MESSAGE ------
    {
        const QString corruptDir = temp.path() + QStringLiteral("/corrupt");
        QDir().mkpath(corruptDir);
        check("corrupt.write-garbage",
              writeFile(corruptDir + QStringLiteral("/session.json"),
                        QByteArrayLiteral("{ this is not json")));
        ApplicationShell *corruptShell = makeShell(corruptDir, QString(), /*restore=*/true);
        bool hasParseDiagnostic = false;
        for (const QString &diagnostic : corruptShell->diagnostics()) {
            if (diagnostic.contains(QStringLiteral("session"), Qt::CaseInsensitive)) {
                hasParseDiagnostic = true;
            }
        }
        check("corrupt.clean-start", corruptShell->editorArea()->paneCount() == 1);
        check("corrupt.diagnostic", hasParseDiagnostic);
        check("corrupt.visible-message",
              corruptShell->statusBar()->currentMessage().contains(
                  QStringLiteral("Session could not be restored")),
              corruptShell->statusBar()->currentMessage());
        delete corruptShell;
        pump();
    }

    // --- 6) a MISSING archive is the ordinary first start (no message) -----
    {
        const QString emptyDir = temp.path() + QStringLiteral("/empty");
        QDir().mkpath(emptyDir);
        ApplicationShell *freshShell = makeShell(emptyDir, QString(), /*restore=*/true);
        check("missing.clean-start", freshShell->editorArea()->paneCount() == 1);
        check("missing.no-alarm",
              !freshShell->statusBar()->currentMessage().contains(
                  QStringLiteral("Session could not be restored")));
        delete freshShell;
        pump();
    }

    std::printf("SESSION-PROBE failures=%d platform=%s\n", failures,
                qPrintable(QApplication::platformName()));
    return failures == 0 ? 0 : 1;
}
