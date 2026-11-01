#pragma once

// Settings panel (Slice 3 / AC-S3.1): theme, key bindings, conflict hints and
// the bad-configuration diagnostics of the Rust settings store.
//
// Ownership split: the store is `crates/vd-settings` (through the C ABI, see
// SettingsClient); this widget only *consumes* it — it applies a theme through
// ThemeManager and rebinds a command through CommandRegistry's IShortcutStore,
// then writes the effective values back to the Rust store as `theme` and
// `keymap.<command id>` entries. Nothing here opens a settings file directly.
#include "settings_client.h"

#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>

class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace vedit::app {
class CommandRegistry;
class ThemeManager;
}

namespace vedit::workspace::settings {

class SettingsPanel : public QWidget {
    Q_OBJECT

public:
    SettingsPanel(vedit::app::ThemeManager *themes, vedit::app::CommandRegistry *commands,
                  QString settingsPath, QWidget *parent = nullptr);
    ~SettingsPanel() override;

    SettingsPanel(const SettingsPanel &) = delete;
    SettingsPanel &operator=(const SettingsPanel &) = delete;

    // Re-reads the Rust store and re-applies its values. Safe to call again after
    // the file was edited by hand (that is the bad-config recovery path).
    void reload();
    // Processes events until no store request is in flight (tests/close).
    bool flush(int timeoutMs = 15000);

    // Programmatic editors; the widgets call exactly these.
    void setTheme(const QString &name);
    void setShortcut(const QString &commandId, const QKeySequence &sequence);
    void clearShortcut(const QString &commandId);
    void applyEditorShortcut();

    // Introspection for the panel and its selftest.
    QString currentTheme() const;
    QStringList conflicts() const; // "Ctrl+S: file.save, view.split.horizontal"
    int conflictCount() const;
    QStringList diagnostics() const; // store warnings + apply/save failures
    QString statusText() const;
    QString settingsPath() const { return client_->path(); }
    const QVector<Entry> &entries() const { return entries_; }

    QComboBox *themeCombo() const { return themeCombo_; }
    QTableWidget *keyTable() const { return keyTable_; }
    QKeySequenceEdit *shortcutEditor() const { return shortcutEditor_; }
    QPushButton *assignButton() const { return assignButton_; }
    QPushButton *clearButton() const { return clearButton_; }
    QPushButton *reloadButton() const { return reloadButton_; }
    QLabel *conflictLabel() const { return conflictLabel_; }
    QLabel *statusLabel() const { return statusLabel_; }
    QPlainTextEdit *diagnosticsView() const { return diagnosticsView_; }

signals:
    void themeChanged(const QString &name);
    void shortcutsChanged();

private:
    void buildUi();
    void refreshWidgets();
    void refreshThemeCombo();
    void refreshKeyTable();
    void refreshConflicts();
    void addDiagnostic(const QString &text);
    void persist();
    void applyThemeFromEntries();
    void applyKeymapFromEntries();
    QString selectedCommand() const;

    vedit::app::ThemeManager *themes_ = nullptr;
    vedit::app::CommandRegistry *commands_ = nullptr;
    std::unique_ptr<SettingsClient> client_;
    QVector<Entry> entries_;
    QStringList diagnostics_;

    QComboBox *themeCombo_ = nullptr;
    QTableWidget *keyTable_ = nullptr;
    QKeySequenceEdit *shortcutEditor_ = nullptr;
    QPushButton *assignButton_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QPushButton *reloadButton_ = nullptr;
    QLabel *conflictLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *diagnosticsLabel_ = nullptr;
    QPlainTextEdit *diagnosticsView_ = nullptr;
};

} // namespace vedit::workspace::settings
