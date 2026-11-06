#pragma once

// Settings panel (Slice 3 / AC-S3.1, item 10.2): the Preferences dialog's three
// pages — Shortcuts, Tools and FIM — on top of the SAME flat Rust store.
//
// Ownership split: the store is `crates/vd-settings` (through the C ABI, see
// SettingsClient); this widget only *consumes* it. It applies a theme through
// ThemeManager and rebinds a command through CommandRegistry's IShortcutStore,
// and it writes every page's values back to the Rust store as the keys declared
// in `settings_keys.h` (the Rust half is `vd_settings::schema::keys`). Nothing
// here opens a settings file directly.
//
// The FIM API key is the one value that is NOT a store value: `fim_api_key` next
// to `settings.toml`, owner-read/write only (0600), or the `VEDIT_FIM_API_KEY`
// environment variable, which wins. The field on the page is write-only — the
// key is never echoed, never written to the store and never put in a diagnostic.
#include "format_options.h"
#include "lint_options.h"
#include "settings_client.h"
#include "ui_scale.h"

#include <QHash>
#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QToolButton;

namespace vedit::app {
class CommandRegistry;
class ThemeManager;
}

namespace vedit::workspace::settings {

// Values of the Tools page. An empty path means "let the toolchain resolver
// decide"; the lists hold one entry per line, exactly as stored.
struct ToolValues {
    QString lspBackend; // keys::kLspBackendVerible / kLspBackendSlang
    QString lintEngine; // keys::kLintEngineSlang / kLintEngineVerible
    QString veribleLs;
    QString slang;
    QString slangServer;
    QString veribleFormat;
    QString veribleRules;
    QString includeDirs; // -I, one directory per line
    QString defines;     // -D, one define per line
};

// Values of the FIM page.
//
// There is deliberately NO key field: a value struct must not be able to carry
// the secret into the UI. The key is written through storeFimApiKey() (or typed
// by the user into the write-only field) and never read back.
struct FimValues {
    bool enabled = false;
    QString protocol; // keys::kFimProtocolOpenAi / kFimProtocolLlamaCpp
    QString endpoint;
    QString model;
    int timeoutMs = 5000;
    int maxTokens = 64;
    bool ghostText = false;
};

class SettingsPanel : public QWidget {
    Q_OBJECT

public:
    SettingsPanel(vedit::app::ThemeManager *themes, vedit::app::CommandRegistry *commands,
                  QString settingsPath, QWidget *parent = nullptr);
    ~SettingsPanel() override;

    SettingsPanel(const SettingsPanel &) = delete;
    SettingsPanel &operator=(const SettingsPanel &) = delete;

    // T132: when deferred (the modal dialog's mode), edits are applied live as a
    // PREVIEW but nothing is written to the store; commitPending() persists and
    // revertPending() restores the snapshot taken when deferred mode started.
    // The default (false) keeps the old immediate-persist behaviour.
    void setDeferredPersistence(bool deferred);
    bool deferredPersistence() const { return deferred_; }
    bool hasPendingChanges() const { return dirty_; }
    // Persists the preview (Rust store + shortcut mirror + the FIM key file) and
    // re-snapshots.
    void commitPending();
    // Discards the preview: restores the theme, the shortcuts and the page values
    // of the snapshot, and drops a key typed but not committed.
    void revertPending();

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
    // Sets every field of the Tools page and stores the change.
    void setToolValues(const ToolValues &values);
    // Sets every field of the FIM page and stores the change.
    void setFimValues(const FimValues &values);

    // Introspection for the panel and its selftest.
    QString currentTheme() const;
    QStringList conflicts() const; // "Ctrl+S: file.save, view.split.horizontal"
    int conflictCount() const;
    QStringList diagnostics() const; // store warnings + apply/save failures
    QString statusText() const;
    QString settingsPath() const { return client_->path(); }
    const QVector<Entry> &entries() const { return entries_; }
    ToolValues toolValues() const;
    FimValues fimValues() const; // never carries the API key

    // --- UX batch 2 / A4: appearance, format and lint pages ----------------
    // Appearance: `ui.font_point_size` (0 = follow the system font) applies
    // immediately; `ui.scale_percent` is written here and applied by the NEXT
    // start (main.cpp sets QT_SCALE_FACTOR before the QApplication exists).
    struct AppearanceValues {
        int fontPointSize = 0;
        int scalePercent = 100;
    };
    AppearanceValues appearanceValues() const;
    void setAppearanceValues(const AppearanceValues &values);
    // Applies `ui.font_point_size` to the whole application NOW and returns the
    // point size QApplication::font() reports afterwards (-1 without a QApplication).
    int applyFontPointSize(int pointSize);

