// RM-12 selftest: the destructive layout commands must not silently destroy
// unsaved documents.
//
// Shape of the defect this locks down: `EditorArea::resetLayout()` calls
// `clearTree()`, which deletes every pane and — with the product editor — the
// KTextEditor document that dies with its view. `View → Reset Layout`,
// `File → New Empty Layout` and (same code path) `View → Mixed Split` used to
// reach it without the shell's unsaved-document confirmation, so a dirty buffer
// vanished with no prompt.
//
// Every scenario below dispatches the REAL command: it takes the command
// registry's QAction (the object the menu, the toolbar and the shortcut are all
// bound to) and triggers it. `resetLayout()` is never called directly.
//
// The shell's public test seam `setUnsavedPromptProvider()` answers the SAME
// unsaved-document question the window close uses, so the test can say Cancel /
// Discard / Save without a modal dialog (and offscreen has no user to ask, so
// the real prompt would answer Cancel — the seam is what makes the answer
// observable at all, including "was the user asked?" via the call count).
#include "app_shell.h"

#include "command_registry.h"
#include "editor_area.h"

#if defined(VEDIT_RESET_HAVE_EDIT_CORE)
#include "kernel/ktexteditor_provider.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextDocument>

#include <cstdio>
#include <memory>

namespace {

int failures = 0;

void check(const QString &name, bool ok, const QString &detail = QString())
{
    std::printf("CHECK %s %s%s%s\n", qPrintable(name), ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : " ", qPrintable(detail));
    std::fflush(stdout);
    if (!ok) {
        ++failures;
    }
}

using vedit::app::ApplicationShell;
using CloseChoice = ApplicationShell::CloseChoice;

std::unique_ptr<ApplicationShell> makeShell(const QString &stateDir)
{
    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    auto shell = std::make_unique<ApplicationShell>(options);
    shell->startSession();
    QApplication::processEvents();
    return shell;
}

// Answers the shell's one unsaved-document question and counts how often it was
// asked. `answer` is what the simulated user clicks.
struct PromptLog {
    int calls = 0;
    QStringList lastDisplays;
    QStringList lastPaths;
    CloseChoice answer = CloseChoice::Cancel;
};

void installPrompt(ApplicationShell &shell, PromptLog *log)
{
    shell.setUnsavedPromptProvider(
        [log](const QStringList &displays, const QStringList &paths) -> CloseChoice {
            ++log->calls;
            log->lastDisplays = displays;
            log->lastPaths = paths;
            return log->answer;
        });
}

bool triggerCommand(ApplicationShell &shell, const char *id)
{
    QAction *action = shell.commands()->action(QString::fromLatin1(id));
    if (action == nullptr) {
        return false;
    }
    action->trigger();
    QApplication::processEvents();
    return true;
}

QByteArray layoutJson(ApplicationShell &shell)
{
    return QJsonDocument(shell.editorArea()->saveLayout()).toJson(QJsonDocument::Compact);
}

// One dirty buffer: the pane/tab that owns it, its text and the layout JSON
// captured AFTER the edit, so "untouched" can be asserted exactly.
struct DirtyState {
    vedit::app::EditorPane *pane = nullptr;
    QString paneId;
    int tab = -1;
    QString text;
    QByteArray layout;

