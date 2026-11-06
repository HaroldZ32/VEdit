#include "edit_core_selftest.h"

#include "atomic_save.h"
#include "document_io.h"
#include "find_replace.h"
#include "highlight_mode.h"
#include "goto_line.h"
#include "lsp_actions.h"
#include "multi_cursor.h"
#include "undo_ops.h"

#if defined(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
#include "ktexteditor_provider.h"
#endif

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>
#include <KSyntaxHighlighting/Definition>

#include <QAction>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>

#include <cstdio>

namespace vedit::editor {
namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : " ", qPrintable(detail));
    std::fflush(stdout);
}

void skip(const char *name, const QString &reason)
{
    std::printf("SKIP %s %s\n", name, qPrintable(reason));
    std::fflush(stdout);
}

QString sha256(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool writeFixture(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(contents) == contents.size();
}

QByteArray readFixture(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

int temporaryFilesLeft(const QString &directory)
{
    return QDir(directory)
        .entryList(QStringList{QStringLiteral(".*.vedit-tmp-*")}, QDir::Files | QDir::Hidden)
        .size();
}

struct ViewFixture {
    KTextEditor::Document *document = nullptr;
    KTextEditor::View *view = nullptr;
    ~ViewFixture()
    {
        // Delete the view first: it references the document, and the provider
        // parents the document to its view (this fixture creates a bare pair,
        // so an unparented document needs explicit deletion afterwards).
        const bool documentParented = document != nullptr && document->parent() != nullptr;
        delete view;
        view = nullptr;
        if (!documentParented) {
            delete document;
        }
        document = nullptr;
    }
};

ViewFixture makeViewFixture()
{
    ViewFixture fixture;
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        return fixture;
    }
    fixture.document = editor->createDocument(nullptr);
    if (fixture.document == nullptr) {
        return fixture;
    }
    fixture.view = fixture.document->createView(nullptr);
    return fixture;
}

// ---------------------------------------------------------------------------
// individual check groups
// ---------------------------------------------------------------------------

void checkDocumentRoundTrip(const QString &scratch)
{
    QString error;
    std::unique_ptr<EditorDocument> document = EditorDocument::create(&error);
    check("editcore.document.create", document != nullptr, error);
    if (document == nullptr) {
        return;
    }
    check("editcore.undo.metaobject-slot", undoSlotAvailable(document->document())
                                               && redoSlotAvailable(document->document()));
    check("editcore.document.setText", document->setText(QStringLiteral("module top;\nendmodule\n"))
                                           && document->text() == QStringLiteral("module top;\nendmodule\n"));

    const QString fixture = scratch + QStringLiteral("/open.v");
    const QByteArray contents = QByteArrayLiteral("module open_probe;\nendmodule\n");
    check("editcore.open.fixture-written", writeFixture(fixture, contents));
    check("editcore.open.file", document->openFile(fixture, &error)
                                    && document->text() == QString::fromUtf8(contents),
          error);
    check("editcore.open.records-path", document->filePath() == QFileInfo(fixture).absoluteFilePath(),
          document->filePath());
}

void checkAtomicSave(const QString &scratch)
{
    const QString directory = scratch + QStringLiteral("/atomic");
    QDir().mkpath(directory);
    const QString target = directory + QStringLiteral("/saved.v");
    check("editcore.atomic.fresh-file", atomicSaveText(target, QByteArrayLiteral("first\n")).ok);
    check("editcore.atomic.fresh-content", readFixture(target) == QByteArrayLiteral("first\n"));
    check("editcore.atomic.no-temp-left", temporaryFilesLeft(directory) == 0);

    const AtomicSaveResult replaced = atomicSaveText(target, QByteArrayLiteral("second\n"));
    check("editcore.atomic.replace", replaced.ok
                                         && readFixture(target) == QByteArrayLiteral("second\n"),
          replaced.error);
    check("editcore.atomic.replace-no-temp", temporaryFilesLeft(directory) == 0);

    qputenv("VEDIT_ATOMIC_FAULT", QByteArrayLiteral("write"));
    const AtomicSaveResult writeFault = atomicSaveText(target, QByteArrayLiteral("broken\n"));
    qunsetenv("VEDIT_ATOMIC_FAULT");
    check("editcore.atomic.fault.write-fails", !writeFault.ok && !writeFault.error.isEmpty());
    check("editcore.atomic.fault.write-keeps-target",
          readFixture(target) == QByteArrayLiteral("second\n"));
    check("editcore.atomic.fault.write-cleans-temp", temporaryFilesLeft(directory) == 0);

    qputenv("VEDIT_ATOMIC_FAULT", QByteArrayLiteral("rename"));
    const AtomicSaveResult renameFault = atomicSaveText(target, QByteArrayLiteral("broken\n"));
    qunsetenv("VEDIT_ATOMIC_FAULT");
    check("editcore.atomic.fault.rename-fails", !renameFault.ok);
    check("editcore.atomic.fault.rename-keeps-target",
          readFixture(target) == QByteArrayLiteral("second\n"));
    check("editcore.atomic.fault.rename-cleans-temp", temporaryFilesLeft(directory) == 0);

    qputenv("VEDIT_ATOMIC_FAULT", QByteArrayLiteral("fsync-dir"));
    const AtomicSaveResult dirFault = atomicSaveText(target, QByteArrayLiteral("third\n"));
    qunsetenv("VEDIT_ATOMIC_FAULT");
    check("editcore.atomic.fault.dir-fsync-nonfatal",
          dirFault.ok && !dirFault.diagnostics.isEmpty(),
          dirFault.diagnostics.join(QLatin1Char(';')));
    check("editcore.atomic.fault.dir-fsync-content",
          readFixture(target) == QByteArrayLiteral("third\n"));

    std::unique_ptr<EditorDocument> document = EditorDocument::create();
    if (document == nullptr) {
        check("editcore.save.round-trip", false, QStringLiteral("no document"));
        return;
    }
    document->setText(QStringLiteral("saved through the kernel\n"));
    QString error;
    check("editcore.save.kernel", document->saveAtomic(&error, target), error);
    check("editcore.save.bytes", readFixture(target) == QByteArrayLiteral("saved through the kernel\n"));
    check("editcore.save.clears-modified", !document->isModified());
}

void checkFindReplace()
{
    std::unique_ptr<EditorDocument> document = EditorDocument::create();
    if (document == nullptr) {
        check("editcore.find.count", false, QStringLiteral("no document"));
        return;
    }
    document->setText(QStringLiteral("aaa bbb aaa ccc aaa\n"));

    QList<KTextEditor::Range> ranges;
    const FindResult found = findMatches(document->document(), QStringLiteral("aaa"), true, &ranges);
    check("editcore.find.count", found.ok && found.matches == 3, QString::number(found.matches));
    check("editcore.find.adjacent", findMatches(document->document(), QStringLiteral("a"),
                                                true).matches == 9);

    const QString original = document->text();
    const ReplaceResult replaced = replaceAll(document->document(), QStringLiteral("aaa"),
                                              QStringLiteral("zz"));
    check("editcore.replace.count", replaced.ok && replaced.found == 3 && replaced.replaced == 3,
          QStringLiteral("found=%1 replaced=%2").arg(replaced.found).arg(replaced.replaced));
    const QString afterReplace = document->text();
    check("editcore.replace.text", afterReplace == QStringLiteral("zz bbb zz ccc zz\n"),
          afterReplace);

    QString error;
    check("editcore.replace.one-undo", documentUndo(document->document(), &error)
                                           && document->text() == original,
          error);
    check("editcore.replace.redo", documentRedo(document->document(), &error)
                                       && document->text() == afterReplace,
          error);
}

void checkGotoLine()
{
    ViewFixture fixture = makeViewFixture();
    if (fixture.view == nullptr) {
        check("editcore.goto.line", false, QStringLiteral("no KTextEditor view"));
        return;
    }
    fixture.document->setText(QStringLiteral("line one\nline two\nline three\n"));

    const GotoResult second = gotoLine(fixture.view, 2);
    check("editcore.goto.line", second.ok && second.line == 1, second.error);
    check("editcore.goto.clamp-low", gotoLine(fixture.view, 0).ok
                                        && fixture.view->cursorPosition().line() == 0);
    const GotoResult clampHigh = gotoLine(fixture.view, 9999);
    check("editcore.goto.clamp-high",
          clampHigh.ok && clampHigh.line == fixture.document->lines() - 1,
          QStringLiteral("line=%1").arg(clampHigh.line));
    check("editcore.goto.column-zero", fixture.view->cursorPosition().column() == 0);
}

void checkUndoRedoSteps()
{
    std::unique_ptr<EditorDocument> document = EditorDocument::create();
    if (document == nullptr) {
        check("editcore.undo.steps", false, QStringLiteral("no document"));
        return;
    }
    // 100 LOGICAL operations. replaceText on distinct ranges is step-wise
    // (probe: 101 steps for 100 replaceText calls); plain typing is merged by
    // KTextEditor's own policy and is deliberately not used here.
    QString initial;
    initial.fill(QLatin1Char('a'), 200);
    document->setText(initial);

    QStringList snapshots;
    snapshots.append(initial);
    int operations = 0;
    for (int index = 0; index < 100; ++index) {
        const QChar replacement(QLatin1Char('b' + (index % 24)));
        const bool replaced = document->document()->replaceText(
            KTextEditor::Range(KTextEditor::Cursor(0, index), KTextEditor::Cursor(0, index + 1)),
            QString(replacement));
        if (!replaced) {
            break;
        }
        ++operations;
        snapshots.append(document->text());
    }
    check("editcore.undo.100-operations", operations == 100, QString::number(operations));

    int undoMismatches = 0;
    QString error;
    for (int step = operations; step >= 1; --step) {
        if (!documentUndo(document->document(), &error) || document->text() != snapshots.at(step - 1)) {
            ++undoMismatches;
        }
    }
    check("editcore.undo.100-back-to-original", undoMismatches == 0
                                                    && document->text() == snapshots.first(),
          QStringLiteral("mismatches=%1 error=%2").arg(undoMismatches).arg(error));

    int redoMismatches = 0;
    for (int step = 1; step <= operations; ++step) {
        if (!documentRedo(document->document(), &error) || document->text() != snapshots.at(step)) {
            ++redoMismatches;
        }
    }
    check("editcore.redo.100-forwards", redoMismatches == 0
                                            && document->text() == snapshots.last(),
          QStringLiteral("mismatches=%1").arg(redoMismatches));
    std::printf("EDITCORE-UNDO sha-before=%s sha-after=%s\n",
                qPrintable(sha256(snapshots.first())), qPrintable(sha256(snapshots.last())));
}

void checkMultiCursor()
{
    ViewFixture fixture = makeViewFixture();
    if (fixture.view == nullptr) {
        check("editcore.multicursor.native-api", false, QStringLiteral("no KTextEditor view"));
        return;
    }
    fixture.document->setText(QStringLiteral("aaa\n"));
    const QList<KTextEditor::Cursor> positions{KTextEditor::Cursor(0, 0), KTextEditor::Cursor(0, 1),
                                               KTextEditor::Cursor(0, 2)};
    const MultiCursorResult cursors = setViewCursors(fixture.view, positions);
    check("editcore.multicursor.native-api", cursors.ok && cursors.cursors == 3, cursors.error);
    const QList<KTextEditor::Cursor> readBack = viewCursors(fixture.view);
    check("editcore.multicursor.native-read-back",
          readBack.size() == 3 && readBack.at(0) == KTextEditor::Cursor(0, 0)
              && readBack.at(2) == KTextEditor::Cursor(0, 2));

    // Record what the native view's convenience insert does with three cursors.
    fixture.document->setText(QStringLiteral("aaa\n"));
    fixture.view->setCursorPositions(positions);
    const MultiCursorResult nativeInsert = viewInsertText(fixture.view, QStringLiteral("X"));
    const int nativeX = fixture.document->text().count(QLatin1Char('X'));
    check("editcore.multicursor.native-insert-call", nativeInsert.ok, nativeInsert.error);
    std::printf("EDITCORE-MULTICURSOR native-cursors=%d native-inserted-x=%d text=\"%s\"\n",
                nativeInsert.cursors, nativeX, qPrintable(fixture.document->text()));
    check("editcore.multicursor.native-insert-primary", nativeX >= 1, QString::number(nativeX));

    // Deterministic batch edit: three inserts, ONE undo.
    std::unique_ptr<EditorDocument> document = EditorDocument::create();
    if (document == nullptr) {
        check("editcore.multicursor.batch-insert", false, QStringLiteral("no document"));
        return;
    }
    document->setText(QStringLiteral("aaa\n"));
    const MultiCursorResult batch = insertAtCursors(document->document(), positions,
                                                    QStringLiteral("X"));
    check("editcore.multicursor.batch-insert", batch.ok && batch.inserted == 3, batch.error);
    check("editcore.multicursor.batch-text", document->text() == QStringLiteral("XaXaXa\n"),
          document->text());
    QString error;
    check("editcore.multicursor.batch-one-undo",
          documentUndo(document->document(), &error) && document->text() == QStringLiteral("aaa\n"),
          error);
    check("editcore.multicursor.batch-redo",
          documentRedo(document->document(), &error)
              && document->text() == QStringLiteral("XaXaXa\n"),
          error);
}


// ---------------------------------------------------------------------------
// batch 2, AC-2.1/AC-2.2/AC-2.4: applying a rename WorkspaceEdit and the rows a
// references list shows. No server is involved: the engine is fed the answer the
// LSP bridge produces, so the acceptance is about what the PRODUCT does with it.
// ---------------------------------------------------------------------------
void checkWorkspaceEdit(const QString &scratch)
{
    const QString openPath = scratch + QStringLiteral("/rename-open.sv");
    const QString diskPath = scratch + QStringLiteral("/rename-disk.sv");
    check("editcore.wsedit.fixture-open",
          writeFixture(openPath,
                       QByteArray("module top;\n  wire old_name;\n  old_name = 1;\nendmodule\n")));
    check("editcore.wsedit.fixture-disk", writeFixture(diskPath, QByteArray("// old_name\n")));

    // An OPEN document: the view carries the same path property the product's
    // providers set, so the engine's application-wide scan finds it.
    ViewFixture fixture;
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        if (editor != nullptr) {
            fixture.document = editor->createDocument(nullptr);
            if (fixture.document != nullptr) {
                fixture.view = fixture.document->createView(nullptr);
            }
        }
    }
    check("editcore.wsedit.view", fixture.document != nullptr && fixture.view != nullptr);
    if (fixture.document == nullptr || fixture.view == nullptr) {
        return;
    }
    fixture.document->setText(QStringLiteral("module top;\n  wire old_name;\n  old_name = 1;\nendmodule\n"));
    fixture.document->setModified(false);
    fixture.view->setProperty("vedit.openFilePath", openPath);
    check("editcore.wsedit.open-visible", openDocumentCount() >= 1,
          QString::number(openDocumentCount()));

    // Two occurrences in the open buffer + one in a file that is NOT open.
    WorkspaceTextEdit openFirst;
    openFirst.filePath = openPath;
    openFirst.startLine = 1;
    openFirst.startColumn = 7;
    openFirst.endLine = 1;
    openFirst.endColumn = 15;
    openFirst.newText = QStringLiteral("new_name");
    WorkspaceTextEdit openSecond = openFirst;
    openSecond.startLine = 2;
    openSecond.startColumn = 2;
    openSecond.endLine = 2;
    openSecond.endColumn = 10;
    WorkspaceTextEdit onDisk;
    onDisk.filePath = diskPath;
    onDisk.startLine = 0;
    onDisk.startColumn = 3;
    onDisk.endLine = 0;
    onDisk.endColumn = 11;
    onDisk.newText = QStringLiteral("new_name");

    const WorkspaceEditReport report =
        applyWorkspaceEdit(QList<WorkspaceTextEdit>{openFirst, openSecond, onDisk});
    check("editcore.wsedit.applied", report.ok, report.error);
    check("editcore.wsedit.counts",
          report.editCount == 3 && report.fileCount == 2 && report.openDocumentCount == 1
              && report.writtenFileCount == 1,
          QStringLiteral("edits=%1 files=%2 editor=%3 disk=%4")
              .arg(report.editCount)
              .arg(report.fileCount)
              .arg(report.openDocumentCount)
              .arg(report.writtenFileCount));
    check("editcore.wsedit.open-text",
          fixture.document->text()
              == QStringLiteral("module top;\n  wire new_name;\n  new_name = 1;\nendmodule\n"),
          fixture.document->text());
    check("editcore.wsedit.open-dirty", fixture.document->isModified());
    check("editcore.wsedit.disk-text",
          readFixture(diskPath) == QByteArray("// new_name\n"),
          QString::fromUtf8(readFixture(diskPath)));
    check("editcore.wsedit.summary",
          report.summary().contains(QStringLiteral("3 edit(s)"))
              && report.summary().contains(QStringLiteral("2 file(s)")),
          report.summary());

    // ONE undo restores both changes of the open document (the rename edits ran
    // inside one KTextEditor EditingTransaction).
    QString undoError;
    check("editcore.wsedit.one-undo",
          documentUndo(fixture.document, &undoError)
              && fixture.document->text()
                     == QStringLiteral("module top;\n  wire old_name;\n  old_name = 1;\nendmodule\n"),
          undoError);
    check("editcore.wsedit.one-redo",
          documentRedo(fixture.document, &undoError)
              && fixture.document->text()
                     == QStringLiteral("module top;\n  wire new_name;\n  new_name = 1;\nendmodule\n"),
          undoError);

    // A rejected range refuses the WHOLE rename: nothing in the buffer or on disk
    // may change (phase 1 validates before phase 2 applies).
    const QString beforeText = fixture.document->text();
    WorkspaceTextEdit outOfRange;
    outOfRange.filePath = openPath;
    outOfRange.startLine = 999;
    outOfRange.startColumn = 0;
    outOfRange.endLine = 999;
    outOfRange.endColumn = 0;
    outOfRange.newText = QStringLiteral("nope");
    WorkspaceTextEdit validButLate = onDisk;
    validButLate.newText = QStringLiteral("should_not_land");
    const WorkspaceEditReport rejected =
        applyWorkspaceEdit(QList<WorkspaceTextEdit>{validButLate, outOfRange});
    check("editcore.wsedit.out-of-range-refused", !rejected.ok && !rejected.error.isEmpty(),
          rejected.error);
    check("editcore.wsedit.refusal-keeps-buffer", fixture.document->text() == beforeText);
    check("editcore.wsedit.refusal-keeps-file",
          readFixture(diskPath) == QByteArray("// new_name\n"),
          QString::fromUtf8(readFixture(diskPath)));

    // A target that is neither open nor readable is a real, named error.
    WorkspaceTextEdit missing;
    missing.filePath = scratch + QStringLiteral("/does-not-exist.sv");
    missing.startLine = 0;
    missing.startColumn = 0;
    missing.endLine = 0;
    missing.endColumn = 1;
    missing.newText = QStringLiteral("x");
    const WorkspaceEditReport missingReport = applyWorkspaceEdit(QList<WorkspaceTextEdit>{missing});
    check("editcore.wsedit.missing-file-refused",
          !missingReport.ok && missingReport.error.contains(QStringLiteral("cannot change")),
          missingReport.error);
    check("editcore.wsedit.empty-refused", !applyWorkspaceEdit({}).ok);

    // --- references rows (AC-2.2) -------------------------------------------
    check("editcore.references.empty", buildReferenceRows({}).isEmpty());
    WorkspaceLocation inOpen;
    inOpen.filePath = openPath;
    inOpen.line = 1;
    inOpen.column = 7;
    WorkspaceLocation duplicate = inOpen;
    WorkspaceLocation onDiskLocation;
    onDiskLocation.filePath = diskPath;
    onDiskLocation.line = 0;
    onDiskLocation.column = 3;
    const QList<ReferenceRow> rows =
        buildReferenceRows(QList<WorkspaceLocation>{inOpen, duplicate, onDiskLocation});
    check("editcore.references.count", rows.size() == 2, QString::number(rows.size()));
    check("editcore.references.ordered",
          rows.size() == 2 && rows.at(0).filePath == diskPath && rows.at(1).filePath == openPath);
    check("editcore.references.open-snippet",
          rows.size() == 2 && rows.at(1).snippet == QStringLiteral("wire new_name;"),
          rows.size() == 2 ? rows.at(1).snippet : QString());
    check("editcore.references.location-text",
          rows.size() == 2 && rows.at(1).location() == openPath + QStringLiteral(":2:8"),
          rows.size() == 2 ? rows.at(1).location() : QString());
    check("editcore.references.disk-snippet",
          rows.size() == 2 && rows.at(0).snippet == QStringLiteral("// new_name"),
          rows.size() == 2 ? rows.at(0).snippet : QString());
}

