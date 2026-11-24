#include "rename_preview.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace vedit::workspace {

namespace {

// Every user-visible string below is an extractable translate() call, so the
// later i18n-sync card can collect it (R2-6); this file never edits the .ts.
QString plural(quint64 count, const QString &singular, const QString &many)
{
    return QStringLiteral("%1 %2").arg(count).arg(count == 1 ? singular : many);
}

} // namespace

QStringList renameUnsupportedClasses()
{
    // The measured facts behind these lines are in
    // evidence/batch-20261110c/rename-core/03-defect.md §4 and
    // rename_index.rs::plan(): each class is refused WHOLE, because a position
    // inside it has no AST node to rewrite.
    return {
        QCoreApplication::translate(
            "VEdit", "parameter/constant - a type-dimension use (`[WIDTH-1:0]`) has no AST "
                     "position, so the rename would leave the dimension behind"),
        QCoreApplication::translate(
            "VEdit", "instance name - the instance prefix of a hierarchical reference "
                     "(`u_a.sig`) has no AST binding, so the rename would leave it behind"),
        QCoreApplication::translate(
            "VEdit", "port connected at an instantiation - the `.port` connection label has no "
                     "AST position, so the rename would leave the label behind"),
        QCoreApplication::translate(
            "VEdit", "symbol reached through a hierarchical reference - `u_a.sig` binds only "
                     "the leaf name, never the instance path"),
    };
}

QString renamePreviewRowText(const RenamePreviewFile &file, const RenamePreviewHit &hit)
{
    // AC-T13.2 is literal about the shape: file:line plus the before/after text.
    return QStringLiteral("%1:%2  %3  ->  %4")
        .arg(file.label)
        .arg(hit.line)
        .arg(hit.before, hit.after);
}

RenamePreviewDialog::RenamePreviewDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("veditRenamePreviewDialog"));
    setModal(false); // the shell stays usable; jobs run off the GUI thread
    setWindowTitle(QCoreApplication::translate("VEdit", "Rename Symbol in Project"));
    setSizeGripEnabled(true);
    resize(960, 640);

    auto *layout = new QVBoxLayout(this);

    header_ = new QLabel(this);
    header_->setObjectName(QStringLiteral("veditRenameHeader"));
    header_->setWordWrap(true);
    layout->addWidget(header_);

    auto *nameRow = new QHBoxLayout;
    auto *nameLabel = new QLabel(QCoreApplication::translate("VEdit", "New name:"), this);
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName(QStringLiteral("veditRenameName"));
    nameEdit_->setPlaceholderText(
        QCoreApplication::translate("VEdit", "the new SystemVerilog identifier"));
    nameEdit_->setClearButtonEnabled(true);
    nameRow->addWidget(nameLabel);
    nameRow->addWidget(nameEdit_, 1);
    layout->addLayout(nameRow);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("veditRenamePreviewTree"));
    tree_->setColumnCount(1);
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->setRootIsDecorated(true);
    tree_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(tree_, 1);

    total_ = new QLabel(this);
    total_->setObjectName(QStringLiteral("veditRenameTotal"));
    total_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(total_);

    warnings_ = new QLabel(this);
    warnings_->setObjectName(QStringLiteral("veditRenameWarnings"));
    warnings_->setWordWrap(true);
    warnings_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    warnings_->setVisible(false);
    layout->addWidget(warnings_);

    unsupported_ = new QLabel(this);
    unsupported_->setObjectName(QStringLiteral("veditRenameUnsupported"));
    unsupported_->setWordWrap(true);
    unsupported_->setFrameShape(QFrame::StyledPanel);
    unsupported_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    {
        QString block = QCoreApplication::translate(
            "VEdit",
            "Not renamable by this engine (each class is refused WHOLE - this product "
            "never falls back to text replacement):");
        for (const QString &line : renameUnsupportedClasses()) {
            block += QStringLiteral("\n  - ") + line;
        }
        unsupported_->setText(block);
    }
    layout->addWidget(unsupported_);

    hint_ = new QLabel(
        QCoreApplication::translate("VEdit",
                                    "Preview first, then Apply once. Apply writes every file "
                                    "or none; Cancel and closing this dialog write nothing."),
        this);
    hint_->setObjectName(QStringLiteral("veditRenameHint"));
    hint_->setWordWrap(true);
    layout->addWidget(hint_);

    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("veditRenameProgress"));
    progress_->setRange(0, 0); // indeterminate: the engine reports no percentage
    progress_->setTextVisible(false);
    progress_->setVisible(false);
    layout->addWidget(progress_);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("veditRenameStatus"));
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setMinimumHeight(status_->fontMetrics().height() * 2);
    layout->addWidget(status_);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    preview_ = new QPushButton(QCoreApplication::translate("VEdit", "Preview"), this);
    preview_->setObjectName(QStringLiteral("veditRenamePreviewButton"));
    apply_ = new QPushButton(QCoreApplication::translate("VEdit", "Apply"), this);
    apply_->setObjectName(QStringLiteral("veditRenameApplyButton"));
    apply_->setDefault(true);
    cancel_ = new QPushButton(QCoreApplication::translate("VEdit", "Cancel job"), this);
    cancel_->setObjectName(QStringLiteral("veditRenameCancelButton"));
    cancel_->setVisible(false);
    close_ = new QPushButton(QCoreApplication::translate("VEdit", "Close"), this);
    close_->setObjectName(QStringLiteral("veditRenameCloseButton"));
    buttons->addWidget(preview_);
    buttons->addWidget(apply_);
    buttons->addWidget(cancel_);
    buttons->addWidget(close_);
    layout->addLayout(buttons);

    QObject::connect(preview_, &QPushButton::clicked, this, [this]() { previewClicked(); });
    QObject::connect(apply_, &QPushButton::clicked, this, [this]() { applyClicked(); });
    QObject::connect(cancel_, &QPushButton::clicked, this, [this]() { cancelClicked(); });
    QObject::connect(close_, &QPushButton::clicked, this, [this]() { accept(); });
    QObject::connect(nameEdit_, &QLineEdit::returnPressed, this, [this]() { previewClicked(); });

    setStatus(QCoreApplication::translate(
        "VEdit", "Type the new name, then Preview. Nothing is written before Apply."));
    setState(RenamePreviewState::Editing);
}

