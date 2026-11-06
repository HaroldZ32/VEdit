#include "editor_context_menu.h"
#include <QCoreApplication>

#include "lsp_actions.h"
#include "multi_cursor.h"

#include <KTextEditor/View>

#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QKeySequence>
#include <QMenu>
#include <QWidget>

namespace vedit::editor {
namespace {

constexpr auto kMenuObjectName = "vedit-editor-context-menu";

QString shortcutSuffix(const QKeySequence &shortcut)
{
    const QString text = shortcut.toString(QKeySequence::NativeText);
    return text.isEmpty() ? QString() : QStringLiteral("\t") + text;
}

/// What the user should expect from a successfully dispatched action. Used as
/// the item's tooltip when it is enabled, so the menu states the effect instead
/// of only the label.
QString effectText(EditorAction action, const QString &backend)
{
    const QString server =
        backend.isEmpty() ? QStringLiteral("the language server") : backend;
    switch (action) {
    case EditorAction::Definition:
        return QCoreApplication::translate("VEdit", "Ask %1 for the definition at the cursor and jump to it").arg(server);
    case EditorAction::References:
        return QCoreApplication::translate("VEdit", "Ask %1 for the references at the cursor and report their count")
            .arg(server);
    case EditorAction::Rename:
        return QCoreApplication::translate("VEdit", "Ask %1 for a rename preview of the symbol at the cursor")
            .arg(server);
    case EditorAction::Format:
        return QCoreApplication::translate("VEdit", 
            "Run the external verible-verilog-format over this document (one undoable edit)");
    case EditorAction::Hover:
        return QCoreApplication::translate("VEdit", 
            "Ask %1 for a hover at the cursor (no hover request path exists in this build)")
            .arg(server);
    case EditorAction::NextDiagnostic:
        return QCoreApplication::translate("VEdit", "Move the cursor to the next diagnostic of this file (F8)");
    case EditorAction::PreviousDiagnostic:
        return QCoreApplication::translate("VEdit", 
            "Move the cursor to the previous diagnostic of this file (Shift+F8)");
    }
    return QString();
}

ActionAvailability unavailable(const QString &reason)
{
    ActionAvailability state;
    state.enabled = false;
    state.known = false;
    state.source = QStringLiteral("none");
    state.reason = reason;
    return state;
}

QAction *appendItem(QMenu *menu, const KTextEditor::View *view, EditorAction action,
                    const QString &backend)
{
    LspActionChannel *channel = LspActionChannel::instanceFor(view);
    const ActionAvailability state =
        channel != nullptr
            ? channel->availability(action)
            : unavailable(QStringLiteral("the LSP action channel is not installed on this view"));

    const QAction *wired = shellActionFor(view->window(), action);
    QString text = wired != nullptr && !wired->text().isEmpty() ? wired->text()
                                                                : editorActionLabel(action);
    QKeySequence shortcut = wired != nullptr && !wired->shortcut().isEmpty()
                                ? wired->shortcut()
                                : editorActionShortcut(action);
    auto *item = new QAction(text + shortcutSuffix(shortcut), menu);
    item->setObjectName(QStringLiteral("vedit.action.") + editorActionName(action));
    item->setData(static_cast<int>(action));
    item->setEnabled(state.enabled);
    const QString tip = state.enabled ? effectText(action, backend) : state.reason;
    item->setToolTip(tip);
    item->setStatusTip(tip);
    if (channel != nullptr) {
        QObject::connect(item, &QAction::triggered, menu,
                         [channel, action]() { channel->dispatch(action); });
    }
    menu->addAction(item);
    return item;
}

} // namespace

EditorContextMenu::EditorContextMenu(KTextEditor::View *view)
    : QObject(view)
    , view_(view)
{
    setObjectName(QString::fromLatin1(kMenuObjectName));
}

EditorContextMenu *EditorContextMenu::install(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    if (EditorContextMenu *existing = instanceFor(view)) {
        return existing;
    }
    auto *menu = new EditorContextMenu(view);
    // KTextEditor's own context menu is gated behind a KatePart plugin container
    // that this product never creates, so the kernel catches the right-click
    // itself. The filter is installed on the widget tree because the real focus
    // (and the widget that receives the event) is KateViewInternal.
    installEditorEventFilter(view, menu);
    // A composition that DOES provide the KatePart shared menu emits this signal
    // about the menu it is going to show; appending there keeps the sections in
    // that menu too (it never fires when our filter consumes the event).
    QObject::connect(view, &KTextEditor::View::contextMenuAboutToShow, menu,
                     [menu](KTextEditor::View *, QMenu *popup) { menu->appendTo(popup); });
    return menu;
}

EditorContextMenu *EditorContextMenu::instanceFor(const KTextEditor::View *view)
{
    return view != nullptr ? view->findChild<EditorContextMenu *>(QString::fromLatin1(kMenuObjectName))
                           : nullptr;
}

QMenu *EditorContextMenu::buildMenu(QWidget *parent)
{
    if (view_ == nullptr) {
        return nullptr;
    }
    auto *menu = new QMenu(parent != nullptr ? parent : view_);
    menu->setObjectName(QStringLiteral("vedit-editor-context-menu-popup"));
    menu->setToolTipsVisible(true);
    // KTextEditor's documented default menu: undo/redo/cut/copy/paste/selection/
    // spelling/bookmarks, with the view's own actions (never a second copy).
    view_->defaultContextMenu(menu);
    appendTo(menu);
    lastMenu_ = menu;
    return menu;
}

QMenu *EditorContextMenu::showMenu(const QPoint &globalPos)
{
    QMenu *menu = buildMenu();
    if (menu == nullptr) {
        return nullptr;
    }
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(globalPos);
    return menu;
}

bool EditorContextMenu::eventFilter(QObject *watched, QEvent *event)
{
    if (view_ == nullptr) {
        return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::ChildAdded && isEditorOwnedWidget(view_, watched)) {
        auto *child = static_cast<QChildEvent *>(event)->child();
        if (child != nullptr && child->isWidgetType()) {
            installEditorEventFilter(view_, this);
        }
        return QObject::eventFilter(watched, event);
    }
    if (event->type() != QEvent::ContextMenu || !isEditorOwnedWidget(view_, watched)) {
        return QObject::eventFilter(watched, event);
    }
    auto *contextEvent = static_cast<QContextMenuEvent *>(event);
    // The same menu the product's own command registry offers, built from the
    // capability gate at this instant; an unavailable action stays visible and
    // greyed with its reason.
    if (showMenu(contextEvent->globalPos()) != nullptr) {
        event->accept();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

QList<QAction *> EditorContextMenu::appendTo(QMenu *menu)
{
    QList<QAction *> added;
    if (menu == nullptr || view_ == nullptr) {
        return added;
    }
    menu->setToolTipsVisible(true);
    const QString backend = publishedBackend(view_->window());

    menu->addSeparator();
    if (QAction *header = menu->addSection(QCoreApplication::translate("VEdit", "Language Server"))) {
        added.append(header);
    }
    // AC-3.1: the five LSP actions, in the order the capability table names them.
    const QList<EditorAction> lspActions{EditorAction::Definition, EditorAction::References,
                                         EditorAction::Rename, EditorAction::Format,
                                         EditorAction::Hover};
    for (EditorAction action : lspActions) {
        added.append(appendItem(menu, view_, action, backend));
    }

    menu->addSeparator();
    if (QAction *header = menu->addSection(QCoreApplication::translate("VEdit", "Diagnostics"))) {
        added.append(header);
    }
    added.append(appendItem(menu, view_, EditorAction::NextDiagnostic, backend));
    added.append(appendItem(menu, view_, EditorAction::PreviousDiagnostic, backend));

    appendedCount_ += added.size();
    return added;
}

} // namespace vedit::editor