    bool valid() const { return pane != nullptr; }
};

// A pane pointer is not a safe identity across resetLayout(): it deletes the old
// pane and createPane() may allocate the replacement at the same address. The
// pane id is unique per EditorPane instance, so it is the identity used below.
vedit::app::EditorPane *findPane(ApplicationShell &shell, const QString &paneId)
{
    for (vedit::app::EditorPane *pane : shell.editorArea()->panes()) {
        if (pane != nullptr && pane->paneId() == paneId) {
            return pane;
        }
    }
    return nullptr;
}

DirtyState markPaneDirty(vedit::app::EditorPane *pane)
{
    DirtyState state;
    if (pane == nullptr) {
        return state;
    }
    auto *edit = qobject_cast<QPlainTextEdit *>(pane->editorWidget());
    if (edit == nullptr) {
        return state;
    }
    // Replace the demo pane's placeholder text with one unambiguous dirty line,
    // so the failure message can quote exactly what was lost.
    edit->setPlainText(QStringLiteral("RM-12 unsaved edit"));
    edit->document()->setModified(true);
    if (pane->tabCount() < 1) {
        return state;
    }
    state.pane = pane;
    state.paneId = pane->paneId();
    state.tab = 0;
    state.text = edit->toPlainText();
    return state;
}

DirtyState markActiveDirty(ApplicationShell &shell)
{
    DirtyState state = markPaneDirty(shell.editorArea()->activePane());
    if (state.valid()) {
        state.layout = layoutJson(shell);
    }
    return state;
}

// True only when the pane still owns the tab, the text is byte-identical, the
// buffer is still modified and the layout JSON did not change.
bool documentIntact(ApplicationShell &shell, const DirtyState &state, QString *detail)
{
    if (!state.valid()) {
        *detail = QStringLiteral("no dirty state was created");
        return false;
    }
    vedit::app::EditorPane *pane = findPane(shell, state.paneId);
    if (pane == nullptr) {
        *detail = QStringLiteral("pane was destroyed/replaced");
        return false;
    }
    if (state.tab >= state.pane->tabCount()) {
        *detail = QStringLiteral("tab was destroyed");
        return false;
    }
    auto *edit = qobject_cast<QPlainTextEdit *>(pane->editorWidget());
    if (edit == nullptr) {
        *detail = QStringLiteral("editor widget was replaced");
        return false;
    }
    if (edit->toPlainText() != state.text) {
        *detail = QStringLiteral("text changed to '%1'").arg(edit->toPlainText());
        return false;
    }
    if (!pane->isTabModified(state.tab)) {
        *detail = QStringLiteral("buffer is no longer modified");
        return false;
    }
    if (layoutJson(shell) != state.layout) {
        *detail = QStringLiteral("layout JSON changed");
        return false;
    }
    return true;
}

// The user said Cancel: the pane, its text, its modified flag and the layout all
// survive; the prompt was asked exactly once.
void expectCancelled(ApplicationShell &shell, const DirtyState &state, const PromptLog &log,
                     const char *command, const QString &label)
{
    QString shapeDetail;
    const bool shapeIntact = documentIntact(shell, state, &shapeDetail);
    std::printf("SHAPE %-28s prompt_calls=%d panes=%d document=%s\n", command, log.calls,
                int(shell.editorArea()->panes().size()),
                shapeIntact ? "intact" : qPrintable(shapeDetail));
    std::fflush(stdout);
    check(QStringLiteral("%1.prompt-asked").arg(label), log.calls == 1,
          QStringLiteral("prompt calls=%1").arg(log.calls));
    check(QStringLiteral("%1.document").arg(label), findPane(shell, state.paneId) != nullptr,
          QStringLiteral("pane survived"));
    QString detail;
    check(QStringLiteral("%1.text-and-modified").arg(label), documentIntact(shell, state, &detail),
          detail);
}

void testResetLayoutCancel(const QString &root)
{
    auto shell = makeShell(root + QStringLiteral("/reset-cancel"));
    const DirtyState dirty = markActiveDirty(*shell);
    PromptLog log;
    installPrompt(*shell, &log);
    const bool dispatched = triggerCommand(*shell, "view.layout.reset");
    check(QStringLiteral("rm12.reset.action-dispatched"), dispatched);
    expectCancelled(*shell, dirty, log, "view.layout.reset", QStringLiteral("rm12.reset"));
}

void testNewEmptyLayoutCancel(const QString &root)
{
    auto shell = makeShell(root + QStringLiteral("/new-empty-cancel"));
    const DirtyState dirty = markActiveDirty(*shell);
    PromptLog log;
    installPrompt(*shell, &log);
    const bool dispatched = triggerCommand(*shell, "file.new-layout");
    check(QStringLiteral("rm12.new-empty.action-dispatched"), dispatched);
    expectCancelled(*shell, dirty, log, "file.new-layout", QStringLiteral("rm12.new-empty"));
}

// Family check: the mixed split preset reaches the same resetLayout().
void testMixedSplitCancel(const QString &root)
{
    auto shell = makeShell(root + QStringLiteral("/mixed-cancel"));
    const DirtyState dirty = markActiveDirty(*shell);
    PromptLog log;
    installPrompt(*shell, &log);
    const bool dispatched = triggerCommand(*shell, "view.layout.mixed");
    check(QStringLiteral("rm12.mixed.action-dispatched"), dispatched);
    expectCancelled(*shell, dirty, log, "view.layout.mixed", QStringLiteral("rm12.mixed"));
}

// Several dirty buffers are answered by ONE prompt (the shell's existing
// all-documents confirmation), and Cancel keeps every one of them.
void testOnePromptForAllDirty(const QString &root)
{
    auto shell = makeShell(root + QStringLiteral("/one-prompt"));
    shell->editorArea()->splitActive(Qt::Horizontal);
    QApplication::processEvents();
    const auto panes = shell->editorArea()->panes();
    check(QStringLiteral("rm12.one-prompt.two-panes"), panes.size() == 2);
    QStringList paneIds;
    QStringList texts;
    for (vedit::app::EditorPane *pane : panes) {
        auto *edit = qobject_cast<QPlainTextEdit *>(pane->editorWidget());
        check(QStringLiteral("rm12.one-prompt.editor"), edit != nullptr);
        if (edit == nullptr) {
            return;
        }
        edit->setPlainText(QStringLiteral("dirty in this pane"));
        edit->document()->setModified(true);
        paneIds.append(pane->paneId());
        texts.append(edit->toPlainText());
    }
    PromptLog log;
    installPrompt(*shell, &log);
    triggerCommand(*shell, "view.layout.reset");
    std::printf("SHAPE view.layout.reset(2 dirty) prompt_calls=%d prompt_docs=%d panes=%d\n",
                log.calls, int(log.lastDisplays.size()),
                int(shell->editorArea()->panes().size()));
    std::fflush(stdout);
    check(QStringLiteral("rm12.one-prompt.single-call"), log.calls == 1,
          QStringLiteral("calls=%1").arg(log.calls));
    check(QStringLiteral("rm12.one-prompt.lists-both"), log.lastDisplays.size() == 2,
          QStringLiteral("docs=%1").arg(log.lastDisplays.size()));
    bool kept = true;
    for (int index = 0; index < paneIds.size(); ++index) {
        vedit::app::EditorPane *pane = findPane(*shell, paneIds.at(index));
        if (pane == nullptr) {
            kept = false;
            continue;
        }
        auto *edit = qobject_cast<QPlainTextEdit *>(pane->editorWidget());
        kept = kept && edit != nullptr && edit->toPlainText() == texts.at(index)
               && pane->isTabModified(0);
    }
    check(QStringLiteral("rm12.one-prompt.documents-kept"), kept);
}

// The confirmation must really work: Discard resets the layout (the user said
// yes), and the prompt is what made it happen.
void testDiscardResets(const QString &root)
{
    auto shell = makeShell(root + QStringLiteral("/discard"));
    const DirtyState dirty = markActiveDirty(*shell);
    const QString oldPaneId = dirty.paneId;
    PromptLog log;
    log.answer = CloseChoice::Discard;
    installPrompt(*shell, &log);
    triggerCommand(*shell, "view.layout.reset");
    std::printf("SHAPE view.layout.reset(discard) prompt_calls=%d panes_before=1 panes_after=%d\n",
                log.calls, int(shell->editorArea()->panes().size()));
    std::fflush(stdout);
    check(QStringLiteral("rm12.discard.prompt-asked"), log.calls == 1,
          QStringLiteral("calls=%1").arg(log.calls));
    check(QStringLiteral("rm12.discard.layout-reset"),
          shell->editorArea()->panes().size() == 1
              && findPane(*shell, oldPaneId) == nullptr);
    check(QStringLiteral("rm12.discard.no-dirty-left"),
          !shell->editorArea()->panes().isEmpty()
              && !shell->editorArea()->panes().first()->isTabModified(0));
}

// Requirement: a clean reset stays instant and asks nothing — both commands.
void testCleanResetDoesNotPrompt(const QString &root)
{
    auto shell = makeShell(root + QStringLiteral("/clean"));
    PromptLog log;
    log.answer = CloseChoice::Discard; // if it asked, we would notice the call
    installPrompt(*shell, &log);

    shell->editorArea()->splitActive(Qt::Horizontal);
    QApplication::processEvents();
    check(QStringLiteral("rm12.clean.two-panes"), shell->editorArea()->panes().size() == 2);
    triggerCommand(*shell, "view.layout.reset");
    std::printf("SHAPE view.layout.reset(clean) prompt_calls=%d panes_after=%d\n", log.calls,
                int(shell->editorArea()->panes().size()));
    std::fflush(stdout);
    check(QStringLiteral("rm12.clean.reset-no-prompt"), log.calls == 0,
          QStringLiteral("calls=%1").arg(log.calls));
    check(QStringLiteral("rm12.clean.reset-works"), shell->editorArea()->panes().size() == 1);

    log.calls = 0;
    shell->editorArea()->splitActive(Qt::Vertical);
    QApplication::processEvents();
    triggerCommand(*shell, "file.new-layout");
    std::printf("SHAPE file.new-layout(clean) prompt_calls=%d panes_after=%d\n", log.calls,
                int(shell->editorArea()->panes().size()));
    std::fflush(stdout);
    check(QStringLiteral("rm12.clean.new-empty-no-prompt"), log.calls == 0,
          QStringLiteral("calls=%1").arg(log.calls));
    check(QStringLiteral("rm12.clean.new-empty-works"), shell->editorArea()->panes().size() == 1);
}

// The user answered Save: the buffer is written through the SAME confirmation,
// then the reset happens.
void testSaveBranchWritesFile(const QString &root)
{
    QTemporaryDir documents;
    check(QStringLiteral("rm12.save.temp-dir"), documents.isValid());
    if (!documents.isValid()) {
        return;
    }
    const QString path = documents.filePath(QStringLiteral("rm12_save.sv"));
    {
        QFile file(path);
        check(QStringLiteral("rm12.save.seed-file"),
              file.open(QIODevice::WriteOnly | QIODevice::Truncate)
                  && file.write("module m; endmodule\n") > 0);
    }
    auto shell = makeShell(root + QStringLiteral("/save"));
    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    QString error;
    check(QStringLiteral("rm12.save.open"), pane != nullptr && pane->openFile(path, &error),
          error);
    auto *edit = pane != nullptr ? qobject_cast<QPlainTextEdit *>(pane->editorWidget()) : nullptr;
    check(QStringLiteral("rm12.save.editor"), edit != nullptr);
    if (edit == nullptr) {
        return;
    }
    edit->setPlainText(QStringLiteral("// rm12 saved edit\n"));
    edit->document()->setModified(true);
    check(QStringLiteral("rm12.save.dirty"), pane->isTabModified(0));

    PromptLog log;
    log.answer = CloseChoice::Save;
    installPrompt(*shell, &log);
    triggerCommand(*shell, "view.layout.reset");
    std::printf("SHAPE view.layout.reset(save) prompt_calls=%d panes_after=%d\n", log.calls,
                int(shell->editorArea()->panes().size()));
    std::fflush(stdout);
    check(QStringLiteral("rm12.save.prompt-asked"), log.calls == 1,
          QStringLiteral("calls=%1").arg(log.calls));
    QFile written(path);
    const bool readable = written.open(QIODevice::ReadOnly);
    const QString content = readable ? QString::fromUtf8(written.readAll()) : QString();
    check(QStringLiteral("rm12.save.file-written"),
          readable && content.contains(QStringLiteral("rm12 saved edit")), content.trimmed());
    check(QStringLiteral("rm12.save.layout-reset"), shell->editorArea()->panes().size() == 1);
}

// Family: File > Quit / Ctrl+Q must ask too. `QApplication::quit()` ends the
// process without running closeEvent(), so before the fix every dirty buffer
// died unasked even though the window's X button asked.
void testQuitCancel(const QString &root)
{
    auto shell = makeShell(root + QStringLiteral("/quit"));
    const DirtyState dirty = markActiveDirty(*shell);
    PromptLog log;
    installPrompt(*shell, &log);
    const bool dispatched = triggerCommand(*shell, "file.quit");
    std::printf("SHAPE file.quit(dirty) prompt_calls=%d\n", log.calls);
    std::fflush(stdout);
    check(QStringLiteral("rm12.quit.action-dispatched"), dispatched);
    check(QStringLiteral("rm12.quit.prompt-asked"), log.calls == 1,
          QStringLiteral("calls=%1").arg(log.calls));
    QString detail;
    check(QStringLiteral("rm12.quit.document-kept"), documentIntact(*shell, dirty, &detail),
          detail);
}

void testCleanQuitDoesNotPrompt(const QString &root)
{
    auto shell = makeShell(root + QStringLiteral("/quit-clean"));
    PromptLog log;
    log.answer = CloseChoice::Discard; // if it asked, the call count would show it
    installPrompt(*shell, &log);
    triggerCommand(*shell, "file.quit");
    std::printf("SHAPE file.quit(clean) prompt_calls=%d\n", log.calls);
    std::fflush(stdout);
    check(QStringLiteral("rm12.quit.clean-no-prompt"), log.calls == 0,
          QStringLiteral("calls=%1").arg(log.calls));
}

// Existing family member: closing a pane already routes through the tab-close
// confirmation (confirmPaneClose -> promptUnsaved). Headless/offscreen has
// nobody to ask, so the safe answer is Cancel: the document must survive.
void testClosePaneKeepsDirty(const QString &root)
{
    auto shell = makeShell(root + QStringLiteral("/close-pane"));
    const DirtyState dirty = markActiveDirty(*shell);
    triggerCommand(*shell, "file.close-pane");
    std::printf("SHAPE file.close-pane(dirty) panes=%d\n",
                int(shell->editorArea()->panes().size()));
    std::fflush(stdout);
    QString detail;
    check(QStringLiteral("rm12.close-pane.document-kept"), documentIntact(*shell, dirty, &detail),
          detail);
}

#if defined(VEDIT_RESET_HAVE_EDIT_CORE)
// The product editor: a real KTextEditor document, whose lifetime is the view's
// (src/editor/kernel/ktexteditor_provider.cpp: "the document dies with its
// view"). This is the exact lifetime the defect destroyed; Cancel must keep it.
void testRealEditorCancel(const QString &root)
{
    vedit::app::ShellOptions options;
    options.stateDir = root + QStringLiteral("/ktexteditor");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    auto shell = std::make_unique<ApplicationShell>(options);
    shell->setEditorProvider(vedit::editor::makeEditCoreProvider());
    shell->startSession();
    QApplication::processEvents();

    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    auto *view = pane != nullptr ? qobject_cast<KTextEditor::View *>(pane->editorWidget()) : nullptr;
    check(QStringLiteral("rm12.real-editor.view"), view != nullptr);
    if (view == nullptr) {
        return;
    }
    KTextEditor::Document *document = view->document();
    document->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("// rm12 ktexteditor"));
    check(QStringLiteral("rm12.real-editor.dirty"), document->isModified());
    const QString before = document->text();

