#include "terminal_panel.h"

#include "pty_session.h"
#include "terminal_widget.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace vedit::terminal {

TerminalPanel::TerminalPanel(TerminalOptions options, QWidget *parent)
    : QWidget(parent)
    , options_(std::move(options))
{
    setObjectName(QStringLiteral("terminal-panel"));
    buildUi();
    if (options_.autoStart) {
        QString error;
        if (!startShell(options_.shell, options_.workingDirectory, options_.arguments, &error)) {
            banner_->setText(error);
            banner_->setVisible(true);
        }
    }
    updateState();
}

TerminalPanel::~TerminalPanel()
{
    if (session_ != nullptr) {
        if (terminal_ != nullptr) {
            terminal_->attach(nullptr);
        }
        session_->terminate();
        delete session_;
        session_ = nullptr;
    }
}

void TerminalPanel::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 2, 8, 2);
    headerLayout->setSpacing(6);

    shellLabel_ = new QLabel(header);
    shellLabel_->setObjectName(QStringLiteral("terminal-shell"));
    headerLayout->addWidget(shellLabel_);
    headerLayout->addStretch(1);

    const auto addButton = [&headerLayout, header](const QString &text, const QString &name,
                                                   const QString &tooltip) {
        auto *button = new QToolButton(header);
        button->setObjectName(name);
        button->setText(text);
        button->setToolTip(tooltip);
        button->setAutoRaise(true);
        headerLayout->addWidget(button);
        return button;
    };

    QToolButton *restartButton = addButton(QStringLiteral("Restart"), QStringLiteral("terminal-restart"),
                                          QStringLiteral("Close the current shell and open a new session"));
    QToolButton *clearButton = addButton(QStringLiteral("Clear"), QStringLiteral("terminal-clear"),
                                        QStringLiteral("Clear the scrollback of this panel"));
    QToolButton *closeButton = addButton(QStringLiteral("Close"), QStringLiteral("terminal-close"),
                                        QStringLiteral("Close the terminal panel"));

    layout->addWidget(header);

    banner_ = new QLabel(this);
    banner_->setObjectName(QStringLiteral("terminal-exit-banner"));
    banner_->setWordWrap(true);
    banner_->setVisible(false);
    banner_->setStyleSheet(QStringLiteral(
        "background: #5a2b2b; color: #f2dede; padding: 3px 8px;"));
    layout->addWidget(banner_);

    terminal_ = new TerminalWidget(this);
    layout->addWidget(terminal_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("terminal-status"));
    statusLabel_->setContentsMargins(8, 2, 8, 2);
    layout->addWidget(statusLabel_);

    connect(restartButton, &QToolButton::clicked, this, [this]() {
        QString error;
        if (!restart(&error)) {
            banner_->setText(error);
            banner_->setVisible(true);
        }
    });
    connect(clearButton, &QToolButton::clicked, this, [this]() {
        terminal_->screen().reset();
        terminal_->clearSelection();
        terminal_->scrollToBottom();
        terminal_->update();
    });
    connect(closeButton, &QToolButton::clicked, this, [this]() { requestClose(); });
    connect(terminal_, &TerminalWidget::idleActivated, this, [this]() {
        QString error;
        if (!restart(&error)) {
            banner_->setText(error);
            banner_->setVisible(true);
        }
    });
}

void TerminalPanel::requestClose()
{
    emit closeRequested();
    if (auto *dock = qobject_cast<QDockWidget *>(parentWidget())) {
        dock->close();
    }
}

bool TerminalPanel::startShell(const QString &shell, const QString &workingDirectory,
                               const QStringList &arguments, QString *error)
{
    if (session_ != nullptr) {
        terminal_->attach(nullptr);
        session_->terminate();
        delete session_;
        session_ = nullptr;
    }

    options_.shell = shell;
    options_.workingDirectory = workingDirectory;
    options_.arguments = arguments;

    session_ = new PtySession(this);
    connectSession();
    terminal_->attach(session_);

    if (!session_->start(options_.shell, options_.workingDirectory, options_.arguments, options_.columns,
                         options_.rows, error)) {
        updateState();
        return false;
    }

    running_ = true;
    lastDescription_.clear();
    banner_->setVisible(false);
    banner_->clear();
    updateState();
    terminal_->setFocus(Qt::OtherFocusReason);
    return true;
}

bool TerminalPanel::restart(QString *error)
{
    terminal_->screen().reset();
    terminal_->clearSelection();
    terminal_->scrollToBottom();
    return startShell(options_.shell, options_.workingDirectory, options_.arguments, error);
}

void TerminalPanel::stop()
{
    if (session_ != nullptr) {
        session_->terminate();
    }
}

void TerminalPanel::connectSession()
{
    connect(session_, &PtySession::finished, this,
            [this](int exitCode, int signal, const QString &description) {
                handleFinished(exitCode, signal, description);
            });
}

void TerminalPanel::handleFinished(int exitCode, int signal, const QString &description)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(signal);
    running_ = false;
    lastDescription_ = description;
    banner_->setText(QStringLiteral("Shell %1. Press Enter or Restart to start a new session.")
                         .arg(description));
    banner_->setVisible(true);
    updateState();
    emit shellFinished(description);
}

void TerminalPanel::updateState()
{
    const QString path = session_ != nullptr ? session_->shellPath() : QString();
    shellLabel_->setText(path.isEmpty() ? QStringLiteral("Shell") : QStringLiteral("Shell: %1").arg(path));

    if (session_ == nullptr) {
        statusLabel_->setText(QStringLiteral("Not started"));
        return;
    }
    if (running_ && session_->isRunning()) {
        statusLabel_->setText(QStringLiteral("Running · pid %1").arg(session_->pid()));
        return;
    }
    if (session_->terminationSignal() != 0) {
        statusLabel_->setText(QStringLiteral("Killed · signal %1 (%2)")
                                  .arg(session_->terminationSignal())
                                  .arg(session_->exitDescription()));
        return;
    }
    if (session_->exitKnown()) {
        statusLabel_->setText(QStringLiteral("Exited · code %1").arg(session_->exitCode()));
        return;
    }
    statusLabel_->setText(QStringLiteral("Stopped · %1").arg(lastDescription_.isEmpty()
                                                                 ? session_->exitDescription()
                                                                 : lastDescription_));
}

QString TerminalPanel::stateText() const
{
    return statusLabel_->text();
}

QString TerminalPanel::bannerText() const
{
    // The text is the state: startShell()/restart() clear it, so a panel that is
    // not shown (a test, a hidden dock) still reports the banner it would show.
    return banner_->text();
}

QString TerminalPanel::shellPath() const
{
    return session_ != nullptr ? session_->shellPath() : QString();
}

bool TerminalPanel::isRunning() const
{
    return session_ != nullptr && session_->isRunning();
}

QWidget *createTerminalPanel()
{
    return new TerminalPanel();
}

std::function<QWidget *()> terminalPanelFactory()
{
    return []() -> QWidget * { return new TerminalPanel(); };
}

} // namespace vedit::terminal
