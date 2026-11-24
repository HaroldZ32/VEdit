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
//
// Batch 20261110c adds one more page to the SAME tab widget: the Environment
// page (`EnvImportPage`), which imports variables from the user's shell. It is a
// separate file on purpose: the selection lives in `environment.json` next to
// `settings.toml` and is written through its own C ABI jobs, so the page keeps
// the dialog's "preview until Apply/OK" rule with its own draft instead of
// touching the settings store's key/value entries.
#include <QDialog>
#include <QMap>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <memory>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTableWidget;
class QTimer;

struct VdService;

namespace vedit::app {
class CommandRegistry;
class ThemeManager;
}

namespace vedit::workspace::settings {

class SettingsPanel;

// One row of the persisted environment selection. An empty `projectRoot` is the
// GLOBAL layer; any other value is the project root whose layer overrides the
// global one (`crates/vd-config`'s `environment.json` shape; the C ABI calls it
// `VdEnvSelectionEntry`).
struct EnvSelectionRow {
    QString projectRoot;
    QString name;
    QString value;

    bool operator==(const EnvSelectionRow &other) const = default;
};

// Preferences → Environment (batch 20261110c, AC-4.3/4.4 and AC-N5):
//
//   * ONE explicit capture. Pressing the button starts exactly one asynchronous
//     shell child through `vd_env_capture_start`; nothing runs on the GUI thread
//     and nothing runs twice.
//   * The table lists ONLY the variables the capture ADDED or CHANGED relative
//     to this process's own environment. Unchanged variables are not listed, and
//     the capture is never applied wholesale.
//   * No row is checked when the capture lands; the user checks what to import.
//   * "Import selected" stages the checked rows into a DRAFT (it never writes by
//     itself); the dialog's Apply/OK writes the whole `environment.json`
//     atomically through `vd_env_selection_save`, keeping every layer the user
//     did not touch. Cancel discards the draft.
//   * The scope selector defaults to the PROJECT layer (project overrides
//     global); the global layer is one click away.
//   * A hand-edited, broken `environment.json` degrades to an empty selection
//     plus a warning line — it never crashes the page (AC-4.4).
//
// Secret-shaped names (`*TOKEN*`, `*SECRET*`, `*PASSWORD*`, `*KEY*`, matched
// case-insensitively by substring, exactly `vd_config::secret::is_secret_name`)
// are displayed as `<masked>` by DEFAULT (AC-4.8). The value is only shown after
// an EXPLICIT reveal (`setSecretsRevealed` / the page's checkbox), and a
// secret-shaped row can only be staged/stored while it is revealed — matching
// `vd_config::secret`'s rule that the selection is stored as the user approved
// it while display and logs stay masked.
//
// The reveal is VIEW state on purpose: it is never written to a file (a new page
// always starts masked), and a captured value never reaches a status/diagnostic
// line — the capture's stderr tail is redacted against the capture's own
// secret-shaped values before it is rendered or reported.
class EnvImportPage final : public QWidget {
    Q_OBJECT

public:
    // `settingsPath` is the same `settings.toml` the panel gets; the selection
    // file is its sibling `environment.json`, exactly as the Rust engine's
    // `apply_environment_selection_file` documents it.
    explicit EnvImportPage(QString settingsPath, QWidget *parent = nullptr);
    ~EnvImportPage() override;

    EnvImportPage(const EnvImportPage &) = delete;
    EnvImportPage &operator=(const EnvImportPage &) = delete;

    // --- programmatic editors (the widgets call the same paths) -------------
    void setShellCode(int shellCode); // one of VD_ENV_SHELL_*
    int shellCode() const;
    void setShellPath(const QString &path);
    void setSourceFile(const QString &path);
    void setLoginShell(bool login);
    void setInteractive(bool interactive);
    void setProjectRoot(const QString &root);
    void setProjectScope(bool globalScope); // false (default) = project layer
    bool projectScope() const;              // true = global layer

