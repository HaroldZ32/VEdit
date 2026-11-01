#pragma once

// Product wiring of the folding and breadcrumb lanes (task T94).
//
// T19 delivered two lanes and said honestly that neither was wired into
// vedit-app; T29 added the closed-state (tree-sitter OFF) fold source from OUR
// own resources/syntax/vedit-systemverilog.xml. This class is the integration
// point the live editor needs:
//
//   * FOLDING: one FoldInjector per KTextEditor::View. A full rebuild runs only
//     for the three triggers the lane allows (FirstOpen when the file opens,
//     TreeSitterToggled when the runtime switch changes, Reparsed when a caller
//     reports a completed reparse). The key / cursor / scroll paths call the
//     injector's explicit no-ops, so typing never rebuilds folds (A2.1).
//   * NO BYPASS: the injector validates every range through fold_range.h before
//     `exec("fold")` (A1). Nothing here calls KTextEditor commands directly.
//   * BREADCRUMB: a Model fed by the syntax stack's structure query on cursor
//     moves (debounced after text edits, so typing does not reparse per key).
//     With the runtime switch OFF the source reports "no semantics" and the path
//     is EMPTY - no text-heuristic path is invented (T19's contract).
//
// The view owns this QObject (parent), so the binding dies with the pane's
// editor; the provider swaps the status listener through the EditorProvider seam
// and the pane renders breadcrumbDisplay().
#include "breadcrumb_model.h"
#include "fold_injector.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QTimer;

namespace KTextEditor {
class Document;
class View;
}

namespace vedit::folding {
class FoldSource;
class KsyntaxFoldSource;
class SyntaxFoldSource;
}

namespace vedit::breadcrumb {
class SyntaxBreadcrumbSource;
}

namespace vedit::editor {

class FoldBreadcrumbBinding final : public QObject {
    Q_OBJECT
public:
    explicit FoldBreadcrumbBinding(KTextEditor::View *view, QObject *parent = nullptr);
    ~FoldBreadcrumbBinding() override;

    FoldBreadcrumbBinding(const FoldBreadcrumbBinding &) = delete;
    FoldBreadcrumbBinding &operator=(const FoldBreadcrumbBinding &) = delete;

    // The binding the provider installed on this editor widget, or nullptr.
    static FoldBreadcrumbBinding *instanceFor(QWidget *editorWidget);

    // FirstOpen: the file was just loaded into this view (one full rebuild).
    void onDocumentOpened();

    // Release the registered folds BEFORE the document text is replaced. A
    // KTextEditor document that still carries fold ranges from the previous text
    // can crash on the next setText()/setCursorPosition() (observed as a
    // segfault in the search-jump selftest when a pane re-opened a second file),
    // so the open path unfolds first. Safe to call when nothing is registered.
    void releaseFolds();

    // TreeSitterToggled: flip the runtime switch and rebuild with the source the
    // new state selects (the injector unfolds its registry first, so closing the
    // switch really releases the folds).
    void setTreeSitterEnabled(bool enabled);
    bool treeSitterEnabled() const;

    // Reparsed: the caller (a reparse pipeline) reports the semantic update.
    void notifyReparsed();

    vedit::folding::FoldInjector *injector() { return &injector_; }
    const vedit::folding::FoldInjector *injector() const { return &injector_; }

    // The breadcrumb the pane bar renders ("m0 > inc") and the diagnostic form
    // the evidence prints ("module:m0 > function:inc"). Empty when no semantics.
    QString breadcrumbDisplay() const;
    QString breadcrumbDescribe() const;
    quint64 breadcrumbUpdates() const;
    quint64 breadcrumbChanges() const;
    bool semanticActive() const;

    // The pane registers a listener so a cursor move repaints the bar.
    void setStatusListener(std::function<void()> listener);

signals:
    void breadcrumbChanged(const QString &display);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void ensureSources();
    void rebuildFor(vedit::folding::RebuildTrigger trigger);
    void refreshBreadcrumb();
    QString definitionDirectory() const;
    QString definitionFileName() const;
    QString grammarId() const;

    KTextEditor::View *view_ = nullptr;
    vedit::folding::FoldInjector injector_;
    std::unique_ptr<vedit::folding::KsyntaxFoldSource> closedSource_;
    std::unique_ptr<vedit::folding::SyntaxFoldSource> treeSitterSource_;
    std::unique_ptr<vedit::breadcrumb::SyntaxBreadcrumbSource> breadcrumbSource_;
    vedit::breadcrumb::Model breadcrumbModel_;
    QTimer *breadcrumbDebounce_ = nullptr;
    std::function<void()> statusListener_;
    QString textCache_;
    bool sourcesReady_ = false;
    bool opened_ = false;
};

} // namespace vedit::editor