void RenamePreviewDialog::setRequests(PlanRequest plan, JobRequest apply, JobRequest cancel)
{
    planRequest_ = std::move(plan);
    applyRequest_ = std::move(apply);
    cancelRequest_ = std::move(cancel);
}

void RenamePreviewDialog::setSymbol(const QString &from)
{
    symbol_ = from;
    header_->setText(
        QCoreApplication::translate(
            "VEdit",
            "Rename `%1` across the project. Every planned site is listed by file:line "
            "with its before/after text.")
            .arg(from));
    nameEdit_->clear();
    clearPreview();
    setState(RenamePreviewState::Editing);
    setStatus(QCoreApplication::translate(
        "VEdit", "Type the new name, then Preview. Nothing is written before Apply."));
}

QString RenamePreviewDialog::symbol() const
{
    return symbol_;
}

void RenamePreviewDialog::beginPlan()
{
    clearPreview();
    setState(RenamePreviewState::Planning);
    status_->setText(
        QCoreApplication::translate(
            "VEdit", "Planning the rename: indexing the project and resolving `%1`...")
            .arg(symbol_));
}

void RenamePreviewDialog::beginApply()
{
    setState(RenamePreviewState::Applying);
    status_->setText(QCoreApplication::translate(
        "VEdit", "Applying atomically: every planned file is written, or every written "
                 "file is restored."));
}

void RenamePreviewDialog::clearPreview()
{
    tree_->clear();
    total_->clear();
    warnings_->clear();
    warnings_->setVisible(false);
}

