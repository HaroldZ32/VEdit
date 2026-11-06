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
    // RM-5c (review M2): additional DEFAULT bindings for the same command, so a
    // VS Code user's alternative key works without a second menu entry —
    // Ctrl+Y for redo, Shift+Alt+F for format. They apply only while the user
    // has not overridden the command (an override replaces the whole set); the
    // Shortcuts page shows them after the primary and reset restores them.
    QList<QKeySequence> extraDefaultShortcuts;
    int order = 100;                  // sorting inside the menu
    bool checkable = false;
    bool separatorBefore = false;
};

// RM-5b: a shortcut owned by an action that is NOT a registry command — in the
// product that is the KTextEditor view's own action collection (Find, Replace,
// Save, Undo, …). Qt treats a sequence carried by two enabled actions as
// AMBIGUOUS and triggers NEITHER, so a registry default (or a user binding) that
// equals one of these would be a DEAD key. The shell feeds the live editor's
// owners in through setForeignShortcutProvider(), which lets the Shortcuts page
// name the owner instead of silently shipping a binding that cannot fire.
struct ForeignShortcut {
    QString sequence; // portable text, e.g. "Ctrl+F"
    QString owner;    // human label, e.g. "Find… (edit_find)"
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
    // Every non-empty effective shortcut of the registry, sorted, in the form
    // "<sequence>\t<command id>". The product selfcheck prints this as the
    // machine-readable bindings dump (the evidence for "no shared sequence").
    QStringList shortcutBindings() const;
    // Groups of command ids that share one non-empty effective shortcut, in the
    // form "<sequence>: <id>, <id>". A non-empty result is a product defect:
    // Qt treats a duplicated sequence as ambiguous and drops BOTH bindings.
    QStringList duplicateShortcuts() const;

    // --- RM-5b: effective binding + ownership introspection -----------------
    // The registered default (independent of any user override) — what
    // reset-to-default restores and what the Shortcuts page shows as "default".
    QKeySequence defaultShortcut(const QString &id) const;
    // What the QAction carries now (the PRIMARY binding a key press reaches).
    QKeySequence effectiveShortcut(const QString &id) const;
    // Every sequence the action carries right now (primary + extras), in order.
    QList<QKeySequence> effectiveShortcuts(const QString &id) const;
    // RM-5c: add an extra DEFAULT binding after registration (lane commands such
    // as diagnostics.format keep their pinned primary and gain the VS Code
    // alternative). Ignored for an unknown id or a user-overridden command.
    bool addExtraDefaultShortcut(const QString &id, const QKeySequence &sequence);

    // True when the store overrides the default for this command.
    bool hasUserOverride(const QString &id) const;
    // Registry commands whose EFFECTIVE binding is `sequence` (never empty).
    QStringList shortcutOwnerIds(const QKeySequence &sequence) const;
    // Live non-registry actions (the editor) that own `sequence`.
    QStringList foreignShortcutOwners(const QKeySequence &sequence) const;
    // Everything the foreign provider reports (empty when none is installed).
    QList<ForeignShortcut> foreignShortcuts() const;
    // The shell installs the provider (it owns the editor widgets); the registry
    // only asks it for the current owners.
    void setForeignShortcutProvider(std::function<QList<ForeignShortcut>()> provider);
    // Invoked at the END of refreshShortcuts(): the shell re-applies the
    // "shell-owned sequences win over the editor's copies" authority to the live
    // editors, so a user's binding takes effect immediately (no restart).
    void setShortcutApplier(std::function<void()> applier);

private:
    struct Entry {
        CommandSpec spec;
        QAction *action = nullptr;
        std::function<void()> handler;
    };

    // The full default set for `entry` given the store: a user override replaces
    // the primary AND drops the extras; otherwise primary + extras.
    QList<QKeySequence> effectiveFor(const QString &id, const Entry &entry) const;
    bool reassignDefaultShortcut(const QString &id, const QKeySequence &sequence);

    QHash<QString, Entry> entries_;
    QStringList order_;
    QStringList menus_;
    QHash<QString, QString> menuLabels_;
    IShortcutStore *store_ = nullptr;
    QObject *actionOwner_ = nullptr;
    std::function<QList<ForeignShortcut>()> foreignProvider_;
    std::function<void()> shortcutApplier_;
};

} // namespace vedit::app
