#include "desktop_shortcuts.h"

namespace vedit::app {

const QList<DesktopShortcut> &desktopGlobalShortcuts()
{
    static const QList<DesktopShortcut> shortcuts{
        // GNOME / Ubuntu defaults.
        {"Ctrl+Alt+T", "GNOME/Ubuntu: open terminal"},
        {"Ctrl+Alt+L", "GNOME/KDE: lock screen"},
        {"Ctrl+Alt+Del", "GNOME/KDE: log out / shutdown dialog"},
        {"Ctrl+Alt+Up", "GNOME/KDE: switch workspace up"},
        {"Ctrl+Alt+Down", "GNOME/KDE: switch workspace down"},
        {"Ctrl+Alt+Left", "GNOME/KDE: switch workspace left"},
        {"Ctrl+Alt+Right", "GNOME/KDE: switch workspace right"},
        // KDE Plasma defaults.
        {"Ctrl+Alt+D", "KDE: show desktop"},
        {"Ctrl+Alt+Esc", "KDE/X11: kill window"},
        {"Ctrl+Alt+Backspace", "X11: terminate the X server (legacy)"},
        // Virtual terminals: the kernel grabs Ctrl+Alt+F1..F7 before any desktop.
        {"Ctrl+Alt+F1", "Virtual terminal 1"},
        {"Ctrl+Alt+F2", "Virtual terminal 2"},
        {"Ctrl+Alt+F3", "Virtual terminal 3"},
        {"Ctrl+Alt+F4", "Virtual terminal 4"},
        {"Ctrl+Alt+F5", "Virtual terminal 5"},
        {"Ctrl+Alt+F6", "Virtual terminal 6"},
        {"Ctrl+Alt+F7", "Virtual terminal 7"},
    };
    return shortcuts;
}

QString desktopGlobalShortcutOwner(const QKeySequence &sequence)
{
    if (sequence.isEmpty()) {
        return {};
    }
    const QString portable = sequence.toString(QKeySequence::PortableText);
    for (const DesktopShortcut &shortcut : desktopGlobalShortcuts()) {
        if (portable == QLatin1String(shortcut.sequence)) {
            return QString::fromLatin1(shortcut.owner);
        }
    }
    return {};
}

} // namespace vedit::app
