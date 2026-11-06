// Lane selftest for T13 (AC-4.3): find / replace API + the in-view bar.
//
// ADDENDUM A-7 (T13) grants this file. It is the lane's ctest target:
//
//   vedit-editor-find-replace-selftest
//   vedit-editor-find-replace-selftest --screenshot <dir>   # bar evidence PNG
//
// Everything runs against the real KTextEditor document/view in the pinned Qt6
// image. When the KTextEditor plugin is unavailable the program exits 77 (the
// ctest SKIP_RETURN_CODE), never 0 — a skip must not look like a pass.
#include "find_replace.h"

#if defined(VEDIT_EDIT_CORE_HAVE_APP_SEAM)
#include "ktexteditor_provider.h"
#endif
#include "undo_ops.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QShortcut>
#include <QStringList>
#include <QTest>

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
    QTest::keyClick(keyTarget(), Qt::Key_H, Qt::ControlModifier);
    QApplication::processEvents();
    check(QStringLiteral("find.bar.ctrl-h-replace-mode"), bar->replaceRowVisible());

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

    // --- evidence artefact ------------------------------------------------
    if (!screenshotDirectory.isEmpty()) {
        QDir().mkpath(screenshotDirectory);
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
        QApplication::processEvents();
        check(QStringLiteral("find.evidence.replace-screenshot"), bar->grab().save(replaceShot),
              replaceShot);
    }

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
