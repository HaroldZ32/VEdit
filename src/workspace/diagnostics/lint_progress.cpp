#include "lint_progress.h"
#include <QCoreApplication>

#include "diagnostics_controller.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStringLiteral>
#include <QTimer>
#include <QVBoxLayout>

namespace vedit::workspace::diagnostics {

LintProgressBar::LintProgressBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("lint-progress"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("lint-progress-bar"));
    progress_->setRange(0, 100);
    progress_->setTextVisible(true);
    progress_->setFixedHeight(16);
    layout->addWidget(progress_);

    auto *row = new QHBoxLayout();
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("lint-progress-status"));
    status_->setWordWrap(true);
    row->addWidget(status_, 1);
    cancel_ = new QPushButton(QCoreApplication::translate("VEdit", "Cancel"), this);
    cancel_->setObjectName(QStringLiteral("lint-progress-cancel"));
    rerun_ = new QPushButton(QCoreApplication::translate("VEdit", "Re-run"), this);
    rerun_->setObjectName(QStringLiteral("lint-progress-rerun"));
    row->addWidget(cancel_);
    row->addWidget(rerun_);
    layout->addLayout(row);

    connect(cancel_, &QPushButton::clicked, this, &LintProgressBar::requestCancel);
    connect(rerun_, &QPushButton::clicked, this, &LintProgressBar::requestRerun);

    timer_ = new QTimer(this);
    timer_->setInterval(100);
    connect(timer_, &QTimer::timeout, this, &LintProgressBar::refreshFromController);
    timer_->start();

    applySnapshot(LintPassSnapshot{});
}

void LintProgressBar::setController(DiagnosticsController *controller)
{
    controller_ = controller;
    refreshFromController();
}

void LintProgressBar::setPollInterval(int milliseconds)
{
    timer_->setInterval(milliseconds);
    if (!timer_->isActive()) {
        timer_->start();
    }
}

int LintProgressBar::pollInterval() const
{
    return timer_->interval();
}

void LintProgressBar::refreshFromController()
{
    if (controller_ == nullptr) {
        return;
    }
    // The strip drives the job + poll: refresh() is cheap and idempotent.
    controller_->refresh();
    applySnapshot(controller_->projectPass());
}

void LintProgressBar::applySnapshot(const LintPassSnapshot &snapshot)
{
    snapshot_ = snapshot;
    if (snapshot.phase != LintPhase::Idle) {
        everStarted_ = true;
    }
    const bool running = snapshot.running();
    progress_->setValue(snapshot.percent());
    if (snapshot.total > 0) {
        progress_->setFormat(QStringLiteral("%1/%2").arg(snapshot.done).arg(snapshot.total));
    } else {
        progress_->setFormat(QStringLiteral("-%"));
    }
    status_->setText(snapshot.message.isEmpty()
                         ? (running ? QCoreApplication::translate("VEdit", "linting…") : QCoreApplication::translate("VEdit", "idle"))
                         : snapshot.message);
    cancel_->setVisible(running);
    cancel_->setEnabled(running);
    // A rerun is possible after any pass that has a request behind it. A-15: a
    // pass with no recorded request (an empty file set) must not show an enabled
    // button — that was the silent dead button after a first-run `resolve` failure.
    const bool rerunAvailable = controller_ == nullptr || controller_->rerunAvailable();
    const bool canRerun = !running && snapshot.phase != LintPhase::Idle && rerunAvailable;
    rerun_->setVisible(canRerun);
    rerun_->setEnabled(canRerun);
    rerun_->setToolTip(rerunAvailable
                           ? QString()
                           : QCoreApplication::translate("VEdit", "no request to re-run: the last pass had no files"));
    // The strip exists for the pass: hidden until one has started, and hidden
    // again while the controller has none (the shell only sees a started pass).
    setVisible(everStarted_ && snapshot.phase != LintPhase::Idle);
}

QString LintProgressBar::statusText() const
{
    return status_->text();
}

QString LintProgressBar::progressText() const
{
    return progress_->format();
}

bool LintProgressBar::running() const
{
    return snapshot_.running();
}

bool LintProgressBar::cancelVisible() const
{
    return !cancel_->isHidden();
}

bool LintProgressBar::rerunEnabled() const
{
    return rerun_->isEnabled();
}

QString LintProgressBar::rerunHint() const
{
    return rerun_->toolTip();
}

bool LintProgressBar::stripVisible() const
{
    return !isHidden();
}

int LintProgressBar::percent() const
{
    return progress_->value();
}

void LintProgressBar::requestCancel()
{
    if (controller_ == nullptr) {
        return;
    }
    controller_->cancelProjectPass();
    refreshFromController();
    emit cancelRequested();
}

void LintProgressBar::requestRerun()
{
    if (controller_ == nullptr) {
        return;
    }
    QString error;
    if (!controller_->rerunProjectPass(&error)) {
        // A-15: never a silent no-op. The refusal is shown in the same status
        // line the pass uses and the button stays disabled with the reason.
        status_->setText(error.isEmpty() ? QCoreApplication::translate("VEdit", "cannot re-run this pass") : error);
        rerun_->setEnabled(false);
        rerun_->setToolTip(status_->text());
        return;
    }
    refreshFromController();
    emit rerunRequested();
}

} // namespace vedit::workspace::diagnostics
