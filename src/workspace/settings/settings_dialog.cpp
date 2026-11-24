#include "settings_dialog.h"

#include "settings_panel.h"

#if defined(VEDIT_SETTINGS_HAVE_FFI)
#include "vd_ffi.h"
#endif

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QProcessEnvironment>
#include <QGuiApplication>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>

namespace vedit::workspace::settings {

namespace {

// One capture is a shell start-up; the engine's own default budget is 15 s.
constexpr unsigned long long kCaptureTimeoutMs = 15000;

constexpr int kDiffCheckColumn = 0;
constexpr int kDiffNameColumn = 1;
constexpr int kDiffCurrentColumn = 2;
// batch-20261114 E2: the layer the project expansion resolved the name in.
constexpr int kDiffLayerColumn = 3;
constexpr int kDiffCapturedColumn = 4;

// The four tokens the Rust expansion reports (E1), phrased for the one column
// that names them. An unknown token is shown verbatim rather than swallowed.
QString candidateLayerLabel(const QString &layer)
{
    if (layer == QLatin1String("project")) {
        return QCoreApplication::translate("VEdit", "Project");
    }
    if (layer == QLatin1String("imported")) {
        return QCoreApplication::translate("VEdit", "Imported");
    }
    if (layer == QLatin1String("environment")) {
        return QCoreApplication::translate("VEdit", "Environment");
    }
    if (layer == QLatin1String("missing")) {
        return QCoreApplication::translate("VEdit", "Missing");
    }
    return layer;
}

constexpr int kSelectionCheckColumn = 0;

#if defined(VEDIT_SETTINGS_HAVE_FFI)
QString lastServiceError(VdService *service)
{
    VdError error{};
    if (service != nullptr && vd_service_error(service, &error) == 1) {
        return QString::fromUtf8(error.message);
    }
    return {};
}

// AC-4.9: the KIND of failure decides the sentence the page shows (a user
// cancellation is not an error of the same class as a missing shell). 0 when the
// service recorded no error.
int lastServiceErrorCode(VdService *service)
{
    VdError error{};
    if (service != nullptr && vd_service_error(service, &error) == 1) {
        return error.code;
    }
    return 0;
}

QStringList ownedList(const VdStringList &list)
{
    QStringList values;
    for (std::size_t index = 0; index < list.count; ++index) {
        if (list.items != nullptr && list.items[index] != nullptr) {
            values.append(QString::fromUtf8(list.items[index]));
        }
    }
    return values;
}
#endif

// One value as one table cell: a real newline would break the row, so `\r` and
// `\n` are rendered as their two-character escapes. No masking decision here —
// callers choose between "masked by name" and "the current view's rule".
QString escapedValue(const QString &value)
{
    QString text = value;
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return text;
}

QString joinedSuffix(const QStringList &parts)
{
    return parts.isEmpty() ? QString() : QStringLiteral(" · ") + parts.join(QStringLiteral("; "));
}

QString firstLines(const QString &text, int count)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() <= count) {
        return lines.join(QStringLiteral(" "));
    }
    return lines.mid(0, count).join(QStringLiteral(" ")) + QStringLiteral(" …");
}

} // namespace

// --- Preferences → Environment page (batch 20261110c) ----------------------

EnvImportPage::EnvImportPage(QString settingsPath, QWidget *parent)
    : QWidget(parent), captureTimeoutMs_(static_cast<int>(kCaptureTimeoutMs))
{
    setObjectName(QStringLiteral("env-import-page"));
    QFileInfo storeInfo(settingsPath);
    QString directory = storeInfo.absolutePath();
    if (directory.isEmpty()) {
        directory = QDir::homePath() + QStringLiteral("/.config/vedit");
    }
    selectionPath_ = directory + QStringLiteral("/environment.json");
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    service_ = vd_service_create();
#endif
    buildUi();
    reload();
}

EnvImportPage::~EnvImportPage()
{
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    if (service_ != nullptr) {
        if (captureJob_ != 0) {
            vd_env_capture_cancel(service_, captureJob_);
        }
        captureJob_ = 0;
        loadJob_ = 0;
        saveJob_ = 0;
        vd_service_destroy(service_);
        service_ = nullptr;
    }
#endif
}

bool EnvImportPage::isSecretShapedName(const QString &name)
{
    // Exactly `vd_config::secret::is_secret_name`: substring, ASCII
    // case-insensitive, deliberately over-inclusive.
    const QString upper = name.toUpper();
    static const char *const markers[] = {"TOKEN", "SECRET", "PASSWORD", "KEY"};
    for (const char *marker : markers) {
        if (upper.contains(QLatin1String(marker))) {
            return true;
        }
    }
    return false;
}

QString EnvImportPage::displayValue(const QString &name, const QString &value)
{
    if (isSecretShapedName(name)) {
        return QStringLiteral("<masked>");
    }
    return escapedValue(value);
}

QString EnvImportPage::displayValueFor(const QString &name, const QString &value) const
{
    if (isSecretShapedName(name) && !secretsRevealed_) {
        return QStringLiteral("<masked>");
    }
    return escapedValue(value);
}

QString EnvImportPage::redactKnownSecrets(const QString &text) const
{
    QString redacted = text;
    // A value shorter than four characters is not redacted: replacing "1"
    // everywhere would mangle unrelated output while protecting nothing.
    const auto redactValue = [&redacted](const QString &value) {
        if (value.size() >= 4) {
            redacted.replace(value, QStringLiteral("<masked>"), Qt::CaseSensitive);
        }
    };
    for (const DiffRow &row : diff_) {
        if (isSecretShapedName(row.name)) {
            redactValue(row.captured);
        }
    }
    // An rc file can echo a secret-shaped variable that is NOT in the diff (it
    // is unchanged in this process) — those values are masked as well, plus the
    // secret-shaped rows the user already stores.
    const QProcessEnvironment ambient = QProcessEnvironment::systemEnvironment();
    for (const QString &name : ambient.keys()) {
        if (isSecretShapedName(name)) {
            redactValue(ambient.value(name));
        }
    }
    for (const EnvSelectionRow &row : selection_) {
        if (isSecretShapedName(row.name)) {
            redactValue(row.value);
        }
    }
    return redacted;
}

void EnvImportPage::applyReveal(bool revealed)
{
    secretsRevealed_ = revealed;
    if (revealSecrets_ != nullptr && revealSecrets_->isChecked() != revealed) {
        const QSignalBlocker blocker(revealSecrets_);
        revealSecrets_->setChecked(revealed);
    }
    refreshDiffTable();
    refreshSelectionTable();
}

void EnvImportPage::setSecretsRevealed(bool revealed)
{
    applyReveal(revealed);
}

void EnvImportPage::setCaptureTimeoutMs(int timeoutMs)
{
    captureTimeoutMs_ = timeoutMs > 0 ? timeoutMs : 0;
}

