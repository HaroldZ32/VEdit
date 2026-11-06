#include "command_registry.h"

#include "desktop_shortcuts.h"

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
#include <QCoreApplication>

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
            *error = QCoreApplication::translate("VEdit", "cannot read shortcut store %1").arg(path_);
        }
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "invalid shortcut store %1: %2").arg(path_, parseError.errorString());
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
            *error = QCoreApplication::translate("VEdit", "cannot create shortcut store directory %1").arg(directory);
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
            *error = QCoreApplication::translate("VEdit", "cannot write shortcut store %1").arg(path_);
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot commit shortcut store %1").arg(path_);
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
    Entry entry;
    entry.spec = spec;
    entry.action = action;
    const QList<QKeySequence> initial = effectiveFor(spec.id, entry);
    action->setShortcuts(initial);
    action->setShortcutVisibleInContextMenu(true);
    const QKeySequence shortcut = initial.isEmpty() ? QKeySequence() : initial.first();

    // U3-F1 (A2 review H1): a duplicate default is a product defect — Qt treats
    // the sequence as ambiguous and drops BOTH bindings, and Preferences shows
    // the same sequence for two commands. Warn at registration so the class can
    // never recur silently; the product selfcheck also asserts the table is
    // duplicate-free and prints the bindings.
    if (!shortcut.isEmpty()) {
        for (auto it = entries_.constBegin(); it != entries_.constEnd(); ++it) {
            if (it->action != nullptr && it->action->shortcut() == shortcut) {
                qWarning("CommandRegistry: duplicate shortcut '%s' for '%s' and '%s'",
                         qPrintable(shortcut.toString()), qPrintable(it.key()),
                         qPrintable(spec.id));
                break;
            }
        }
    }

    if (!menus_.contains(spec.menu)) {
        menus_.append(spec.menu);
    }
    order_.append(spec.id);
    const QString commandId = spec.id;
    entry.spec = std::move(spec);
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

QList<QKeySequence> CommandRegistry::effectiveFor(const QString &id, const Entry &entry) const
{
    QList<QKeySequence> sequences;
    const QKeySequence primary =
        store_ != nullptr ? store_->shortcut(id, entry.spec.defaultShortcut)
                          : entry.spec.defaultShortcut;
    if (!primary.isEmpty()) {
        sequences.append(primary);
    }
    // Extras are part of the DEFAULT set: a stored override replaces them.
    if (store_ == nullptr
        || store_->shortcut(id, entry.spec.defaultShortcut) == entry.spec.defaultShortcut) {
        for (const QKeySequence &extra : entry.spec.extraDefaultShortcuts) {
            if (!extra.isEmpty() && !sequences.contains(extra)) {
                sequences.append(extra);
            }
        }
    }
    return sequences;
}

QList<QKeySequence> CommandRegistry::effectiveShortcuts(const QString &id) const
{
    const auto it = entries_.constFind(id);
    if (it == entries_.constEnd() || it->action == nullptr) {
        return {};
    }
    return it->action->shortcuts();
}

bool CommandRegistry::addExtraDefaultShortcut(const QString &id, const QKeySequence &sequence)
{
    auto it = entries_.find(id);
    if (it == entries_.end() || sequence.isEmpty()) {
        return false;
    }
    if (!it->spec.extraDefaultShortcuts.contains(sequence)) {
        it->spec.extraDefaultShortcuts.append(sequence);
    }
    // Apply only while the user has not replaced the default set.
    it->action->setShortcuts(effectiveFor(id, it.value()));
    return true;
}

bool CommandRegistry::reassignDefaultShortcut(const QString &id, const QKeySequence &sequence)
{
    auto it = entries_.find(id);
    if (it == entries_.end() || sequence.isEmpty()) {
        return false;
    }
    it->spec.defaultShortcut = sequence;
    it->action->setShortcuts(effectiveFor(id, it.value()));
    return true;
}

