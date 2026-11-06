#pragma once

// Panel-facing VCS seam (T23). The panel never talks to git/svn directly: it
// renders whatever a VcsSource reports, so the same widget is driven by the
// production backend (C ABI -> vd-vcs) or by a stub in tests.
//
// The seam is deliberately read-only: there is no method through which the panel
// could request a commit/update/revert/switch. The absence of write methods is
// the panel-side half of the "no write operation is ever called" guarantee; the
// backend-side half is the allow-list in crates/vd-vcs/src/runner.rs.
#include <QString>

#include <cstddef>
#include <functional>
#include <map>
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
struct VdVcsHistoryResult;
struct VdVcsWorkspaceResult;

namespace vedit::workspace::vcs {

enum class Backend { None, Git, Subversion };

// Batch 7 (i18n): the two display-name helpers return a TRANSLATED QString
// (context "VEdit"), so the header, the refs tree and the change rows show the
// catalogue's language instead of the bare English literal. They previously
// returned `const char *` without any translate() call (found in batch 7a).
QString backendName(Backend backend);

struct Change {
    std::string path;
    int kind = 0; // 1 added, 2 modified, 3 deleted, 4 renamed, 5 conflict, 6 unknown
};

QString changeKindName(int kind);

// --- refs (batch 4a, AC-4.1 / AC-4.2) --------------------------------------
//
// The codes mirror the VdVcsRef.kind values from the C ABI one for one.
enum class RefKind : int {
    Branch = 1,       // local branch (refs/heads/*)
    Tag = 2,          // tag (refs/tags/*)
    RemoteBranch = 3, // remote-tracking branch (refs/remotes/*)
    Other = 4,        // anything else the backend reported
};

const char *refKindName(RefKind kind);

struct RefEntry {
    std::string name;   // short name: `main`, `v1.0`, `origin/main`
    RefKind kind = RefKind::Other;
    std::string target; // abbreviated object id, may be empty
    bool current = false; // the checked-out branch
};

struct RemoteEntry {
    std::string name; // `origin`
    std::string url;  // fetch URL as configured
};

// The multi-commit operation in progress (batch 5a). The conflict FILES are the
// status changes with kind 5, so no separate read is needed for them.
enum class GitOperation : int {
    None = 0,
    Merge = 1,
    Rebase = 2,
    CherryPick = 3,
    Revert = 4,
};

const char *gitOperationName(GitOperation operation);
// The git word for the operation (`merge`, `rebase`, ...), empty for None.
std::string gitOperationToken(GitOperation operation);

struct Info {
    Backend backend = Backend::None;
    std::string branch;
    std::string revision;
    std::string note;
    // Batch 4a: the same read carries the ref listing, the configured remotes and
    // the current branch's upstream, so one job refreshes the whole refs column.
    std::vector<RefEntry> refs;
    std::vector<RemoteEntry> remotes;
    std::string upstream;
    GitOperation operation = GitOperation::None;
};

struct Status {
    Backend backend = Backend::None;
    std::string branch;
    std::vector<Change> changes;
    std::string note;
    // The backend reported a FAILURE (git/svn could not run, the job failed), as
    // opposed to the normal "this directory is not a working tree" answer. The
    // panel renders the two as different states (error vs empty).
    bool error = false;
};

struct Diff {
    std::string path;
    std::string text;
    bool binary = false;
    std::string note;
};

// --- writes (batch 3b, AC-3.3; batch 4a extends them, AC-4.1 / AC-4.2) ----
//
// The codes mirror VD_VCS_WRITE_* from crates/vd-ffi/include/vd_ffi.h one for
// one; the lane selftest asserts every value against the header, so the seam and
// the C ABI cannot drift apart silently.
enum class WriteOp : int {
    None = 0,
    GitStage = 1,   // git add -- <paths>
    GitUnstage = 2, // git reset HEAD -- <paths>
    GitDiscard = 3, // git checkout -- <paths>   (irreversible)
    GitCommit = 4,  // git commit -m <message> [--author=<author>] [--amend] [-- <paths>]
    GitFetch = 5,   // git fetch [<remote>]
    GitPull = 6,    // git pull --ff-only [<remote> [<branch>]]
    GitPush = 7,    // git push [--set-upstream] [<remote> [<branch>]]
    GitBranchCreate = 8,  // git branch <name> [<start-point>]
    GitBranchSwitch = 9,  // git checkout <name>
    GitBranchDelete = 10, // git branch -d <name>      (irreversible)
    SvnCommit = 11,
    SvnUpdate = 12,
    SvnRevert = 13, // irreversible
    SvnAdd = 14,
    SvnRemove = 15,
    GitBranchRename = 16, // git branch -m <from> <to>
    GitTagCreate = 17,    // git tag <name> [<target>]
    GitTagDelete = 18,    // git tag -d <name>         (irreversible)
    GitCheckout = 19,     // git checkout <ref>        (detached HEAD)
    GitRemoteAdd = 20,    // git remote add <name> <url>
    GitRemoteRemove = 21, // git remote rm <name>      (reversible: re-add)
    GitRemoteSetUrl = 22, // git remote set-url <name> <url>
    GitSetUpstream = 23,  // git branch --set-upstream-to=<upstream> <branch>
    // Batch 5a: history operations and conflict resolution.
    GitMerge = 24,      // git merge --no-edit <target>
    GitRebase = 25,     // git rebase <target> [<name>]
    GitCherryPick = 26, // git cherry-pick <target>
    GitRevert = 27,     // git revert --no-edit <target>
    GitReset = 28,      // git reset --<mode> <target>   (hard is irreversible)
    GitClean = 29,      // git clean -f -- <paths>       (irreversible)
    GitContinue = 30,   // git <target> --continue
    GitAbort = 31,      // git <target> --abort
    // Batch 5b: the Git workspace operations (`target` is the stash ref / the
    // worktree path; `name` is the existing branch of `worktree add`).
    GitStashSave = 32,      // git stash save [--] [<message>]
    GitStashPop = 33,       // git stash pop [<stash>]
    GitStashApply = 34,     // git stash apply [<stash>]
    GitStashDrop = 35,      // git stash drop [<stash>]      (irreversible)
    GitWorktreeAdd = 36,    // git worktree add <path> [<branch>]
    GitWorktreeRemove = 37, // git worktree remove <path>     (irreversible)
    GitSubmoduleInit = 38,  // git submodule init
    GitSubmoduleUpdate = 39, // git submodule update --init
};

const char *writeOpName(WriteOp op);
bool writeOpIsGit(WriteOp op);
bool writeOpIsSvn(WriteOp op);
// The operations that talk to a remote (fetch / pull / push): the only ones that
// may carry credentials, and the only ones whose failure can be an authentication
// failure worth a credential prompt.
bool writeOpIsNetwork(WriteOp op);
// The operations the product always confirms first (their effect cannot be
// undone from the panel). The panel asks for confirmation on these; the backend
// does not enforce it.
bool writeOpIsIrreversible(WriteOp op);

struct WriteRequest {
    WriteOp op = WriteOp::None;
    std::string root;    // working tree (the panel's project root)
    std::string message; // commit operations only
    std::string author;  // GitCommit only
    bool amend = false;  // GitCommit only
    std::vector<std::string> paths; // repository-relative, as Status reports them
    // Batch 4a. `name` is the branch / tag / remote / upstream local branch;
    // `target` the start point, rename destination, checkout ref, remote name for
    // fetch/pull/push (or the upstream ref for GitSetUpstream); `url` the remote
    // URL. See the field comments in crates/vd-ffi/include/vd_ffi.h.
    std::string name;
    std::string target;
    std::string url;
    // Credentials for a network operation. Held in memory only: they never enter
    // the tool argv, are never written to disk and are redacted out of every
    // captured output string by the backend.
    std::string credential_user;
    std::string credential_password;
    bool set_upstream = false; // GitPush only
    std::string mode; // GitReset only: soft / mixed / hard
};

struct WriteResult {
    WriteOp op = WriteOp::None;
    bool ok = false; // the tool exited zero
    std::string stdoutText;
    std::string stderrText;
    std::string note; // why the request produced no tool run