void EnvImportPage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *warning = new QLabel(
        tr("Capturing starts one child shell and runs its start-up files. It never changes "
           "this application's own environment and it never applies the whole environment — "
           "only the variables you select are stored."),
        this);
    warning->setObjectName(QStringLiteral("env-import-warning"));
    warning->setWordWrap(true);
    layout->addWidget(warning);

    auto *shellGroup = new QGroupBox(tr("Capture from your shell"), this);
    auto *shellForm = new QFormLayout(shellGroup);

    shellCombo_ = new QComboBox(shellGroup);
    shellCombo_->setObjectName(QStringLiteral("env-import-shell"));
    shellCombo_->addItem(QStringLiteral("bash"), 0);
    shellCombo_->addItem(QStringLiteral("zsh"), 1);
    shellCombo_->addItem(QStringLiteral("csh"), 2);
    shellCombo_->addItem(QStringLiteral("tcsh"), 3);
    shellCombo_->addItem(QStringLiteral("fish"), 4);
    shellCombo_->addItem(QStringLiteral("sh"), 5);
    shellCombo_->addItem(QStringLiteral("ksh"), 6);
    shellCombo_->addItem(tr("custom program"), 7);
    shellCombo_->setToolTip(tr("The shell family whose start-up chain is captured."));
    shellForm->addRow(tr("Shell:"), shellCombo_);

    shellPathEdit_ = new QLineEdit(shellGroup);
    shellPathEdit_->setObjectName(QStringLiteral("env-import-shell-path"));
    shellPathEdit_->setPlaceholderText(tr("(the shell family's default program)"));
    shellPathEdit_->setToolTip(
        tr("Optional explicit program (absolute, or a name on PATH). Required for a custom "
           "program."));
    shellForm->addRow(tr("Program:"), shellPathEdit_);

    sourceFileEdit_ = new QLineEdit(shellGroup);
    sourceFileEdit_->setObjectName(QStringLiteral("env-import-source-file"));
    sourceFileEdit_->setPlaceholderText(tr("(no rc file: only the shell's own environment)"));
    sourceFileEdit_->setToolTip(
        tr("An rc file to source before the environment is read. A broken rc file is "
           "reported with its exit status; the capture still returns the environment."));
    shellForm->addRow(tr("RC file:"), sourceFileEdit_);

    loginCheck_ = new QCheckBox(tr("Start a login shell (-l)"), shellGroup);
    loginCheck_->setObjectName(QStringLiteral("env-import-login"));
    interactiveCheck_ = new QCheckBox(tr("Start an interactive shell (-i)"), shellGroup);
    interactiveCheck_->setObjectName(QStringLiteral("env-import-interactive"));
    shellForm->addRow(loginCheck_);
    shellForm->addRow(interactiveCheck_);

    projectRootEdit_ = new QLineEdit(shellGroup);
    projectRootEdit_->setObjectName(QStringLiteral("env-import-project-root"));
    projectRootEdit_->setText(QDir::currentPath());
    projectRootEdit_->setToolTip(
        tr("The project root of the project layer. Project values override global values with "
           "the same name."));
    auto *rootRow = new QWidget(shellGroup);
    auto *rootLayout = new QHBoxLayout(rootRow);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(projectRootEdit_, 1);
    auto *currentFolder = new QPushButton(tr("Current folder"), rootRow);
    currentFolder->setObjectName(QStringLiteral("env-import-current-folder"));
    rootLayout->addWidget(currentFolder);
    shellForm->addRow(tr("Project root:"), rootRow);

    scopeCombo_ = new QComboBox(shellGroup);
    scopeCombo_->setObjectName(QStringLiteral("env-import-scope"));
    scopeCombo_->addItem(tr("Project (overrides global)"), false);
    scopeCombo_->addItem(tr("Global (all projects)"), true);
    scopeCombo_->setToolTip(
        tr("Where the selected variables are stored. The project layer is the default; a "
           "project value overrides a global value with the same name."));
    shellForm->addRow(tr("Store into:"), scopeCombo_);

    captureButton_ = new QPushButton(tr("Capture from shell"), shellGroup);
    captureButton_->setObjectName(QStringLiteral("env-import-capture"));
    shellForm->addRow(captureButton_);
    // AC-4.9: the user can stop the capture that is in flight; the page then
    // reports "cancelled" (never a silent skip, never a half-applied result).
    cancelButton_ = new QPushButton(tr("Cancel capture"), shellGroup);
    cancelButton_->setObjectName(QStringLiteral("env-import-cancel"));
    cancelButton_->setToolTip(
        tr("Stops the capture that is in flight. The environment is not applied and the list "
           "stays as it was."));
    cancelButton_->setEnabled(false);
    shellForm->addRow(cancelButton_);
    layout->addWidget(shellGroup);

    captureReportLabel_ = new QLabel(this);
    captureReportLabel_->setObjectName(QStringLiteral("env-import-capture-report"));
    captureReportLabel_->setWordWrap(true);
    QFont monospace(QStringLiteral("monospace"));
    captureReportLabel_->setFont(monospace);
    layout->addWidget(captureReportLabel_);

    auto *diffGroup = new QGroupBox(tr("Added or changed in your shell (nothing is selected)"),
                                    this);
    auto *diffLayout = new QVBoxLayout(diffGroup);
    diffTable_ = new QTableWidget(0, 5, diffGroup);
    diffTable_->setObjectName(QStringLiteral("env-import-diff"));
    diffTable_->setHorizontalHeaderLabels(
        {QString(), tr("Variable"), tr("Current"), tr("Provided by"), tr("From shell")});
    diffTable_->horizontalHeader()->setStretchLastSection(true);
    diffTable_->verticalHeader()->setVisible(false);
    diffTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    diffTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    diffTable_->setToolTip(
        tr("Only variables the capture ADDED or CHANGED and the project's filelist REFERENCES "
           "are listed; check the ones to import. A secret-shaped name is masked until you "
           "reveal it explicitly."));
    diffLayout->addWidget(diffTable_, 1);
    // E2: the one sentence that explains an empty (or partially folded) list. It
    // is hidden whenever the list needs no explanation.
    candidateHint_ = new QLabel(diffGroup);
    candidateHint_->setObjectName(QStringLiteral("env-import-candidates-hint"));
    candidateHint_->setWordWrap(true);
    candidateHint_->hide();
    diffLayout->addWidget(candidateHint_);
    importButton_ = new QPushButton(tr("Import selected"), diffGroup);
    importButton_->setObjectName(QStringLiteral("env-import-import"));
    importButton_->setToolTip(
        tr("Stages the checked variables into the selected layer; Apply or OK stores them."));
    diffLayout->addWidget(importButton_, 0, Qt::AlignLeft);
    // AC-4.8: the EXPLICIT expansion. Default off, reset by every new capture, and
    // never written to a file — a reopened page always starts masked again.
    revealSecrets_ = new QCheckBox(tr("Show secret-shaped values"), diffGroup);
    revealSecrets_->setObjectName(QStringLiteral("env-import-reveal-secrets"));
    revealSecrets_->setChecked(false);
    revealSecrets_->setToolTip(
        tr("Explicitly reveals a secret-shaped value for THIS view: the reveal is not "
           "remembered, never reaches a log, and a secret-shaped row can only be stored while "
           "it is revealed."));
    diffLayout->addWidget(revealSecrets_, 0, Qt::AlignLeft);
    // E2: "the rest stay folded" is a VISIBLE, explicit control, OFF by default —
    // never a hidden rule and never a name blacklist.
    showAllCandidatesCheck_ = new QCheckBox(tr("Show variables the project does not reference"),
                                            diffGroup);
    showAllCandidatesCheck_->setObjectName(QStringLiteral("env-import-show-all"));
    showAllCandidatesCheck_->setChecked(false);
    showAllCandidatesCheck_->setToolTip(
        tr("Off by default: only names the project's filelist references are offered. Turning "
           "this on lists every added or changed variable, still marking which ones the "
           "filelist does not use."));
    connect(showAllCandidatesCheck_, &QCheckBox::toggled, this,
            &EnvImportPage::setShowAllCandidates);
    diffLayout->addWidget(showAllCandidatesCheck_, 0, Qt::AlignLeft);
    layout->addWidget(diffGroup, 2);

    auto *storedGroup = new QGroupBox(tr("Stored selection"), this);
    auto *storedLayout = new QVBoxLayout(storedGroup);
    auto *storedHint = new QLabel(
        tr("Both layers stay in one file; importing never replaces the variables you did not "
           "select. Selection file: %1")
            .arg(selectionPath_),
        storedGroup);
    storedHint->setObjectName(QStringLiteral("env-import-stored-hint"));
    storedHint->setWordWrap(true);
    storedLayout->addWidget(storedHint);

    selectionTable_ = new QTableWidget(0, 4, storedGroup);
    selectionTable_->setObjectName(QStringLiteral("env-import-selection"));
    selectionTable_->setHorizontalHeaderLabels(
        {QString(), tr("Variable"), tr("Layer"), tr("Value")});
    selectionTable_->horizontalHeader()->setStretchLastSection(true);
    selectionTable_->verticalHeader()->setVisible(false);
    selectionTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    selectionTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    storedLayout->addWidget(selectionTable_, 1);

    auto *storedButtons = new QHBoxLayout();
    removeButton_ = new QPushButton(tr("Remove checked"), storedGroup);
    removeButton_->setObjectName(QStringLiteral("env-import-remove"));
    storedButtons->addWidget(removeButton_);
    reloadButton_ = new QPushButton(tr("Reload from disk"), storedGroup);
    reloadButton_->setObjectName(QStringLiteral("env-import-reload"));
    storedButtons->addWidget(reloadButton_);
    storedButtons->addStretch(1);
    storedLayout->addLayout(storedButtons);

    auto *addForm = new QHBoxLayout();
    nameEdit_ = new QLineEdit(storedGroup);
    nameEdit_->setObjectName(QStringLiteral("env-import-name"));
    nameEdit_->setPlaceholderText(tr("NAME"));
    addForm->addWidget(nameEdit_);
    valueEdit_ = new QLineEdit(storedGroup);
    valueEdit_->setObjectName(QStringLiteral("env-import-value"));
    valueEdit_->setPlaceholderText(tr("value"));
    addForm->addWidget(valueEdit_, 1);
    addButton_ = new QPushButton(tr("Add / update"), storedGroup);
    addButton_->setObjectName(QStringLiteral("env-import-add"));
    addForm->addWidget(addButton_);
    storedLayout->addLayout(addForm);
    layout->addWidget(storedGroup, 2);

    warningLabel_ = new QLabel(this);
    warningLabel_->setObjectName(QStringLiteral("env-import-warnings"));
    warningLabel_->setWordWrap(true);
    warningLabel_->setText(tr("No problems reported."));
    layout->addWidget(warningLabel_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("env-import-status"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    connect(captureButton_, &QPushButton::clicked, this, [this]() { startCapture(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this]() { cancelCapture(); });
    connect(revealSecrets_, &QCheckBox::toggled, this, [this](bool checked) {
        applyReveal(checked);
        setStatus(checked ? tr("Secret-shaped values are shown for this view only; the reveal is "
                               "not remembered and never reaches a log.")
                          : tr("Secret-shaped values are masked again."));
    });
    connect(importButton_, &QPushButton::clicked, this, [this]() { stageImport(); });
    connect(removeButton_, &QPushButton::clicked, this, [this]() { removeSelectedStored(); });
    connect(reloadButton_, &QPushButton::clicked, this, [this]() { reload(); });
    connect(addButton_, &QPushButton::clicked, this, [this]() {
        addVariable(nameEdit_->text(), valueEdit_->text());
    });
    connect(currentFolder, &QPushButton::clicked, this, [this]() {
        setProjectRoot(QDir::currentPath());
    });
    connect(scopeCombo_, &QComboBox::currentIndexChanged, this, [this]() { refreshStatus(); });
    refreshStatus();
}

// --- programmatic editors --------------------------------------------------

void EnvImportPage::setShellCode(int shellCode)
{
    const int index = shellCombo_->findData(shellCode);
    if (index >= 0) {
        shellCombo_->setCurrentIndex(index);
    }
}

int EnvImportPage::shellCode() const
{
    return shellCombo_->currentData().toInt();
}

void EnvImportPage::setShellPath(const QString &path)
{
    shellPathEdit_->setText(path);
}

void EnvImportPage::setSourceFile(const QString &path)
{
    sourceFileEdit_->setText(path);
}

void EnvImportPage::setLoginShell(bool login)
{
    loginCheck_->setChecked(login);
}

void EnvImportPage::setInteractive(bool interactive)
{
    interactiveCheck_->setChecked(interactive);
}

void EnvImportPage::setProjectRoot(const QString &root)
{
    projectRootEdit_->setText(root);
}

void EnvImportPage::setProjectScope(bool globalScope)
{
    scopeCombo_->setCurrentIndex(globalScope ? 1 : 0);
}

bool EnvImportPage::projectScope() const
{
    return scopeCombo_->currentData().toBool();
}

// --- capture ---------------------------------------------------------------

bool EnvImportPage::startCapture()
{
    if (busy()) {
        setStatus(tr("A request is already in flight; wait for it to finish."));
        return false;
    }
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    if (service_ == nullptr) {
        setStatus(tr("This build has no vd-ffi environment ABI."));
        return false;
    }
    const QByteArray program = shellPathEdit_->text().trimmed().toUtf8();
    const QByteArray source = sourceFileEdit_->text().trimmed().toUtf8();
    const QByteArray workdir = projectRootEdit_->text().trimmed().toUtf8();
    VdEnvCaptureOptions options{};
    options.shell = shellCode();
    options.shell_path = program.isEmpty() ? nullptr : program.constData();
    options.source_file = source.isEmpty() ? nullptr : source.constData();
    options.workdir = workdir.isEmpty() ? nullptr : workdir.constData();
    options.timeout_ms = static_cast<unsigned long long>(captureTimeoutMs_);
    options.login = loginCheck_->isChecked() ? 1 : 0;
    options.interactive = interactiveCheck_->isChecked() ? 1 : 0;
    const unsigned long long job = vd_env_capture_start(service_, &options);
    if (job == 0) {
        setStatus(tr("The capture was refused: %1").arg(lastServiceError(service_)));
        return false;
    }
    captureJob_ = job;
    // The cancel affordance must be usable the moment the capture is in flight,
    // not one poll later.
    if (cancelButton_ != nullptr) {
        cancelButton_->setEnabled(true);
    }
    // A new capture is new data: the reveal must be asked for again (AC-4.8) and
    // the previous capture's result is not allowed to look like this one's.
    applyReveal(false);
    captureExitCode_ = -1;
    captureStderrTail_.clear();
    warnings_.clear();
    captureReport_.clear();
    setStatus(tr("Capturing from the shell…"));
    ensureTimer();
    return true;
#else
    setStatus(tr("This build has no vd-ffi environment ABI."));
    return false;
#endif
}

bool EnvImportPage::cancelCapture()
{
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    if (service_ != nullptr && captureJob_ != 0) {
        if (vd_env_capture_cancel(service_, captureJob_) == 1) {
            setStatus(tr("Cancelling the capture…"));
            return true;
        }
    }
#endif
    setStatus(tr("No capture is running."));
    return false;
}

void EnvImportPage::collectDiff(const QStringList &items)
{
    diff_.clear();
    QMap<QString, QString> captured; // last export wins, exactly like a shell
    for (const QString &item : items) {
        const int equals = item.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            continue;
        }
        captured.insert(item.left(equals), item.mid(equals + 1));
    }
    const QProcessEnvironment process = QProcessEnvironment::systemEnvironment();
    QMapIterator<QString, QString> it(captured);
    while (it.hasNext()) {
        it.next();
        const QString &name = it.key();
        const bool present = process.contains(name);
        if (present && process.value(name) == it.value()) {
            continue; // unchanged: never listed
        }
        DiffRow row;
        row.name = name;
        row.current = present ? process.value(name) : QString();
        row.captured = it.value();
        row.added = !present;
        row.checked = false; // default: nothing selected
        diff_.append(row);
    }
    refreshDiffTable();
}

void EnvImportPage::applyCandidateFilter()
{
    visibleDiffRows_.clear();
    visibleDiffRows_.reserve(diff_.size());
    for (int index = 0; index < diff_.size(); ++index) {
        DiffRow &entry = diff_[index];
        // No project context: keep the standalone behaviour (every added or
        // changed row). With context: only filelist-referenced names, unless the
        // user explicitly folded everything open.
        const bool visible = !projectContext_ || showAllCandidates_
                             || referencedLayers_.contains(entry.name);
        if (visible) {
            visibleDiffRows_.append(index);
        } else {
            // A hidden row must not stay checked: staging reads the checks, and
            // an invisible checked row would import without ever being shown.
            entry.checked = false;
        }
    }
}

QString EnvImportPage::candidateHintText() const
{
    if (!projectContext_) {
        return {};
    }
    if (referencedVariables_.isEmpty()) {
        return tr("The project's filelist references no variables, so this candidate list stays "
                  "empty: a variable no filelist mentions is never offered here.");
    }
    if (diff_.isEmpty()) {
        return tr("This capture added or changed no variable. The project's filelist references: "
                  "%1.")
            .arg(referencedVariables_.join(QStringLiteral(", ")));
    }
    if (visibleDiffRows_.isEmpty()) {
        return tr("The capture added or changed %1 variable(s), but none is referenced by the "
                  "project's filelist (%2); nothing in this capture can be imported.")
            .arg(diff_.size())
            .arg(referencedVariables_.join(QStringLiteral(", ")));
    }
    const int hidden = hiddenDiffRowCount();
    if (hidden > 0) {
        return tr("%1 variable(s) are folded away because the project's filelist does not "
                  "reference them.")
            .arg(hidden);
    }
    return {};
}

QString EnvImportPage::candidateHint() const
{
    return candidateHintText();
}

void EnvImportPage::refreshDiffTable()
{
    applyCandidateFilter();
    diffTable_->setRowCount(visibleDiffRows_.size());
    for (int row = 0; row < visibleDiffRows_.size(); ++row) {
        const DiffRow &entry = diff_.at(visibleDiffRows_.at(row));
        auto *check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        check->setCheckState(entry.checked ? Qt::Checked : Qt::Unchecked);
        diffTable_->setItem(row, kDiffCheckColumn, check);
        diffTable_->setItem(row, kDiffNameColumn, new QTableWidgetItem(entry.name));
        diffTable_->setItem(
            row, kDiffCurrentColumn,
            new QTableWidgetItem(entry.added ? tr("(not set)") : displayValueFor(entry.name,
                                                                                entry.current)));
        const QString layer = referencedLayers_.value(entry.name);
        diffTable_->setItem(row, kDiffLayerColumn,
                            new QTableWidgetItem(layer.isEmpty()
                                                     ? tr("not referenced")
                                                     : candidateLayerLabel(layer)));
        auto *captured = new QTableWidgetItem(displayValueFor(entry.name, entry.captured));
        captured->setToolTip(tr("A secret-shaped name is masked until you reveal it; the value is "
                                "stored as captured when you check it while revealed."));
        diffTable_->setItem(row, kDiffCapturedColumn, captured);
    }
    if (candidateHint_ != nullptr) {
        const QString hint = candidateHintText();
        candidateHint_->setText(hint);
        candidateHint_->setVisible(!hint.isEmpty());
    }
}

void EnvImportPage::setProjectInputs(const QVariantMap &inputs)
{
    // An EMPTY payload means "no project snapshot" (a standalone build, or a
    // dialog constructed without a shell), NOT "a project that references
    // nothing": the project lane always publishes root/filelist keys, so a real
    // project with an empty referenced set still arrives as a non-empty map and
    // gets the "references no variables" explanation.
    if (inputs.isEmpty()) {
        projectContext_ = false;
        referencedVariables_.clear();
        referencedLayers_.clear();
        refreshDiffTable();
        return;
    }
    setReferencedVariables(inputs.value(QStringLiteral("referencedVariables")).toStringList(),
                           inputs.value(QStringLiteral("referencedVariableLayers")).toStringList());
}

void EnvImportPage::setReferencedVariables(const QStringList &names, const QStringList &layers)
{
    projectContext_ = true;
    referencedVariables_ = names;
    referencedLayers_.clear();
    // Parallel lists by contract (E1); a truncated payload simply drops the
    // unpaired tail rather than inventing a layer.
    const int count = std::min(names.size(), layers.size());
    for (int index = 0; index < count; ++index) {
        referencedLayers_.insert(names.at(index), layers.at(index));
    }
    refreshDiffTable();
}

int EnvImportPage::hiddenDiffRowCount() const
{
    return diff_.size() - visibleDiffRows_.size();
}

QString EnvImportPage::diffLayer(int row) const
{
    if (row < 0 || row >= visibleDiffRows_.size()) {
        return {};
    }
    return referencedLayers_.value(diff_.at(visibleDiffRows_.at(row)).name);
}

void EnvImportPage::setShowAllCandidates(bool showAll)
{
    showAllCandidates_ = showAll;
    if (showAllCandidatesCheck_ != nullptr && showAllCandidatesCheck_->isChecked() != showAll) {
        const QSignalBlocker blocker(showAllCandidatesCheck_);
        showAllCandidatesCheck_->setChecked(showAll);
    }
    refreshDiffTable();
}

int EnvImportPage::diffRowCount() const
{
    return visibleDiffRows_.size();
}

QString EnvImportPage::diffName(int row) const
{
    if (row < 0 || row >= visibleDiffRows_.size()) {
        return {};
    }
    return diff_.at(visibleDiffRows_.at(row)).name;
}

QString EnvImportPage::diffKind(int row) const
{
    if (row < 0 || row >= visibleDiffRows_.size()) {
        return {};
    }
    return diff_.at(visibleDiffRows_.at(row)).added ? QStringLiteral("added")
                                                    : QStringLiteral("changed");
}

QString EnvImportPage::diffCapturedValue(int row) const
{
    if (row < 0 || row >= visibleDiffRows_.size()) {
        return {};
    }
    return diff_.at(visibleDiffRows_.at(row)).captured;
}

QString EnvImportPage::diffDisplayValue(int row) const
{
    if (row < 0 || row >= visibleDiffRows_.size()) {
        return {};
    }
    return displayValueFor(diff_.at(visibleDiffRows_.at(row)).name,
                           diff_.at(visibleDiffRows_.at(row)).captured);
}

bool EnvImportPage::diffChecked(int row) const
{
    const QTableWidgetItem *item = diffTable_->item(row, kDiffCheckColumn);
    return item != nullptr && item->checkState() == Qt::Checked;
}

void EnvImportPage::setDiffChecked(int row, bool checked)
{
    QTableWidgetItem *item = diffTable_->item(row, kDiffCheckColumn);
    if (item == nullptr) {
        return;
    }
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}

// --- stored selection ------------------------------------------------------

void EnvImportPage::sortSelection(QVector<EnvSelectionRow> *rows) const
{
    std::sort(rows->begin(), rows->end(),
              [](const EnvSelectionRow &left, const EnvSelectionRow &right) {
                  const bool leftGlobal = left.projectRoot.isEmpty();
                  const bool rightGlobal = right.projectRoot.isEmpty();
                  if (leftGlobal != rightGlobal) {
                      return leftGlobal;
                  }
                  if (left.projectRoot != right.projectRoot) {
                      return left.projectRoot < right.projectRoot;
                  }
                  return left.name < right.name;
              });
}

void EnvImportPage::refreshSelectionTable()
{
    selectionTable_->setRowCount(draft_.size());
    for (int row = 0; row < draft_.size(); ++row) {
        const EnvSelectionRow &entry = draft_.at(row);
        auto *check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        check->setCheckState(Qt::Unchecked);
        selectionTable_->setItem(row, kSelectionCheckColumn, check);
        selectionTable_->setItem(row, 1, new QTableWidgetItem(entry.name));
        selectionTable_->setItem(
            row, 2,
            new QTableWidgetItem(entry.projectRoot.isEmpty()
                                     ? tr("Global")
                                     : tr("Project: %1").arg(entry.projectRoot)));
        selectionTable_->setItem(row, 3,
                                 new QTableWidgetItem(displayValueFor(entry.name, entry.value)));
    }
}

int EnvImportPage::selectionRowCount() const
{
    return draft_.size();
}

QString EnvImportPage::selectionName(int row) const
{
    return row >= 0 && row < draft_.size() ? draft_.at(row).name : QString();
}

QString EnvImportPage::selectionRoot(int row) const
{
    return row >= 0 && row < draft_.size() ? draft_.at(row).projectRoot : QString();
}

QString EnvImportPage::selectionValue(int row) const
{
    return row >= 0 && row < draft_.size() ? draft_.at(row).value : QString();
}

QString EnvImportPage::selectionDisplayValue(int row) const
{
    if (row < 0 || row >= draft_.size()) {
        return {};
    }
    return displayValueFor(draft_.at(row).name, draft_.at(row).value);
}

bool EnvImportPage::selectionRemoveChecked(int row) const
{
    const QTableWidgetItem *item = selectionTable_->item(row, kSelectionCheckColumn);
    return item != nullptr && item->checkState() == Qt::Checked;
}

void EnvImportPage::setSelectionRemoveChecked(int row, bool checked)
{
    QTableWidgetItem *item = selectionTable_->item(row, kSelectionCheckColumn);
    if (item == nullptr) {
        return;
    }
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}

QString EnvImportPage::effectiveValue(const QString &name) const
{
    QString global;
    bool hasGlobal = false;
    QString project;
    bool hasProject = false;
    const QString root = projectRootEdit_->text().trimmed();
    for (const EnvSelectionRow &row : draft_) {
        if (row.name != name) {
            continue;
        }
        if (row.projectRoot.isEmpty()) {
            global = row.value;
            hasGlobal = true;
        } else if (!root.isEmpty() && row.projectRoot == root) {
            project = row.value;
            hasProject = true;
        }
    }
    if (hasProject) {
        return project;
    }
    return hasGlobal ? global : QString();
}

// --- staging / persistence -------------------------------------------------

QString EnvImportPage::draftLayerRoot() const
{
    if (projectScope()) {
        return QString(); // global layer
    }
    return projectRootEdit_->text().trimmed();
}

bool EnvImportPage::stageImport()
{
    if (busy()) {
        setStatus(tr("A request is already in flight; wait for it to finish."));
        return false;
    }
    const QString layer = draftLayerRoot();
    if (!projectScope() && layer.isEmpty()) {
        setStatus(tr("Choose a project root before storing into the project layer."));
        return false;
    }
    int staged = 0;
    int masked = 0;
    // Only VISIBLE rows can be checked (the filter unchecks what it hides), so
    // staging walks the candidate list the user sees, never the raw diff.
    for (int row = 0; row < visibleDiffRows_.size(); ++row) {
        if (!diffChecked(row)) {
            continue;
        }
        const DiffRow &entry = diff_.at(visibleDiffRows_.at(row));
        if (isSecretShapedName(entry.name) && !secretsRevealed_) {
            // AC-4.8: a secret-shaped value is masked by default and is NOT
            // persisted without the explicit reveal — storing it here would write
            // a value the page never showed. Once revealed (the user saw the
            // value and approved it) the row goes through like any other.
            ++masked;
            continue;
        }
        EnvSelectionRow selected;
        selected.projectRoot = layer;
        selected.name = entry.name;
        selected.value = entry.captured;
        bool replaced = false;
        for (EnvSelectionRow &existing : draft_) {
            if (existing.name == entry.name && existing.projectRoot == layer) {
                existing.value = entry.captured;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            draft_.append(selected);
        }
        ++staged;
    }
    if (staged == 0) {
        setStatus(masked > 0
                      ? tr("The %1 checked variable(s) are secret-shaped and stay masked; reveal "
                           "them explicitly before importing.")
                            .arg(masked)
                      : tr("No variable is checked; nothing was staged."));
        return false;
    }
    sortSelection(&draft_);
    refreshSelectionTable();
    QString message = tr("%1 checked variable(s) staged for the %2 layer — press OK or Apply to "
                         "store them.")
                          .arg(staged)
                          .arg(projectScope() ? tr("global") : tr("project"));
    if (masked > 0) {
        message += tr(" %1 secret-shaped row(s) were skipped: they stay masked until you reveal "
                      "them explicitly.")
                       .arg(masked);
    }
    setStatus(message);
    return true;
}

bool EnvImportPage::removeSelectedStored()
{
    if (busy()) {
        setStatus(tr("A request is already in flight; wait for it to finish."));
        return false;
    }
    QVector<EnvSelectionRow> kept;
    int removed = 0;
    for (int row = 0; row < draft_.size(); ++row) {
        if (selectionRemoveChecked(row)) {
            ++removed;
            continue;
        }
        kept.append(draft_.at(row));
    }
    if (removed == 0) {
        setStatus(tr("No stored row is checked; nothing was removed."));
        return false;
    }
    draft_ = kept;
    refreshSelectionTable();
    setStatus(tr("%1 stored variable(s) removed from the draft — press OK or Apply to store the "
                 "result.")
                  .arg(removed));
    return true;
}

bool EnvImportPage::addVariable(const QString &name, const QString &value)
{
    if (busy()) {
        setStatus(tr("A request is already in flight; wait for it to finish."));
        return false;
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setStatus(tr("The variable name must not be empty."));
        return false;
    }
    if (trimmed.contains(QLatin1Char('='))) {
        setStatus(tr("A variable name must not contain '='."));
        return false;
    }
    for (const QChar character : trimmed) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
            setStatus(tr("A variable name must not contain control characters."));
            return false;
        }
    }
    if (isSecretShapedName(trimmed) && !secretsRevealed_) {
        // AC-4.8: the manual row is the second write path into the same store, so
        // it obeys the same rule as a captured row — no secret-shaped value is
        // stored while it is masked.
        setStatus(tr("Variable %1 is secret-shaped and stays masked; reveal secret values "
                     "explicitly before adding it.")
                      .arg(trimmed));
        return false;
    }
    const QString layer = draftLayerRoot();
    if (!projectScope() && layer.isEmpty()) {
        setStatus(tr("Choose a project root before storing into the project layer."));
        return false;
    }
    bool updated = false;
    for (EnvSelectionRow &row : draft_) {
        if (row.name == trimmed && row.projectRoot == layer) {
            row.value = value;
            updated = true;
            break;
        }
    }
    if (!updated) {
        EnvSelectionRow row;
        row.projectRoot = layer;
        row.name = trimmed;
        row.value = value;
        draft_.append(row);
    }
    sortSelection(&draft_);
    refreshSelectionTable();
    setStatus(tr("Variable %1 %2 in the %3 layer — press OK or Apply to store it.")
                  .arg(trimmed)
                  .arg(updated ? tr("updated") : tr("added"))
                  .arg(projectScope() ? tr("global") : tr("project")));
    return true;
}

