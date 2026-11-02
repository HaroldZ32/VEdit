#pragma once

// Modal Preferences dialog (T132). The dock panel was poor UX for settings (it
// competes with the editor and reads like an ordinary panel), so the same
// content now lives in a modal `Settings → Preferences…` dialog (Ctrl+,).
//
// Semantics (chosen and documented):
//   * edits are PREVIEWED immediately (theme, key bindings, conflict hints),
//   * nothing is written to the Rust store until Apply or OK,
//   * Cancel restores the snapshot taken when the dialog opened (theme +
//     shortcuts) and writes nothing,
//   * Apply stores the preview and keeps the dialog open; OK stores and closes.
// The Rust settings store stays the single source of truth: the dialog only
// drives SettingsPanel, which consumes the store through SettingsClient.
#include <QDialog>
#include <QString>

#include <memory>

class QDialogButtonBox;
class QPushButton;

namespace vedit::app {
class CommandRegistry;
class ThemeManager;
}

namespace vedit::workspace::settings {

class SettingsPanel;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(vedit::app::ThemeManager *themes, vedit::app::CommandRegistry *commands,
                   QString settingsPath, QWidget *parent = nullptr);
    ~SettingsDialog() override;

    SettingsPanel *panel() const { return panel_; }
    QDialogButtonBox *buttons() const { return buttons_; }

    // Apply: persist the preview, keep the dialog open.
    void applyPending();
    // Discard the preview (Cancel / close).
    void revertPending();
    bool hasPendingChanges() const;

    // QDialog entry points (buttons and Esc route here).
    void accept() override; // OK: persist + close
    void reject() override; // Cancel: discard + close

private:
    SettingsPanel *panel_ = nullptr;
    QDialogButtonBox *buttons_ = nullptr;
};

} // namespace vedit::workspace::settings
