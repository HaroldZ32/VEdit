// Test-only stall shim for the GlobalSearch destructor lifetime test (T64).
//
// It is NOT part of the product build and is never linked into vedit-app: the
// search self-check re-executes itself with this library in LD_PRELOAD when it
// needs the destructor to race a genuinely blocking read (see
// search_selftest.cpp, runSlowLifecycleProbe()).
//
// Simulated storage: read() on a file whose path ends with ".slowio" takes
// 11.5 s (longer than the old 10 s destructor bound, shorter than the 15 s scan
// budget - the exact window that freed GlobalSearch under its worker), then
// writes "<path>.released" so the child can tell that the blocked read ended.
// Every other read is passed straight through.
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

constexpr const char *kSlowSuffix = ".slowio";
constexpr long kStallMs = 11500;

using ReadFn = ssize_t (*)(int, void *, size_t);

ReadFn realRead()
{
    static ReadFn fn = reinterpret_cast<ReadFn>(dlsym(RTLD_NEXT, "read"));
    return fn;
}

void sleepMs(long ms)
{
    timespec delay{};
    delay.tv_sec = ms / 1000;
    delay.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

bool endsWithSlowSuffix(const char *path)
{
    const size_t length = std::strlen(path);
    const size_t suffix = std::strlen(kSlowSuffix);
    return length >= suffix && std::strcmp(path + length - suffix, kSlowSuffix) == 0;
}

void stallOnce(int fd)
{
    static bool stalled = false;
    if (stalled || fd <= 2) {
        return;
    }
    char link[64];
    std::snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    char path[4096];
    const ssize_t length = ::readlink(link, path, sizeof path - 1);
    if (length <= 0) {
        return;
    }
    path[length] = '\0';
    if (!endsWithSlowSuffix(path)) {
        return;
    }
    stalled = true;
    sleepMs(kStallMs);
    char marker[4200];
    std::snprintf(marker, sizeof marker, "%s.released", path);
    const int file = ::open(marker, O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    if (file >= 0) {
        ::close(file);
    }
}

} // namespace

extern "C" ssize_t read(int fd, void *buffer, size_t count)
{
    const ReadFn real = realRead();
    if (real == nullptr) {
        errno = ENOSYS;
        return -1;
    }
    stallOnce(fd);
    return real(fd, buffer, count);
}
