#pragma once

// In-file find / replace on the KTextEditor document.
//
// Probe constraint 2 (evidence/edit-core/00-probe.md §5): searchText() returns
// AT MOST ONE match per call, so matches are collected by iterating with the
// previous match's end() as the next start, and a zero-length match advances by
// one character. All replacements run bottom-up inside ONE EditingTransaction,
// so the whole replace-all is a single undo step.
//
// T13 extends the original pair with FindOptions (case / whole word / regular
// expression) and an optional scope range (the selection), and adds the in-view
// find/replace bar (`FindReplaceBar`) with Ctrl+F / Ctrl+H / F3 / Shift+F3.
// The two original signatures stay as thin wrappers because
// `edit_core_selftest.cpp` (outside this task's write scope) calls them.
//
// ux-batch-2 B2 aligns this bar with the project search dock: `FindOptions` IS
// the shared option model (src/workspace/search/search_query.h — case, whole
// word, regex, scope) and the bar gains the current-file / whole-project scope
// switch. Choosing "whole project" does NOT search the tree from inside one
// document: it publishes query + options through the shared SearchHub and the
// dock runs the project search, so both surfaces stay in sync in both
// directions.
#include "../../workspace/search/search_query.h"

#include <KTextEditor/Range>

#include <QFrame>
#include <QList>
#include <QMetaObject>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QKeyEvent;
class QToolButton;

namespace KTextEditor {
class Document;
class View;
}

namespace vedit::editor {

struct FindResult {
    bool ok = false;
    int matches = 0;
    QString error;
};

struct ReplaceResult {
    bool ok = false;
    int found = 0;
    int replaced = 0;
    QString error;
};

// What the search should honour. This is the SAME type the project search dock
// uses (the shared model of ux-batch-2 B2): case sensitive, whole word, regex
// and scope (current file | whole project). The defaults reproduce the old
// in-file behaviour (case-sensitive plain text, the current document).
using FindOptions = vedit::search::QueryOptions;

// All non-overlapping matches of `needle` in forward order (original API).
FindResult findMatches(KTextEditor::Document *document, const QString &needle,
                       bool caseSensitive = true, QList<KTextEditor::Range> *ranges = nullptr);

// Replaces every match with `replacement` inside one editing transaction
// (original API).
ReplaceResult replaceAll(KTextEditor::Document *document, const QString &needle,
                         const QString &replacement, bool caseSensitive = true);

// All non-overlapping matches honouring `options`; a non-null, valid `scope`
// restricts the search to that range (the editor selection). Whole-word
// filtering is applied by us on top of the kernel search, so it also works in
// regex mode (where the kernel only honours it for plain text).
FindResult findMatches(KTextEditor::Document *document, const QString &needle,
                       const FindOptions &options, const KTextEditor::Range *scope,
                       QList<KTextEditor::Range> *ranges);

// Replace every match inside `scope` (when non-null) in ONE transaction.
ReplaceResult replaceAll(KTextEditor::Document *document, const QString &needle,
                         const QString &replacement, const FindOptions &options,
                         const KTextEditor::Range *scope);

// Replace one already-known match in one transaction (the bar's "Replace").
ReplaceResult replaceOne(KTextEditor::Document *document, const KTextEditor::Range &match,
                         const QString &replacement);

// The in-view find/replace bar (T13, AC-4.3).
//
// One bar per KTextEditor::View, created by `install()` and driven by the
// view's shortcuts: Ctrl+F (find), Ctrl+H (replace), F3 / Shift+F3 (next /
// previous). It is a child of the view, so it dies with the pane. Every action
// is public so the lane selftest drives the SAME code path the shortcuts hit.
class FindReplaceBar final : public QFrame
{
    Q_OBJECT
public:
    // Creates (once) the bar for `view`, installs its shortcuts and returns it.
    static FindReplaceBar *install(KTextEditor::View *view);
    // The bar already installed on `editorWidget`, or nullptr.
    static FindReplaceBar *instanceFor(QWidget *editorWidget);

    ~FindReplaceBar() override;

    // Shortcut targets.
    void openFind();
    void openReplace();
    void findNext();
    void findPrevious();
    void replaceCurrent();
    void replaceAllMatches();
    void closeBar();

    // Observable state (the lane selftest asserts these; the status label is
    // rendered from the same values).
    QString pattern() const;
    QString replacement() const;
    QString statusText() const;
    int matchCount() const;
    int currentMatchIndex() const; // 0-based, -1 when nothing is current
    bool replaceRowVisible() const;
    bool inSelection() const;
    bool caseSensitive() const;
    bool wholeWords() const;
    bool regex() const;
    // The shared scope option: CurrentFile (this bar's in-file search) or
    // WholeProject (hand the query + options to the search dock).
    vedit::search::SearchScope scope() const;
    vedit::search::QueryOptions queryOptions() const;
    vedit::search::SearchHandoff handoff() const;
    KTextEditor::View *view() const;

    // Direct setters (used by the test, and mirrors typing/clicking).
    void setPattern(const QString &pattern);
    void setReplacement(const QString &text);
    void setCaseSensitive(bool enabled);
    void setWholeWords(bool enabled);
    void setRegex(bool enabled);
    void setScope(vedit::search::SearchScope scope);
    void setInSelection(bool enabled);
    // Adopt a hand-off from the other surface (widgets only; no re-publish).
    void applyHandoff(const vedit::search::SearchHandoff &handoff);

signals:
    // Emitted when the whole-project scope hands a query to the dock (the dock
    // is reached through the shared SearchHub; this is the observable seam).
    void projectSearchRequested(const vedit::search::SearchHandoff &handoff);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit FindReplaceBar(KTextEditor::View *view);

    void buildUi();
    void installKeyHandling();
    void clearKernelFindShortcuts();
    bool handleShortcutKey(QKeyEvent *event, bool overridePhase);
    void openBar(bool withReplace);
    void positionOverlay();
    void refreshDocumentConnection();
    void captureScopeAtOpen();
    void applySelectionToggle(bool checked);

    void recompute(bool selectCurrent);
    void goTo(int index);
    void updateStatus();
    FindOptions options() const;
    const KTextEditor::Range *selectionScope() const;
    int indexAtOrAfterCursor() const;
    int indexAtOrBeforeCursor() const;

    bool projectScope() const;
    void publishToHub();
    void adoptFromHub(const vedit::search::SearchHandoff &handoff);
    void setScopeWidgets(vedit::search::SearchScope scope);
    void requestProjectSearch();

    KTextEditor::View *view_ = nullptr;
    KTextEditor::Document *connectedDocument_ = nullptr;
    QMetaObject::Connection textConnection_;

    QLineEdit *findEdit_ = nullptr;
    QLineEdit *replaceEdit_ = nullptr;
    QWidget *replaceRow_ = nullptr;
    QToolButton *caseButton_ = nullptr;
    QToolButton *wholeButton_ = nullptr;
    QToolButton *regexButton_ = nullptr;
    QToolButton *selectionButton_ = nullptr;
    QComboBox *scopeCombo_ = nullptr;
    QLabel *status_ = nullptr;

    QList<KTextEditor::Range> ranges_;
    KTextEditor::Range scope_;
    KTextEditor::Range lastApplied_;
    int current_ = -1;
    bool hasScope_ = false;
    bool dirty_ = true;
    bool suppressRefresh_ = false;
    bool syncingButtons_ = false;
    bool syncingHub_ = false; // adopting a hub snapshot must not re-publish
    int hubSubscription_ = -1;
    QString error_;
    QString transient_;
};

} // namespace vedit::editor
