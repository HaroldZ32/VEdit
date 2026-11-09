#include "settings_panel.h"

#include "command_registry.h"
#include "i18n.h"
#include "product_theme.h"
#include "settings_keys.h"
#include "theme_manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

#include <cerrno>
#include <cstring>
#include <limits>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <QCoreApplication>
#endif

namespace vedit::workspace::settings {

// T18: the interface-language runtime lives in the application shell; the panel
// only consumes its store key and its restart contract.
namespace i18n = vedit::app::i18n;

namespace {
const char *const kThemeKey = "theme";
// UI-Mosaic T3: the product (chrome) theme id. ApplicationShell reads the same
// key at startup (settings.toml > session archive > yunwu), so the spelling is
// shared state, not a private detail of this panel.
const char *const kUiThemeKey = "uiTheme";
const char *const kKeymapPrefix = "keymap.";

// FIM timeout/token bounds; the Rust transport clamps to the same range, so a
// hand-edited file cannot make the UI spin outside it.
constexpr int kMinTimeoutMs = 100;
constexpr int kMaxTimeoutMs = 60000;
constexpr int kDefaultTimeoutMs = 5000;
constexpr int kMinMaxTokens = 1;
constexpr int kMaxMaxTokens = 512;
constexpr int kDefaultMaxTokens = 64;

QString keyText(const QVector<Entry> &entries, const char *key)
{
    return SettingsClient::valueOf(entries, QString::fromLatin1(key));
}

bool keyBool(const QVector<Entry> &entries, const char *key)
{
    const QString value = keyText(entries, key).trimmed().toLower();
    return value == QStringLiteral("true") || value == QStringLiteral("1")
        || value == QStringLiteral("yes") || value == QStringLiteral("on");
}

int keyInt(const QVector<Entry> &entries, const char *key, int fallback)
{
    bool ok = false;
    const int value = keyText(entries, key).trimmed().toInt(&ok);
    return ok ? value : fallback;
}

// Permissions that must not be set on the FIM key file.
QFileDevice::Permissions loosePermissions()
{
    return QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
}

#if defined(Q_OS_UNIX)
// True when a process with this pid exists (or exists but is not ours). Used so a
// sweep never deletes a temporary file another VEdit instance is writing right now.
bool processIsAlive(qlonglong pid)
{
    if (pid <= 0 || pid > static_cast<qlonglong>(std::numeric_limits<int>::max())) {
        return false;
    }
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}
#endif

// Best-effort removal of temporary key files left behind by a crash (see the
// residual window above). Called before a new temp is created, so it never sees
// its own. A temp whose `<pid>` is still alive belongs to a running instance and
// is left alone. Unlink failures are ignored: hygiene must not fail the save.
void sweepStaleKeyFiles(const QString &path)
{
#if defined(Q_OS_UNIX)
    const QFileInfo info(path);
    const QString prefix = info.fileName() + QStringLiteral(".tmp.");
    const QDir directory(info.absolutePath());
    const QStringList candidates = directory.entryList(
        QStringList{prefix + QStringLiteral("*")}, QDir::Files | QDir::Hidden);
    for (const QString &name : candidates) {
        bool parsed = false;
        const qlonglong pid =
            name.mid(prefix.size()).section(QLatin1Char('.'), 0, 0).toLongLong(&parsed);
        if (parsed && processIsAlive(pid)) {
            continue;
        }
        QFile::remove(directory.filePath(name));
    }
#else
    Q_UNUSED(path)
#endif
}

// Write `content` to `path` with owner-only permissions FROM CREATION, replacing
// the target atomically.
//
// Why not "write then chmod": a local user who can read the directory can open a
// freshly created 0644 file while it is still empty, hold the descriptor, and read
// the secret after it is written. Here the temporary file is created with
// O_CREAT|O_EXCL and mode 0600, its mode is VERIFIED with fstat before the first
// byte of the secret is written, and it is renamed over the target (same
// directory). So the NEW secret is never written to a group/other-readable file;
// a pre-existing loose file keeps its old content until it is atomically replaced.
//
// Residual window, stated plainly: a kill or power loss between creating the temp
// file and the rename leaves a 0600 temp file that CONTAINS THE SECRET (no
// group/other access, but debris with a secret in it). The target still holds the
// previous content, so the user is not locked out, and the next save sweeps those
// stale `fim_api_key.tmp.<pid>.<n>` files away (sweepStaleKeyFiles below).
//
// Returns false with `*error` set; on failure this call unlinks its own temporary
// file (the unlink result is not checked) so no debris of its own is intended to
// remain.
#if defined(Q_OS_UNIX)
bool writeOwnerOnlyFile(const QString &path, const QByteArray &content, QString *error)
{
    sweepStaleKeyFiles(path);
    const QByteArray target = QFile::encodeName(path);
    QByteArray temporary;
    int fd = -1;
    QString reason;
    for (int attempt = 0; attempt < 8; ++attempt) {
        temporary = target + QByteArrayLiteral(".tmp.")
            + QByteArray::number(static_cast<qlonglong>(::getpid())) + QByteArrayLiteral(".")
            + QByteArray::number(attempt);
        fd = ::open(temporary.constData(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
        if (fd >= 0) {
            break;
        }
        reason = QString::fromLocal8Bit(std::strerror(errno));
        if (errno != EEXIST) {
            *error = reason;
            return false;
        }
    }
    if (fd < 0) {
        *error = reason.isEmpty() ? QStringLiteral("cannot create the temporary key file") : reason;
        return false;
    }
    struct stat created {};
    const bool verified = ::fstat(fd, &created) == 0 && S_ISREG(created.st_mode)
        && (created.st_mode & 0077) == 0
        && (created.st_mode & (S_IRUSR | S_IWUSR)) == (S_IRUSR | S_IWUSR);
    if (!verified) {
        const QString detail = reason.isEmpty() ? QStringLiteral("the created file is not owner-only")
                                                : reason;
        ::close(fd);
        ::unlink(temporary.constData());
        *error = detail;
        return false;
    }
    bool ok = true;
    qint64 written = 0;
    while (ok && written < content.size()) {
        const ssize_t chunk = ::write(fd, content.constData() + written,
                                      static_cast<size_t>(content.size() - written));
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            reason = QString::fromLocal8Bit(std::strerror(errno));
            ok = false;
        } else if (chunk == 0) {
            reason = QStringLiteral("short write");
            ok = false;
        } else {
            written += static_cast<qint64>(chunk);
        }
    }
    if (ok && ::fsync(fd) != 0) {
        reason = QString::fromLocal8Bit(std::strerror(errno));
        ok = false;
    }
    if (::close(fd) != 0 && ok) {
        reason = QString::fromLocal8Bit(std::strerror(errno));
        ok = false;
    }
    if (ok && ::rename(temporary.constData(), target.constData()) != 0) {
        reason = QString::fromLocal8Bit(std::strerror(errno));
        ok = false;
    }
    if (!ok) {
        ::unlink(temporary.constData());
        *error = reason;
        return false;
    }
    struct stat replaced {};
    if (::stat(target.constData(), &replaced) != 0 || !S_ISREG(replaced.st_mode)
        || (replaced.st_mode & 0077) != 0) {
        ::unlink(target.constData());
        *error = QStringLiteral("the key file is not owner-only after the atomic replace");
        return false;
    }
    return true;
}
#else
// The product ships on Linux (CentOS 7 and modern distributions); this branch
// keeps the code portable. It is NOT covered by the security acceptance test.
bool writeOwnerOnlyFile(const QString &path, const QByteArray &content, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = file.errorString();
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        *error = file.errorString();
        file.close();
        QFile::remove(path);
        return false;
    }
    const bool wrote = file.write(content) == content.size() && file.flush();
    const QString reason = file.errorString();
    file.close();
    if (!wrote) {
        QFile::remove(path);
        *error = reason;
        return false;
    }
    return true;
}
#endif

// A path editor with a "…" button that opens a file chooser; the caller connects
// the line edit to its persistence handler.
QWidget *makePathRow(QWidget *owner, QLineEdit **edit, const QString &objectName,
                     const QString &title)
{
    auto *container = new QWidget(owner);
    auto *row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    *edit = new QLineEdit(container);
    (*edit)->setObjectName(objectName);
    (*edit)->setPlaceholderText(QObject::tr("empty: use the resolved toolchain"));
    auto *browse = new QPushButton(QStringLiteral("…"), container);
    browse->setObjectName(objectName + QStringLiteral("-browse"));
    browse->setToolTip(QObject::tr("Choose %1").arg(title));
    browse->setMaximumWidth(32);
    QObject::connect(browse, &QPushButton::clicked, container, [edit, container, title]() {
        // V12: never the native/portal chooser; only Qt's own dialog obeys this
        // application's stylesheet and palette. Spelled here rather than through
        // src/common/file_dialog_filters.h because this lane also builds
        // standalone, without that include path.
        const QString chosen =
            QFileDialog::getOpenFileName(container, QObject::tr("Select %1").arg(title),
                                         (*edit)->text(), QString(), nullptr,
                                         QFileDialog::DontUseNativeDialog);
        if (!chosen.isEmpty()) {
            (*edit)->setText(chosen);
        }
    });
    row->addWidget(*edit, 1);
    row->addWidget(browse);
    return container;
}
} // namespace

SettingsPanel::SettingsPanel(vedit::app::ThemeManager *themes,
                             vedit::app::CommandRegistry *commands, QString settingsPath,
                             QWidget *parent)
    : QWidget(parent)
    , themes_(themes)
    , commands_(commands)
    , client_(std::make_unique<SettingsClient>(std::move(settingsPath), this))
{
    setObjectName(QStringLiteral("settings-panel"));
    // UX batch 2 / A4: remember the font the process started with, so
    // "follow the system" can put it back after an explicit size was tried.
    ui_scale::rememberStartupFont();
    buildUi();
    reload();
}

SettingsPanel::~SettingsPanel() = default;

void SettingsPanel::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    // Theme stays above the tabs: it is one appearance switch that applies live,
    // not a page of its own (item 10.2 fixes the three pages).
    auto *themeGroup = new QGroupBox(tr("Theme"), this);
    auto *themeForm = new QFormLayout(themeGroup);
    // UI-Mosaic T3: the PRODUCT theme is listed above the syntax theme. One
    // switches the whole chrome (palette + cards.qss), the other only the
    // editor's syntax colours; the display names are the contract's frozen data.
    auto *uiThemeCombo = new QComboBox(themeGroup);
    uiThemeCombo->setObjectName(QStringLiteral("settings-ui-theme"));
    for (const vedit::app::ProductTheme &theme : vedit::app::productThemes()) {
        uiThemeCombo->addItem(theme.displayName, theme.id);
    }
    themeForm->addRow(tr("Interface theme:"), uiThemeCombo);
    auto *uiThemeHint = new QLabel(
        tr("Applied immediately and stored as `uiTheme` by the Rust service."), themeGroup);
    uiThemeHint->setWordWrap(true);
    themeForm->addRow(uiThemeHint);
    themeCombo_ = new QComboBox(themeGroup);
    themeCombo_->setObjectName(QStringLiteral("settings-theme"));
    themeForm->addRow(tr("Theme:"), themeCombo_);
    auto *themeHint = new QLabel(
        tr("Applied immediately and stored as `theme` by the Rust service."), themeGroup);
    themeHint->setWordWrap(true);
    themeForm->addRow(themeHint);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("settings-tabs"));
    tabs_->addTab(buildShortcutsPage(), tr("Shortcuts"));
    tabs_->addTab(buildToolsPage(), tr("Tools"));
    tabs_->addTab(buildFimPage(), tr("FIM"));
    // UX batch 2 / A4: appearance (font + scale), the unified lint page and the
    // formatter options. Appended so the three earlier page positions (and the
    // checks that name them) stay valid.
    tabs_->addTab(buildAppearancePage(), tr("Appearance"));
    tabs_->addTab(buildLintPage(), tr("Lint"));
    tabs_->addTab(buildFormatPage(), tr("Format"));
    tabs_->setTabToolTip(0, tr("Command shortcuts; a conflicting binding is shown in red."));
    tabs_->setTabToolTip(1, tr("LSP backend, lint engine, tool paths and -I/-D."));
    tabs_->setTabToolTip(
        2, tr("Fill-in completions. The API key is never stored in settings.toml."));
    tabs_->setTabToolTip(
        3, tr("Application font (applied immediately) and interface scale (applied on the "
              "next start)."));
    tabs_->setTabToolTip(
        4, tr("The selected lint engine's options. Both engines keep their own settings."));
    tabs_->setTabToolTip(
        5, tr("verible-verilog-format options; an empty value passes no flag."));

    auto *diagnosticsGroup = new QGroupBox(tr("Diagnostics"), this);
    auto *diagnosticsLayout = new QVBoxLayout(diagnosticsGroup);
    diagnosticsLabel_ = new QLabel(diagnosticsGroup);
    diagnosticsLabel_->setObjectName(QStringLiteral("settings-diagnostics-label"));
    diagnosticsLabel_->setWordWrap(true);
    diagnosticsLayout->addWidget(diagnosticsLabel_);
    diagnosticsView_ = new QPlainTextEdit(diagnosticsGroup);
    diagnosticsView_->setObjectName(QStringLiteral("settings-diagnostics"));
    diagnosticsView_->setReadOnly(true);
    QFont monospace(QStringLiteral("monospace"));
    diagnosticsView_->setFont(monospace);
    diagnosticsView_->setMaximumHeight(120);
    diagnosticsLayout->addWidget(diagnosticsView_);
    reloadButton_ = new QPushButton(tr("Reload from disk"), diagnosticsGroup);
    reloadButton_->setObjectName(QStringLiteral("settings-reload"));
    diagnosticsLayout->addWidget(reloadButton_, 0, Qt::AlignLeft);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("settings-status"));
    statusLabel_->setWordWrap(true);

    layout->addWidget(themeGroup);
    layout->addWidget(tabs_, 3);
    layout->addWidget(diagnosticsGroup, 1);
    layout->addWidget(statusLabel_);

    connect(themeCombo_, &QComboBox::activated, this,
            [this](int index) { setTheme(themeCombo_->itemText(index)); });
    // UI-Mosaic T3: the interface theme applies immediately and is persisted
    // under `uiTheme`; the state label uses the same wording as the syntax
    // theme's success/failure paths.
    connect(uiThemeCombo, &QComboBox::activated, this, [this, uiThemeCombo](int index) {
        if (themes_ == nullptr) {
            return;
        }
        const QString id = uiThemeCombo->itemData(index).toString();
        QString error;
        if (!themes_->applyUiTheme(id, &error)) {
            addDiagnostic(error);
            statusLabel_->setText(tr("Interface theme '%1' could not be applied.").arg(id));
            refreshThemeCombo();
            return;
        }
        // UI-Mosaic T6: a product theme carries a paired syntax theme. It is
        // applied only when the user never chose a syntax theme explicitly
        // (the `theme` key is then absent); an explicit choice is preserved.
        const vedit::app::ProductTheme *product = vedit::app::productThemeById(id);
        if (product != nullptr
            && SettingsClient::valueOf(entries_, QString::fromLatin1(kThemeKey)).isEmpty()) {
            QString syntaxError;
            if (!themes_->applyTheme(product->syntaxTheme, &syntaxError)) {
                addDiagnostic(syntaxError);
            }
        }
        SettingsClient::setValue(&entries_, QString::fromLatin1(kUiThemeKey), id);
        persist();
        refreshThemeCombo();
        statusLabel_->setText(
            tr("Interface theme '%1' applied and stored by the Rust service.").arg(id));
    });
    connect(assignButton_, &QPushButton::clicked, this, [this]() { applyEditorShortcut(); });
    connect(clearButton_, &QPushButton::clicked, this, [this]() {
        const QString command = selectedCommand();
        if (!command.isEmpty()) {
            resetShortcut(command);
        }
    });
    connect(resetAllButton_, &QPushButton::clicked, this, [this]() { resetAllShortcuts(); });
    connect(reloadButton_, &QPushButton::clicked, this, [this]() { reload(); });
    connect(keyTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const QString command = selectedCommand();
        if (!command.isEmpty() && commands_ != nullptr) {
            if (const QAction *action = commands_->action(command); action != nullptr) {
                shortcutEditor_->setKeySequence(action->shortcut());
            }
        }
    });

    // Tools page: every edit reaches the store (or the deferred preview).
    connect(lspBackendCombo_, &QComboBox::currentIndexChanged, this,
            [this]() { persistFromWidgets(); });
    // UX batch 2 / A4 / §0.1 R1: switching the engine swaps the set the page
    // shows. The half the widgets showed is folded into ITS OWN set FIRST, so the
    // switch can never copy one engine's values into the other's keys.
    connect(lintEngineCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        if (updating_) {
            return;
        }
        lintValues_ = lintValues();
        lintBackend_ = lintBackendFromSetting(lintEngineCombo_->currentData().toString());
        applyLintWidgets();
        persistFromWidgets();
    });
    for (QLineEdit *edit : {veribleLsEdit_, slangEdit_, slangServerEdit_, veribleFormatEdit_,
                            veribleRulesEdit_}) {
        connect(edit, &QLineEdit::editingFinished, this, [this]() { persistFromWidgets(); });
    }
    connect(includeDirsEdit_, &QPlainTextEdit::textChanged, this,
            [this]() { persistFromWidgets(); });
    connect(definesEdit_, &QPlainTextEdit::textChanged, this,
            [this]() { persistFromWidgets(); });

    // FIM page.
    connect(fimEnabledCheck_, &QCheckBox::toggled, this, [this]() { persistFromWidgets(); });
    connect(fimGhostCheck_, &QCheckBox::toggled, this, [this]() { persistFromWidgets(); });
    connect(fimProtocolCombo_, &QComboBox::currentIndexChanged, this,
            [this]() { persistFromWidgets(); });
    for (QLineEdit *edit : {fimEndpointEdit_, fimModelEdit_}) {
        connect(edit, &QLineEdit::editingFinished, this, [this]() { persistFromWidgets(); });
    }
    connect(fimTimeoutSpin_, &QSpinBox::valueChanged, this, [this]() { persistFromWidgets(); });
    connect(fimMaxTokensSpin_, &QSpinBox::valueChanged, this,
            [this]() { persistFromWidgets(); });
    connect(fimKeySaveButton_, &QPushButton::clicked, this, [this]() {
        if (fimApiKeyEdit_->text().isEmpty()) {
            statusLabel_->setText(tr("Type the API key first; the field is write-only."));
            return;
        }
        if (persistTypedFimKey()) {
            statusLabel_->setText(
                tr("API key stored owner-only (0600) in %1").arg(fimKeyFilePath()));
        }
        refreshFimKeySource();
    });
    connect(fimKeyClearButton_, &QPushButton::clicked, this, [this]() { clearFimKey(); });

    // Appearance page: the font applies to the running application at once; the
    // scale is a restart switch (the hint says so).
    connect(fontPointSizeSpin_, &QSpinBox::valueChanged, this, [this](int value) {
        if (updating_) {
            return;
        }
        applyFontPointSize(value);
        persistFromWidgets();
    });
    connect(scalePercentCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        if (updating_) {
            return;
        }
        persistFromWidgets();
        applyAppearanceEntries();
    });

    // Lint page: every widget writes both sets back (the inactive one verbatim).
    connect(lintRulesetCombo_, &QComboBox::currentIndexChanged, this,
            [this]() { persistFromWidgets(); });
    connect(lintAutofixCombo_, &QComboBox::currentIndexChanged, this,
            [this]() { persistFromWidgets(); });
    for (QLineEdit *edit : {lintRulesEdit_, lintRulesConfigEdit_, lintSlangErrorLimitEdit_}) {
        connect(edit, &QLineEdit::editingFinished, this, [this]() { persistFromWidgets(); });
    }
    for (QPlainTextEdit *edit : {lintWaiverFilesEdit_, lintSlangWarningsEdit_}) {
        connect(edit, &QPlainTextEdit::textChanged, this, [this]() { persistFromWidgets(); });
    }

    // Format page: one connection per knob widget, driven by the same table.
    for (QWidget *widget : formatKnobWidgets_) {
        if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            connect(combo, &QComboBox::currentIndexChanged, this,
                    [this]() { persistFromWidgets(); });
        } else if (auto *spin = qobject_cast<QSpinBox *>(widget)) {
            connect(spin, &QSpinBox::valueChanged, this,
                    [this]() { persistFromWidgets(); });
        }
    }
    connect(formatRestoreButton_, &QPushButton::clicked, this,
            [this]() { restoreFormatDefaults(); });
    connect(formatAdvancedButton_, &QToolButton::toggled, this, [this](bool shown) {
        formatAdvancedButton_->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
        if (formatAdvancedContainer_ != nullptr) {
            formatAdvancedContainer_->setVisible(shown);
        }
    });
}

