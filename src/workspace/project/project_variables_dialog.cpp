#include "project_variables_dialog.h"
#include <QCoreApplication>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringLiteral>
#include <QVBoxLayout>

namespace vedit::workspace::project {

ProjectVariablesDialog::ProjectVariablesDialog(const QStringList &names,
                                               const QMap<QString, QString> &known,
                                               QWidget *parent)
    : QDialog(parent)
    , names_(names)
{
    setObjectName(QStringLiteral("project-variables-dialog"));
    setWindowTitle(QCoreApplication::translate("VEdit", "Filelist variables"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *hint = new QLabel(
        QCoreApplication::translate("VEdit", "These variables are used by the imported .f filelists but are not "
                       "resolved by the project table or the environment. Fill them in once."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    for (const QString &name : names_) {
        auto *edit = new QLineEdit(this);
        edit->setObjectName(QStringLiteral("project-variable-%1").arg(name));
        edit->setText(known.value(name));
        edit->setPlaceholderText(QCoreApplication::translate("VEdit", "value for %1").arg(name));
        form->addRow(name, edit);
        edits_.insert(name, edit);
    }
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

int ProjectVariablesDialog::rowCount() const
{
    return static_cast<int>(names_.size());
}

QStringList ProjectVariablesDialog::names() const
{
    return names_;
}

QMap<QString, QString> ProjectVariablesDialog::values() const
{
    QMap<QString, QString> out;
    for (auto it = edits_.constBegin(); it != edits_.constEnd(); ++it) {
        out.insert(it.key(), it.value() != nullptr ? it.value()->text() : QString());
    }
    return out;
}

QStringList ProjectVariablesDialog::variableTable() const
{
    QStringList out;
    for (const QString &name : names_) {
        const auto *edit = edits_.value(name);
        const QString value = edit != nullptr ? edit->text() : QString();
        if (!value.isEmpty()) {
            out.append(QStringLiteral("%1=%2").arg(name, value));
        }
    }
    return out;
}

void ProjectVariablesDialog::setValue(const QString &name, const QString &value)
{
    if (QLineEdit *edit = edits_.value(name); edit != nullptr) {
        edit->setText(value);
        return;
    }
    // A probe/test may add a row for a name the dialog was not built with.
    names_.append(name);
    auto *edit = new QLineEdit(this);
    edit->setText(value);
    edits_.insert(name, edit);
}

QMap<QString, QString> ProjectVariablesDialog::parseTable(const QStringList &entries)
{
    QMap<QString, QString> out;
    for (const QString &entry : entries) {
        const QString trimmed = entry.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const int split = trimmed.indexOf(QLatin1Char('='));
        const QString name = (split < 0 ? trimmed : trimmed.left(split)).trimmed();
        if (name.isEmpty()) {
            continue;
        }
        out.insert(name, split < 0 ? QString() : trimmed.mid(split + 1));
    }
    return out;
}

QStringList ProjectVariablesDialog::toTable(const QMap<QString, QString> &values)
{
    QStringList out;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (it.key().trimmed().isEmpty()) {
            continue;
        }
        out.append(QStringLiteral("%1=%2").arg(it.key().trimmed(), it.value()));
    }
    return out;
}

} // namespace vedit::workspace::project