// AC-2.1/AC-2.2 capability gating: an unavailable action is disabled WITH the
// backend's own reason (never "not implemented"), and the channel refuses it.
void checkLspActionGates()
{
    QMainWindow host;
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    KTextEditor::Document *document = editor != nullptr ? editor->createDocument(nullptr) : nullptr;
    check("editcore.gates.document", document != nullptr);
    if (document == nullptr) {
        return;
    }
    document->setText(QStringLiteral("module gated;\n  wire sig;\nendmodule\n"));
    KTextEditor::View *view = document->createView(&host);
    check("editcore.gates.view", view != nullptr);
    if (view == nullptr) {
        return;
    }
    view->setProperty("vedit.openFilePath", QStringLiteral("/tmp/gated.sv"));
    // The shell action the menu uses, created exactly like the shell does.
    auto *renameAction = new QAction(QStringLiteral("&Rename Symbol..."), &host);
    renameAction->setObjectName(QStringLiteral("action-diagnostics.rename"));
    host.addAction(renameAction);
    host.show();

    QVariantMap gates;
    QVariantMap renameRow;
    renameRow.insert(QStringLiteral("enabled"), false);
    renameRow.insert(QStringLiteral("known"), true);
    renameRow.insert(QStringLiteral("source"), QStringLiteral("backend"));
    renameRow.insert(
        QStringLiteral("reason"),
        QStringLiteral("verible does not advertise renameProvider for this session"));
    gates.insert(QStringLiteral("rename"), renameRow);
    QVariantMap backendMap;
    backendMap.insert(QStringLiteral("backend"), QStringLiteral("verible"));
    host.setProperty("vedit.diagnostics.gates", gates);
    host.setProperty("vedit.diagnostics.capabilities", backendMap);

    LspActionChannel *channel = LspActionChannel::install(view);
    check("editcore.gates.channel", channel != nullptr);
    if (channel == nullptr) {
        delete view;
        return;
    }
    const ActionAvailability unavailable = channel->availability(EditorAction::Rename);
    check("editcore.gates.rename-disabled", !unavailable.enabled && unavailable.source == QStringLiteral("backend"),
          unavailable.reason);
    check("editcore.gates.rename-real-reason",
          unavailable.reason.contains(QStringLiteral("renameProvider")),
          unavailable.reason);
    // The lane disables the action from the same decision; this is that state.
    renameAction->setEnabled(unavailable.enabled);
    renameAction->setToolTip(unavailable.reason);
    check("editcore.gates.rename-action-greyed",
          !renameAction->isEnabled()
              && renameAction->toolTip() == unavailable.reason);
    const ActionDispatch refused = channel->dispatch(EditorAction::Rename);
    check("editcore.gates.rename-refused", !refused.dispatched && !refused.reason.isEmpty(),
          refused.reason);

    // Supported again => enabled, and the reason is gone.
    renameRow.insert(QStringLiteral("enabled"), true);
    renameRow.insert(QStringLiteral("reason"), QString());
    renameRow.insert(QStringLiteral("source"), QStringLiteral("backend"));
    gates.insert(QStringLiteral("rename"), renameRow);
    host.setProperty("vedit.diagnostics.gates", gates);
    // The lane re-applies the gate to the action on every live-state refresh
    // (applyGate); the channel then merges that action state with the map.
    renameAction->setEnabled(true);
    renameAction->setToolTip(QString());
    const ActionAvailability available = channel->availability(EditorAction::Rename);
    check("editcore.gates.rename-enabled", available.enabled, available.reason);
    renameAction->setEnabled(available.enabled);
    check("editcore.gates.rename-action-enabled", renameAction->isEnabled());

    view->setParent(nullptr);
    delete view;
    delete document;
}

