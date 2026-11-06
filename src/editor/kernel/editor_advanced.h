#pragma once

// The editor's advanced features (T16, AC-3.4).
//
// Five groups, each item resolved to ONE of two honest states - usable, or
// greyed WITH the measurement that makes it unavailable. Nothing is silently
// `undefined`:
//
//   group 1  LSP completion / signature / code action
//   group 2  sticky scroll
//   group 3  inlay hints
//   group 4  snippet
//   group 5  indent guides / whitespace / column ruler
//
// What this build can serve is decided from the product itself, never from a
// wish list:
//   * signatureHelp is synthESISED by the editor (signature_help.*, A-8) - the
//     backend has no signatureHelp at all, so it is the editor's own surface;
//   * keyword completion is KTextEditor's real view option
//     (`keyword-completion`, one of the measured `View::configKeys()`);
//   * snippets are a small built-in SystemVerilog template set in this file
//     (KTextEditor 6.24 exposes no snippet API);
//   * LSP completion / codeAction / inlayHint have no request in the C ABI;
//   * sticky scroll has no KTextEditor API at all;
//   * indent guides / whitespace / column ruler are KatePart *renderer* options
//     that `View::configKeys()` does not expose, so the product cannot toggle
//     them without private KatePart APIs.
//
// The controller is a child of the view. Ctrl+Alt+A opens the feature menu at
// the caret; the same menu is what the evidence probe inspects.
#include <QList>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QString>

class QAction;

namespace KTextEditor {
class View;
} // namespace KTextEditor

namespace vedit::editor {

enum class AdvancedFeature {
    LspCompletion = 0,
    KeywordCompletion,
    SignatureHelp,
    CodeAction,
    StickyScroll,
    InlayHints,
    Snippets,
    IndentGuides,
    Whitespace,
    ColumnRuler,
};

/// Stable lowercase id (menu object names / machine lines).
QString advancedFeatureName(AdvancedFeature feature);
/// The menu label.
QString advancedFeatureLabel(AdvancedFeature feature);
/// The group number+name from AC-3.4 ("1 lsp-actions", "2 sticky-scroll", ...).
QString advancedFeatureGroup(AdvancedFeature feature);

/// One feature's availability.
struct FeatureState {
    bool enabled = false;
    /// False when the answer depends on a backend that is not connected.
    bool known = true;
    /// `editor` / `backend` / `unavailable` / `abi` / `ktexteditor`.
    QString source;
    /// Why it is unavailable; never empty when `enabled` is false.
    QString reason;
};

class EditorAdvanced final : public QObject
{
    Q_OBJECT
public:
    static EditorAdvanced *install(KTextEditor::View *view);
    static EditorAdvanced *instanceFor(const KTextEditor::View *view);

    /// The AC-3.4 order: the five groups, their items in group order.
    QList<AdvancedFeature> features() const;
    FeatureState availability(AdvancedFeature feature) const;

    /// Apply one feature. A usable feature performs its effect and returns true;
    /// an unavailable one refuses, sets `lastRefusal()` and prints one
    /// `ADVANCED-REFUSED` line. Never a silent no-op.
    bool apply(AdvancedFeature feature);
    /// Insert one built-in snippet (`module`, `always_ff`, `case`) - the snippet
    /// group's concrete effect. False with a reason when the view has no document.
    bool insertSnippet(const QString &snippetId);

    /// The menu for the probe / future integrations. The caller owns it.
    QMenu *buildMenu(QWidget *parent = nullptr);
    /// Show the menu at `globalPos`; it deletes itself when closed.
    QMenu *showMenu(const QPoint &globalPos);

    int applyCount(AdvancedFeature feature) const;
    QString lastRefusal() const { return lastRefusal_; }
    KTextEditor::View *view() const { return view_; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit EditorAdvanced(KTextEditor::View *view);
    static int indexOf(AdvancedFeature feature);
    /// The measured `View::configKeys()` of this build (probe observable).
    QStringList configKeys() const;

    KTextEditor::View *view_ = nullptr;
    QPointer<QMenu> lastMenu_;
    QList<int> applied_;
    QString lastRefusal_;
};

} // namespace vedit::editor
