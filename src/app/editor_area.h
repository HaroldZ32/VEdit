#pragma once

// Split editor area of the application shell.
//
// The area owns a binary tree of editor *panes* (the product editor widget is
// supplied by the EditorProvider seam). It implements the shell's layout
// requirements: horizontal and vertical splits, focus switching, closing and
// re-layout, and a JSON layout model that the session store persists.
//
// T12: every pane hosts a TAB BAR of documents instead of exactly one file. The
// strip carries the file name, the per-tab dirty marker, the active-tab
// highlight, the per-tab close button and an overflow scroll button. It replaced
// the old "pane-1 · KTextEditor (edit-core) ◀ active" header line, which leaked
// the provider name and never told the user which file was being edited or
// whether it was saved.
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <Qt>

#include <functional>
#include <memory>

#include "session_store.h"

class QAction;
class QEvent;
class QTimer;
class QLabel;
class QMouseEvent;
class QTabBar;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace vedit::breadcrumb {
class Bar;
}

namespace vedit::app {

class EditorProvider;

// The answer to "this document has unsaved changes and is being closed". The
// pane asks, the shell prompts (it owns the modal dialog and the status bar).
//   * Save    — write the document through the provider, then close it,
//   * Discard — close without writing,
//   * Cancel  — keep the document open.
// The shell's prompt saves a non-active tab through saveTab() itself and answers
// Discard, so that path writes the file exactly once; a callback that does not
// save may answer Save and the pane writes it before closing.
enum class SaveDecision { Save, Discard, Cancel };

// One editor pane: a tab bar of documents and one editor widget per document
// (the provider creates them). The pane asks for activation on click and on
// focus-in of any editor widget.
class EditorPane final : public QWidget {
public:
    EditorPane(QString paneId, EditorProvider *provider, QWidget *parent = nullptr);
    ~EditorPane() override;

    QString paneId() const;
    // The ACTIVE tab's editor widget (a pane always has one tab).
    QWidget *editorWidget() const;
    // True when `widget` is one of this pane's editor widgets or a DESCENDANT of
    // one. The product editor (KTextEditor) hosts its real mouse target in a
    // nested widget (KatePart's KateViewInternal), so pane activation has to
    // recognise the whole editor subtree, not only the top-level view.
    bool containsEditorWidget(const QWidget *widget) const;
    void setActive(bool active);
    bool isActive() const;
    void setOnFocusRequested(std::function<void(EditorPane *)> callback);
    void setOnCloseRequested(std::function<void(EditorPane *)> callback);
    // T130: the pane's own close button must not discard an unsaved document.
    // True when ANY tab of this pane has unsaved changes.
    bool isModified() const;
    // T12: the ACTIVE tab's dirty state — what the window title, the status bar
    // and the active tab's marker are painted from.
    bool isActiveTabModified() const;

    // Replaces this pane's editor widgets with ones created by `provider`,
    // keeping the pane, its tabs and the split layout. Documents with a file
    // path are re-opened from disk (the shell installs the real provider before
    // the user can open a file).
    void setProvider(EditorProvider *provider);
    EditorProvider *provider() const;
    // Opens `absolutePath` as a tab of THIS pane: an already open file is
    // focused, an untouched empty tab is reused, otherwise a new tab is added.
    bool openFile(const QString &absolutePath, QString *error = nullptr);
    QString currentFilePath() const;
    bool saveFile(QString *error = nullptr);
    // AC-1.4: Save As for the ACTIVE tab. The document is written to
    // `absolutePath` through the provider's own save seam (atomic, and the one
    // place that clears the modified flag), then THIS tab adopts the new path:
    // the tab label, currentFilePath()/tabFilePath() and therefore the session
    // archive all report the new file. A refused save restores the previous path,
    // so a document never claims a file that was not written. Returns false with
    // *error filled; the caller owns the dialog and the overwrite question.
    bool saveAsFile(const QString &absolutePath, QString *error = nullptr);

    // --- T12: document tabs -------------------------------------------------
    int tabCount() const;
    int activeTabIndex() const;
    void setActiveTab(int index);
    QString tabFilePath(int index) const;
    QStringList openFilePaths() const;
    // The editor widget of one tab (nullptr for a bad index). The diagnostics
    // lane subscribes to EVERY open document's change signal, not only the
    // active tab's widget.
    QWidget *tabEditorWidget(int index) const;
    // The rendered tab label (dirty marker included); empty for a bad index.
    QString tabText(int index) const;
    bool isTabModified(int index) const;
    int tabIndexForPath(const QString &absolutePath) const;
    // Writes one tab's document (the active-tab save is saveFile()). Used by
    // the shell's Save/Discard/Cancel prompt so a non-active dirty tab can be
    // saved without activating it.
    bool saveTab(int index, QString *error = nullptr);
    // Closes one tab. A dirty tab is offered to the close-decision callback
    // first; without a callback a dirty tab is never discarded. The last tab of
    // a pane is replaced by a fresh empty tab, so a pane always has one.
    bool closeTab(int index);

