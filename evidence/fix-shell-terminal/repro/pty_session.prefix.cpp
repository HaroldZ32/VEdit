#include "pty_session.h"

#include <QFileInfo>
#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace vedit::terminal {

namespace {

// Everything the child needs is prepared BEFORE fork(): a forked child of a Qt
// application may only call async-signal-safe functions, so allocating strings or
// vectors in the child is out of the question.
struct ChildPlan {
    std::string shell;
    std::string workingDirectory;
    std::vector<std::string> arguments;
    std::vector<std::string> environment;
    std::vector<char *> argv;
    std::vector<char *> envp;
};

void setEnvironment(ChildPlan &plan, const std::string &name, const std::string &value)
{
    const std::string prefix = name + "=";
    for (std::string &entry : plan.environment) {
        if (entry.compare(0, prefix.size(), prefix) == 0) {
            entry = prefix + value;
            return;
        }
    }
    plan.environment.push_back(prefix + value);
}

QString defaultShell()
{
    const QString fromEnvironment = qEnvironmentVariable("SHELL");
    if (!fromEnvironment.isEmpty() && QFileInfo(fromEnvironment).isExecutable()) {
        return fromEnvironment;
    }
    if (QFileInfo(QStringLiteral("/bin/bash")).isExecutable()) {
        return QStringLiteral("/bin/bash");
    }
    return QStringLiteral("/bin/sh");
}

} // namespace

PtySession::PtySession(QObject *parent)
    : QObject(parent)
{
}

PtySession::~PtySession()
{
    if (running_ && pid_ > 0) {
        ::kill(static_cast<pid_t>(pid_), SIGHUP);
        for (int attempt = 0; attempt < 50 && running_; ++attempt) {
            int status = 0;
            const pid_t waited = ::waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
            if (waited == static_cast<pid_t>(pid_)) {
                running_ = false;
                break;
            }
            ::usleep(10000);
        }
        if (running_) {
            ::kill(static_cast<pid_t>(pid_), SIGKILL);
            int status = 0;
            ::waitpid(static_cast<pid_t>(pid_), &status, 0);
            running_ = false;
        }
    }
    stopNotifier();
    closeMaster();
}

bool PtySession::start(const QString &shell,
                       const QString &workingDirectory,
                       const QStringList &arguments,
                       int columns,
                       int rows,
                       QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    if (running_) {
        return fail(QStringLiteral("a shell session is already running (pid %1)").arg(pid_));
    }

    const QString resolved = shell.isEmpty() ? defaultShell() : shell;
    if (!QFileInfo(resolved).isExecutable()) {
        return fail(QStringLiteral("shell is not executable: %1").arg(resolved));
    }

    columns = std::max(1, columns);
    rows = std::max(1, rows);

    ChildPlan plan;
    plan.shell = resolved.toStdString();
    plan.workingDirectory = workingDirectory.toStdString();
    plan.arguments.emplace_back(plan.shell);
    for (const QString &argument : arguments) {
        plan.arguments.emplace_back(argument.toStdString());
    }
    for (char **entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        plan.environment.emplace_back(*entry);
    }
    setEnvironment(plan, "TERM", "xterm-256color");
    setEnvironment(plan, "COLORTERM", "truecolor");
    setEnvironment(plan, "TERM_PROGRAM", "VEdit");
    for (std::string &argument : plan.arguments) {
        plan.argv.push_back(argument.data());
    }
    plan.argv.push_back(nullptr);
    for (std::string &entry : plan.environment) {
        plan.envp.push_back(entry.data());
    }
    plan.envp.push_back(nullptr);

    errno = 0;
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        return fail(QStringLiteral("posix_openpt failed: %1").arg(QString::fromLocal8Bit(::strerror(errno))));
    }
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        const QString message = QStringLiteral("grantpt/unlockpt failed: %1").arg(QString::fromLocal8Bit(::strerror(errno)));
        ::close(master);
        return fail(message);
    }
    const char *slaveName = ::ptsname(master);
    if (slaveName == nullptr) {
        const QString message = QStringLiteral("ptsname failed: %1").arg(QString::fromLocal8Bit(::strerror(errno)));
        ::close(master);
        return fail(message);
    }
    const std::string slavePath(slaveName);

    struct winsize size {};
    size.ws_col = static_cast<unsigned short>(columns);
    size.ws_row = static_cast<unsigned short>(rows);
    ::ioctl(master, TIOCSWINSZ, &size);

    const pid_t child = ::fork();
    if (child < 0) {
        const QString message = QStringLiteral("fork failed: %1").arg(QString::fromLocal8Bit(::strerror(errno)));
        ::close(master);
        return fail(message);
    }
    if (child == 0) {
        // Child: only async-signal-safe calls from here on.
        ::setsid();
        const int slave = ::open(slavePath.c_str(), O_RDWR);
        if (slave < 0) {
            ::_exit(127);
        }
        ::ioctl(slave, TIOCSCTTY, 0);
        ::dup2(slave, STDIN_FILENO);
        ::dup2(slave, STDOUT_FILENO);
        ::dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) {
            ::close(slave);
        }
        if (!plan.workingDirectory.empty()) {
            if (::chdir(plan.workingDirectory.c_str()) != 0) {
                // Keep the shell at the inherited directory; the prompt shows it.
            }
        }
        ::execve(plan.shell.c_str(), plan.argv.data(), plan.envp.data());
        ::_exit(127);
    }

    const int flags = ::fcntl(master, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(master, F_SETFL, flags | O_NONBLOCK);
    }

    master_ = master;
    pid_ = static_cast<qint64>(child);
    running_ = true;
    exitKnown_ = false;
    exitCode_ = -1;
    terminationSignal_ = 0;
    shellPath_ = resolved;
    pendingWrite_.clear();

    notifier_ = new QSocketNotifier(master_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this]() { readAvailable(); });

    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(50);
    connect(pollTimer_, &QTimer::timeout, this, [this]() { pollChild(); });
    pollTimer_->start();

    return true;
}