bool EnvImportPage::hasPendingChanges() const
{
    QVector<EnvSelectionRow> stored = selection_;
    QVector<EnvSelectionRow> pending = draft_;
    sortSelection(&stored);
    sortSelection(&pending);
    return stored != pending;
}

bool EnvImportPage::commitPending()
{
    if (busy()) {
        setStatus(tr("A request is already in flight; wait for it to finish."));
        return false;
    }
    if (!hasPendingChanges()) {
        setStatus(tr("There is nothing to store."));
        return false;
    }
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    if (service_ == nullptr) {
        setStatus(tr("This build has no vd-ffi environment ABI."));
        return false;
    }
    QVector<EnvSelectionRow> rows = draft_;
    sortSelection(&rows);
    QVector<QByteArray> roots;
    QVector<QByteArray> names;
    QVector<QByteArray> values;
    roots.reserve(rows.size());
    names.reserve(rows.size());
    values.reserve(rows.size());
    for (const EnvSelectionRow &row : rows) {
        roots.append(row.projectRoot.toUtf8());
        names.append(row.name.toUtf8());
        values.append(row.value.toUtf8());
    }
    QVector<VdEnvSelectionInput> inputs;
    inputs.reserve(rows.size());
    for (int index = 0; index < rows.size(); ++index) {
        VdEnvSelectionInput input{};
        input.project_root = roots.at(index).constData();
        input.name = names.at(index).constData();
        input.value = values.at(index).constData();
        inputs.append(input);
    }
    const QByteArray path = selectionPath_.toUtf8();
    const unsigned long long job = vd_env_selection_save(
        service_, path.constData(), inputs.constData(), static_cast<std::size_t>(inputs.size()));
    if (job == 0) {
        setStatus(tr("The selection was refused: %1").arg(lastServiceError(service_)));
        return false;
    }
    saveJob_ = job;
    setStatus(tr("Storing %1 variable(s) in %2…").arg(rows.size()).arg(selectionPath_));
    ensureTimer();
    return true;
#else
    setStatus(tr("This build has no vd-ffi environment ABI."));
    return false;
#endif
}

