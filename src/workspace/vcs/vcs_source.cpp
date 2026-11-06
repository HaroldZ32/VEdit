#include "vcs_source.h"

#include "vcs_toolchain.h"

#include <cctype>
#include <cstdio>

// Batch 7: backendName()/changeKindName() translate, so QCoreApplication is
// needed in EVERY build (a stub-only lane compiles this file too).
#include <QCoreApplication>

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

QString backendName(Backend backend)
{
    switch (backend) {
    case Backend::Git:
        return QCoreApplication::translate("VEdit", "Git");
    case Backend::Subversion:
        return QCoreApplication::translate("VEdit", "Subversion");
    case Backend::None:
        return QCoreApplication::translate("VEdit", "No repository");
    }
    return QCoreApplication::translate("VEdit", "Unknown");
}

QString changeKindName(int kind)
{
    switch (kind) {
    case 1:
        return QCoreApplication::translate("VEdit", "added");
    case 2:
        return QCoreApplication::translate("VEdit", "modified");
    case 3:
        return QCoreApplication::translate("VEdit", "deleted");
    case 4:
        return QCoreApplication::translate("VEdit", "renamed");
    case 5:
        return QCoreApplication::translate("VEdit", "conflict");
    default:
        return QCoreApplication::translate("VEdit", "unknown");
    }
}

const char *refKindName(RefKind kind)
{
    switch (kind) {
    case RefKind::Branch:
        return "branch";
    case RefKind::Tag:
        return "tag";
    case RefKind::RemoteBranch:
        return "remote-branch";
    case RefKind::Other:
        break;
    }
    return "other";
}

const char *writeOpName(WriteOp op)
{
    switch (op) {
    case WriteOp::GitStage:
        return "stage";
    case WriteOp::GitUnstage:
        return "unstage";
    case WriteOp::GitDiscard:
        return "discard";
    case WriteOp::GitCommit:
        return "commit";
    case WriteOp::GitFetch:
        return "fetch";
    case WriteOp::GitPull:
        return "pull";
    case WriteOp::GitPush:
        return "push";
    case WriteOp::GitBranchCreate:
        return "branch-create";
    case WriteOp::GitBranchSwitch:
        return "branch-switch";
    case WriteOp::GitBranchDelete:
        return "branch-delete";
    case WriteOp::GitBranchRename:
        return "branch-rename";
    case WriteOp::GitTagCreate:
        return "tag-create";
    case WriteOp::GitTagDelete:
        return "tag-delete";
    case WriteOp::GitCheckout:
        return "checkout";
    case WriteOp::GitRemoteAdd:
        return "remote-add";
    case WriteOp::GitRemoteRemove:
        return "remote-remove";
    case WriteOp::GitRemoteSetUrl:
        return "remote-set-url";
    case WriteOp::GitSetUpstream:
        return "set-upstream";
    case WriteOp::GitMerge:
        return "merge";
    case WriteOp::GitRebase:
        return "rebase";
    case WriteOp::GitCherryPick:
        return "cherry-pick";
    case WriteOp::GitRevert:
        return "revert";
    case WriteOp::GitReset:
        return "reset";
    case WriteOp::GitClean:
        return "clean";
    case WriteOp::GitContinue:
        return "continue";
    case WriteOp::GitAbort:
        return "abort";
    case WriteOp::SvnCommit:
        return "svn-commit";
    case WriteOp::SvnUpdate:
        return "svn-update";
    case WriteOp::SvnRevert:
        return "svn-revert";
    case WriteOp::SvnAdd:
        return "svn-add";
    case WriteOp::SvnRemove:
        return "svn-remove";
    case WriteOp::GitStashSave:
        return "stash-save";
    case WriteOp::GitStashPop:
        return "stash-pop";
    case WriteOp::GitStashApply:
        return "stash-apply";
    case WriteOp::GitStashDrop:
        return "stash-drop";
    case WriteOp::GitWorktreeAdd:
        return "worktree-add";
    case WriteOp::GitWorktreeRemove:
        return "worktree-remove";
    case WriteOp::GitSubmoduleInit:
        return "submodule-init";
    case WriteOp::GitSubmoduleUpdate:
        return "submodule-update";
    case WriteOp::None:
        break;
    }
    return "none";
}

const char *historyKindName(HistoryKind kind)
{
    switch (kind) {
    case HistoryKind::Commits:
        return "commits";
    case HistoryKind::Commit:
        return "commit";
    case HistoryKind::FileHistory:
        return "file-history";
    case HistoryKind::Blame:
        return "blame";
    case HistoryKind::Compare:
        return "compare";
    }
    return "commits";
}

