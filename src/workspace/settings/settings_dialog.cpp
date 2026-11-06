#include "settings_dialog.h"
#include <QCoreApplication>

#include "settings_panel.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace vedit::workspace::settings {

SettingsDialog::SettingsDialog(vedit::app::ThemeManager *themes,
                               vedit::app::CommandRegistry *commands, QString settingsPath,
                               QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("settings-dialog"));
    setWindowTitle(QCoreApplication::translate("VEdit", "Preferences"));
    setModal(true);
    // Tall enough for the three pages (item 10.2): the tools form and the FIM
    // key group are the tallest, and the diagnostics box must stay visible.
    resize(900, 760);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    panel_ = new SettingsPanel(themes, commands, std::move(settingsPath), this);
    layout->addWidget(panel_, 1);

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
}

void SettingsDialog::revertPending()
{
    if (panel_ != nullptr) {
        panel_->revertPending();
    }
}

bool SettingsDialog::hasPendingChanges() const
{
    return panel_ != nullptr && panel_->hasPendingChanges();
}

void SettingsDialog::accept()
{
    if (panel_ != nullptr) {
        panel_->commitPending();
    }
    QDialog::accept();
}

void SettingsDialog::reject()
{
    if (panel_ != nullptr) {
        panel_->revertPending();
    }
    QDialog::reject();
}

} // namespace vedit::workspace::settings
