#pragma once

// The editor's right-click menu (T15, AC-3.1).
//
// KTextEditor's OWN automatic context menu does not exist in this product: it is
// served by a KatePart plugin container (`ktexteditor_popup`), and vedit loads no
// KTextEditor plugin host - measured against the v6.24.0 source, where
// `ViewPrivate::contextMenu()` returns nullptr when no plugin client provides
// that container, so `KateViewInternal::contextMenuEvent` shows nothing. The
// kernel therefore owns the menu:
//
//   * the standard editing actions come from KTextEditor's documented
//     `View::defaultContextMenu(menu)` (undo/redo/cut/copy/paste/select all/...),
//   * two sections are appended from the LSP action channel (lsp_actions.*):
//     definition, references, rename, format, hover, plus diagnostics navigation
//     (F8 / Shift+F8). Every item carries the channel's decision: enabled only
//     when the backend capability table AND this build's request path AND the
//     wired shell action all allow it, with the reason as the item's tooltip - an
//     unavailable action is greyed WITH its reason, never silently missing.
//
// The menu is built on every right-click; no capability value is cached. When a
// composition DOES provide the KatePart shared menu, the object also appends to
// `View::contextMenuAboutToShow` (the documented extension point).
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

class LspActionChannel;

class EditorContextMenu final : public QObject
{
    Q_OBJECT
public:
    /// Install (idempotently) the menu on `view`; the object is a child of the
    /// view, so it dies with the pane. The event filter follows the kernel's key
    /// rule (the real focus is KateViewInternal), so a right-click anywhere in
    /// the editor reaches it.
    static EditorContextMenu *install(KTextEditor::View *view);
    static EditorContextMenu *instanceFor(const KTextEditor::View *view);

    /// Build the complete menu (KTextEditor's default actions + the channel's
    /// two sections). The CALLER owns the returned menu; nullptr when the view is
    /// gone.
    QMenu *buildMenu(QWidget *parent = nullptr);

    /// Build and show the menu at `globalPos`. The menu deletes itself when it
    /// closes. Returns the menu it showed.
    QMenu *showMenu(const QPoint &globalPos);

    /// Append only the channel's sections to `menu` (used by buildMenu and, in a
    /// plugin-hosted composition, by contextMenuAboutToShow).
    QList<QAction *> appendTo(QMenu *menu);

    /// The last menu this object built (evidence/inspection seam); null after it
    /// was closed and deleted.
    QMenu *lastMenu() const { return lastMenu_; }

    /// Items appended so far across all menus (probe observable).
    int appendedCount() const { return appendedCount_; }
    KTextEditor::View *view() const { return view_; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit EditorContextMenu(KTextEditor::View *view);

    KTextEditor::View *view_ = nullptr;
    QPointer<QMenu> lastMenu_;
    int appendedCount_ = 0;
};

} // namespace vedit::editor
