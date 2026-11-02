#include "command_registry.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStringLiteral>

#include <algorithm>

namespace vedit::app {

// ---------------------------------------------------------------------------
// JsonShortcutStore
// ---------------------------------------------------------------------------

JsonShortcutStore::JsonShortcutStore(QString filePath)
    : path_(std::move(filePath))
{
}

void JsonShortcutStore::load(QString *error)
{
    QFile file(path_);
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot read shortcut store %1").arg(path_);
        }
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid shortcut store %1: %2").arg(path_, parseError.errorString());
        }
        return;
    }
    const QJsonObject shortcuts = document.object().value(QStringLiteral("shortcuts")).toObject();
    for (auto it = shortcuts.constBegin(); it != shortcuts.constEnd(); ++it) {
        const QKeySequence sequence(it.value().toString());
        if (!sequence.isEmpty()) {
            shortcuts_.insert(it.key(), sequence.toString());
        }
    }
}

QKeySequence JsonShortcutStore::shortcut(const QString &commandId, const QKeySequence &fallback) const
{
    const QString stored = shortcuts_.value(commandId);
    if (stored.isEmpty()) {
        return fallback;
    }
    const QKeySequence sequence(stored);
    return sequence.isEmpty() ? fallback : sequence;
}

void JsonShortcutStore::setShortcut(const QString &commandId, const QKeySequence &sequence)
{
    if (sequence.isEmpty()) {
        shortcuts_.remove(commandId);
        return;
    }
    shortcuts_.insert(commandId, sequence.toString());
}

void JsonShortcutStore::clearShortcut(const QString &commandId)
{
    shortcuts_.remove(commandId);
}

bool JsonShortcutStore::save(QString *error)
{
    const QString directory = QFileInfo(path_).absolutePath();
    if (!directory.isEmpty() && !QDir().mkpath(directory)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create shortcut store directory %1").arg(directory);
        }
        return false;
    }
    QJsonObject shortcuts;
    for (auto it = shortcuts_.constBegin(); it != shortcuts_.constEnd(); ++it) {
        shortcuts.insert(it.key(), it.value());
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("shortcuts"), shortcuts);

    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write shortcut store %1").arg(path_);
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot commit shortcut store %1").arg(path_);
        }
        return false;
    }
    return true;
}

QString JsonShortcutStore::location() const
{
    return path_;
}

QHash<QString, QString> JsonShortcutStore::all() const
{
    return shortcuts_;
}

// ---------------------------------------------------------------------------
// CommandRegistry
// ---------------------------------------------------------------------------

CommandRegistry::CommandRegistry(QObject *actionOwner)
    : actionOwner_(actionOwner)
{
}

CommandRegistry::~CommandRegistry() = default;

void CommandRegistry::setStore(IShortcutStore *store)
{
    store_ = store;
    refreshShortcuts();
}

IShortcutStore *CommandRegistry::store() const
{
    return store_;
}

QAction *CommandRegistry::add(CommandSpec spec)
{
    if (spec.id.isEmpty() || entries_.contains(spec.id)) {
        return nullptr;
    }
    auto *action = new QAction(spec.text, actionOwner_);
    action->setObjectName(QStringLiteral("action-") + spec.id);
    action->setCheckable(spec.checkable);
    const QKeySequence shortcut =
        store_ != nullptr ? store_->shortcut(spec.id, spec.defaultShortcut) : spec.defaultShortcut;
    action->setShortcut(shortcut);
    action->setShortcutVisibleInContextMenu(true);

    if (!menus_.contains(spec.menu)) {
        menus_.append(spec.menu);
    }
    order_.append(spec.id);
    Entry entry;
    const QString commandId = spec.id;
    entry.spec = std::move(spec);
    entry.action = action;
    entries_.insert(commandId, entry);

    QObject::connect(action, &QAction::triggered, action, [this, commandId]() {
        const auto it = entries_.constFind(commandId);
        if (it != entries_.constEnd() && it->handler) {
            it->handler();
        }
    });
    return action;
}

QAction *CommandRegistry::action(const QString &id) const
{
    const auto it = entries_.constFind(id);
    return it == entries_.constEnd() ? nullptr : it->action;
}

void CommandRegistry::setHandler(const QString &id, std::function<void()> handler)
{
    auto it = entries_.find(id);
    if (it != entries_.end()) {
        it->handler = std::move(handler);
    }
}

QList<QAction *> CommandRegistry::actionsInMenu(const QString &menu) const
{
    QList<Entry> selected;
    for (const QString &id : order_) {
        const auto it = entries_.constFind(id);
        if (it != entries_.constEnd() && it->spec.menu == menu) {
            selected.append(it.value());
        }
    }
    std::sort(selected.begin(), selected.end(), [](const Entry &a, const Entry &b) {
        if (a.spec.order != b.spec.order) {
            return a.spec.order < b.spec.order;
        }
        return a.spec.id < b.spec.id;
    });
    QList<QAction *> result;
    for (const Entry &entry : selected) {
        result.append(entry.action);
    }
    return result;
}

QStringList CommandRegistry::menus() const
{
    return menus_;
}

QStringList CommandRegistry::ids() const
{
    return order_;
}

bool CommandRegistry::separatorBefore(const QString &id) const
{
    const auto it = entries_.constFind(id);
    return it != entries_.constEnd() && it->spec.separatorBefore;
}

QString CommandRegistry::menuLabel(const QString &menu) const
{
    return menuLabels_.value(menu, menu);
}

void CommandRegistry::setMenuLabel(const QString &menu, const QString &label)
{
    menuLabels_.insert(menu, label);
}

void CommandRegistry::refreshShortcuts()
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        const QKeySequence shortcut =
            store_ != nullptr ? store_->shortcut(it.key(), it->spec.defaultShortcut)
                              : it->spec.defaultShortcut;
        it->action->setShortcut(shortcut);
    }
}

bool CommandRegistry::saveShortcuts(QString *error)
{
    if (store_ == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("no shortcut store configured");
        }
        return false;
    }
    return store_->save(error);
}

} // namespace vedit::app
