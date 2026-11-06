#pragma once

// RM-5c (review M1): the key sequences a DESKTOP ENVIRONMENT claims globally.
//
// A window manager / desktop shell consumes its global shortcut BEFORE the
// application sees the event, so a shipped default bound to one of these is a
// DEAD key on that desktop no matter what our registry, the editor or the
// shortcut page say. The product must never ship one, and a user assignment to
// one must be refused with the desktop's name (the Shortcuts page consumes this
// list through the same "foreign owner" seam the editor actions use).
//
// The list is deliberately DATA, not logic: KDE and GNOME ship different values
// and a user may remap them, so the table names the well-known defaults of the
// two desktops this product targets (the user runs both, RM-6) and the X11
// virtual-terminal escape. Extending it is a one-line data change.
#include <QList>
#include <QKeySequence>
#include <QString>

namespace vedit::app {

struct DesktopShortcut {
    const char *sequence; // portable text, e.g. "Ctrl+Alt+T"
    const char *owner;    // what the desktop does with it, e.g. "GNOME/Ubuntu: open terminal"
};

// The well-known DE-global bindings in the modifier family this product uses.
// Sources: GNOME/Ubuntu and KDE Plasma default keybindings, plus the kernel's
// VT switching escape (Ctrl+Alt+F1..F7) and Ctrl+Alt+Del (logout).
const QList<DesktopShortcut> &desktopGlobalShortcuts();

// The desktop action that owns `sequence` (empty when nothing well-known does).
QString desktopGlobalShortcutOwner(const QKeySequence &sequence);

} // namespace vedit::app