    // Format: every knob of the table in `format_options.h`; absent/empty means
    // "verible's own default, send no flag". The panel also publishes the values
    // to the process-wide holder `formatVerilog` reads.
    FormatValues formatValues() const;
    void setFormatValues(const FormatValues &values);
    void restoreFormatDefaults();

    // Lint: BOTH halves are persisted (plan §0.1 R1); the page shows the half of
    // the CURRENT engine and greys out the knobs that engine does not have.
    LintValues lintValues() const;
    void setLintValues(const LintValues &values);
    QString lintBackend() const; // "verible" / "slang"
    // The knob widget (by store key for format, by knob for lint) so the page
    // and its self-test address the same object without touching page internals.
    QWidget *formatKnobWidget(const QString &key) const;
    QWidget *lintKnobWidget(LintKnob knob) const;
    // True when the knob's control is enabled right now.
    bool lintKnobEnabled(LintKnob knob) const;
    // The (translated) reason a lint knob is greyed out; empty when it is usable.
    QString lintKnobReason(LintKnob knob) const;
    // Stores the page's Format/Lint/Appearance values into `entries` and the
    // process-wide holders WITHOUT writing the store (used by the self-test and
    // by the "apply now" path). `persist()` does all of it on every widget edit.
    void publishFormatAndLint();

    // Tabs (item 10.2). The Tools page's form is the extension point the i18n
    // lane uses for the interface-language row (AC-10.1.1.1).
    QTabWidget *tabs() const { return tabs_; }
    QWidget *shortcutsPage() const { return shortcutsPage_; }
    QWidget *toolsPage() const { return toolsPage_; }
    QWidget *fimPage() const { return fimPage_; }
    QFormLayout *toolsForm() const { return toolsForm_; }

    // T18 / AC-4.1, AC-4.2: the interface-language row on the Tools page and the
    // ONE action that applies it. Choosing a value is a preview only; the store
    // is written after the unsaved-document guard passed, so a cancelled restart
    // writes nothing and the language really has not changed.
    QComboBox *languageCombo() const { return languageCombo_; }
    QPushButton *languageRestartButton() const { return languageRestartButton_; }
    QLabel *languageHint() const { return languageHint_; }
    QString languageSetting() const;                 // auto | en | zh_CN
    QString storedLanguage() const;                  // persisted value (auto when absent)
    QString languageStatus() const { return languageStatus_; }
    void setLanguageSetting(const QString &setting); // programmatic editor
    // Writes ONLY ui.language (the rest of the store is left as it is stored).
    bool storeLanguage(const QString &setting, QString *error = nullptr);
    void requestLanguageRestart();

    // FIM API key state. The key itself is never exposed to the UI.
    QString fimKeyFilePath() const;
    // "environment" | "user-file" | "insecure-file" | "none"
    QString fimKeySource() const;
    // True when the environment variable or a usable 0600 key file provides one.
    bool fimKeyStored() const;
    // Removes the user-level key file (the environment variable is out of our
    // hands and is reported as still present).
    void clearFimKey();
    // Writes the key to the owner-only (0600 from creation) user file. This is
    // the ONLY programmatic way in: it never puts the value into a widget, so no
    // code path can echo a secret into the UI.
    bool storeFimApiKey(const QString &key);

    QWidget *appearancePage() const { return appearancePage_; }
    QWidget *lintPage() const { return lintPage_; }
    QWidget *formatPage() const { return formatPage_; }
    QSpinBox *fontPointSizeSpin() const { return fontPointSizeSpin_; }
    QComboBox *scalePercentCombo() const { return scalePercentCombo_; }
    QLabel *scaleHintLabel() const { return scaleHint_; }
    QLabel *lintTransportHint() const { return lintTransportHint_; }
    QLabel *lintVeribleStatus() const { return lintVeribleStatus_; }
    QLabel *lintSlangStatus() const { return lintSlangStatus_; }
    QPushButton *formatRestoreButton() const { return formatRestoreButton_; }
    QToolButton *formatAdvancedButton() const { return formatAdvancedButton_; }
    QWidget *formatAdvancedContainer() const { return formatAdvancedContainer_; }