QWidget *SettingsPanel::buildShortcutsPage()
{
    shortcutsPage_ = new QWidget(this);
    shortcutsPage_->setObjectName(QStringLiteral("settings-page-shortcuts"));
    auto *keyLayout = new QVBoxLayout(shortcutsPage_);
    keyLayout->setContentsMargins(8, 8, 8, 8);

    // RM-5b: the page must SHOW the current effective binding and the default it
    // falls back to; an empty cell read like a bug, so "unbound" is explicit.
    keyTable_ = new QTableWidget(0, 4, shortcutsPage_);
    keyTable_->setObjectName(QStringLiteral("settings-keys"));
    keyTable_->setHorizontalHeaderLabels(
        {tr("Command"), tr("Effective shortcut"), tr("Default"), tr("State")});
    keyTable_->horizontalHeader()->setStretchLastSection(true);
    keyTable_->verticalHeader()->setVisible(false);
    keyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    keyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    keyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    keyLayout->addWidget(keyTable_, 1);

    auto *editorRow = new QHBoxLayout();
    shortcutEditor_ = new QKeySequenceEdit(shortcutsPage_);
    shortcutEditor_->setObjectName(QStringLiteral("settings-shortcut-editor"));
    assignButton_ = new QPushButton(tr("Assign to selected command"), shortcutsPage_);
    assignButton_->setObjectName(QStringLiteral("settings-assign"));
    clearButton_ = new QPushButton(tr("Reset to Default"), shortcutsPage_);
    clearButton_->setObjectName(QStringLiteral("settings-clear"));
    resetAllButton_ = new QPushButton(tr("Reset All to Defaults"), shortcutsPage_);
    resetAllButton_->setObjectName(QStringLiteral("settings-reset-all"));
    editorRow->addWidget(new QLabel(tr("New shortcut:"), shortcutsPage_));
    editorRow->addWidget(shortcutEditor_, 1);
    editorRow->addWidget(assignButton_);
    editorRow->addWidget(clearButton_);
    editorRow->addWidget(resetAllButton_);
    keyLayout->addLayout(editorRow);

    conflictLabel_ = new QLabel(shortcutsPage_);
    conflictLabel_->setObjectName(QStringLiteral("settings-conflict"));
    conflictLabel_->setWordWrap(true);
    keyLayout->addWidget(conflictLabel_);
    return shortcutsPage_;
}