    // --- batch-20261114 E2: the filelist-referenced candidate filter ----------
    // The user's rule: a variable the project's filelist never references must
    // not appear in the candidate list (a name blacklist — "hide PATH" — is
    // explicitly wrong). The project lane publishes the E1 referenced set through
    // the `vedit.project.inputs` shell property; the dialog hands that same map
    // here. ONCE a map with the two keys is set, the table lists ONLY those names
    // and the rest stay folded behind `showAllCandidates()` (OFF by default).
    // With no map set the page keeps its standalone behaviour (every added or
    // changed row): a page built without a project has no set to filter by.
    void setProjectInputs(const QVariantMap &inputs);
    void setReferencedVariables(const QStringList &names, const QStringList &layers);
    bool hasProjectContext() const { return projectContext_; }
    QStringList referencedVariables() const { return referencedVariables_; }
    // Rows the second filter currently hides (0 with no project context).
    int hiddenDiffRowCount() const;
    // The layer the project expansion resolved this row's name in:
    // "project" / "imported" / "environment" / "missing", or "" when no filelist
    // references the name (only visible while show-all is on).
    QString diffLayer(int row) const;
    bool showAllCandidates() const { return showAllCandidates_; }
    void setShowAllCandidates(bool showAll);
    QCheckBox *showAllCandidatesCheck() const { return showAllCandidatesCheck_; }
    // The one-line explanation under the candidate table ("" when the current
    // list needs none): why it is empty, or how many rows are folded away.
    QString candidateHint() const;

    // --- capture -----------------------------------------------------------
    // Starts ONE capture job and returns false (with a status line) when the
    // arguments were rejected or a request is already in flight. Completion is
    // asynchronous: poll `busy()` / `flush()`.
    bool startCapture();
    // AC-4.9: stops the capture that is in flight. False (with a status line)
    // when there is none. The page then reports "cancelled" when the job settles.
    bool cancelCapture();
    // Wall-clock budget of one capture. The product default is 15 s; the
    // self-test lowers it so the timeout path is exercised without a long wait.
    void setCaptureTimeoutMs(int timeoutMs);
    int captureTimeoutMs() const { return captureTimeoutMs_; }
    // AC-4.9: the last capture's exit status (-1 when there is no process result:
    // spawn failure, timeout, cancel) and the stderr TAIL the page shows — already
    // redacted against the capture's secret-shaped values, so it is safe to log.
    int captureExitCode() const { return captureExitCode_; }
    QString captureStderrTail() const { return captureStderrTail_; }
    int diffRowCount() const;
    QString diffName(int row) const;
    // "added" | "changed"
    QString diffKind(int row) const;
    // What the cell shows: the value, or `<masked>` for a secret-shaped name;
    // newlines are rendered as "\n" so a multi-line value stays one row.
    QString diffDisplayValue(int row) const;
    QString diffCapturedValue(int row) const; // raw (for the store, not the UI)
    bool diffChecked(int row) const;
    void setDiffChecked(int row, bool checked);

    // --- stored selection ---------------------------------------------------
    int selectionRowCount() const;
    QString selectionName(int row) const;
    QString selectionRoot(int row) const;  // empty = global layer
    QString selectionValue(int row) const; // raw, never painted
    QString selectionDisplayValue(int row) const;
    bool selectionRemoveChecked(int row) const;
    void setSelectionRemoveChecked(int row, bool checked);
    // Project layer wins over the global layer with the same name, exactly the
    // documented priority (`EnvironmentSelection::merged_for`).
    QString effectiveValue(const QString &name) const;

    // --- masking (AC-4.8) ---------------------------------------------------
    // The explicit expansion: true paints a secret-shaped value and lets that row
    // be staged. False (the default, and the state of every NEW page) paints
    // `<masked>`. This is view state: it is never persisted and never logged.
    void setSecretsRevealed(bool revealed);
    bool secretsRevealed() const { return secretsRevealed_; }

