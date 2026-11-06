// Lane selftest for T14 (AC-4.2 + AC-4.4). ADDENDUM A-10 (T14) grants this file.
//
//   vedit-editor-multi-cursor-selftest
//   vedit-editor-multi-cursor-selftest --screenshot <dir>
//
// Everything runs on a real KTextEditor::View in the pinned Qt6 image: the block
// selection, Alt+click and Ctrl+click use REAL QTest mouse events at coordinates
// produced by the public View::cursorToCoordinate(), and every editing key is a
// real QTest::keyClick. Exit 77 means the KTextEditor plugin is missing (ctest
// SKIP_RETURN_CODE), never 0.
#include "multi_cursor.h"

#if defined(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
#include "ktexteditor_provider.h"
#endif
#include "lsp_actions.h"
#include "undo_ops.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMainWindow>
#include <QStatusBar>
#include <QStringList>
#include <QTest>
#include <QVariantMap>

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

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

QString lineText(KTextEditor::Document *document, int line)
{
    return document != nullptr ? document->line(line) : QString();
}

QString detailedLines(KTextEditor::Document *document, int count)
{
    QStringList lines;
    for (int line = 0; line < count; ++line) {
        lines.append(QStringLiteral("[%1]='%2'").arg(line).arg(lineText(document, line)));
    }
    return lines.join(QStringLiteral(", "));
}

// The widget a REAL mouse event is delivered to: Qt routes the press to the
// deepest child under the point, which for KatePart is its inner view widget
// (`KateViewInternal`), NOT the public KTextEditor::View. Events sent to the View
// object arrive in view coordinates and therefore never exercise the coordinate
// mapping the product applies to the real delivery target.
QWidget *deliveryTarget(KTextEditor::View *view, const QPoint &viewPoint)
{
    if (view == nullptr) {
        return nullptr;
    }
    QWidget *target = view->childAt(viewPoint);
    return target != nullptr ? target : view;
}

QPoint pointInTarget(QWidget *target, KTextEditor::View *view, const QPoint &viewPoint)
{
    if (target == nullptr || target == view) {
        return viewPoint;
    }
    return target->mapFrom(view, viewPoint);
}

const QList<QKeySequence> &ownedSequences()
{
    static const QList<QKeySequence> sequences{
        QKeySequence(QStringLiteral("Ctrl+/")),       QKeySequence(QStringLiteral("Alt+Up")),
        QKeySequence(QStringLiteral("Alt+Down")),     QKeySequence(QStringLiteral("Shift+Alt+Down")),
        QKeySequence(QStringLiteral("Ctrl+Shift+K")), QKeySequence(QStringLiteral("Ctrl+Shift+U")),
        QKeySequence(QStringLiteral("Ctrl+Shift+L")), QKeySequence(QStringLiteral("Ctrl+=")),
        QKeySequence(QStringLiteral("Ctrl+-")),       QKeySequence(QStringLiteral("Ctrl+Shift+W")),
    };
    return sequences;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-multi-cursor-selftest"));

    QString screenshotDirectory;
    const QStringList arguments = QCoreApplication::arguments();
    const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        screenshotDirectory = arguments.at(screenshotIndex + 1);
    }

    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        std::printf("SKIP: KTextEditor::Editor::instance() is null (katepart missing)\n");
        return 77;
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    if (document == nullptr) {
        std::printf("SKIP: KTextEditor could not create a document (katepart missing)\n");
        return 77;
    }
    KTextEditor::View *view = document->createView(nullptr);
    if (view == nullptr) {
        std::printf("SKIP: KTextEditor could not create a view (katepart missing)\n");
        return 77;
    }
    view->resize(820, 420);
    view->show();
    view->setFocus();
    QApplication::processEvents();

    // Same install order as the product's EditCoreProvider: the LAST filter runs
    // FIRST, so MultiCursorController must be installed after EditorEnhancements.
    vedit::editor::EditorEnhancements *enhancements =
        vedit::editor::EditorEnhancements::install(view);
    vedit::editor::MultiCursorController *multi =
        vedit::editor::MultiCursorController::install(view);
    check(QStringLiteral("edit.controllers.installed"), enhancements != nullptr && multi != nullptr);
    if (enhancements == nullptr || multi == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }

    // Always install an explicit SINGLE cursor before a single-cursor scenario:
    // View::setCursorPosition() moves the primary cursor and does not clear a
    // multi-cursor set left by an earlier check.
    const auto keyTarget = [view]() -> QWidget * {
        QWidget *focus = QApplication::focusWidget();
        return focus != nullptr ? focus : view;
    };
    const auto singleCursor = [&](int line, int column) {
        vedit::editor::setViewCursors(view, {KTextEditor::Cursor(line, column)});
        QApplication::processEvents();
    };

    // --- the provider really installs both controllers --------------------
#if defined(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
    {
        vedit::editor::EditCoreProvider provider;
        QWidget *editorWidget = provider.createEditor(nullptr);
        check(QStringLiteral("edit.provider.editor"), editorWidget != nullptr);
        check(QStringLiteral("edit.provider.multi-cursor"),
              vedit::editor::MultiCursorController::instanceFor(editorWidget) != nullptr);
        check(QStringLiteral("edit.provider.enhancements"),
              vedit::editor::EditorEnhancements::instanceFor(editorWidget) != nullptr);
        delete editorWidget;
    }