const char *workspaceKindName(WorkspaceKind kind)
{
    switch (kind) {
    case WorkspaceKind::Stashes:
        return "stashes";
    case WorkspaceKind::Worktrees:
        return "worktrees";
    }
    return "stashes";
}

const char *gitOperationName(GitOperation operation)
{
    switch (operation) {
    case GitOperation::Merge:
        return "merge";
    case GitOperation::Rebase:
        return "rebase";
    case GitOperation::CherryPick:
        return "cherry-pick";
    case GitOperation::Revert:
        return "revert";
    case GitOperation::None:
        break;
    }
    return "none";
}

std::string gitOperationToken(GitOperation operation)
{
    return operation == GitOperation::None ? std::string() : std::string(gitOperationName(operation));
}

bool writeOpIsGit(WriteOp op)
{
    return op == WriteOp::GitStage || op == WriteOp::GitUnstage || op == WriteOp::GitDiscard
        || op == WriteOp::GitCommit || op == WriteOp::GitFetch || op == WriteOp::GitPull
        || op == WriteOp::GitPush || op == WriteOp::GitBranchCreate
        || op == WriteOp::GitBranchSwitch || op == WriteOp::GitBranchDelete
        || op == WriteOp::GitBranchRename || op == WriteOp::GitTagCreate
        || op == WriteOp::GitTagDelete || op == WriteOp::GitCheckout
        || op == WriteOp::GitRemoteAdd || op == WriteOp::GitRemoteRemove
        || op == WriteOp::GitRemoteSetUrl || op == WriteOp::GitSetUpstream
        || op == WriteOp::GitMerge || op == WriteOp::GitRebase
        || op == WriteOp::GitCherryPick || op == WriteOp::GitRevert
        || op == WriteOp::GitReset || op == WriteOp::GitClean
        || op == WriteOp::GitContinue || op == WriteOp::GitAbort
        || op == WriteOp::GitStashSave || op == WriteOp::GitStashPop
        || op == WriteOp::GitStashApply || op == WriteOp::GitStashDrop
        || op == WriteOp::GitWorktreeAdd || op == WriteOp::GitWorktreeRemove
        || op == WriteOp::GitSubmoduleInit || op == WriteOp::GitSubmoduleUpdate;
}

bool writeOpIsSvn(WriteOp op)
{
    return op == WriteOp::SvnCommit || op == WriteOp::SvnUpdate || op == WriteOp::SvnRevert
        || op == WriteOp::SvnAdd || op == WriteOp::SvnRemove;
}

bool writeOpIsNetwork(WriteOp op)
{
    return op == WriteOp::GitFetch || op == WriteOp::GitPull || op == WriteOp::GitPush;
}

bool writeOpIsIrreversible(WriteOp op)
{
    // Discard throws the working-tree changes away, a revert does the same for
    // SVN, and deleting a branch or a tag drops the only pointer to its commit;
    // none of them can be undone from the panel, so all confirm first.
    // Batch 5b: dropping the only reference to a stash, and deleting a linked
    // worktree's checkout, cannot be undone from the panel either.
    return op == WriteOp::GitDiscard || op == WriteOp::SvnRevert
        || op == WriteOp::GitBranchDelete || op == WriteOp::GitTagDelete
        || op == WriteOp::GitReset || op == WriteOp::GitClean
        || op == WriteOp::GitStashDrop || op == WriteOp::GitWorktreeRemove;
}