QWidget *SettingsPanel::buildToolsPage()
{
    toolsPage_ = new QWidget(this);
    toolsPage_->setObjectName(QStringLiteral("settings-page-tools"));
    toolsForm_ = new QFormLayout(toolsPage_);
    toolsForm_->setContentsMargins(10, 10, 10, 10);
    toolsForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    toolsForm_->setVerticalSpacing(6);

    // T18 / AC-4.1: the interface-language row. The selection is a PREVIEW until
    // the restart action confirms it; see requestLanguageRestart().
    languageCombo_ = new QComboBox(toolsPage_);
    languageCombo_->setObjectName(QStringLiteral("settings-tools-language"));
    languageCombo_->addItem(tr("Auto"), i18n::languageSetting(i18n::UiLanguage::Auto));
    languageCombo_->addItem(QStringLiteral("English"), i18n::languageSetting(i18n::UiLanguage::English));
    languageCombo_->addItem(QString::fromUtf8("简体中文"),
                            i18n::languageSetting(i18n::UiLanguage::SimplifiedChinese));
    languageCombo_->setToolTip(
        tr("Interface language. Auto follows the system locale; a switch needs a restart."));
    languageRestartButton_ = new QPushButton(tr("Restart now to apply"), toolsPage_);
    languageRestartButton_->setObjectName(QStringLiteral("settings-tools-language-restart"));
    auto *languageRow = new QWidget(toolsPage_);
    auto *languageLayout = new QHBoxLayout(languageRow);
    languageLayout->setContentsMargins(0, 0, 0, 0);
    languageLayout->addWidget(languageCombo_, 1);
    languageLayout->addWidget(languageRestartButton_);
    toolsForm_->addRow(tr("Interface language:"), languageRow);
    languageHint_ = new QLabel(tr("The interface language is applied on the next start."), toolsPage_);
    languageHint_->setObjectName(QStringLiteral("settings-tools-language-hint"));
    languageHint_->setWordWrap(true);
    toolsForm_->addRow(languageHint_);
    connect(languageCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { updateLanguageHint(); });
    connect(languageRestartButton_, &QPushButton::clicked, this,
            [this]() { requestLanguageRestart(); });

    lspBackendCombo_ = new QComboBox(toolsPage_);
    lspBackendCombo_->setObjectName(QStringLiteral("settings-tools-lsp-backend"));
    lspBackendCombo_->addItem(QStringLiteral("verible-verilog-ls"),
                              QString::fromLatin1(keys::kLspBackendVerible));
    lspBackendCombo_->addItem(QStringLiteral("slang-server"),
                              QString::fromLatin1(keys::kLspBackendSlang));
    lspBackendCombo_->setToolTip(tr("Which language server answers diagnostics, outline, "
                                    "definition, references and rename."));
    toolsForm_->addRow(tr("LSP backend:"), lspBackendCombo_);

    lintEngineCombo_ = new QComboBox(toolsPage_);
    lintEngineCombo_->setObjectName(QStringLiteral("settings-tools-lint-engine"));
    lintEngineCombo_->addItem(QStringLiteral("slang"),
                              QString::fromLatin1(keys::kLintEngineSlang));
    lintEngineCombo_->addItem(QStringLiteral("verible-verilog-lint"),
                              QString::fromLatin1(keys::kLintEngineVerible));
    lintEngineCombo_->setToolTip(
        tr("Which tool runs the project-wide batch lint (independent of the LSP backend)."));
    toolsForm_->addRow(tr("Lint engine:"), lintEngineCombo_);

    toolsForm_->addRow(
        tr("verible-verilog-ls:"),
        makePathRow(toolsPage_, &veribleLsEdit_, QStringLiteral("settings-tools-verible-ls"),
                    QStringLiteral("verible-verilog-ls")));
    toolsForm_->addRow(
        tr("slang:"),
        makePathRow(toolsPage_, &slangEdit_, QStringLiteral("settings-tools-slang"),
                    QStringLiteral("slang")));
    toolsForm_->addRow(
        tr("slang-server:"),
        makePathRow(toolsPage_, &slangServerEdit_, QStringLiteral("settings-tools-slang-server"),
                    QStringLiteral("slang-server")));
    toolsForm_->addRow(
        tr("verible-verilog-format:"),
        makePathRow(toolsPage_, &veribleFormatEdit_,
                    QStringLiteral("settings-tools-verible-format"),
                    QStringLiteral("verible-verilog-format")));
    toolsForm_->addRow(
        tr("verible rules:"),
        makePathRow(toolsPage_, &veribleRulesEdit_, QStringLiteral("settings-tools-verible-rules"),
                    QStringLiteral(".rules.verible_lint")));

    includeDirsEdit_ = new QPlainTextEdit(toolsPage_);
    includeDirsEdit_->setObjectName(QStringLiteral("settings-tools-include-dirs"));
    includeDirsEdit_->setPlaceholderText(tr("one directory per line, e.g. rtl/include"));
    includeDirsEdit_->setMaximumHeight(64);
    toolsForm_->addRow(tr("Include dirs (-I):"), includeDirsEdit_);

    definesEdit_ = new QPlainTextEdit(toolsPage_);
    definesEdit_->setObjectName(QStringLiteral("settings-tools-defines"));
    definesEdit_->setPlaceholderText(tr("one define per line, e.g. WIDTH=8"));
    definesEdit_->setMaximumHeight(56);
    toolsForm_->addRow(tr("Defines (-D):"), definesEdit_);

    auto *hint = new QLabel(
        tr("Stored in settings.toml. The backend and lint-engine choices apply on the next "
           "parse; -I/-D are handed to the project-wide lint."),
        toolsPage_);
    hint->setWordWrap(true);
    toolsForm_->addRow(hint);
    return toolsPage_;
}

QWidget *SettingsPanel::buildFimPage()
{
    fimPage_ = new QWidget(this);
    fimPage_->setObjectName(QStringLiteral("settings-page-fim"));
    auto *layout = new QVBoxLayout(fimPage_);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    fimEnabledCheck_ = new QCheckBox(tr("Enable FIM completion"), fimPage_);
    fimEnabledCheck_->setObjectName(QStringLiteral("settings-fim-enabled"));
    form->addRow(fimEnabledCheck_);

    fimGhostCheck_ = new QCheckBox(tr("Show the suggestion as ghost text in the editor"),
                                   fimPage_);
    fimGhostCheck_->setObjectName(QStringLiteral("settings-fim-ghost-text"));
    form->addRow(fimGhostCheck_);

    fimProtocolCombo_ = new QComboBox(fimPage_);
    fimProtocolCombo_->setObjectName(QStringLiteral("settings-fim-protocol"));
    fimProtocolCombo_->addItem(QStringLiteral("OpenAI-compatible (/v1/completions)"),
                               QString::fromLatin1(keys::kFimProtocolOpenAi));
    fimProtocolCombo_->addItem(QStringLiteral("llama.cpp (/infill)"),
                               QString::fromLatin1(keys::kFimProtocolLlamaCpp));
    form->addRow(tr("Protocol:"), fimProtocolCombo_);

    fimEndpointEdit_ = new QLineEdit(fimPage_);
    fimEndpointEdit_->setObjectName(QStringLiteral("settings-fim-endpoint"));
    fimEndpointEdit_->setPlaceholderText(QStringLiteral("http://127.0.0.1:8080"));
    form->addRow(tr("Endpoint:"), fimEndpointEdit_);

    fimModelEdit_ = new QLineEdit(fimPage_);
    fimModelEdit_->setObjectName(QStringLiteral("settings-fim-model"));
    fimModelEdit_->setPlaceholderText(tr("model name (required for the OpenAI protocol)"));
    form->addRow(tr("Model:"), fimModelEdit_);

    fimTimeoutSpin_ = new QSpinBox(fimPage_);
    fimTimeoutSpin_->setObjectName(QStringLiteral("settings-fim-timeout"));
    fimTimeoutSpin_->setRange(kMinTimeoutMs, kMaxTimeoutMs);
    fimTimeoutSpin_->setSingleStep(100);
    fimTimeoutSpin_->setValue(kDefaultTimeoutMs);
    fimTimeoutSpin_->setSuffix(QStringLiteral(" ms"));
    form->addRow(tr("Timeout:"), fimTimeoutSpin_);

    fimMaxTokensSpin_ = new QSpinBox(fimPage_);
    fimMaxTokensSpin_->setObjectName(QStringLiteral("settings-fim-max-tokens"));
    fimMaxTokensSpin_->setRange(kMinMaxTokens, kMaxMaxTokens);
    fimMaxTokensSpin_->setValue(kDefaultMaxTokens);
    form->addRow(tr("Max tokens:"), fimMaxTokensSpin_);

    layout->addLayout(form);

    auto *keyGroup = new QGroupBox(tr("API key"), fimPage_);
    auto *keyLayout = new QVBoxLayout(keyGroup);
    fimApiKeyEdit_ = new QLineEdit(keyGroup);
    fimApiKeyEdit_->setObjectName(QStringLiteral("settings-fim-api-key"));
    fimApiKeyEdit_->setEchoMode(QLineEdit::Password);
    fimApiKeyEdit_->setPlaceholderText(tr("write-only — the stored key is never shown"));
    fimApiKeyEdit_->setInputMethodHints(Qt::ImhHiddenText | Qt::ImhNoPredictiveText
                                        | Qt::ImhSensitiveData);
    keyLayout->addWidget(fimApiKeyEdit_);

    auto *keyRow = new QHBoxLayout();
    fimKeySaveButton_ = new QPushButton(tr("Save key"), keyGroup);
    fimKeySaveButton_->setObjectName(QStringLiteral("settings-fim-key-save"));
    fimKeyClearButton_ = new QPushButton(tr("Remove key file"), keyGroup);
    fimKeyClearButton_->setObjectName(QStringLiteral("settings-fim-key-clear"));
    keyRow->addWidget(fimKeySaveButton_);
    keyRow->addWidget(fimKeyClearButton_);
    keyRow->addStretch(1);
    keyLayout->addLayout(keyRow);

    fimKeySourceLabel_ = new QLabel(keyGroup);
    fimKeySourceLabel_->setObjectName(QStringLiteral("settings-fim-key-source"));
    fimKeySourceLabel_->setWordWrap(true);
    keyLayout->addWidget(fimKeySourceLabel_);

    auto *keyHint = new QLabel(
        tr("The key is never written to settings.toml and never logged. "
           "VEDIT_FIM_API_KEY takes priority; otherwise it is read from the owner-only "
           "(0600) file `fim_api_key` beside settings.toml."),
        keyGroup);
    keyHint->setWordWrap(true);
    keyLayout->addWidget(keyHint);
    layout->addWidget(keyGroup);

    auto *hint = new QLabel(
        tr("FIM is off by default: with no endpoint configured VEdit never touches the "
           "network."),
        fimPage_);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch(1);
    return fimPage_;
}

void SettingsPanel::reload()
{
    diagnostics_.clear();
    statusLabel_->setText(QCoreApplication::translate("VEdit", "Loading settings from %1 …").arg(client_->path()));
    client_->load([this](Snapshot snapshot) {
        if (!snapshot.completed) {
            addDiagnostic(snapshot.error.isEmpty() ? QCoreApplication::translate("VEdit", "settings could not be loaded")
                                                   : snapshot.error);
            statusLabel_->setText(
                QCoreApplication::translate("VEdit", "Settings unavailable; showing the built-in defaults."));
            refreshWidgets();
            return;
        }
        for (const QString &warning : snapshot.warnings) {
            addDiagnostic(warning);
        }
        entries_ = snapshot.entries;
        // T18: the snapshot as it was READ (not the preview) is the base for the
        // language-only write; a restart must not persist other previews.
        storedEntries_ = snapshot.entries;
        applyThemeFromEntries();
        applyKeymapFromEntries();
        applyToolEntries();
        applyFimEntries();
        applyAppearanceEntries();
        applyLintEntries();
        applyFormatEntries();
        refreshWidgets();
        statusLabel_->setText(
            snapshot.warnings.isEmpty()
                ? QCoreApplication::translate("VEdit", "Settings loaded from %1").arg(client_->path())
                : QCoreApplication::translate(
                      "VEdit",
                      "Settings loaded from %1 with %2 warning(s); the built-in "
                      "defaults were kept where the file is invalid.")
                      .arg(client_->path())
                      .arg(snapshot.warnings.size()));
    });
}

bool SettingsPanel::flush(int timeoutMs)
{
    return client_->flush(timeoutMs);
}

void SettingsPanel::applyThemeFromEntries()
{
    const QString stored = SettingsClient::valueOf(entries_, QString::fromLatin1(kThemeKey));
    if (themes_ == nullptr) {
        return;
    }
    // UI-Mosaic T3: the stored product theme is restored first (the shell reads
    // the same `uiTheme` key at startup). An empty value means "not chosen yet",
    // so whatever the session/startup path applied is kept.
    const QString storedUi = SettingsClient::valueOf(entries_, QString::fromLatin1(kUiThemeKey));
    if (!storedUi.isEmpty()) {
        // UI-Mosaic R1/M2: an id no product theme answers is treated as UNSET here
        // too (same rule as the startup path), so reloading Preferences keeps the
        // theme the shell is running instead of logging a failed re-apply.
        if (vedit::app::productThemeById(storedUi) == nullptr) {
            addDiagnostic(tr("Ignoring unknown interface theme '%1' (from %2)")
                              .arg(storedUi, QStringLiteral("settings.toml")));
        } else if (themes_->currentUiTheme() != storedUi) {
            QString uiError;
            if (!themes_->applyUiTheme(storedUi, &uiError)) {
                addDiagnostic(
                    tr("Interface theme '%1' could not be applied: %2").arg(storedUi, uiError));
            }
        }
    }
    if (stored.isEmpty()) {
        refreshThemeCombo();
        return;
    }
    QString error;
    if (!themes_->applyTheme(stored, &error)) {
        addDiagnostic(error);
        // Bad configuration must not leave the UI without a theme: fall back to
        // the built-in default and keep the diagnostic around.
        QString fallbackError;
        if (!themes_->applyTheme(QString(), &fallbackError)) {
            addDiagnostic(fallbackError);
        }
        statusLabel_->setText(QCoreApplication::translate("VEdit", "Theme '%1' is invalid; the default theme is "
                                             "applied instead.")
                                  .arg(stored));
        return;
    }
    refreshThemeCombo();
}

