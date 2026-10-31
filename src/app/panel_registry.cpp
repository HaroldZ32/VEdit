#include "panel_registry.h"

#include <QDockWidget>
#include <QFont>
#include <QLabel>
#include <QMainWindow>
#include <QStringLiteral>
#include <QVBoxLayout>
#include <QWidget>

namespace vedit::app {

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

    auto *empty = new QLabel(hint.isEmpty() ? QStringLiteral("Empty state: no project / no data.")
                                            : hint,
                             placeholder);
    empty->setWordWrap(true);
    empty->setObjectName(QStringLiteral("panel-empty-state-") + spec.id);
    layout->addWidget(empty);
    layout->addStretch(1);
    return placeholder;
}

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
    dock->setWidget(createContent(spec));
    window_->addDockWidget(spec.area, dock);
    docks_.insert(spec.id, dock);
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
