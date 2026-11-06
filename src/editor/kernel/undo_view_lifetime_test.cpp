// Regression RM-5a (real-machine defect): repeated Ctrl+Z in one file must
// never detach the view from its document, empty the buffer, or kill editing.
//
// The reported sequence: several edits in one file, then Ctrl+Z a few times —
// the editor area went blank ("一片大白"), undo stopped responding and editing
// stopped working. Recovery was close + discard + reopen (the file on disk was
// untouched). This test drives the PRODUCT seam the user drives:
//
//   EditorArea -> EditorPane -> EditCoreProvider::openFile()
//              -> loadFileIntoDocument()
//              -> EditorPane::triggerEditorAction({"edit_undo","undo"})
//
// and asserts after every undo:
//   (a) the pane still holds the SAME editor widget and its document pointer is
//       the SAME object (nothing was re-created or re-bound),
//   (b) the document text is never emptied — undoing user edits must stop at
//       the text the file was OPENED with,
//   (c) typing right after the undos still lands in the buffer,
//   (d) the tab still shows the file (path + non-empty label), and
//   (e) the real Ctrl+Z key path (sent to the editor's inner widget) behaves the
//       same way as the toolbar/menu command.
//
// Exit 77 means the KTextEditor plugin is unavailable (ctest SKIP_RETURN_CODE),
// never 0.
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"

#include <KTextEditor/Command>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QApplication>
#include <QFile>
#include <QPoint>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

#include <cstdio>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;

// Folded-line count reported by the view: real displayed span minus visible
// span. A laid-out, scrolled-to-top view reports the number of folded lines.
int hiddenLines(KTextEditor::View *view)
{
    if (view == nullptr) {
        return -1;
    }
    const int firstReal = view->firstDisplayedLine(KTextEditor::View::RealLine);
    const int lastReal = view->lastDisplayedLine(KTextEditor::View::RealLine);
    const int firstVisible = view->firstDisplayedLine(KTextEditor::View::VisibleLine);
    const int lastVisible = view->lastDisplayedLine(KTextEditor::View::VisibleLine);
    return (lastReal - firstReal) - (lastVisible - firstVisible);
}

void check(const QString &name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("CHECK %s PASS\n", qPrintable(name));
        return;
    }
    ++g_failures;
    std::printf("CHECK %s FAIL%s\n", qPrintable(name),
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" (%1)").arg(detail)));
}

// The widget Qt hands a real key/mouse event to: KatePart's KateViewInternal, a
// CHILD of the public KTextEditor::View (see editor_area_activation_test.cpp).
QWidget *deliveryTarget(KTextEditor::View *view, const QPoint &viewPoint)
{
    if (view == nullptr) {
        return nullptr;
    }
    QWidget *target = view->childAt(viewPoint);
    return target != nullptr ? target : view;
}