void EnvImportPage::discardPending()
{
    if (busy()) {
        // A save already in flight owns the file; resetting the draft under it
        // would desynchronise the VIEW from what lands on disk. The dialog
        // flushes after Apply, so this only guards a fast Apply-then-Cancel.
        setStatus(tr("A request is already in flight; wait for it to finish."));
        return;
    }
    draft_ = selection_;
    refreshSelectionTable();
    setStatus(tr("Staged changes discarded; the stored selection is unchanged."));
}

// --- jobs ------------------------------------------------------------------

void EnvImportPage::ensureTimer()
{
    if (timer_ == nullptr) {
        timer_ = new QTimer(this);
        timer_->setInterval(10);
        connect(timer_, &QTimer::timeout, this, [this]() { pollJobs(); });
    }
    if (!timer_->isActive()) {
        timer_->start();
    }
}

bool EnvImportPage::busy() const
{
    return captureJob_ != 0 || loadJob_ != 0 || saveJob_ != 0;
}

void EnvImportPage::pollJobs()
{
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    if (service_ == nullptr) {
        return;
    }
    if (loadJob_ != 0) {
        VdEnvSelection raw{};
        const int state = vd_env_selection_poll(service_, loadJob_, &raw);
        if (state == VD_STATUS_READY) {
            QVector<EnvSelectionRow> rows;
            for (std::size_t index = 0; index < raw.count; ++index) {
                const VdEnvSelectionEntry &entry = raw.items[index];
                EnvSelectionRow row;
                row.projectRoot = entry.project_root == nullptr
                                      ? QString()
                                      : QString::fromUtf8(entry.project_root);
                row.name = entry.name == nullptr ? QString() : QString::fromUtf8(entry.name);
                row.value = entry.value == nullptr ? QString() : QString::fromUtf8(entry.value);
                if (!row.name.isEmpty()) {
                    rows.append(row);
                }
            }
            warnings_ = ownedList(raw.warnings);
            sortSelection(&rows);
            selection_ = rows;
            draft_ = rows;
            vd_env_selection_free(&raw);
            vd_env_selection_release(service_, loadJob_);
            loadJob_ = 0;
            refreshSelectionTable();
            setStatus(rows.isEmpty()
                          ? tr("No imported variables are stored yet.")
                          : tr("%1 imported variable(s) read from %2.")
                                .arg(rows.size())
                                .arg(selectionPath_));
        } else if (state != VD_STATUS_PENDING) {
            warnings_.clear();
            const QString reason = lastServiceError(service_);
            vd_env_selection_free(&raw);
            vd_env_selection_release(service_, loadJob_);
            loadJob_ = 0;
            selection_.clear();
            draft_.clear();
            refreshSelectionTable();
            setStatus(tr("The stored selection could not be read (%1); the page keeps working "
                         "and nothing was changed.")
                          .arg(reason.isEmpty() ? tr("unknown reason") : reason));
        }
    }
    if (captureJob_ != 0) {
        VdEnvCaptureResult raw{};
        const int state = vd_env_capture_poll(service_, captureJob_, &raw);
        if (state == VD_STATUS_READY) {
            QStringList items;
            for (std::size_t index = 0; index < raw.count; ++index) {
                if (raw.items != nullptr && raw.items[index] != nullptr) {
                    items.append(QString::fromUtf8(raw.items[index]));
                }
            }
            const QStringList captureWarnings = ownedList(raw.warnings);
            const QString shell = raw.shell == nullptr ? QString() : QString::fromUtf8(raw.shell);
            const int exitCode = raw.exit_code;
            const QString rawStderr =
                raw.stderr == nullptr ? QString() : QString::fromUtf8(raw.stderr);
            vd_env_capture_result_free(&raw);
            vd_env_capture_release(service_, captureJob_);
            captureJob_ = 0;
            // The diff is collected FIRST: redacting the stderr tail needs to know
            // which captured values are secret-shaped (an rc file can echo one).
            collectDiff(items);
            warnings_ = captureWarnings;
            captureExitCode_ = exitCode;
            captureStderrTail_ = redactKnownSecrets(rawStderr);
            QString report = tr("shell: %1 · exit status: %2")
                                 .arg(shell.isEmpty() ? tr("(unknown)") : shell)
                                 .arg(exitCode);
            if (!captureWarnings.isEmpty()) {
                report += joinedSuffix(captureWarnings);
            }
            if (!captureStderrTail_.trimmed().isEmpty()) {
                report += tr(" · stderr tail: %1").arg(firstLines(captureStderrTail_, 3));
            }
            captureReport_ = report;
            if (exitCode != 0) {
                // AC-4.9: a non-zero exit (a missing/unreadable rc file, a broken
                // start-up file) is NOT discarded: the environment was still read,
                // the exit status and stderr tail stay visible.
                setStatus(tr("The shell reported a problem (exit status %1); the environment was "
                             "still read — see the capture report for the stderr tail.")
                              .arg(exitCode));
            } else if (diff_.isEmpty()) {
                setStatus(tr("The capture found no variable that was added or changed."));
            } else if (visibleDiffRows_.isEmpty()) {
                setStatus(tr("The capture added or changed %1 variable(s), but none is "
                             "referenced by the project's filelist; nothing can be imported "
                             "here.")
                              .arg(diff_.size()));
            } else {
                setStatus(tr("%1 variable(s) were added or changed; none is selected.")
                              .arg(visibleDiffRows_.size()));
            }
        } else if (state != VD_STATUS_PENDING) {
            // AC-4.9: the FOUR failure classes get their own sentence instead of
            // one generic "failed" line — a user cancellation is reported as a
            // cancellation, and a missing shell names the program that failed.
            const QString reason = lastServiceError(service_);
            const int code = lastServiceErrorCode(service_);
            vd_env_capture_result_free(&raw);
            vd_env_capture_release(service_, captureJob_);
            captureJob_ = 0;
            diff_.clear();
            refreshDiffTable();
            captureExitCode_ = -1;
            captureStderrTail_.clear();
            captureReport_ = tr("the capture did not produce an environment");
            warnings_.clear();
            if (code == VD_ERR_CANCELLED) {
                setStatus(tr("The capture was cancelled; nothing was changed."));
            } else if (code == VD_ERR_TIMEOUT) {
                setStatus(tr("The capture timed out and was stopped; nothing was changed. (%1)")
                              .arg(reason.isEmpty() ? tr("unknown reason") : reason));
            } else if (code == VD_ERR_NOT_FOUND) {
                setStatus(tr("The shell could not be started; nothing was changed. (%1)")
                              .arg(reason.isEmpty() ? tr("unknown reason") : reason));
            } else {
                setStatus(tr("The capture failed (%1); nothing was changed.")
                              .arg(reason.isEmpty() ? tr("unknown reason") : reason));
            }
        }
    }
    if (saveJob_ != 0) {
        VdEnvSelection raw{};
        const int state = vd_env_selection_poll(service_, saveJob_, &raw);
        if (state == VD_STATUS_READY) {
            vd_env_selection_free(&raw);
            vd_env_selection_release(service_, saveJob_);
            saveJob_ = 0;
            selection_ = draft_;
            refreshSelectionTable();
            setStatus(tr("Stored %1 variable(s) in %2.")
                          .arg(selection_.size())
                          .arg(selectionPath_));
        } else if (state != VD_STATUS_PENDING) {
            const QString reason = lastServiceError(service_);
            vd_env_selection_free(&raw);
            vd_env_selection_release(service_, saveJob_);
            saveJob_ = 0;
            setStatus(tr("The selection could not be stored (%1); the draft is kept and the "
                         "file is unchanged.")
                          .arg(reason.isEmpty() ? tr("unknown reason") : reason));
        }
    }
#endif
    if (!busy() && timer_ != nullptr) {
        timer_->stop();
    }
    refreshStatus();
}

