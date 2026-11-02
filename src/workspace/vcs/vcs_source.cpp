#include "vcs_source.h"

#if defined(VEDIT_VCS_HAVE_FFI)
#include "vd_ffi.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <cstring>
#include <thread>

#include <utility>
#endif

namespace vedit::workspace::vcs {

const char *backendName(Backend backend)
{
    switch (backend) {
    case Backend::Git:
        return "Git";
    case Backend::Subversion:
        return "Subversion";
    case Backend::None:
        return "No repository";
    }
    return "Unknown";
}

const char *changeKindName(int kind)
{
    switch (kind) {
    case 1:
        return "added";
    case 2:
        return "modified";
    case 3:
        return "deleted";
    case 4:
        return "renamed";
    case 5:
        return "conflict";
    default:
        return "unknown";
    }
}

// --- asynchronous request seam (T99 / C2) ---------------------------------
// The default hands the synchronous result back immediately, so stubs and the
// existing panel tests keep their synchronous semantics. FfiVcsSource overrides
// it with a job + QTimer so the GUI thread never waits for git/svn.

void VcsSource::requestInfo(const std::string &root, InfoCallback done)
{
    if (done) {
        done(info(root));
    }
}

void VcsSource::requestStatus(const std::string &root, StatusCallback done)
{
    if (done) {
        done(status(root));
    }
}

void VcsSource::requestDiff(const std::string &root, const std::string &path, DiffCallback done)
{
    if (done) {
        done(diff(root, path));
    }
}

#if defined(VEDIT_VCS_HAVE_FFI)

namespace {

Backend fromI32(int value)
{
    switch (value) {
    case 1:
        return Backend::Git;
    case 2:
        return Backend::Subversion;
    default:
        return Backend::None;
    }
}

// Copy a borrowed C string. The matching vd_vcs_*_free() releases the Rust-owned
// memory exactly once: T23's take() freed here AND the free function freed the
// same pointer again (a double free with a Rust/std allocator mismatch that the
// stub-only panel tests never reached).
std::string copyText(const char *value)
{
    return value != nullptr ? std::string(value) : std::string();
}

Info takeInfo(const VdVcsInfo &raw)
{
    Info result;
    result.backend = fromI32(raw.backend);
    result.branch = copyText(raw.branch);
    result.revision = copyText(raw.revision);
    result.note = copyText(raw.note);
    return result;
}

Status takeStatus(const VdVcsStatus &raw)
{
    Status result;
    result.backend = fromI32(raw.backend);
    result.branch = copyText(raw.branch);
    result.note = copyText(raw.note);
    for (std::size_t index = 0; index < raw.count; ++index) {
        Change change;
        change.path = copyText(raw.changes[index].path);
        change.kind = raw.changes[index].kind;
        result.changes.push_back(std::move(change));
    }
    return result;
}

Diff takeDiff(const VdVcsDiff &raw)
{
    Diff result;
    result.path = copyText(raw.path);
    result.text = copyText(raw.text);
    result.binary = raw.binary != 0;
    result.note = copyText(raw.note);
    return result;
}

std::string serviceErrorText(VdService *service)
{
    if (service == nullptr) {
        return {};
    }
    VdError error{};
    if (vd_service_error(service, &error) == 1) {
        return std::string(error.message);
    }
    return {};
}

} // namespace

FfiVcsSource::FfiVcsSource()
    : service_(vd_service_create())
{
}

FfiVcsSource::~FfiVcsSource()
{
    // Drop the callbacks first: a job that is still running finishes into the
    // service state, which dies with the handle below.
    info_job_.reset();
    status_job_.reset();
    diff_job_.reset();
    delete timer_;
    timer_ = nullptr;
    vd_service_destroy(service_);
    service_ = nullptr;
}

void FfiVcsSource::ensureTimer()
{
    if (timer_ == nullptr) {
        timer_ = new QTimer();
        timer_->setInterval(10);
        QObject::connect(timer_, &QTimer::timeout, timer_, [this]() { pollJobs(); });
    }
    if (!timer_->isActive()) {
        timer_->start();
    }
}

void FfiVcsSource::requestInfo(const std::string &root, InfoCallback done)
{
    if (service_ == nullptr) {
        if (done) {
            done(Info{Backend::None, {}, {}, "this build has no vd-ffi VCS ABI"});
        }
        return;
    }
    const unsigned long long job = vd_vcs_info_job(service_, root.c_str());
    if (job == 0) {
        Info result;
        result.note = serviceErrorText(service_);
        if (result.note.empty()) {
            result.note = "the VCS info job was rejected";
        }
        if (done) {
            done(std::move(result));
        }
        return;
    }
    info_job_ = std::make_unique<PendingInfo>();
    info_job_->job = job;
    info_job_->done = std::move(done);
    ensureTimer();
}

void FfiVcsSource::requestStatus(const std::string &root, StatusCallback done)
{
    if (service_ == nullptr) {
        Status result;
        result.note = "this build has no vd-ffi VCS ABI";
        if (done) {
            done(std::move(result));
        }
        return;
    }
    const unsigned long long job = vd_vcs_status(service_, root.c_str());
    if (job == 0) {
        Status result;
        result.note = serviceErrorText(service_);
        if (result.note.empty()) {
            result.note = "the VCS status job was rejected";
        }
        if (done) {
            done(std::move(result));
        }
        return;
    }
    status_job_ = std::make_unique<PendingStatus>();
    status_job_->job = job;
    status_job_->done = std::move(done);
    ensureTimer();
}

void FfiVcsSource::requestDiff(const std::string &root, const std::string &path, DiffCallback done)
{
    if (service_ == nullptr) {
        if (done) {
            done(Diff{path, {}, false, "this build has no vd-ffi VCS ABI"});
        }
        return;
    }
    const unsigned long long job = vd_vcs_diff_job(service_, root.c_str(), path.c_str());
    if (job == 0) {
        Diff result;
        result.path = path;
        result.note = serviceErrorText(service_);
        if (result.note.empty()) {
            result.note = "the VCS diff job was rejected";
        }
        if (done) {
            done(std::move(result));
        }
        return;
    }
    diff_job_ = std::make_unique<PendingDiff>();
    diff_job_->job = job;
    diff_job_->path = path;
    diff_job_->done = std::move(done);
    ensureTimer();
}

void FfiVcsSource::pollJobs()
{
    ++poll_rounds_;
    if (service_ == nullptr) {
        return;
    }

    if (info_job_) {
        VdVcsInfo raw{};
        std::memset(&raw, 0, sizeof(raw));
        const int state = vd_vcs_info_poll(service_, info_job_->job, &raw);
        if (state == VD_STATUS_READY) {
            Info result = takeInfo(raw);
            vd_vcs_info_free(&raw);
            InfoCallback done = std::move(info_job_->done);
            info_job_.reset();
            if (done) {
                done(std::move(result));
            }
        } else if (state != VD_STATUS_PENDING) {
            Info result;
            result.note = serviceErrorText(service_);
            if (result.note.empty()) {
                result.note = "the VCS info job failed (state " + std::to_string(state) + ")";
            }
            vd_vcs_info_free(&raw);
            InfoCallback done = std::move(info_job_->done);
            info_job_.reset();
            if (done) {
                done(std::move(result));
            }
        }
    }

    if (status_job_) {
        VdVcsStatus raw{};
        std::memset(&raw, 0, sizeof(raw));
        const int state = vd_vcs_poll(service_, status_job_->job, &raw);
        if (state == VD_STATUS_READY) {
            Status result = takeStatus(raw);
            vd_vcs_status_free(&raw);
            StatusCallback done = std::move(status_job_->done);
            status_job_.reset();
            if (done) {
                done(std::move(result));
            }
        } else if (state != VD_STATUS_PENDING) {
            Status result;
            result.note = serviceErrorText(service_);
            if (result.note.empty()) {
                result.note = "the VCS status job failed (state " + std::to_string(state) + ")";
            }
            vd_vcs_status_free(&raw);
            StatusCallback done = std::move(status_job_->done);
            status_job_.reset();
            if (done) {
                done(std::move(result));
            }
        }
    }

    if (diff_job_) {
        VdVcsDiff raw{};
        std::memset(&raw, 0, sizeof(raw));
        const int state = vd_vcs_diff_poll(service_, diff_job_->job, &raw);
        if (state == VD_STATUS_READY) {
            Diff result = takeDiff(raw);
            vd_vcs_diff_free(&raw);
            DiffCallback done = std::move(diff_job_->done);
            const std::string path = diff_job_->path;
            diff_job_.reset();
            if (done) {
                done(std::move(result));
            }
            (void)path;
        } else if (state != VD_STATUS_PENDING) {
            Diff result;
            result.path = diff_job_->path;
            result.note = serviceErrorText(service_);
            if (result.note.empty()) {
                result.note = "the VCS diff job failed (state " + std::to_string(state) + ")";
            }
            vd_vcs_diff_free(&raw);
            DiffCallback done = std::move(diff_job_->done);
            diff_job_.reset();
            if (done) {
                done(std::move(result));
            }
        }
    }

    if (!info_job_ && !status_job_ && !diff_job_ && timer_ != nullptr) {
        timer_->stop();
    }
}

Info FfiVcsSource::info(const std::string &root)
{
    // Synchronous seam (tests/legacy): start the same job and wait for it. The
    // panel uses requestInfo() instead.
    Info result;
    VdService *service = vd_service_create();
    if (service == nullptr) {
        result.note = "vd_service_create failed";
        return result;
    }
    const unsigned long long job = vd_vcs_info_job(service, root.c_str());
    VdVcsInfo raw{};
    std::memset(&raw, 0, sizeof(raw));
    int state = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 2000 && state == VD_STATUS_PENDING; ++attempt) {
        state = vd_vcs_info_poll(service, job, &raw);
        if (state == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (state == VD_STATUS_READY) {
        result = takeInfo(raw);
        vd_vcs_info_free(&raw);
    } else if (job == 0) {
        result.note = "the VCS info job was rejected";
    } else {
        result.note = "vcs info job did not complete (state " + std::to_string(state) + ")";
    }
    vd_service_destroy(service);
    return result;
}

Status FfiVcsSource::status(const std::string &root)
{
    Status result;
    VdService *service = vd_service_create();
    if (service == nullptr) {
        result.note = "vd_service_create failed";
        return result;
    }
    const unsigned long long job = vd_vcs_status(service, root.c_str());
    VdVcsStatus raw{};
    std::memset(&raw, 0, sizeof(raw));
    int state = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 2000 && state == VD_STATUS_PENDING; ++attempt) {
        state = vd_vcs_poll(service, job, &raw);
        if (state == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (state == VD_STATUS_READY) {
        result = takeStatus(raw);
        vd_vcs_status_free(&raw);
    } else if (job == 0) {
        result.note = "the VCS status job was rejected";
    } else {
        result.note = "vcs status job did not complete (state " + std::to_string(state) + ")";
    }
    vd_service_destroy(service);
    return result;
}

Diff FfiVcsSource::diff(const std::string &root, const std::string &path)
{
    Diff result;
    result.path = path;
    VdService *service = vd_service_create();
    if (service == nullptr) {
        result.note = "vd_service_create failed";
        return result;
    }
    const unsigned long long job = vd_vcs_diff_job(service, root.c_str(), path.c_str());
    VdVcsDiff raw{};
    std::memset(&raw, 0, sizeof(raw));
    int state = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 2000 && state == VD_STATUS_PENDING; ++attempt) {
        state = vd_vcs_diff_poll(service, job, &raw);
        if (state == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (state == VD_STATUS_READY) {
        result = takeDiff(raw);
        vd_vcs_diff_free(&raw);
    } else if (job == 0) {
        result.note = "the VCS diff job was rejected";
    } else {
        result.note = "vcs diff job did not complete (state " + std::to_string(state) + ")";
    }
    vd_service_destroy(service);
    return result;
}

#endif // VEDIT_VCS_HAVE_FFI

StubVcsSource::StubVcsSource(Info info, Status status, std::vector<Diff> diffs)
    : info_(std::move(info))
    , status_(std::move(status))
    , diffs_(std::move(diffs))
{
}

Info StubVcsSource::info(const std::string &)
{
    ++calls_;
    return info_;
}

Status StubVcsSource::status(const std::string &)
{
    ++calls_;
    return status_;
}

Diff StubVcsSource::diff(const std::string &, const std::string &path)
{
    ++calls_;
    for (const Diff &diff : diffs_) {
        if (diff.path == path) {
            return diff;
        }
    }
    return Diff{path, {}, false, "stub has no diff for this path"};
}

} // namespace vedit::workspace::vcs
