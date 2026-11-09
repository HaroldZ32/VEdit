#include "panel_registry.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QMainWindow>
#include <QPalette>
#include <QStringLiteral>
#include <QVBoxLayout>
#include <QWidget>

namespace vedit::app {

PanelRegistry::PanelRegistry() = default;

PanelRegistry::~PanelRegistry()
{
    // T129: the docks outlive this registry (they are QMainWindow children).
    // Drop the visibility connections before the listener/std::function is
    // destroyed, otherwise dock destruction calls into a dead registry.
    for (const QMetaObject::Connection &connection : visibilityConnections_) {
        QObject::disconnect(connection);
    }
}

namespace {

QWidget *makePlaceholder(const PanelSpec &spec, const QString &hint)
{
    auto *placeholder = new QWidget();
    placeholder->setObjectName(QStringLiteral("panel-placeholder-") + spec.id);
    auto *layout = new QVBoxLayout(placeholder);
    layout->setContentsMargins(10, 10, 10, 10);

    auto *title = new QLabel(QStringLiteral("%1").arg(spec.title), placeholder);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *seam = new QLabel(
        QStringLiteral("Content seam: %1 lane\nThis dock is the shell's placeholder; the lane replaces the\n"
                       "factory through PanelRegistry::setFactory(\"%2\", ...).")
            .arg(spec.owner, spec.id),
        placeholder);
    seam->setWordWrap(true);
    layout->addWidget(seam);

    auto *empty = new QLabel(hint.isEmpty() ? QCoreApplication::translate("VEdit", "Empty state: no project / no data.")
                                            : hint,
                             placeholder);
    empty->setWordWrap(true);
    empty->setObjectName(QStringLiteral("panel-empty-state-") + spec.id);
    layout->addWidget(empty);
    layout->addStretch(1);
    return placeholder;
}

// 卡片投影（UI-Mosaic V5/V6）：轻量、柔和——blur 12（浅色）/ 14（深色）、offset (0,1)。
// 第一轮 blur 8/10 实测纵向间隙 10px 达标，但上边只剩 1px、左边 2px 阴影（差 Δ≥3
// 的连续像素）；按复核给的修复方向把阴影带放宽到 blur 12/14，同时 separator 纵向
// 间隙提到 16px，让间隙中段保留 ≥6px 纯画布。
// 深色主题的画布接近黑色，同一条黑投影必须更浓才看得见（阴影带 Δ≥3 的要求），
// 所以透明度仍按画布亮度分两档：浅色 22/255、深色 220/255。
//
// V6：深浅必须读 QApplication::palette()。右栏的 Module Tree / Interface 两张 dock 在
// 产品主题应用之前创建，owner->palette() 会停留在浅色，阴影就永远停在 α22，在
// #131315 上不可见；而 dock 的 palette 也可能滞后于应用调色板。因此这里做成可
// 重复调用的 applyCardShadow，主题变化时由 CardShadowWatcher 重新应用。
void applyCardShadow(QWidget *owner)
{
    if (owner == nullptr) {
        return;
    }
    const bool light = QApplication::palette().color(QPalette::Window).lightness() > 128;
    // 深色主题的投影用稍大的 blur 与更高的 α：Δ≥3 的阴影带要求至少 3px，而
    // 在 #131315 这样的画布上，同一条黑边只有再浓一点、再铺开一点才够宽；
    // 浅色主题反过来，α 一大就会把间隙填灰。
    // 已存在的 effect 就地更新，避免重复叠加多个投影。
    auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(owner->graphicsEffect());
    if (shadow == nullptr) {
        shadow = new QGraphicsDropShadowEffect(owner);
        owner->setGraphicsEffect(shadow);
    }
    shadow->setBlurRadius(light ? 12 : 14);
    shadow->setOffset(0, 1);
    shadow->setColor(QColor(0, 0, 0, light ? 22 : 220));
}

// 轻量事件过滤器：parent 到 owner，随 owner 一起销毁（不泄漏、不轮询）。
// 应用调色板变化时按当前调色板重算阴影；PaletteChange 一并处理，覆盖 dock 自身
// palette 被 setPalette 更新的路径。
class CardShadowWatcher : public QObject
{
public:
    explicit CardShadowWatcher(QWidget *owner) : QObject(owner), owner_(owner) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == owner_
            && (event->type() == QEvent::ApplicationPaletteChange
                || event->type() == QEvent::PaletteChange)) {
            applyCardShadow(owner_);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget *owner_ = nullptr;
};

} // namespace

void PanelRegistry::addPanel(PanelSpec spec)
{
    const QString panelId = spec.id;
    if (panelId.isEmpty() || specs_.contains(panelId)) {
        return;
    }
    // QHash does not preserve insertion order, so the order is kept explicitly.
    order_.append(panelId);
    specs_.insert(panelId, std::move(spec));
}

bool PanelRegistry::contains(const QString &id) const
{
    return specs_.contains(id);
}

QString PanelRegistry::title(const QString &id) const
{
    const auto it = specs_.constFind(id);
    return it == specs_.constEnd() ? QString() : it->title;
}

QStringList PanelRegistry::ids() const
{
    QStringList result;
    for (const QString &id : order_) {
        result.append(id);
    }
    return result;
}

QStringList PanelRegistry::insertedIds() const
{
    QStringList result;
    for (const QString &id : order_) {
        if (docks_.contains(id)) {
            result.append(id);
        }
    }
    return result;
}

void PanelRegistry::setFactory(const QString &id, std::function<QWidget *()> factory)
{
    auto it = specs_.find(id);
    if (it != specs_.end()) {
        it->factory = std::move(factory);
    }
}

void PanelRegistry::setPlaceholderHint(const QString &id, const QString &hint)
{
    hints_.insert(id, hint);
}

void PanelRegistry::setVisibilityListener(
    std::function<void(const QString &id, bool visible)> listener)
{
    visibilityListener_ = std::move(listener);
}

QWidget *PanelRegistry::createContent(const PanelSpec &spec) const
{
    if (spec.factory) {
        if (QWidget *widget = spec.factory()) {
            return widget;
        }
    }
    return makePlaceholder(spec, hints_.value(spec.id));
}

QDockWidget *PanelRegistry::insert(const QString &id, QMainWindow *window)
{
    if (window != nullptr) {
        window_ = window;
        // UI-Mosaic V3: dock 布局会把面板贴到窗口四边，卡片就没有「浮在灰画布
        // 上」的留白。左右各留 16px：8px 是这个产品的最小留白（@gap@），另外
        // 8px 让 C 判据要求的 4~6px 阴影带也落在留白里，窗口最外圈 8px 仍是
        // 纯画布（F1）。上下不加——上面有菜单栏+工具栏、下面是状态栏，本来就是
        // 画布带，再缩只会把工具栏挤出窗口。
        window_->setContentsMargins(16, 0, 16, 0);
    }
    if (docks_.contains(id)) {
        return docks_.value(id);
    }
    const auto it = specs_.constFind(id);
    if (it == specs_.constEnd() || window_ == nullptr) {
        return nullptr;
    }
    const PanelSpec &spec = it.value();
    auto *dock = new QDockWidget(spec.title, window_);
    dock->setObjectName(QStringLiteral("dock-") + spec.id);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    QWidget *content = createContent(spec);
    // 卡片体：QSS 用这个属性给真实面板画描边与圆角（QDockWidget 自身的
    // border 在 Qt 6.10 上不绘制，`QDockWidget > QWidget` 又匹配不到真实
    // 面板的根控件——见 resources/themes/cards.qss 与 probe.py 的 before 对照）。
    content->setProperty("veditDockCard", true);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setContentsMargins(1, 0, 1, 1);
    dock->setWidget(content);
    applyCardShadow(dock);
    // 主题切换后重新按应用调色板算阴影（两张右栏 dock 在主题应用之前创建）。
    dock->installEventFilter(new CardShadowWatcher(dock));
    window_->addDockWidget(spec.area, dock);
    docks_.insert(spec.id, dock);
    // T129: a dock's own X button hides it without touching the menu action;
    // mirror every visibility change back to the shell's action.
    visibilityConnections_.append(
        QObject::connect(dock, &QDockWidget::visibilityChanged, dock,
                         [this, id](bool visible) {
                             if (visibilityListener_) {
                                 visibilityListener_(id, visible);
                             }
                         }));
    return dock;
}

QDockWidget *PanelRegistry::dock(const QString &id) const
{
    return docks_.value(id, nullptr);
}

bool PanelRegistry::isInserted(const QString &id) const
{
    return docks_.contains(id);
}

bool PanelRegistry::isVisible(const QString &id) const
{
    const QDockWidget *found = docks_.value(id, nullptr);
    return found != nullptr && found->isVisible();
}

void PanelRegistry::showPanel(const QString &id, QMainWindow *window)
{
    if (QDockWidget *panel = insert(id, window)) {
        panel->show();
        panel->raise();
    }
}

void PanelRegistry::hidePanel(const QString &id)
{
    if (QDockWidget *panel = docks_.value(id, nullptr)) {
        panel->hide();
    }
}

bool PanelRegistry::toggle(const QString &id, QMainWindow *window)
{
    QDockWidget *panel = insert(id, window);
    if (panel == nullptr) {
        return false;
    }
    if (panel->isVisible()) {
        panel->hide();
        return false;
    }
    panel->show();
    panel->raise();
    return true;
}

QList<QDockWidget *> PanelRegistry::docks() const
{
    QList<QDockWidget *> result;
    for (const QString &id : order_) {
        if (QDockWidget *panel = docks_.value(id, nullptr)) {
            result.append(panel);
        }
    }
    return result;
}

Qt::DockWidgetArea PanelRegistry::area(const QString &id) const
{
    const auto it = specs_.constFind(id);
    return it == specs_.constEnd() ? Qt::NoDockWidgetArea : it.value().area;
}

} // namespace vedit::app
