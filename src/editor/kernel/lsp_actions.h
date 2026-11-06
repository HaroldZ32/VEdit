#pragma once

// The editor's LSP action channel (T15, AC-3.1/3.2).
//
// The RIGHT-CLICK MENU (editor_context_menu.*) is a client of this channel: it
// asks for every action's availability and dispatches through the ONE entry
// point here. The channel never keeps a second copy of a backend capability:
//
//   * the capability decisions come from the diagnostics lane's published gate
//     map (`vedit.diagnostics.gates` on the top-level window, filled from the
//     C ABI table by BackendSwitchState::gate()),
//   * the request path is the shell action registered for the action
//     (`action-diagnostics.definition` / ... in src/app/command_registry.cpp),
//     so the menu triggers exactly what the menu bar/toolbar trigger,
//   * this channel adds the two facts the gate cannot know: whether THIS build
//     has a request path for the action at all (hover has none), and whether
//     the view can serve it right now (F8 needs at least one diagnostic).
//
// Every refusal names its reason (and prints one `ACTION-REFUSED` machine line);
// an action is never silently `undefined`.
//

// F8 / Shift+F8 diagnostic navigation lives here too: the entries are the
// view's inline annotations (DiagnosticsAnnotations, pushed by the diagnostics
// lane) and the selection rule is a pure function, so the order/wrap behaviour
// is testable without a server.
#include "diagnostics_annotations.h"

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QAction;
class QWidget;

namespace KTextEditor {
class Cursor;
class View;
} // namespace KTextEditor


namespace vedit::editor {

// ---------------------------------------------------------------------------
// LSP navigation results: applying a rename and listing references (batch 2,
// AC-2.1/AC-2.2). These live here because they are the RESULT side of the LSP
// action channel: the same lane that dispatches `diagnostics.rename` /
// `diagnostics.references` feeds their answers back through these two functions.
//
// Both are widget-free so the edit-core selfcheck can drive them without a
// window; both name their refusal instead of degrading silently.
// ---------------------------------------------------------------------------

/// One text edit of an LSP `WorkspaceEdit` (positions 0-based, LSP convention:
/// line + UTF-16 column, end position exclusive).
struct WorkspaceTextEdit {
    QString filePath; // absolute path (the caller resolves the URI)
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;
    QString newText; // "" deletes the range
};

/// One navigation target (`textDocument/references` answer).
struct WorkspaceLocation {
    QString filePath; // absolute path
    int line = 0;     // 0-based
    int column = 0;   // 0-based
};

/// One row of the references list.
struct ReferenceRow {
    QString filePath;
    int line = 0;   // 0-based
    int column = 0; // 0-based
    /// The source line (trimmed and length-capped for display); "" when the file
    /// could not be read — a location is still useful on its own.
    QString snippet;
    /// "path:line:column" with 1-based line/column, exactly what the list shows.
    QString location() const;
};

/// Builds the rows the references UI lists: sorted by path, then line, then
/// column; duplicates collapse. A snippet is read from the OPEN document when the
/// file is open in the editor (so it matches the buffer the user sees) and from
/// disk otherwise. An empty answer yields an empty list — the UI's real empty
/// state.
QList<ReferenceRow> buildReferenceRows(const QList<WorkspaceLocation> &locations);

/// What one `applyWorkspaceEdit()` call did.
struct WorkspaceEditReport {
    bool ok = false;
    QString error;
    int editCount = 0;
    int fileCount = 0;
    int openDocumentCount = 0; // documents changed inside the editor (undoable)
    int writtenFileCount = 0;  // targets that were not open, written atomically
    QStringList perFile;       // "<path>: <n> edit(s) [editor|disk]"
    /// One line for the status bar / log: counts plus where the edits landed.
    QString summary() const;
};

/// Applies a rename result (AC-2.1).
///
/// Open documents are found across the whole application — every
/// `KTextEditor::View` of every pane and every tab counts, not just the active
/// one — so a rename spanning panes lands in the buffers the user sees. A target
/// that is not open is read, edited and written atomically through the kernel's
/// own save path; the report says which files took which route.
///
/// An open document is edited inside ONE `KTextEditor::Document::
/// EditingTransaction`, so the whole rename is ONE undo step.
///
/// Two phases: every file is resolved and every range validated FIRST, so an
/// unusable edit (missing file, out-of-range position) refuses the whole rename
/// and leaves every buffer and every file untouched. Only then are the edits
/// applied. A write failure after that point is reported honestly: the report
/// carries the files that were already changed.
WorkspaceEditReport applyWorkspaceEdit(const QList<WorkspaceTextEdit> &edits);

/// How many open documents the engine can see (probe/selfcheck seam). Both
/// editor providers mark the open path on the view (`vedit.openFilePath`, see
/// editor_provider.h); the document URL is the fallback.
int openDocumentCount();

/// One action the editor's context menu offers (AC-3.1: the LSP actions plus the
/// diagnostics navigation).
enum class EditorAction {
    Definition = 0,
    References,
    Rename,
    Format,
    Hover,
    NextDiagnostic,
    PreviousDiagnostic,
};

/// The LSP action id used by the capability gate map and the backend capability
/// table (`definition`, `references`, `rename`, `formatting`, `hover`). The two
/// navigation actions have no capability row and return an empty string.
QString editorActionCapability(EditorAction action);
/// The shell command id of the wired request path (`diagnostics.definition` ...).
/// Hover has none in this build and returns an empty string.
QString editorActionCommand(EditorAction action);
/// The menu label, the default shortcut and the human name (for log lines).
QString editorActionLabel(EditorAction action);
QKeySequence editorActionShortcut(EditorAction action);
QString editorActionName(EditorAction action);

/// The merged availability of one action.
struct ActionAvailability {
    bool enabled = false;
    /// False when the backend row is `Unknown`: the UI says "connect to know"
    /// instead of pretending support or absence.
    bool known = true;
    /// `backend` / `always-verible-format` / `editor` / `unwired` / `none`.
    QString source;
    /// Why the action is unavailable; never empty when `enabled` is false.
    QString reason;
};

/// The capability gate map the diagnostics lane publishes on the shell
/// (`vedit.diagnostics.gates`). Empty when that lane is not installed.
QVariantMap publishedGates(QWidget *host);
/// The backend name from the published capability table (`vedit.diagnostics.
/// capabilities`), or an empty string when the lane is not installed.
QString publishedBackend(QWidget *host);

/// The shell action that carries the wired request path for `action`
/// (`action-<command id>`), or nullptr.
QAction *shellActionFor(QWidget *host, EditorAction action);

/// The result of one dispatch: what the channel actually did, and why not.
struct ActionDispatch {
    bool dispatched = false;
    QString reason;
};

/// Install (idempotently) and find the channel of one view.
class LspActionChannel final : public QObject
{
    Q_OBJECT
public:
    static LspActionChannel *install(KTextEditor::View *view);
    static LspActionChannel *instanceFor(const KTextEditor::View *view);

