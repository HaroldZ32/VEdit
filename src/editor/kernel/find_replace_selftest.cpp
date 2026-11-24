// Lane selftest for T13 (AC-4.3): find / replace API + the in-view bar.
//
// ADDENDUM A-7 (T13) grants this file. It is the lane's ctest target:
//
//   vedit-editor-find-replace-selftest
//   vedit-editor-find-replace-selftest --screenshot <dir>   # bar evidence PNG
//   vedit-editor-find-replace-selftest --history-restart <dir>
//       Batch 20261110d: run once in a directory to WRITE the on-disk search
//       history, run again to LOAD it — two real processes, the same file.
//
// Everything runs against the real KTextEditor document/view in the pinned Qt6
// image. When the KTextEditor plugin is unavailable the program exits 77 (the
// ctest SKIP_RETURN_CODE), never 0 — a skip must not look like a pass.
#include "find_replace.h"

#if defined(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
#include "ktexteditor_provider.h"
#endif
#include "multi_cursor.h"
#include "undo_ops.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPoint>
#include <QFile>
#include <QShortcut>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

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

QString keySequenceText(const QKeySequence &sequence)
{
    return sequence.toString(QKeySequence::PortableText);
}

KTextEditor::View *makeView(KTextEditor::Document *document, const QSize &size)
{
    KTextEditor::View *view = document->createView(nullptr);
    if (view == nullptr) {
        return nullptr;
    }
    view->resize(size);
    view->show();
    return view;
}

} // namespace

