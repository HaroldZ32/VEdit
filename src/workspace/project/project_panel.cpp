#include "project_panel.h"

#include "project_model.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStringLiteral>
#include <QVBoxLayout>

namespace vedit::workspace::project {

ProjectPanel::ProjectPanel(ProjectModel *model, QWidget *parent)
    : QWidget(parent)
    , model_(model)
{
    setObjectName(QStringLiteral("project-panel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("project-summary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    auto *buttons = new QHBoxLayout();
    auto *refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
    refreshButton->setObjectName(QStringLiteral("project-refresh"));
    buttons->addWidget(refreshButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    emptyHint_ = new QLabel(
        QStringLiteral("Open a project with --project <dir>; the expanded file list appears here."),
        this);
    emptyHint_->setObjectName(QStringLiteral("project-empty-hint"));
    emptyHint_->setWordWrap(true);
    layout->addWidget(emptyHint_);

    auto *filesHeader = new QLabel(QStringLiteral("Files"), this);
    filesHeader->setObjectName(QStringLiteral("project-files-header"));
    QFont filesFont = filesHeader->font();
    filesFont.setBold(true);
    filesHeader->setFont(filesFont);
    layout->addWidget(filesHeader);

    fileList_ = new QListWidget(this);
    fileList_->setObjectName(QStringLiteral("project-file-list"));
    layout->addWidget(fileList_, 3);

    diagnosticsHeader_ = new QLabel(QStringLiteral("Diagnostics (0)"), this);
    diagnosticsHeader_->setObjectName(QStringLiteral("project-diagnostics-header"));
    layout->addWidget(diagnosticsHeader_);

    diagnostics_ = new QListWidget(this);
    diagnostics_->setObjectName(QStringLiteral("project-diagnostics"));
    diagnostics_->setWordWrap(true);
    layout->addWidget(diagnostics_, 2);

    connect(refreshButton, &QPushButton::clicked, this, &ProjectPanel::refresh);
    if (model_ != nullptr) {
        connect(model_, &ProjectModel::stateChanged, this, &ProjectPanel::updateUi);
        connect(model_, &ProjectModel::refreshFinished, this,
                [this](bool, const QString &) { updateUi(); });
    }
    updateUi();
}

ProjectModel *ProjectPanel::model() const
{
    return model_;
}

void ProjectPanel::refresh()
{
    if (model_ != nullptr) {
        model_->refresh();
    }
}

QString ProjectPanel::summaryText() const
{
    return summary_ != nullptr ? summary_->text() : QString();
}

int ProjectPanel::fileCount() const
{
    return fileList_ != nullptr ? fileList_->count() : 0;
}

QStringList ProjectPanel::displayedFiles() const
{
    QStringList out;
    if (fileList_ == nullptr) {
        return out;
    }
    for (int row = 0; row < fileList_->count(); ++row) {
        out.append(fileList_->item(row)->text());
    }
    return out;
}

QStringList ProjectPanel::displayedDiagnostics() const
{
    QStringList out;
    if (diagnostics_ == nullptr) {
        return out;
    }
    for (int row = 0; row < diagnostics_->count(); ++row) {
        out.append(diagnostics_->item(row)->text());
    }
    return out;
}

void ProjectPanel::updateUi()
{
    if (model_ == nullptr) {
        return;
    }
    summary_->setText(model_->summary());

    fileList_->clear();
    fileList_->addItems(model_->displayFiles());

    const QStringList diagnostics = model_->diagnostics();
    diagnosticsHeader_->setText(QStringLiteral("Diagnostics (%1)").arg(diagnostics.size()));
    diagnostics_->clear();
    diagnostics_->addItems(diagnostics);

    if (emptyHint_ != nullptr) {
        emptyHint_->setVisible(!model_->hasProject());
    }
}

} // namespace vedit::workspace::project