    // UX batch 2 (item 7): File > New — a fresh empty document tab. An
    // untouched "untitled" tab is reused (New twice must not stack blank tabs);
    // a dirty or already-opened document gets a new tab next to it.
    void newDocument();

    // UX batch 2 (item 7): the shell's undo/redo bridge. The editor widget owns
    // the real action (KTextEditor registers its standard actions on an internal
    // child with an objectName such as "edit_undo"), so it is looked up by name
    // in the widget subtree; a plain-text provider without such an action falls
    // back to invoking the widget's own method ("undo"/"redo"). This keeps the
    // shell free of a KTextEditor include.
    bool triggerEditorAction(const QStringList &objectNames, const QString &fallbackMethod);
    // The enabled state the shell mirrors on its command: the found action's own
    // state; for the method fallback, true while an editor widget exists.
    bool editorActionEnabled(const QStringList &objectNames) const;
    // Test/inspection seam: the pane's tab bar widget.
    QTabBar *tabBar() const;
    // Drops every provider subscription this pane holds. The AREA calls it
    // before destroying a pane while the provider is still alive; the destructor
    // deliberately does not (the provider can already be gone when a window's
    // child widgets are torn down, and a destroyed provider must never be used).
    void releaseListeners();

    // --- session-restore seam (T19) ----------------------------------------
    // Captures every tab of this pane in the archive's document schema: the file
    // path plus the caret and vertical scroll offset of its editor. A tab that
    // has no file yet is not archived (it cannot be re-opened; unsaved content is
    // the close prompt's job, never the archive's). `activeTab` receives the
    // index of the ACTIVE tab inside the returned, path-only list, or -1 when the
    // active tab is an unarchived (untitled) one.
    QList<SessionDocument> documentStates(int *activeTab = nullptr) const;
    // Re-opens the archived documents of this pane in order and then activates
    // the saved tab. A file that no longer exists is reported in `diagnostics`
    // and skipped (the pane keeps whatever it had). `applyPositions` is false
    // when the archive was written by a different provider, so a caret line can
    // never be replayed into an editor that numbers its lines differently.
    // Returns the number of documents actually opened.
    int restoreDocuments(const QList<SessionDocument> &documents, int activeTab,
                         bool applyPositions, QStringList *diagnostics);

    // The shell installs both: the decision callback for closing a dirty tab,
    // and a state callback so the title/status bar follow tab switches and
    // dirty-flag changes.
    void setOnTabCloseRequested(std::function<SaveDecision(EditorPane *, int)> callback);
    void setOnStateChanged(std::function<void()> callback);
    // FIX 2: the pane's half of the shell's ONE lane-agnostic "a document was
    // opened" notification. EditorPane::openFile() is the open seam every
    // surface uses (File > Open, CLI, Files tree, Ctrl+P, search jump, project
    // double click), so this fires for all of them.
    void setOnFileOpened(std::function<void(const QString &absolutePath)> callback);

    // UX batch 2 (R3): the ONE notification every SUCCESSFUL write goes through.
    // saveTab() (Ctrl+S, Save All, the close prompt) and saveAsFile() are the
    // only two ways a document reaches disk, so this is the single save seam the
    // save-triggered project lint hangs on. It fires AFTER the bytes are on
    // disk and after the tab was refreshed: a listener may only schedule work,
    // never delay the save. A FAILED save fires nothing. `saveAs` is true when
    // the document adopted a new path (Save As / first save of an untitled tab).
    void setOnDocumentSaved(
        std::function<void(const QString &absolutePath, bool saveAs)> callback);

    // FIX 1: the project root the tab tooltips render against (empty = no
    // project, absolute tooltips). The shell keeps it in sync with its own root;
    // the pane only displays, it never opens by it.
    void setDisplayRoot(const QString &root);

    // T94/T20: the pane breadcrumb's current text (empty while hidden). With
    // the breadcrumb lane present this is the clickable Bar's segments joined
    // with " > " (the same spelling as breadcrumb::Model::display()), so the
    // T94 self-check assertions keep their meaning.
    QString breadcrumbText() const;