void RenamePreviewDialog::showPlan(const RenamePreviewPlan &plan)
{
    tree_->clear();

    bool truncated = false;
    for (const RenamePreviewFile &file : plan.entries) {
        auto *fileItem = new QTreeWidgetItem(tree_);
        const quint64 hits = static_cast<quint64>(file.hits.size());
        QString head =
            QStringLiteral("%1  (%2)")
                .arg(file.label,
                     plural(hits, QCoreApplication::translate("VEdit", "replacement"),
                     QCoreApplication::translate("VEdit", "replacements")));
        if (file.truncated) {
            truncated = true;
            head += QCoreApplication::translate(
                "VEdit", "  - preview truncated; applying this file is refused");
        }
        fileItem->setText(0, head);
        for (const RenamePreviewHit &hit : file.hits) {
            auto *row = new QTreeWidgetItem(fileItem);
            row->setText(0, renamePreviewRowText(file, hit));
            row->setToolTip(0, renamePreviewRowText(file, hit));
        }
    }
    tree_->expandAll();

    const quint64 files = static_cast<quint64>(plan.entries.size());
    total_->setText(
        QStringLiteral("TOTAL: %1 in %2: `%3` -> `%4` (project `%5`)")
            .arg(plural(plan.occurrences, QCoreApplication::translate("VEdit", "replacement"),
                          QCoreApplication::translate("VEdit", "replacements")),
                 plural(files, QCoreApplication::translate("VEdit", "file"),
                          QCoreApplication::translate("VEdit", "files")), plan.from, plan.to, plan.root));

    if (plan.warnings.isEmpty()) {
        warnings_->clear();
        warnings_->setVisible(false);
    } else {
        warnings_->setText(QCoreApplication::translate("VEdit", "Index warnings: ")
                           + plan.warnings.join(QStringLiteral("; ")));
        warnings_->setVisible(true);
    }

    setState(RenamePreviewState::Ready);
    status_->setText(
        QCoreApplication::translate(
            "VEdit", "Plan ready: %1 in %2. Review every file:line above, then Apply once.")
            .arg(plural(plan.occurrences, QCoreApplication::translate("VEdit", "replacement"),
                          QCoreApplication::translate("VEdit", "replacements")),
                 plural(files, QCoreApplication::translate("VEdit", "file"),
                          QCoreApplication::translate("VEdit", "files"))));
    if (truncated) {
        status_->setText(
            status_->text()
            + QCoreApplication::translate(
                "VEdit", " A truncated file refuses its own apply (no partial plan)."));
    }
}

void RenamePreviewDialog::showPlanRefused(const QString &code, const QString &message)
{
    clearPreview();
    setState(RenamePreviewState::Failed);
    QString line = QStringLiteral("%1: %2").arg(code, message);
    // The frozen C ABI caps one reason at VD_ERROR_MESSAGE_CAPACITY (256) bytes.
    // An ambiguous symbol lists EVERY declaration, so a deep project path can
    // reach the cap; say so instead of letting a cut list look complete.
    if (code.toUtf8().size() + 2 + message.toUtf8().size() >= 255) {
        line += QCoreApplication::translate(
            "VEdit", " [the engine reason reached the C ABI's 255-byte cap; the "
                     "declaration list above may be cut]");
    }
    setStatus(line);
}

void RenamePreviewDialog::showPlanCancelled()
{
    clearPreview();
    setState(RenamePreviewState::Cancelled);
    setStatus(QCoreApplication::translate(
        "VEdit", "Cancelled while planning: nothing was written."));
}

void RenamePreviewDialog::showApplyApplied(quint64 files, quint64 occurrences, bool verified,
                                           bool cancelArrivedLate)
{
    setState(RenamePreviewState::Applied);
    QString line = QCoreApplication::translate(
                       "VEdit", "Applied %1 in %2; verified=%3. The files hold the new name.")
                       .arg(plural(occurrences,
                                   QCoreApplication::translate("VEdit", "replacement"),
                                   QCoreApplication::translate("VEdit", "replacements")),
                            plural(files, QCoreApplication::translate("VEdit", "file"),
                                   QCoreApplication::translate("VEdit", "files")))
                       .arg(verified ? 1 : 0);
    if (cancelArrivedLate) {
        // Never claim a cancel that lost the race: the apply engine had already
        // written and verified every file, so the honest state is Applied.
        line += QCoreApplication::translate(
            "VEdit", " The cancel arrived after the apply had finished; the rename stands.");
    }
    setStatus(line);
}