bool EnvImportPage::flush(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (busy() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return !busy();
}

void EnvImportPage::reload()
{
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    if (service_ == nullptr) {
        setStatus(tr("This build has no vd-ffi environment ABI."));
        return;
    }
    if (busy()) {
        setStatus(tr("A request is already in flight; wait for it to finish."));
        return;
    }
    const QByteArray path = selectionPath_.toUtf8();
    const unsigned long long job = vd_env_selection_load(service_, path.constData());
    if (job == 0) {
        setStatus(tr("The stored selection could not be read: %1")
                      .arg(lastServiceError(service_)));
        return;
    }
    loadJob_ = job;
    warnings_.clear();
    setStatus(tr("Reading the stored selection…"));
    ensureTimer();
#else
    setStatus(tr("This build has no vd-ffi environment ABI."));
#endif
}

void EnvImportPage::refreshStatus()
{
    warningLabel_->setText(warnings_.isEmpty()
                               ? tr("No problems reported by the last request.")
                               : tr("%1 problem(s): %2")
                                     .arg(warnings_.size())
                                     .arg(warnings_.join(QStringLiteral("; "))));
    captureReportLabel_->setText(captureReport_);
    addButton_->setText(projectScope() ? tr("Add / update in the global layer")
                                       : tr("Add / update in the project layer"));
    if (cancelButton_ != nullptr) {
#if defined(VEDIT_SETTINGS_HAVE_FFI)
        // Only a capture can be cancelled; a load/save job keeps its own path.
        cancelButton_->setEnabled(captureJob_ != 0);
#else
        cancelButton_->setEnabled(false);
#endif
    }
}

void EnvImportPage::setStatus(const QString &text)
{
    status_ = text;
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(text);
    }
}