    // --- staging / persistence ---------------------------------------------
    // Stages every CHECKED diff row into the draft layer chosen by the scope
    // combo; a same-name row in that layer is REPLACED (that is the override),
    // never duplicated. False with a status line when nothing is checked.
    bool stageImport();
    // Stages removal of every checked stored row from the draft.
    bool removeSelectedStored();
    // Stages one NAME=VALUE row in the chosen layer; an existing name in that
    // layer is updated. The name is validated (non-empty, no '=', no control
    // characters) exactly like the C ABI store does, and a secret-shaped name is
    // refused while it is masked (AC-4.8).
    bool addVariable(const QString &name, const QString &value);
    bool hasPendingChanges() const;
    // Starts the atomic whole-file save of the draft; completion asynchronous.
    // Returns false (with a status line) when there is nothing to store.
    bool commitPending();
    // Drops the draft; the stored selection is left untouched.
    void discardPending();

    // --- state --------------------------------------------------------------
    bool busy() const;
    bool flush(int timeoutMs = 15000);
    void reload(); // re-reads environment.json (async)
    QString statusText() const;
    // Load warnings + capture warnings (paths and reasons only, never values).
    QStringList warnings() const;
    QString selectionPath() const { return selectionPath_; }
    // The capture's own report: shell, exit code, stderr tail, warnings. The
    // stderr tail is the REDACTED one (see captureStderrTail()).
    QString captureReport() const;

    // Widgets the automation addresses (never hidden behind page internals).
    QPushButton *captureButton() const { return captureButton_; }
    QPushButton *cancelButton() const { return cancelButton_; }
    QCheckBox *revealSecretsCheck() const { return revealSecrets_; }
    QPushButton *importButton() const { return importButton_; }
    QPushButton *removeButton() const { return removeButton_; }
    QPushButton *addButton() const { return addButton_; }
    QPushButton *reloadButton() const { return reloadButton_; }
    QComboBox *shellCombo() const { return shellCombo_; }
    QComboBox *scopeCombo() const { return scopeCombo_; }
    QLineEdit *shellPathEdit() const { return shellPathEdit_; }
    QLineEdit *sourceFileEdit() const { return sourceFileEdit_; }
    QLineEdit *projectRootEdit() const { return projectRootEdit_; }
    QLineEdit *nameEdit() const { return nameEdit_; }
    QLineEdit *valueEdit() const { return valueEdit_; }
    QCheckBox *loginCheck() const { return loginCheck_; }
    QCheckBox *interactiveCheck() const { return interactiveCheck_; }
    QTableWidget *diffTable() const { return diffTable_; }
    QTableWidget *selectionTable() const { return selectionTable_; }

    // The DEFAULT display rule, exported so the self-test asserts exactly the
    // text a masked table paints: `<masked>` for a secret-shaped name, "\n" for a
    // real newline. `displayValueFor()` (private) is the reveal-aware rule the
    // page itself paints with.
    static QString displayValue(const QString &name, const QString &value);
    static bool isSecretShapedName(const QString &name);

private:
    struct DiffRow {
        QString name;
        QString current;
        QString captured;
        bool added = false;
        bool checked = false;
    };

    void buildUi();
    void ensureTimer();
    void pollJobs();
    void refreshDiffTable();
    // Recomputes `visibleDiffRows_` from the second filter; called by every path
    // that can change the rows or the filter. A row the filter hides is
    // UNCHECKED, so a staged import can never carry a row the user cannot see.
    void applyCandidateFilter();
    QString candidateHintText() const;
    void refreshSelectionTable();
    void refreshStatus();
    void setStatus(const QString &text);
    void collectDiff(const QStringList &items);
    void sortSelection(QVector<EnvSelectionRow> *rows) const;
    QString draftLayerRoot() const;
    // What the CURRENT view paints: `<masked>` unless the row is revealed.
    QString displayValueFor(const QString &name, const QString &value) const;
    // Replaces the capture's own secret-shaped values inside text (the stderr
    // tail can echo them); used before ANY text leaves the page.
    QString redactKnownSecrets(const QString &text) const;
    void applyReveal(bool revealed);