void SettingsPanel::applyKeymapFromEntries()
{
    if (commands_ == nullptr || commands_->store() == nullptr) {
        return;
    }
    const QString prefix = QString::fromLatin1(kKeymapPrefix);
    for (const Entry &entry : entries_) {
        if (!entry.key.startsWith(prefix)) {
            continue;
        }
        const QString command = entry.key.mid(prefix.size());
        if (command.isEmpty()) {
            continue;
        }
        const QKeySequence sequence(entry.value);
        if (sequence.isEmpty()) {
            commands_->store()->clearShortcut(command);
        } else {
            commands_->store()->setShortcut(command, sequence);
        }
    }
    commands_->refreshShortcuts();
}

void SettingsPanel::setTheme(const QString &name)
{
    if (themes_ == nullptr || name.isEmpty()) {
        return;
    }
    QString error;
    if (!themes_->applyTheme(name, &error)) {
        addDiagnostic(error);
        statusLabel_->setText(QCoreApplication::translate("VEdit", "Theme '%1' could not be applied.").arg(name));
        refreshWidgets();
        return;
    }
    SettingsClient::setValue(&entries_, QString::fromLatin1(kThemeKey), name);
    persist();
    refreshThemeCombo();
    statusLabel_->setText(QCoreApplication::translate("VEdit", "Theme '%1' applied and stored by the Rust service.")
                              .arg(name));
    emit themeChanged(name);
}

void SettingsPanel::setShortcut(const QString &commandId, const QKeySequence &sequence)
{
    if (commandId.isEmpty() || commands_ == nullptr || commands_->store() == nullptr) {
        return;
    }
    if (sequence.isEmpty()) {
        clearShortcut(commandId);
        return;
    }
    // RM-5b: never apply a sequence another owner already carries. Qt drops BOTH
    // actions of an ambiguous sequence, so "assign it anyway" is how a working
    // binding silently stops firing; the owner is named instead and the edit is
    // refused (the user can free it first, then assign).
    QStringList takenBy;
    for (const QString &id : commands_->shortcutOwnerIds(sequence)) {
        if (id == commandId) {
            continue;
        }
        QAction *owner = commands_->action(id);
        takenBy.append(tr("%1 (%2)").arg(owner != nullptr ? owner->text() : id, id));
    }
    for (const QString &label : commands_->foreignShortcutOwners(sequence)) {
        // RM-5c: the same seam now also carries the DESKTOP's global shortcuts
        // (the shell feeds desktop_shortcuts.h into the provider), so the owner
        // line says which one it is; no extra suffix is invented here.
        takenBy.append(label);
    }
    if (!takenBy.isEmpty()) {
        const QString message =
            tr("Shortcut %1 is already used by %2 — nothing was changed.")
                .arg(sequence.toString(QKeySequence::PortableText),
                     takenBy.join(QStringLiteral("; ")));
        statusLabel_->setText(message);
        addDiagnostic(message);
        refreshConflicts();
        return;
    }
    commands_->store()->setShortcut(commandId, sequence);
    commands_->refreshShortcuts();
    SettingsClient::setValue(&entries_,
                             QString::fromLatin1(kKeymapPrefix) + commandId,
                             sequence.toString());
    persist();
    refreshWidgets();
    statusLabel_->setText(QCoreApplication::translate("VEdit", "Shortcut for '%1' set to %2 and stored by the Rust "
                                         "service.")
                              .arg(commandId, sequence.toString()));
    emit shortcutsChanged();
}

void SettingsPanel::clearShortcut(const QString &commandId)
{
    if (commandId.isEmpty() || commands_ == nullptr || commands_->store() == nullptr) {
        return;
    }
    commands_->store()->clearShortcut(commandId);
    commands_->refreshShortcuts();
    SettingsClient::setValue(&entries_, QString::fromLatin1(kKeymapPrefix) + commandId, QString());
    persist();
    refreshWidgets();
    statusLabel_->setText(QCoreApplication::translate("VEdit", "Shortcut for '%1' cleared (default restored).")
                              .arg(commandId));
    emit shortcutsChanged();
}

QString SettingsPanel::shortcutText(const QString &commandId) const
{
    if (commands_ == nullptr) {
        return tr("(unbound)");
    }
    // RM-5c: a command may carry an EXTRA default (Ctrl+Y for redo, Shift+Alt+F
    // for format); the user must see all of them, not only the primary.
    QStringList sequences;
    for (const QKeySequence &sequence : commands_->effectiveShortcuts(commandId)) {
        if (!sequence.isEmpty()) {
            sequences.append(sequence.toString(QKeySequence::PortableText));
        }
    }
    return sequences.isEmpty() ? tr("(unbound)") : sequences.join(QStringLiteral(" / "));
}

QString SettingsPanel::defaultShortcutText(const QString &commandId) const
{
    if (commands_ == nullptr) {
        return tr("(none)");
    }
    const QKeySequence sequence = commands_->defaultShortcut(commandId);
    return sequence.isEmpty() ? tr("(none)") : sequence.toString(QKeySequence::PortableText);
}

QString SettingsPanel::shortcutState(const QString &commandId) const
{
    if (commands_ == nullptr) {
        return {};
    }
    if (commands_->hasUserOverride(commandId)) {
        return tr("custom");
    }
    if (commands_->effectiveShortcut(commandId).isEmpty()) {
        return tr("unbound");
    }
    return tr("default");
}

void SettingsPanel::resetShortcut(const QString &commandId)
{
    // Clearing the stored override is exactly "back to the registered default":
    // the action is refreshed from spec.defaultShortcut on the same call.
    clearShortcut(commandId);
}

void SettingsPanel::resetAllShortcuts()
{
    if (commands_ == nullptr || commands_->store() == nullptr) {
        return;
    }
    for (const QString &commandId : commands_->ids()) {
        commands_->store()->clearShortcut(commandId);
        SettingsClient::setValue(&entries_, QString::fromLatin1(kKeymapPrefix) + commandId,
                                 QString());
    }
    commands_->refreshShortcuts();
    persist();
    refreshWidgets();
    statusLabel_->setText(tr("Every shortcut is back to its default."));
    emit shortcutsChanged();
}

void SettingsPanel::applyEditorShortcut()
{
    const QString command = selectedCommand();
    if (command.isEmpty()) {
        statusLabel_->setText(QCoreApplication::translate("VEdit", "Select a command in the table first."));
        return;
    }
    setShortcut(command, shortcutEditor_->keySequence());
}

void SettingsPanel::persist()
{
    if (client_ == nullptr) {
        return;
    }
    if (deferred_) {
        // Preview only: the dialog decides when this becomes persistent. The FIM
        // key is part of the preview too — it is written by commitPending().
        dirty_ = true;
        statusLabel_->setText(QCoreApplication::translate("VEdit", "Preview — press Apply or OK to store it, "
                                             "Cancel to discard."));
        refreshWidgets();
        return;
    }
    persistTypedFimKey();
    // Mirror into the shortcut store the shell loads at startup (the existing Qt
    // persistence path); failure is a diagnostic, never fatal.
    if (commands_ != nullptr) {
        QString shortcutError;
        if (!commands_->saveShortcuts(&shortcutError) && !shortcutError.isEmpty()) {
            addDiagnostic(shortcutError);
        }
    }
    client_->save(entries_, [this](bool ok, const QString &error) {
        if (!ok) {
            addDiagnostic(error.isEmpty() ? QCoreApplication::translate("VEdit", "the Rust settings store rejected "
                                                           "the write")
                                          : error);
            statusLabel_->setText(QCoreApplication::translate("VEdit", "The settings change was applied but could not "
                                                 "be stored; see the diagnostics."));
            refreshWidgets();
            return;
        }
        statusLabel_->setText(QCoreApplication::translate("VEdit", "Stored by the Rust service in %1")
                                  .arg(client_->path()));
        refreshWidgets();
    });
}

void SettingsPanel::setDeferredPersistence(bool deferred)
{
    deferred_ = deferred;
    if (deferred) {
        snapshotOriginal();
        committedEntries_ = entries_;
        dirty_ = false;
    }
}
void SettingsPanel::snapshotOriginal()
{
    originalTheme_ = themes_ != nullptr ? themes_->currentTheme() : QString();
    originalShortcuts_.clear();
    if (commands_ == nullptr) {
        return;
    }
    for (const QString &command : commands_->ids()) {
        const QAction *action = commands_->action(command);
        if (action != nullptr) {
            originalShortcuts_.insert(command, action->shortcut());
        }
    }
}

void SettingsPanel::restoreOriginalShortcuts()
{
    if (commands_ == nullptr) {
        return;
    }
    if (commands_->store() != nullptr) {
        for (auto it = originalShortcuts_.constBegin(); it != originalShortcuts_.constEnd(); ++it) {
            if (it.value().isEmpty()) {
                commands_->store()->clearShortcut(it.key());
            } else {
                commands_->store()->setShortcut(it.key(), it.value());
            }
        }
        commands_->refreshShortcuts();
    }
}

void SettingsPanel::commitPending()
{
    if (!deferred_) {
        return;
    }
    // Collect whatever is on the pages right now (a field may not have emitted
    // its change signal yet), then write the preview through the Rust store and
    // the owner-only key file, and make the committed state the new snapshot.
    writeToolEntries(toolValues());
    writeFimEntries(fimValues());
    const bool wasDeferred = deferred_;
    deferred_ = false;
    persist();
    deferred_ = wasDeferred;
    dirty_ = false;
    committedEntries_ = entries_;
    snapshotOriginal();
    statusLabel_->setText(QCoreApplication::translate("VEdit", "Stored by the Rust service in %1").arg(client_->path()));
    refreshWidgets();
}

void SettingsPanel::revertPending()
{
    if (!deferred_) {
        return;
    }
    if (themes_ != nullptr && !originalTheme_.isEmpty()
        && themes_->currentTheme() != originalTheme_) {
        QString error;
        if (!themes_->applyTheme(originalTheme_, &error)) {
            addDiagnostic(error);
        }
    }
    restoreOriginalShortcuts();
    entries_ = committedEntries_;
    applyToolEntries();
    applyFimEntries();
    applyAppearanceEntries();
    applyLintEntries();
    applyFormatEntries();
    // A key typed into the write-only field is part of the preview: discarding
    // the preview discards it, and nothing was written to disk anyway.
    fimApiKeyEdit_->clear();
    dirty_ = false;
    statusLabel_->setText(QCoreApplication::translate("VEdit", "Changes discarded; nothing was stored."));
    refreshWidgets();
}

void SettingsPanel::refreshWidgets()
{
    refreshLintKnobStates();
    refreshThemeCombo();
    refreshKeyTable();
    refreshConflicts();
    refreshFimKeySource();
    diagnosticsLabel_->setText(
        diagnostics_.isEmpty()
            ? QCoreApplication::translate("VEdit", "No configuration problems reported by the Rust store.")
            : QCoreApplication::translate(
                  "VEdit",
                  "%1 configuration problem(s) — the built-in defaults were kept "
                  "where needed:")
                  .arg(diagnostics_.size()));
    diagnosticsView_->setPlainText(diagnostics_.join(QLatin1Char('\n')));
}

// --- Tools and FIM pages (item 10.2) ---------------------------------------

void SettingsPanel::persistFromWidgets()
{
    if (updating_) {
        return;
    }
    writeToolEntries(toolValues());
    writeFimEntries(fimValues());
    writeAppearanceEntries(appearanceValues());
    lintValues_ = lintValues();
    writeLintEntries(&entries_, lintValues_);
    writeFormatEntries(&entries_, formatValues());
    // The formatter and the lint job live in the diagnostics lane, which links no
    // settings code: these two holders are how the page's values reach them.
    setCurrentLintValues(lintValues_);
    setCurrentFormatValues(formatValues());
    refreshLintKnobStates();
    persist();
}