    // T90/F-2: re-apply the single-authority invariant for Ctrl+S on every
    // editor widget this pane owns (an editor may re-inject its own standard
    // save shortcut at runtime). Returns the number of duplicate bindings
    // removed.
    int enforceSingleCtrlSAuthority();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    // One open document: the provider's editor widget plus where it came from.
    struct Document;

    void addTabForDocument(QWidget *editor, const QString &absolutePath = QString());
    void removeTab(int index);
    void replaceTabEditor(int index, QWidget *editor);
    void wireEditor(QWidget *editor);
    void unwireEditor(EditorProvider *provider, QWidget *editor);
    void syncCurrentEditor();
    void refreshTab(int index);
    void refreshAllTabs();
    void updateActiveBadge();
    void updateBreadcrumb();
    void applyTabStyle();
    void notifyStateChanged();
    QWidget *createEditorWidget();
    QWidget *fallbackEditor();
    Document *documentAt(int index);
    const Document *documentAt(int index) const;
    QString documentLabel(const Document &document) const;
    void requestFocus();
    // T19: applies an archived caret/scroll exactly once, and only while the
    // editor is actually shown — a hidden stacked page has no final viewport
    // geometry, so a scroll offset applied there would clamp to the top. Called
    // from the editor's Show event (every tab switch) and at restore time.
    void applyPendingPosition(QWidget *editor);

    QString paneId_;
    EditorProvider *provider_ = nullptr;
    QTabBar *tabs_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QLabel *activeBadge_ = nullptr;
    // T20/A-3: the real clickable breadcrumb (breadcrumb lane) when the build
    // has it; `breadcrumb_` is the plain-label fallback for a tree without it.
    vedit::breadcrumb::Bar *breadcrumbBar_ = nullptr;
    QLabel *breadcrumb_ = nullptr;
    QToolButton *closeButton_ = nullptr;
    // The active tab's editor widget (kept in sync with the stack; every
    // provider consumer — jump targets, diagnostics, search — reads it through
    // editorWidget()).
    QWidget *editor_ = nullptr;
    QVBoxLayout *layout_ = nullptr;
    QList<Document *> documents_;
    std::function<void(EditorPane *)> onFocusRequested_;
    std::function<void(EditorPane *)> onCloseRequested_;
    std::function<SaveDecision(EditorPane *, int)> onTabCloseRequested_;
    std::function<void()> onStateChanged_;
    std::function<void(const QString &)> onFileOpened_;
    std::function<void(const QString &, bool)> onDocumentSaved_;
    // FIX 1: the root the tab tooltip is rendered against; the OPEN path stays
    // absolute (Document::path), only the rendering consults this.
    QString displayRoot_;
    // The tab strip's stylesheet is repainted from the palette on every theme
    // change. Both guards exist because setStyleSheet() itself sends a
    // StyleChange event: without them changeEvent -> applyTabStyle ->
    // setStyleSheet -> changeEvent would recurse until the stack dies.
    QString tabStyleSheet_;
    bool applyingTabStyle_ = false;
    bool active_ = false;
    // T19: archived positions of restored tabs, keyed by editor widget. An entry
    // is consumed the first time its editor is shown (see applyPendingPosition).
    QHash<QWidget *, SessionDocument> pendingPositions_;
};

// The split container. Invariants:
//   * at least one pane exists while the area is visible (ensurePane());
//   * every structural change rebuilds the QSplitter tree from the layout model
//     so the model is the single source of truth (and the session JSON).
class EditorArea final : public QWidget {
public:
    explicit EditorArea(QWidget *parent = nullptr);
    ~EditorArea() override;

    void setProvider(EditorProvider *provider);
    EditorProvider *provider() const;

    EditorPane *ensurePane();
    QList<EditorPane *> panes() const;
    int paneCount() const;
    EditorPane *activePane() const;
    void setActivePane(EditorPane *pane);

    // Splits the active pane. Qt::Horizontal puts the new pane to the right,
    // Qt::Vertical puts it below; the new pane becomes active (returned).
    EditorPane *splitActive(Qt::Orientation orientation);
    bool closeActivePane();
    // T130: close a specific pane (the per-pane X button); returns false when
    // the close was blocked (unsaved modifications).
    bool closePane(EditorPane *pane);
    void focusNextPane();
    void resetLayout();

