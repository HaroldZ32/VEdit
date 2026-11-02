#pragma once

// Terminal panel: the content the application shell inserts into its "terminal"
// dock (task T35). It owns the local shell session, shows its exit status, and
// can be closed and rebuilt.
//
// Panels are created through PanelRegistry::setFactory("terminal", ...) -- the
// factory signature is std::function<QWidget *()>, so `createTerminalPanel()` and
// `terminalPanelFactory()` below are the two entry points the shell can use.
// Remote sessions are not supported by design (product non-goal 4): the panel
// never opens a socket.
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;

namespace vedit::terminal {

class PtySession;
class TerminalWidget;

struct TerminalOptions {
    QString shell;              // empty => $SHELL, then /bin/bash, then /bin/sh
    QString workingDirectory;   // empty => inherit the application's directory
    QStringList arguments;
    int columns = 80;
    int rows = 24;
    bool autoStart = true;      // start a shell as soon as the panel is created
};

class TerminalPanel final : public QWidget {
    Q_OBJECT

public:
    explicit TerminalPanel(TerminalOptions options = TerminalOptions(), QWidget *parent = nullptr);
    ~TerminalPanel() override;

    // Replaces the current session with a new one on the same options.
    bool startShell(const QString &shell = QString(),
                    const QString &workingDirectory = QString(),
                    const QStringList &arguments = QStringList(),
                    QString *error = nullptr);
    bool restart(QString *error = nullptr);
    void stop();

    PtySession *session() const { return session_; }
    TerminalWidget *terminal() const { return terminal_; }
    QString shellPath() const;
    bool isRunning() const;
    QString stateText() const;
    QString bannerText() const;
    const TerminalOptions &options() const { return options_; }

signals:
    void closeRequested();
    void shellFinished(const QString &description);

private:
    void buildUi();
    void connectSession();
    void handleFinished(int exitCode, int signal, const QString &description);
    void updateState();
    void requestClose();

    TerminalOptions options_;
    PtySession *session_ = nullptr;
    TerminalWidget *terminal_ = nullptr;
    QLabel *shellLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *banner_ = nullptr;
    bool running_ = false;
    QString lastDescription_;
};

// Factories for PanelRegistry::setFactory("terminal", ...).
QWidget *createTerminalPanel();
std::function<QWidget *()> terminalPanelFactory();

} // namespace vedit::terminal