#if defined(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
void checkProvider()
{
    EditCoreProvider provider;
    check("editcore.provider.id", provider.id() == QStringLiteral("ktexteditor"), provider.id());
    QWidget *editor = provider.createEditor(nullptr);
    check("editcore.provider.creates-view", editor != nullptr && isEditCoreWidget(editor));
    if (editor == nullptr) {
        return;
    }

    QTemporaryDir temp;
    const QString fixture = temp.path() + QStringLiteral("/provider.v");
    const QByteArray contents = QByteArrayLiteral("module provider;\nendmodule\n");
    check("editcore.provider.fixture", writeFixture(fixture, contents));
    QString error;
    check("editcore.provider.open", provider.openFile(editor, fixture, &error), error);
    check("editcore.provider.open-records-path",
          provider.currentFilePath(editor) == QFileInfo(fixture).absoluteFilePath(),
          provider.currentFilePath(editor));
    KTextEditor::Document *document = documentFor(editor);
    check("editcore.provider.document", document != nullptr
                                            && document->text() == QString::fromUtf8(contents));

    if (document != nullptr) {
        document->setText(QStringLiteral("module provider_saved;\nendmodule\n"));
    }
    check("editcore.provider.save", provider.saveFile(editor, &error), error);
    check("editcore.provider.save-bytes",
          readFixture(fixture) == QByteArrayLiteral("module provider_saved;\nendmodule\n"));
    delete editor;
}
#endif