bool typeText(KTextEditor::View *view, const QString &text)
{
    QWidget *target = deliveryTarget(view, view->cursorToCoordinate(view->cursorPosition()));
    if (target == nullptr) {
        return false;
    }
    target->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    QTest::keyClicks(target, text);
    QApplication::processEvents();
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    if (KTextEditor::Editor::instance() == nullptr) {
        std::printf("SKIP: the KTextEditor plugin is unavailable\n");
        return 77;
    }

    QTemporaryDir dir;
    check(QStringLiteral("setup.temp-dir"), dir.isValid());
    if (!dir.isValid()) {
        return 1;
    }
    const QString path = dir.filePath(QStringLiteral("rm5_undo_top.sv"));
    const QString openedText =
        QStringLiteral("module rm5_undo_top;\n  wire a;\n  wire b;\nendmodule\n");
    {
        QFile file(path);
        check(QStringLiteral("setup.file-writable"), file.open(QIODevice::WriteOnly));
        file.write(openedText.toUtf8());
    }

    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    vedit::app::EditorArea area;
    area.setProvider(provider.get());
    area.resize(1200, 700);
    area.show();
    QTest::qWait(100);
    QApplication::processEvents();

    vedit::app::EditorPane *pane = area.ensurePane();
    check(QStringLiteral("setup.pane"), pane != nullptr);
    if (pane == nullptr) {
        return 1;
    }
    QString openError;
    check(QStringLiteral("setup.open-file"), pane->openFile(path, &openError), openError);
    QApplication::processEvents();
    check(QStringLiteral("setup.tab-shows-file"), pane->tabCount() == 1
                                                     && pane->tabFilePath(0) == path
                                                     && !pane->tabText(0).isEmpty(),
          QStringLiteral("tabs=%1 path=%2 label=%3")
              .arg(pane->tabCount())
              .arg(pane->tabFilePath(0), pane->tabText(0)));

    KTextEditor::View *view = vedit::editor::viewFor(pane->editorWidget());
    check(QStringLiteral("setup.view"), view != nullptr);
    if (view == nullptr) {
        return 1;
    }
    KTextEditor::Document *document = view->document();
    check(QStringLiteral("setup.document"), document != nullptr);
    if (document == nullptr) {
        return 1;
    }
    check(QStringLiteral("setup.opened-text"), document->text() == openedText,
          QStringLiteral("size=%1").arg(document->text().size()));

    // --- RM-5a follow-up: the two guarantees are tested TOGETHER -----------
    // The native-fold probe used to rely on a side effect of Document::setText()
    // (the load path this task replaced), and it was the only place the fold
    // guarantee was asserted. Here, on the SAME loaded document: a fold can be
    // applied (KDE's own command) AND the load still leaves the undo floor at the
    // opened text (no undo step is created by the load or by folding).
    {
        KTextEditor::Command *foldCommand =
            KTextEditor::Editor::instance()->queryCommand(QStringLiteral("fold"));
        check(QStringLiteral("cross.fold.command"), foldCommand != nullptr);
        const int beforeHidden = hiddenLines(view);
        QString foldMessage;
        const bool folded = foldCommand != nullptr
            && foldCommand->exec(view, QStringLiteral("fold"), foldMessage,
                                 KTextEditor::Range(0, 0, 2, 0));
        QApplication::processEvents();
        check(QStringLiteral("cross.fold.applied"),
              folded && hiddenLines(view) > beforeHidden,
              QStringLiteral("hidden %1 -> %2 message='%3'")
                  .arg(beforeHidden)
                  .arg(hiddenLines(view))
                  .arg(foldMessage));
        check(QStringLiteral("cross.fold.not-an-edit"), document->text() == openedText,
              QStringLiteral("size=%1").arg(document->text().size()));
        // Folding is a view operation, so the document's undo stack must still be
        // empty: the shell's edit.undo action has nothing to do on a fresh file.
        check(QStringLiteral("cross.fold.no-undo-step"),
              !pane->editorActionEnabled({QStringLiteral("edit_undo"), QStringLiteral("undo")}));
        // Unfold again so the following zero-edit Ctrl+Z block measures the undo
        // floor, not a folded view.
        QString unfoldMessage;
        if (foldCommand != nullptr) {
            foldCommand->exec(view, QStringLiteral("unfold"), unfoldMessage,
                              KTextEditor::Range(0, 0, 2, 0));
        }
        QApplication::processEvents();
    }

    // --- the user's MINIMAL repro: a file that was NEVER edited ------------
    // "在撤无可撤的情况下，再按 Ctrl+Z 会直接把当前显示的文件清空": with the old
    // open path the file LOAD was itself one undoable step, so the very first
    // Ctrl+Z on a never-edited file emptied the buffer. Both dispatch paths are
    // driven — the toolbar/menu command (triggerEditorAction) and the real
    // Ctrl+Z key, which is what the user actually pressed. Redo with nothing to
    // redo must be a no-op in both paths too.
    {
        const QString before = document->text();
        const bool commandRan = pane->triggerEditorAction(
            {QStringLiteral("edit_undo"), QStringLiteral("undo")}, QStringLiteral("undo"));
        QApplication::processEvents();
        check(QStringLiteral("zero-edit.undo-command-buffer-unchanged"),
              document->text() == before,
              QStringLiteral("ran=%1 text=%2 opened=%3")
                  .arg(commandRan ? 1 : 0)
                  .arg(document->text().size())
                  .arg(openedText.size()));
        check(QStringLiteral("zero-edit.undo-command-identity"),
              pane->editorWidget() == view && view->document() == document,
              QStringLiteral("text=%1").arg(document->text().size()));

        QWidget *keyTarget =
            deliveryTarget(view, view->cursorToCoordinate(view->cursorPosition()));
        if (keyTarget != nullptr) {
            QTest::keyClick(keyTarget, Qt::Key_Z, Qt::ControlModifier);
        }
        QApplication::processEvents();
        check(QStringLiteral("zero-edit.undo-key-buffer-unchanged"),
              document->text() == before,
              QStringLiteral("text=%1 opened=%2")
                  .arg(document->text().size())
                  .arg(openedText.size()));
        check(QStringLiteral("zero-edit.undo-key-identity"),
              pane->editorWidget() == view && view->document() == document,
              QStringLiteral("text=%1").arg(document->text().size()));

        const bool redoCommandRan = pane->triggerEditorAction(
            {QStringLiteral("edit_redo"), QStringLiteral("redo")}, QStringLiteral("redo"));
        QApplication::processEvents();
        check(QStringLiteral("zero-edit.redo-command-buffer-unchanged"),
              document->text() == before,
              QStringLiteral("ran=%1 text=%2")
                  .arg(redoCommandRan ? 1 : 0)
                  .arg(document->text().size()));
        if (keyTarget != nullptr) {
            QTest::keyClick(keyTarget, Qt::Key_Y, Qt::ControlModifier);
            QTest::keyClick(keyTarget, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        }
        QApplication::processEvents();
        check(QStringLiteral("zero-edit.redo-key-buffer-unchanged"),
              document->text() == before,
              QStringLiteral("text=%1").arg(document->text().size()));
        // Typing still lands after the refused undo/redo pair.
        view->setCursorPosition(KTextEditor::Cursor(document->lines() - 1, 0));
        const int beforeNoopTyping = document->text().size();
        typeText(view, QStringLiteral("q"));
        check(QStringLiteral("zero-edit.typing-after-refused-undo"),
              document->text().size() > beforeNoopTyping,
              QStringLiteral("before=%1 after=%2")
                  .arg(beforeNoopTyping)
                  .arg(document->text().size()));
        // Undo that one real edit; the buffer must return to the opened text.
        pane->triggerEditorAction({QStringLiteral("edit_undo"), QStringLiteral("undo")},
                                  QStringLiteral("undo"));
        QApplication::processEvents();
        check(QStringLiteral("zero-edit.undo-returns-to-opened-text"),
              document->text() == openedText,
              QStringLiteral("text=%1 opened=%2")
                  .arg(document->text().size())
                  .arg(openedText.size()));
    }

    // --- the user's edits (through the real key path) ----------------------
    view->setCursorPosition(KTextEditor::Cursor(document->lines() - 1, 0));
    const int beforeTyping = document->text().size();
    typeText(view, QStringLiteral("abc"));
    check(QStringLiteral("edit.typing-lands"), document->text().size() > beforeTyping,
          QStringLiteral("before=%1 after=%2").arg(beforeTyping).arg(document->text().size()));
    for (int i = 0; i < 3; ++i) {
        document->insertText(KTextEditor::Cursor(document->lines() - 1, 0),
                             QStringLiteral("  wire b%1;\n").arg(i));
        QApplication::processEvents();
    }
    check(QStringLiteral("edit.text-changed"), document->text() != openedText,
          QStringLiteral("size=%1").arg(document->text().size()));

    // --- (a)-(d): repeated undo through the toolbar/menu command -----------
    // triggerEditorAction() is exactly what the shell's edit.undo command, the
    // toolbar button and the Edit menu call. Once the undo stack is exhausted
    // the editor's own action is legitimately DISABLED (that is what greys the
    // menu/toolbar entry), so a refused trigger is correct — but then the buffer
    // must still BE the text the file was opened with, never an empty buffer.
    const int undos = 8;
    bool firstPressRan = false;
    for (int press = 1; press <= undos; ++press) {
        const QString before = document->text();
        const bool ran = pane->triggerEditorAction(
            {QStringLiteral("edit_undo"), QStringLiteral("undo")}, QStringLiteral("undo"));
        QApplication::processEvents();
        const QString step = QStringLiteral("press=%1 text=%2").arg(press).arg(document->text().size());
        if (press == 1) {
            firstPressRan = ran;
        }
        // (a) identity: no re-created widget, no re-bound document.
        check(QStringLiteral("undo.%1.editor-identity").arg(press),
              pane->editorWidget() == view
                  && vedit::editor::viewFor(pane->editorWidget()) != nullptr
                  && vedit::editor::viewFor(pane->editorWidget())->document() == document,
              step);
        // (b) the opened file's text must survive every undo: either a real undo
        // step ran, or the loaded state is the floor and nothing is left to undo.
        check(QStringLiteral("undo.%1.buffer-is-opened-state-or-changed").arg(press),
              ran || document->text() == openedText, step);
        check(QStringLiteral("undo.%1.buffer-not-empty").arg(press), !document->text().isEmpty(),
              step);
        if (ran) {
            check(QStringLiteral("undo.%1.changed-something").arg(press),
                  document->text() != before, step);
        }
        // (d) the tab still shows the file.
        check(QStringLiteral("undo.%1.tab-shows-file").arg(press),
              pane->tabCount() == 1 && pane->tabFilePath(0) == path
                  && !pane->tabText(0).isEmpty(),
              QStringLiteral("%1 tabs=%2 label=%3")
                  .arg(step)
                  .arg(pane->tabCount())
                  .arg(pane->tabText(0)));
    }
    check(QStringLiteral("undo.first-press-runs"), firstPressRan);
    // The undo walk must have come back to the OPENED text, never to "".
    check(QStringLiteral("undo.floor-is-opened-text"), document->text() == openedText,
          QStringLiteral("text=%1 opened=%2")
              .arg(document->text().size())
              .arg(openedText.size()));

    // (c) editing is still alive after the undo walk.
    view->setCursorPosition(KTextEditor::Cursor(document->lines() - 1, 0));
    const int beforeAfterUndo = document->text().size();
    typeText(view, QStringLiteral("xyz"));
    check(QStringLiteral("edit.typing-after-undo"),
          document->text().size() > beforeAfterUndo,
          QStringLiteral("before=%1 after=%2")
              .arg(beforeAfterUndo)
              .arg(document->text().size()));

    // --- the real Ctrl+Z key path (the user's own input) -------------------
    // A physical Ctrl+Z is delivered to the editor's inner widget; the editor's
    // own standard action is what handles it. It must not walk past the opened
    // state either.
    QWidget *keyTarget = deliveryTarget(view, view->cursorToCoordinate(view->cursorPosition()));
    for (int press = 1; press <= 4; ++press) {
        if (keyTarget != nullptr) {
            QTest::keyClick(keyTarget, Qt::Key_Z, Qt::ControlModifier);
        }
        QApplication::processEvents();
        check(QStringLiteral("key-undo.%1.buffer-not-empty").arg(press),
              !document->text().isEmpty(),
              QStringLiteral("text=%1").arg(document->text().size()));
        check(QStringLiteral("key-undo.%1.editor-identity").arg(press),
              pane->editorWidget() == view && view->document() == document,
              QStringLiteral("text=%1").arg(document->text().size()));
    }
    check(QStringLiteral("key-undo.floor-is-opened-text"), document->text() == openedText,
          QStringLiteral("text=%1 opened=%2")
              .arg(document->text().size())
              .arg(openedText.size()));
    // Typing is still alive after the key-driven undo walk.
    view->setCursorPosition(KTextEditor::Cursor(document->lines() - 1, 0));
    const int beforeFinalTyping = document->text().size();
    typeText(view, QStringLiteral("tail"));
    check(QStringLiteral("edit.typing-after-key-undo"),
          document->text().size() > beforeFinalTyping,
          QStringLiteral("before=%1 after=%2")
              .arg(beforeFinalTyping)
              .arg(document->text().size()));

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
