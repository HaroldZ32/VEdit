#include "project_variables_dialog.h"
#include <QCoreApplication>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QStringLiteral>
#include <QVBoxLayout>

namespace vedit::workspace::project {

ProjectVariablesDialog::ProjectVariablesDialog(const QStringList &names,
                                               const QMap<QString, QString> &known,
                                               QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("project-variables-dialog"));
    setWindowTitle(QCoreApplication::translate("VEdit", "Filelist variables"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // WHY these variables exist (RM-3 item 4): one short line, no marketing.
    auto *purpose = new QLabel(
        QCoreApplication::translate(
            "VEdit",
            "These variables are substituted into filelist paths and include directories."),
        this);
    purpose->setObjectName(QStringLiteral("project-variables-purpose"));
    purpose->setWordWrap(true);
    layout->addWidget(purpose);

    // The deletion rule (RM-3 item 2), stated in the dialog's own text so no
    // value and no required name can disappear silently.
    auto *rule = new QLabel(
        QCoreApplication::translate(
            "VEdit",
            "Deleting a variable removes its value; if a filelist still references it, the "
            "project reports it as missing and asks for it again."),
        this);
    rule->setObjectName(QStringLiteral("project-variables-rule"));
    rule->setWordWrap(true);
    layout->addWidget(rule);

    // The Add button, the feedback note and the button box are created BEFORE the
    // rows: every row is inserted above them, so the visual order stays
    // "purpose, rule, rows…, Add, note, OK/Cancel".
    addButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Add variable"), this);
    addButton_->setObjectName(QStringLiteral("project-variable-add"));
    connect(addButton_, &QPushButton::clicked, this, [this]() {
        appendRow(QString(), QString(), false);
        rows_.last().nameEdit->setFocus();
    });

    note_ = new QLabel(this);
    note_->setObjectName(QStringLiteral("project-variables-note"));
    note_->setWordWrap(true);
    note_->hide();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        // Inline validation (RM-3 item 3): an invalid row keeps the dialog open
        // with its reason beside it - never a modal, never a silent ignore.
        if (validateRows()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(addButton_, 0, Qt::AlignLeft);
    layout->addWidget(note_);
    layout->addWidget(buttons);

    // Every known name is ALWAYS shown, even when the caller asked only for the
    // missing ones: callers REPLACE the model's table with what this dialog
    // returns, and a replace must never drop a variable the user did not see.
    QStringList requested;
    for (const QString &name : names) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty() && !requested.contains(trimmed)) {
            requested.append(trimmed);
        }
    }
    knownNames_ = known.keys();
    for (auto it = known.constBegin(); it != known.constEnd(); ++it) {
        if (!requested.contains(it.key())) {
            requested.append(it.key());
        }
    }
    for (const QString &name : requested) {
        appendRow(name, known.value(name), !known.contains(name));
    }
}

void ProjectVariablesDialog::appendRow(const QString &name, const QString &value, bool required)
{
    Row row;
    row.required = required;
    auto *container = new QWidget(this);
    auto *containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(2);

    auto *rowWidget = new QWidget(container);
    auto *rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(6);

    row.nameEdit = new QLineEdit(name, rowWidget);
    row.nameEdit->setObjectName(QStringLiteral("project-variable-name-%1").arg(name));
    row.nameEdit->setPlaceholderText(QCoreApplication::translate("VEdit", "name"));
    rowLayout->addWidget(row.nameEdit, 2);

    row.valueEdit = new QLineEdit(value, rowWidget);
    row.valueEdit->setObjectName(
        name.isEmpty() ? QStringLiteral("project-variable-value-new")
                       : QStringLiteral("project-variable-%1").arg(name));
    row.valueEdit->setPlaceholderText(
        QCoreApplication::translate("VEdit", "value for %1").arg(name));
    rowLayout->addWidget(row.valueEdit, 3);

    row.removeButton = new QPushButton(QCoreApplication::translate("VEdit", "Delete"), rowWidget);
    row.removeButton->setObjectName(QStringLiteral("project-variable-remove-%1").arg(name));
    rowLayout->addWidget(row.removeButton);

    containerLayout->addWidget(rowWidget);

    row.reason = new QLabel(container);
    row.reason->setObjectName(QStringLiteral("project-variable-reason-%1").arg(name));
    row.reason->setWordWrap(true);
    row.reason->setStyleSheet(QStringLiteral("color: #b3261e;"));
    row.reason->hide();
    containerLayout->addWidget(row.reason);

    row.widget = container;
    // Deleting resolves the row by its NAME EDIT POINTER: the name may have been
    // edited since the button was created, so a captured name would go stale.
    QLineEdit *nameEdit = row.nameEdit;
    connect(row.removeButton, &QPushButton::clicked, this, [this, nameEdit]() {
        for (int index = 0; index < rows_.size(); ++index) {
            if (rows_.at(index).nameEdit == nameEdit) {
                removeRowAt(index);
                return;
            }
        }
    });

    auto *mainLayout = static_cast<QVBoxLayout *>(layout());
    const int addIndex = mainLayout->indexOf(addButton_);
    mainLayout->insertWidget(addIndex >= 0 ? addIndex : mainLayout->count(), container);

    rows_.append(row);
    updateNote(QString());
}

void ProjectVariablesDialog::removeRowAt(int index)
{
    if (index < 0 || index >= rows_.size()) {
        return;
    }
    const Row row = rows_.takeAt(index);
    const QString removedName = row.nameEdit->text().trimmed();
    if (row.widget != nullptr) {
        row.widget->deleteLater();
    }
    validateRows();
    updateNote(row.required
                  ? QCoreApplication::translate(
                        "VEdit", "Removed %1 — the project still needs it and will ask again")
                        .arg(removedName)
                  : QCoreApplication::translate("VEdit", "Removed %1").arg(removedName));
}

ProjectVariablesDialog::Row *ProjectVariablesDialog::rowFor(const QString &name)
{
    const QString target = name.trimmed();
    for (Row &row : rows_) {
        if (row.nameEdit->text().trimmed() == target) {
            return &row;
        }
    }
    return nullptr;
}

const ProjectVariablesDialog::Row *ProjectVariablesDialog::rowFor(const QString &name) const
{
    const QString target = name.trimmed();
    for (const Row &row : rows_) {
        if (row.nameEdit->text().trimmed() == target) {
            return &row;
        }
    }
    return nullptr;
}

QStringList ProjectVariablesDialog::allRowNames() const
{
    QStringList names;
    names.reserve(rows_.size());
    for (const Row &row : rows_) {
        names.append(row.nameEdit->text().trimmed());
    }
    return names;
}

int ProjectVariablesDialog::rowCount() const
{
    return static_cast<int>(rows_.size());
}

QStringList ProjectVariablesDialog::names() const
{
    return allRowNames();
}

QMap<QString, QString> ProjectVariablesDialog::values() const
{
    QMap<QString, QString> out;
    for (const Row &row : rows_) {
        const QString name = row.nameEdit->text().trimmed();
        if (!name.isEmpty()) {
            out.insert(name, row.valueEdit->text());
        }
    }
    return out;
}

QStringList ProjectVariablesDialog::variableTable() const
{
    QStringList out;
    QSet<QString> seen;
    for (const Row &row : rows_) {
        const QString name = row.nameEdit->text().trimmed();
        const QString value = row.valueEdit->text();
        // Only a VALID name with a value is written; an emptied row is how the
        // user deletes an entry.
        if (!isValidName(name) || value.isEmpty() || seen.contains(name)) {
            continue;
        }
        seen.insert(name);
        out.append(QStringLiteral("%1=%2").arg(name, value));
    }
    return out;
}

void ProjectVariablesDialog::setValue(const QString &name, const QString &value)
{
    if (Row *row = rowFor(name)) {
        row->valueEdit->setText(value);
        return;
    }
    // A probe/test may add a row for a name the dialog was not built with; this
    // is the same seam the Add button uses.
    addVariable(name, value);
}

bool ProjectVariablesDialog::addVariable(const QString &name, const QString &value)
{
    appendRow(name.trimmed(), value, false);
    validateRows();
    const Row *row = rowFor(name);
    return row != nullptr && row->reason->isHidden();
}

bool ProjectVariablesDialog::removeVariable(const QString &name)
{
    const QString target = name.trimmed();
    for (int index = 0; index < rows_.size(); ++index) {
        if (rows_.at(index).nameEdit->text().trimmed() == target) {
            removeRowAt(index);
            return true;
        }
    }
    return false;
}

QString ProjectVariablesDialog::rowReason(const QString &name) const
{
    const Row *row = rowFor(name);
    return row != nullptr && !row->reason->isHidden() ? row->reason->text() : QString();
}

bool ProjectVariablesDialog::validateRows()
{
    const QStringList names = allRowNames();
    bool valid = true;
    for (Row &row : rows_) {
        const QString name = row.nameEdit->text().trimmed();
        QString reason;
        if (name.isEmpty()) {
            reason = QCoreApplication::translate("VEdit", "Enter a variable name");
        } else if (!isValidName(name)) {
            reason = QCoreApplication::translate(
                "VEdit",
                "A name may only contain letters, digits and '_', and cannot start with a "
                "digit");
        } else if (names.count(name) > 1) {
            reason = QCoreApplication::translate("VEdit", "This name appears more than once");
        }
        row.reason->setText(reason);
        row.reason->setVisible(!reason.isEmpty());
        if (!reason.isEmpty()) {
            valid = false;
        }
    }
    return valid;
}

QString ProjectVariablesDialog::note() const
{
    // The text, not "text while visible": this dialog is also driven headless by
    // the regression, where a child widget is never shown.
    return note_ != nullptr ? note_->text() : QString();
}

void ProjectVariablesDialog::updateNote(const QString &text)
{
    if (note_ == nullptr) {
        return;
    }
    note_->setText(text);
    note_->setVisible(!text.isEmpty());
}

bool ProjectVariablesDialog::isValidName(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    const QChar first = name.at(0);
    const ushort firstCode = first.unicode();
    const bool asciiLetter = (firstCode >= 'A' && firstCode <= 'Z')
        || (firstCode >= 'a' && firstCode <= 'z');
    if (!asciiLetter && firstCode != '_') {
        return false;
    }
    for (const QChar character : name) {
        const ushort code = character.unicode();
        const bool asciiAlnum = (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z')
            || (code >= '0' && code <= '9');
        if (!asciiAlnum && code != '_') {
            return false;
        }
    }
    return true;
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