void checkBoundary(const BoundaryHooks &hooks)
{
    if (!hooks.calls) {
        skip("editcore.boundary.zero-calls", QStringLiteral("no C ABI hooks in this build"));
        return;
    }
    // The whole check group above ran between these two snapshots: opening,
    // editing, undo/redo, replace-all, goto-line, multi-cursor, atomic saves.
    const qint64 before = hooks.calls();
    ViewFixture fixture = makeViewFixture();
    if (fixture.view != nullptr) {
        fixture.document->setText(QStringLiteral("a b a b a\n"));
        replaceAll(fixture.document, QStringLiteral("a"), QStringLiteral("c"));
        documentUndo(fixture.document);
        documentRedo(fixture.document);
        gotoLine(fixture.view, 1);
        insertAtCursors(fixture.document,
                        {KTextEditor::Cursor(0, 0), KTextEditor::Cursor(0, 1)},
                        QStringLiteral("Z"));
    }
    const qint64 after = hooks.calls();
    check("editcore.boundary.zero-calls", before == after,
          QStringLiteral("before=%1 after=%2").arg(before).arg(after));

    if (hooks.positiveControl) {
        const qint64 controlBefore = hooks.calls();
        hooks.positiveControl();
        check("editcore.boundary.counter-live", hooks.calls() == controlBefore + 1,
              QStringLiteral("before=%1 after=%2").arg(controlBefore).arg(hooks.calls()));
    } else {
        skip("editcore.boundary.counter-live", QStringLiteral("no positive-control hook"));
    }
}

