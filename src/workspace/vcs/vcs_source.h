#pragma once

// Panel-facing VCS seam (T23). The panel never talks to git/svn directly: it
// renders whatever a VcsSource reports, so the same widget is driven by the
// production backend (C ABI -> vd-vcs) or by a stub in tests.
//
// The seam is deliberately read-only: there is no method through which the panel
// could request a commit/update/revert/switch. The absence of write methods is
// the panel-side half of the "no write operation is ever called" guarantee; the
// backend-side half is the allow-list in crates/vd-vcs/src/runner.rs.
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// The C ABI types used by FfiVcsSource; the concrete declarations come from the
// generated vd_ffi.h in the implementation file, so the seam header stays free
// of the C ABI (a non-FFI build compiles it unchanged).
class QTimer;
struct VdService;
struct VdVcsInfo;
struct VdVcsStatus;
struct VdVcsDiff;

namespace vedit::workspace::vcs {

enum class Backend { None, Git, Subversion };

const char *backendName(Backend backend);

struct Change {
    std::string path;
    int kind = 0; // 1 added, 2 modified, 3 deleted, 4 renamed, 5 conflict, 6 unknown
};

const char *changeKindName(int kind);

struct Info {
    Backend backend = Backend::None;
    std::string branch;
    std::string revision;
    std::string note;
};

struct Status {
    Backend backend = Backend::None;
    std::string branch;
    std::vector<Change> changes;
    std::string note;
};

struct Diff {
    std::string path;
    std::string text;
    bool binary = false;
    std::string note;
};

class VcsSource {
public:
    using InfoCallback = std::function<void(Info)>;
    using StatusCallback = std::function<void(Status)>;
    using DiffCallback = std::function<void(Diff)>;

    virtual ~VcsSource() = default;
    virtual Info info(const std::string &root) = 0;
    virtual Status status(const std::string &root) = 0;
    virtual Diff diff(const std::string &root, const std::string &path) = 0;

    // Asynchronous request seam used by the panel (T99, C2): the call returns
    // immediately and `done` is invoked with the result later. The default
    // implementation calls the synchronous method and hands the result back at
    // once, so stubs and the existing panel tests keep their synchronous
    // semantics. FfiVcsSource overrides it with a C ABI job + QTimer polling so
    // no git/svn subprocess ever runs on the GUI thread.
    virtual void requestInfo(const std::string &root, InfoCallback done);
    virtual void requestStatus(const std::string &root, StatusCallback done);
    virtual void requestDiff(const std::string &root, const std::string &path, DiffCallback done);
};

// Production source: the C ABI exposed by vd-vcs (read-only endpoints only).
// Built only when the FFI header + static library are present.
//
// T99: the three reads are job-based (vd_vcs_status / vd_vcs_info_job /
// vd_vcs_diff_job + poll). The asynchronous path polls on a QTimer owned by this
// object, so the panel never waits for git/svn on the GUI thread. The
// synchronous overrides remain for the seam and are implemented as a bounded
// wait for the same job.
class FfiVcsSource final : public VcsSource {
public:
    FfiVcsSource();
    ~FfiVcsSource() override;

    FfiVcsSource(const FfiVcsSource &) = delete;
    FfiVcsSource &operator=(const FfiVcsSource &) = delete;

    Info info(const std::string &root) override;
    Status status(const std::string &root) override;
    Diff diff(const std::string &root, const std::string &path) override;

    void requestInfo(const std::string &root, InfoCallback done) override;
    void requestStatus(const std::string &root, StatusCallback done) override;
    void requestDiff(const std::string &root, const std::string &path, DiffCallback done) override;

    // Diagnostics for the non-blocking regression test: how many poll rounds the
    // timer has performed (0 when the build has no FFI).
    std::size_t pollRounds() const { return poll_rounds_; }

private:
    struct PendingInfo {
        unsigned long long job = 0;
        InfoCallback done;
    };
    struct PendingStatus {
        unsigned long long job = 0;
        StatusCallback done;
    };
    struct PendingDiff {
        unsigned long long job = 0;
        std::string path;
        DiffCallback done;
    };

    void ensureTimer();
    void pollJobs();
    void finishInfo(int state, VdVcsInfo *raw);
    void finishStatus(int state, VdVcsStatus *raw);
    void finishDiff(int state, VdVcsDiff *raw);

    VdService *service_ = nullptr;
    QTimer *timer_ = nullptr;
    std::unique_ptr<PendingInfo> info_job_;
    std::unique_ptr<PendingStatus> status_job_;
    std::unique_ptr<PendingDiff> diff_job_;
    std::size_t poll_rounds_ = 0;
};

// Test/dev stub: canned data, records every call, counts write attempts (there
// is no write method, so any attempt would have to be a future API change).
class StubVcsSource final : public VcsSource {
public:
    StubVcsSource(Info info, Status status, std::vector<Diff> diffs);

    Info info(const std::string &root) override;
    Status status(const std::string &root) override;
    Diff diff(const std::string &root, const std::string &path) override;

    std::size_t calls() const { return calls_; }
    std::size_t writeAttempts() const { return write_attempts_; }

private:
    Info info_;
    Status status_;
    std::vector<Diff> diffs_;
    std::size_t calls_ = 0;
    std::size_t write_attempts_ = 0;
};

} // namespace vedit::workspace::vcs
