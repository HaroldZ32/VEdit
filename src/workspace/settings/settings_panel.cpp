#include "settings_panel.h"

#include "command_registry.h"
#include "theme_manager.h"

#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

namespace vedit::workspace::settings {

namespace {
const char *const kThemeKey = "theme";
const char *const kKeymapPrefix = "keymap.";
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
    buildUi();
    reload();
}

SettingsPanel::~SettingsPanel() = default;

void SettingsPanel::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    auto *themeGroup = new QGroupBox(QStringLiteral("Theme"), this);
    auto *themeForm = new QFormLayout(themeGroup);
    themeCombo_ = new QComboBox(themeGroup);
    themeCombo_->setObjectName(QStringLiteral("settings-theme"));
    themeForm->addRow(QStringLiteral("Theme:"), themeCombo_);
    auto *themeHint = new QLabel(
        QStringLiteral("Applied immediately and stored as `theme` by the Rust service."),
        themeGroup);
    themeHint->setWordWrap(true);
    themeForm->addRow(themeHint);

    auto *keyGroup = new QGroupBox(QStringLiteral("Key bindings"), this);
    auto *keyLayout = new QVBoxLayout(keyGroup);
    keyTable_ = new QTableWidget(0, 3, keyGroup);
    keyTable_->setObjectName(QStringLiteral("settings-keys"));
    keyTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Command"), QStringLiteral("Shortcut"), QStringLiteral("State")});
    keyTable_->horizontalHeader()->setStretchLastSection(true);
    keyTable_->verticalHeader()->setVisible(false);
    keyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    keyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    keyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    keyLayout->addWidget(keyTable_, 1);

    auto *editorRow = new QHBoxLayout();
    shortcutEditor_ = new QKeySequenceEdit(keyGroup);
    shortcutEditor_->setObjectName(QStringLiteral("settings-shortcut-editor"));
    assignButton_ = new QPushButton(QStringLiteral("Assign to selected command"), keyGroup);
    assignButton_->setObjectName(QStringLiteral("settings-assign"));
    clearButton_ = new QPushButton(QStringLiteral("Clear"), keyGroup);
    clearButton_->setObjectName(QStringLiteral("settings-clear"));
    editorRow->addWidget(new QLabel(QStringLiteral("New shortcut:"), keyGroup));
    editorRow->addWidget(shortcutEditor_, 1);
    editorRow->addWidget(assignButton_);
    editorRow->addWidget(clearButton_);
    keyLayout->addLayout(editorRow);

    conflictLabel_ = new QLabel(keyGroup);
    conflictLabel_->setObjectName(QStringLiteral("settings-conflict"));
    conflictLabel_->setWordWrap(true);
    keyLayout->addWidget(conflictLabel_);

    auto *diagnosticsGroup = new QGroupBox(QStringLiteral("Diagnostics"), this);
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
    reloadButton_ = new QPushButton(QStringLiteral("Reload from disk"), diagnosticsGroup);
    reloadButton_->setObjectName(QStringLiteral("settings-reload"));
    diagnosticsLayout->addWidget(reloadButton_, 0, Qt::AlignLeft);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("settings-status"));
    statusLabel_->setWordWrap(true);

    layout->addWidget(themeGroup);
    layout->addWidget(keyGroup, 2);
    layout->addWidget(diagnosticsGroup, 1);
    layout->addWidget(statusLabel_);

    connect(themeCombo_, &QComboBox::activated, this,
            [this](int index) { setTheme(themeCombo_->itemText(index)); });
    connect(assignButton_, &QPushButton::clicked, this, [this]() { applyEditorShortcut(); });
    connect(clearButton_, &QPushButton::clicked, this, [this]() {
        const QString command = selectedCommand();
        if (!command.isEmpty()) {
            clearShortcut(command);
        }
    });
    connect(reloadButton_, &QPushButton::clicked, this, [this]() { reload(); });
    connect(keyTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const QString command = selectedCommand();
        if (!command.isEmpty() && commands_ != nullptr) {
            if (const QAction *action = commands_->action(command); action != nullptr) {
                shortcutEditor_->setKeySequence(action->shortcut());
            }
        }
    });
}