    QComboBox *themeCombo() const { return themeCombo_; }
    QTableWidget *keyTable() const { return keyTable_; }
    QKeySequenceEdit *shortcutEditor() const { return shortcutEditor_; }
    QPushButton *assignButton() const { return assignButton_; }
    QPushButton *clearButton() const { return clearButton_; }
    QPushButton *reloadButton() const { return reloadButton_; }
    QLabel *conflictLabel() const { return conflictLabel_; }
    QLabel *statusLabel() const { return statusLabel_; }
    QPlainTextEdit *diagnosticsView() const { return diagnosticsView_; }

    QComboBox *lspBackendCombo() const { return lspBackendCombo_; }
    QComboBox *lintEngineCombo() const { return lintEngineCombo_; }
    QLineEdit *veribleLsEdit() const { return veribleLsEdit_; }
    QLineEdit *slangEdit() const { return slangEdit_; }
    QLineEdit *slangServerEdit() const { return slangServerEdit_; }
    QLineEdit *veribleFormatEdit() const { return veribleFormatEdit_; }
    QLineEdit *veribleRulesEdit() const { return veribleRulesEdit_; }
    QPlainTextEdit *includeDirsEdit() const { return includeDirsEdit_; }
    QPlainTextEdit *definesEdit() const { return definesEdit_; }

    QCheckBox *fimEnabledCheck() const { return fimEnabledCheck_; }
    QComboBox *fimProtocolCombo() const { return fimProtocolCombo_; }
    QLineEdit *fimEndpointEdit() const { return fimEndpointEdit_; }
    QLineEdit *fimModelEdit() const { return fimModelEdit_; }
    QSpinBox *fimTimeoutSpin() const { return fimTimeoutSpin_; }
    QSpinBox *fimMaxTokensSpin() const { return fimMaxTokensSpin_; }
    QLineEdit *fimApiKeyEdit() const { return fimApiKeyEdit_; }
    QCheckBox *fimGhostCheck() const { return fimGhostCheck_; }
    QLabel *fimKeySourceLabel() const { return fimKeySourceLabel_; }
    QPushButton *fimKeySaveButton() const { return fimKeySaveButton_; }
    QPushButton *fimKeyClearButton() const { return fimKeyClearButton_; }

signals:
    void themeChanged(const QString &name);
    void shortcutsChanged();

private:
    void buildUi();
    QWidget *buildShortcutsPage();
    QWidget *buildToolsPage();
    QWidget *buildFimPage();
    void refreshWidgets();
    void refreshThemeCombo();
    void refreshKeyTable();
    void refreshConflicts();
    void refreshFimKeySource();
    void addDiagnostic(const QString &text);
    void persist();
    void persistFromWidgets();
    void applyThemeFromEntries();
    void applyKeymapFromEntries();
    void applyToolEntries();
    void applyFimEntries();
    // Appearance / Lint / Format pages (UX batch 2 / A4).
    QWidget *buildAppearancePage();
    QWidget *buildLintPage();
    QWidget *buildFormatPage();
    void applyAppearanceEntries();
    void applyLintEntries();
    void applyFormatEntries();
    void applyFormatWidgets(const FormatValues &values);
    void applyLintWidgets();     // shows the CURRENT engine's half of lintValues_
    void readLintWidgets(LintValues *values) const; // folds the widgets into the active half
    void writeAppearanceEntries(const AppearanceValues &values);
    // Re-derives the per-knob enabled state, tooltips and the "stored only" hint
    // from the CURRENT engine (plan §0.1 R1).
    void refreshLintKnobStates();
    // Restores the page from the last snapshot (deferred-mode cancel).
    void restoreAppearancePreview();
    void updateLanguageHint();
    void applyToolValues(const ToolValues &values);
    void applyFimValues(const FimValues &values);
    void writeToolEntries(const ToolValues &values);
    void writeFimEntries(const FimValues &values);
    // Writes the value typed into the write-only field to the key file; a no-op
    // when the field is empty. Returns false after adding a diagnostic.
    bool persistTypedFimKey();
    bool writeFimKeyFile(const QString &key);
    void snapshotOriginal();
    void restoreOriginalShortcuts();
    QString selectedCommand() const;

