#include "vcs_panel.h"

#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QStringList>
#include <QVBoxLayout>

namespace vedit::workspace::vcs {

VcsPanel::VcsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("vcs-panel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    header_ = new QLabel(this);
    header_->setObjectName(QStringLiteral("vcs-header"));
    QFont headerFont = header_->font();
    headerFont.setBold(true);
    header_->setFont(headerFont);
    layout->addWidget(header_);

    empty_ = new QLabel(this);
    empty_->setObjectName(QStringLiteral("vcs-empty-state"));
    empty_->setWordWrap(true);
    layout->addWidget(empty_);

    files_ = new QListWidget(this);
    files_->setObjectName(QStringLiteral("vcs-files"));
    layout->addWidget(files_, 1);

    diff_ = new QPlainTextEdit(this);
    diff_->setObjectName(QStringLiteral("vcs-diff"));
    diff_->setReadOnly(true);
    diff_->setFont(QFont(QStringLiteral("monospace")));
    layout->addWidget(diff_, 2);

    connect(files_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) {
            return;
        }
        const QString path = files_->item(row)->text();
        selectFile(path.toStdString());
    });

    showEmptyState(Status{});
}

VcsPanel::~VcsPanel() = default;

void VcsPanel::setSource(std::unique_ptr<VcsSource> source)
{
    source_ = std::move(source);
    refresh();
}

void VcsPanel::setRoot(const std::string &root)
{
    root_ = root;
}

void VcsPanel::refresh()
{
    if (!source_) {
        showEmptyState(Status{});
        return;
    }
    const Status status = source_->status(root_);
    backend_ = status.backend;
    if (status.backend == Backend::None) {
        showEmptyState(status);
        return;
    }

    empty_->setVisible(false);
    files_->setVisible(true);
    diff_->setVisible(true);
    files_->clear();
    for (const Change &change : status.changes) {
        files_->addItem(QStringLiteral("%1  %2")
                            .arg(QString::fromStdString(change.path),
                                 QString::fromLatin1(changeKindName(change.kind))));
    }

    const Info info = source_->info(root_);
    QStringList parts;
    parts << QString::fromLatin1(backendName(status.backend));
    if (!info.branch.empty()) {
        parts << QString::fromStdString(info.branch);
    }
    if (!info.revision.empty()) {
        parts << QStringLiteral("@ ") + QString::fromStdString(info.revision);
    }
    parts << QStringLiteral("· %1 changed file(s)").arg(status.changes.size());
    header_->setText(parts.join(QStringLiteral(" ")));
}

void VcsPanel::showEmptyState(const Status &status)
{
    backend_ = Backend::None;
    files_->clear();
    files_->setVisible(false);
    diff_->clear();
    diff_->setVisible(false);
    empty_->setVisible(true);
    header_->setText(QString::fromLatin1(backendName(Backend::None)));
    const std::string note = status.note.empty()
        ? std::string("not a Git or Subversion working tree")
        : status.note;
    empty_->setText(QStringLiteral("%1\n\nOpen a directory that is a Git or Subversion working "
                                   "tree to see its branch, changed files and diffs.")
                        .arg(QString::fromStdString(note)));
}

void VcsPanel::selectFile(const std::string &path)
{
    if (!source_) {
        diff_->setPlainText(QStringLiteral("No VCS backend available."));
        return;
    }
    const Diff diff = source_->diff(root_, path);
    if (!diff.text.empty()) {
        diff_->setPlainText(QString::fromStdString(diff.text));
        return;
    }
    const std::string note = diff.note.empty() ? std::string("no diff to show") : diff.note;
    diff_->setPlainText(QStringLiteral("%1: %2")
                            .arg(QString::fromStdString(path), QString::fromStdString(note)));
}

int VcsPanel::fileCount() const
{
    return files_->count();
}

QString VcsPanel::fileText(int row) const
{
    const QListWidgetItem *item = files_->item(row);
    return item != nullptr ? item->text() : QString();
}

QString VcsPanel::headerText() const
{
    return header_->text();
}

QString VcsPanel::emptyText() const
{
    return empty_->text();
}

QString VcsPanel::diffText() const
{
    return diff_->toPlainText();
}

bool VcsPanel::emptyStateVisible() const
{
    return empty_->isVisibleTo(this);
}

} // namespace vedit::workspace::vcs
