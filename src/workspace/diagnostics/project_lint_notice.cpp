#include "project_lint_notice.h"

#include "jump/jump_registry.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace vedit::workspace::diagnostics {

namespace {

QString rowText(const DiagnosticEntry &entry)
{
    QString text = QStringLiteral("%1:%2:%3 — %4")
                       .arg(QFileInfo(entry.file).fileName())
                       .arg(entry.line + 1)
                       .arg(entry.column + 1)
                       .arg(entry.message);
    if (!entry.code.isEmpty()) {
        text += QStringLiteral(" [%1]").arg(entry.code);
    } else if (!entry.source.isEmpty()) {
        text += QStringLiteral(" [%1]").arg(entry.source);
    }
    return text;
}

} // namespace

ProjectLintNotice::ProjectLintNotice(QWidget *window, QStatusBar *statusBar, QObject *parent)
    : QObject(parent)
    , window_(window)
{
    if (statusBar != nullptr) {
        statusBar_ = statusBar;
        notice_ = new QLabel(statusBar);
        notice_->setObjectName(QStringLiteral("veditProjectLintNotice"));
        notice_->setTextFormat(Qt::RichText);
        notice_->setOpenExternalLinks(false);
        notice_->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        notice_->setVisible(false);
        connect(notice_, &QLabel::linkActivated, this, [this](const QString &) {
            activateNotice();
        });
        statusBar_->addPermanentWidget(notice_);
    }
}

ProjectLintNotice::~ProjectLintNotice()
{
    // The status bar label and the dialog are children of widgets this object
    // does not own (the shell and its status bar). Deleting them here was a real
    // crash: the shell destroys its status bar BEFORE this wiring object, so the
    // label was already gone. Hide what still exists and let the owner delete.
    if (dialog_ != nullptr) {
        dialog_->hide();
    }
    if (notice_ != nullptr) {
        notice_->hide();
    }
}

void ProjectLintNotice::present(const QVector<DiagnosticEntry> &errors, Mode mode)
{
    rows_ = errors;
    std::sort(rows_.begin(), rows_.end(),
              [](const DiagnosticEntry &left, const DiagnosticEntry &right) {
                  if (left.file != right.file) {
                      return left.file < right.file;
                  }
                  if (left.line != right.line) {
                      return left.line < right.line;
                  }
                  return left.column < right.column;
              });
    nextNoticeRow_ = 0;

    if (notice_ != nullptr) {
        if (rows_.isEmpty()) {
            notice_->clear();
            notice_->setVisible(false);
        } else {
            ++noticesShown_;
            notice_->setText(
                QCoreApplication::translate(
                    "VEdit", "<a href=\"jump\">⚠ %1 project error(s) — click to jump</a>")
                    .arg(rows_.size()));
            notice_->setToolTip(
                QCoreApplication::translate("VEdit",
                                            "The automatic project lint found %1 error(s). "
                                            "Each click jumps to the next one.")
                    .arg(rows_.size()));
            notice_->setVisible(true);
        }
    }

    if (mode == Mode::Explicit && !rows_.isEmpty()) {
        // item 12: only the EXPLICIT run may take over the window with a dialog,
        // and only when there is something to guide: an empty modal is worse
        // than none.
        showDialog();
    } else if (rows_.isEmpty() && dialog_ != nullptr) {
        dialog_->hide();
    }
}

void ProjectLintNotice::clear()
{
    rows_.clear();
    nextNoticeRow_ = 0;
    if (notice_ != nullptr) {
        notice_->clear();
        notice_->setVisible(false);
    }
    if (dialog_ != nullptr) {
        dialog_->hide();
    }
}

bool ProjectLintNotice::noticeVisible() const
{
    return notice_ != nullptr && notice_->isVisibleTo(notice_->parentWidget());
}

QString ProjectLintNotice::noticeText() const
{
    return notice_ != nullptr ? notice_->text() : QString();
}

void ProjectLintNotice::showDialog()
{
    if (dialog_ == nullptr) {
        auto *dialog = new QDialog(window_);
        dialog->setObjectName(QStringLiteral("veditElabDialog"));
        dialog->setWindowTitle(QCoreApplication::translate("VEdit", "Project lint errors"));
        auto *layout = new QVBoxLayout(dialog);
        auto *header = new QLabel(dialog);
        header->setObjectName(QStringLiteral("veditElabDialogHeader"));
        header->setWordWrap(true);
        layout->addWidget(header);
        auto *tree = new QTreeWidget(dialog);
        tree->setObjectName(QStringLiteral("veditElabDialogRows"));
        tree->setHeaderHidden(true);
        tree->setRootIsDecorated(false);
        tree->setUniformRowHeights(true);
        tree->setMinimumSize(560, 260);
        layout->addWidget(tree, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::hide);
        connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::hide);
        connect(tree, &QTreeWidget::itemDoubleClicked, this,
                [this, tree](QTreeWidgetItem *item, int) {
                    if (item == nullptr || tree == nullptr) {
                        return;
                    }
                    const int index = item->data(0, Qt::UserRole).toInt();
                    jumpTo(index);
                });
        dialog_ = dialog;
    }
    if (auto *header = dialog_->findChild<QLabel *>(QStringLiteral("veditElabDialogHeader"))) {
        header->setText(
            QCoreApplication::translate(
                "VEdit",
                "The project lint reported %1 error(s). Double-click one to jump to it.")
                .arg(rows_.size()));
    }
    if (auto *tree = dialog_->findChild<QTreeWidget *>(QStringLiteral("veditElabDialogRows"))) {
        tree->clear();
        for (int index = 0; index < rows_.size(); ++index) {
            const DiagnosticEntry &entry = rows_.at(index);
            auto *item = new QTreeWidgetItem(tree);
            item->setText(0, rowText(entry));
            item->setData(0, Qt::UserRole, index);
            item->setToolTip(0, QStringLiteral("%1:%2:%3 — %4")
                                    .arg(entry.file)
                                    .arg(entry.line + 1)
                                    .arg(entry.column + 1)
                                    .arg(entry.message));
        }
    }
    ++dialogsShown_;
    // Modal, but NOT exec(): the caller (a command handler / the pass poll)
    // must return to the event loop, and a headless test must be able to see it.
    dialog_->setModal(true);
    dialog_->show();
    dialog_->raise();
}

void ProjectLintNotice::activateNotice()
{
    if (rows_.isEmpty()) {
        return;
    }
    const int index = nextNoticeRow_ % rows_.size();
    nextNoticeRow_ = index + 1;
    ++noticeJumps_;
    jumpTo(index);
}

void ProjectLintNotice::jumpTo(int index)
{
    if (index < 0 || index >= rows_.size()) {
        return;
    }
    const DiagnosticEntry &entry = rows_.at(index);
    emit jumpRequested(entry.file, entry.line, entry.column);
    vedit::workspace::jump::jumpTo(entry.file, entry.line + 1, entry.column + 1);
}

} // namespace vedit::workspace::diagnostics