    QString selectionPath_;
    VdService *service_ = nullptr;
    QTimer *timer_ = nullptr;

    unsigned long long captureJob_ = 0;
    unsigned long long loadJob_ = 0;
    unsigned long long saveJob_ = 0;

    QVector<DiffRow> diff_;
    // Indices into `diff_` the candidate table currently shows (E2). The public
    // row accessors are addressed by THIS order, so "the candidate list" always
    // means what the user sees; `diff_.size()` remains the unfiltered count.
    QVector<int> visibleDiffRows_;
    bool projectContext_ = false;
    QStringList referencedVariables_;
    QMap<QString, QString> referencedLayers_;
    bool showAllCandidates_ = false;
    QVector<EnvSelectionRow> selection_; // last successful load
    QVector<EnvSelectionRow> draft_;     // selection + staged changes
    QStringList warnings_;
    QString captureReport_;
    QString status_;
    // AC-4.8/4.9 state. `secretsRevealed_` is deliberately NOT serialized
    // anywhere: a new page starts masked, which is what "not persisted" means.
    bool secretsRevealed_ = false;
    int captureTimeoutMs_ = 15000;
    int captureExitCode_ = -1;
    QString captureStderrTail_;

    QComboBox *shellCombo_ = nullptr;
    QLineEdit *shellPathEdit_ = nullptr;
    QLineEdit *sourceFileEdit_ = nullptr;
    QCheckBox *loginCheck_ = nullptr;
    QCheckBox *interactiveCheck_ = nullptr;
    QLineEdit *projectRootEdit_ = nullptr;
    QComboBox *scopeCombo_ = nullptr;
    QPushButton *captureButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QLabel *captureReportLabel_ = nullptr;
    QTableWidget *diffTable_ = nullptr;
    QLabel *candidateHint_ = nullptr;
    QPushButton *importButton_ = nullptr;
    QCheckBox *showAllCandidatesCheck_ = nullptr;
    QCheckBox *revealSecrets_ = nullptr;
    QTableWidget *selectionTable_ = nullptr;
    QPushButton *removeButton_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *valueEdit_ = nullptr;
    QPushButton *addButton_ = nullptr;
    QPushButton *reloadButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *warningLabel_ = nullptr;
};

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    // `projectInputs` is the `vedit.project.inputs` snapshot the caller (the
    // Settings integration) took from the shell: it carries the E1 referenced set
    // the Environment page filters its candidate list by (E2). Empty = no project
    // context, and the page keeps its standalone behaviour.
    SettingsDialog(vedit::app::ThemeManager *themes, vedit::app::CommandRegistry *commands,
                   QString settingsPath, QWidget *parent = nullptr,
                   const QVariantMap &projectInputs = QVariantMap());
    ~SettingsDialog() override;

    SettingsPanel *panel() const { return panel_; }
    QDialogButtonBox *buttons() const { return buttons_; }
    // The content scroll area between the dialog edge and the action row
    // (batch 20261110d). Never null for a successfully constructed dialog.
    QScrollArea *contentScroll() const { return scroll_; }

    // The dialog's capped height for a screen's available geometry (batch
    // 20261110d): 90% of the available height, never above the preferred 760
    // and never below the 420 minimum. Exposed so the geometry test can check
    // BOTH 1366×768 and 1920×1080 without a real display.
    static constexpr int kPreferredWidth = 900;
    static constexpr int kPreferredHeight = 760;
    static constexpr int kMinimumHeight = 420;
    static int cappedHeightFor(const QSize &available);
    // The Environment page appended to the panel's own tab widget (batch
    // 20261110c). Never null for a successfully constructed dialog.
    EnvImportPage *environmentPage() const { return environmentPage_; }

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
    QScrollArea *scroll_ = nullptr;
    QDialogButtonBox *buttons_ = nullptr;
    EnvImportPage *environmentPage_ = nullptr;
};

} // namespace vedit::workspace::settings