ToolValues SettingsPanel::toolValues() const
{
    ToolValues values;
    if (lspBackendCombo_ != nullptr) {
        values.lspBackend = lspBackendCombo_->currentData().toString();
    }
    if (lintEngineCombo_ != nullptr) {
        values.lintEngine = lintEngineCombo_->currentData().toString();
    }
    if (veribleLsEdit_ != nullptr) {
        values.veribleLs = veribleLsEdit_->text().trimmed();
    }
    if (slangEdit_ != nullptr) {
        values.slang = slangEdit_->text().trimmed();
    }
    if (slangServerEdit_ != nullptr) {
        values.slangServer = slangServerEdit_->text().trimmed();
    }
    if (veribleFormatEdit_ != nullptr) {
        values.veribleFormat = veribleFormatEdit_->text().trimmed();
    }
    if (veribleRulesEdit_ != nullptr) {
        values.veribleRules = veribleRulesEdit_->text().trimmed();
    }
    if (includeDirsEdit_ != nullptr) {
        values.includeDirs = includeDirsEdit_->toPlainText().trimmed();
    }
    if (definesEdit_ != nullptr) {
        values.defines = definesEdit_->toPlainText().trimmed();
    }
    return values;
}

void SettingsPanel::setToolValues(const ToolValues &values)
{
    applyToolValues(values);
    writeToolEntries(values);
    // UX batch 2 / A4 / §0.1 R1: the Lint page shows the SELECTED engine's set, so
    // a programmatic engine change has to swap the page the same way the combo
    // does. The half the page shows is folded into its own set first.
    lintValues_ = lintValues();
    lintBackend_ = lintBackendFromSetting(values.lintEngine);
    applyLintWidgets();
    persist();
}