// Real process-restart probe for the on-disk history (batch 20261110d). The
// product loads once at install and writes once at quit; this runs those two
// steps in two SEPARATE processes against the same state directory.
int runHistoryRestartProbe(const QString &stateDir)
{
    const QString &dir = stateDir;
    const QStringList loaded = vedit::search::loadSearchHistoryFile(dir);
    if (loaded.contains(QStringLiteral("history_probe_alpha"))) {
        std::printf("HISTORY-RESTART loaded=%d list='%s'\n", static_cast<int>(loaded.size()),
                    qPrintable(loaded.join(QStringLiteral(","))));
        return 0;
    }
    auto &hub = vedit::search::SearchHub::instance();
    hub.clearHistory();
    hub.setHistoryStore(
        [dir]() { return vedit::search::loadSearchHistoryFile(dir); }, nullptr);
    hub.noteQuery(QStringLiteral("history_probe_alpha"));
    hub.noteQuery(QStringLiteral("history_probe_beta"));
    const bool saved = vedit::search::saveSearchHistoryFile(dir, hub.history());
    std::printf("HISTORY-RESTART writable=%d entries=%d list='%s'\n", saved ? 1 : 0,
                static_cast<int>(hub.history().size()),
                qPrintable(hub.history().join(QStringLiteral(","))));
    return saved ? 0 : 1;
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-find-replace-selftest"));

    QString screenshotDirectory;
    const QStringList arguments = QCoreApplication::arguments();
    const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        screenshotDirectory = arguments.at(screenshotIndex + 1);
    }
    const int restartIndex = arguments.indexOf(QStringLiteral("--history-restart"));
    if (restartIndex >= 0 && restartIndex + 1 < arguments.size()) {
        return runHistoryRestartProbe(arguments.at(restartIndex + 1));
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
    document->setText(QStringLiteral("alpha beta alpha\nAlpha gamma alpha\nbeta alpha\n"));
    KTextEditor::View *view = makeView(document, QSize(760, 320));
    if (view == nullptr) {
        std::printf("SKIP: KTextEditor could not create a view (katepart missing)\n");
        return 77;
    }
    view->setFocus();
    QApplication::processEvents();

    const auto keyTarget = [view]() -> QWidget * {
        QWidget *focus = QApplication::focusWidget();
        return focus != nullptr ? focus : view;
    };

    vedit::editor::FindReplaceBar *bar = vedit::editor::FindReplaceBar::install(view);
    check(QStringLiteral("find.bar.installed"), bar != nullptr);
    if (bar == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    check(QStringLiteral("find.bar.install-idempotent"),
          vedit::editor::FindReplaceBar::instanceFor(view) == bar);

    // --- the bar is the SINGLE owner of the find keys ----------------------
    // Ownership rule (written down, asserted here): the bar handles Ctrl+F /
    // Ctrl+H / F3 / Shift+F3 unconditionally. It claims the sequences as
    // ShortcutOverride on the view, so no kernel QAction can win by priority
    // or turn the key into an "Ambiguous shortcut" refusal. Any kernel action
    // still advertising one of the sequences would be a second owner, so the
    // check below fails if one exists.
    const QList<QKeySequence> ownedSequences{
        QKeySequence(QStringLiteral("Ctrl+F")), QKeySequence(QStringLiteral("Ctrl+H")),
        QKeySequence(QStringLiteral("F3")), QKeySequence(QStringLiteral("Shift+F3"))};
    bool secondOwner = false;
    for (QAction *action : view->findChildren<QAction *>()) {
        if (ownedSequences.contains(action->shortcut())) {
            secondOwner = true;
            std::printf("CHECK-DETAIL second owner: '%s' -> '%s'\n", qPrintable(action->text()),
                        qPrintable(keySequenceText(action->shortcut())));
        }
    }
    check(QStringLiteral("find.keys.single-owner"), !secondOwner);

    // --- real key events open the bar (the same path the product uses) ----
    view->activateWindow();
    view->setFocus();
    QApplication::processEvents();
    QWidget *focused = QApplication::focusWidget();
    check(QStringLiteral("find.real-focus.inner-widget"),
          focused != nullptr && focused != view && view->isAncestorOf(focused),
          focused != nullptr ? QString(focused->metaObject()->className()) : QStringLiteral("none"));
    QTest::keyClick(keyTarget(), Qt::Key_F, Qt::ControlModifier);
    QApplication::processEvents();
    check(QStringLiteral("find.bar.ctrl-f-opens"), bar->isVisible());
    check(QStringLiteral("find.bar.find-mode"), !bar->replaceRowVisible());
    // Batch 20261110d contract change: Ctrl+H opens the bar with the replace row
    // FOLDED (the disclosure chevron reveals it). The old assertion required the
    // row to appear; the stronger three-state pair below replaces it.
    QTest::keyClick(keyTarget(), Qt::Key_H, Qt::ControlModifier);
    QApplication::processEvents();
    check(QStringLiteral("find.bar.ctrl-h-opens"), bar->isVisible());
    check(QStringLiteral("find.bar.ctrl-h-folded"), !bar->replaceRowVisible());
    auto *disclosure =
        bar->findChild<QToolButton *>(QStringLiteral("vedit-find-disclosure"));
    check(QStringLiteral("find.disclosure.exists"), disclosure != nullptr);
    check(QStringLiteral("find.disclosure.default-folded"),
          disclosure != nullptr && !disclosure->isChecked() && !bar->replaceRowVisible());
    if (disclosure != nullptr) {
        QTest::mouseClick(disclosure, Qt::LeftButton);
        QApplication::processEvents();
        check(QStringLiteral("find.disclosure.click-expands"), bar->replaceRowVisible());
        // openReplace() must NOT override the folded default.
        bar->openReplace();
        QApplication::processEvents();
        check(QStringLiteral("find.disclosure.open-replace-stays-folded"),
              !bar->replaceRowVisible() && !disclosure->isChecked());
        QTest::mouseClick(disclosure, Qt::LeftButton);
        QApplication::processEvents();
        check(QStringLiteral("find.disclosure.click-expands-again"), bar->replaceRowVisible());
        QTest::mouseClick(disclosure, Qt::LeftButton);
        QApplication::processEvents();
        check(QStringLiteral("find.disclosure.click-folds-again"), !bar->replaceRowVisible());
    }

    // --- case sensitivity --------------------------------------------------
    bar->setPattern(QStringLiteral("alpha"));
    check(QStringLiteral("find.count.case-sensitive"), bar->matchCount() == 4,
          QString::number(bar->matchCount()));
    bar->setCaseSensitive(false);
    check(QStringLiteral("find.count.case-insensitive"), bar->matchCount() == 5,
          QString::number(bar->matchCount()));
    bar->setPattern(QStringLiteral("ALPHA"));
    check(QStringLiteral("find.count.case-insensitive-pattern"), bar->matchCount() == 5);
    bar->setCaseSensitive(true);
    check(QStringLiteral("find.count.case-sensitive-pattern"), bar->matchCount() == 0);

    // --- whole word --------------------------------------------------------
    bar->setCaseSensitive(true);
    bar->setPattern(QStringLiteral("alp"));
    check(QStringLiteral("find.count.partial"), bar->matchCount() == 4,
          QString::number(bar->matchCount()));
    bar->setWholeWords(true);
    check(QStringLiteral("find.count.whole-word-rejects"), bar->matchCount() == 0,
          QString::number(bar->matchCount()));
    bar->setPattern(QStringLiteral("alpha"));
    check(QStringLiteral("find.count.whole-word-accepts"), bar->matchCount() == 4);
    bar->setWholeWords(false);

    // --- regular expression ------------------------------------------------
    bar->setRegex(true);
    bar->setPattern(QStringLiteral("alp.a"));
    check(QStringLiteral("find.count.regex"), bar->matchCount() == 4,
          QString::number(bar->matchCount()));
    bar->setPattern(QStringLiteral("alp(h)a"));
    check(QStringLiteral("find.count.regex-group"), bar->matchCount() == 4);
    bar->setPattern(QStringLiteral("("));
    check(QStringLiteral("find.regex.invalid-no-matches"), bar->matchCount() == 0);
    check(QStringLiteral("find.regex.invalid-reported"),
          bar->statusText().contains(QStringLiteral("invalid regular expression")),
          bar->statusText());
    bar->setPattern(QStringLiteral("alpha"));
    bar->setRegex(false);

    // --- F3 / Shift+F3 navigation + current index -------------------------
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    bar->setPattern(QStringLiteral("alpha"));
    bar->findNext();
    check(QStringLiteral("find.next.first"), bar->currentMatchIndex() == 0,
          QString::number(bar->currentMatchIndex()));
    check(QStringLiteral("find.next.first-selection"),
          view->selectionRange() ==
              KTextEditor::Range(KTextEditor::Cursor(0, 0), KTextEditor::Cursor(0, 5)),
          QStringLiteral("%1,%2").arg(view->selectionRange().start().column()).arg(
              view->selectionRange().end().column()));
    bar->findNext();
    check(QStringLiteral("find.next.second"), bar->currentMatchIndex() == 1,
          QString::number(bar->currentMatchIndex()));
    check(QStringLiteral("find.next.second-selection"),
          view->selectionRange().start().column() == 11,
          QString::number(view->selectionRange().start().column()));
    check(QStringLiteral("find.status.current-of-total"),
          bar->statusText().contains(QStringLiteral("2 / 4")), bar->statusText());
    bar->findPrevious();
    check(QStringLiteral("find.previous.wraps-back"), bar->currentMatchIndex() == 0,
          QString::number(bar->currentMatchIndex()));

    // The real F3 key with the bar hidden must reuse the last pattern.
    bar->closeBar();
    QApplication::processEvents();
    check(QStringLiteral("find.bar.escape-closes"), !bar->isVisible());
    const KTextEditor::Range beforeKey = view->selectionRange();
    QTest::keyClick(keyTarget(), Qt::Key_F3);
    QApplication::processEvents();
    check(QStringLiteral("find.f3-key-navigates"), view->selectionRange() != beforeKey);
    check(QStringLiteral("find.f3-key-hides-bar"), !bar->isVisible());
    const int afterF3Index = bar->currentMatchIndex();
    QTest::keyClick(keyTarget(), Qt::Key_F3, Qt::ShiftModifier);
    QApplication::processEvents();
    check(QStringLiteral("find.shift-f3-key-navigates-back"),
          bar->currentMatchIndex() != afterF3Index,
          QStringLiteral("%1").arg(bar->currentMatchIndex()));

    // --- selection-scoped replace -----------------------------------------
    bar->openReplace();
    document->setText(QStringLiteral("alpha alpha\nalpha alpha\n"));
    QApplication::processEvents();
    view->setSelection(
        KTextEditor::Range(KTextEditor::Cursor(0, 0), KTextEditor::Cursor(0, 11)));
    bar->setInSelection(true);
    check(QStringLiteral("find.scope.in-selection"), bar->inSelection());
    bar->setPattern(QStringLiteral("alpha"));
    bar->setReplacement(QStringLiteral("X"));
    bar->replaceAllMatches();
    check(QStringLiteral("find.scope.line0-replaced"),
          document->line(0) == QStringLiteral("X X"), document->line(0));
    check(QStringLiteral("find.scope.line1-untouched"),
          document->line(1) == QStringLiteral("alpha alpha"), document->line(1));
    QString undoError;
    const bool undone = vedit::editor::documentUndo(document, &undoError);
    check(QStringLiteral("find.scope.replace-all-single-undo"),
          undone && document->line(0) == QStringLiteral("alpha alpha"), undoError);
    check(QStringLiteral("find.scope.undo-restores-line1"),
          document->line(1) == QStringLiteral("alpha alpha"));
    bar->setInSelection(false);

    // --- Replace (current) is one undo step too ---------------------------
    document->setText(QStringLiteral("one two one two\n"));
    QApplication::processEvents();
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    bar->setPattern(QStringLiteral("one"));
    bar->setReplacement(QStringLiteral("1"));
    bar->findNext();
    bar->replaceCurrent();
    check(QStringLiteral("find.replace-current.text"),
          document->line(0) == QStringLiteral("1 two one two"),
          document->line(0).trimmed());
    undoError.clear();
    check(QStringLiteral("find.replace-current.single-undo"),
          vedit::editor::documentUndo(document, &undoError) &&
              document->line(0) == QStringLiteral("one two one two"),
          undoError);

    // --- Replace All (document-wide) is one undo step ---------------------
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    bar->setPattern(QStringLiteral("one"));
    bar->replaceAllMatches();
    check(QStringLiteral("find.replace-all.text"),
          document->line(0) == QStringLiteral("1 two 1 two"),
          document->line(0).trimmed());
    check(QStringLiteral("find.replace-all.status"),
          bar->statusText().contains(QStringLiteral("Replaced 2 of 2")), bar->statusText());
    undoError.clear();
    check(QStringLiteral("find.replace-all.single-undo"),
          vedit::editor::documentUndo(document, &undoError) &&
              document->line(0) == QStringLiteral("one two one two"),
          undoError);

    // --- edge cases the reviewer asked to be explicit about (A-7) ---------
    // 1) Empty REPLACEMENT deletes every match, in ONE undo step.
    bar->openReplace();
    document->setText(QStringLiteral("alpha x alpha\nalpha\n"));
    QApplication::processEvents();
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    bar->setPattern(QStringLiteral("alpha"));
    bar->setReplacement(QString());
    bar->replaceAllMatches();
    check(QStringLiteral("find.replace-empty.deletes-matches"),
          document->line(0) == QStringLiteral(" x ") && document->line(1).isEmpty(),
          QStringLiteral("line0='%1' line1='%2'").arg(document->line(0), document->line(1)));
    undoError.clear();
    check(QStringLiteral("find.replace-empty.single-undo"),
          vedit::editor::documentUndo(document, &undoError) &&
              document->line(0) == QStringLiteral("alpha x alpha") &&
              document->line(1) == QStringLiteral("alpha"),
          undoError);

    // 2) Empty PATTERN is refused by contract: no matches, no edit at all.
    document->setText(QStringLiteral("alpha\n"));
    QApplication::processEvents();
    bar->setPattern(QString());
    bar->setReplacement(QStringLiteral("X"));
    bar->replaceAllMatches();
    check(QStringLiteral("find.empty-pattern.no-edit"),
          document->line(0) == QStringLiteral("alpha") &&
              bar->statusText().contains(QStringLiteral("Type to search")),
          QStringLiteral("line0='%1' status='%2'").arg(document->line(0), bar->statusText()));
    const vedit::editor::FindOptions emptyOptions;
    const vedit::editor::ReplaceResult emptyPattern = vedit::editor::replaceAll(
        document, QString(), QStringLiteral("X"), emptyOptions, nullptr);
    check(QStringLiteral("find.empty-pattern.api-refuses"),
          !emptyPattern.ok && document->line(0) == QStringLiteral("alpha") &&
              emptyPattern.error.contains(QStringLiteral("empty")),
          emptyPattern.error);

    // --- Escape closes from inside the pattern field ----------------------
    bar->openFind();
    QApplication::processEvents();
    QLineEdit *patternField = bar->findChild<QLineEdit *>(QStringLiteral("vedit-find-pattern"));
    check(QStringLiteral("find.bar.pattern-field"), patternField != nullptr);
    if (patternField != nullptr) {
        patternField->setFocus();
        QTest::keyClick(patternField, Qt::Key_Escape);
        QApplication::processEvents();
        check(QStringLiteral("find.bar.escape-from-field"), !bar->isVisible());
    }

    // --- AC-8.3: Return in the find field is find-next ---------------------
    // The product installs the editor filters on the view AFTER the bar, so
    // they run first: EditorEnhancements used to claim Return for the document,
    // so the field never saw returnPressed. The FULL filter stack is installed
    // here (AC-8.3 asks for it) — bar, EditorEnhancements, MultiCursorController
    // — both sides of the rule are asserted: Return in the field searches and
    // leaves the document alone, Return in the document still splits the line.
    vedit::editor::EditorEnhancements::install(view);
    vedit::editor::MultiCursorController::install(view);
    document->setText(QStringLiteral("alpha beta alpha\nalpha gamma\n"));
    QApplication::processEvents();
    bar->openFind();
    bar->setPattern(QStringLiteral("alpha"));
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    bar->findNext();
    QApplication::processEvents();
    QLineEdit *returnField = bar->findChild<QLineEdit *>(QStringLiteral("vedit-find-pattern"));
    check(QStringLiteral("find.return.field"), returnField != nullptr);
    if (returnField != nullptr) {
        const int beforeReturn = bar->currentMatchIndex();
        const QString textBeforeReturn = document->text();
        returnField->setFocus(Qt::OtherFocusReason);
        QApplication::processEvents();
        QTest::keyClick(returnField, Qt::Key_Return);
        QApplication::processEvents();
        check(QStringLiteral("find.return.next"),
              bar->currentMatchIndex() == beforeReturn + 1,
              QStringLiteral("%1 -> %2").arg(beforeReturn).arg(bar->currentMatchIndex()));
        check(QStringLiteral("find.return.focus-stays"),
              QApplication::focusWidget() == returnField,
              QApplication::focusWidget() != nullptr
                  ? QString(QApplication::focusWidget()->metaObject()->className())
                  : QStringLiteral("none"));
        check(QStringLiteral("find.return.document-unchanged"),
              document->text() == textBeforeReturn, document->text());
        QTest::keyClick(returnField, Qt::Key_Return, Qt::ShiftModifier);
        QApplication::processEvents();
        check(QStringLiteral("find.return.shift-previous"),
              bar->currentMatchIndex() == beforeReturn,
              QStringLiteral("%1").arg(bar->currentMatchIndex()));
        check(QStringLiteral("find.return.shift-focus-stays"),
              QApplication::focusWidget() == returnField);
        check(QStringLiteral("find.return.shift-document-unchanged"),
              document->text() == textBeforeReturn, document->text());
        // A second plain Return keeps advancing from the new current match.
        QTest::keyClick(returnField, Qt::Key_Return);
        QApplication::processEvents();
        check(QStringLiteral("find.return.repeats"),
              bar->currentMatchIndex() == beforeReturn + 1 &&
                  document->text() == textBeforeReturn,
              QStringLiteral("%1").arg(bar->currentMatchIndex()));
        // AC-8.6: Return with an empty query is a no-op, never an edit. The
        // selection is cleared first so the bar's existing "fill from the
        // selected text" behaviour cannot repopulate the field.
        view->setSelection(KTextEditor::Range::invalid());
        view->setCursorPosition(KTextEditor::Cursor(0, 0));
        bar->setPattern(QString());
        QApplication::processEvents();
        const QString textBeforeEmpty = document->text();
        returnField->setFocus(Qt::OtherFocusReason);
        QApplication::processEvents();
        QTest::keyClick(returnField, Qt::Key_Return);
        QApplication::processEvents();
        check(QStringLiteral("find.return.empty-query-noop"),
              document->text() == textBeforeEmpty && bar->currentMatchIndex() == -1 &&
                  bar->statusText().contains(QStringLiteral("Type to search")),
              QStringLiteral("status='%1' index=%2")
                  .arg(bar->statusText())
                  .arg(bar->currentMatchIndex()));
    }

    // AC-8.4: the focus-aware grab must not delete normal editing behaviour —
    // Return inside the document still inserts its (auto-indented) newline.
    bar->closeBar();
    document->setText(QStringLiteral("if (x) {"));
    QApplication::processEvents();
    view->setCursorPosition(KTextEditor::Cursor(0, document->lineLength(0)));
    view->setFocus();
    QApplication::processEvents();
    QTest::keyClick(keyTarget(), Qt::Key_Return);
    QApplication::processEvents();
    check(QStringLiteral("find.return.editor-still-splits"),
          document->lines() == 2 && document->line(1) == QStringLiteral("    "),
          QStringLiteral("lines=%1 line1='%2'")
              .arg(document->lines())
              .arg(document->line(1)));

    // --- F1 (T26 High): Return in the REPLACE field must only replace ------
    // The field had NO claim, so QLineEdit's ignored Return propagated into
    // KatePart and EditorEnhancements inserted a newline at the document cursor
    // ('alpha alpha\n' -> 'X \nalpha\n', 3 lines). Asserted by exact bytes, so
    // any stray edit fails the check.
    bar->openReplace();
    QApplication::processEvents();
    document->setText(QStringLiteral("alpha alpha\n"));
    QApplication::processEvents();
    QLineEdit *replaceField = bar->findChild<QLineEdit *>(QStringLiteral("vedit-find-replacement"));
    check(QStringLiteral("find.replace.return.field"), replaceField != nullptr);
    if (replaceField != nullptr && returnField != nullptr) {
        bar->setPattern(QStringLiteral("alpha"));
        bar->setReplacement(QStringLiteral("X"));
        view->setCursorPosition(KTextEditor::Cursor(0, 0));
        bar->findNext(); // current = the first match
        replaceField->setFocus(Qt::OtherFocusReason);
        QApplication::processEvents();
        QTest::keyClick(replaceField, Qt::Key_Return);
        QApplication::processEvents();
        check(QStringLiteral("find.replace.return.replaces-once"),
              document->text() == QStringLiteral("X alpha\n"), document->text());
        check(QStringLiteral("find.replace.return.no-newline"),
              document->text().count(QLatin1Char('\n')) == 1,
              QStringLiteral("newlines=%1").arg(document->text().count(QLatin1Char('\n'))));
        check(QStringLiteral("find.replace.return.focus-stays"),
              QApplication::focusWidget() == replaceField,
              QApplication::focusWidget() != nullptr
                  ? QString(QApplication::focusWidget()->metaObject()->className())
                  : QStringLiteral("none"));
        // `replaceCurrent()` continues walking forward: the replaced match is
        // gone, so the surviving match becomes the current one.
        check(QStringLiteral("find.replace.return.advances"),
              bar->currentMatchIndex() == 0 && bar->matchCount() == 1,
              QStringLiteral("index=%1 count=%2")
                  .arg(bar->currentMatchIndex())
                  .arg(bar->matchCount()));
        // Shift+Return: replace the current match, then step back one match.
        QTest::keyClick(replaceField, Qt::Key_Return, Qt::ShiftModifier);
        QApplication::processEvents();
        check(QStringLiteral("find.replace.return.shift-replaces"),
              document->text() == QStringLiteral("X X\n"), document->text());
        check(QStringLiteral("find.replace.return.shift-no-newline"),
              document->text().count(QLatin1Char('\n')) == 1,
              QStringLiteral("newlines=%1").arg(document->text().count(QLatin1Char('\n'))));
        check(QStringLiteral("find.replace.return.shift-focus-stays"),
              QApplication::focusWidget() == replaceField);
    }

    // The claim must exist for EVERY text-entry control the bar owns, not just
    // the two known fields: a control added later without a claim would bring
    // the corruption back. Driven with a pattern that matches nothing, so the
    // document must stay byte-identical after a Return in each field.
    document->setText(QStringLiteral("one two\n"));
    QApplication::processEvents();
    bar->setPattern(QStringLiteral("zzzz"));
    bar->setReplacement(QString());
    const QString beforeEveryField = document->text();
    bool everyFieldClaimed = true;
    const QList<QLineEdit *> barFields = bar->findChildren<QLineEdit *>();
    for (QLineEdit *field : barFields) {
        field->setFocus(Qt::OtherFocusReason);
        QApplication::processEvents();
        QTest::keyClick(field, Qt::Key_Return);
        QApplication::processEvents();
        if (document->text() != beforeEveryField) {
            everyFieldClaimed = false;
            std::printf("CHECK-DETAIL unclaimed Return in field '%s' changed the document to '%s'\n",
                        qPrintable(field->objectName()), qPrintable(document->text()));
        }
    }
    check(QStringLiteral("find.return.all-text-inputs-claimed"),
          everyFieldClaimed && barFields.size() >= 2,
          QStringLiteral("fields=%1").arg(barFields.size()));

    // --- AC-8.4: Esc and the close button hand focus back to the editor ----
    bar->openFind();
    QApplication::processEvents();
    if (returnField != nullptr) {
        returnField->setFocus(Qt::OtherFocusReason);
        QApplication::processEvents();
        QTest::keyClick(returnField, Qt::Key_Escape);
        QApplication::processEvents();
        QWidget *afterEscape = QApplication::focusWidget();
        check(QStringLiteral("find.escape.focus-to-editor"),
              !bar->isVisible() && afterEscape != nullptr && afterEscape != returnField &&
                  (afterEscape == view || view->isAncestorOf(afterEscape)),
              afterEscape != nullptr ? QString(afterEscape->metaObject()->className())
                                     : QStringLiteral("none"));
    }
    bar->openFind();
    QApplication::processEvents();
    auto *closeButton = bar->findChild<QToolButton *>(QStringLiteral("vedit-find-close"));
    check(QStringLiteral("find.close.button"), closeButton != nullptr);
    if (closeButton != nullptr) {
        QTest::mouseClick(closeButton, Qt::LeftButton);
        QApplication::processEvents();
        QWidget *afterClose = QApplication::focusWidget();
        check(QStringLiteral("find.close.focus-to-editor"),
              !bar->isVisible() && afterClose != nullptr &&
                  (afterClose == view || view->isAncestorOf(afterClose)),
              afterClose != nullptr ? QString(afterClose->metaObject()->className())
                                    : QStringLiteral("none"));
    }

    // --- AC-8.1: drag by the bar background, clamped, per session, resettable
    // A pane wider than the bar's natural width leaves room to drag in both
    // directions; the clamping case is exercised separately below.
    view->resize(QSize(1000, 400));
    QApplication::processEvents();
    bar->openFind();
    bar->restoreDefaultPosition();
    QApplication::processEvents();
    check(QStringLiteral("find.overlay.default-top-right"),
          !bar->isCustomPosition() && bar->pos() == bar->defaultPosition(),
          QStringLiteral("pos=%1,%2 default=%3,%4")
              .arg(bar->pos().x())
              .arg(bar->pos().y())
              .arg(bar->defaultPosition().x())
              .arg(bar->defaultPosition().y()));
    // One synthetic press/move/release at `localPoint` of `target`. Only the
    // BACKGROUND is a drag handle, so the negative control below delivers the
    // same sequence to a control and expects the bar not to move.
    const auto dragOn = [](QWidget *target, const QPointF &localPoint, const QPoint &delta) {
        const QPointF global = target->mapToGlobal(localPoint.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, localPoint, global, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &press);
        QMouseEvent move(QEvent::MouseMove, localPoint + QPointF(delta), global + QPointF(delta),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, localPoint + QPointF(delta),
                            global + QPointF(delta), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(target, &release);
        QApplication::processEvents();
    };
    const auto dragBy = [&dragOn, bar](const QPoint &delta) {
        dragOn(bar, QPointF(4, 4), delta);
    };
    const QPoint dragOrigin = bar->pos();
    dragBy(QPoint(-120, 90));
    check(QStringLiteral("find.overlay.drag-moves"),
          bar->pos() == dragOrigin + QPoint(-120, 90),
          QStringLiteral("pos=%1,%2").arg(bar->pos().x()).arg(bar->pos().y()));
    check(QStringLiteral("find.overlay.drag-is-custom"), bar->isCustomPosition());
    dragBy(QPoint(-5000, -5000));
    check(QStringLiteral("find.overlay.clamp-top-left"), bar->pos() == QPoint(0, 0),
          QStringLiteral("pos=%1,%2").arg(bar->pos().x()).arg(bar->pos().y()));
    dragBy(QPoint(5000, 5000));
    check(QStringLiteral("find.overlay.clamp-inside"), view->rect().contains(bar->geometry()),
          QStringLiteral("bar=%1,%2 %3x%4 view=%5x%6")
              .arg(bar->pos().x())
              .arg(bar->pos().y())
              .arg(bar->width())
              .arg(bar->height())
              .arg(view->width())
              .arg(view->height()));
    // AC-8.1 negative control (added on top of the positive assertions): a
    // press+move on a CONTROL never starts a drag — the bar must not move even
    // though the same sequence on the background moves it.
    auto *nextControl = bar->findChild<QToolButton *>(QStringLiteral("vedit-find-next"));
    check(QStringLiteral("find.overlay.negative-control.widget"), nextControl != nullptr);
    if (nextControl != nullptr) {
        const QPoint beforeControlPress = bar->pos();
        dragOn(nextControl, QPointF(nextControl->rect().center()), QPoint(-80, 60));
        check(QStringLiteral("find.overlay.control-press-does-not-drag"),
              bar->pos() == beforeControlPress,
              QStringLiteral("pos=%1,%2 was %3,%4")
                  .arg(bar->pos().x())
                  .arg(bar->pos().y())
                  .arg(beforeControlPress.x())
                  .arg(beforeControlPress.y()));
    }
    const QPoint draggedPosition = bar->pos();
    bar->closeBar();
    QApplication::processEvents();
    bar->openFind();
    QApplication::processEvents();
    check(QStringLiteral("find.overlay.position-per-session"), bar->pos() == draggedPosition,
          QStringLiteral("reopened at %1,%2 (was %3,%4)")
              .arg(bar->pos().x())
              .arg(bar->pos().y())
              .arg(draggedPosition.x())
              .arg(draggedPosition.y()));
    bar->restoreDefaultPosition();
    QApplication::processEvents();
    check(QStringLiteral("find.overlay.restore-default"),
          !bar->isCustomPosition() && bar->pos() == bar->defaultPosition(),
          QStringLiteral("pos=%1,%2").arg(bar->pos().x()).arg(bar->pos().y()));

    // --- AC-8.2: at 360 px every option and the close button stay reachable -
    view->resize(QSize(360, 400));
    QApplication::processEvents();
    document->setText(QStringLiteral("alpha beta alpha\nAlpha gamma alpha\nbeta alpha\n"));
    bar->openReplace();
    if (disclosure != nullptr) {
        // The disclose button is the way to reveal the replace row (folded by
        // default); the narrow reachability audit covers both rows.
        disclosure->setChecked(true);
    }
    bar->setPattern(QStringLiteral("alpha"));
    QApplication::processEvents();
    check(QStringLiteral("find.narrow.compact-layout"), bar->compactLayout());
    check(QStringLiteral("find.narrow.bar-fits-width"),
          bar->width() <= view->width() - 20,
          QStringLiteral("bar=%1 view=%2").arg(bar->width()).arg(view->width()));
    check(QStringLiteral("find.narrow.bar-inside-view"), view->rect().contains(bar->geometry()),
          QStringLiteral("bar=%1,%2 %3x%4")
              .arg(bar->pos().x())
              .arg(bar->pos().y())
              .arg(bar->width())
              .arg(bar->height()));
    bool everyControlReachable = true;
    const QStringList narrowControls{
        QStringLiteral("vedit-find-pattern"),   QStringLiteral("vedit-find-case"),
        QStringLiteral("vedit-find-whole"),     QStringLiteral("vedit-find-regex"),
        QStringLiteral("vedit-find-scope"),     QStringLiteral("vedit-find-scope-selection"),
        QStringLiteral("vedit-find-prev"),      QStringLiteral("vedit-find-next"),
        QStringLiteral("vedit-find-status"),    QStringLiteral("vedit-find-close"),
        QStringLiteral("vedit-find-replacement"), QStringLiteral("vedit-find-replace"),
        QStringLiteral("vedit-find-replace-all"), QStringLiteral("vedit-find-disclosure")};
    for (const QString &name : narrowControls) {
        QWidget *control = bar->findChild<QWidget *>(name);
        if (control == nullptr || !control->isVisible()) {
            everyControlReachable = false;
            std::printf("CHECK-DETAIL narrow control missing/hidden: %s\n", qPrintable(name));
            continue;
        }
        const QRect mapped(control->mapTo(bar, QPoint(0, 0)), control->size());
        if (!bar->rect().contains(mapped)) {
            everyControlReachable = false;
            std::printf("CHECK-DETAIL narrow control clipped: %s at %d,%d %dx%d\n",
                        qPrintable(name), mapped.x(), mapped.y(), mapped.width(),
                        mapped.height());
        }
    }
    check(QStringLiteral("find.narrow.every-control-reachable"), everyControlReachable,
          QStringLiteral("bar %1x%2, view %3x%4")
              .arg(bar->width())
              .arg(bar->height())
              .arg(view->width())
              .arg(view->height()));
    if (!screenshotDirectory.isEmpty()) {
        QDir().mkpath(screenshotDirectory);
        const QString narrowShot =
            screenshotDirectory + QStringLiteral("/find-bar-narrow-360.png");
        check(QStringLiteral("find.evidence.narrow-screenshot"),
              bar->grab().save(narrowShot), narrowShot);
        const QString narrowViewShot =
            screenshotDirectory + QStringLiteral("/find-bar-narrow-360-view.png");
        check(QStringLiteral("find.evidence.narrow-view-screenshot"),
              view->grab().save(narrowViewShot), narrowViewShot);
    }

    // --- the provider really installs the bar on the views it creates -----
#if defined(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
    {
        vedit::editor::EditCoreProvider provider;
        QWidget *editorWidget = provider.createEditor(nullptr);
        check(QStringLiteral("find.provider.creates-editor"), editorWidget != nullptr);
        check(QStringLiteral("find.provider.bar-installed"),
              vedit::editor::FindReplaceBar::instanceFor(editorWidget) != nullptr);
        delete editorWidget;
    }
#else
    std::printf("SKIP-CHECK find.provider.bar-installed (no EditorProvider seam in this build)\n");
#endif

    // --- batch 20261110d: the bar at the two reference viewports ----------
    // 1366×768 (the smallest supported laptop) and 1920×1080. At both, the wide
    // form is used, every visible control lies inside the bar, the disclosure
    // button is visible, and NO two control rectangles intersect — the
    // assertion that catches "all controls squeezed into one blob".
    const auto visibleControlRects = [bar]() {
        const QStringList names{
            QStringLiteral("vedit-find-pattern"),    QStringLiteral("vedit-find-case"),
            QStringLiteral("vedit-find-whole"),      QStringLiteral("vedit-find-regex"),
            QStringLiteral("vedit-find-scope"),      QStringLiteral("vedit-find-scope-selection"),
            QStringLiteral("vedit-find-prev"),       QStringLiteral("vedit-find-next"),
            QStringLiteral("vedit-find-status"),     QStringLiteral("vedit-find-close"),
            QStringLiteral("vedit-find-replacement"), QStringLiteral("vedit-find-replace"),
            QStringLiteral("vedit-find-replace-all"), QStringLiteral("vedit-find-disclosure")};
        QList<QPair<QString, QRect>> rects;
        for (const QString &name : names) {
            QWidget *control = bar->findChild<QWidget *>(name);
            if (control == nullptr || !control->isVisible()) {
                continue;
            }
            rects.append({name, QRect(control->mapTo(bar, QPoint(0, 0)), control->size())});
        }
        return rects;
    };
    // The threshold must be the PREFERRED width, not the field's 200 px
    // minimum (batch 20261110d): this is the root-cause assertion, and it turns
    // red when the code regresses to `minimumSizeHint()`.
    std::printf("THRESHOLD used=%d preferred=%d minimum=%d\n",
                bar->wideLayoutThreshold(), bar->wideLayoutPreferredWidth(),
                bar->wideLayoutMinimumWidth());
    // The threshold MUST be the row's preferred width. On this Qt build the
    // preferred and minimum measurements coincide (both 699 px: every control's
    // sizeHint equals its minimum, and the field carries a 200 px minimum), so
    // the assertion is "used == preferred"; the BAND viewport below is what
    // actually turns red for a too-low threshold.
    check(QStringLiteral("find.geometry.threshold-is-preferred"),
          bar->wideLayoutThreshold() > 0 &&
              bar->wideLayoutThreshold() == bar->wideLayoutPreferredWidth(),
          QStringLiteral("used=%1 preferred=%2 minimum=%3")
              .arg(bar->wideLayoutThreshold())
              .arg(bar->wideLayoutPreferredWidth())
              .arg(bar->wideLayoutMinimumWidth()));

    struct ViewportCase {
        QSize size;
        bool expectCompact;
    };
    // 700×500 is the BAND case: the width leaves less than the wide form's
    // preferred width, so the bar MUST reflow. It is the case a too-low
    // threshold (the field's minimum instead of the row's preferred width)
    // keeps in the one-row form, where the controls no longer fit.
    for (const ViewportCase &viewportCase :
         {ViewportCase{QSize(1366, 768), false}, ViewportCase{QSize(1920, 1080), false},
          ViewportCase{QSize(700, 500), true}}) {
        const QSize &viewport = viewportCase.size;
        view->resize(viewport);
        QApplication::processEvents();
        bar->openFind();
        if (disclosure != nullptr) {
            disclosure->setChecked(true);
        }
        bar->setPattern(QStringLiteral("alpha"));
        QApplication::processEvents();
        const QString tag =
            QStringLiteral("%1x%2").arg(viewport.width()).arg(viewport.height());
        check(QStringLiteral("find.geometry.form-") + tag,
              bar->compactLayout() == viewportCase.expectCompact);
        check(QStringLiteral("find.geometry.bar-inside-view-") + tag,
              view->rect().contains(bar->geometry()),
              QStringLiteral("bar=%1x%2 view=%3x%4")
                  .arg(bar->width())
                  .arg(bar->height())
                  .arg(view->width())
                  .arg(view->height()));
        const QList<QPair<QString, QRect>> rects = visibleControlRects();
        QStringList outside;
        for (const auto &entry : rects) {
            if (!bar->rect().contains(entry.second)) {
                outside.append(entry.first);
            }
        }
        check(QStringLiteral("find.geometry.all-controls-inside-") + tag, outside.isEmpty(),
              outside.join(QStringLiteral(",")));
        QStringList overlaps;
        for (int left = 0; left < rects.size(); ++left) {
            for (int right = left + 1; right < rects.size(); ++right) {
                if (rects.at(left).second.intersects(rects.at(right).second)) {
                    overlaps.append(rects.at(left).first + QStringLiteral(" x ") +
                                    rects.at(right).first);
                }
            }
        }
        check(QStringLiteral("find.geometry.no-overlap-") + tag, overlaps.isEmpty(),
              overlaps.join(QStringLiteral(", ")));
        if (disclosure != nullptr) {
            const QRect mapped(disclosure->mapTo(bar, QPoint(0, 0)), disclosure->size());
            check(QStringLiteral("find.geometry.disclosure-clickable-") + tag,
                  disclosure->isVisible() && disclosure->isEnabled() &&
                      bar->rect().contains(mapped));
        }
        std::printf("GEOMETRY %s controls=%d overlaps=%d outside=%d bar=%dx%d\n",
                    qPrintable(tag), static_cast<int>(rects.size()),
                    static_cast<int>(overlaps.size()), static_cast<int>(outside.size()),
                    bar->width(), bar->height());
    }

    // --- batch 20261110d: bounded shared search history --------------------
    {
        auto &hub = vedit::search::SearchHub::instance();
        int saveCalls = 0;
        QStringList lastSaved;
        hub.setHistoryStore(nullptr, [&saveCalls, &lastSaved](const QStringList &entries) {
            ++saveCalls;
            lastSaved = entries;
        });
        hub.clearHistory();
        saveCalls = 0;
        for (int index = 1; index <= vedit::search::kSearchHistoryLimit; ++index) {
            hub.noteQuery(QStringLiteral("query_%1").arg(index));
        }
        check(QStringLiteral("history.bounded"),
              hub.history().size() == vedit::search::kSearchHistoryLimit,
              QString::number(hub.history().size()));
        check(QStringLiteral("history.newest-first"),
              hub.history().value(0) == QStringLiteral("query_20") &&
                  hub.history().value(hub.history().size() - 1) == QStringLiteral("query_1"));
        const int callsAfterFill = saveCalls;
        check(QStringLiteral("history.store-called"), saveCalls == vedit::search::kSearchHistoryLimit,
              QString::number(saveCalls));

        // Duplicate: moves to the front, never appends a second copy.
        hub.noteQuery(QStringLiteral("query_10"));
        const QStringList deduped = hub.history();
        check(QStringLiteral("history.dedupe"),
              deduped.size() == vedit::search::kSearchHistoryLimit &&
                  deduped.value(0) == QStringLiteral("query_10") &&
                  deduped.count(QStringLiteral("query_10")) == 1,
              QString::number(deduped.size()));
        check(QStringLiteral("history.dedupe-one-save"), saveCalls == callsAfterFill + 1);

        // Blank / whitespace-only queries are not history entries.
        hub.noteQuery(QStringLiteral("   "));
        check(QStringLiteral("history.blank-ignored"),
              hub.history() == deduped && saveCalls == callsAfterFill + 1);

        // The (N+1)th query evicts the oldest.
        hub.noteQuery(QStringLiteral("query_21"));
        const QStringList evicted = hub.history();
        check(QStringLiteral("history.evicts-oldest"),
              evicted.size() == vedit::search::kSearchHistoryLimit &&
                  !evicted.contains(QStringLiteral("query_1")) &&
                  evicted.contains(QStringLiteral("query_21")) &&
                  evicted.value(0) == QStringLiteral("query_21") &&
                  lastSaved == evicted,
              QStringLiteral("size=%1 first=%2")
                  .arg(evicted.size())
                  .arg(evicted.value(0)));
        std::printf("HISTORY-AFTER-21 count=%d newest-first='%s' oldest='%s'\n",
                    static_cast<int>(evicted.size()), qPrintable(evicted.join(QStringLiteral(","))),
                    qPrintable(evicted.value(evicted.size() - 1)));
        hub.clearHistory();
        hub.setHistoryStore(nullptr, nullptr);
    }

    // --- batch 20261110d: the on-disk form (round trip + broken file) ------
    {
        QTemporaryDir stateDir;
        check(QStringLiteral("history.file.temp"), stateDir.isValid());
        const QString path = vedit::search::searchHistoryPath(stateDir.path());
        check(QStringLiteral("history.file.path"),
              path.endsWith(QStringLiteral("/search-history.json")), path);
        // A broken file is an EMPTY history, never a crash or an error dialog.
        {
            QFile broken(path);
            if (broken.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                broken.write("{ this is not json");
            }
        }
        check(QStringLiteral("history.file.broken-is-empty"),
              vedit::search::loadSearchHistoryFile(stateDir.path()).isEmpty());

        auto &hub = vedit::search::SearchHub::instance();
        hub.clearHistory();
        hub.setHistoryStore(
            [&stateDir]() { return vedit::search::loadSearchHistoryFile(stateDir.path()); },
            nullptr);
        hub.noteQuery(QStringLiteral("probe_one"));
        hub.noteQuery(QStringLiteral("probe_two"));
        hub.noteQuery(QStringLiteral("probe_one"));
        check(QStringLiteral("history.file.in-memory"),
              hub.history() == QStringList{QStringLiteral("probe_one"),
                                           QStringLiteral("probe_two")},
              hub.history().join(QStringLiteral(",")));
        // The product saves once at quit; do the same write here.
        check(QStringLiteral("history.file.saved"),
              vedit::search::saveSearchHistoryFile(stateDir.path(), hub.history()));
        // "Relaunch": a fresh store adopts the file as-is.
        hub.setHistoryStore(
            [&stateDir]() { return vedit::search::loadSearchHistoryFile(stateDir.path()); },
            nullptr);
        check(QStringLiteral("history.file.survives-relaunch"),
              hub.history() == QStringList{QStringLiteral("probe_one"),
                                           QStringLiteral("probe_two")},
              hub.history().join(QStringLiteral(",")));
        std::printf("HISTORY-FILE relaunch='%s'\n", qPrintable(hub.history().join(QStringLiteral(","))));
        hub.clearHistory();
        hub.setHistoryStore(nullptr, nullptr);
    }

    // --- evidence artefact ------------------------------------------------
    if (!screenshotDirectory.isEmpty()) {
        QDir().mkpath(screenshotDirectory);
        // Back to a normal pane width so the wide form is captured too (the
        // narrow form was captured above at 360 px).
        view->resize(QSize(760, 320));
        QApplication::processEvents();
        // A realistic state: the alpha sample, no scope, cursor on the first
        // match, so the screenshot shows counts instead of an empty result.
        document->setText(QStringLiteral("alpha beta alpha\nAlpha gamma alpha\nbeta alpha\n"));
        view->setCursorPosition(KTextEditor::Cursor(0, 0));
        bar->setInSelection(false);
        bar->setPattern(QStringLiteral("alpha"));
        QApplication::processEvents();
        const QString findShot =
            screenshotDirectory + QStringLiteral("/find-bar-find.png");
        const QString replaceShot =
            screenshotDirectory + QStringLiteral("/find-bar-replace.png");
        bar->openFind();
        QApplication::processEvents();
        check(QStringLiteral("find.evidence.find-screenshot"), bar->grab().save(findShot),
              findShot);
        bar->openReplace();
        if (disclosure != nullptr) {
            disclosure->setChecked(true);
        }
        QApplication::processEvents();
        check(QStringLiteral("find.evidence.replace-screenshot"), bar->grab().save(replaceShot),
              replaceShot);
        if (disclosure != nullptr) {
            disclosure->setChecked(false);
        }
    }

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
