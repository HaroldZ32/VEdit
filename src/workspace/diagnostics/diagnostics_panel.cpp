#include "diagnostics_panel.h"

#include "diagnostics_controller.h"
#include "diagnostics_outline_model.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLabel>
#include <QStringLiteral>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace vedit::workspace::diagnostics {

DiagnosticsPanel::DiagnosticsPanel(DiagnosticsController *controller, QWidget *parent)
    : QWidget(parent)
    , controller_(controller)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("diagnosticsStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);

    model_ = new OutlineModel(this);
    view_ = new QTreeView(this);
    view_->setObjectName(QStringLiteral("diagnosticsOutline"));
    view_->setModel(model_);
    view_->setRootIsDecorated(false);
    view_->setAlternatingRowColors(true);
    layout->addWidget(view_, 1);

    // One-line empty state. A missing toolchain is an environment state, not a
    // list of per-file errors: the outline stays empty and says why once.
    emptyState_ = new QLabel(this);
    emptyState_->setObjectName(QStringLiteral("diagnosticsEmptyState"));
    emptyState_->setWordWrap(true);
    layout->addWidget(emptyState_);

    refreshFromController();

    // The analysis is asynchronous (LSP + slang jobs): poll the controller, and
    // repaint the widgets whenever anything observable changed. Before T118 the
    // panel refreshed only in its constructor, so a finished analysis was never
    // shown - that is why the outline stayed empty on a real desktop.
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(300);
    connect(pollTimer_, &QTimer::timeout, this, [this]() { poll(); });
    pollTimer_->start();
}

void DiagnosticsPanel::poll()
{
    if (controller_ != nullptr) {
        controller_->refresh();
    }
    refreshFromController();
}

QString DiagnosticsPanel::statusText() const
{
    if (status_ == nullptr) {
        return QString();
    }
    return status_->text();
}

int DiagnosticsPanel::outlineRowCount() const
{
    return model_ != nullptr ? model_->rowCount() : 0;
}

QString DiagnosticsPanel::emptyStateText() const
{
    return emptyState_ != nullptr ? emptyState_->text() : QString();
}

bool DiagnosticsPanel::emptyStateVisible() const
{
    return emptyState_ != nullptr && emptyState_->isVisibleTo(this);
}

DiagnosticsController *DiagnosticsPanel::controller() const
{
    return controller_;
}

void DiagnosticsPanel::refreshFromController()
{
    if (controller_ == nullptr) {
        if (status_ != nullptr) {
            status_->setText(QStringLiteral("no diagnostics controller"));
        }
        return;
    }
    if (model_ != nullptr) {
        model_->setSymbols(controller_->outline());
    }
    if (status_ != nullptr) {
        const QString toolchain = controller_->toolchainReport().missing().isEmpty()
            ? QString()
            : controller_->toolchainReport().userMessage();
        if (!toolchain.isEmpty()) {
            status_->setText(toolchain);
        } else if (!controller_->analyzeFileName().isEmpty()) {
            const QDateTime at = controller_->lastAnalysisAt();
            status_->setText(QStringLiteral("%1 — analyzed %2 (%3): %4 diagnostic(s)")
                                 .arg(QFileInfo(controller_->analyzeFileName()).fileName(),
                                      at.isValid() ? at.toString(QStringLiteral("HH:mm:ss"))
                                                   : QStringLiteral("pending"),
                                      controller_->analyzeWasDirty()
                                          ? QStringLiteral("editor buffer, not saved yet")
                                          : QStringLiteral("saved file"),
                                      QString::number(controller_->diagnostics().size())));
        } else {
            status_->setText(QStringLiteral("%1 — %2 diagnostic(s)")
                                 .arg(controller_->session().message)
                                 .arg(controller_->diagnostics().size()));
        }
    }
    if (emptyState_ != nullptr) {
        const QStringList missing = controller_->toolchainReport().missing();
        if (model_ != nullptr && model_->rowCount() > 0) {
            emptyState_->clear();
            emptyState_->setVisible(false);
        } else if (!missing.isEmpty()) {
            emptyState_->setText(QStringLiteral("No symbols — the outline backend is missing (%1). "
                                                "See the status line above for the one-line fix.")
                                     .arg(missing.join(QStringLiteral(", "))));
            emptyState_->setVisible(true);
        } else if (!controller_->analyzeFileName().isEmpty()) {
            emptyState_->setText(QStringLiteral("No symbols yet for %1 — analysis is starting, "
                                                "or this file has no structure.")
                                     .arg(QFileInfo(controller_->analyzeFileName()).fileName()));
            emptyState_->setVisible(true);
        } else {
            emptyState_->setText(QStringLiteral("No symbols yet — open a file; analysis starts "
                                                "automatically."));
            emptyState_->setVisible(true);
        }
    }
}

QWidget *createOutlinePanel(DiagnosticsController *controller)
{
    return new DiagnosticsPanel(controller);
}

} // namespace vedit::workspace::diagnostics
