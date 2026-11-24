#include "workbench_interaction.h"

#include <QAbstractButton>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDockWidget>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPropertyAnimation>
#include <QStyleOption>
#include <QTabBar>
#include <QToolBar>

namespace vedit::app {
namespace {

class WorkbenchInteraction final : public QObject
{
    Q_DECLARE_TR_FUNCTIONS(WorkbenchInteraction)

public:
    explicit WorkbenchInteraction(QMainWindow *window) : QObject(window), window_(window)
    {
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                clearDrag();
                QWidget *handle = handleAt(watched, mouse->globalPosition().toPoint(), false);
                auto *dock = qobject_cast<QDockWidget *>(handle);
                if (handle != nullptr && (dock == nullptr ||
                    dock->features().testFlag(QDockWidget::DockWidgetMovable))) {
                    source_ = handle;
                    origin_ = mouse->globalPosition().toPoint();
                }
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (!(mouse->buttons() & Qt::LeftButton)) {
                clearDrag();
            } else if (source_ && !badge_ &&
                       (mouse->globalPosition().toPoint() - origin_).manhattanLength()
                           >= QApplication::startDragDistance()) {
                badge_ = showWorkbenchDragFeedback(window_, source_->windowTitle());
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
                clearDrag(true);
            }
            break;
        case QEvent::ApplicationDeactivate:
            clearDrag();
            break;
        case QEvent::KeyPress:
            if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                clearDrag();
            }
            break;
        case QEvent::ContextMenu: {
            auto *context = static_cast<QContextMenuEvent *>(event);
            const bool keyboard = context->reason() == QContextMenuEvent::Keyboard;
            auto *dock = qobject_cast<QDockWidget *>(handleAt(watched, context->globalPos(), keyboard));
            if (dock != nullptr) {
                showDockMenu(dock, keyboard ? keyboardAnchor(watched, dock) : context->globalPos());
                context->accept();
                return true;
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

private:
    QWidget *handleAt(QObject *watched, const QPoint &global, bool keyboard) const
    {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget == nullptr || qobject_cast<QAbstractButton *>(widget)) {
            return nullptr;
        }
        if (auto *bar = qobject_cast<QTabBar *>(widget); bar && bar->parentWidget() == window_) {
            const int tab = keyboard ? bar->currentIndex() : bar->tabAt(bar->mapFromGlobal(global));
            if (tab < 0) {
                return nullptr;
            }
            QDockWidget *match = nullptr;
            for (auto *dock : window_->findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
                if (!dock->isFloating() && dock->windowTitle() == bar->tabText(tab) &&
                    !window_->tabifiedDockWidgets(dock).isEmpty()) {
                    if (match != nullptr) {
                        return nullptr;
                    }
                    match = dock;
                }
            }
            return match;
        }
        if (auto *dock = qobject_cast<QDockWidget *>(widget); dock && dock->parentWidget() == window_) {
            QWidget *content = dock->widget();
            const QPoint point = dock->mapFromGlobal(global);
            if (keyboard || (dock->rect().contains(point) && content != nullptr &&
                             !content->geometry().contains(point))) {
                return dock;
            }
        }
        auto *toolbar = qobject_cast<QToolBar *>(widget);
        if (toolbar && toolbar->parentWidget() == window_ && toolbar->isMovable() && !keyboard) {
            QStyleOptionToolBar option;
            option.initFrom(toolbar);
            option.state.setFlag(QStyle::State_Horizontal, toolbar->orientation() == Qt::Horizontal);
            option.features = QStyleOptionToolBar::Movable;
            const QRect handle = toolbar->style()->subElementRect(QStyle::SE_ToolBarHandle, &option,
                                                                  toolbar);
            if (handle.contains(toolbar->mapFromGlobal(global))) {
                return toolbar;
            }
        }
        return nullptr;
    }

    static QPoint keyboardAnchor(QObject *watched, const QDockWidget *dock)
    {
        if (auto *bar = qobject_cast<QTabBar *>(watched)) {
            return bar->mapToGlobal(bar->tabRect(bar->currentIndex()).bottomLeft());
        }
        return dock->mapToGlobal(QPoint(0, dock->fontMetrics().height()));
    }

    void clearDrag(bool dropped = false)
    {
        source_.clear();
        finishWorkbenchDragFeedback(badge_, dropped);
        badge_.clear();
    }

    static void showDockMenu(QDockWidget *dock, const QPoint &global)
    {
        auto *menu = new QMenu(dock);
        menu->setObjectName(QStringLiteral("workbench-dock-context-menu"));
        menu->setAttribute(Qt::WA_DeleteOnClose);
        const bool floating = dock->isFloating();
        QAction *placement = menu->addAction(floating ? tr("Dock Panel") : tr("Float Panel"),
                                             dock, [dock, floating]() { dock->setFloating(!floating); });
        placement->setEnabled(floating ? dock->allowedAreas() != Qt::NoDockWidgetArea
                                       : dock->features().testFlag(QDockWidget::DockWidgetFloatable));
        QAction *hide = menu->addAction(tr("Hide Panel"), dock, &QWidget::hide);
        hide->setEnabled(dock->features().testFlag(QDockWidget::DockWidgetClosable));
        menu->popup(global);
    }

    QMainWindow *window_;
    QPointer<QWidget> source_;
    QPointer<QLabel> badge_;
    QPoint origin_;
};

} // namespace

QLabel *showWorkbenchDragFeedback(QWidget *window, const QString &title)
{
    const auto name = QStringLiteral("workbench-drag-feedback");
    delete window->findChild<QLabel *>(name, Qt::FindDirectChildrenOnly);
    auto *badge = new QLabel(WorkbenchInteraction::tr("Moving %1").arg(title), window);
    badge->setObjectName(name);
    badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    badge->setAttribute(Qt::WA_ShowWithoutActivating);
    badge->setTextFormat(Qt::PlainText);
    badge->setStyleSheet(QStringLiteral("background: palette(highlight); color: palette(highlighted-text);"
                                       " border-radius: 5px; padding: 5px 10px;"));
    badge->adjustSize();
    auto *shell = qobject_cast<QMainWindow *>(window);
    const auto *menu = shell != nullptr ? shell->menuWidget() : nullptr;
    badge->move(qMax(0, (window->width() - badge->width()) / 2), menu != nullptr ? menu->height() + 52 : 24);
    badge->raise();
    if (!qEnvironmentVariableIsSet("VEDIT_REDUCED_MOTION") &&
        badge->style()->styleHint(QStyle::SH_Widget_Animate, nullptr, badge) != 0) {
        auto *opacity = new QGraphicsOpacityEffect(badge);
        opacity->setOpacity(0.0);
        badge->setGraphicsEffect(opacity);
        auto *fade = new QPropertyAnimation(opacity, "opacity", opacity);
        fade->setDuration(160);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        fade->start();
    }
    badge->show();
    return badge;
}

void finishWorkbenchDragFeedback(QLabel *badge, bool dropped)
{
    if (badge == nullptr) {
        return;
    }
    auto *opacity = qobject_cast<QGraphicsOpacityEffect *>(badge->graphicsEffect());
    if (!dropped || opacity == nullptr) {
        delete badge;
        return;
    }
    auto *fade = opacity->findChild<QPropertyAnimation *>();
    fade->stop();
    fade->setDuration(140);
    fade->setStartValue(opacity->opacity());
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::InQuad);
    QObject::connect(fade, &QPropertyAnimation::finished, badge, &QObject::deleteLater);
    fade->start();
}

void installWorkbenchInteraction(QMainWindow *window)
{
    new WorkbenchInteraction(window);
}

} // namespace vedit::app