void RenamePreviewDialog::showApplyFailed(const QString &failedFile, bool rolledBack,
                                          const QString &message)
{
    setState(RenamePreviewState::Failed);
    QString line;
    if (failedFile.isEmpty()) {
        line = QCoreApplication::translate("VEdit", "Apply failed: ");
    } else {
        line = QCoreApplication::translate("VEdit", "Apply failed at %1: ").arg(failedFile);
    }
    line += rolledBack
        ? QCoreApplication::translate(
              "VEdit", "rolled back=1 - every file that was already written was restored "
                       "byte for byte; nothing was renamed.")
        : QCoreApplication::translate(
              "VEdit", "rolled back=0 - at least one file could not be restored; inspect "
                       "the listed file before retrying.");
    if (!message.isEmpty()) {
        line += QStringLiteral(" (%1)").arg(message);
    }
    setStatus(line);
}

void RenamePreviewDialog::showApplyRefused(const QString &code, const QString &message)
{
    setState(RenamePreviewState::Failed);
    setStatus(QCoreApplication::translate(
                  "VEdit", "%1: %2 - the apply job did not start, so nothing was written.")
                  .arg(code, message));
}

void RenamePreviewDialog::showApplyCancelled(bool rolledBack, quint64 filesWritten)
{
    setState(RenamePreviewState::Cancelled);
    // `rolled_back` is only meaningful once something WAS written: the engine's
    // "cancelled before any write" outcome reports applied_files=0 and
    // rolled_back=0 (there was nothing to restore), which is NOT a failed
    // restore. Never tell the user to inspect a project that was untouched.
    if (filesWritten == 0) {
        setStatus(QCoreApplication::translate(
            "VEdit", "Cancelled: nothing was renamed; no file was written."));
    } else if (rolledBack) {
        setStatus(QCoreApplication::translate(
            "VEdit", "Cancelled: nothing was renamed (every written file was restored)."));
    } else {
        setStatus(QCoreApplication::translate(
            "VEdit", "Cancelled, but at least one file could not be restored; inspect the "
                     "project before retrying."));
    }
}

void RenamePreviewDialog::showCancelRequested()
{
    cancel_->setEnabled(false);
    setStatus(QCoreApplication::translate(
        "VEdit", "Cancel requested - waiting for the job to stop..."));
}

void RenamePreviewDialog::setState(RenamePreviewState state)
{
    state_ = state;
    // ONE place decides which controls the current phase exposes, so a click can
    // never start a second job behind a running one.
    refreshButtons();
}

void RenamePreviewDialog::refreshButtons()
{
    const bool busy = state_ == RenamePreviewState::Planning
        || state_ == RenamePreviewState::Applying;
    const bool editable = state_ == RenamePreviewState::Editing
        || state_ == RenamePreviewState::Ready || state_ == RenamePreviewState::Failed
        || state_ == RenamePreviewState::Cancelled;
    preview_->setEnabled(editable);
    apply_->setEnabled(state_ == RenamePreviewState::Ready);
    cancel_->setVisible(busy);
    cancel_->setEnabled(busy);
    cancel_->setText(QCoreApplication::translate("VEdit", "Cancel job"));
    close_->setEnabled(!busy);
    progress_->setVisible(busy);
    nameEdit_->setReadOnly(!editable);
}

void RenamePreviewDialog::setStatus(const QString &line)
{
    status_->setText(line);
    status_->setToolTip(line);
}

void RenamePreviewDialog::previewClicked()
{
    if (state_ == RenamePreviewState::Planning || state_ == RenamePreviewState::Applying) {
        return;
    }
    const QString to = nameEdit_->text().trimmed();
    if (to.isEmpty()) {
        setStatus(QCoreApplication::translate(
            "VEdit", "Type the new name first: the preview needs the target identifier."));
        nameEdit_->setFocus();
        return;
    }
    if (!planRequest_) {
        setStatus(QCoreApplication::translate(
            "VEdit", "This build has no rename job: the preview cannot start."));
        return;
    }
    beginPlan();
    planRequest_(symbol_, to);
}