    // One user-facing line: the first stderr line when the tool failed, the note
    // when the request never ran, empty when it succeeded.
    std::string summary() const;
};


// --- history reads (batch 4b, AC-4.3) ---------------------------------------
//
// One request/response pair covers the five reads: the commit list (paged +
// searched), one commit's detail, a single file's history, blame and a two-commit
// comparison. The `kind` says which fields of the result are meaningful, exactly
// like the C ABI's `VdVcsHistoryResult.kind`, so the seam, the ABI and the panel
// cannot drift apart.
enum class HistoryKind : int {
    Commits = 1,    // one page of the commit list
    Commit = 2,     // one commit's detail
    FileHistory = 3, // one page of a single file's history
    Blame = 4,      // one file's blame
    Compare = 5,    // `git diff <from> <to>`
};

const char *historyKindName(HistoryKind kind);

// Upper bound on one page, mirroring vd_vcs::history::MAX_PAGE.
constexpr int kMaxHistoryPage = 200;

struct CommitEntry {
    std::string hash;
    std::string shortHash;
    std::string author;
    std::string email;
    long long time = 0; // unix seconds
    std::string subject;
};

struct FileStat {
    std::string path;
    // -1 when git reported the file as binary.
    long long additions = 0;
    long long deletions = 0;
};

struct BlameLine {
    int line = 0; // 1-based
    std::string hash;
    std::string shortHash;
    std::string author;
    long long time = 0;
    std::string text;
};

struct HistoryRequest {
    HistoryKind kind = HistoryKind::Commits;
    std::string root;
    // Commit / ref pair or path, per kind.
    std::string hash;   // Commit
    std::string path;   // FileHistory / Blame
    std::string from;   // Compare
    std::string to;     // Compare
    std::string query;  // Commits (literal, case-insensitive)
    int offset = 0;
    int limit = 50;
};

struct HistoryData {
    HistoryKind kind = HistoryKind::Commits;
    Backend backend = Backend::None;
    std::vector<CommitEntry> commits;
    std::vector<FileStat> files;
    std::vector<BlameLine> blame;
    std::string hash;
    std::string author;
    std::string email;
    std::string subject;
    std::string body;
    std::string patch;
    std::string path;
    std::string from;
    std::string to;
    int offset = 0;
    bool hasMore = false;
    bool truncated = false;
    // The read FAILED (git reported an error) as opposed to being empty (an
    // empty repository, an untouched path) — the two are different panel states.
    bool error = false;
    long long time = 0;
    std::string note;
};

// --- workspace listings (batch 5b, AC-5.2) ----------------------------------
//
// Two asynchronous reads: the stash stack and the linked worktrees. `kind`
// selects which fields of one row are meaningful, exactly like the C ABI's
// `VdVcsWorkspaceResult.kind`, so the seam, the ABI and the panel cannot drift
// apart. Both are Git-only reads; a Subversion working tree answers with a note.
enum class WorkspaceKind : int {
    Stashes = 1,   // `git stash list`
    Worktrees = 2, // `git worktree list`
};

const char *workspaceKindName(WorkspaceKind kind);

struct WorkspaceEntry {
    // Stash fields.
    std::string refName; // `stash@{0}`
    std::string subject; // `On main: ...`
    // Worktree fields.
    std::string path;   // absolute path
    std::string branch; // empty when detached / bare
    // Shared.
    std::string hash; // commit id (abbreviated for a worktree)
    bool mainWorktree = false;
    bool detached = false;
    bool bare = false;
};

struct WorkspaceData {
    WorkspaceKind kind = WorkspaceKind::Stashes;
    Backend backend = Backend::None;
    std::vector<WorkspaceEntry> entries;
    bool truncated = false;
    // The read FAILED (the tool reported an error, e.g. `git worktree` does not
    // exist on the delivery baseline) as opposed to being empty; `note` then
    // carries git's real reason.
    bool error = false;
    std::string note;
};

struct WorkspaceRequest {
    WorkspaceKind kind = WorkspaceKind::Stashes;
    std::string root;
};

// True when a failed network operation failed because the transport could not
// obtain credentials (git's "could not read Username …: terminal prompts
// disabled", "Authentication failed", an SSH publickey refusal, …), which is
// exactly the case the credential dialog exists for. Never inspects anything but
// the (already redacted) captured text.
bool writeResultNeedsCredentials(const WriteResult &result);

class VcsSource {
public:
    using InfoCallback = std::function<void(Info)>;
    using StatusCallback = std::function<void(Status)>;
    using DiffCallback = std::function<void(Diff)>;
    using WriteCallback = std::function<void(WriteResult)>;
    using HistoryCallback = std::function<void(HistoryData)>;

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