void SettingsPanel::applyToolValues(const ToolValues &values)
{
    updating_ = true;
    if (lspBackendCombo_ != nullptr) {
        const int index = lspBackendCombo_->findData(values.lspBackend);
        lspBackendCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (lintEngineCombo_ != nullptr) {
        const int index = lintEngineCombo_->findData(values.lintEngine);
        lintEngineCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (veribleLsEdit_ != nullptr) {
        veribleLsEdit_->setText(values.veribleLs);
    }
    if (slangEdit_ != nullptr) {
        slangEdit_->setText(values.slang);
    }
    if (slangServerEdit_ != nullptr) {
        slangServerEdit_->setText(values.slangServer);
    }
    if (veribleFormatEdit_ != nullptr) {
        veribleFormatEdit_->setText(values.veribleFormat);
    }
    if (veribleRulesEdit_ != nullptr) {
        veribleRulesEdit_->setText(values.veribleRules);
    }
    if (includeDirsEdit_ != nullptr) {
        includeDirsEdit_->setPlainText(values.includeDirs);
    }
    if (definesEdit_ != nullptr) {
        definesEdit_->setPlainText(values.defines);
    }
    updating_ = false;
}

void SettingsPanel::writeToolEntries(const ToolValues &values)
{
    const auto key = [](const char *name) { return QString::fromLatin1(name); };
    SettingsClient::setValue(&entries_, key(keys::kLspBackend), values.lspBackend);
    SettingsClient::setValue(&entries_, key(keys::kLintEngine), values.lintEngine);
    SettingsClient::setValue(&entries_, key(keys::kVeribleLs), values.veribleLs);
    SettingsClient::setValue(&entries_, key(keys::kSlang), values.slang);
    SettingsClient::setValue(&entries_, key(keys::kSlangServer), values.slangServer);
    SettingsClient::setValue(&entries_, key(keys::kVeribleFormat), values.veribleFormat);
    SettingsClient::setValue(&entries_, key(keys::kVeribleRules), values.veribleRules);
    SettingsClient::setValue(&entries_, key(keys::kIncludeDirs), values.includeDirs);
    SettingsClient::setValue(&entries_, key(keys::kDefines), values.defines);
}

void SettingsPanel::applyToolEntries()
{
    const auto key = [](const char *name) { return QString::fromLatin1(name); };
    ToolValues values;
    values.lspBackend = keyText(entries_, keys::kLspBackend);
    if (values.lspBackend.isEmpty()) {
        values.lspBackend = key(keys::kLspBackendVerible);
    }
    values.lintEngine = keyText(entries_, keys::kLintEngine);
    if (values.lintEngine.isEmpty()) {
        values.lintEngine = key(keys::kLintEngineSlang);
    }
    values.veribleLs = keyText(entries_, keys::kVeribleLs);
    values.slang = keyText(entries_, keys::kSlang);
    values.slangServer = keyText(entries_, keys::kSlangServer);
    values.veribleFormat = keyText(entries_, keys::kVeribleFormat);
    values.veribleRules = keyText(entries_, keys::kVeribleRules);
    values.includeDirs = keyText(entries_, keys::kIncludeDirs);
    values.defines = keyText(entries_, keys::kDefines);
    applyToolValues(values);
    setLanguageSetting(SettingsClient::valueOf(entries_, i18n::languageKey()));
}

// --- T18 interface language (AC-4.1 / AC-4.2) ------------------------------

QString SettingsPanel::languageSetting() const
{
    if (languageCombo_ == nullptr) {
        return i18n::languageSetting(i18n::UiLanguage::Auto);
    }
    const QString value = languageCombo_->currentData().toString();
    return value.isEmpty() ? i18n::languageSetting(i18n::UiLanguage::Auto) : value;
}

QString SettingsPanel::storedLanguage() const
{
    const QString stored = SettingsClient::valueOf(storedEntries_, i18n::languageKey()).trimmed();
    return stored.isEmpty() ? i18n::languageSetting(i18n::UiLanguage::Auto) : stored;
}

void SettingsPanel::setLanguageSetting(const QString &setting)
{
    if (languageCombo_ == nullptr) {
        return;
    }
    const QString wanted = setting.trimmed().isEmpty()
                               ? i18n::languageSetting(i18n::UiLanguage::Auto)
                               : setting.trimmed();
    const int index = languageCombo_->findData(wanted);
    const bool blocked = languageCombo_->blockSignals(true);
    languageCombo_->setCurrentIndex(index >= 0 ? index : 0);
    languageCombo_->blockSignals(blocked);
    updateLanguageHint();
}

void SettingsPanel::updateLanguageHint()
{
    if (languageHint_ == nullptr || languageCombo_ == nullptr) {
        return;
    }
    if (languageSetting() != storedLanguage()) {
        languageHint_->setText(tr("Restart to apply."));
    } else {
        languageHint_->setText(tr("The interface language is applied on the next start."));
    }
}

bool SettingsPanel::storeLanguage(const QString &setting, QString *error)
{
    if (client_ == nullptr) {
        if (error != nullptr) {
            *error = tr("The settings store is unavailable.");
        }
        return false;
    }
    const QString normalized = setting.trimmed().isEmpty()
                                   ? i18n::languageSetting(i18n::UiLanguage::Auto)
                                   : setting.trimmed();
    // Base on the snapshot as READ from the store, never on the preview: storing
    // the language must not smuggle the dialog's other previews into the file.
    QVector<Entry> base = storedEntries_;
    SettingsClient::setValue(&base, i18n::languageKey(), normalized);
    bool answered = false;
    bool stored = false;
    QString failure;
    client_->save(base, [&answered, &stored, &failure](bool ok, const QString &message) {
        answered = true;
        stored = ok;
        failure = message;
    });
    if (!client_->flush()) {
        if (error != nullptr) {
            *error = tr("The settings store did not answer.");
        }
        return false;
    }
    if (!answered || !stored) {
        if (error != nullptr) {
            *error = failure.isEmpty() ? tr("The settings store refused the write.") : failure;
        }
        return false;
    }
    entries_ = base;
    committedEntries_ = base;
    storedEntries_ = base;
    updateLanguageHint();
    return true;
}

void SettingsPanel::requestLanguageRestart()
{
    const QString selected = languageSetting();
    if (selected == storedLanguage()) {
        languageStatus_ = tr("The interface language is already the stored one.");
        if (statusLabel_ != nullptr) {
            statusLabel_->setText(languageStatus_);
        }
        return;
    }
    const i18n::PersistLanguage persist = [this, selected](QString *error) {
        return storeLanguage(selected, error);
    };
    QString error;
    const i18n::RestartOutcome outcome = i18n::requestRestart(selected, persist, &error);
    switch (outcome) {
    case i18n::RestartOutcome::Restarted:
        languageStatus_ = tr("Restarting to apply the interface language…");
        break;
    case i18n::RestartOutcome::Cancelled:
        // The shell put the same sentence on its status bar; the modal dialog
        // covers that bar, so the page repeats it (AC-4.2).
        languageStatus_ = tr("Language not switched: restart cancelled");
        break;
    case i18n::RestartOutcome::Failed:
        languageStatus_ =
            error.isEmpty() ? tr("The interface language could not be changed.") : error;
        addDiagnostic(languageStatus_);
        break;
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(languageStatus_);
    }
    updateLanguageHint();
}

FimValues SettingsPanel::fimValues() const
{
    FimValues values;
    if (fimEnabledCheck_ != nullptr) {
        values.enabled = fimEnabledCheck_->isChecked();
    }
    if (fimGhostCheck_ != nullptr) {
        values.ghostText = fimGhostCheck_->isChecked();
    }
    if (fimProtocolCombo_ != nullptr) {
        values.protocol = fimProtocolCombo_->currentData().toString();
    }
    if (fimEndpointEdit_ != nullptr) {
        values.endpoint = fimEndpointEdit_->text().trimmed();
    }
    if (fimModelEdit_ != nullptr) {
        values.model = fimModelEdit_->text().trimmed();
    }
    if (fimTimeoutSpin_ != nullptr) {
        values.timeoutMs = fimTimeoutSpin_->value();
    }
    if (fimMaxTokensSpin_ != nullptr) {
        values.maxTokens = fimMaxTokensSpin_->value();
    }
    // apiKey is deliberately absent: the field is write-only and the key never
    // travels through a value struct.
    return values;
}

void SettingsPanel::setFimValues(const FimValues &values)
{
    applyFimValues(values);
    writeFimEntries(values);
    persist();
}

void SettingsPanel::applyFimValues(const FimValues &values)
{
    updating_ = true;
    if (fimEnabledCheck_ != nullptr) {
        fimEnabledCheck_->setChecked(values.enabled);
    }
    if (fimGhostCheck_ != nullptr) {
        fimGhostCheck_->setChecked(values.ghostText);
    }
    if (fimProtocolCombo_ != nullptr) {
        const int index = fimProtocolCombo_->findData(values.protocol);
        fimProtocolCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (fimEndpointEdit_ != nullptr) {
        fimEndpointEdit_->setText(values.endpoint);
    }
    if (fimModelEdit_ != nullptr) {
        fimModelEdit_->setText(values.model);
    }
    if (fimTimeoutSpin_ != nullptr) {
        fimTimeoutSpin_->setValue(values.timeoutMs);
    }
    if (fimMaxTokensSpin_ != nullptr) {
        fimMaxTokensSpin_->setValue(values.maxTokens);
    }
    // The key field is only ever written by the user; a page load/revert CLEARS
    // it, so no value struct can echo a secret into the UI.
    if (fimApiKeyEdit_ != nullptr) {
        fimApiKeyEdit_->clear();
    }
    updating_ = false;
}

void SettingsPanel::writeFimEntries(const FimValues &values)
{
    const auto key = [](const char *name) { return QString::fromLatin1(name); };
    SettingsClient::setValue(&entries_, key(keys::kFimEnabled),
                             values.enabled ? QStringLiteral("true") : QStringLiteral("false"));
    SettingsClient::setValue(&entries_, key(keys::kFimProtocol), values.protocol);
    SettingsClient::setValue(&entries_, key(keys::kFimEndpoint), values.endpoint);
    SettingsClient::setValue(&entries_, key(keys::kFimModel), values.model);
    SettingsClient::setValue(&entries_, key(keys::kFimTimeoutMs),
                             QString::number(values.timeoutMs));
    SettingsClient::setValue(&entries_, key(keys::kFimMaxTokens),
                             QString::number(values.maxTokens));
    SettingsClient::setValue(&entries_, key(keys::kFimGhostText),
                             values.ghostText ? QStringLiteral("true") : QStringLiteral("false"));
}

void SettingsPanel::applyFimEntries()
{
    const auto key = [](const char *name) { return QString::fromLatin1(name); };
    FimValues values;
    values.enabled = keyBool(entries_, keys::kFimEnabled);
    values.ghostText = keyBool(entries_, keys::kFimGhostText);
    values.protocol = keyText(entries_, keys::kFimProtocol);
    if (values.protocol.isEmpty()) {
        values.protocol = key(keys::kFimProtocolOpenAi);
    }
    values.endpoint = keyText(entries_, keys::kFimEndpoint);
    values.model = keyText(entries_, keys::kFimModel);
    values.timeoutMs = keyInt(entries_, keys::kFimTimeoutMs, kDefaultTimeoutMs);
    values.maxTokens = keyInt(entries_, keys::kFimMaxTokens, kDefaultMaxTokens);
    applyFimValues(values);

    // A plaintext key can only come from an older build or a hand edit. It is
    // never used — and it is dropped here, so the next save scrubs the file.
    for (int index = entries_.size() - 1; index >= 0; --index) {
        if (entries_.at(index).key == QStringLiteral("fim.api_key")) {
            entries_.removeAt(index);
            addDiagnostic(tr("settings.toml held a plaintext `fim.api_key`; it is ignored and "
                             "will be removed on the next save — use %1 or the owner-only key "
                             "file instead.")
                              .arg(QString::fromLatin1(keys::kFimApiKeyEnvironment)));
        }
    }
}

QString SettingsPanel::fimKeyFilePath() const
{
    const QFileInfo info(client_->path());
    return info.absolutePath() + QLatin1Char('/') + QString::fromLatin1(keys::kFimApiKeyFile);
}

QString SettingsPanel::fimKeySource() const
{
    if (!qEnvironmentVariable(keys::kFimApiKeyEnvironment).trimmed().isEmpty()) {
        return QStringLiteral("environment");
    }
    const QString path = fimKeyFilePath();
    if (!QFileInfo::exists(path)) {
        return QStringLiteral("none");
    }
    return ((QFile::permissions(path) & loosePermissions()) != 0) ? QStringLiteral("insecure-file")
                                                                 : QStringLiteral("user-file");
}

bool SettingsPanel::fimKeyStored() const
{
    const QString source = fimKeySource();
    return source == QStringLiteral("environment") || source == QStringLiteral("user-file");
}

void SettingsPanel::refreshFimKeySource()
{
    if (fimKeySourceLabel_ == nullptr) {
        return;
    }
    const QString source = fimKeySource();
    if (source == QStringLiteral("environment")) {
        fimKeySourceLabel_->setStyleSheet(QString());
        fimKeySourceLabel_->setText(
            tr("Key source: environment variable %1 — it takes priority and is never written to "
               "disk.")
                .arg(QString::fromLatin1(keys::kFimApiKeyEnvironment)));
    } else if (source == QStringLiteral("user-file")) {
        fimKeySourceLabel_->setStyleSheet(QString());
        fimKeySourceLabel_->setText(
            tr("Key source: owner-only key file (0600) %1").arg(fimKeyFilePath()));
    } else if (source == QStringLiteral("insecure-file")) {
        fimKeySourceLabel_->setStyleSheet(QStringLiteral("color: #a33; font-weight: bold;"));
        fimKeySourceLabel_->setText(
            tr("Key source: %1 is readable by other users, so it is IGNORED. Run "
               "`chmod 600` on it, or use %2.")
                .arg(fimKeyFilePath(), QString::fromLatin1(keys::kFimApiKeyEnvironment)));
    } else {
        fimKeySourceLabel_->setStyleSheet(QString());
        fimKeySourceLabel_->setText(
            tr("Key source: none — set %1 or save a key here.")
                .arg(QString::fromLatin1(keys::kFimApiKeyEnvironment)));
    }
}

bool SettingsPanel::persistTypedFimKey()
{
    if (fimApiKeyEdit_ == nullptr) {
        return true;
    }
    const QString typed = fimApiKeyEdit_->text().trimmed();
    if (typed.isEmpty()) {
        return true;
    }
    if (!storeFimApiKey(typed)) {
        return false;
    }
    // The value is gone from memory as soon as it has reached the file: the field
    // is write-only, never a place a key lives.
    fimApiKeyEdit_->clear();
    return true;
}

bool SettingsPanel::writeFimKeyFile(const QString &key)
{
    const QString path = fimKeyFilePath();
    // A fresh profile has no settings directory yet (the store creates it on its
    // own, asynchronous save).
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        addDiagnostic(tr("cannot create the settings directory %1; the API key was NOT stored")
                          .arg(directory));
        return false;
    }
    QByteArray bytes = key.toUtf8();
    QString reason;
    const bool written = writeOwnerOnlyFile(path, bytes, &reason);
    bytes.fill('\0');
    if (!written) {
        addDiagnostic(tr("cannot write the API key file %1: %2; the API key was NOT stored")
                          .arg(path, reason));
        return false;
    }
    refreshFimKeySource();
    return true;
}

bool SettingsPanel::storeFimApiKey(const QString &key)
{
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) {
        addDiagnostic(tr("the API key is empty; nothing was stored"));
        return false;
    }
    return writeFimKeyFile(trimmed);
}

void SettingsPanel::clearFimKey()
{
    const QString path = fimKeyFilePath();
    const bool removed = !QFileInfo::exists(path) || QFile::remove(path);
    if (!removed) {
        addDiagnostic(tr("cannot remove the API key file %1").arg(path));
    }
    if (fimApiKeyEdit_ != nullptr) {
        fimApiKeyEdit_->clear();
    }
    refreshFimKeySource();
    refreshWidgets();
    if (!removed) {
        return;
    }
    statusLabel_->setText(fimKeySource() == QStringLiteral("environment")
                              ? tr("The owner-only key file was removed; %1 is still set and "
                                   "still wins.")
                                    .arg(QString::fromLatin1(keys::kFimApiKeyEnvironment))
                              : tr("The API key file was removed; FIM has no key."));
}

void SettingsPanel::refreshThemeCombo()
{
    if (themeCombo_ == nullptr || themes_ == nullptr) {
        return;
    }
    const QString current = themes_->currentTheme();
    const QStringList names = themes_->availableThemes();
    themeCombo_->blockSignals(true);
    themeCombo_->clear();
    themeCombo_->addItems(names);
    const int index = themeCombo_->findText(current);
    if (index >= 0) {
        themeCombo_->setCurrentIndex(index);
    } else if (!current.isEmpty()) {
        themeCombo_->addItem(current);
        themeCombo_->setCurrentIndex(themeCombo_->count() - 1);
    }
    themeCombo_->blockSignals(false);
    // The product-theme combo (UI-Mosaic T3) follows the SAME live state: a
    // switch made from the View menu or the toolbar is reflected here without a
    // second source of truth.
    auto *uiCombo = findChild<QComboBox *>(QStringLiteral("settings-ui-theme"));
    if (uiCombo != nullptr) {
        const int uiIndex = uiCombo->findData(themes_->currentUiTheme());
        uiCombo->blockSignals(true);
        if (uiIndex >= 0) {
            uiCombo->setCurrentIndex(uiIndex);
        }
        uiCombo->blockSignals(false);
    }
}

void SettingsPanel::refreshKeyTable()
{
    if (keyTable_ == nullptr) {
        return;
    }
    const QString selected = selectedCommand();
    keyTable_->blockSignals(true);
    keyTable_->setRowCount(0);
    if (commands_ == nullptr) {
        keyTable_->blockSignals(false);
        return;
    }
    const QString prefix = QString::fromLatin1(kKeymapPrefix);
    int row = 0;
    for (const QString &command : commands_->ids()) {
        const QAction *action = commands_->action(command);
        if (action == nullptr) {
            continue;
        }
        keyTable_->insertRow(row);
        auto *nameItem = new QTableWidgetItem(
            QStringLiteral("%1 (%2)").arg(action->text(), command));
        keyTable_->setItem(row, 0, nameItem);
        keyTable_->setItem(row, 1, new QTableWidgetItem(shortcutText(command)));
        keyTable_->setItem(row, 2, new QTableWidgetItem(defaultShortcutText(command)));
        keyTable_->setItem(row, 3, new QTableWidgetItem(shortcutState(command)));
        if (command == selected) {
            keyTable_->selectRow(row);
        }
        ++row;
    }
    keyTable_->blockSignals(false);
}

void SettingsPanel::refreshConflicts()
{
    if (conflictLabel_ == nullptr) {
        return;
    }
    const QStringList found = conflicts();
    if (found.isEmpty()) {
        conflictLabel_->setStyleSheet(QString());
        conflictLabel_->setText(QCoreApplication::translate("VEdit", "No conflicting shortcuts."));
        return;
    }
    conflictLabel_->setStyleSheet(QStringLiteral("color: #a33; font-weight: bold;"));
    conflictLabel_->setText(QCoreApplication::translate("VEdit", "Conflicting shortcuts: %1")
                                .arg(found.join(QStringLiteral(" | "))));
}

QStringList SettingsPanel::conflicts() const
{
    QStringList result;
    if (commands_ == nullptr) {
        return result;
    }
    QHash<QString, QStringList> bySequence;
    for (const QString &command : commands_->ids()) {
        const QAction *action = commands_->action(command);
        if (action == nullptr) {
            continue;
        }
        const QString sequence = action->shortcut().toString();
        if (sequence.isEmpty()) {
            continue;
        }
        bySequence[sequence].append(command);
    }
    for (auto it = bySequence.constBegin(); it != bySequence.constEnd(); ++it) {
        if (it.value().size() > 1) {
            result.append(QStringLiteral("%1: %2").arg(it.key(), it.value().join(QStringLiteral(", "))));
        }
    }
    // RM-5b: a registry binding that the LIVE EDITOR also carries is equally
    // dead (Qt drops both). The shell normally strips the editor's copy of a
    // shell-owned sequence, so this only shows a real, unexpected collision.
    for (const QString &command : commands_->ids()) {
        const QKeySequence sequence = commands_->effectiveShortcut(command);
        if (sequence.isEmpty()) {
            continue;
        }
        for (const QString &foreign : commands_->foreignShortcutOwners(sequence)) {
            result.append(tr("%1: %2 also used by the editor (%3)")
                              .arg(sequence.toString(QKeySequence::PortableText), command, foreign));
        }
    }
    return result;
}

int SettingsPanel::conflictCount() const
{
    return conflicts().size();
}

QStringList SettingsPanel::diagnostics() const
{
    return diagnostics_;
}

QString SettingsPanel::statusText() const
{
    return statusLabel_ != nullptr ? statusLabel_->text() : QString();
}

QString SettingsPanel::currentTheme() const
{
    return themes_ != nullptr ? themes_->currentTheme() : QString();
}

void SettingsPanel::addDiagnostic(const QString &text)
{
    if (!text.isEmpty()) {
        diagnostics_.append(text);
    }
}

// --- UX batch 2 / A4: appearance ------------------------------------------

namespace {

QString translated(const char *text)
{
    return QCoreApplication::translate("VEdit", text);
}

// The one widget-value bridge the three new pages share: every knob is stored as
// a string, and "" always means "no value configured" (the tool's own default).
QString widgetValue(const QWidget *widget)
{
    if (const auto *combo = qobject_cast<const QComboBox *>(widget)) {
        return combo->currentData().toString().trimmed();
    }
    if (const auto *spin = qobject_cast<const QSpinBox *>(widget)) {
        return spin->value() == 0 ? QString() : QString::number(spin->value());
    }
    if (const auto *edit = qobject_cast<const QLineEdit *>(widget)) {
        return edit->text().trimmed();
    }
    if (const auto *plain = qobject_cast<const QPlainTextEdit *>(widget)) {
        return plain->toPlainText().trimmed();
    }
    return QString();
}

void setWidgetValue(QWidget *widget, const QString &value)
{
    if (auto *combo = qobject_cast<QComboBox *>(widget)) {
        const int index = combo->findData(value);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        return;
    }
    if (auto *spin = qobject_cast<QSpinBox *>(widget)) {
        bool ok = false;
        const int number = value.toInt(&ok);
        spin->setValue(ok ? number : 0);
        return;
    }
    if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
        edit->setText(value);
        return;
    }
    if (auto *plain = qobject_cast<QPlainTextEdit *>(widget)) {
        plain->setPlainText(value);
    }
}

// One Format-page knob, built from its table entry.
QWidget *makeFormatKnobWidget(const FormatKnobSpec &knob, QWidget *parent)
{
    const QString objectName =
        QStringLiteral("settings-format-%1").arg(QString::fromLatin1(knob.flag));
    switch (knob.kind) {
    case FormatKnobKind::Integer: {
        auto *spin = new QSpinBox(parent);
        spin->setObjectName(objectName);
        const int maximum = QString::fromLatin1(knob.flag) == QStringLiteral("column_limit") ? 1000 : 16;
        spin->setRange(0, maximum);
        // 0 is not a verible value for any of these knobs, so it can carry the
        // "not set" state without ambiguity.
        spin->setSpecialValueText(translated("Default (verible)"));
        return spin;
    }
    case FormatKnobKind::Boolean:
    case FormatKnobKind::LineTerminator:
    case FormatKnobKind::Alignment:
    case FormatKnobKind::Indentation: {
        auto *combo = new QComboBox(parent);
        combo->setObjectName(objectName);
        const auto add = [combo](const QString &text, const QString &data) {
            combo->addItem(text, data);
        };
        switch (knob.kind) {
        case FormatKnobKind::Boolean:
            add(translated("Default (verible)"), QString());
            add(QStringLiteral("true"), QStringLiteral("true"));
            add(QStringLiteral("false"), QStringLiteral("false"));
            break;
        case FormatKnobKind::LineTerminator:
            add(translated("Default (verible: auto)"), QString());
            add(QStringLiteral("CRLF"), QStringLiteral("CRLF"));
            add(QStringLiteral("LF"), QStringLiteral("LF"));
            add(QStringLiteral("auto"), QStringLiteral("auto"));
            break;
        case FormatKnobKind::Alignment:
            add(translated("Default (verible: infer)"), QString());
            add(QStringLiteral("align"), QStringLiteral("align"));
            add(QStringLiteral("flush-left"), QStringLiteral("flush-left"));
            add(QStringLiteral("preserve"), QStringLiteral("preserve"));
            add(QStringLiteral("infer"), QStringLiteral("infer"));
            break;
        case FormatKnobKind::Indentation:
            add(translated("Default (verible: wrap)"), QString());
            add(QStringLiteral("indent"), QStringLiteral("indent"));
            add(QStringLiteral("wrap"), QStringLiteral("wrap"));
            break;
        case FormatKnobKind::Integer:
            break;
        }
        return combo;
    }
    }
    return nullptr;
}

} // namespace