void SettingsPanel::reload()
{
    diagnostics_.clear();
    statusLabel_->setText(QStringLiteral("Loading settings from %1 …").arg(client_->path()));
    client_->load([this](Snapshot snapshot) {
        if (!snapshot.completed) {
            addDiagnostic(snapshot.error.isEmpty() ? QStringLiteral("settings could not be loaded")
                                                   : snapshot.error);
            statusLabel_->setText(
                QStringLiteral("Settings unavailable; showing the built-in defaults."));
            refreshWidgets();
            return;
        }
        for (const QString &warning : snapshot.warnings) {
            addDiagnostic(warning);
        }
        entries_ = snapshot.entries;
        applyThemeFromEntries();
        applyKeymapFromEntries();
        refreshWidgets();
        statusLabel_->setText(
            snapshot.warnings.isEmpty()
                ? QStringLiteral("Settings loaded from %1").arg(client_->path())
                : QStringLiteral("Settings loaded from %1 with %2 warning(s); the built-in "
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
    if (stored.isEmpty() || themes_ == nullptr) {
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
        statusLabel_->setText(QStringLiteral("Theme '%1' is invalid; the default theme is "
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
        statusLabel_->setText(QStringLiteral("Theme '%1' could not be applied.").arg(name));
        refreshWidgets();
        return;
    }
    SettingsClient::setValue(&entries_, QString::fromLatin1(kThemeKey), name);
    persist();
    refreshThemeCombo();
    statusLabel_->setText(QStringLiteral("Theme '%1' applied and stored by the Rust service.")
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
    commands_->store()->setShortcut(commandId, sequence);
    commands_->refreshShortcuts();
    SettingsClient::setValue(&entries_,
                             QString::fromLatin1(kKeymapPrefix) + commandId,
                             sequence.toString());
    persist();
    refreshWidgets();
    statusLabel_->setText(QStringLiteral("Shortcut for '%1' set to %2 and stored by the Rust "
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
    statusLabel_->setText(QStringLiteral("Shortcut for '%1' cleared (default restored).")
                              .arg(commandId));
    emit shortcutsChanged();
}

void SettingsPanel::applyEditorShortcut()
{
    const QString command = selectedCommand();
    if (command.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Select a command in the table first."));
        return;
    }
    setShortcut(command, shortcutEditor_->keySequence());
}

void SettingsPanel::persist()
{
    if (client_ == nullptr) {
        return;
    }
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
            addDiagnostic(error.isEmpty() ? QStringLiteral("the Rust settings store rejected "
                                                           "the write")
                                          : error);
            statusLabel_->setText(QStringLiteral("The settings change was applied but could not "
                                                 "be stored; see the diagnostics."));
            refreshWidgets();
            return;
        }
        statusLabel_->setText(QStringLiteral("Stored by the Rust service in %1")
                                  .arg(client_->path()));
        refreshWidgets();
    });
}

void SettingsPanel::refreshWidgets()
{
    refreshThemeCombo();
    refreshKeyTable();
    refreshConflicts();
    diagnosticsLabel_->setText(
        diagnostics_.isEmpty()
            ? QStringLiteral("No configuration problems reported by the Rust store.")
            : QStringLiteral("%1 configuration problem(s) — the built-in defaults were kept "
                             "where needed:")
                  .arg(diagnostics_.size()));
    diagnosticsView_->setPlainText(diagnostics_.join(QLatin1Char('\n')));
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
        keyTable_->setItem(row, 1, new QTableWidgetItem(action->shortcut().toString()));
        const bool overridden = commands_->store() != nullptr
            && commands_->store()->shortcut(command, action->shortcut()) != action->shortcut();
        keyTable_->setItem(row, 2, new QTableWidgetItem(overridden
                                                            ? QStringLiteral("override")
                                                            : QStringLiteral("default")));
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
        conflictLabel_->setText(QStringLiteral("No conflicting shortcuts."));
        return;
    }
    conflictLabel_->setStyleSheet(QStringLiteral("color: #a33; font-weight: bold;"));
    conflictLabel_->setText(QStringLiteral("Conflicting shortcuts: %1")
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