    // Write seam (batch 3b). A source that has no write path says so through
    // writeCapable() and its requestWrite() answers with a note instead of
    // pretending a tool ran.
    virtual bool writeCapable() const { return false; }
    virtual WriteResult write(const WriteRequest &request);
    virtual void requestWrite(const WriteRequest &request, WriteCallback done);

    // History seam (batch 4b). A source without a history path answers with an
    // empty result whose `note` says so instead of pretending a read ran. The
    // default asynchronous implementation calls the synchronous one, so stubs and
    // tests keep their synchronous semantics; FfiVcsSource runs a C ABI job and
    // polls it, so no `git log` ever runs on the GUI thread.
    virtual bool historyCapable() const { return false; }
    virtual HistoryData history(const HistoryRequest &request);
    virtual void requestHistory(const HistoryRequest &request, HistoryCallback done);

    // Workspace seam (batch 5b). A source without a workspace path answers with an
    // empty result whose `note` says so; the default asynchronous implementation
    // calls the synchronous one, and FfiVcsSource runs a C ABI job polled on its
    // QTimer, so no `git stash list` / `git worktree list` runs on the GUI thread.
    using WorkspaceCallback = std::function<void(WorkspaceData)>;
    virtual bool workspaceCapable() const { return false; }
    virtual WorkspaceData workspace(const WorkspaceRequest &request);
    virtual void requestWorkspace(const WorkspaceRequest &request, WorkspaceCallback done);
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