    /// The menu order (AC-3.1): the five LSP actions, then the navigation pair.
    QList<EditorAction> actions() const;

    /// Resolve one action for this view (never guesses: an absent gate map or a
    /// missing request path is an explicit, reason-carrying refusal).
    ActionAvailability availability(EditorAction action) const;
    /// Dispatch one action through the wired request path. A refusal is logged
    /// and carries its reason; nothing is silent.
    ActionDispatch dispatch(EditorAction action);

    QAction *shellAction(EditorAction action) const;
    KTextEditor::View *view() const { return view_; }

    // --- observables (probe / self-check) ---------------------------------
    int dispatchCount(EditorAction action) const;
    int refusalCount(EditorAction action) const;
    QString lastRefusal() const { return lastRefusal_; }

private:
    explicit LspActionChannel(KTextEditor::View *view);
    static int indexOf(EditorAction action);

    KTextEditor::View *view_ = nullptr;
    QList<int> dispatches_;
    QList<int> refusals_;
    QString lastRefusal_;
};

/// Ctrl+click (UX batch 2, item 13): put the caret on the clicked position and
/// run the SAME `Definition` dispatch F12, the Project menu and the context menu
/// use. The caret moves FIRST because that request path reads the view's cursor;
/// no request/capability logic is duplicated here — the channel's own
/// `availability()`/`dispatch()` decide and trigger. The clicked position is
/// installed as the SINGLE cursor position, because a Ctrl+click is a click:
/// the consumed press would otherwise leave an existing multi-cursor set alive.
///
/// When the action is unavailable (backend gate off, no request path, no wired
/// action) nothing is dispatched and the caret is left alone: the caller can
/// then let the click through as a plain click and report `reason`.
ActionDispatch dispatchDefinitionAt(KTextEditor::View *view,
                                    const KTextEditor::Cursor &position);

// --- F8 / Shift+F8 ---------------------------------------------------------

/// One navigation step over a view's diagnostics.
struct DiagnosticStep {
    bool found = false; ///< false when the view has no diagnostics at all
    int index = -1;     ///< index into the ordered entry list
    int line = 0;       ///< zero-based target line
    int column = 0;     ///< zero-based target column
    bool wrapped = false;
};

/// Order diagnostics by (line, column); invalid lines are dropped. The F8 rule
/// and the menu's "at least one diagnostic" fact both use this one list.
QList<DiagnosticAnnotation> orderedDiagnostics(const QList<DiagnosticAnnotation> &entries);

/// Pure selection rule: the first diagnostic strictly after (forward) / before
/// (backward) the given position, wrapping around. `found = false` when the list
/// is empty (the caller then reports it instead of moving the cursor).
DiagnosticStep nextDiagnostic(const QList<DiagnosticAnnotation> &ordered, int line, int column);
DiagnosticStep previousDiagnostic(const QList<DiagnosticAnnotation> &ordered, int line, int column);

/// The F8/Shift+F8 key filter installed on a view. It follows the editor
/// kernel's key rule (the real focus is KateViewInternal, so the filter is
/// installed on the view's widget tree and claims ShortcutOverride for exactly
/// F8 / Shift+F8).
class DiagnosticNavigator final : public QObject
{
    Q_OBJECT
public:
    static DiagnosticNavigator *install(KTextEditor::View *view);
    static DiagnosticNavigator *instanceFor(const KTextEditor::View *view);

    /// Navigate from the view's cursor; applies the cursor move + status message
    /// and returns the step. Never moves when the view has no diagnostics.
    DiagnosticStep navigate(bool forward);
    /// Diagnostics currently attached to the view (ordered).
    QList<DiagnosticAnnotation> diagnostics() const;
    int stepCount() const { return steps_; }

    KTextEditor::View *view() const { return view_; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit DiagnosticNavigator(KTextEditor::View *view);

    KTextEditor::View *view_ = nullptr;
    int steps_ = 0;
};

/// One machine line per performed action / refusal / navigation (the evidence
/// probes read these). `VEDIT_QUIET` silences it, like the other lanes.
void logEditorActionLine(const QString &line);

/// The file shown by a view (the provider's open-file property first, then the
/// document URL), empty when the view has neither.
QString viewFilePath(const KTextEditor::View *view);

} // namespace vedit::editor