    // UX batch 2 (item 7): File > New and the toolbar "New" open an empty
    // document in the ACTIVE pane (see EditorPane::newDocument()).
    void newDocument();
    // UX batch 2 (item 7): undo/redo on the active pane's editor (see
    // EditorPane::triggerEditorAction()). False when there is no active editor
    // or the editor has no such action, so a dead button is never pretended.
    bool triggerActiveEditorAction(const QStringList &objectNames,
                                   const QString &fallbackMethod);
    // The state the shell mirrors on its Undo/Redo commands; false while there
    // is no active editor widget (the "no active view => greyed out" rule).
    bool activeEditorActionEnabled(const QStringList &objectNames) const;

    QJsonObject saveLayout() const;
    // Replaces the current tree. Returns false (with *error) only for a
    // malformed/unsupported document; provider mismatches are diagnostics.
    // Archive schema v2: every leaf carries its pane's tabs (file path, caret,
    // scroll) and the active tab, so a restart re-opens the same documents. A v1
    // leaf (no "documents") restores the tree with one empty tab per pane, which
    // is exactly what the v1 archive described.
    bool restoreLayout(const QJsonObject &layout, QString *error);
    QStringList diagnostics() const;

    void setOnLayoutChanged(std::function<void()> callback);
    void setOnActivePaneChanged(std::function<void(EditorPane *)> callback);
    // T130: returning false blocks the close (unsaved changes). The shell
    // installs this; no callback means "allow" for direct/unit use.
    void setOnClosePaneRequested(std::function<bool(EditorPane *)> callback);

    // T12: forwarded to every pane (current and future): the per-tab close
    // decision and the "something a user can see changed" notification that
    // drives the window title and the status bar.
    void setOnTabCloseRequested(std::function<SaveDecision(EditorPane *, int)> callback);
    void setOnStateChanged(std::function<void()> callback);
    // FIX 2: forwarded to every pane (current and future) — the area itself does
    // not decide anything about the opened path; the shell raises its one
    // lane-agnostic signal from it.
    void setOnFileOpened(std::function<void(const QString &absolutePath)> callback);
    // R3: forwarded to every pane (current and future), exactly like the open
    // notification above; the area itself decides nothing about a save.
    void setOnDocumentSaved(
        std::function<void(const QString &absolutePath, bool saveAs)> callback);
    // FIX 1: forwarded to every pane (current and future); a later pane starts
    // with the root the area holds.
    void setDisplayRoot(const QString &root);

    // T90/F-2: runtime guard for the T80 invariant. Editor/standard-action
    // re-application (KStandardShortcut re-apply, KDE system settings, a
    // provider that repopulates its action collection) can put a second Ctrl+S
    // binding back after the pane was created; this re-runs the enforcement on
    // every pane. Returns the number of duplicate bindings removed.
    int enforceSingleCtrlSAuthority();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Node;

    EditorPane *createPane(const QString &paneId = QString());
    Node *newLeaf(EditorPane *pane);
    Node *findLeaf(Node *node, const EditorPane *pane) const;
    Node *findParent(Node *node, const Node *child) const;
    std::unique_ptr<Node> *ownerSlot(Node *child);
    void collectPanes(const Node *node, QList<EditorPane *> *out) const;
    QJsonObject nodeToJson(const Node *node) const;
    Node *nodeFromJson(const QJsonObject &json, QList<EditorPane *> *created, QString *error);
    QWidget *buildWidget(Node *node);
    void rebuild();
    void clearTree();
    void deletePane(EditorPane *pane);
    // UX batch 2: picks the pane whose editor subtree contains `widget`.
    void activatePaneContaining(const QWidget *widget);

    QVBoxLayout *layout_ = nullptr;
    QWidget *content_ = nullptr;
    std::unique_ptr<Node> root_;
    EditorProvider *provider_ = nullptr;
    EditorPane *active_ = nullptr;
    int nextPaneNumber_ = 1;
    std::function<void()> onLayoutChanged_;
    std::function<void(EditorPane *)> onActivePaneChanged_;
    std::function<bool(EditorPane *)> onClosePaneRequested_;
    std::function<SaveDecision(EditorPane *, int)> onTabCloseRequested_;
    std::function<void()> onStateChanged_;
    std::function<void(const QString &)> onFileOpened_;
    std::function<void(const QString &, bool)> onDocumentSaved_;
    // FIX 1: applied to every pane at creation and on a root switch.
    QString displayRoot_;
    QStringList diagnostics_;
    bool rebuilding_ = false;
    QTimer *shortcutGuard_ = nullptr;
};

} // namespace vedit::app