void PtySession::write(const QByteArray &data)
{
    if (master_ < 0 || data.isEmpty()) {
        return;
    }
    pendingWrite_.append(data);
    flushWrites();
}

void PtySession::flushWrites()
{
    while (!pendingWrite_.isEmpty() && master_ >= 0) {
        const ssize_t written = ::write(master_, pendingWrite_.constData(), static_cast<size_t>(pendingWrite_.size()));
        if (written > 0) {
            pendingWrite_.remove(0, static_cast<int>(written));
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (writeNotifier_ == nullptr) {
                writeNotifier_ = new QSocketNotifier(master_, QSocketNotifier::Write, this);
                connect(writeNotifier_, &QSocketNotifier::activated, this, [this]() { flushWrites(); });
            }
            writeNotifier_->setEnabled(true);
            return;
        }
        pendingWrite_.clear();
        return;
    }
    if (writeNotifier_ != nullptr) {
        writeNotifier_->setEnabled(false);
    }
}

void PtySession::resize(int columns, int rows)
{
    if (master_ < 0) {
        return;
    }
    struct winsize size {};
    size.ws_col = static_cast<unsigned short>(std::max(1, columns));
    size.ws_row = static_cast<unsigned short>(std::max(1, rows));
    ::ioctl(master_, TIOCSWINSZ, &size);
}

void PtySession::terminate()
{
    if (!running_ || pid_ <= 0) {
        return;
    }
    ::kill(static_cast<pid_t>(pid_), SIGHUP);
    const qint64 leaving = pid_;
    QTimer::singleShot(500, this, [this, leaving]() {
        if (running_ && pid_ == leaving) {
            ::kill(static_cast<pid_t>(leaving), SIGKILL);
        }
    });
}

void PtySession::readAvailable()
{
    qint64 activationBytes = 0;
    char buffer[8192];
    for (;;) {
        const ssize_t count = ::read(master_, buffer, sizeof buffer);
        if (count > 0) {
            activationBytes += count;
            emit outputReady(QByteArray(buffer, static_cast<int>(count)));
            continue;
        }
        if (count == 0) {
            // EOF: the slave side is gone; the child is being reaped by the timer.
            stopNotifier();
            pollChild();
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        // EIO is the normal "all writers closed" result on Linux ptys.
        stopNotifier();
        pollChild();
        break;
    }
    noteActivation(activationBytes, false);
}

void PtySession::noteActivation(qint64 bytes, bool capped)
{
    if (bytes > maxBytesPerActivation_) {
        maxBytesPerActivation_ = bytes;
    }
    if (capped) {
        ++cappedActivations_;
    }
}

void PtySession::pollChild()
{
    if (pid_ <= 0 || !running_) {
        return;
    }
    int status = 0;
    const pid_t waited = ::waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
    if (waited == static_cast<pid_t>(pid_)) {
        finalize(status);
    }
}

void PtySession::finalize(int status)
{
    running_ = false;
    if (WIFEXITED(status)) {
        exitKnown_ = true;
        exitCode_ = WEXITSTATUS(status);
        terminationSignal_ = 0;
    } else if (WIFSIGNALED(status)) {
        exitKnown_ = true;
        terminationSignal_ = WTERMSIG(status);
        exitCode_ = 128 + terminationSignal_;
    } else {
        exitKnown_ = false;
        exitCode_ = -1;
        terminationSignal_ = 0;
    }
    if (pollTimer_ != nullptr) {
        pollTimer_->stop();
    }
    stopNotifier();
    closeMaster();
    emit finished(exitCode_, terminationSignal_, exitDescription());
}

void PtySession::stopNotifier()
{
    if (notifier_ != nullptr) {
        notifier_->setEnabled(false);
        notifier_->deleteLater();
        notifier_ = nullptr;
    }
    if (writeNotifier_ != nullptr) {
        writeNotifier_->setEnabled(false);
        writeNotifier_->deleteLater();
        writeNotifier_ = nullptr;
    }
}

void PtySession::closeMaster()
{
    if (master_ >= 0) {
        ::close(master_);
        master_ = -1;
    }
}

QString PtySession::signalName(int signal)
{
    const char *abbreviation = ::sigabbrev_np(signal);
    if (abbreviation == nullptr) {
        return QStringLiteral("signal %1").arg(signal);
    }
    return QStringLiteral("SIG") + QString::fromLatin1(abbreviation);
}

bool PtySession::exitKnown() const
{
    return exitKnown_;
}

QString PtySession::exitDescription() const
{
    if (!exitKnown_) {
        return running_ ? QStringLiteral("running") : QStringLiteral("exit status unknown");
    }
    if (terminationSignal_ != 0) {
        return QStringLiteral("killed by signal %1 (%2)").arg(terminationSignal_).arg(signalName(terminationSignal_));
    }
    return QStringLiteral("exited with code %1").arg(exitCode_);
}

} // namespace vedit::terminal
