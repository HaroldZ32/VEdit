#pragma once

// Menu / action / shortcut framework of the application shell.
//
// Every user-visible command is declared once as a CommandSpec. The shell turns
// the specs into QActions and menus; a shortcut store supplies the effective key
// sequence, so key bindings can be rebound without touching menu construction.
//
// The *storage point* for rebindings belongs to the settings lane (settings-ui):
// it implements IShortcutStore or wraps JsonShortcutStore. This task only
// defines the interface and a plain JSON default so the shell is usable now.
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QAction;
class QObject;

namespace vedit::app {

struct CommandSpec {
    QString id;                       // stable, e.g. "view.split.horizontal"
    QString text;                     // menu text
    QString menu;                     // menu id: "file" | "view" | "help" | anything
    QKeySequence defaultShortcut;     // empty => no default binding
    int order = 100;                  // sorting inside the menu
    bool checkable = false;
    bool separatorBefore = false;
};

// Rebind storage seam (settings-ui owns the real implementation/UI).
class IShortcutStore {
public:
    virtual ~IShortcutStore() = default;
    virtual QKeySequence shortcut(const QString &commandId, const QKeySequence &fallback) const = 0;
    virtual void setShortcut(const QString &commandId, const QKeySequence &sequence) = 0;
    virtual void clearShortcut(const QString &commandId) = 0;
    virtual bool save(QString *error) = 0;
    virtual QString location() const = 0;
};

// Default JSON store. Format:
//   {"version":1,"shortcuts":{"view.split.horizontal":"Ctrl+Shift+J", ...}}
// A malformed file is reported through *error and otherwise ignored (bad
// configuration must not prevent the app from starting).
class JsonShortcutStore final : public IShortcutStore {
public:
    explicit JsonShortcutStore(QString filePath);
    QKeySequence shortcut(const QString &commandId, const QKeySequence &fallback) const override;
    void setShortcut(const QString &commandId, const QKeySequence &sequence) override;
    void clearShortcut(const QString &commandId) override;
    bool save(QString *error) override;
    QString location() const override;
    void load(QString *error);
    QHash<QString, QString> all() const;

private:
    QString path_;
    QHash<QString, QString> shortcuts_;
};

class CommandRegistry {
public:
    explicit CommandRegistry(QObject *actionOwner);
    ~CommandRegistry();

    CommandRegistry(const CommandRegistry &) = delete;
    CommandRegistry &operator=(const CommandRegistry &) = delete;

    void setStore(IShortcutStore *store); // not owned
    IShortcutStore *store() const;
    QAction *add(CommandSpec spec);
    QAction *action(const QString &id) const;
    void setHandler(const QString &id, std::function<void()> handler);
    QList<QAction *> actionsInMenu(const QString &menu) const;
    QStringList menus() const; // order of first appearance
    QStringList ids() const;
    QString menuLabel(const QString &menu) const;
    bool separatorBefore(const QString &id) const;
    void setMenuLabel(const QString &menu, const QString &label);
    void refreshShortcuts();
    bool saveShortcuts(QString *error);

private:
    struct Entry {
        CommandSpec spec;
        QAction *action = nullptr;
        std::function<void()> handler;
    };

    QHash<QString, Entry> entries_;
    QStringList order_;
    QStringList menus_;
    QHash<QString, QString> menuLabels_;
    IShortcutStore *store_ = nullptr;
    QObject *actionOwner_ = nullptr;
};

} // namespace vedit::app