    bool writeCapable() const override { return service_ != nullptr; }
    WriteResult write(const WriteRequest &request) override;
    void requestWrite(const WriteRequest &request, WriteCallback done) override;

    bool historyCapable() const override { return service_ != nullptr; }
    HistoryData history(const HistoryRequest &request) override;
    void requestHistory(const HistoryRequest &request, HistoryCallback done) override;

    bool workspaceCapable() const override { return service_ != nullptr; }
    WorkspaceData workspace(const WorkspaceRequest &request) override;
    void requestWorkspace(const WorkspaceRequest &request, WorkspaceCallback done) override;

    // Diagnostics for the non-blocking regression test: how many poll rounds the
    // timer has performed, and how many write jobs were started (both 0 when the
    // build has no FFI).
    std::size_t pollRounds() const { return poll_rounds_; }
    std::size_t writeJobs() const { return write_jobs_; }

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
    struct PendingWrite {
        unsigned long long job = 0;
        WriteOp op = WriteOp::None;
        WriteCallback done;
    };
    // Batch 3b: reads and writes are serialized per source. `git status`
    // opportunistically rewrites the index and therefore takes `.git/index.lock`;
    // starting `git add`/`git commit` while a status read is running made one of
    // them fail with "index.lock: File exists" under the E2E probe. Reads are
    // cheap to repeat, so a read that arrives during a write is kept as the
    // newest deferred request, and a write that arrives during a read waits for
    // the read (pumpQueued()).
    struct DeferredStatus {
        std::string root;
        StatusCallback done;
    };
    struct QueuedWrite {
        WriteRequest request;
        WriteCallback done;
    };
    // Batch 4b: one pending history read. Several may be in flight (a fast user
    // can ask for a page and a detail at once); each carries its own callback.
    struct PendingHistory {
        unsigned long long job = 0;
        HistoryKind kind = HistoryKind::Commits;
        HistoryCallback done;
    };
    // Batch 5b: one pending workspace listing (stashes or worktrees).
    struct PendingWorkspace {
        unsigned long long job = 0;
        WorkspaceKind kind = WorkspaceKind::Stashes;
        WorkspaceCallback done;
    };

    void ensureTimer();
    void pollJobs();
    void finishInfo(int state, VdVcsInfo *raw);
    void finishStatus(int state, VdVcsStatus *raw);
    void finishDiff(int state, VdVcsDiff *raw);
    void startStatus(const std::string &root, StatusCallback done);
    void startWrite(const WriteRequest &request, WriteCallback done);
    void startHistory(const HistoryRequest &request, HistoryCallback done);
    void finishHistory(std::unique_ptr<PendingHistory> pending, VdVcsHistoryResult &raw);
    void startWorkspace(const WorkspaceRequest &request, WorkspaceCallback done);
    void finishWorkspace(std::unique_ptr<PendingWorkspace> pending, VdVcsWorkspaceResult &raw);
    void pumpQueued();