void CommandRegistry::refreshShortcuts()
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        it->action->setShortcuts(effectiveFor(it.key(), it.value()));
    }
    // RM-5b: the shell re-applies its authority to the live editors here, so a
    // binding the user just changed is effective IMMEDIATELY (the editor's own
    // copy of a now-shell-owned sequence is removed before the next key press).
    if (shortcutApplier_) {
        shortcutApplier_();
    }
}

void CommandRegistry::setShortcutApplier(std::function<void()> applier)
{
    shortcutApplier_ = std::move(applier);
}

QKeySequence CommandRegistry::defaultShortcut(const QString &id) const
{
    const auto it = entries_.constFind(id);
    return it == entries_.constEnd() ? QKeySequence() : it->spec.defaultShortcut;
}

QKeySequence CommandRegistry::effectiveShortcut(const QString &id) const
{
    const auto it = entries_.constFind(id);
    return it == entries_.constEnd() || it->action == nullptr ? QKeySequence()
                                                              : it->action->shortcut();
}

bool CommandRegistry::hasUserOverride(const QString &id) const
{
    const auto it = entries_.constFind(id);
    if (it == entries_.constEnd() || store_ == nullptr) {
        return false;
    }
    return store_->shortcut(id, it->spec.defaultShortcut) != it->spec.defaultShortcut;
}

QStringList CommandRegistry::shortcutOwnerIds(const QKeySequence &sequence) const
{
    QStringList owners;
    if (sequence.isEmpty()) {
        return owners;
    }
    for (const QString &id : order_) {
        const auto it = entries_.constFind(id);
        if (it != entries_.constEnd() && it->action != nullptr
            && it->action->shortcuts().contains(sequence)) {
            owners.append(id);
        }
    }
    return owners;
}

void CommandRegistry::setForeignShortcutProvider(
    std::function<QList<ForeignShortcut>()> provider)
{
    foreignProvider_ = std::move(provider);
}

QList<ForeignShortcut> CommandRegistry::foreignShortcuts() const
{
    return foreignProvider_ ? foreignProvider_() : QList<ForeignShortcut>{};
}

QStringList CommandRegistry::foreignShortcutOwners(const QKeySequence &sequence) const
{
    QStringList owners;
    if (sequence.isEmpty()) {
        return owners;
    }
    const QString portable = sequence.toString(QKeySequence::PortableText);
    for (const ForeignShortcut &foreign : foreignShortcuts()) {
        if (foreign.sequence == portable) {
            owners.append(foreign.owner);
        }
    }
    return owners;
}

bool CommandRegistry::saveShortcuts(QString *error)
{
    if (store_ == nullptr) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "no shortcut store configured");
        }
        return false;
    }
    return store_->save(error);
}

QStringList CommandRegistry::shortcutBindings() const
{
    QStringList lines;
    for (const QString &id : order_) {
        const auto it = entries_.constFind(id);
        if (it == entries_.constEnd() || it->action == nullptr) {
            continue;
        }
        for (const QKeySequence &sequence : it->action->shortcuts()) {
            if (!sequence.isEmpty()) {
                lines.append(sequence.toString() + QLatin1Char('\t') + id);
            }
        }
    }
    lines.sort();
    return lines;
}

QStringList CommandRegistry::duplicateShortcuts() const
{
    QHash<QString, QStringList> byShortcut;
    for (const QString &line : shortcutBindings()) {
        const int tab = line.indexOf(QLatin1Char('\t'));
        if (tab <= 0) {
            continue;
        }
        byShortcut[line.left(tab)].append(line.mid(tab + 1));
    }
    QStringList duplicates;
    for (auto it = byShortcut.constBegin(); it != byShortcut.constEnd(); ++it) {
        if (it.value().size() < 2) {
            continue;
        }
        QStringList ids = it.value();
        ids.sort();
        duplicates.append(it.key() + QStringLiteral(": ") + ids.join(QStringLiteral(", ")));
    }
    duplicates.sort();
    return duplicates;
}

} // namespace vedit::app