QString EnvImportPage::statusText() const
{
    return status_;
}

QStringList EnvImportPage::warnings() const
{
    return warnings_;
}

QString EnvImportPage::captureReport() const
{
    return captureReport_;
}

int SettingsDialog::cappedHeightFor(const QSize &available)
{
    if (available.width() <= 0 || available.height() <= 0) {
        return kPreferredHeight;
    }
    const int cap = std::max(kMinimumHeight, available.height() * 9 / 10);
    return std::min(kPreferredHeight, cap);
}

SettingsDialog::SettingsDialog(vedit::app::ThemeManager *themes,
                               vedit::app::CommandRegistry *commands, QString settingsPath,
                               QWidget *parent, const QVariantMap &projectInputs)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("settings-dialog"));
    setWindowTitle(QCoreApplication::translate("VEdit", "Preferences"));
    setModal(true);
    // Batch 20261110d — size discipline. The measured real-machine window was
    // 1510×1741: taller than the screen, so the OK/Apply/Cancel row was below
    // the screen edge and unreachable. The dialog is now CAPPED to the screen's
    // available height and its CONTENT scrolls; the action row stays outside the
    // scroll area, pinned at the bottom, so it is always reachable. The cap is
    // 90% of the available height (never above the preferred 760), so a short
    // laptop screen keeps a margin for the window title bar.
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QSize available = screen != nullptr ? screen->availableGeometry().size() : QSize();
    if (available.width() > 0 && available.height() > 0) {
        setMaximumHeight(std::max(kMinimumHeight, available.height() * 9 / 10));
    }
    resize(kPreferredWidth, cappedHeightFor(available));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    const QString storePath = settingsPath;
    panel_ = new SettingsPanel(themes, commands, std::move(settingsPath), this);
    // The panel (and the Environment page inside its tab widget) can be taller
    // than the screen; it scrolls instead of growing the dialog. The button box
    // is added to the DIALOG layout AFTER this widget, which keeps it fixed
    // below the scroll area.
    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("settings-dialog-scroll"));
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setWidget(panel_);
    layout->addWidget(scroll_, 1);

    // Batch 20261110c: the Environment page joins the panel's own tabs so it
    // shares the dialog's Apply/OK/Cancel discipline. The page keeps its own
    // draft; nothing is written before the dialog commits.
    environmentPage_ = new EnvImportPage(storePath, this);
    // batch-20261114 E2: the candidate list is filtered by the project's
    // referenced set from the moment the page exists, so no capture can ever
    // offer a variable the filelist does not use as its DEFAULT view.
    environmentPage_->setProjectInputs(projectInputs);
    if (QTabWidget *tabs = panel_->tabs(); tabs != nullptr) {
        const int index = tabs->addTab(environmentPage_,
                                       QCoreApplication::translate("VEdit", "Environment"));
        tabs->setTabToolTip(
            index, QCoreApplication::translate(
                       "VEdit",
                       "Import variables from your shell; a project value overrides a global "
                       "value with the same name."));
    }

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                        | QDialogButtonBox::Apply,
                                    this);
    buttons_->setObjectName(QStringLiteral("settings-dialog-buttons"));
    layout->addWidget(buttons_);

    // Preview semantics: edits are visible at once, Apply/OK persist them.
    panel_->setDeferredPersistence(true);

    connect(buttons_, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    if (QPushButton *apply = buttons_->button(QDialogButtonBox::Apply); apply != nullptr) {
        connect(apply, &QPushButton::clicked, this, &SettingsDialog::applyPending);
    }
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::applyPending()
{
    if (panel_ != nullptr) {
        panel_->commitPending();
    }
    if (environmentPage_ != nullptr && environmentPage_->commitPending()) {
        // The selection file is written by an asynchronous job; the dialog must
        // not leave a half-finished write behind (or report a stored selection
        // the writer never received).
        environmentPage_->flush();
    }
}

void SettingsDialog::revertPending()
{
    if (environmentPage_ != nullptr) {
        environmentPage_->discardPending();
    }
    if (panel_ != nullptr) {
        panel_->revertPending();
    }
}

bool SettingsDialog::hasPendingChanges() const
{
    return (panel_ != nullptr && panel_->hasPendingChanges())
        || (environmentPage_ != nullptr && environmentPage_->hasPendingChanges());
}

void SettingsDialog::accept()
{
    if (panel_ != nullptr) {
        panel_->commitPending();
    }
    if (environmentPage_ != nullptr && environmentPage_->commitPending()) {
        environmentPage_->flush();
    }
    QDialog::accept();
}

void SettingsDialog::reject()
{
    if (environmentPage_ != nullptr) {
        environmentPage_->discardPending();
    }
    if (panel_ != nullptr) {
        panel_->revertPending();
    }
    QDialog::reject();
}

} // namespace vedit::workspace::settings