void RenamePreviewDialog::applyClicked()
{
    // THE one confirmation: exactly one apply job starts from exactly one click,
    // and the phase change disables the button before the request runs.
    if (state_ != RenamePreviewState::Ready) {
        return;
    }
    beginApply();
    if (applyRequest_) {
        applyRequest_();
    }
}

void RenamePreviewDialog::cancelClicked()
{
    if (state_ != RenamePreviewState::Planning && state_ != RenamePreviewState::Applying) {
        return;
    }
    setStatus(QCoreApplication::translate(
        "VEdit", "Cancel requested - waiting for the job to stop..."));
    cancel_->setEnabled(false);
    if (cancelRequest_) {
        cancelRequest_();
    }
}

void RenamePreviewDialog::reject()
{
    if (state_ == RenamePreviewState::Planning || state_ == RenamePreviewState::Applying) {
        // A job must never outlive the dialog that shows its progress.
        setStatus(QCoreApplication::translate(
            "VEdit", "A rename job is running: press Cancel job to stop it, then close."));
        return;
    }
    QDialog::reject();
}

void RenamePreviewDialog::closeEvent(QCloseEvent *event)
{
    if (state_ == RenamePreviewState::Planning || state_ == RenamePreviewState::Applying) {
        setStatus(QCoreApplication::translate(
            "VEdit", "A rename job is running: press Cancel job to stop it, then close."));
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

// --- observable surface -----------------------------------------------------

RenamePreviewState RenamePreviewDialog::state() const
{
    return state_;
}

QString RenamePreviewDialog::statusText() const
{
    return status_->text();
}

QString RenamePreviewDialog::totalText() const
{
    return total_->text();
}

QString RenamePreviewDialog::unsupportedText() const
{
    return unsupported_->text();
}

QString RenamePreviewDialog::hintText() const
{
    return hint_->text();
}

QStringList RenamePreviewDialog::renderedFiles() const
{
    QStringList lines;
    for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
        lines.append(tree_->topLevelItem(index)->text(0));
    }
    return lines;
}

QStringList RenamePreviewDialog::renderedRows() const
{
    QStringList lines;
    for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
        const QTreeWidgetItem *file = tree_->topLevelItem(index);
        for (int child = 0; child < file->childCount(); ++child) {
            lines.append(file->child(child)->text(0));
        }
    }
    return lines;
}

bool RenamePreviewDialog::cancelVisible() const
{
    return cancel_->isVisibleTo(const_cast<RenamePreviewDialog *>(this));
}

bool RenamePreviewDialog::progressVisible() const
{
    return progress_->isVisibleTo(const_cast<RenamePreviewDialog *>(this));
}

bool RenamePreviewDialog::applyEnabled() const
{
    return apply_->isEnabled();
}

bool RenamePreviewDialog::previewEnabled() const
{
    return preview_->isEnabled();
}

QLineEdit *RenamePreviewDialog::nameEdit() const
{
    return nameEdit_;
}

QPushButton *RenamePreviewDialog::previewButton() const
{
    return preview_;
}

QPushButton *RenamePreviewDialog::applyButton() const
{
    return apply_;
}

QPushButton *RenamePreviewDialog::cancelButton() const
{
    return cancel_;
}

QPushButton *RenamePreviewDialog::closeButton() const
{
    return close_;
}

QTreeWidget *RenamePreviewDialog::tree() const
{
    return tree_;
}

QLabel *RenamePreviewDialog::totalLabel() const
{
    return total_;
}

QLabel *RenamePreviewDialog::statusLabel() const
{
    return status_;
}

QLabel *RenamePreviewDialog::unsupportedLabel() const
{
    return unsupported_;
}

QLabel *RenamePreviewDialog::warningsLabel() const
{
    return warnings_;
}

QProgressBar *RenamePreviewDialog::progressBar() const
{
    return progress_;
}

} // namespace vedit::workspace
