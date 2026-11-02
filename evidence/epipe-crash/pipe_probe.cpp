// T134 reverse-control probe: writing to a closed pipe.
//   default disposition        -> SIGPIPE kills the process (exit 141)
//   signal(SIGPIPE, SIG_IGN)   -> write returns -1/EPIPE, the process survives
// vedit-app needs the second behaviour because Rust is statically linked into
// the C++ host and its runtime init (which would ignore SIGPIPE) never runs.
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <unistd.h>

int main(int argc, char **argv)
{
    const bool ignore = argc > 1 && std::strcmp(argv[1], "--ignore-sigpipe") == 0;
    if (ignore) {
        std::signal(SIGPIPE, SIG_IGN);
    }
    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0) {
        std::printf("pipe failed errno=%d\n", errno);
        return 2;
    }
    ::close(fds[0]); // the reader is gone: the next write raises SIGPIPE
    const char byte = 'x';
    const ssize_t written = ::write(fds[1], &byte, 1);
    std::printf("ignore=%d write_result=%zd errno=%d (%s)\n", ignore ? 1 : 0, written, errno,
                std::strerror(errno));
    std::fflush(stdout);
    return 0;
}
