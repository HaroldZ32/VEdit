#pragma once

// One local shell session for the terminal panel (task T35).
//
// The session is a child process attached to a POSIX pseudo terminal: the child
// gets a real tty (so an interactive shell behaves as it does in a terminal),
// the panel owns the master side through QSocketNotifier, and the child's exit
// status is reaped and reported -- that is what makes "退出码与进程结束状态可见"
// true (contract (b)).
//
// Deliberately local-only: no socket, no listener, no remote transport (the
// product's non-goal 4). See src/terminal/README.md.
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

class QSocketNotifier;
class QTimer;

namespace vedit::terminal {

class PtySession final : public QObject {
    Q_OBJECT

public:
    explicit PtySession(QObject *parent = nullptr);
    ~PtySession() override;

    // Spawns `shell` (default: $SHELL, then /bin/bash, then /bin/sh) with
    // `arguments` on a fresh pty of `columns` x `rows`. Returns false and fills
    // `error` when the pty or the child cannot be created; nothing is left
    // running in that case.
    bool start(const QString &shell = QString(),
               const QString &workingDirectory = QString(),
               const QStringList &arguments = QStringList(),
               int columns = 80,
               int rows = 24,
               QString *error = nullptr);

    void write(const QByteArray &data);
    void resize(int columns, int rows);
    void terminate();

    bool isRunning() const { return running_; }
    qint64 pid() const { return pid_; }
    QString shellPath() const { return shellPath_; }

    // Exit status as the shell reports it: the child's own code when it exited
    // normally, 128 + signal when it was killed, -1 while unknown.
    int exitCode() const { return exitCode_; }
    int terminationSignal() const { return terminationSignal_; }
    bool exitKnown() const;
    // "running" / "exited with code 0" / "killed by signal 15 (SIGTERM)".
    QString exitDescription() const;

signals:
    void outputReady(const QByteArray &data);
    void finished(int exitCode, int signal, const QString &description);

private slots:
    void readAvailable();
    void pollChild();
    void flushWrites();

private:
    void stopNotifier();
    void closeMaster();
    void finalize(int status);
    static QString signalName(int signal);

    int master_ = -1;
    qint64 pid_ = -1;
    bool running_ = false;
    bool exitKnown_ = false;
    int exitCode_ = -1;
    int terminationSignal_ = 0;
    QString shellPath_;
    QSocketNotifier *notifier_ = nullptr;
    QSocketNotifier *writeNotifier_ = nullptr;
    QTimer *pollTimer_ = nullptr;
    QByteArray pendingWrite_;
};

} // namespace vedit::terminal