#endif

    // --- the REAL focus path: keys go to KatePart's inner widget ----------
    // The lane's other key checks already use keyTarget(), but these assert the
    // target explicitly and exercise one key per family through it, so a filter
    // that only watched the public View cannot pass them.
    {
        view->activateWindow();
        view->setFocus();
        QApplication::processEvents();
        QWidget *inner = keyTarget();
        check(QStringLiteral("edit.real-focus.inner-widget"), inner != view && view->isAncestorOf(inner),
              inner != nullptr ? QString(inner->metaObject()->className()) : QStringLiteral("none"));
        document->setText(QStringLiteral("abc\ndef\nghi\n"));
        QApplication::processEvents();
        check(QStringLiteral("edit.real-focus.multi-cursor-setup"),
              vedit::editor::setViewCursors(view, {KTextEditor::Cursor(0, 0),
                                                   KTextEditor::Cursor(1, 0),
                                                   KTextEditor::Cursor(2, 0)})
                  .ok);
        inner = keyTarget();
        QTest::keyClick(inner, Qt::Key_Z);
        QApplication::processEvents();
        check(QStringLiteral("edit.real-focus.multi-cursor-typing"),
              lineText(document, 0) == QStringLiteral("zabc") &&
                  lineText(document, 1) == QStringLiteral("zdef") &&
                  lineText(document, 2) == QStringLiteral("zghi"),
              detailedLines(document, 3));
        document->setText(QStringLiteral("a\nb\nc\n"));
        QApplication::processEvents();
        check(QStringLiteral("edit.real-focus.line-setup"),
              vedit::editor::setViewCursors(view, {KTextEditor::Cursor(1, 0)}).ok);
        inner = keyTarget();
        QTest::keyClick(inner, Qt::Key_Up, Qt::AltModifier);
        QApplication::processEvents();
        check(QStringLiteral("edit.real-focus.line-op"),
              lineText(document, 0) == QStringLiteral("b") &&
                  lineText(document, 1) == QStringLiteral("a"),
              detailedLines(document, 2));
    }

    // --- Alt+drag block selection: real mouse drag, left column per line ----
    document->setText(QStringLiteral("one\ntwo\nthree\nfour\n"));
    QApplication::processEvents();
    singleCursor(0, 0);
    QApplication::processEvents();
    // Start at line 1: the view point of (0,0) is the inner widget's origin, and
    // QTest treats a QPoint(0,0) argument as "null" and substitutes the widget
    // centre. The rectangle is still the pointer's real rectangle.
    const QPoint blockStart = view->cursorToCoordinate(KTextEditor::Cursor(1, 0));
    const QPoint blockEnd = view->cursorToCoordinate(KTextEditor::Cursor(3, 2));
    // Drive the REAL delivery target, exactly as a user click does (see
    // deliveryTarget(); the plain-click control already worked this way).
    QWidget *blockTarget = deliveryTarget(view, blockStart);
    check(QStringLiteral("edit.block.inner-widget"),
          blockTarget != nullptr && blockTarget != view && view->isAncestorOf(blockTarget),
          blockTarget != nullptr ? QString(blockTarget->metaObject()->className())
                                 : QStringLiteral("none"));
    check(QStringLiteral("edit.block.inner-widget-same-for-end"),
          blockTarget == deliveryTarget(view, blockEnd));
    QTest::mousePress(blockTarget, Qt::LeftButton, Qt::AltModifier,
                      pointInTarget(blockTarget, view, blockStart));
    QTest::mouseMove(blockTarget, pointInTarget(blockTarget, view, blockEnd));
    QApplication::processEvents();
    check(QStringLiteral("edit.block.drag-active"), multi->blockSelectionActive());
    check(QStringLiteral("edit.block.drag-native-mode"), view->blockSelection());
    if (!screenshotDirectory.isEmpty()) {
        QDir().mkpath(screenshotDirectory);
        view->grab().save(screenshotDirectory + QStringLiteral("/block-selection-drag.png"));
    }
    QTest::mouseRelease(blockTarget, Qt::LeftButton, Qt::AltModifier,
                        pointInTarget(blockTarget, view, blockEnd));
    QApplication::processEvents();
    check(QStringLiteral("edit.block.cursor-count"), multi->cursorCount() == 3,
          QString::number(multi->cursorCount()));
    bool leftColumn = true;
    bool expectedLines = true;
    const QList<KTextEditor::Cursor> blockCursors = multi->cursors();
    for (int index = 0; index < static_cast<int>(blockCursors.size()); ++index) {
        const KTextEditor::Cursor &cursor = blockCursors.at(index);
        if (cursor.column() != 0) {
            leftColumn = false;
        }
        if (cursor != KTextEditor::Cursor(index + 1, 0)) {
            expectedLines = false;
        }
    }
    check(QStringLiteral("edit.block.left-column"), leftColumn);
    // The rectangle must be the one the pointer described: lines 1..3 at column
    // 0. An inner-widget coordinate mistake shifts it and fails here.
    check(QStringLiteral("edit.block.cursor-lines"), expectedLines,
          QStringLiteral("cursors=%1").arg(blockCursors.size()));
    check(QStringLiteral("edit.block.native-mode-off"), !view->blockSelection());
    const QString blockOriginal = document->text();
    check(QStringLiteral("edit.real-focus.block-typing-target"),
          keyTarget() != view && view->isAncestorOf(keyTarget()),
          keyTarget() != nullptr ? QString(keyTarget()->metaObject()->className())
                                 : QStringLiteral("none"));
    QTest::keyClicks(keyTarget(), QStringLiteral("X"));
    QApplication::processEvents();
    check(QStringLiteral("edit.block.rectangular-insert"),
          lineText(document, 0) == QStringLiteral("one") &&
              lineText(document, 1) == QStringLiteral("Xtwo") &&
              lineText(document, 2) == QStringLiteral("Xthree") &&
              lineText(document, 3) == QStringLiteral("Xfour"),
          detailedLines(document, 4));
    if (!screenshotDirectory.isEmpty()) {
        view->grab().save(screenshotDirectory + QStringLiteral("/multi-cursor-result.png"));
    }
    QString undoError;
    check(QStringLiteral("edit.block.one-undo"),
          vedit::editor::documentUndo(document, &undoError) &&
              document->text() == blockOriginal,
          undoError);

    // --- Alt+click adds, Alt+click on an existing cursor removes -----------
    // UX batch 2 (item 13): the add-cursor gesture moved from Ctrl+click to
    // Alt+click; Ctrl+click is "go to definition" (asserted further below).
    document->setText(QStringLiteral("aaa\nbbb\nccc\n"));
    QApplication::processEvents();
    singleCursor(0, 1);
    QApplication::processEvents();
    const KTextEditor::Cursor altClickCursor(1, 1);
    const QPoint altClickPoint = view->cursorToCoordinate(altClickCursor);
    const auto altClick = [&]() {
        // The real delivery target, not the View (item 13's Alt+click is the
        // gesture the review measured as offset).
        QWidget *target = deliveryTarget(view, altClickPoint);
        QTest::mousePress(target, Qt::LeftButton, Qt::AltModifier,
                          pointInTarget(target, view, altClickPoint));
        QTest::mouseRelease(target, Qt::LeftButton, Qt::AltModifier,
                            pointInTarget(target, view, altClickPoint));
        QApplication::processEvents();
    };
    altClick();
    check(QStringLiteral("edit.alt-click.add"), multi->cursorCount() == 2,
          QString::number(multi->cursorCount()));
    check(QStringLiteral("edit.alt-click.clicked-position"),
          multi->cursors().contains(altClickCursor),
          QStringLiteral("cursors=%1").arg(multi->cursorCount()));
    altClick();
    check(QStringLiteral("edit.alt-click.toggle-off"), multi->cursorCount() == 1,
          QString::number(multi->cursorCount()));
    altClick();
    check(QStringLiteral("edit.alt-click.readd"), multi->cursorCount() == 2);
    const QString altOriginal = document->text();
    QTest::keyClick(keyTarget(), Qt::Key_Y);
    QApplication::processEvents();
    check(QStringLiteral("edit.alt-click.typing"),
          lineText(document, 0) == QStringLiteral("ayaa") &&
              lineText(document, 1) == QStringLiteral("bybb"),
          detailedLines(document, 2));
    undoError.clear();
    check(QStringLiteral("edit.alt-click.one-undo"),
          vedit::editor::documentUndo(document, &undoError) &&
              document->text() == altOriginal,
          undoError);

    // --- multi-cursor typing / Backspace / Delete / Return: one undo each --
    const auto resetThreeCursors = [&](const QList<KTextEditor::Cursor> &positions) {
        document->setText(QStringLiteral("abc\ndef\nghi\n"));
        QApplication::processEvents();
        return vedit::editor::setViewCursors(view, positions).ok;
    };
    check(QStringLiteral("edit.multi.three-cursors"),
          resetThreeCursors({KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                             KTextEditor::Cursor(2, 0)}));
    QString multiOriginal = document->text();
    QTest::keyClick(keyTarget(), Qt::Key_Z);
    QApplication::processEvents();
    check(QStringLiteral("edit.multi.type"),
          lineText(document, 0) == QStringLiteral("zabc") &&
              lineText(document, 1) == QStringLiteral("zdef") &&
              lineText(document, 2) == QStringLiteral("zghi"),
          detailedLines(document, 3));
    undoError.clear();
    check(QStringLiteral("edit.multi.type-one-undo"),
          vedit::editor::documentUndo(document, &undoError) &&
              document->text() == multiOriginal,
          undoError);

    check(QStringLiteral("edit.multi.backspace-setup"),
          resetThreeCursors({KTextEditor::Cursor(0, 1), KTextEditor::Cursor(1, 1),
                             KTextEditor::Cursor(2, 1)}));
    multiOriginal = document->text();
    QTest::keyClick(keyTarget(), Qt::Key_Backspace);
    QApplication::processEvents();
    check(QStringLiteral("edit.multi.backspace"),
          lineText(document, 0) == QStringLiteral("bc") &&
              lineText(document, 1) == QStringLiteral("ef") &&
              lineText(document, 2) == QStringLiteral("hi"),
          detailedLines(document, 3));
    undoError.clear();
    check(QStringLiteral("edit.multi.backspace-one-undo"),
          vedit::editor::documentUndo(document, &undoError) &&
              document->text() == multiOriginal,
          undoError);

    check(QStringLiteral("edit.multi.delete-setup"),
          resetThreeCursors({KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                             KTextEditor::Cursor(2, 0)}));
    multiOriginal = document->text();
    QTest::keyClick(keyTarget(), Qt::Key_Delete);
    QApplication::processEvents();
    check(QStringLiteral("edit.multi.delete"),
          lineText(document, 0) == QStringLiteral("bc") &&
              lineText(document, 1) == QStringLiteral("ef") &&
              lineText(document, 2) == QStringLiteral("hi"),
          detailedLines(document, 3));
    undoError.clear();
    check(QStringLiteral("edit.multi.delete-one-undo"),
          vedit::editor::documentUndo(document, &undoError) &&
              document->text() == multiOriginal,
          undoError);

    check(QStringLiteral("edit.multi.return-setup"),
          resetThreeCursors({KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                             KTextEditor::Cursor(2, 0)}));
    multiOriginal = document->text();
    QTest::keyClick(keyTarget(), Qt::Key_Return);
    QApplication::processEvents();
    check(QStringLiteral("edit.multi.return"),
          lineText(document, 0).isEmpty() && lineText(document, 1) == QStringLiteral("abc") &&
              lineText(document, 2).isEmpty() && lineText(document, 3) == QStringLiteral("def"),
          detailedLines(document, 4));
    undoError.clear();
    check(QStringLiteral("edit.multi.return-one-undo"),
          vedit::editor::documentUndo(document, &undoError) &&
              document->text() == multiOriginal,
          undoError);

    // --- AC-4.4: auto-indent on Return ------------------------------------
    document->setText(QStringLiteral("    int a;\n"));
    QApplication::processEvents();
    singleCursor(0, document->lineLength(0));
    QTest::keyClick(keyTarget(), Qt::Key_Return);
    QApplication::processEvents();
    check(QStringLiteral("edit.return.auto-indent"), lineText(document, 1) == QStringLiteral("    "),
          detailedLines(document, 2));
    check(QStringLiteral("edit.return.cursor-after-indent"),
          view->cursorPosition() == KTextEditor::Cursor(1, 4));

    document->setText(QStringLiteral("if (x) {"));
    QApplication::processEvents();
    singleCursor(0, document->lineLength(0));
    QTest::keyClick(keyTarget(), Qt::Key_Return);
    QApplication::processEvents();
    check(QStringLiteral("edit.return.brace-indent"), lineText(document, 1) == QStringLiteral("    "),
          detailedLines(document, 2));

    // --- AC-4.4: bracket / quote auto-pair, skip-over, pair Backspace ------
    // Control: ordinary typing (no pairing) must reach the view unchanged.
    document->setText(QStringLiteral("abc"));
    QApplication::processEvents();
    singleCursor(0, 1);
    view->activateWindow();
    view->setFocus();
    QApplication::processEvents();
    QTest::keyClicks(keyTarget(), QStringLiteral("Z"));
    QApplication::processEvents();
    check(QStringLiteral("edit.typing.control"), document->line(0) == QStringLiteral("aZbc"),
          QStringLiteral("line='%1' cursor=%2,%3 cursors=%4")
              .arg(document->line(0))
              .arg(view->cursorPosition().line())
              .arg(view->cursorPosition().column())
              .arg(multi->cursorCount()));
    document->setText(QString());
    QApplication::processEvents();
    singleCursor(0, 0);
    QTest::keyClicks(keyTarget(), QStringLiteral("("));
    QApplication::processEvents();
    check(QStringLiteral("edit.pair.round"), document->text() == QStringLiteral("()"),
          document->text());
    check(QStringLiteral("edit.pair.inside"), view->cursorPosition() == KTextEditor::Cursor(0, 1));
    QTest::keyClicks(keyTarget(), QStringLiteral(")"));
    QApplication::processEvents();
    check(QStringLiteral("edit.pair.skip-over"),
          document->text() == QStringLiteral("()") &&
              view->cursorPosition() == KTextEditor::Cursor(0, 2),
          document->text());
    singleCursor(0, 1);
    check(QStringLiteral("edit.pair.backspace-precondition"),
          view->cursorPosition() == KTextEditor::Cursor(0, 1) && multi->cursorCount() == 1,
          QStringLiteral("cursor=%1,%2 cursors=%3 text='%4'")
              .arg(view->cursorPosition().line())
              .arg(view->cursorPosition().column())
              .arg(multi->cursorCount())
              .arg(document->text()));
    QTest::keyClick(keyTarget(), Qt::Key_Backspace);
    QApplication::processEvents();
    check(QStringLiteral("edit.pair.backspace-deletes-both"), document->text().isEmpty(),
          document->text());
    document->setText(QString());
    QApplication::processEvents();
    singleCursor(0, 0);
    QTest::keyClicks(keyTarget(), QStringLiteral("\""));
    check(QStringLiteral("edit.pair.quote-precondition"),
          document->text() == QStringLiteral("\"\"") &&
              view->cursorPosition() == KTextEditor::Cursor(0, 1),
          QStringLiteral("text='%1' cursor=%2,%3")
              .arg(document->text())
              .arg(view->cursorPosition().line())
              .arg(view->cursorPosition().column()));
    QTest::keyClicks(keyTarget(), QStringLiteral("a"));
    QApplication::processEvents();
    check(QStringLiteral("edit.pair.quote"), document->text() == QStringLiteral("\"a\""),
          document->text());

    // --- AC-4.4: Ctrl+/ comment toggle ------------------------------------
    document->setText(QStringLiteral("    code;\n"));
    QApplication::processEvents();
    singleCursor(0, 6);
    QTest::keyClick(keyTarget(), Qt::Key_Slash, Qt::ControlModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.comment.on"), lineText(document, 0) == QStringLiteral("    // code;"),
          lineText(document, 0));
    QTest::keyClick(keyTarget(), Qt::Key_Slash, Qt::ControlModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.comment.off"), lineText(document, 0) == QStringLiteral("    code;"),
          lineText(document, 0));

    // --- AC-4.4: move / duplicate / delete line ---------------------------
    document->setText(QStringLiteral("a\nb\nc\n"));
    QApplication::processEvents();
    singleCursor(1, 0);
    QTest::keyClick(keyTarget(), Qt::Key_Up, Qt::AltModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.move.up"),
          lineText(document, 0) == QStringLiteral("b") &&
              lineText(document, 1) == QStringLiteral("a") &&
              lineText(document, 2) == QStringLiteral("c") &&
              view->cursorPosition().line() == 0,
          detailedLines(document, 3));
    QTest::keyClick(keyTarget(), Qt::Key_Down, Qt::AltModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.move.down"),
          lineText(document, 0) == QStringLiteral("a") &&
              lineText(document, 1) == QStringLiteral("b") &&
              view->cursorPosition().line() == 1,
          detailedLines(document, 3));
    QTest::keyClick(keyTarget(), Qt::Key_Down, Qt::ShiftModifier | Qt::AltModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.duplicate.line"),
          lineText(document, 0) == QStringLiteral("a") &&
              lineText(document, 1) == QStringLiteral("b") &&
              lineText(document, 2) == QStringLiteral("b") &&
              lineText(document, 3) == QStringLiteral("c"),
          detailedLines(document, 4));
    singleCursor(1, 0);
    QTest::keyClick(keyTarget(), Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.delete.line"),
          lineText(document, 0) == QStringLiteral("a") &&
              lineText(document, 1) == QStringLiteral("b") &&
              lineText(document, 2) == QStringLiteral("c"),
          detailedLines(document, 3));

    // --- AC-4.4: case conversion -----------------------------------------
    document->setText(QStringLiteral("abc DEF\n"));
    QApplication::processEvents();
    singleCursor(0, 0);
    QTest::keyClick(keyTarget(), Qt::Key_U, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.case.upper"), lineText(document, 0) == QStringLiteral("ABC DEF"),
          lineText(document, 0));
    QTest::keyClick(keyTarget(), Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.case.lower"), lineText(document, 0) == QStringLiteral("abc def"),
          lineText(document, 0));

    // --- AC-4.4: font zoom (config value changes and reverts) -------------
    const double fontBefore = enhancements->fontPointSize();
    QTest::keyClick(keyTarget(), Qt::Key_Equal, Qt::ControlModifier);
    QApplication::processEvents();
    const double fontZoomed = enhancements->fontPointSize();
    check(QStringLiteral("edit.zoom.in"), fontZoomed > fontBefore,
          QStringLiteral("%1 -> %2").arg(fontBefore).arg(fontZoomed));
    check(QStringLiteral("edit.zoom.config-changed"),
          view->configValue(QStringLiteral("font")).value<QFont>().pointSizeF() == fontZoomed,
          QString::number(view->configValue(QStringLiteral("font")).value<QFont>().pointSizeF()));
    QTest::keyClick(keyTarget(), Qt::Key_Minus, Qt::ControlModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.zoom.out"), enhancements->fontPointSize() == fontBefore,
          QStringLiteral("%1").arg(enhancements->fontPointSize()));

    // --- AC-4.4: word wrap toggles the config key -------------------------
    const bool wrapBefore = enhancements->wordWrapEnabled();
    QTest::keyClick(keyTarget(), Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.wrap.flip"), enhancements->wordWrapEnabled() != wrapBefore);
    check(QStringLiteral("edit.wrap.config"),
          view->configValue(QStringLiteral("dynamic-word-wrap")).toBool() ==
              enhancements->wordWrapEnabled());
    QTest::keyClick(keyTarget(), Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::processEvents();
    check(QStringLiteral("edit.wrap.back"), enhancements->wordWrapEnabled() == wrapBefore);

    // --- AC-4.4: bracket-match highlight (KatePart's native, always-on) -----
    // The KF6 6.24.0 View interface documents a
    // `disable-bracket-match-highlight-if-inactive` key but does not accept it
    // (configKeys() omits it, set/get round-trip stays invalid), so there is no
    // knob to assert. The feature itself is proven on pixels: with the cursor
    // right after '(', KatePart paints that bracket and its match in the
    // highlight colour; one column away it paints nothing there. Same document,
    // same font, and the cursor block never overlaps the sampled glyph.
    const QString bracketKey = QStringLiteral("disable-bracket-match-highlight-if-inactive");
    std::printf("CHECK-DETAIL bracket config key '%s': listed=%d accepted=%d\n",
                qPrintable(bracketKey), view->configKeys().contains(bracketKey) ? 1 : 0,
                view->configValue(bracketKey).isValid() ? 1 : 0);
    document->setText(QStringLiteral("x = (1 + 2)\n"));
    QApplication::processEvents();
    singleCursor(0, 5); // immediately after '(' at column 4
    view->setFocus();
    QApplication::processEvents();
    const QImage highlighted = view->grab().toImage();
    singleCursor(0, 1); // on a space, next to no bracket
    QApplication::processEvents();
    const QImage plain = view->grab().toImage();
    const QPoint bracketPoint = view->cursorToCoordinate(KTextEditor::Cursor(0, 4));
    int differentPixels = 0;
    if (bracketPoint.x() >= 0 && bracketPoint.y() >= 0 &&
        highlighted.size() == plain.size()) {
        for (int y = bracketPoint.y(); y < bracketPoint.y() + 14 && y < highlighted.height(); ++y) {
            for (int x = bracketPoint.x(); x < bracketPoint.x() + 10 && x < highlighted.width();
                 ++x) {
                if (highlighted.pixelColor(x, y) != plain.pixelColor(x, y)) {
                    ++differentPixels;
                }
            }
        }
    }
    check(QStringLiteral("edit.bracket.native-highlight"), differentPixels > 20,
          QStringLiteral("pixels=%1 point=%2,%3")
              .arg(differentPixels)
              .arg(bracketPoint.x())
              .arg(bracketPoint.y()));

    // --- single-owner keyboard rule (same rule as the find bar) -----------
    bool secondOwner = false;
    for (QAction *action : view->findChildren<QAction *>()) {
        for (const QKeySequence &sequence : action->shortcuts()) {
            if (ownedSequences().contains(sequence)) {
                secondOwner = true;
                std::printf("CHECK-DETAIL second owner: '%s' -> '%s'\n",
                            qPrintable(action->text()),
                            qPrintable(sequence.toString(QKeySequence::PortableText)));
            }
        }
    }
    check(QStringLiteral("edit.keys.single-owner"), !secondOwner);

    // Evidence for the native bracket-match highlight: this build exposes no
    // config key for it (recorded above), so a picture of the real view with the
    // cursor on a bracket is the only observable.
    if (!screenshotDirectory.isEmpty()) {
        document->setText(QStringLiteral(
            "x = (1 + (2 * 3))\nvoid f() {\n  body();\n}\n"));
        QApplication::processEvents();
        // Cursor immediately after the outer '(': KatePart highlights that
        // bracket and its match when the view has focus.
        singleCursor(0, 5);
        view->setFocus();
        QApplication::processEvents();
        view->grab().save(screenshotDirectory + QStringLiteral("/bracket-match.png"));
    }

    // --- UX batch 2 (item 13): Ctrl+click = go to definition ----------------
    // REAL seam, no mock of the dispatch: the QAction the shell's F12 command
    // and Project menu trigger (same object name the command registry creates)
    // is connected to a recorder. The recorder reads the view's file and caret
    // exactly where the shell's navigation handler reads them, so "the
    // definition action ran with the clicked file/line/column" is asserted on
    // the channel's own dispatch -> shell action -> trigger path.
    {
        QMainWindow definitionHost;
        KTextEditor::Document *definitionDocument = editor->createDocument(nullptr);
        check(QStringLiteral("definition.document"), definitionDocument != nullptr);
        KTextEditor::View *definitionView =
            definitionDocument != nullptr ? definitionDocument->createView(&definitionHost)
                                          : nullptr;
        check(QStringLiteral("definition.view"), definitionView != nullptr);
        if (definitionView != nullptr) {
            definitionHost.setCentralWidget(definitionView);
            definitionHost.resize(820, 420);
            definitionHost.show();
            definitionDocument->setText(
                QStringLiteral("module top;\n  wire sig;\nendmodule\n"));
            definitionView->setProperty("vedit.openFilePath",
                                        QStringLiteral("/tmp/b4-definition.sv"));
            QApplication::processEvents();

            auto *definitionAction =
                new QAction(QStringLiteral("&Go to Definition"), &definitionHost);
            definitionAction->setObjectName(QStringLiteral("action-diagnostics.definition"));
            definitionAction->setEnabled(true);
            definitionHost.addAction(definitionAction);
            int triggered = 0;
            QString dispatchedFile;
            int dispatchedLine = -1;
            int dispatchedColumn = -1;
            QObject::connect(definitionAction, &QAction::triggered, &definitionHost, [&]() {
                ++triggered;
                dispatchedFile = vedit::editor::viewFilePath(definitionView);
                const KTextEditor::Cursor caret = definitionView->cursorPosition();
                dispatchedLine = caret.line();
                dispatchedColumn = caret.column();
            });

            QVariantMap gates;
            QVariantMap definitionRow;
            definitionRow.insert(QStringLiteral("enabled"), true);
            definitionRow.insert(QStringLiteral("known"), true);
            definitionRow.insert(QStringLiteral("source"), QStringLiteral("backend"));
            definitionRow.insert(QStringLiteral("reason"), QString());
            gates.insert(QStringLiteral("definition"), definitionRow);
            definitionHost.setProperty("vedit.diagnostics.gates", gates);
            QVariantMap capabilities;
            capabilities.insert(QStringLiteral("backend"), QStringLiteral("verible"));
            definitionHost.setProperty("vedit.diagnostics.capabilities",
                                       capabilities);

            vedit::editor::LspActionChannel *definitionChannel =
                vedit::editor::LspActionChannel::install(definitionView);
            vedit::editor::MultiCursorController *definitionCursors =
                vedit::editor::MultiCursorController::install(definitionView);
            check(QStringLiteral("definition.seams"),
                  definitionChannel != nullptr && definitionCursors != nullptr);
            check(QStringLiteral("definition.action-is-the-f12-path"),
                  definitionChannel != nullptr
                      && definitionChannel->shellAction(vedit::editor::EditorAction::Definition)
                          == definitionAction);

            if (definitionChannel != nullptr && definitionCursors != nullptr) {
                const KTextEditor::Cursor clickCursor(1, 2);
                const QPoint clickPoint = definitionView->cursorToCoordinate(clickCursor);
                check(QStringLiteral("definition.click-coordinate"),
                      clickPoint.x() >= 0 && clickPoint.y() >= 0,
                      QStringLiteral("%1,%2").arg(clickPoint.x()).arg(clickPoint.y()));

                // A real click is delivered to the deepest child under the point
                // (KatePart's KateViewInternal), NOT to the public View; the old
                // checks sent QTest events to the View and so never exercised the
                // coordinate mapping the product applies to the delivered event.
                QWidget *clickTarget = deliveryTarget(definitionView, clickPoint);
                check(QStringLiteral("definition.ctrl-click.inner-widget"),
                      clickTarget != nullptr && clickTarget != definitionView
                          && definitionView->isAncestorOf(clickTarget),
                      clickTarget != nullptr ? QString(clickTarget->metaObject()->className())
                                             : QStringLiteral("none"));
                const auto clickAt = [&](const KTextEditor::Cursor &cursor,
                                         Qt::KeyboardModifiers modifiers) {
                    const QPoint point = definitionView->cursorToCoordinate(cursor);
                    QWidget *target = deliveryTarget(definitionView, point);
                    const QPoint local = pointInTarget(target, definitionView, point);
                    QTest::mousePress(target, Qt::LeftButton, modifiers, local);
                    QTest::mouseRelease(target, Qt::LeftButton, modifiers, local);
                    QApplication::processEvents();
                };
                clickAt(clickCursor, Qt::ControlModifier);
                check(QStringLiteral("definition.ctrl-click.dispatched"),
                      definitionChannel->dispatchCount(vedit::editor::EditorAction::Definition)
                          == 1,
                      QString::number(definitionChannel->dispatchCount(
                          vedit::editor::EditorAction::Definition)));
                check(QStringLiteral("definition.ctrl-click.action-triggered"), triggered == 1,
                      QString::number(triggered));
                check(QStringLiteral("definition.ctrl-click.file"),
                      dispatchedFile == QStringLiteral("/tmp/b4-definition.sv"), dispatchedFile);
                check(QStringLiteral("definition.ctrl-click.line-column"),
                      dispatchedLine == clickCursor.line()
                          && dispatchedColumn == clickCursor.column(),
                      QStringLiteral("%1:%2 (expected %3:%4)")
                          .arg(dispatchedLine)
                          .arg(dispatchedColumn)
                          .arg(clickCursor.line())
                          .arg(clickCursor.column()));
                check(QStringLiteral("definition.ctrl-click.document-unchanged"),
                      definitionDocument->text()
                          == QStringLiteral("module top;\n  wire sig;\nendmodule\n"));
                check(QStringLiteral("definition.ctrl-click.no-added-cursor"),
                      definitionCursors->cursorCount() == 1,
                      QString::number(definitionCursors->cursorCount()));

                // --- R2: a successful Ctrl+click collapses a 3-cursor set ---
                // The consumed press suppresses the native click that would
                // collapse the set, so the seam must install the single position
                // itself. Control first: an ordinary click from three cursors
                // collapses to one (KatePart's own behaviour).
                check(QStringLiteral("definition.collapse.plain-setup"),
                      vedit::editor::setViewCursors(
                          definitionView,
                          {KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                           KTextEditor::Cursor(2, 0)})
                          .ok);
                clickAt(KTextEditor::Cursor(2, 4), Qt::NoModifier);
                check(QStringLiteral("definition.collapse.plain-click"),
                      definitionCursors->cursorCount() == 1
                          && definitionView->cursorPosition() == KTextEditor::Cursor(2, 4),
                      QStringLiteral("count=%1 caret=%2:%3")
                          .arg(definitionCursors->cursorCount())
                          .arg(definitionView->cursorPosition().line())
                          .arg(definitionView->cursorPosition().column()));

                check(QStringLiteral("definition.collapse.ctrl-setup"),
                      vedit::editor::setViewCursors(
                          definitionView,
                          {KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                           KTextEditor::Cursor(2, 0)})
                          .ok,
                      QString::number(definitionCursors->cursorCount()));
                const int triggeredBeforeCollapse = triggered;
                clickAt(clickCursor, Qt::ControlModifier);
                check(QStringLiteral("definition.collapse.ctrl-click"),
                      definitionCursors->cursorCount() == 1,
                      QStringLiteral("count=%1 (a Ctrl+click is a click and must collapse "
                                     "the set)")
                          .arg(definitionCursors->cursorCount()));
                check(QStringLiteral("definition.collapse.ctrl-click-position"),
                      definitionView->cursorPosition() == clickCursor
                          && dispatchedLine == clickCursor.line()
                          && dispatchedColumn == clickCursor.column(),
                      QStringLiteral("caret=%1:%2 dispatched=%3:%4")
                          .arg(definitionView->cursorPosition().line())
                          .arg(definitionView->cursorPosition().column())
                          .arg(dispatchedLine)
                          .arg(dispatchedColumn));
                check(QStringLiteral("definition.collapse.ctrl-click-dispatched"),
                      triggered == triggeredBeforeCollapse + 1,
                      QStringLiteral("%1 -> %2").arg(triggeredBeforeCollapse).arg(triggered));

                // --- the backend gate is off: plain click + ONE reason ------
                const QString backendGoneReason =
                    QStringLiteral("verible-verilog-ls does not advertise definitionProvider");
                definitionRow.insert(QStringLiteral("enabled"), false);
                definitionRow.insert(QStringLiteral("reason"), backendGoneReason);
                gates.insert(QStringLiteral("definition"), definitionRow);
                definitionHost.setProperty("vedit.diagnostics.gates", gates);
                // The lane greys the wired action from the same decision; mirror
                // that here so the channel sees the same state as in the app.
                definitionAction->setEnabled(false);
                definitionAction->setToolTip(backendGoneReason);
                QApplication::processEvents();

                // Plain clicks must go to the real widget the user clicks
                // (KatePart's inner view), so the "behaves like a plain click"
                // assertion is made against KatePart's own mouse handling.
                definitionView->setFocus(Qt::MouseFocusReason);
                QApplication::processEvents();
                QWidget *focused = QApplication::focusWidget();
                const bool realInner =
                    focused != nullptr && focused != definitionView
                    && definitionView->isAncestorOf(focused);
                check(QStringLiteral("definition.plain-click.inner-widget"), realInner,
                      focused != nullptr ? QString(focused->metaObject()->className())
                                         : QStringLiteral("none"));

                // Control: an ordinary click through the same path moves the
                // caret, so the disabled behaviour below is comparable.
                clickAt(KTextEditor::Cursor(2, 4), Qt::NoModifier);
                check(QStringLiteral("definition.plain-click.caret"),
                      definitionView->cursorPosition() == KTextEditor::Cursor(2, 4),
                      QStringLiteral("%1:%2")
                          .arg(definitionView->cursorPosition().line())
                          .arg(definitionView->cursorPosition().column()));

                const QString beforeDisabled = definitionDocument->text();
                const int dispatchBefore = definitionChannel->dispatchCount(
                    vedit::editor::EditorAction::Definition);
                const int triggeredBefore = triggered;
                definitionHost.statusBar()->clearMessage();
                clickAt(KTextEditor::Cursor(1, 4), Qt::ControlModifier);
                const QString reasonMessage = definitionHost.statusBar()->currentMessage();
                check(QStringLiteral("definition.gate-off.no-request"),
                      triggered == triggeredBefore
                          && definitionChannel->dispatchCount(
                                 vedit::editor::EditorAction::Definition)
                              == dispatchBefore,
                      QStringLiteral("triggered=%1 dispatches=%2")
                          .arg(triggered)
                          .arg(definitionChannel->dispatchCount(
                              vedit::editor::EditorAction::Definition)));
                check(QStringLiteral("definition.gate-off.document-unchanged"),
                      definitionDocument->text() == beforeDisabled);
                check(QStringLiteral("definition.gate-off.plain-click"),
                      definitionView->cursorPosition() == KTextEditor::Cursor(1, 4),
                      QStringLiteral("%1:%2")
                          .arg(definitionView->cursorPosition().line())
                          .arg(definitionView->cursorPosition().column()));
                check(QStringLiteral("definition.gate-off.status-reason"),
                      reasonMessage.contains(QStringLiteral("Go to Definition is unavailable"))
                          && reasonMessage.contains(
                              QStringLiteral("does not advertise definitionProvider")),
                      reasonMessage);

                // One-time: a second unavailable Ctrl+click posts no message.
                definitionHost.statusBar()->clearMessage();
                clickAt(KTextEditor::Cursor(2, 2), Qt::ControlModifier);
                check(QStringLiteral("definition.gate-off.one-time-message"),
                      definitionHost.statusBar()->currentMessage().isEmpty(),
                      definitionHost.statusBar()->currentMessage());
                check(QStringLiteral("definition.gate-off.second-plain-click"),
                      definitionView->cursorPosition() == KTextEditor::Cursor(2, 2),
                      QStringLiteral("%1:%2")
                          .arg(definitionView->cursorPosition().line())
                          .arg(definitionView->cursorPosition().column()));
                check(QStringLiteral("definition.gate-off.still-no-request"),
                      triggered == triggeredBefore);

                // --- R3: the published gate row and the QAction are TWO gates.
                // The old scenario turned both off together, so a product that
                // honours only one of them still passed. Disable only the gate
                // row first, then only the action.
                const QString gateOnlyReason =
                    QStringLiteral("gate-only: verible-verilog-ls does not advertise "
                                   "definitionProvider");
                definitionRow.insert(QStringLiteral("enabled"), false);
                definitionRow.insert(QStringLiteral("reason"), gateOnlyReason);
                gates.insert(QStringLiteral("definition"), definitionRow);
                definitionAction->setEnabled(true);
                definitionAction->setToolTip(QString());
                definitionHost.setProperty("vedit.diagnostics.gates", gates);
                QApplication::processEvents();
                const vedit::editor::ActionAvailability gateOnly =
                    definitionChannel->availability(vedit::editor::EditorAction::Definition);
                check(QStringLiteral("definition.gate-only.action-enabled"),
                      definitionAction->isEnabled());
                check(QStringLiteral("definition.gate-only.availability"),
                      !gateOnly.enabled && gateOnly.reason == gateOnlyReason, gateOnly.reason);
                const int gateOnlyTriggered = triggered;
                definitionHost.statusBar()->clearMessage();
                clickAt(KTextEditor::Cursor(2, 3), Qt::ControlModifier);
                check(QStringLiteral("definition.gate-only.no-request"),
                      triggered == gateOnlyTriggered);
                check(QStringLiteral("definition.gate-only.plain-click"),
                      definitionView->cursorPosition() == KTextEditor::Cursor(2, 3),
                      QStringLiteral("%1:%2")
                          .arg(definitionView->cursorPosition().line())
                          .arg(definitionView->cursorPosition().column()));
                check(QStringLiteral("definition.gate-only.status-reason"),
                      definitionHost.statusBar()->currentMessage().contains(gateOnlyReason),
                      definitionHost.statusBar()->currentMessage());

                const QString actionOnlyReason =
                    QStringLiteral("action-only: the definition QAction is disabled");
                definitionRow.insert(QStringLiteral("enabled"), true);
                definitionRow.insert(QStringLiteral("reason"), QString());
                gates.insert(QStringLiteral("definition"), definitionRow);
                definitionAction->setEnabled(false);
                definitionAction->setToolTip(actionOnlyReason);
                definitionHost.setProperty("vedit.diagnostics.gates", gates);
                QApplication::processEvents();
                const vedit::editor::ActionAvailability actionOnly =
                    definitionChannel->availability(vedit::editor::EditorAction::Definition);
                check(QStringLiteral("definition.action-only.gate-row-enabled"),
                      definitionRow.value(QStringLiteral("enabled")).toBool());
                check(QStringLiteral("definition.action-only.availability"),
                      !actionOnly.enabled && actionOnly.reason == actionOnlyReason,
                      actionOnly.reason);
                const int actionOnlyTriggered = triggered;
                definitionHost.statusBar()->clearMessage();
                clickAt(KTextEditor::Cursor(2, 4), Qt::ControlModifier);
                check(QStringLiteral("definition.action-only.no-request"),
                      triggered == actionOnlyTriggered);
                check(QStringLiteral("definition.action-only.plain-click"),
                      definitionView->cursorPosition() == KTextEditor::Cursor(2, 4),
                      QStringLiteral("%1:%2")
                          .arg(definitionView->cursorPosition().line())
                          .arg(definitionView->cursorPosition().column()));
                check(QStringLiteral("definition.action-only.status-reason"),
                      definitionHost.statusBar()->currentMessage().contains(actionOnlyReason),
                      definitionHost.statusBar()->currentMessage());

                // --- R4: the one-time latch re-arms when availability returns.
                // An episode ends when the shell reports the action available
                // again (applyGate + publishGates), even if the recovery is seen
                // by F12 and no successful Ctrl+click happens.
                const QString rearmReason =
                    QStringLiteral("rearm: the definition backend went away");
                definitionRow.insert(QStringLiteral("enabled"), false);
                definitionRow.insert(QStringLiteral("reason"), rearmReason);
                gates.insert(QStringLiteral("definition"), definitionRow);
                definitionAction->setEnabled(false);
                definitionHost.setProperty("vedit.diagnostics.gates", gates);
                QApplication::processEvents();
                definitionHost.statusBar()->clearMessage();
                clickAt(KTextEditor::Cursor(1, 4), Qt::ControlModifier);
                check(QStringLiteral("definition.rearm.first-message"),
                      definitionHost.statusBar()->currentMessage().contains(rearmReason),
                      definitionHost.statusBar()->currentMessage());
                definitionHost.statusBar()->clearMessage();
                clickAt(KTextEditor::Cursor(1, 4), Qt::ControlModifier);
                check(QStringLiteral("definition.rearm.one-time"),
                      definitionHost.statusBar()->currentMessage().isEmpty(),
                      definitionHost.statusBar()->currentMessage());

                // Availability comes back without a successful Ctrl+click. The
                // product order is applyGate() (action) first, publishGates()
                // (property) second, so only the host property change observes
                // the final state.
                definitionAction->setEnabled(true);
                definitionAction->setToolTip(QString());
                definitionRow.insert(QStringLiteral("enabled"), true);
                definitionRow.insert(QStringLiteral("reason"), QString());
                gates.insert(QStringLiteral("definition"), definitionRow);
                definitionHost.setProperty("vedit.diagnostics.gates", gates);
                QApplication::processEvents();
                check(QStringLiteral("definition.rearm.restored"),
                      definitionChannel->availability(vedit::editor::EditorAction::Definition)
                          .enabled);

                // It goes away again before any Ctrl+click: the new episode must
                // surface its reason again (the latch re-armed on restoration).
                definitionAction->setEnabled(false);
                definitionRow.insert(QStringLiteral("enabled"), false);
                definitionRow.insert(QStringLiteral("reason"), rearmReason);
                gates.insert(QStringLiteral("definition"), definitionRow);
                definitionHost.setProperty("vedit.diagnostics.gates", gates);
                QApplication::processEvents();
                definitionHost.statusBar()->clearMessage();
                clickAt(KTextEditor::Cursor(1, 4), Qt::ControlModifier);
                check(QStringLiteral("definition.rearm.message-after-restore"),
                      definitionHost.statusBar()->currentMessage().contains(rearmReason),
                      definitionHost.statusBar()->currentMessage());

                // A changed reason is new information even inside one episode.
                const QString changedReason =
                    QStringLiteral("rearm: a different definition reason");
                definitionRow.insert(QStringLiteral("reason"), changedReason);
                gates.insert(QStringLiteral("definition"), definitionRow);
                definitionHost.setProperty("vedit.diagnostics.gates", gates);
                QApplication::processEvents();
                definitionHost.statusBar()->clearMessage();
                clickAt(KTextEditor::Cursor(1, 4), Qt::ControlModifier);
                check(QStringLiteral("definition.rearm.changed-reason"),
                      definitionHost.statusBar()->currentMessage().contains(changedReason),
                      definitionHost.statusBar()->currentMessage());
                definitionHost.statusBar()->clearMessage();
                clickAt(KTextEditor::Cursor(1, 4), Qt::ControlModifier);
                check(QStringLiteral("definition.rearm.changed-reason-one-time"),
                      definitionHost.statusBar()->currentMessage().isEmpty(),
                      definitionHost.statusBar()->currentMessage());
            }
        }

        // --- R3(a): two panes share ONE shell action, as in the product ------
        // Kernel-level assertion: the CLICKED view's channel and caret carry the
        // dispatch while the other pane's channel is untouched. The shell's
        // EditorArea::activePane() is NOT reachable from this target (src/app is
        // not linked), so it is documented as a seam-only gap in the evidence,
        // not silently claimed here.
        {
            QMainWindow twoPaneHost;
            auto *central = new QWidget(&twoPaneHost);
            auto *twoPaneLayout = new QHBoxLayout(central);
            twoPaneLayout->setContentsMargins(0, 0, 0, 0);
            twoPaneLayout->setSpacing(0);
            twoPaneHost.setCentralWidget(central);
            KTextEditor::Document *documentA = editor->createDocument(nullptr);
            KTextEditor::Document *documentB = editor->createDocument(nullptr);
            KTextEditor::View *viewA =
                documentA != nullptr ? documentA->createView(central) : nullptr;
            KTextEditor::View *viewB =
                documentB != nullptr ? documentB->createView(central) : nullptr;
            check(QStringLiteral("definition.two-pane.views"),
                  documentA != nullptr && documentB != nullptr && viewA != nullptr
                      && viewB != nullptr);
            if (viewA != nullptr && viewB != nullptr) {
                twoPaneLayout->addWidget(viewA);
                twoPaneLayout->addWidget(viewB);
                documentA->setText(QStringLiteral("module a;\n  wire aa;\nendmodule\n"));
                documentB->setText(QStringLiteral("module b;\n  wire bb;\nendmodule\n"));
                viewA->setProperty("vedit.openFilePath",
                                   QStringLiteral("/tmp/b4-two-pane-a.sv"));
                viewB->setProperty("vedit.openFilePath",
                                   QStringLiteral("/tmp/b4-two-pane-b.sv"));
                twoPaneHost.resize(820, 420);
                twoPaneHost.show();
                // Both views must have their inner widgets laid out before the
                // coordinates are meaningful (a split pane is laid out in more
                // than one event-loop turn).
                QApplication::processEvents();
                QTest::qWait(100);
                QApplication::processEvents();

                auto *twoPaneAction =
                    new QAction(QStringLiteral("&Go to Definition"), &twoPaneHost);
                twoPaneAction->setObjectName(
                    QStringLiteral("action-diagnostics.definition"));
                twoPaneAction->setEnabled(true);
                twoPaneHost.addAction(twoPaneAction);
                QVariantMap twoPaneGates;
                QVariantMap twoPaneRow;
                twoPaneRow.insert(QStringLiteral("enabled"), true);
                twoPaneRow.insert(QStringLiteral("known"), true);
                twoPaneRow.insert(QStringLiteral("source"), QStringLiteral("backend"));
                twoPaneRow.insert(QStringLiteral("reason"), QString());
                twoPaneGates.insert(QStringLiteral("definition"), twoPaneRow);
                twoPaneHost.setProperty("vedit.diagnostics.gates",
                                        twoPaneGates);
                QVariantMap twoPaneCapabilities;
                twoPaneCapabilities.insert(QStringLiteral("backend"),
                                           QStringLiteral("verible"));
                twoPaneHost.setProperty("vedit.diagnostics.capabilities",
                                        twoPaneCapabilities);

                vedit::editor::LspActionChannel *channelA =
                    vedit::editor::LspActionChannel::install(viewA);
                vedit::editor::LspActionChannel *channelB =
                    vedit::editor::LspActionChannel::install(viewB);
                vedit::editor::MultiCursorController *cursorsA =
                    vedit::editor::MultiCursorController::install(viewA);
                vedit::editor::MultiCursorController *cursorsB =
                    vedit::editor::MultiCursorController::install(viewB);
                check(QStringLiteral("definition.two-pane.seams"),
                      channelA != nullptr && channelB != nullptr && cursorsA != nullptr
                          && cursorsB != nullptr);

                int twoPaneTriggered = 0;
                QString triggeredFile;
                KTextEditor::Cursor triggeredCaret;
                QObject::connect(twoPaneAction, &QAction::triggered, &twoPaneHost, [&]() {
                    ++twoPaneTriggered;
                    // What the shell's navigation handler reads: the view that
                    // currently holds keyboard focus (the product uses the shell's
                    // active pane; see the seam-only note in the evidence).
                    KTextEditor::View *focusedView = nullptr;
                    for (QWidget *widget = QApplication::focusWidget(); widget != nullptr;
                         widget = widget->parentWidget()) {
                        if (widget == viewA) {
                            focusedView = viewA;
                            break;
                        }
                        if (widget == viewB) {
                            focusedView = viewB;
                            break;
                        }
                    }
                    triggeredFile = focusedView != nullptr
                                        ? vedit::editor::viewFilePath(focusedView)
                                        : QString();
                    triggeredCaret = focusedView != nullptr
                                         ? focusedView->cursorPosition()
                                         : KTextEditor::Cursor();
                });

                // A is the previously active pane; the click lands in B.
                viewA->setFocus(Qt::MouseFocusReason);
                vedit::editor::setViewCursors(viewA, {KTextEditor::Cursor(0, 0)});
                QApplication::processEvents();
                const KTextEditor::Cursor twoPaneClick(1, 2);
                const QPoint twoPanePoint = viewB->cursorToCoordinate(twoPaneClick);
                QWidget *twoPaneTarget = deliveryTarget(viewB, twoPanePoint);
                check(QStringLiteral("definition.two-pane.inner-widget"),
                      twoPaneTarget != nullptr && twoPaneTarget != viewB
                          && viewB->isAncestorOf(twoPaneTarget),
                      twoPaneTarget != nullptr
                          ? QString(twoPaneTarget->metaObject()->className())
                          : QStringLiteral("none"));
                const QPoint twoPaneLocal =
                    pointInTarget(twoPaneTarget, viewB, twoPanePoint);
                QTest::mousePress(twoPaneTarget, Qt::LeftButton, Qt::ControlModifier,
                                  twoPaneLocal);
                QTest::mouseRelease(twoPaneTarget, Qt::LeftButton, Qt::ControlModifier,
                                    twoPaneLocal);
                QApplication::processEvents();

                check(QStringLiteral("definition.two-pane.clicked-channel"),
                      channelB->dispatchCount(vedit::editor::EditorAction::Definition) == 1,
                      QString::number(channelB->dispatchCount(
                          vedit::editor::EditorAction::Definition)));
                check(QStringLiteral("definition.two-pane.other-channel"),
                      channelA->dispatchCount(vedit::editor::EditorAction::Definition) == 0,
                      QString::number(channelA->dispatchCount(
                          vedit::editor::EditorAction::Definition)));
                check(QStringLiteral("definition.two-pane.action-triggered"),
                      twoPaneTriggered == 1, QString::number(twoPaneTriggered));
                check(QStringLiteral("definition.two-pane.clicked-caret"),
                      viewB->cursorPosition() == twoPaneClick
                          && triggeredCaret == twoPaneClick,
                      QStringLiteral("caret=%1:%2 recorded=%3:%4")
                          .arg(viewB->cursorPosition().line())
                          .arg(viewB->cursorPosition().column())
                          .arg(triggeredCaret.line())
                          .arg(triggeredCaret.column()));
                check(QStringLiteral("definition.two-pane.clicked-file"),
                      triggeredFile == QStringLiteral("/tmp/b4-two-pane-b.sv"),
                      triggeredFile);
                check(QStringLiteral("definition.two-pane.other-caret-untouched"),
                      viewA->cursorPosition() == KTextEditor::Cursor(0, 0),
                      QStringLiteral("%1:%2")
                          .arg(viewA->cursorPosition().line())
                          .arg(viewA->cursorPosition().column()));
                check(QStringLiteral("definition.two-pane.clicked-cursor-count"),
                      cursorsB->cursorCount() == 1,
                      QString::number(cursorsB->cursorCount()));
                QWidget *twoPaneFocus = QApplication::focusWidget();
                check(QStringLiteral("definition.two-pane.keyboard-focus"),
                      twoPaneFocus != nullptr && viewB->isAncestorOf(twoPaneFocus),
                      twoPaneFocus != nullptr
                          ? QString(twoPaneFocus->metaObject()->className())
                          : QStringLiteral("none"));
            }
        }
    }

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
