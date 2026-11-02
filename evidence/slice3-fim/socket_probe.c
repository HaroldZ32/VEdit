// LD_PRELOAD syscall counter for the "default-off means zero network" proof.
//
// It increments a counter for socket()/connect()/getaddrinfo() and writes the
// totals at process exit. Counting the syscalls is stronger than "the code does
// not look like it sends anything": it also catches a library or helper that
// would open a socket behind our back.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static unsigned long g_sockets;
static unsigned long g_connects;
static unsigned long g_getaddrinfo;

static void append_line(const char *fmt, int first, int second)
{
    const char *path = getenv("VEDIT_SOCKET_PROBE_OUT");
    FILE *out = path != NULL ? fopen(path, "a") : NULL;
    if (out == NULL) {
        out = stderr;
    }
    fprintf(out, fmt, first, second);
    fprintf(out, "\n");
    if (out != stderr) {
        fclose(out);
    }
}

static void write_totals(void)
{
    const char *path = getenv("VEDIT_SOCKET_PROBE_OUT");
    FILE *out = path != NULL ? fopen(path, "a") : NULL;
    if (out == NULL) {
        out = stderr;
    }
    fprintf(out, "SOCKETS=%lu\nCONNECTS=%lu\nGETADDRINFO=%lu\n", g_sockets, g_connects,
            g_getaddrinfo);
    if (out != stderr) {
        fclose(out);
    }
}

__attribute__((constructor)) static void init_probe(void)
{
    atexit(write_totals);
}

int socket(int domain, int type, int protocol)
{
    ++g_sockets;
    append_line("SOCKET domain=%d type=%d", domain, type);
    int (*real)(int, int, int) = (int (*)(int, int, int))dlsym(RTLD_NEXT, "socket");
    if (real == NULL) {
        errno = ENOSYS;
        return -1;
    }
    return real(domain, type, protocol);
}

int connect(int fd, const struct sockaddr *address, socklen_t length)
{
    ++g_connects;
    append_line("CONNECT fd_placeholder=%d len=%d", fd, (int)length);
    int (*real)(int, const struct sockaddr *, socklen_t) =
        (int (*)(int, const struct sockaddr *, socklen_t))dlsym(RTLD_NEXT, "connect");
    if (real == NULL) {
        errno = ENOSYS;
        return -1;
    }
    return real(fd, address, length);
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **result)
{
    ++g_getaddrinfo;
    int (*real)(const char *, const char *, const struct addrinfo *, struct addrinfo **) =
        (int (*)(const char *, const char *, const struct addrinfo *, struct addrinfo **))
            dlsym(RTLD_NEXT, "getaddrinfo");
    if (real == NULL) {
        return EAI_FAIL;
    }
    return real(node, service, hints, result);
}
