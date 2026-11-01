#include "vcs_source.h"

#if defined(VEDIT_VCS_HAVE_FFI)
#include "vd_ffi.h"

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

std::string take(char *value)
{
    if (value == nullptr) {
        return {};
    }
    std::string text(value);
    std::free(value);
    return text;
}

} // namespace

Info FfiVcsSource::info(const std::string &root)
{
    Info result;
    VdVcsInfo raw = vd_vcs_info(root.c_str());
    result.backend = fromI32(raw.backend);
    result.branch = take(raw.branch);
    result.revision = take(raw.revision);
    result.note = take(raw.note);
    vd_vcs_info_free(&raw);
    return result;
}

Status FfiVcsSource::status(const std::string &root)
{
    Status result;
    // The status query is the job-based service call: create a service, start the
    // read-only job, poll to completion, then release the C values. The job path
    // uses exactly the same read-only runner as info()/diff().
    VdService *service = vd_service_create();
    if (service == nullptr) {
        result.note = "vd_service_create failed";
        return result;
    }
    const unsigned long long job = vd_vcs_status(service, root.c_str());
    VdVcsStatus raw;
    std::memset(&raw, 0, sizeof(raw));
    int state = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 200 && state == VD_STATUS_PENDING; ++attempt) {
        state = vd_vcs_poll(service, job, &raw);
        if (state == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (state == VD_STATUS_READY) {
        result.backend = fromI32(raw.backend);
        result.branch = take(raw.branch);
        for (std::size_t index = 0; index < raw.count; ++index) {
            Change change;
            change.path = raw.changes[index].path != nullptr ? std::string(raw.changes[index].path) : std::string();
            change.kind = raw.changes[index].kind;
            result.changes.push_back(std::move(change));
        }
        result.note = take(raw.note);
        vd_vcs_status_free(&raw);
    } else {
        result.note = "vcs status job did not complete (state "
            + std::to_string(state) + ")";
    }
    vd_service_destroy(service);
    return result;
}

Diff FfiVcsSource::diff(const std::string &root, const std::string &path)
{
    Diff result;
    VdVcsDiff raw = vd_vcs_diff(root.c_str(), path.c_str());
    result.path = take(raw.path);
    result.text = take(raw.text);
    result.binary = raw.binary != 0;
    result.note = take(raw.note);
    vd_vcs_diff_free(&raw);
    return result;
}

#else

Info FfiVcsSource::info(const std::string &)
{
    return Info{Backend::None, {}, {}, "this build has no vd-ffi VCS ABI"};
}

Status FfiVcsSource::status(const std::string &)
{
    Status result;
    result.note = "this build has no vd-ffi VCS ABI";
    return result;
}

Diff FfiVcsSource::diff(const std::string &, const std::string &path)
{
    return Diff{path, {}, false, "this build has no vd-ffi VCS ABI"};
}

#endif

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
