// Probe C (T14): tmp -> write -> fsync(file) -> rename -> fsync(dir) semantics on
// the filesystem in use. Fails loudly if fsync(dir) returns EINVAL (some FUSE /
// network filesystems do), which decides whether A1e's fault matrix is real.
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
void report(const char *step, int rc, int err) {
    std::printf("FSYNC step=%s rc=%d errno=%d (%s)\n", step, rc, err, err ? std::strerror(err) : "ok");
}
}

int main(int argc, char **argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::string target = dir + "/fsync-probe-target.txt";
    const std::string tmp = dir + "/fsync-probe-target.txt.tmp";

    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    report("open_tmp", fd >= 0 ? 0 : -1, fd >= 0 ? 0 : errno);
    if (fd < 0) {
        return 1;
    }
    const char payload[] = "probe-payload\n";
    const ssize_t wr = ::write(fd, payload, sizeof(payload) - 1);
    report("write", wr == static_cast<ssize_t>(sizeof(payload) - 1) ? 0 : -1,
           wr == static_cast<ssize_t>(sizeof(payload) - 1) ? 0 : errno);
    const int fsFile = ::fsync(fd);
    report("fsync_file", fsFile, fsFile == 0 ? 0 : errno);
    ::close(fd);

    const int ren = ::rename(tmp.c_str(), target.c_str());
    report("rename", ren, ren == 0 ? 0 : errno);

    const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    report("open_dir", dfd >= 0 ? 0 : -1, dfd >= 0 ? 0 : errno);
    int fsDir = -1;
    if (dfd >= 0) {
        fsDir = ::fsync(dfd);
        report("fsync_dir", fsDir, fsDir == 0 ? 0 : errno);
        ::close(dfd);
    }
    struct stat st {};
    const int st_rc = ::stat(target.c_str(), &st);
    std::printf("FSYNC stat rc=%d size=%lld\n", st_rc, st_rc == 0 ? static_cast<long long>(st.st_size) : -1LL);
    ::unlink(target.c_str());
    const bool ok = fsFile == 0 && ren == 0 && fsDir == 0;
    std::printf("FSYNC verdict=%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