QWidget *SettingsPanel::buildAppearancePage()
{
    appearancePage_ = new QWidget(this);
    appearancePage_->setObjectName(QStringLiteral("settings-page-appearance"));
    auto *layout = new QVBoxLayout(appearancePage_);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *group = new QGroupBox(tr("Interface"), appearancePage_);
    group->setObjectName(QStringLiteral("settings-appearance-group"));
    auto *form = new QFormLayout(group);

    fontPointSizeSpin_ = new QSpinBox(group);
    fontPointSizeSpin_->setObjectName(QStringLiteral("settings-appearance-font-size"));
    fontPointSizeSpin_->setRange(0, ui_scale::kMaxFontPointSize);
    fontPointSizeSpin_->setSpecialValueText(tr("Follow the system"));
    fontPointSizeSpin_->setToolTip(
        tr("ui.font_point_size: application font size in points. Applied immediately; "
           "\"Follow the system\" stores no value at all."));
    form->addRow(tr("Font size:"), fontPointSizeSpin_);

    scalePercentCombo_ = new QComboBox(group);
    scalePercentCombo_->setObjectName(QStringLiteral("settings-appearance-scale"));
    for (const int percent : ui_scale::kScalePercents) {
        scalePercentCombo_->addItem(QStringLiteral("%1 %").arg(percent), percent);
    }
    scalePercentCombo_->setToolTip(
        tr("ui.scale_percent: interface zoom. Qt reads QT_SCALE_FACTOR once, when the "
           "application is constructed, so this takes effect on the next start."));
    form->addRow(tr("Interface scale:"), scalePercentCombo_);

    scaleHint_ = new QLabel(group);
    scaleHint_->setObjectName(QStringLiteral("settings-appearance-scale-hint"));
    scaleHint_->setWordWrap(true);
    form->addRow(scaleHint_);
    layout->addWidget(group);

    auto *fontHint = new QLabel(
        tr("The font applies to the whole interface immediately. The editor's own font "
           "is a KTextEditor setting and belongs to the editor lane."),
        appearancePage_);
    fontHint->setObjectName(QStringLiteral("settings-appearance-font-hint"));
    fontHint->setWordWrap(true);
    layout->addWidget(fontHint);
    layout->addStretch(1);
    return appearancePage_;
}

QWidget *SettingsPanel::buildLintPage()
{
    lintPage_ = new QWidget(this);
    lintPage_->setObjectName(QStringLiteral("settings-page-lint"));
    auto *layout = new QVBoxLayout(lintPage_);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *intro = new QLabel(
        tr("The options of the selected lint engine (Tools → Lint engine). The page shows "
           "one set of controls; both engines keep their own settings, so switching the "
           "engine never loses the other one."),
        lintPage_);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    lintVeribleGroup_ = new QGroupBox(tr("verible-verilog-lint"), lintPage_);
    lintVeribleGroup_->setObjectName(QStringLiteral("settings-lint-verible"));
    auto *veribleForm = new QFormLayout(lintVeribleGroup_);
    // The greying alone is easy to miss in a flat style, so each group also says
    // in words whether it is the active engine's set and what is applied.
    lintVeribleStatus_ = new QLabel(lintVeribleGroup_);
    lintVeribleStatus_->setObjectName(QStringLiteral("settings-lint-verible-status"));
    lintVeribleStatus_->setWordWrap(true);
    veribleForm->addRow(lintVeribleStatus_);

    lintRulesetCombo_ = new QComboBox(lintVeribleGroup_);
    lintRulesetCombo_->setObjectName(QStringLiteral("settings-lint-verible-ruleset"));
    lintRulesetCombo_->addItem(tr("No flag (verible default)"), QString());
    for (const char *value : {keys::kRulesetDefault, keys::kRulesetAll, keys::kRulesetNone}) {
        lintRulesetCombo_->addItem(QString::fromLatin1(value), QString::fromLatin1(value));
    }
    veribleForm->addRow(tr("Rule set (--ruleset):"), lintRulesetCombo_);

    lintRulesEdit_ = new QLineEdit(lintVeribleGroup_);
    lintRulesEdit_->setObjectName(QStringLiteral("settings-lint-verible-rules"));
    lintRulesEdit_->setPlaceholderText(tr("empty: verible's built-in rule set"));
    veribleForm->addRow(tr("Rules (--rules):"), lintRulesEdit_);

    veribleForm->addRow(
        tr("Rules file (--rules_config):"),
        makePathRow(lintVeribleGroup_, &lintRulesConfigEdit_,
                    QStringLiteral("settings-lint-verible-rules-config"),
                    QStringLiteral(".rules.verible_lint")));

    lintWaiverFilesEdit_ = new QPlainTextEdit(lintVeribleGroup_);
    lintWaiverFilesEdit_->setObjectName(QStringLiteral("settings-lint-verible-waivers"));
    lintWaiverFilesEdit_->setPlaceholderText(tr("empty: no waiver files (one path per line)"));
    lintWaiverFilesEdit_->setMaximumHeight(70);
    veribleForm->addRow(tr("Waiver files (--waiver_files):"), lintWaiverFilesEdit_);

    lintAutofixCombo_ = new QComboBox(lintVeribleGroup_);
    lintAutofixCombo_->setObjectName(QStringLiteral("settings-lint-verible-autofix"));
    lintAutofixCombo_->addItem(tr("No flag (verible default: no)"), QString());
    for (const char *value : {keys::kAutofixNo, keys::kAutofixPatch, keys::kAutofixPatchInteractive,
                              keys::kAutofixInplace, keys::kAutofixInplaceInteractive,
                              keys::kAutofixGenerateWaiver}) {
        lintAutofixCombo_->addItem(QString::fromLatin1(value), QString::fromLatin1(value));
    }
    veribleForm->addRow(tr("Autofix (--autofix):"), lintAutofixCombo_);
    layout->addWidget(lintVeribleGroup_);

    lintSlangGroup_ = new QGroupBox(tr("slang"), lintPage_);
    lintSlangGroup_->setObjectName(QStringLiteral("settings-lint-slang"));
    auto *slangForm = new QFormLayout(lintSlangGroup_);
    lintSlangStatus_ = new QLabel(lintSlangGroup_);
    lintSlangStatus_->setObjectName(QStringLiteral("settings-lint-slang-status"));
    lintSlangStatus_->setWordWrap(true);
    slangForm->addRow(lintSlangStatus_);

    lintSlangWarningsEdit_ = new QPlainTextEdit(lintSlangGroup_);
    lintSlangWarningsEdit_->setObjectName(QStringLiteral("settings-lint-slang-warnings"));
    lintSlangWarningsEdit_->setPlaceholderText(
        tr("empty: slang's own selection (one -W value per line, e.g. everything)"));
    lintSlangWarningsEdit_->setMaximumHeight(70);
    slangForm->addRow(tr("Warnings (-W):"), lintSlangWarningsEdit_);

    lintSlangErrorLimitEdit_ = new QLineEdit(lintSlangGroup_);
    lintSlangErrorLimitEdit_->setObjectName(QStringLiteral("settings-lint-slang-error-limit"));
    lintSlangErrorLimitEdit_->setPlaceholderText(tr("empty: slang's own limit"));
    lintSlangErrorLimitEdit_->setValidator(new QIntValidator(0, 1000000, lintSlangErrorLimitEdit_));
    slangForm->addRow(tr("Error limit (--error-limit):"), lintSlangErrorLimitEdit_);
    layout->addWidget(lintSlangGroup_);

    lintTransportHint_ = new QLabel(lintPage_);
    lintTransportHint_->setObjectName(QStringLiteral("settings-lint-transport-hint"));
    lintTransportHint_->setWordWrap(true);
    layout->addWidget(lintTransportHint_);

    lintKnobWidgets_.clear();
    lintKnobWidgets_.insert(static_cast<int>(LintKnob::Ruleset), lintRulesetCombo_);
    lintKnobWidgets_.insert(static_cast<int>(LintKnob::Rules), lintRulesEdit_);
    lintKnobWidgets_.insert(static_cast<int>(LintKnob::RulesConfig), lintRulesConfigEdit_);
    lintKnobWidgets_.insert(static_cast<int>(LintKnob::WaiverFiles), lintWaiverFilesEdit_);
    lintKnobWidgets_.insert(static_cast<int>(LintKnob::Autofix), lintAutofixCombo_);
    lintKnobWidgets_.insert(static_cast<int>(LintKnob::SlangWarnings), lintSlangWarningsEdit_);
    lintKnobWidgets_.insert(static_cast<int>(LintKnob::SlangErrorLimit), lintSlangErrorLimitEdit_);
    layout->addStretch(1);
    return lintPage_;
}

QWidget *SettingsPanel::buildFormatPage()
{
    formatPage_ = new QWidget(this);
    formatPage_->setObjectName(QStringLiteral("settings-page-format"));
    auto *layout = new QVBoxLayout(formatPage_);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *intro = new QLabel(
        tr("Options for verible-verilog-format. An empty value means \"use the "
           "formatter's own default\": no flag is passed at all, so an untouched page "
           "formats exactly as before."),
        formatPage_);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *basic = new QGroupBox(tr("Formatting"), formatPage_);
    basic->setObjectName(QStringLiteral("settings-format-basic"));
    auto *basicForm = new QFormLayout(basic);
    formatKnobWidgets_.clear();
    for (const FormatKnobSpec &knob : kFormatKnobs) {
        if (knob.advanced) {
            continue;
        }
        QWidget *widget = makeFormatKnobWidget(knob, basic);
        if (widget == nullptr) {
            continue;
        }
        widget->setToolTip(tr(knob.hint));
        basicForm->addRow(tr(knob.label), widget);
        formatKnobWidgets_.insert(QString::fromLatin1(knob.key), widget);
    }
    layout->addWidget(basic);

    formatAdvancedButton_ = new QToolButton(formatPage_);
    formatAdvancedButton_->setObjectName(QStringLiteral("settings-format-advanced-toggle"));
    formatAdvancedButton_->setText(tr("Advanced options"));
    formatAdvancedButton_->setCheckable(true);
    formatAdvancedButton_->setChecked(false);
    formatAdvancedButton_->setArrowType(Qt::RightArrow);
    formatAdvancedButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    layout->addWidget(formatAdvancedButton_, 0, Qt::AlignLeft);

    formatAdvancedContainer_ = new QWidget(formatPage_);
    formatAdvancedContainer_->setObjectName(QStringLiteral("settings-format-advanced"));
    auto *advancedForm = new QFormLayout(formatAdvancedContainer_);
    for (const FormatKnobSpec &knob : kFormatKnobs) {
        if (!knob.advanced) {
            continue;
        }
        QWidget *widget = makeFormatKnobWidget(knob, formatAdvancedContainer_);
        if (widget == nullptr) {
            continue;
        }
        widget->setToolTip(tr(knob.hint));
        advancedForm->addRow(tr(knob.label), widget);
        formatKnobWidgets_.insert(QString::fromLatin1(knob.key), widget);
    }
    formatAdvancedContainer_->setVisible(false);
    layout->addWidget(formatAdvancedContainer_);

    auto *actions = new QHBoxLayout();
    formatRestoreButton_ = new QPushButton(tr("Restore defaults"), formatPage_);
    formatRestoreButton_->setObjectName(QStringLiteral("settings-format-restore"));
    formatRestoreButton_->setToolTip(
        tr("Clears every format option, so the formatter's own defaults apply and no "
           "flag is passed."));
    actions->addWidget(formatRestoreButton_);
    actions->addStretch(1);
    layout->addLayout(actions);
    layout->addStretch(1);
    return formatPage_;
}

