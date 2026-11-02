#include "crash_reporter.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace vedit::app::crash {
namespace {

// Fixed-size state: the signal handler must never allocate.
constexpr int kEventSlots = 32;
constexpr int kEventText = 192;
constexpr int kPathSize = 1024;
constexpr int kBuildSize = 512;
constexpr int kFileSize = 512;
constexpr int kBacktraceFrames = 64;
constexpr int kAppImageShaSize = 96;

struct EventSlot {
    unsigned sequence;
    char text[kEventText];
};

char g_reportDirectory[kPathSize];
char g_buildInfo[kBuildSize];
char g_currentFile[kFileSize];
char g_appImage[2 * kPathSize];
char g_appImageSha[kAppImageShaSize];
EventSlot g_events[kEventSlots];
volatile sig_atomic_t g_eventSequence = 0;
volatile sig_atomic_t g_installed = 0;
char g_alternateStack[64 * 1024];

void copyBounded(char *destination, const char *source, std::size_t capacity)
{
    if (capacity == 0) {
        return;
    }
    std::size_t index = 0;
    if (source != nullptr) {
        while (source[index] != '\0' && index + 1 < capacity) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

// --- async-signal-safe text helpers ---------------------------------------
void writeText(int fd, const char *text)
{
    if (text == nullptr) {
        return;
    }
    std::size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    std::size_t written = 0;
    while (written < length) {
        const ssize_t chunk = ::write(fd, text + written, length - written);
        if (chunk <= 0) {
            return;
        }
        written += static_cast<std::size_t>(chunk);
    }
}

void writeNumber(int fd, long long value)
{
    char digits[32];
    int position = 0;
    bool negative = value < 0;
    unsigned long long magnitude = negative ? static_cast<unsigned long long>(-value)
                                            : static_cast<unsigned long long>(value);
    if (magnitude == 0) {
        digits[position++] = '0';
    }
    while (magnitude > 0 && position < 31) {
        digits[position++] = static_cast<char>('0' + (magnitude % 10));
        magnitude /= 10;
    }
    if (negative) {
        writeText(fd, "-");
    }
    while (position > 0) {
        char one[2] = {digits[--position], '\0'};
        writeText(fd, one);
    }
}

void writeHex(int fd, unsigned long long value)
{
    static const char kDigits[] = "0123456789abcdef";
    char buffer[2 * sizeof(unsigned long long) + 1];
    int position = static_cast<int>(sizeof(buffer)) - 1;
    buffer[position] = '\0';
    if (value == 0) {
        buffer[--position] = '0';
    }
    while (value > 0 && position > 0) {
        buffer[--position] = kDigits[value & 0xF];
        value >>= 4;
    }
    writeText(fd, &buffer[position]);
}

const char *signalName(int signal)
{
    switch (signal) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGABRT:
        return "SIGABRT";
    case SIGTRAP:
        return "SIGTRAP";
    default:
        return "SIGUNKNOWN";
    }
}

void writeReport(int signal, const siginfo_t *info, int fd)
{
    writeText(fd, "vedit crash report\n");
    writeText(fd, "signal=");
    writeNumber(fd, signal);
    writeText(fd, " (");
    writeText(fd, signalName(signal));
    writeText(fd, ")\n");
    if (info != nullptr) {
        writeText(fd, "si_code=");
        writeNumber(fd, info->si_code);
        writeText(fd, "\nfault_address=0x");
        writeHex(fd, reinterpret_cast<unsigned long long>(info->si_addr));
        writeText(fd, "\n");
    }
    writeText(fd, "pid=");
    writeNumber(fd, static_cast<long long>(::getpid()));
    writeText(fd, "\nbuild=");
    writeText(fd, g_buildInfo[0] != '\0' ? g_buildInfo : "<unknown>");
    writeText(fd, "\n");
    if (g_appImage[0] != '\0') {
        writeText(fd, "appimage=");
        writeText(fd, g_appImage);
        writeText(fd, "\nappimage_sha256=");
        writeText(fd, g_appImageSha[0] != '\0' ? g_appImageSha : "<not computed>");
        writeText(fd, "\n");
    }
    writeText(fd, "current_file=");
    writeText(fd, g_currentFile[0] != '\0' ? g_currentFile : "<none>");
    writeText(fd, "\nlast_events:\n");
    const unsigned current = static_cast<unsigned>(g_eventSequence);
    const unsigned first = current > kEventSlots ? current - (kEventSlots - 1) : 1;
    for (unsigned sequence = first; sequence != 0 && sequence <= current; ++sequence) {
        const EventSlot &slot = g_events[sequence % kEventSlots];
        if (slot.sequence != sequence) {
            continue;
        }
        writeText(fd, "  [");
        writeNumber(fd, static_cast<long long>(sequence));
        writeText(fd, "] ");
        writeText(fd, slot.text);
        writeText(fd, "\n");
    }
    writeText(fd, "backtrace:\n");
    void *frames[kBacktraceFrames];
    // warm up once at install(); this call itself is allowed to fail.
    const int count = ::backtrace(frames, kBacktraceFrames);
    ::backtrace_symbols_fd(frames, count, fd);
    writeText(fd, "end\n");
    ::fsync(fd);
}

QString stateRootFromEnvironment()
{
    const QByteArray xdg = qgetenv("XDG_STATE_HOME");
    if (!xdg.isEmpty()) {
        return QString::fromLocal8Bit(xdg) + QStringLiteral("/vedit");
    }
    const QByteArray home = qgetenv("HOME");
    if (!home.isEmpty()) {
        return QString::fromLocal8Bit(home) + QStringLiteral("/.local/state/vedit");
    }
    return QDir::tempPath() + QStringLiteral("/vedit-state");
}

QString appImageSha256(const QString &path)
{
    // A 95 MB AppImage takes a moment; cap the work so startup stays snappy.
    // AppRun may export the hash directly, which is instant.
    const QByteArray exported = qgetenv("VEDIT_APPIMAGE_SHA256");
    if (!exported.isEmpty()) {
        return QString::fromLatin1(exported.left(kAppImageShaSize - 1));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("<unreadable>");
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const quint64 cap = 160ull * 1024ull * 1024ull;
    quint64 consumed = 0;
    while (!file.atEnd() && consumed < cap) {
        const QByteArray chunk = file.read(1 << 20);
        if (chunk.isEmpty()) {
            break;
        }
        hash.addData(chunk);
        consumed += static_cast<quint64>(chunk.size());
    }
    if (consumed >= cap) {
        return QStringLiteral("<too large to hash>");
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

// Not static: `-rdynamic` exports it so a backtrace shows a NAME for the
// frame that is actually handling the crash (T117).
extern "C" void vedit_crash_handler(int signal, siginfo_t *info, void *)
{
    // Compose the path without allocation: <dir>/crash-<pid>-<time>.log
    char path[kPathSize + 128];
    std::size_t length = 0;
    const char *directory = g_reportDirectory;
    while (directory[length] != '\0' && length + 64 < sizeof(path)) {
        path[length] = directory[length];
        ++length;
    }
    const char *suffix = "/crash-";
    while (*suffix != '\0' && length + 64 < sizeof(path)) {
        path[length++] = *suffix++;
    }
    auto appendNumber = [&](long long value) {
        char digits[24];
        int position = 0;
        unsigned long long magnitude = value < 0 ? static_cast<unsigned long long>(-value)
                                                 : static_cast<unsigned long long>(value);
        if (magnitude == 0) {
            digits[position++] = '0';
        }
        while (magnitude > 0 && position < 23) {
            digits[position++] = static_cast<char>('0' + (magnitude % 10));
            magnitude /= 10;
        }
        while (position > 0 && length + 1 < sizeof(path)) {
            path[length++] = digits[--position];
        }
    };
    appendNumber(static_cast<long long>(::getpid()));
    path[length++] = '-';
    const time_t now = ::time(nullptr);
    appendNumber(static_cast<long long>(now));
    const char *tail = ".log";
    while (*tail != '\0' && length + 1 < sizeof(path)) {
        path[length++] = *tail++;
    }
    path[length] = '\0';

    const int fd = ::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fd >= 0) {
        writeReport(signal, info, fd);
        ::close(fd);
    }
    // Let the process die with the real signal (and dump core if configured).
    // Returning from the handler delivers the re-raised signal under the default
    // disposition, so the process is reported as killed by the signal (and cores
    // when the system allows it) instead of exiting with 128+signal.
    ::signal(signal, SIG_DFL);
    if (::kill(::getpid(), signal) != 0) {
        ::_exit(128 + signal);
    }
}

void install(const QString &buildInfo)
{
    if (g_installed != 0) {
        return;
    }
    const QString directory = stateRootFromEnvironment();
    QDir().mkpath(directory);
    copyBounded(g_reportDirectory, directory.toLocal8Bit().constData(), sizeof(g_reportDirectory));
    copyBounded(g_buildInfo, buildInfo.toLocal8Bit().constData(), sizeof(g_buildInfo));

    const QByteArray appImage = qgetenv("APPIMAGE");
    if (!appImage.isEmpty()) {
        copyBounded(g_appImage, appImage.constData(), sizeof(g_appImage));
        const QByteArray exportedSha = qgetenv("VEDIT_APPIMAGE_SHA256");
        if (!exportedSha.isEmpty()) {
            copyBounded(g_appImageSha, exportedSha.constData(), sizeof(g_appImageSha));
        } else {
            const QString sha = appImageSha256(QString::fromLocal8Bit(appImage));
            copyBounded(g_appImageSha, sha.toLatin1().constData(), sizeof(g_appImageSha));
        }
    }

    // Core dumps: the VM produced none, which is exactly why this module exists.
    struct rlimit coreLimit {};
    coreLimit.rlim_cur = RLIM_INFINITY;
    coreLimit.rlim_max = RLIM_INFINITY;
    ::setrlimit(RLIMIT_CORE, &coreLimit);

    // Alternate stack so a stack-overflow SIGSEGV can still be reported.
    stack_t alternate {};
    alternate.ss_sp = g_alternateStack;
    alternate.ss_size = sizeof(g_alternateStack);
    alternate.ss_flags = 0;
    ::sigaltstack(&alternate, nullptr);

    struct sigaction action {};
    action.sa_sigaction = vedit_crash_handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    ::sigemptyset(&action.sa_mask);
    for (const int signal : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) {
        ::sigaction(signal, &action, nullptr);
    }

    // Warm the unwinder up so the handler does not allocate on first use.
    void *warmup[8];
    (void)::backtrace(warmup, 8);
    g_installed = 1;
}

void event(const char *format, ...)
{
    if (format == nullptr) {
        return;
    }
    const unsigned sequence = static_cast<unsigned>(g_eventSequence) + 1;
    EventSlot &slot = g_events[sequence % kEventSlots];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(slot.text, sizeof(slot.text), format, arguments);
    va_end(arguments);
    slot.sequence = sequence;
    g_eventSequence = static_cast<sig_atomic_t>(sequence);
}

void setCurrentFile(const QString &path)
{
    if (path.isEmpty()) {
        g_currentFile[0] = '\0';
        return;
    }
    const QFileInfo info(path);
    const QString rendered = QStringLiteral("%1 (%2 bytes)")
                                 .arg(info.absoluteFilePath())
                                 .arg(info.exists() ? info.size() : -1);
    copyBounded(g_currentFile, rendered.toLocal8Bit().constData(), sizeof(g_currentFile));
}

void clearCurrentFile()
{
    g_currentFile[0] = '\0';
}

QString reportDirectory()
{
    return QString::fromLocal8Bit(g_reportDirectory);
}

} // namespace vedit::app::crash