    PromptLog log;
    installPrompt(*shell, &log);
    triggerCommand(*shell, "view.layout.reset");
    auto *afterView = pane != nullptr ? qobject_cast<KTextEditor::View *>(pane->editorWidget())
                                      : nullptr;
    KTextEditor::Document *afterDocument = afterView != nullptr ? afterView->document() : nullptr;
    std::printf("SHAPE view.layout.reset(ktexteditor) prompt_calls=%d document=%s\n", log.calls,
                afterDocument == document ? "same-object" : "LOST/REPLACED");
    std::fflush(stdout);
    check(QStringLiteral("rm12.real-editor.prompt-asked"), log.calls == 1,
          QStringLiteral("calls=%1").arg(log.calls));
    check(QStringLiteral("rm12.real-editor.document-survives"),
          afterDocument == document && afterDocument->text() == before,
          afterDocument == nullptr ? QStringLiteral("document was destroyed")
                                   : QStringLiteral("text intact: %1").arg(afterDocument->text()));
}
#endif

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vedit-reset-layout-guard-selftest"));

    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::printf("CHECK rm12.temp-dir FAIL\n");
        return 1;
    }
    const QString root = temporary.path();

    testResetLayoutCancel(root);
    testNewEmptyLayoutCancel(root);
    testMixedSplitCancel(root);
    testOnePromptForAllDirty(root);
    testDiscardResets(root);
    testCleanResetDoesNotPrompt(root);
    testSaveBranchWritesFile(root);
#if defined(VEDIT_RESET_HAVE_EDIT_CORE)
    testRealEditorCancel(root);
#else
    std::printf("SKIP rm12.real-editor (build has no edit-core provider)\n");
#endif
    testClosePaneKeepsDirty(root);
    testQuitCancel(root);
    testCleanQuitDoesNotPrompt(root);

    std::printf("RM12-RESET-LAYOUT-SELFTEST failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