bool writeResultNeedsCredentials(const WriteResult &result)
{
    if (result.ok) {
        return false;
    }
    // One lowercase copy; every marker is matched against it. The text was
    // redacted by the backend already, so this never sees a password.
    std::string text = result.stderrText + "\n" + result.note;
    for (char &character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    static const char *const markers[] = {
        "could not read username",
        "could not read password",
        "terminal prompts disabled",
        "authentication failed",
        "authentication required",
        "invalid username or password",
        "permission denied (publickey",
        "permission denied, please try again",
        "fatal: authentication",
        "remote: http basic: access denied",
    };
    for (const char *marker : markers) {
        if (text.find(marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string WriteResult::summary() const
{
    if (!note.empty()) {
        return note;
    }
    std::size_t start = 0;
    while (start < stderrText.size()) {
        const std::size_t end = stderrText.find('\n', start);
        const std::string line = stderrText.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty()) {
            return line;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return {};
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

WriteResult VcsSource::write(const WriteRequest &request)
{
    WriteResult result;
    result.op = request.op;
    result.note = "this VCS source has no write path";
    return result;
}

void VcsSource::requestWrite(const WriteRequest &request, WriteCallback done)
{
    if (done) {
        done(write(request));
    }
}

HistoryData VcsSource::history(const HistoryRequest &request)
{
    HistoryData result;
    result.kind = request.kind;
    result.offset = request.offset;
    result.note = "this VCS source has no history path";
    return result;
}

void VcsSource::requestHistory(const HistoryRequest &request, HistoryCallback done)
{
    if (done) {
        done(history(request));
    }
}

WorkspaceData VcsSource::workspace(const WorkspaceRequest &request)
{
    WorkspaceData result;
    result.kind = request.kind;
    result.note = "this VCS source has no workspace listing path";
    return result;
}

void VcsSource::requestWorkspace(const WorkspaceRequest &request, WorkspaceCallback done)
{
    if (done) {
        done(workspace(request));
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
    // Batch 4a: the ref listing rides the same job. The arrays are owned by the
    // Rust side and released by vd_vcs_info_free(), so every string is copied.
    result.refs.reserve(raw.ref_count);
    for (std::size_t index = 0; index < raw.ref_count; ++index) {
        const VdVcsRef &entry = raw.refs[index];
        RefEntry ref;
        ref.name = copyText(entry.name);
        switch (entry.kind) {
        case 1:
            ref.kind = RefKind::Branch;
            break;
        case 2:
            ref.kind = RefKind::Tag;
            break;
        case 3:
            ref.kind = RefKind::RemoteBranch;
            break;
        default:
            ref.kind = RefKind::Other;
            break;
        }
        ref.target = copyText(entry.target);
        ref.current = entry.current != 0;
        result.refs.push_back(std::move(ref));
    }
    result.remotes.reserve(raw.remote_count);
    for (std::size_t index = 0; index < raw.remote_count; ++index) {
        const VdVcsRemote &entry = raw.remotes[index];
        RemoteEntry remote;
        remote.name = copyText(entry.name);
        remote.url = copyText(entry.url);
        result.remotes.push_back(std::move(remote));
    }
    result.upstream = copyText(raw.upstream);
    switch (raw.operation) {
    case 1:
        result.operation = GitOperation::Merge;
        break;
    case 2:
        result.operation = GitOperation::Rebase;
        break;
    case 3:
        result.operation = GitOperation::CherryPick;
        break;
    case 4:
        result.operation = GitOperation::Revert;
        break;
    default:
        result.operation = GitOperation::None;
        break;
    }
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

WriteResult takeWriteResult(const VdVcsWriteResult &raw, WriteOp requested)
{
    WriteResult result;
    result.op = static_cast<WriteOp>(raw.op);
    if (result.op == WriteOp::None) {
        result.op = requested;
    }
    result.ok = raw.ok != 0;
    result.stdoutText = copyText(raw.stdout_text);
    result.stderrText = copyText(raw.stderr_text);
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

// Fill the borrowed-pointer view the C ABI takes. Every pointer refers into
// `request` / `paths`, which stay alive for the duration of the job call.
void fillWriteRequest(const WriteRequest &request, VdVcsWriteRequest &raw,
                      std::vector<const char *> &paths)
{
    paths.clear();
    paths.reserve(request.paths.size());
    for (const std::string &path : request.paths) {
        paths.push_back(path.c_str());
    }
    raw = VdVcsWriteRequest{};
    raw.op = static_cast<int32_t>(request.op);
    raw.root = request.root.c_str();
    raw.message = request.message.empty() ? nullptr : request.message.c_str();
    raw.author = request.author.empty() ? nullptr : request.author.c_str();
    raw.amend = request.amend ? 1 : 0;
    raw.count = paths.size();
    raw.paths = paths.empty() ? nullptr : paths.data();
    raw.name = request.name.empty() ? nullptr : request.name.c_str();
    raw.target = request.target.empty() ? nullptr : request.target.c_str();
    raw.url = request.url.empty() ? nullptr : request.url.c_str();
    // Credentials are only ever sent as a pair: the backend refuses a half pair,
    // so a caller that set just one field gets no credential at all.
    const bool credentials =
        !request.credential_user.empty() && !request.credential_password.empty();
    raw.credential_user = credentials ? request.credential_user.c_str() : nullptr;
    raw.credential_password = credentials ? request.credential_password.c_str() : nullptr;
    raw.set_upstream = request.set_upstream ? 1 : 0;
    raw.mode = request.mode.empty() ? nullptr : request.mode.c_str();
}


// --- history conversion (batch 4b) ----------------------------------------

HistoryData takeHistory(const VdVcsHistoryResult &raw)
{
    HistoryData data;
    const int kind = raw.kind == 0 ? 1 : raw.kind;
    data.kind = static_cast<HistoryKind>(kind);
    data.backend = fromI32(raw.backend);
    data.offset = static_cast<int>(raw.offset);
    data.hasMore = raw.has_more != 0;
    data.truncated = raw.truncated != 0;
    data.error = raw.error != 0;
    data.commits.reserve(raw.commit_count);
    for (std::size_t index = 0; index < raw.commit_count; ++index) {
        CommitEntry entry;
        entry.hash = copyText(raw.commits[index].hash);
        entry.shortHash = copyText(raw.commits[index].short_hash);
        entry.author = copyText(raw.commits[index].author);
        entry.email = copyText(raw.commits[index].email);
        entry.time = raw.commits[index].time;
        entry.subject = copyText(raw.commits[index].subject);
        data.commits.push_back(std::move(entry));
    }
    data.files.reserve(raw.file_count);
    for (std::size_t index = 0; index < raw.file_count; ++index) {
        FileStat file;
        file.path = copyText(raw.files[index].path);
        file.additions = raw.files[index].additions;
        file.deletions = raw.files[index].deletions;
        data.files.push_back(std::move(file));
    }
    data.blame.reserve(raw.blame_count);
    for (std::size_t index = 0; index < raw.blame_count; ++index) {
        BlameLine line;
        line.line = static_cast<int>(raw.blame[index].line);
        line.hash = copyText(raw.blame[index].hash);
        line.shortHash = copyText(raw.blame[index].short_hash);
        line.author = copyText(raw.blame[index].author);
        line.time = raw.blame[index].time;
        line.text = copyText(raw.blame[index].text);
        data.blame.push_back(std::move(line));
    }
    data.hash = copyText(raw.hash);
    data.author = copyText(raw.author);
    data.email = copyText(raw.email);
    data.subject = copyText(raw.subject);
    data.body = copyText(raw.body);
    data.patch = copyText(raw.patch);
    data.path = copyText(raw.path);
    data.from = copyText(raw.from);
    data.to = copyText(raw.to);
    data.time = raw.time;
    data.note = copyText(raw.note);
    return data;
}

/// Start the C ABI job for one history request; 0 when it was rejected.
unsigned long long startHistoryJob(VdService *service, const HistoryRequest &request)
{
    if (service == nullptr) {
        return 0;
    }
    const unsigned long long offset = request.offset > 0 ? static_cast<unsigned long long>(request.offset) : 0;
    int limit = request.limit > 0 ? request.limit : 50;
    if (limit > kMaxHistoryPage) {
        limit = kMaxHistoryPage;
    }
    switch (request.kind) {
    case HistoryKind::Commits:
        return vd_vcs_history_commits_job(service, request.root.c_str(), offset,
                                          static_cast<unsigned long long>(limit),
                                          request.query.empty() ? nullptr : request.query.c_str());
    case HistoryKind::Commit:
        return vd_vcs_history_commit_job(service, request.root.c_str(), request.hash.c_str());
    case HistoryKind::FileHistory:
        return vd_vcs_history_file_job(service, request.root.c_str(), request.path.c_str(), offset,
                                       static_cast<unsigned long long>(limit));
    case HistoryKind::Blame:
        return vd_vcs_history_blame_job(service, request.root.c_str(), request.path.c_str());
    case HistoryKind::Compare:
        return vd_vcs_history_compare_job(service, request.root.c_str(), request.from.c_str(),
                                          request.to.c_str());
    }
    return 0;
}

// --- workspace conversion (batch 5b) --------------------------------------

WorkspaceData takeWorkspace(const VdVcsWorkspaceResult &raw)
{
    WorkspaceData data;
    data.kind = static_cast<WorkspaceKind>(raw.kind == 2 ? 2 : 1);
    data.backend = fromI32(raw.backend);
    data.truncated = raw.truncated != 0;
    data.error = raw.error != 0;
    data.note = copyText(raw.note);
    data.entries.reserve(raw.count);
    for (std::size_t index = 0; index < raw.count; ++index) {
        WorkspaceEntry entry;
        entry.refName = copyText(raw.entries[index].ref_name);
        entry.hash = copyText(raw.entries[index].hash);
        entry.subject = copyText(raw.entries[index].subject);
        entry.path = copyText(raw.entries[index].path);
        entry.branch = copyText(raw.entries[index].branch);
        entry.mainWorktree = raw.entries[index].main_worktree != 0;
        entry.detached = raw.entries[index].detached != 0;
        entry.bare = raw.entries[index].bare != 0;
        data.entries.push_back(std::move(entry));
    }
    return data;
}

/// Start the C ABI job for one workspace listing; 0 when it was rejected.
unsigned long long startWorkspaceJob(VdService *service, const WorkspaceRequest &request)
{
    if (service == nullptr) {
        return 0;
    }
    switch (request.kind) {
    case WorkspaceKind::Stashes:
        return vd_vcs_workspace_stashes_job(service, request.root.c_str());
    case WorkspaceKind::Worktrees:
        return vd_vcs_workspace_worktrees_job(service, request.root.c_str());
    }
    return 0;
}

} // namespace

FfiVcsSource::FfiVcsSource()
    : service_(vd_service_create())
{
    // Batch 6a (rev3): the VCS lane's init prints ONE observable toolchain line
    // (`VCS-TOOLCHAIN ...`, bundled > PATH > env). It is printed once for the
    // whole process; the panel factory also copies the values into properties.
    vedit::workspace::vcs::reportToolchainOnce();
}

FfiVcsSource::~FfiVcsSource()
{
    // Drop the callbacks first: a job that is still running finishes into the
    // service state, which dies with the handle below.
    info_job_.reset();
    status_job_.reset();
    diff_job_.reset();
    write_job_.reset();
    queued_write_.reset();
    deferred_status_.reset();
    history_jobs_.clear();
    workspace_jobs_.clear();
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
            Info info;
            info.backend = Backend::None;
            info.note = "this build has no vd-ffi VCS ABI";
            done(std::move(info));
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
        result.error = true;
        if (done) {
            done(std::move(result));
        }
        return;
    }
    if (write_job_ || queued_write_) {
        // A write holds (or is about to hold) `.git/index.lock`, and `git status`
        // opportunistically rewrites the index too, so starting the read now
        // makes one of the two fail with "index.lock: File exists". The read is
        // cheap to repeat: keep the newest request and run it after the write.
        deferred_status_ = std::make_unique<DeferredStatus>();
        deferred_status_->root = root;
        deferred_status_->done = std::move(done);
        ensureTimer();
        return;
    }
    startStatus(root, std::move(done));
}

void FfiVcsSource::startStatus(const std::string &root, StatusCallback done)
{
    const unsigned long long job = vd_vcs_status(service_, root.c_str());
    if (job == 0) {
        Status result;
        result.note = serviceErrorText(service_);
        if (result.note.empty()) {
            result.note = "the VCS status job was rejected";
        }
        result.error = true;
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

WriteResult FfiVcsSource::write(const WriteRequest &request)
{
    // Synchronous seam (tests/legacy): start the same job and wait for it. The
    // panel uses requestWrite() instead, so nothing waits on the GUI thread.
    WriteResult result;
    result.op = request.op;
    VdService *service = vd_service_create();
    if (service == nullptr) {
        result.note = "vd_service_create failed";
        return result;
    }
    std::vector<const char *> paths;
    VdVcsWriteRequest raw{};
    fillWriteRequest(request, raw, paths);
    const unsigned long long job = vd_vcs_write_job(service, &raw);
    VdVcsWriteResult rawResult{};
    std::memset(&rawResult, 0, sizeof(rawResult));
    int state = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 4000 && state == VD_STATUS_PENDING; ++attempt) {
        state = vd_vcs_write_poll(service, job, &rawResult);
        if (state == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (state == VD_STATUS_READY) {
        result = takeWriteResult(rawResult, request.op);
        vd_vcs_write_free(&rawResult);
    } else if (job == 0) {
        result.note = "the VCS write job was rejected";
    } else {
        result.note = "vcs write job did not complete (state " + std::to_string(state) + ")";
    }
    vd_service_destroy(service);
    return result;
}

void FfiVcsSource::requestWrite(const WriteRequest &request, WriteCallback done)
{
    if (service_ == nullptr) {
        WriteResult result;
        result.op = request.op;
        result.note = "this build has no vd-ffi VCS ABI";
        if (done) {
            done(std::move(result));
        }
        return;
    }
    if (status_job_ || write_job_) {
        // Serialized behind the running job: a write started while `git status`
        // holds (or refreshes) the index lock would fail, and two writes at once
        // would collide on the same lock. This is normally not reachable (the
        // panel disables its buttons while a write runs) but the seam must not
        // depend on a well-behaved caller.
        if (queued_write_) {
            WriteResult superseded;
            superseded.op = queued_write_->request.op;
            superseded.note = "the VCS write was superseded by a newer request";
            WriteCallback previous = std::move(queued_write_->done);
            queued_write_.reset();
            if (previous) {
                previous(std::move(superseded));
            }
        }
        queued_write_ = std::make_unique<QueuedWrite>();
        queued_write_->request = request;
        queued_write_->done = std::move(done);
        ensureTimer();
        return;
    }
    startWrite(request, std::move(done));
}

void FfiVcsSource::startWrite(const WriteRequest &request, WriteCallback done)
{
    std::vector<const char *> paths;
    VdVcsWriteRequest raw{};
    fillWriteRequest(request, raw, paths);
    const unsigned long long job = vd_vcs_write_job(service_, &raw);
    if (job == 0) {
        WriteResult result;
        result.op = request.op;
        result.note = serviceErrorText(service_);
        if (result.note.empty()) {
            result.note = "the VCS write job was rejected";
        }
        if (done) {
            done(std::move(result));
        }
        return;
    }
    ++write_jobs_;
    write_job_ = std::make_unique<PendingWrite>();
    write_job_->job = job;
    write_job_->op = request.op;
    write_job_->done = std::move(done);
    ensureTimer();
}


HistoryData FfiVcsSource::history(const HistoryRequest &request)
{
    // Synchronous seam (tests/legacy): the same job, polled to completion. The
    // panel uses requestHistory() instead, so the GUI thread never waits.
    HistoryData result;
    result.kind = request.kind;
    VdService *service = vd_service_create();
    if (service == nullptr) {
        result.error = true;
        result.note = "vd_service_create failed";
        return result;
    }
    const unsigned long long job = startHistoryJob(service, request);
    if (job == 0) {
        result.error = true;
        result.note = serviceErrorText(service);
        if (result.note.empty()) {
            result.note = "the VCS history job was rejected";
        }
        vd_service_destroy(service);
        return result;
    }
    VdVcsHistoryResult raw{};
    std::memset(&raw, 0, sizeof(raw));
    int state = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 4000 && state == VD_STATUS_PENDING; ++attempt) {
        state = vd_vcs_history_poll(service, job, &raw);
        if (state == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (state == VD_STATUS_READY) {
        result = takeHistory(raw);
        vd_vcs_history_free(&raw);
    } else {
        result.error = true;
        result.note = serviceErrorText(service);
        if (result.note.empty()) {
            result.note = "the history read did not complete (state " + std::to_string(state) + ")";
        }
    }
    vd_service_destroy(service);
    return result;
}

void FfiVcsSource::requestHistory(const HistoryRequest &request, HistoryCallback done)
{
    if (service_ == nullptr) {
        HistoryData result;
        result.kind = request.kind;
        result.error = true;
        result.note = "this build has no vd-ffi VCS ABI";
        if (done) {
            done(std::move(result));
        }
        return;
    }
    startHistory(request, std::move(done));
}

void FfiVcsSource::startHistory(const HistoryRequest &request, HistoryCallback done)
{
    const unsigned long long job = startHistoryJob(service_, request);
    if (job == 0) {
        HistoryData result;
        result.kind = request.kind;
        result.error = true;
        result.note = serviceErrorText(service_);
        if (result.note.empty()) {
            result.note = "the VCS history job was rejected";
        }
        if (done) {
            done(std::move(result));
        }
        return;
    }
    auto pending = std::make_unique<PendingHistory>();
    pending->job = job;
    pending->kind = request.kind;
    pending->done = std::move(done);
    history_jobs_.push_back(std::move(pending));
    ensureTimer();
}

void FfiVcsSource::finishHistory(std::unique_ptr<PendingHistory> pending, VdVcsHistoryResult &raw)
{
    HistoryData data = takeHistory(raw);
    vd_vcs_history_free(&raw);
    HistoryCallback done = pending ? std::move(pending->done) : HistoryCallback{};
    if (done) {
        done(std::move(data));
    }
}

WorkspaceData FfiVcsSource::workspace(const WorkspaceRequest &request)
{
    // Synchronous seam (tests/legacy): the same job, polled to completion. The
    // panel uses requestWorkspace() instead, so the GUI thread never waits.
    WorkspaceData result;
    result.kind = request.kind;
    VdService *service = vd_service_create();
    if (service == nullptr) {
        result.error = true;
        result.note = "vd_service_create failed";
        return result;
    }
    const unsigned long long job = startWorkspaceJob(service, request);
    if (job == 0) {
        result.error = true;
        result.note = serviceErrorText(service);
        if (result.note.empty()) {
            result.note = "the VCS workspace job was rejected";
        }
        vd_service_destroy(service);
        return result;
    }
    VdVcsWorkspaceResult raw{};
    std::memset(&raw, 0, sizeof(raw));
    int state = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 4000 && state == VD_STATUS_PENDING; ++attempt) {
        state = vd_vcs_workspace_poll(service, job, &raw);
        if (state == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (state == VD_STATUS_READY) {
        result = takeWorkspace(raw);
        vd_vcs_workspace_free(&raw);
    } else {
        result.error = true;
        result.note = serviceErrorText(service);
        if (result.note.empty()) {
            result.note = "the workspace read did not complete (state "
                + std::to_string(state) + ")";
        }
    }
    vd_service_destroy(service);
    return result;
}

void FfiVcsSource::requestWorkspace(const WorkspaceRequest &request, WorkspaceCallback done)
{
    if (service_ == nullptr) {
        WorkspaceData result;
        result.kind = request.kind;
        result.error = true;
        result.note = "this build has no vd-ffi VCS ABI";
        if (done) {
            done(std::move(result));
        }
        return;
    }
    startWorkspace(request, std::move(done));
}

void FfiVcsSource::startWorkspace(const WorkspaceRequest &request, WorkspaceCallback done)
{
    const unsigned long long job = startWorkspaceJob(service_, request);
    if (job == 0) {
        WorkspaceData result;
        result.kind = request.kind;
        result.error = true;
        result.note = serviceErrorText(service_);
        if (result.note.empty()) {
            result.note = "the VCS workspace job was rejected";
        }
        if (done) {
            done(std::move(result));
        }
        return;
    }
    auto pending = std::make_unique<PendingWorkspace>();
    pending->job = job;
    pending->kind = request.kind;
    pending->done = std::move(done);
    workspace_jobs_.push_back(std::move(pending));
    ensureTimer();
}

void FfiVcsSource::finishWorkspace(std::unique_ptr<PendingWorkspace> pending,
                                   VdVcsWorkspaceResult &raw)
{
    WorkspaceData data = takeWorkspace(raw);
    vd_vcs_workspace_free(&raw);
    WorkspaceCallback done = pending ? std::move(pending->done) : WorkspaceCallback{};
    if (done) {
        done(std::move(data));
    }
}

void FfiVcsSource::pumpQueued()
{
    if (service_ == nullptr) {
        return;
    }
    if (info_job_ || status_job_ || diff_job_ || write_job_) {
        return;
    }
    if (queued_write_) {
        std::unique_ptr<QueuedWrite> queued = std::move(queued_write_);
        queued_write_.reset();
        startWrite(queued->request, std::move(queued->done));
        return;
    }
    if (deferred_status_) {
        std::unique_ptr<DeferredStatus> deferred = std::move(deferred_status_);
        deferred_status_.reset();
        startStatus(deferred->root, std::move(deferred->done));
    }
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
            result.error = true;
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

    if (write_job_) {
        VdVcsWriteResult raw{};
        std::memset(&raw, 0, sizeof(raw));
        const int state = vd_vcs_write_poll(service_, write_job_->job, &raw);
        if (state == VD_STATUS_READY) {
            WriteResult result = takeWriteResult(raw, write_job_->op);
            vd_vcs_write_free(&raw);
            WriteCallback done = std::move(write_job_->done);
            write_job_.reset();
            if (done) {
                done(std::move(result));
            }
        } else if (state != VD_STATUS_PENDING) {
            WriteResult result;
            result.op = write_job_->op;
            result.note = serviceErrorText(service_);
            if (result.note.empty()) {
                result.note = "the VCS write job failed (state " + std::to_string(state) + ")";
            }
            vd_vcs_write_free(&raw);
            WriteCallback done = std::move(write_job_->done);
            write_job_.reset();
            if (done) {
                done(std::move(result));
            }
        }
    }

    // Batch 4b: every pending history read is polled on the same timer. They are
    // independent jobs (no index lock), so several may finish in one tick.
    for (std::size_t index = 0; index < history_jobs_.size();) {
        PendingHistory *pending = history_jobs_[index].get();
        VdVcsHistoryResult raw{};
        std::memset(&raw, 0, sizeof(raw));
        const int state = vd_vcs_history_poll(service_, pending->job, &raw);
        if (state == VD_STATUS_PENDING) {
            ++index;
            continue;
        }
        std::unique_ptr<PendingHistory> finished = std::move(history_jobs_[index]);
        history_jobs_.erase(history_jobs_.begin() + static_cast<std::ptrdiff_t>(index));
        if (state == VD_STATUS_READY) {
            finishHistory(std::move(finished), raw);
        } else {
            HistoryData result;
            result.kind = finished->kind;
            result.error = true;
            result.note = serviceErrorText(service_);
            if (result.note.empty()) {
                result.note = "the history read failed (state " + std::to_string(state) + ")";
            }
            HistoryCallback done = std::move(finished->done);
            if (done) {
                done(std::move(result));
            }
        }
    }

    // Batch 5b: every pending workspace listing is polled on the same timer.
    for (std::size_t index = 0; index < workspace_jobs_.size();) {
        PendingWorkspace *pending = workspace_jobs_[index].get();
        VdVcsWorkspaceResult raw{};
        std::memset(&raw, 0, sizeof(raw));
        const int state = vd_vcs_workspace_poll(service_, pending->job, &raw);
        if (state == VD_STATUS_PENDING) {
            ++index;
            continue;
        }
        std::unique_ptr<PendingWorkspace> finished = std::move(workspace_jobs_[index]);
        workspace_jobs_.erase(workspace_jobs_.begin() + static_cast<std::ptrdiff_t>(index));
        if (state == VD_STATUS_READY) {
            finishWorkspace(std::move(finished), raw);
        } else {
            WorkspaceData result;
            result.kind = finished->kind;
            result.error = true;
            result.note = serviceErrorText(service_);
            if (result.note.empty()) {
                result.note = "the workspace read failed (state " + std::to_string(state) + ")";
            }
            WorkspaceCallback done = std::move(finished->done);
            if (done) {
                done(std::move(result));
            }
        }
    }

    // Nothing running: start whatever the read/write serialization held back.
    pumpQueued();
    if (!info_job_ && !status_job_ && !diff_job_ && !write_job_ && !queued_write_
        && !deferred_status_ && history_jobs_.empty() && workspace_jobs_.empty()
        && timer_ != nullptr) {
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
        result.error = true;
    } else {
        result.note = "vcs status job did not complete (state " + std::to_string(state) + ")";
        result.error = true;
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

void StubVcsSource::requestStatus(const std::string &root, StatusCallback done)
{
    if (defer_status_ && !pending_status_) {
        pending_status_ = std::move(done);
        return;
    }
    if (done) {
        done(status(root));
    }
}

void StubVcsSource::flushStatus()
{
    StatusCallback done = std::move(pending_status_);
    pending_status_ = nullptr;
    if (done) {
        done(status_);
    }
}

HistoryData StubVcsSource::history(const HistoryRequest &request)
{
    ++calls_;
    history_requests_.push_back(request);
    const auto failure = history_errors_.find(static_cast<int>(request.kind));
    if (failure != history_errors_.end()) {
        HistoryData result;
        result.kind = request.kind;
        result.error = true;
        result.note = failure->second;
        return result;
    }
    const auto found = history_data_.find(static_cast<int>(request.kind));
    if (found == history_data_.end()) {
        HistoryData result;
        result.kind = request.kind;
        result.note = "the stub has no data for this history read";
        return result;
    }
    HistoryData result = found->second;
    result.kind = request.kind;
    result.offset = request.offset;
    return result;
}

void StubVcsSource::requestHistory(const HistoryRequest &request, HistoryCallback done)
{
    if (done) {
        done(history(request));
    }
}

void StubVcsSource::setHistoryData(HistoryKind kind, HistoryData data)
{
    history_data_[static_cast<int>(kind)] = std::move(data);
}

void StubVcsSource::setHistoryError(HistoryKind kind, const std::string &note)
{
    history_errors_[static_cast<int>(kind)] = note;
}

WriteResult StubVcsSource::write(const WriteRequest &request)
{
    ++write_attempts_;
    writes_.push_back(request);
    WriteResult result;
    result.op = request.op;
    result.ok = write_ok_;
    if (!write_ok_) {
        result.note = write_note_.empty() ? std::string("the stub refused the write") : write_note_;
        result.stderrText = write_stderr_;
    } else {
        result.stdoutText = "stub: " + std::string(writeOpName(request.op));
    }
    return result;
}

void StubVcsSource::requestWrite(const WriteRequest &request, WriteCallback done)
{
    if (done) {
        done(write(request));
    }
}

void StubVcsSource::setWriteOutcome(bool ok, const std::string &note)
{
    write_ok_ = ok;
    write_note_ = note;
}

void StubVcsSource::setWriteStderr(const std::string &text)
{
    write_stderr_ = text;
}

void StubVcsSource::setInfo(Info info)
{
    info_ = std::move(info);
}

WorkspaceData StubVcsSource::workspace(const WorkspaceRequest &request)
{
    workspace_requests_.push_back(request);
    const auto found = workspace_data_.find(static_cast<int>(request.kind));
    if (found != workspace_data_.end()) {
        return found->second;
    }
    const auto error = workspace_errors_.find(static_cast<int>(request.kind));
    if (error != workspace_errors_.end()) {
        WorkspaceData result;
        result.kind = request.kind;
        result.error = true;
        result.note = error->second;
        return result;
    }
    WorkspaceData result;
    result.kind = request.kind;
    result.backend = Backend::Git;
    return result;
}

void StubVcsSource::requestWorkspace(const WorkspaceRequest &request, WorkspaceCallback done)
{
    if (done) {
        done(workspace(request));
    }
}

void StubVcsSource::setWorkspaceData(WorkspaceKind kind, WorkspaceData data)
{
    workspace_data_[static_cast<int>(kind)] = std::move(data);
}

void StubVcsSource::setWorkspaceError(WorkspaceKind kind, const std::string &note)
{
    workspace_errors_[static_cast<int>(kind)] = note;
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