SettingsPanel::AppearanceValues SettingsPanel::appearanceValues() const
{
    AppearanceValues values;
    if (fontPointSizeSpin_ != nullptr) {
        values.fontPointSize = fontPointSizeSpin_->value();
    }
    if (scalePercentCombo_ != nullptr) {
        bool ok = false;
        const int percent = scalePercentCombo_->currentData().toInt(&ok);
        values.scalePercent = ok && ui_scale::validScalePercent(percent) ? percent : 100;
    }
    return values;
}

void SettingsPanel::applyAppearanceEntries()
{
    updating_ = true;
    bool ok = false;
    const QString storedSize =
        SettingsClient::valueOf(entries_, QString::fromLatin1(keys::kUiFontPointSize));
    int pointSize = storedSize.toInt(&ok);
    if (!ok || !ui_scale::validFontPointSize(pointSize)) {
        pointSize = 0;
    }
    if (fontPointSizeSpin_ != nullptr) {
        fontPointSizeSpin_->setValue(pointSize);
    }
    const QString storedScale =
        SettingsClient::valueOf(entries_, QString::fromLatin1(keys::kUiScalePercent));
    const int percent = storedScale.toInt(&ok);
    const int scale = ok && ui_scale::validScalePercent(percent) ? percent : 100;
    if (scalePercentCombo_ != nullptr) {
        const int index = scalePercentCombo_->findData(scale);
        scalePercentCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    updating_ = false;

    const QString factor = ui_scale::scaleFactor(scale);
    if (scaleHint_ != nullptr) {
        scaleHint_->setText(
            factor.isEmpty()
                ? tr("Scale 100 %: no QT_SCALE_FACTOR override is set.")
                : tr("Scale %1 %: the next start sets QT_SCALE_FACTOR=%2 — restart VEdit "
                     "to apply it.")
                      .arg(scale)
                      .arg(factor));
    }
    applyFontPointSize(pointSize);
}

void SettingsPanel::writeAppearanceEntries(const AppearanceValues &values)
{
    const auto key = [](const char *name) { return QString::fromLatin1(name); };
    // A default value REMOVES its key: "follow the system" / "100 %" must leave
    // no residue in settings.toml, exactly like the format knobs.
    SettingsClient::setValue(&entries_, key(keys::kUiFontPointSize),
                             values.fontPointSize == 0 ? QString()
                                                       : QString::number(values.fontPointSize));
    SettingsClient::setValue(&entries_, key(keys::kUiScalePercent),
                             values.scalePercent == 100 ? QString()
                                                        : QString::number(values.scalePercent));
}

void SettingsPanel::setAppearanceValues(const AppearanceValues &values)
{
    updating_ = true;
    if (fontPointSizeSpin_ != nullptr) {
        fontPointSizeSpin_->setValue(values.fontPointSize);
    }
    if (scalePercentCombo_ != nullptr) {
        const int index = scalePercentCombo_->findData(values.scalePercent);
        scalePercentCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    updating_ = false;
    writeAppearanceEntries(values);
    // Re-read the just-written values so the "restart to apply" hint matches the
    // store (and the font is applied from the same path a reload uses).
    applyAppearanceEntries();
    persist();
}

int SettingsPanel::applyFontPointSize(int pointSize)
{
    return ui_scale::applyFontPointSize(pointSize);
}

// --- UX batch 2 / A4: lint -------------------------------------------------

QString SettingsPanel::lintBackend() const
{
    return lintBackendSetting(lintBackend_);
}

void SettingsPanel::applyLintWidgets()
{
    updating_ = true;
    const LintVeribleValues &verible = lintValues_.verible;
    setWidgetValue(lintRulesetCombo_, verible.ruleset);
    if (lintRulesEdit_ != nullptr) {
        lintRulesEdit_->setText(verible.rules);
    }
    if (lintRulesConfigEdit_ != nullptr) {
        lintRulesConfigEdit_->setText(verible.rulesConfig);
    }
    if (lintWaiverFilesEdit_ != nullptr) {
        lintWaiverFilesEdit_->setPlainText(verible.waiverFiles);
    }
    setWidgetValue(lintAutofixCombo_, verible.autofix);
    if (lintSlangWarningsEdit_ != nullptr) {
        lintSlangWarningsEdit_->setPlainText(lintValues_.slang.warnings);
    }
    if (lintSlangErrorLimitEdit_ != nullptr) {
        lintSlangErrorLimitEdit_->setText(lintValues_.slang.errorLimit);
    }
    updating_ = false;

    // The inactive engine's controls are replaced by the CURRENT engine's values:
    // R1 says the page shows the selected engine's set, and switching must not
    // lose the other one (it stays in the store and in lintValues_).
    if (lintVeribleGroup_ != nullptr) {
        lintVeribleGroup_->setTitle(
            lintBackend_ == LintBackend::Verible
                ? tr("verible-verilog-lint (selected)")
                : tr("verible-verilog-lint (not the selected engine)"));
    }
    if (lintSlangGroup_ != nullptr) {
        lintSlangGroup_->setTitle(lintBackend_ == LintBackend::Slang
                                      ? tr("slang (selected)")
                                      : tr("slang (not the selected engine)"));
    }
    refreshLintKnobStates();
}

void SettingsPanel::readLintWidgets(LintValues *values) const
{
    if (values == nullptr) {
        return;
    }
    // Only the ACTIVE engine's half is taken from the widgets. The other half is
    // carried verbatim from the store snapshot, which is what makes the two
    // configurations independent (R1).
    if (lintBackend_ == LintBackend::Verible) {
        values->verible.ruleset = widgetValue(lintRulesetCombo_);
        values->verible.rules =
            lintRulesEdit_ != nullptr ? lintRulesEdit_->text().trimmed() : QString();
        values->verible.rulesConfig =
            lintRulesConfigEdit_ != nullptr ? lintRulesConfigEdit_->text().trimmed() : QString();
        values->verible.waiverFiles = lintWaiverFilesEdit_ != nullptr
            ? lintWaiverFilesEdit_->toPlainText().trimmed()
            : QString();
        values->verible.autofix = widgetValue(lintAutofixCombo_);
    } else {
        values->slang.warnings = lintSlangWarningsEdit_ != nullptr
            ? lintSlangWarningsEdit_->toPlainText().trimmed()
            : QString();
        values->slang.errorLimit =
            lintSlangErrorLimitEdit_ != nullptr ? lintSlangErrorLimitEdit_->text().trimmed()
                                               : QString();
    }
}

LintValues SettingsPanel::lintValues() const
{
    LintValues values = lintValues_;
    readLintWidgets(&values);
    return values;
}

void SettingsPanel::applyLintEntries()
{
    lintValues_ = lintValuesFromEntries(entries_);
    lintBackend_ = lintBackendFromSetting(
        SettingsClient::valueOf(entries_, QString::fromLatin1(keys::kLintEngine)));
    applyLintWidgets();
    setCurrentLintValues(lintValues());
}

void SettingsPanel::setLintValues(const LintValues &values)
{
    lintValues_ = values;
    applyLintWidgets();
    writeLintEntries(&entries_, values);
    setCurrentLintValues(values);
    persist();
}

void SettingsPanel::refreshLintKnobStates()
{
    QStringList pending;
    for (auto it = lintKnobWidgets_.cbegin(); it != lintKnobWidgets_.cend(); ++it) {
        const LintKnob knob = static_cast<LintKnob>(it.key());
        QWidget *widget = it.value();
        if (widget == nullptr) {
            continue;
        }
        const LintKnobState state = lintKnobState(lintBackend_, knob);
        widget->setEnabled(state.enabled());
        if (state.pending()) {
            pending.append(lintKnobFlagName(knob));
        }
        widget->setToolTip(
            state.reason.isEmpty()
                ? tr("%1 is sent to the selected lint job.")
                      .arg(lintKnobFlagName(knob))
                : state.reason);
    }
    if (lintVeribleStatus_ != nullptr) {
        lintVeribleStatus_->setText(
            lintBackend_ == LintBackend::Verible
                ? tr("Active engine. --ruleset, --rules, --rules_config and --waiver_files "
                     "are applied to the lint job; --autofix is stored only (the lint job's "
                     "C ABI has no field for it).")
                : tr("Not the selected engine — these controls are disabled. Switch Tools → "
                     "Lint engine to verible-verilog-lint to edit them; their values are kept."));
    }
    if (lintSlangStatus_ != nullptr) {
        lintSlangStatus_->setText(
            lintBackend_ == LintBackend::Slang
                ? tr("Active engine. -W and --error-limit are applied to the lint job.")
                : tr("Not the selected engine — these controls are disabled. Switch Tools → "
                     "Lint engine to slang to edit them; their values are kept."));
    }
    if (lintTransportHint_ == nullptr) {
        return;
    }
    if (pending.isEmpty()) {
        lintTransportHint_->setText(
            tr("Every option of the selected engine is applied to the lint job."));
    } else {
        lintTransportHint_->setText(
            tr("Stored but not applied yet (the lint job's C ABI has no field for them): %1. "
               "The values stay in settings.toml and are never sent as a silent no-op.")
                .arg(pending.join(QStringLiteral(", "))));
    }
}

QWidget *SettingsPanel::lintKnobWidget(LintKnob knob) const
{
    return lintKnobWidgets_.value(static_cast<int>(knob));
}

bool SettingsPanel::lintKnobEnabled(LintKnob knob) const
{
    const QWidget *widget = lintKnobWidget(knob);
    return widget != nullptr && widget->isEnabled();
}

QString SettingsPanel::lintKnobReason(LintKnob knob) const
{
    return lintKnobState(lintBackend_, knob).reason;
}

// --- UX batch 2 / A4: format -----------------------------------------------

FormatValues SettingsPanel::formatValues() const
{
    FormatValues values;
    for (const FormatKnobSpec &knob : kFormatKnobs) {
        const QString key = QString::fromLatin1(knob.key);
        values.insert(key, widgetValue(formatKnobWidgets_.value(key)));
    }
    return values;
}

void SettingsPanel::applyFormatWidgets(const FormatValues &values)
{
    updating_ = true;
    for (const FormatKnobSpec &knob : kFormatKnobs) {
        const QString key = QString::fromLatin1(knob.key);
        setWidgetValue(formatKnobWidgets_.value(key), values.value(key));
    }
    updating_ = false;
}

void SettingsPanel::applyFormatEntries()
{
    applyFormatWidgets(formatValuesFromEntries(entries_));
    setCurrentFormatValues(formatValues());
}

void SettingsPanel::setFormatValues(const FormatValues &values)
{
    applyFormatWidgets(values);
    writeFormatEntries(&entries_, values);
    setCurrentFormatValues(values);
    persist();
}

void SettingsPanel::restoreFormatDefaults()
{
    setFormatValues(defaultFormatValues());
    statusLabel_->setText(
        tr("Format options restored to verible's defaults (no flags are passed)."));
}

QWidget *SettingsPanel::formatKnobWidget(const QString &key) const
{
    return formatKnobWidgets_.value(key);
}

void SettingsPanel::publishFormatAndLint()
{
    setCurrentFormatValues(formatValues());
    setCurrentLintValues(lintValues());
}

QString SettingsPanel::selectedCommand() const
{
    if (keyTable_ == nullptr) {
        return {};
    }
    const QList<QTableWidgetItem *> selected = keyTable_->selectedItems();
    if (selected.isEmpty()) {
        return {};
    }
    const QString label = keyTable_->item(selected.first()->row(), 0)->text();
    const int open = label.lastIndexOf(QLatin1Char('('));
    const int close = label.lastIndexOf(QLatin1Char(')'));
    if (open < 0 || close <= open) {
        return {};
    }
    return label.mid(open + 1, close - open - 1);
}

} // namespace vedit::workspace::settings