// The user-visible acceptance of T81: after the product open path, the document
// must carry a syntax-highlighting mode from the editor's own repository, and the
// engine must really apply format ranges and folding regions to its text. This is
// the closest observable proxy to "the view renders colors"; it works both on a
// developer host (host definitions visible) and in the delivered runtime (only
// VEdit's definitions visible).
void checkHighlighting(const QString &scratch)
{
    struct Case {
        const char *label;
        const char *fileName;
        const char *expectedMode;
        const char *text;
    };
    const Case cases[] = {
        {"verilog", "highlight.v", "Verilog",
         "module top;\n  // comment\n  assign out = 1;\n  initial begin\n"
         "    out <= 1'b0;\n  end\nendmodule\n"},
        {"systemverilog", "highlight.sv", "SystemVerilog",
         "module top;\n  logic [7:0] q;\n  always_ff @(posedge clk)\n"
         "    q <= d;\nendmodule\n"},
    };
    for (const Case &testCase : cases) {
        const QString prefix =
            QStringLiteral("editcore.highlight.") + QLatin1String(testCase.label);
        const QByteArray fixtureName = (prefix + QStringLiteral(".fixture")).toUtf8();
        const QString path = scratch + QLatin1Char('/') + QLatin1String(testCase.fileName);
        check(fixtureName.constData(), writeFixture(path, QByteArray(testCase.text)));

        QString error;
        std::unique_ptr<EditorDocument> document = EditorDocument::create(&error);
        if (document == nullptr) {
            const QByteArray documentName = (prefix + QStringLiteral(".document")).toUtf8();
            check(documentName.constData(), false, error);
            continue;
        }
        const QByteArray openName = (prefix + QStringLiteral(".open")).toUtf8();
        check(openName.constData(), document->openFile(path, &error), error);

        KTextEditor::Document *editorDocument = document->document();
        const QString mode = editorDocument->highlightingMode();
        const QByteArray modeName = (prefix + QStringLiteral(".mode")).toUtf8();
        check(modeName.constData(), mode == QLatin1String(testCase.expectedMode),
              QStringLiteral("mode='%1' expected='%2'")
                  .arg(mode, QLatin1String(testCase.expectedMode)));

        const KSyntaxHighlighting::Definition definition = editorDefinitionForMode(mode);
        const QByteArray definitionName = (prefix + QStringLiteral(".definition")).toUtf8();
        check(definitionName.constData(), definition.isValid(), definition.name());

        const HighlightStats stats = measureHighlighting(definition, editorDocument->text());
        const QByteArray rangesName = (prefix + QStringLiteral(".format-ranges")).toUtf8();
        check(rangesName.constData(), definition.isValid() && stats.formats > 0,
              QString::number(stats.formats));
        const QByteArray foldingName = (prefix + QStringLiteral(".folding-regions")).toUtf8();
        check(foldingName.constData(),
              definition.isValid() && stats.foldingBegins > 0 && stats.foldingEnds > 0,
              QStringLiteral("begins=%1 ends=%2")
                  .arg(stats.foldingBegins)
                  .arg(stats.foldingEnds));
    }
}

} // namespace

SelfCheckResult runSelfChecks(const BoundaryHooks &hooks)
{
    g_checks = 0;
    g_failures = 0;

    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        check("editcore.scratch", false, QStringLiteral("cannot create a temporary directory"));
        return SelfCheckResult{g_checks, g_failures};
    }
    check("editcore.scratch", true);

    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    check("editcore.ktexteditor.instance", editor != nullptr);

    checkDocumentRoundTrip(scratch.path());
    checkHighlighting(scratch.path());
    checkAtomicSave(scratch.path());
    checkFindReplace();
    checkGotoLine();
    checkUndoRedoSteps();
    checkMultiCursor();
    checkWorkspaceEdit(scratch.path());
    checkLspActionGates();
#if defined(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
    checkProvider();
#else
    skip("editcore.provider", QStringLiteral("built without the src/app EditorProvider seam"));
#endif
    checkBoundary(hooks);

    std::printf("EDIT-CORE-SELFTEST ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    return SelfCheckResult{g_checks, g_failures};
}

} // namespace vedit::editor