    vedit::app::ThemeManager *themes_ = nullptr;
    vedit::app::CommandRegistry *commands_ = nullptr;
    std::unique_ptr<SettingsClient> client_;
    QVector<Entry> entries_;
    // T18: the store contents as last READ/committed — the base for a
    // language-only write (never the deferred preview `entries_`).
    QVector<Entry> storedEntries_;
    QStringList diagnostics_;
    // T132 deferred-mode state.
    bool deferred_ = false;
    bool dirty_ = false;
    // True while page widgets are filled from the store: change handlers must
    // not write back what they are being told.
    bool updating_ = false;
    QVector<Entry> committedEntries_;
    QString originalTheme_;
    QHash<QString, QKeySequence> originalShortcuts_;

    QComboBox *themeCombo_ = nullptr;
    // UX batch 2 / A4 pages.
    QWidget *appearancePage_ = nullptr;
    QWidget *lintPage_ = nullptr;
    QWidget *formatPage_ = nullptr;
    QSpinBox *fontPointSizeSpin_ = nullptr;
    QComboBox *scalePercentCombo_ = nullptr;
    QLabel *scaleHint_ = nullptr;
    QGroupBox *lintVeribleGroup_ = nullptr;
    QGroupBox *lintSlangGroup_ = nullptr;
    QComboBox *lintRulesetCombo_ = nullptr;
    QLineEdit *lintRulesEdit_ = nullptr;
    QLineEdit *lintRulesConfigEdit_ = nullptr;
    QPlainTextEdit *lintWaiverFilesEdit_ = nullptr;
    QComboBox *lintAutofixCombo_ = nullptr;
    QPlainTextEdit *lintSlangWarningsEdit_ = nullptr;
    QLineEdit *lintSlangErrorLimitEdit_ = nullptr;
    QLabel *lintTransportHint_ = nullptr;
    QLabel *lintVeribleStatus_ = nullptr;
    QLabel *lintSlangStatus_ = nullptr;
    QHash<int, QWidget *> lintKnobWidgets_;
    LintValues lintValues_;               // BOTH halves, as read from the store
    LintBackend lintBackend_ = LintBackend::Slang; // which half the widgets show
    QHash<QString, QWidget *> formatKnobWidgets_;
    QWidget *formatAdvancedContainer_ = nullptr;
    QToolButton *formatAdvancedButton_ = nullptr;
    QPushButton *formatRestoreButton_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QWidget *shortcutsPage_ = nullptr;
    QWidget *toolsPage_ = nullptr;
    QWidget *fimPage_ = nullptr;
    QFormLayout *toolsForm_ = nullptr;
    QTableWidget *keyTable_ = nullptr;
    QKeySequenceEdit *shortcutEditor_ = nullptr;
    QPushButton *assignButton_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QPushButton *reloadButton_ = nullptr;
    QLabel *conflictLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *diagnosticsLabel_ = nullptr;
    QPlainTextEdit *diagnosticsView_ = nullptr;

    QComboBox *lspBackendCombo_ = nullptr;
    QComboBox *lintEngineCombo_ = nullptr;
    QComboBox *languageCombo_ = nullptr;
    QPushButton *languageRestartButton_ = nullptr;
    QLabel *languageHint_ = nullptr;
    QString languageStatus_;
    QLineEdit *veribleLsEdit_ = nullptr;
    QLineEdit *slangEdit_ = nullptr;
    QLineEdit *slangServerEdit_ = nullptr;
    QLineEdit *veribleFormatEdit_ = nullptr;
    QLineEdit *veribleRulesEdit_ = nullptr;
    QPlainTextEdit *includeDirsEdit_ = nullptr;
    QPlainTextEdit *definesEdit_ = nullptr;

    QCheckBox *fimEnabledCheck_ = nullptr;
    QComboBox *fimProtocolCombo_ = nullptr;
    QLineEdit *fimEndpointEdit_ = nullptr;
    QLineEdit *fimModelEdit_ = nullptr;
    QSpinBox *fimTimeoutSpin_ = nullptr;
    QSpinBox *fimMaxTokensSpin_ = nullptr;
    QLineEdit *fimApiKeyEdit_ = nullptr;
    QCheckBox *fimGhostCheck_ = nullptr;
    QLabel *fimKeySourceLabel_ = nullptr;
    QPushButton *fimKeySaveButton_ = nullptr;
    QPushButton *fimKeyClearButton_ = nullptr;
};

} // namespace vedit::workspace::settings