    VdService *service_ = nullptr;
    QTimer *timer_ = nullptr;
    std::unique_ptr<PendingInfo> info_job_;
    std::unique_ptr<PendingStatus> status_job_;
    std::unique_ptr<PendingDiff> diff_job_;
    std::unique_ptr<PendingWrite> write_job_;
    std::unique_ptr<QueuedWrite> queued_write_;
    std::unique_ptr<DeferredStatus> deferred_status_;
    std::vector<std::unique_ptr<PendingHistory>> history_jobs_;
    std::vector<std::unique_ptr<PendingWorkspace>> workspace_jobs_;
    std::size_t poll_rounds_ = 0;
    std::size_t write_jobs_ = 0;
};

// Test/dev stub: canned data, records every call, counts write attempts (there
// is no write method, so any attempt would have to be a future API change).
class StubVcsSource final : public VcsSource {
public:
    StubVcsSource(Info info, Status status, std::vector<Diff> diffs);

    Info info(const std::string &root) override;
    Status status(const std::string &root) override;
    Diff diff(const std::string &root, const std::string &path) override;
    void requestStatus(const std::string &root, StatusCallback done) override;

    bool writeCapable() const override { return true; }
    WriteResult write(const WriteRequest &request) override;
    void requestWrite(const WriteRequest &request, WriteCallback done) override;

    bool historyCapable() const override { return true; }
    HistoryData history(const HistoryRequest &request) override;
    void requestHistory(const HistoryRequest &request, HistoryCallback done) override;
    // Canned history per kind; the request is recorded so a probe can assert the
    // exact page/ref/path the panel asked for.
    void setHistoryData(HistoryKind kind, HistoryData data);
    void setHistoryError(HistoryKind kind, const std::string &note);
    const std::vector<HistoryRequest> &historyRequests() const { return history_requests_; }

    bool workspaceCapable() const override { return true; }
    WorkspaceData workspace(const WorkspaceRequest &request) override;
    void requestWorkspace(const WorkspaceRequest &request, WorkspaceCallback done) override;
    // Canned listing per kind; the request is recorded so a probe can assert the
    // exact listing the panel asked for.
    void setWorkspaceData(WorkspaceKind kind, WorkspaceData data);
    void setWorkspaceError(WorkspaceKind kind, const std::string &note);
    const std::vector<WorkspaceRequest> &workspaceRequests() const { return workspace_requests_; }

    std::size_t calls() const { return calls_; }
    std::size_t writeAttempts() const { return write_attempts_; }
    const std::vector<WriteRequest> &writes() const { return writes_; }

    // Answers the next write with a failure instead of success.
    void setWriteOutcome(bool ok, const std::string &note = std::string());
    // Batch 4a: the stub reports this stderr verbatim on a failed write, which is
    // how the credential-retry path is driven with git's real authentication text.
    void setWriteStderr(const std::string &text);
    // Batch 4a: replace the canned Info (refs / remotes / upstream).
    void setInfo(Info info);

    // Holds the status callback until flushStatus() is called, which is how the
    // panel's LOADING state is observed deterministically.
    void setDeferStatus(bool defer) { defer_status_ = defer; }
    bool statusPending() const { return static_cast<bool>(pending_status_); }
    void flushStatus();

private:
    Info info_;
    Status status_;
    std::vector<Diff> diffs_;
    std::size_t calls_ = 0;
    std::size_t write_attempts_ = 0;
    std::vector<WriteRequest> writes_;
    bool write_ok_ = true;
    std::string write_note_;
    std::string write_stderr_;
    std::map<int, HistoryData> history_data_;
    std::map<int, std::string> history_errors_;
    std::vector<HistoryRequest> history_requests_;
    std::map<int, WorkspaceData> workspace_data_;
    std::map<int, std::string> workspace_errors_;
    std::vector<WorkspaceRequest> workspace_requests_;
    bool defer_status_ = false;
    StatusCallback pending_status_;
};

} // namespace vedit::workspace::vcs
