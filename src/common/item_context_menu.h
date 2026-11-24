#pragma once

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPersistentModelIndex>

#include <functional>
#include <utility>

namespace vedit::common {

// Item views deliver context events through their viewport. Keep the reason so
// the keyboard uses the current row while a mouse click on blank space stays blank.
class ItemContextMenu final : public QObject
{
public:
    using Populate = std::function<void(QMenu &, const QPersistentModelIndex &)>;

    ItemContextMenu(QAbstractItemView *view, Populate populate)
        : QObject(view), view_(view), populate_(std::move(populate))
    {
        view->installEventFilter(this);
        view->viewport()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::ContextMenu) {
            return false;
        }
        auto *context = static_cast<QContextMenuEvent *>(event);
        const bool keyboard = context->reason() == QContextMenuEvent::Keyboard;
        auto *viewport = view_->viewport();
        const QPoint global = context->globalPos();
        const QModelIndex index = keyboard ? view_->currentIndex()
                                           : view_->indexAt(viewport->mapFromGlobal(global));
        const QRect row = view_->visualRect(index);
        const QPoint anchor = keyboard
            ? viewport->mapToGlobal(row.isEmpty() ? viewport->rect().center() : row.bottomLeft())
            : global;
        auto *menu = new QMenu(view_->window());
        menu->setPalette(QApplication::palette());
        connect(view_, &QObject::destroyed, menu, &QObject::deleteLater);
        menu->setObjectName(view_->objectName() + QStringLiteral("-context-menu"));
        menu->setAttribute(Qt::WA_DeleteOnClose);
        populate_(*menu, QPersistentModelIndex(index));
        if (menu->isEmpty()) {
            delete menu;
            return false;
        }
        menu->popup(anchor);
        context->accept();
        return true;
    }

private:
    QAbstractItemView *view_;
    Populate populate_;
};

inline void addCopyAction(QMenu &menu, const QString &label, const QString &value)
{
    QAction *action = menu.addAction(label, &menu, [value]() { QApplication::clipboard()->setText(value); });
    action->setEnabled(!value.isEmpty());
}

} // namespace vedit::common
