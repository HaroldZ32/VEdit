//! VCS backend (service 6 of 7).
//!
//! Backend selection, branch and per-file changes come from `vd-vcs`; this
//! module is the C projection. "No repository" is a normal result
//! (`backend = 0`) with a note, never an error — the panel has to render an
//! empty state, not a failure dialog.

use crate::capi::{
    c_string, free_string, VdError, VdVcsChange, VdVcsStatus, VD_ERR_INVALID_ARGUMENT,
    VD_ERR_SERVICE, VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::ffi::c_char;
use std::path::{Path, PathBuf};

/// One changed path, owned.
pub(crate) struct VcsChangeData {
    pub(crate) path: String,
    pub(crate) kind: i32,
}

/// A status snapshot, owned.
pub(crate) struct VcsData {
    pub(crate) backend: i32,
    pub(crate) branch: Option<String>,
    pub(crate) changes: Vec<VcsChangeData>,
    pub(crate) note: Option<String>,
}

impl VcsData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with `vd_vcs_status_free`.
    pub(crate) unsafe fn into_c(self) -> VdVcsStatus {
        let items: Vec<VdVcsChange> = self
            .changes
            .into_iter()
            .map(|change| VdVcsChange {
                path: c_string(&change.path),
                kind: change.kind,
            })
            .collect();
        let (count, changes) = if items.is_empty() {
            (0, std::ptr::null_mut())
        } else {
            (
                items.len(),
                Box::into_raw(items.into_boxed_slice()) as *mut VdVcsChange,
            )
        };
        VdVcsStatus {
            backend: self.backend,
            branch: match &self.branch {
                Some(branch) => c_string(branch),
                None => std::ptr::null_mut(),
            },
            count,
            changes,
            note: match &self.note {
                Some(note) => c_string(note),
                None => std::ptr::null_mut(),
            },
        }
    }
}

/// Query the working tree containing `root`.
pub(crate) fn status(root: &Path) -> Result<VcsData, VdError> {
    if !root.is_dir() {
        return Err(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            &format!("not a directory: {}", root.display()),
        ));
    }
    let report = vd_vcs::status(root);
    Ok(VcsData {
        backend: report.backend.as_i32(),
        branch: report.branch,
        changes: report
            .changes
            .into_iter()
            .map(|change| VcsChangeData {
                path: change.path,
                kind: change.kind.as_i32(),
            })
            .collect(),
        note: report.note,
    })
}

/// Start a VCS status job for `root`.
///
/// Returns the job id, or `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; `root` must be null or a valid NUL-terminated
/// string.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_status(service: *mut VdService, root: *const c_char) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(root) = crate::capi::c_str(root) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root must be a valid UTF-8 path",
        ));
        return 0;
    };
    let root = PathBuf::from(root);
    state.start(move || status(&root).map(JobValue::Vcs))
}

/// Poll a VCS job.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdVcsStatus`].
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdVcsStatus,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    match state.poll(job) {
        Polled::Unknown => unknown_job(state),
        Polled::Pending => VD_STATUS_PENDING,
        Polled::Failed => VD_STATUS_FAILED,
        Polled::Ready(JobValue::Vcs(data)) => {
            *out = data.into_c();
            VD_STATUS_READY
        }
        Polled::Ready(_) => {
            state.record_error(VdError::new(
                crate::capi::VD_ERR_WRONG_JOB_KIND,
                "this job id belongs to a different service",
            ));
            POLL_MISUSE
        }
    }
}

/// Release a filled [`VdVcsStatus`]; null-safe.
///
/// # Safety
///
/// `status` must be null or point to a struct filled by [`vd_vcs_poll`] and not
/// freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_status_free(status: *mut VdVcsStatus) {
    if status.is_null() {
        return;
    }
    let status = &mut *status;
    if !status.changes.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(status.changes, status.count);
        for change in slice.iter_mut() {
            free_string(change.path);
            change.path = std::ptr::null_mut();
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            status.changes,
            status.count,
        )));
    }
    free_string(status.branch);
    free_string(status.note);
    status.backend = 0;
    status.branch = std::ptr::null_mut();
    status.count = 0;
    status.changes = std::ptr::null_mut();
    status.note = std::ptr::null_mut();
}

// --- job-based read-only queries for the VCS panel (T23, async since T99) ---
//
// `vd_vcs_status`, `vd_vcs_info_job` and `vd_vcs_diff_job` are the service-layer
// jobs; the panel polls them and never waits for a `git`/`svn` subprocess on the
// GUI thread. All of them go through the same read-only allow-list
// (`ProcessVcsService` -> `SystemRunner`, which refuses any write subcommand
// before spawning).

/// One branch / tag / remote-tracking ref (owned strings).
#[repr(C)]
pub struct VdVcsRef {
    /// Short name: a branch (`main`), a tag (`v1.0`) or a remote-tracking ref
    /// (`origin/main`); NULL only when the listing was truncated mid-entry.
    pub name: *mut c_char,
    /// 1 branch, 2 tag, 3 remote-tracking, 4 other.
    pub kind: i32,
    /// Abbreviated object id (7 characters), or NULL.
    pub target: *mut c_char,
    /// 1 when this is the checked-out branch.
    pub current: i32,
}

/// One configured remote (owned strings).
#[repr(C)]
pub struct VdVcsRemote {
    /// Remote name (`origin`), or NULL.
    pub name: *mut c_char,
    /// Fetch URL as configured, or NULL.
    pub url: *mut c_char,
}

/// Branch + revision of the working tree containing `root` (owned strings).
///
/// Batch 4a: the same job also carries the ref listing, the configured remotes
/// and the current branch's upstream, so one asynchronous read refreshes the
/// whole refs column.
#[repr(C)]
pub struct VdVcsInfo {
    /// Backend: 0 none, 1 git, 2 svn.
    pub backend: i32,
    /// Branch name, or NULL.
    pub branch: *mut c_char,
    /// Short revision, or NULL.
    pub revision: *mut c_char,
    /// Why the identity is unavailable, or NULL.
    pub note: *mut c_char,
    /// Number of entries in `refs`.
    pub ref_count: usize,
    /// Branches / tags / remote-tracking refs, or NULL when `ref_count` is 0.
    pub refs: *mut VdVcsRef,
    /// Number of entries in `remotes`.
    pub remote_count: usize,
    /// Configured remotes, or NULL when `remote_count` is 0.
    pub remotes: *mut VdVcsRemote,
    /// Upstream of the current branch (`origin/main`), or NULL.
    pub upstream: *mut c_char,
    /// The multi-commit operation in progress: 0 none, 1 merge, 2 rebase,
    /// 3 cherry-pick, 4 revert (batch 5a). The unmerged files are the status
    /// changes with kind 5, so the conflict list needs no separate read.
    pub operation: i32,
}

/// One file's unified diff (owned strings).
#[repr(C)]
pub struct VdVcsDiff {
    /// Path asked for, or NULL.
    pub path: *mut c_char,
    /// Unified diff text, or NULL.
    pub text: *mut c_char,
    /// 1 when the tool reported binary content.
    pub binary: i32,
    /// Why the diff is empty, or NULL.
    pub note: *mut c_char,
}

/// One commit row of the history reader (owned strings).
#[repr(C)]
pub struct VdVcsHistoryEntry {
    /// Full object id, or NULL.
    pub hash: *mut c_char,
    /// Abbreviated object id, or NULL.
    pub short_hash: *mut c_char,
    /// Author name, or NULL.
    pub author: *mut c_char,
    /// Author email, or NULL.
    pub email: *mut c_char,
    /// Author time, unix seconds.
    pub time: i64,
    /// First line of the commit message, or NULL.
    pub subject: *mut c_char,
}

/// One file of a commit / comparison (owned string).
#[repr(C)]
pub struct VdVcsFileStat {
    /// Repository-relative path, or NULL.
    pub path: *mut c_char,
    /// Added lines (`-1` when git reported the file as binary).
    pub additions: i64,
    /// Deleted lines (`-1` when binary).
    pub deletions: i64,
}

/// One blamed line (owned strings).
#[repr(C)]
pub struct VdVcsBlameLine {
    /// 1-based line number.
    pub line: u64,
    /// Full object id of the commit that introduced the line, or NULL.
    pub hash: *mut c_char,
    /// Abbreviated object id, or NULL.
    pub short_hash: *mut c_char,
    /// Author name, or NULL.
    pub author: *mut c_char,
    /// Author time, unix seconds.
    pub time: i64,
    /// The line's text without the trailing newline, or NULL.
    pub text: *mut c_char,
}

/// One stash entry or one linked worktree (batch 5b, AC-5.2), owned by the job
/// until it is converted to C. A stash fills `ref_name` / `hash` / `subject`; a
/// worktree fills `path` / `hash` / `branch` and the flags.
#[repr(C)]
pub struct VdVcsWorkspaceEntry {
    /// Stash reference (`stash@{0}`), or NULL for a worktree.
    pub ref_name: *mut c_char,
    /// Commit id the entry points at (abbreviated for a worktree), or NULL.
    pub hash: *mut c_char,
    /// Stash subject, or NULL for a worktree.
    pub subject: *mut c_char,
    /// Absolute worktree path, or NULL for a stash.
    pub path: *mut c_char,
    /// Checked-out branch of a worktree (empty when detached/bare), or NULL.
    pub branch: *mut c_char,
    /// 1 for the MAIN worktree (the first row `git worktree list` prints).
    pub main_worktree: i32,
    /// 1 when the worktree has a detached HEAD.
    pub detached: i32,
    /// 1 when the entry is a bare repository.
    pub bare: i32,
}

/// One workspace listing (batch 5b, AC-5.2), owned by the caller until freed.
///
/// `error` distinguishes a FAILED read from an empty list: the delivery baseline
/// (git 1.8.3.1) has no `git worktree` command, so its worktree listing carries
/// `error = 1` and git's real message in `note` instead of an invented empty list.
#[repr(C)]
pub struct VdVcsWorkspaceResult {
    /// Backend: 0 none, 1 git, 2 svn.
    pub backend: i32,
    /// 1 stashes, 2 worktrees.
    pub kind: i32,
    /// Number of entries in `entries`.
    pub count: usize,
    /// The rows, or NULL when `count` is 0.
    pub entries: *mut VdVcsWorkspaceEntry,
    /// 1 when the listing was cut at its cap.
    pub truncated: i32,
    /// 1 when the read failed (as opposed to an empty list).
    pub error: i32,
    /// Why the list is empty or failed (the tool's real reason), or NULL.
    pub note: *mut c_char,
}

/// One history read (batch 4b, AC-4.3), owned by the caller until freed.
///
/// `kind` says which arrays/strings are meaningful: `1` commit page
/// (`commits` with `offset` and `has_more`), `2` commit detail (`hash`, `author`,
/// `email`, `time`, `subject`, `body`, `files`, `patch`), `3` file history
/// (`path` with `commits`), `4` blame (`path` with `blame`), `5` comparison
/// (`from`, `to`, `files`, `patch`). `error` distinguishes a failed read from an
/// empty repository or an empty result, which carries `note` instead.
#[repr(C)]
pub struct VdVcsHistoryResult {
    /// Backend: 0 none, 1 git, 2 svn.
    pub backend: i32,
    /// 1 commit page, 2 commit detail, 3 file history, 4 blame, 5 comparison.
    pub kind: i32,
    /// Number of entries in `commits`.
    pub commit_count: usize,
    /// Commit rows, or NULL when `commit_count` is 0.
    pub commits: *mut VdVcsHistoryEntry,
    /// Number of entries in `files`.
    pub file_count: usize,
    /// File stats, or NULL when `file_count` is 0.
    pub files: *mut VdVcsFileStat,
    /// Number of entries in `blame`.
    pub blame_count: usize,
    /// Blame lines, or NULL when `blame_count` is 0.
    pub blame: *mut VdVcsBlameLine,
    /// Requested page offset.
    pub offset: u64,
    /// 1 when a further page exists.
    pub has_more: i32,
    /// 1 when the listing was cut at its cap.
    pub truncated: i32,
    /// 1 when the read failed (as opposed to an empty repository / no result).
    pub error: i32,
    /// Commit hash (detail, or the commit a blame row belongs to), or NULL.
    pub hash: *mut c_char,
    /// Author name (detail), or NULL.
    pub author: *mut c_char,
    /// Author email (detail), or NULL.
    pub email: *mut c_char,
    /// Commit subject (detail), or NULL.
    pub subject: *mut c_char,
    /// Commit body without the subject (detail), or NULL.
    pub body: *mut c_char,
    /// Unified patch (detail / comparison), or NULL.
    pub patch: *mut c_char,
    /// Repository-relative path (file history / blame), or NULL.
    pub path: *mut c_char,
    /// Comparison "from" side as requested, or NULL.
    pub from: *mut c_char,
    /// Comparison "to" side as requested, or NULL.
    pub to: *mut c_char,
    /// Author time, unix seconds (detail).
    pub time: i64,
    /// Why the read is empty or failed, or NULL.
    pub note: *mut c_char,
}

fn owned(text: Option<&str>) -> *mut c_char {
    text.map_or(std::ptr::null_mut(), c_string)
}

// --- job payloads for the two read-only panel queries (T99) ----------------
//
// T24 N-1: `vd_vcs_info` / `vd_vcs_diff` used to be synchronous C calls, so the
// panel ran one `git`/`svn` subprocess on the GUI thread. They are now job-based
// like `vd_vcs_status`: the start call only spawns the worker and returns the id,
// and the caller polls. The read-only gate is unchanged (the jobs call the same
// `ProcessVcsService`, whose runner refuses write subcommands before spawning).

/// One ref, owned by the job until it is converted to C.
pub(crate) struct VcsRefData {
    pub(crate) name: String,
    pub(crate) kind: i32,
    pub(crate) target: String,
    pub(crate) current: bool,
}

/// One remote, owned by the job until it is converted to C.
pub(crate) struct VcsRemoteData {
    pub(crate) name: String,
    pub(crate) url: String,
}

/// Branch + revision identity (plus the batch-4a ref listing), owned by the job
/// until it is converted to C.
pub(crate) struct VcsInfoPayload {
    pub(crate) backend: i32,
    pub(crate) branch: Option<String>,
    pub(crate) revision: Option<String>,
    pub(crate) note: Option<String>,
    pub(crate) refs: Vec<VcsRefData>,
    pub(crate) remotes: Vec<VcsRemoteData>,
    pub(crate) upstream: Option<String>,
    pub(crate) operation: i32,
}

impl VcsInfoPayload {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with `vd_vcs_info_free`.
    pub(crate) unsafe fn into_c(self) -> VdVcsInfo {
        let VcsInfoPayload {
            backend,
            branch,
            revision,
            note,
            refs,
            remotes,
            upstream,
            operation,
        } = self;
        let items: Vec<VdVcsRef> = refs
            .into_iter()
            .map(|entry| VdVcsRef {
                name: c_string(&entry.name),
                kind: entry.kind,
                target: c_string(&entry.target),
                current: i32::from(entry.current),
            })
            .collect();
        let (ref_count, refs) = if items.is_empty() {
            (0, std::ptr::null_mut())
        } else {
            (
                items.len(),
                Box::into_raw(items.into_boxed_slice()) as *mut VdVcsRef,
            )
        };
        let remote_items: Vec<VdVcsRemote> = remotes
            .into_iter()
            .map(|entry| VdVcsRemote {
                name: c_string(&entry.name),
                url: c_string(&entry.url),
            })
            .collect();
        let (remote_count, remotes) = if remote_items.is_empty() {
            (0, std::ptr::null_mut())
        } else {
            (
                remote_items.len(),
                Box::into_raw(remote_items.into_boxed_slice()) as *mut VdVcsRemote,
            )
        };
        VdVcsInfo {
            backend,
            branch: owned(branch.as_deref()),
            revision: owned(revision.as_deref()),
            note: owned(note.as_deref()),
            ref_count,
            refs,
            remote_count,
            remotes,
            upstream: owned(upstream.as_deref()),
            operation,
        }
    }
}

/// One file's unified diff, owned by the job until it is converted to C.
pub(crate) struct VcsDiffData {
    pub(crate) path: String,
    pub(crate) text: String,
    pub(crate) binary: bool,
    pub(crate) note: Option<String>,
}

impl VcsDiffData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with `vd_vcs_diff_free`.
    pub(crate) unsafe fn into_c(self) -> VdVcsDiff {
        VdVcsDiff {
            path: owned(Some(&self.path)),
            text: owned(Some(&self.text)),
            binary: i32::from(self.binary),
            note: owned(self.note.as_deref()),
        }
    }
}

// --- history reads (batch 4b, AC-4.3) --------------------------------------
//
// Five asynchronous reads over the same job registry: the commit list (paged +
// search), one commit's detail, a single file's history, blame and a two-commit
// comparison. They share ONE result struct (`VdVcsHistoryResult`) whose `kind`
// says which payload the job produced, so the ABI adds one poll/free pair instead
// of five. The GUI thread only starts a job and polls it.

/// History kind: one page of the commit list.
pub const VD_VCS_HISTORY_COMMITS: i32 = 1;
/// History kind: one commit's detail (meta + files + patch).
pub const VD_VCS_HISTORY_COMMIT: i32 = 2;
/// History kind: one page of a single file's history.
pub const VD_VCS_HISTORY_FILE: i32 = 3;
/// History kind: one file's blame.
pub const VD_VCS_HISTORY_BLAME: i32 = 4;
/// History kind: a two-commit comparison.
pub const VD_VCS_HISTORY_COMPARE: i32 = 5;

/// Workspace kind: the stash stack (`git stash list`).
pub const VD_VCS_WORKSPACE_STASHES: i32 = 1;
/// Workspace kind: the linked worktrees (`git worktree list`).
pub const VD_VCS_WORKSPACE_WORKTREES: i32 = 2;

/// One commit row, owned by the job until it is converted to C.
pub(crate) struct VcsHistoryEntryData {
    pub(crate) hash: String,
    pub(crate) short_hash: String,
    pub(crate) author: String,
    pub(crate) email: String,
    pub(crate) time: i64,
    pub(crate) subject: String,
}

/// One file stat, owned by the job until it is converted to C.
pub(crate) struct VcsFileStatData {
    pub(crate) path: String,
    pub(crate) additions: i64,
    pub(crate) deletions: i64,
}

/// One blamed line, owned by the job until it is converted to C.
pub(crate) struct VcsBlameLineData {
    pub(crate) line: usize,
    pub(crate) hash: String,
    pub(crate) short_hash: String,
    pub(crate) author: String,
    pub(crate) time: i64,
    pub(crate) text: String,
}

/// One history read result, owned by the job until it is converted to C.
pub(crate) struct VcsHistoryData {
    pub(crate) kind: i32,
    pub(crate) backend: i32,
    pub(crate) offset: usize,
    pub(crate) has_more: bool,
    pub(crate) truncated: bool,
    pub(crate) error: bool,
    pub(crate) commits: Vec<VcsHistoryEntryData>,
    pub(crate) files: Vec<VcsFileStatData>,
    pub(crate) blame: Vec<VcsBlameLineData>,
    pub(crate) hash: Option<String>,
    pub(crate) author: Option<String>,
    pub(crate) email: Option<String>,
    pub(crate) subject: Option<String>,
    pub(crate) body: Option<String>,
    pub(crate) patch: Option<String>,
    pub(crate) path: Option<String>,
    pub(crate) from: Option<String>,
    pub(crate) to: Option<String>,
    pub(crate) time: i64,
    pub(crate) note: Option<String>,
}

impl VcsHistoryData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with `vd_vcs_history_free`.
    pub(crate) unsafe fn into_c(self) -> VdVcsHistoryResult {
        let VcsHistoryData {
            kind,
            backend,
            offset,
            has_more,
            truncated,
            error,
            commits,
            files,
            blame,
            hash,
            author,
            email,
            subject,
            body,
            patch,
            path,
            from,
            to,
            time,
            note,
        } = self;
        let commit_items: Vec<VdVcsHistoryEntry> = commits
            .into_iter()
            .map(|entry| VdVcsHistoryEntry {
                hash: c_string(&entry.hash),
                short_hash: c_string(&entry.short_hash),
                author: c_string(&entry.author),
                email: c_string(&entry.email),
                time: entry.time,
                subject: c_string(&entry.subject),
            })
            .collect();
        let (commit_count, commits) = if commit_items.is_empty() {
            (0, std::ptr::null_mut())
        } else {
            (
                commit_items.len(),
                Box::into_raw(commit_items.into_boxed_slice()) as *mut VdVcsHistoryEntry,
            )
        };
        let file_items: Vec<VdVcsFileStat> = files
            .into_iter()
            .map(|entry| VdVcsFileStat {
                path: c_string(&entry.path),
                additions: entry.additions,
                deletions: entry.deletions,
            })
            .collect();
        let (file_count, files) = if file_items.is_empty() {
            (0, std::ptr::null_mut())
        } else {
            (
                file_items.len(),
                Box::into_raw(file_items.into_boxed_slice()) as *mut VdVcsFileStat,
            )
        };
        let blame_items: Vec<VdVcsBlameLine> = blame
            .into_iter()
            .map(|entry| VdVcsBlameLine {
                line: entry.line as u64,
                hash: c_string(&entry.hash),
                short_hash: c_string(&entry.short_hash),
                author: c_string(&entry.author),
                time: entry.time,
                text: c_string(&entry.text),
            })
            .collect();
        let (blame_count, blame) = if blame_items.is_empty() {
            (0, std::ptr::null_mut())
        } else {
            (
                blame_items.len(),
                Box::into_raw(blame_items.into_boxed_slice()) as *mut VdVcsBlameLine,
            )
        };
        VdVcsHistoryResult {
            backend,
            kind,
            commit_count,
            commits,
            file_count,
            files,
            blame_count,
            blame,
            offset: offset as u64,
            has_more: i32::from(has_more),
            truncated: i32::from(truncated),
            error: i32::from(error),
            hash: owned(hash.as_deref()),
            author: owned(author.as_deref()),
            email: owned(email.as_deref()),
            subject: owned(subject.as_deref()),
            body: owned(body.as_deref()),
            patch: owned(patch.as_deref()),
            path: owned(path.as_deref()),
            from: owned(from.as_deref()),
            to: owned(to.as_deref()),
            time,
            note: owned(note.as_deref()),
        }
    }
}

/// Convert a backend enum into the C integer.
fn backend_i32(backend: vd_vcs::Backend) -> i32 {
    backend.as_i32()
}

/// One commit page.
fn history_commits_data(root: &Path, offset: usize, limit: usize, query: &str) -> VcsHistoryData {
    let page = vd_vcs::history::commits(
        root,
        vd_vcs::history::PageRequest::new(offset, limit),
        query,
    );
    VcsHistoryData {
        kind: VD_VCS_HISTORY_COMMITS,
        backend: backend_i32(page.backend),
        offset: page.offset,
        has_more: page.has_more,
        truncated: false,
        error: page.error,
        commits: page
            .commits
            .into_iter()
            .map(|entry| VcsHistoryEntryData {
                hash: entry.hash,
                short_hash: entry.short_hash,
                author: entry.author,
                email: entry.email,
                time: entry.time,
                subject: entry.subject,
            })
            .collect(),
        files: Vec::new(),
        blame: Vec::new(),
        hash: None,
        author: None,
        email: None,
        subject: None,
        body: None,
        patch: None,
        path: None,
        from: None,
        to: None,
        time: 0,
        note: page.note,
    }
}

/// One commit's detail.
fn history_commit_data(root: &Path, hash: &str) -> VcsHistoryData {
    let detail = vd_vcs::history::commit_detail(root, hash);
    VcsHistoryData {
        kind: VD_VCS_HISTORY_COMMIT,
        backend: backend_i32(detail.backend),
        offset: 0,
        has_more: false,
        truncated: false,
        error: detail.error,
        commits: Vec::new(),
        files: detail
            .files
            .into_iter()
            .map(|file| VcsFileStatData {
                path: file.path,
                additions: file.additions,
                deletions: file.deletions,
            })
            .collect(),
        blame: Vec::new(),
        hash: Some(detail.hash),
        author: Some(detail.author),
        email: Some(detail.email),
        subject: Some(detail.subject),
        body: Some(detail.body),
        patch: Some(detail.patch),
        path: None,
        from: None,
        to: None,
        time: detail.time,
        note: detail.note,
    }
}

/// One page of a single file's history.
fn history_file_data(root: &Path, path: &str, offset: usize, limit: usize) -> VcsHistoryData {
    let page =
        vd_vcs::history::file_history(root, path, vd_vcs::history::PageRequest::new(offset, limit));
    VcsHistoryData {
        kind: VD_VCS_HISTORY_FILE,
        backend: backend_i32(page.backend),
        offset: page.offset,
        has_more: page.has_more,
        truncated: false,
        error: page.error,
        commits: page
            .commits
            .into_iter()
            .map(|entry| VcsHistoryEntryData {
                hash: entry.hash,
                short_hash: entry.short_hash,
                author: entry.author,
                email: entry.email,
                time: entry.time,
                subject: entry.subject,
            })
            .collect(),
        files: Vec::new(),
        blame: Vec::new(),
        hash: None,
        author: None,
        email: None,
        subject: None,
        body: None,
        patch: None,
        path: Some(page.path),
        from: None,
        to: None,
        time: 0,
        note: page.note,
    }
}

/// One file's blame.
fn history_blame_data(root: &Path, path: &str) -> VcsHistoryData {
    let blamed = vd_vcs::history::blame(root, path);
    VcsHistoryData {
        kind: VD_VCS_HISTORY_BLAME,
        backend: backend_i32(blamed.backend),
        offset: 0,
        has_more: false,
        truncated: blamed.truncated,
        error: blamed.error,
        commits: Vec::new(),
        files: Vec::new(),
        blame: blamed
            .lines
            .into_iter()
            .map(|line| VcsBlameLineData {
                line: line.line,
                hash: line.hash,
                short_hash: line.short_hash,
                author: line.author,
                time: line.time,
                text: line.text,
            })
            .collect(),
        hash: None,
        author: None,
        email: None,
        subject: None,
        body: None,
        patch: None,
        path: Some(blamed.path),
        from: None,
        to: None,
        time: 0,
        note: blamed.note,
    }
}

/// A two-commit comparison.
fn history_compare_data(root: &Path, from: &str, to: &str) -> VcsHistoryData {
    let comparison = vd_vcs::history::compare(root, from, to);
    VcsHistoryData {
        kind: VD_VCS_HISTORY_COMPARE,
        backend: backend_i32(comparison.backend),
        offset: 0,
        has_more: false,
        truncated: false,
        error: comparison.error,
        commits: Vec::new(),
        files: comparison
            .files
            .into_iter()
            .map(|file| VcsFileStatData {
                path: file.path,
                additions: file.additions,
                deletions: file.deletions,
            })
            .collect(),
        blame: Vec::new(),
        hash: None,
        author: None,
        email: None,
        subject: None,
        body: None,
        patch: Some(comparison.patch),
        path: None,
        from: Some(comparison.from),
        to: Some(comparison.to),
        time: 0,
        note: comparison.note,
    }
}

fn info_data(root: &Path) -> VcsInfoPayload {
    let service = vd_vcs::service::ProcessVcsService::production();
    let info = vd_vcs::service::VcsService::info(&service, root);
    // Batch 4a: the refs / remotes / upstream listing rides the same job, so the
    // panel refreshes the whole refs column with one asynchronous read.
    let listing = vd_vcs::refs(root);
    let mut note = info.note;
    if note.is_none() {
        note = listing.note;
    }
    if listing.truncated {
        let message = format!(
            "the ref listing was truncated at {} entries",
            vd_vcs::MAX_REFS
        );
        note = Some(match note {
            Some(existing) => format!("{existing}; {message}"),
            None => message,
        });
    }
    // Batch 5a: the same read reports whether a merge/rebase/cherry-pick/revert
    // is in progress (the conflict FILES come from the status job, kind 5).
    let operation = vd_vcs::operation_state(root)
        .operation
        .map_or(0, vd_vcs::OperationKind::as_i32);
    VcsInfoPayload {
        backend: info.backend.as_i32(),
        branch: info.branch,
        revision: info.revision,
        note,
        refs: listing
            .refs
            .into_iter()
            .map(|entry| VcsRefData {
                name: entry.name,
                kind: entry.kind.as_i32(),
                target: entry.target,
                current: entry.current,
            })
            .collect(),
        remotes: listing
            .remotes
            .into_iter()
            .map(|entry| VcsRemoteData {
                name: entry.name,
                url: entry.url,
            })
            .collect(),
        upstream: listing.upstream,
        operation,
    }
}

fn diff_data(root: &Path, path: &Path) -> VcsDiffData {
    let service = vd_vcs::service::ProcessVcsService::production();
    let diff = vd_vcs::service::VcsService::file_diff(&service, root, &path.display().to_string());
    VcsDiffData {
        path: diff.path,
        text: diff.text,
        binary: diff.binary,
        note: diff.note,
    }
}

/// Starts a branch + revision job for `root`.
///
/// Returns the job id, or `0` when the arguments were rejected. The panel polls
/// with [`vd_vcs_info_poll`], so the GUI thread never waits for `git`/`svn`.
///
/// # Safety
///
/// `service` must be null or live; `root` must be null or a valid NUL-terminated
/// string.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_info_job(service: *mut VdService, root: *const c_char) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(root) = crate::capi::c_str(root) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root must be a valid UTF-8 path",
        ));
        return 0;
    };
    let root = PathBuf::from(root);
    state.start(move || Ok(JobValue::VcsInfo(info_data(&root))))
}

/// Polls a branch + revision job started by [`vd_vcs_info_job`].
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdVcsInfo`].
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_info_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdVcsInfo,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    match state.poll(job) {
        Polled::Unknown => unknown_job(state),
        Polled::Pending => VD_STATUS_PENDING,
        Polled::Failed => VD_STATUS_FAILED,
        Polled::Ready(JobValue::VcsInfo(data)) => {
            *out = data.into_c();
            VD_STATUS_READY
        }
        Polled::Ready(_) => {
            state.record_error(VdError::new(
                crate::capi::VD_ERR_WRONG_JOB_KIND,
                "this job id belongs to a different service",
            ));
            POLL_MISUSE
        }
    }
}

/// Releases the strings of a [`VdVcsInfo`] (the struct itself is by value).
///
/// # Safety
///
/// `info` must be null or a struct returned by `vd_vcs_info_poll` that was not freed.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_info_free(info: *mut VdVcsInfo) {
    let Some(info) = (if info.is_null() {
        None
    } else {
        Some(&mut *info)
    }) else {
        return;
    };
    for field in [&mut info.branch, &mut info.revision, &mut info.note] {
        if !field.is_null() {
            free_string(*field);
            *field = std::ptr::null_mut();
        }
    }
    if !info.refs.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(info.refs, info.ref_count);
        for entry in slice.iter_mut() {
            free_string(entry.name);
            free_string(entry.target);
            entry.name = std::ptr::null_mut();
            entry.target = std::ptr::null_mut();
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            info.refs,
            info.ref_count,
        )));
    }
    if !info.remotes.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(info.remotes, info.remote_count);
        for entry in slice.iter_mut() {
            free_string(entry.name);
            free_string(entry.url);
            entry.name = std::ptr::null_mut();
            entry.url = std::ptr::null_mut();
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            info.remotes,
            info.remote_count,
        )));
    }
    free_string(info.upstream);
    info.ref_count = 0;
    info.refs = std::ptr::null_mut();
    info.remote_count = 0;
    info.remotes = std::ptr::null_mut();
    info.upstream = std::ptr::null_mut();
}

/// Starts a unified-diff job for one file of the working tree containing `root`.
///
/// Returns the job id, or `0` when the arguments were rejected. The panel polls
/// with [`vd_vcs_diff_poll`], so the GUI thread never waits for `git diff`.
///
/// # Safety
///
/// `service` must be null or live; `root` and `path` must be null or valid
/// NUL-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_diff_job(
    service: *mut VdService,
    root: *const c_char,
    path: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let (root, path) = (crate::capi::c_str(root), crate::capi::c_str(path));
    let (Some(root), Some(path)) = (root, path) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root and path must be valid UTF-8 paths",
        ));
        return 0;
    };
    let (root, path) = (PathBuf::from(root), PathBuf::from(path));
    state.start(move || Ok(JobValue::VcsDiff(diff_data(&root, &path))))
}

/// Polls a diff job started by [`vd_vcs_diff_job`].
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdVcsDiff`].
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_diff_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdVcsDiff,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    match state.poll(job) {
        Polled::Unknown => unknown_job(state),
        Polled::Pending => VD_STATUS_PENDING,
        Polled::Failed => VD_STATUS_FAILED,
        Polled::Ready(JobValue::VcsDiff(data)) => {
            *out = data.into_c();
            VD_STATUS_READY
        }
        Polled::Ready(_) => {
            state.record_error(VdError::new(
                crate::capi::VD_ERR_WRONG_JOB_KIND,
                "this job id belongs to a different service",
            ));
            POLL_MISUSE
        }
    }
}

/// Releases the strings of a [`VdVcsDiff`].
///
/// # Safety
///
/// `diff` must be null or a struct returned by `vd_vcs_diff_poll` that was not freed.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_diff_free(diff: *mut VdVcsDiff) {
    let Some(diff) = (if diff.is_null() {
        None
    } else {
        Some(&mut *diff)
    }) else {
        return;
    };
    for field in [&mut diff.path, &mut diff.text, &mut diff.note] {
        if !field.is_null() {
            free_string(*field);
            *field = std::ptr::null_mut();
        }
    }
}

// --- history jobs (batch 4b, AC-4.3) ---------------------------------------
//
// Five start functions over the same registry. The caller never supplies an
// argv: the typed arguments (root, page bounds, a validated query / hash / path /
// ref pair) become the invocation inside `vd-vcs`, so a value cannot turn into an
// option, and the read-only gate runs before any process exists.

/// Starts a paged commit-list job for `root`.
///
/// Returns the job id, or `0` when the arguments were rejected. Poll with
/// [`vd_vcs_history_poll`], so the GUI thread never waits for `git log`.
///
/// # Safety
///
/// `service` must be null or live; `root` and `query` must be null or valid
/// NUL-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_history_commits_job(
    service: *mut VdService,
    root: *const c_char,
    offset: u64,
    limit: u64,
    query: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(root) = crate::capi::c_str(root) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root must be a valid UTF-8 path",
        ));
        return 0;
    };
    // `c_str` borrows the caller's buffer: copy before it crosses into the job.
    let query = crate::capi::c_str(query).unwrap_or_default().to_string();
    let offset = usize::try_from(offset).unwrap_or(usize::MAX);
    let limit = usize::try_from(limit).unwrap_or(usize::MAX);
    let root = PathBuf::from(root);
    state.start(move || {
        Ok(JobValue::VcsHistoryCommits(Box::new(history_commits_data(
            &root, offset, limit, &query,
        ))))
    })
}

/// Starts a commit-detail job (meta + files + patch) for `hash`.
///
/// # Safety
///
/// `service` must be null or live; `root` and `hash` must be null or valid
/// NUL-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_history_commit_job(
    service: *mut VdService,
    root: *const c_char,
    hash: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let (Some(root), Some(hash)) = (crate::capi::c_str(root), crate::capi::c_str(hash)) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root and hash must be valid UTF-8 strings",
        ));
        return 0;
    };
    let root = PathBuf::from(root);
    let hash = hash.to_string();
    state.start(move || {
        Ok(JobValue::VcsHistoryCommit(Box::new(history_commit_data(
            &root, &hash,
        ))))
    })
}

/// Starts a paged single-file history job for `path`.
///
/// # Safety
///
/// `service` must be null or live; `root` and `path` must be null or valid
/// NUL-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_history_file_job(
    service: *mut VdService,
    root: *const c_char,
    path: *const c_char,
    offset: u64,
    limit: u64,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let (Some(root), Some(path)) = (crate::capi::c_str(root), crate::capi::c_str(path)) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root and path must be valid UTF-8 strings",
        ));
        return 0;
    };
    let offset = usize::try_from(offset).unwrap_or(usize::MAX);
    let limit = usize::try_from(limit).unwrap_or(usize::MAX);
    let root = PathBuf::from(root);
    let path = path.to_string();
    state.start(move || {
        Ok(JobValue::VcsHistoryFile(Box::new(history_file_data(
            &root, &path, offset, limit,
        ))))
    })
}

/// Starts a blame job for `path`.
///
/// # Safety
///
/// `service` must be null or live; `root` and `path` must be null or valid
/// NUL-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_history_blame_job(
    service: *mut VdService,
    root: *const c_char,
    path: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let (Some(root), Some(path)) = (crate::capi::c_str(root), crate::capi::c_str(path)) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root and path must be valid UTF-8 strings",
        ));
        return 0;
    };
    let root = PathBuf::from(root);
    let path = path.to_string();
    state.start(move || {
        Ok(JobValue::VcsHistoryBlame(Box::new(history_blame_data(
            &root, &path,
        ))))
    })
}

/// Starts a two-commit comparison job (`git diff <from> <to>`).
///
/// # Safety
///
/// `service` must be null or live; `root`, `from` and `to` must be null or valid
/// NUL-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_history_compare_job(
    service: *mut VdService,
    root: *const c_char,
    from: *const c_char,
    to: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let (Some(root), Some(from), Some(to)) = (
        crate::capi::c_str(root),
        crate::capi::c_str(from),
        crate::capi::c_str(to),
    ) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root, from and to must be valid UTF-8 strings",
        ));
        return 0;
    };
    let root = PathBuf::from(root);
    let (from, to) = (from.to_string(), to.to_string());
    state.start(move || {
        Ok(JobValue::VcsHistoryCompare(Box::new(history_compare_data(
            &root, &from, &to,
        ))))
    })
}

/// The history payload of a finished job, when the job IS a history read.
///
/// Batch 7: the five history reads have their own typed `JobValue` variants;
/// this helper is the ONE place `vd_vcs_history_poll` may accept a payload, so a
/// job of another kind (identity, workspace, status, diff, write) is rejected
/// with `WRONG_JOB_KIND` instead of being reinterpreted through a shared slot.
fn history_payload(value: JobValue) -> Option<Box<VcsHistoryData>> {
    match value {
        JobValue::VcsHistoryCommits(data)
        | JobValue::VcsHistoryCommit(data)
        | JobValue::VcsHistoryFile(data)
        | JobValue::VcsHistoryBlame(data)
        | JobValue::VcsHistoryCompare(data) => Some(data),
        _ => None,
    }
}

/// Polls any history job started by the five `vd_vcs_history_*_job` functions.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdVcsHistoryResult`].
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_history_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdVcsHistoryResult,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    match state.poll(job) {
        Polled::Unknown => unknown_job(state),
        Polled::Pending => VD_STATUS_PENDING,
        Polled::Failed => VD_STATUS_FAILED,
        Polled::Ready(value) => match history_payload(value) {
            Some(data) => {
                *out = (*data).into_c();
                VD_STATUS_READY
            }
            None => {
                state.record_error(VdError::new(
                    crate::capi::VD_ERR_WRONG_JOB_KIND,
                    "this job id does not carry a history result",
                ));
                POLL_MISUSE
            }
        },
    }
}

/// Releases the arrays and strings of a [`VdVcsHistoryResult`].
///
/// # Safety
///
/// `result` must be null or a struct returned by [`vd_vcs_history_poll`] that was
/// not freed.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_history_free(result: *mut VdVcsHistoryResult) {
    let Some(result) = (if result.is_null() {
        None
    } else {
        Some(&mut *result)
    }) else {
        return;
    };
    if !result.commits.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(result.commits, result.commit_count);
        for entry in slice.iter_mut() {
            for field in [
                &mut entry.hash,
                &mut entry.short_hash,
                &mut entry.author,
                &mut entry.email,
                &mut entry.subject,
            ] {
                free_string(*field);
                *field = std::ptr::null_mut();
            }
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            result.commits,
            result.commit_count,
        )));
    }
    if !result.files.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(result.files, result.file_count);
        for entry in slice.iter_mut() {
            free_string(entry.path);
            entry.path = std::ptr::null_mut();
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            result.files,
            result.file_count,
        )));
    }
    if !result.blame.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(result.blame, result.blame_count);
        for entry in slice.iter_mut() {
            for field in [
                &mut entry.hash,
                &mut entry.short_hash,
                &mut entry.author,
                &mut entry.text,
            ] {
                free_string(*field);
                *field = std::ptr::null_mut();
            }
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            result.blame,
            result.blame_count,
        )));
    }
    for field in [
        &mut result.hash,
        &mut result.author,
        &mut result.email,
        &mut result.subject,
        &mut result.body,
        &mut result.patch,
        &mut result.path,
        &mut result.from,
        &mut result.to,
        &mut result.note,
    ] {
        free_string(*field);
        *field = std::ptr::null_mut();
    }
    result.commit_count = 0;
    result.commits = std::ptr::null_mut();
    result.file_count = 0;
    result.files = std::ptr::null_mut();
    result.blame_count = 0;
    result.blame = std::ptr::null_mut();
}

// --- workspace listings (batch 5b, AC-5.2) ---------------------------------
//
// Two asynchronous reads over the same job registry: the stash stack and the
// linked worktrees. They share ONE C result struct whose `kind` selects the
// payload and ride their own TYPED `JobValue::VcsWorkspace` payload (batch 7:
// the former `VcsInfoData` slot workaround is gone, so `VcsInfo` means exactly
// the branch/refs identity read).

/// One stash / worktree row, owned by the job until it is converted to C.
pub(crate) struct VcsWorkspaceEntryData {
    pub(crate) ref_name: String,
    pub(crate) hash: String,
    pub(crate) subject: String,
    pub(crate) path: String,
    pub(crate) branch: String,
    pub(crate) main_worktree: bool,
    pub(crate) detached: bool,
    pub(crate) bare: bool,
}

/// One workspace listing, owned by the job until it is converted to C.
pub(crate) struct VcsWorkspaceData {
    pub(crate) kind: i32,
    pub(crate) backend: i32,
    pub(crate) entries: Vec<VcsWorkspaceEntryData>,
    pub(crate) truncated: bool,
    pub(crate) error: bool,
    pub(crate) note: Option<String>,
}

impl VcsWorkspaceData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with `vd_vcs_workspace_free`.
    pub(crate) unsafe fn into_c(self) -> VdVcsWorkspaceResult {
        let items: Vec<VdVcsWorkspaceEntry> = self
            .entries
            .into_iter()
            .map(|entry| VdVcsWorkspaceEntry {
                ref_name: c_string(&entry.ref_name),
                hash: c_string(&entry.hash),
                subject: c_string(&entry.subject),
                path: c_string(&entry.path),
                branch: c_string(&entry.branch),
                main_worktree: i32::from(entry.main_worktree),
                detached: i32::from(entry.detached),
                bare: i32::from(entry.bare),
            })
            .collect();
        let (count, entries) = if items.is_empty() {
            (0, std::ptr::null_mut())
        } else {
            (
                items.len(),
                Box::into_raw(items.into_boxed_slice()) as *mut VdVcsWorkspaceEntry,
            )
        };
        VdVcsWorkspaceResult {
            backend: self.backend,
            kind: self.kind,
            count,
            entries,
            truncated: i32::from(self.truncated),
            error: i32::from(self.error),
            note: owned(self.note.as_deref()),
        }
    }
}

/// The stash stack of `root`.
fn workspace_stashes_data(root: &Path) -> VcsWorkspaceData {
    let listing = vd_vcs::workspace::stashes(root);
    workspace_data(VD_VCS_WORKSPACE_STASHES, listing)
}

/// The linked worktrees of `root`.
fn workspace_worktrees_data(root: &Path) -> VcsWorkspaceData {
    let listing = vd_vcs::workspace::worktrees(root);
    workspace_data(VD_VCS_WORKSPACE_WORKTREES, listing)
}

fn workspace_data(kind: i32, listing: vd_vcs::workspace::WorkspaceListing) -> VcsWorkspaceData {
    VcsWorkspaceData {
        kind,
        backend: backend_i32(listing.backend),
        entries: listing
            .entries
            .into_iter()
            .map(|entry| VcsWorkspaceEntryData {
                ref_name: entry.ref_name,
                hash: entry.hash,
                subject: entry.subject,
                path: entry.path,
                branch: entry.branch,
                main_worktree: entry.main_worktree,
                detached: entry.detached,
                bare: entry.bare,
            })
            .collect(),
        truncated: listing.truncated,
        error: listing.error,
        note: listing.note,
    }
}

/// Starts a stash-list job for `root`.
///
/// Returns the job id, or `0` when the argument was rejected. Poll with
/// [`vd_vcs_workspace_poll`], so the GUI thread never waits for `git stash list`.
///
/// # Safety
///
/// `service` must be null or live; `root` must be null or a valid NUL-terminated
/// UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_workspace_stashes_job(
    service: *mut VdService,
    root: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(root) = crate::capi::c_str(root) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root must be a valid UTF-8 path",
        ));
        return 0;
    };
    // `c_str` borrows the caller's buffer: copy before it crosses into the job.
    let root = PathBuf::from(root);
    state.start(move || {
        Ok(JobValue::VcsWorkspace(Box::new(workspace_stashes_data(
            &root,
        ))))
    })
}

/// Starts a worktree-list job for `root`.
///
/// Returns the job id, or `0` when the argument was rejected. Poll with
/// [`vd_vcs_workspace_poll`].
///
/// # Safety
///
/// `service` must be null or live; `root` must be null or a valid NUL-terminated
/// UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_workspace_worktrees_job(
    service: *mut VdService,
    root: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(root) = crate::capi::c_str(root) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root must be a valid UTF-8 path",
        ));
        return 0;
    };
    let root = PathBuf::from(root);
    state.start(move || {
        Ok(JobValue::VcsWorkspace(Box::new(workspace_worktrees_data(
            &root,
        ))))
    })
}

/// Polls any workspace job started by the two `vd_vcs_workspace_*_job`
/// functions.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdVcsWorkspaceResult`].
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_workspace_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdVcsWorkspaceResult,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    match state.poll(job) {
        Polled::Unknown => unknown_job(state),
        Polled::Pending => VD_STATUS_PENDING,
        Polled::Failed => VD_STATUS_FAILED,
        Polled::Ready(JobValue::VcsWorkspace(data)) => {
            *out = (*data).into_c();
            VD_STATUS_READY
        }
        Polled::Ready(_) => {
            state.record_error(VdError::new(
                crate::capi::VD_ERR_WRONG_JOB_KIND,
                "this job id does not carry a workspace result",
            ));
            POLL_MISUSE
        }
    }
}

/// Releases the array and strings of a [`VdVcsWorkspaceResult`].
///
/// # Safety
///
/// `result` must be null or a struct returned by [`vd_vcs_workspace_poll`] that
/// was not freed.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_workspace_free(result: *mut VdVcsWorkspaceResult) {
    let Some(result) = (if result.is_null() {
        None
    } else {
        Some(&mut *result)
    }) else {
        return;
    };
    if !result.entries.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(result.entries, result.count);
        for entry in slice.iter_mut() {
            for field in [
                &mut entry.ref_name,
                &mut entry.hash,
                &mut entry.subject,
                &mut entry.path,
                &mut entry.branch,
            ] {
                free_string(*field);
                *field = std::ptr::null_mut();
            }
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            result.entries,
            result.count,
        )));
        result.entries = std::ptr::null_mut();
        result.count = 0;
    }
    free_string(result.note);
    result.note = std::ptr::null_mut();
}

// --- write jobs (batch 3a, AC-3.1 / AC-3.2) --------------------------------
//
// The read-only jobs above are unchanged. Writes are a SEPARATE trio
// (`vd_vcs_write_job` / `vd_vcs_write_poll` / `vd_vcs_write_free`) over the same
// registry, so the GUI thread only starts a job and polls it. The operation code
// and its parameters are validated by `vd_vcs::write`, which is an independent
// allow-list: the read table was NOT widened. The caller never supplies an
// argv — the operation code plus typed fields become the invocation inside
// `vd-vcs`, so a path cannot turn into an option.

/// `git add -- <paths>`: stage the listed files.
pub const VD_VCS_WRITE_GIT_STAGE: i32 = 1;
/// `git reset HEAD -- <paths>`: unstage the listed files.
pub const VD_VCS_WRITE_GIT_UNSTAGE: i32 = 2;
/// `git checkout -- <paths>`: discard changes (irreversible; confirm first).
pub const VD_VCS_WRITE_GIT_DISCARD: i32 = 3;
/// `git commit -m <message> [--author=<author>] [--amend] [-- <paths>]`.
pub const VD_VCS_WRITE_GIT_COMMIT: i32 = 4;
/// `git fetch [<remote>]`.
pub const VD_VCS_WRITE_GIT_FETCH: i32 = 5;
/// `git pull --ff-only [<remote> [<branch>]]`.
pub const VD_VCS_WRITE_GIT_PULL: i32 = 6;
/// `git push [--set-upstream] [<remote> [<branch>]]`.
pub const VD_VCS_WRITE_GIT_PUSH: i32 = 7;
/// `git branch <name> [<start>]`.
pub const VD_VCS_WRITE_GIT_BRANCH_CREATE: i32 = 8;
/// `git checkout <name>`: switch to an existing branch.
pub const VD_VCS_WRITE_GIT_BRANCH_SWITCH: i32 = 9;
/// `git branch -d <name>` (irreversible; confirm first).
pub const VD_VCS_WRITE_GIT_BRANCH_DELETE: i32 = 10;
/// `svn commit -m <message> [paths...]`.
pub const VD_VCS_WRITE_SVN_COMMIT: i32 = 11;
/// `svn update [paths...]`.
pub const VD_VCS_WRITE_SVN_UPDATE: i32 = 12;
/// `svn revert <paths...>` (irreversible).
pub const VD_VCS_WRITE_SVN_REVERT: i32 = 13;
/// `svn add <paths...>`.
pub const VD_VCS_WRITE_SVN_ADD: i32 = 14;
/// `svn remove <paths...>` (irreversible).
pub const VD_VCS_WRITE_SVN_REMOVE: i32 = 15;
/// `git branch -m <old> <new>`.
pub const VD_VCS_WRITE_GIT_BRANCH_RENAME: i32 = 16;
/// `git tag <name> [<target>]`.
pub const VD_VCS_WRITE_GIT_TAG_CREATE: i32 = 17;
/// `git tag -d <name>` (irreversible; confirm first).
pub const VD_VCS_WRITE_GIT_TAG_DELETE: i32 = 18;
/// `git checkout <ref>`: check out a branch, tag or commit (detached HEAD for
/// anything but a branch).
pub const VD_VCS_WRITE_GIT_CHECKOUT: i32 = 19;
/// `git remote add <name> <url>`.
pub const VD_VCS_WRITE_GIT_REMOTE_ADD: i32 = 20;
/// `git remote rm <name>` (irreversible; confirm first).
pub const VD_VCS_WRITE_GIT_REMOTE_REMOVE: i32 = 21;
/// `git remote set-url <name> <url>`.
pub const VD_VCS_WRITE_GIT_REMOTE_SET_URL: i32 = 22;
/// `git branch --set-upstream-to=<upstream> <branch>`.
pub const VD_VCS_WRITE_GIT_SET_UPSTREAM: i32 = 23;

/// `git merge --no-edit <target>` (batch 5a).
pub const VD_VCS_WRITE_GIT_MERGE: i32 = 24;
/// `git rebase <target> [<name>]`.
pub const VD_VCS_WRITE_GIT_REBASE: i32 = 25;
/// `git cherry-pick <target>`.
pub const VD_VCS_WRITE_GIT_CHERRY_PICK: i32 = 26;
/// `git revert --no-edit <target>`.
pub const VD_VCS_WRITE_GIT_REVERT: i32 = 27;
/// `git reset --<mode> <target>` (mode: soft / mixed / hard).
pub const VD_VCS_WRITE_GIT_RESET: i32 = 28;
/// `git clean -f -- <paths>` (untracked files only).
pub const VD_VCS_WRITE_GIT_CLEAN: i32 = 29;
/// `git <target> --continue` (target: merge / rebase / cherry-pick / revert).
pub const VD_VCS_WRITE_GIT_CONTINUE: i32 = 30;
/// `git <target> --abort` (target: merge / rebase / cherry-pick / revert).
pub const VD_VCS_WRITE_GIT_ABORT: i32 = 31;

// Batch 5b: the Git workspace operations.
/// `git stash save [--] [<message>]` (the message is positional: git 1.8.3.1
/// has no `stash save -m` and no `stash push`).
pub const VD_VCS_WRITE_GIT_STASH_SAVE: i32 = 32;
/// `git stash pop [<stash>]`.
pub const VD_VCS_WRITE_GIT_STASH_POP: i32 = 33;
/// `git stash apply [<stash>]`.
pub const VD_VCS_WRITE_GIT_STASH_APPLY: i32 = 34;
/// `git stash drop [<stash>]` (irreversible).
pub const VD_VCS_WRITE_GIT_STASH_DROP: i32 = 35;
/// `git worktree add <path> [<branch>]` (path in `target`, branch in `name`).
pub const VD_VCS_WRITE_GIT_WORKTREE_ADD: i32 = 36;
/// `git worktree remove <path>` (irreversible; never `--force`).
pub const VD_VCS_WRITE_GIT_WORKTREE_REMOVE: i32 = 37;
/// `git submodule init`.
pub const VD_VCS_WRITE_GIT_SUBMODULE_INIT: i32 = 38;
/// `git submodule update --init`.
pub const VD_VCS_WRITE_GIT_SUBMODULE_UPDATE: i32 = 39;

/// One write request (every pointer is borrowed only for the duration of
/// [`vd_vcs_write_job`]; the job owns its own copies).
#[repr(C)]
pub struct VdVcsWriteRequest {
    /// One of the `VD_VCS_WRITE_*` operation codes; anything else is refused.
    pub op: i32,
    /// Working tree root (the panel's project root; the repository root is found
    /// by walking up). Must be a valid UTF-8 path.
    pub root: *const c_char,
    /// Commit message (`GitCommit` / `SvnCommit` only), or NULL.
    pub message: *const c_char,
    /// Author override (`GitCommit` only), or NULL.
    pub author: *const c_char,
    /// Non-zero to amend the previous commit (`GitCommit` only).
    pub amend: i32,
    /// Number of entries in `paths`.
    pub count: usize,
    /// Repository-relative paths, or NULL when `count` is 0.
    pub paths: *const *const c_char,
    /// Branch / tag / remote name (create, switch, delete, rename source,
    /// remote add/rm/set-url, upstream local branch; for push/pull it is the
    /// branch), or NULL.
    pub name: *const c_char,
    /// Start point (branch/tag create), new name (rename), ref to check out, the
    /// remote (fetch/pull/push), or the upstream ref (set-upstream), or NULL.
    pub target: *const c_char,
    /// Remote URL (`remote add` / `remote set-url` only), or NULL.
    pub url: *const c_char,
    /// Remote username (fetch/pull/push only), or NULL. Held in memory only.
    pub credential_user: *const c_char,
    /// Remote password (fetch/pull/push only), or NULL. Held in memory only: it
    /// never enters the tool argv, is never written to disk, and is redacted out
    /// of the captured output.
    pub credential_password: *const c_char,
    /// Non-zero to push with `--set-upstream` (`GitPush` only).
    pub set_upstream: i32,
    /// Reset mode (`GitReset` only): `soft`, `mixed` or `hard`, or NULL.
    pub mode: *const c_char,
}

/// Result of one write job.
#[repr(C)]
pub struct VdVcsWriteResult {
    /// The operation that ran (one of the `VD_VCS_WRITE_*` codes).
    pub op: i32,
    /// 1 when the tool exited zero, 0 when it reported a failure.
    pub ok: i32,
    /// Captured stdout (may be empty), or NULL.
    pub stdout_text: *mut c_char,
    /// Captured stderr (the real reason when `ok` is 0), or NULL.
    pub stderr_text: *mut c_char,
    /// Why the request produced no tool run, or NULL.
    pub note: *mut c_char,
}

/// Result of one write job, owned until it is converted to C.
pub(crate) struct VcsWriteData {
    pub(crate) op: i32,
    pub(crate) ok: bool,
    pub(crate) stdout_text: String,
    pub(crate) stderr_text: String,
    pub(crate) note: Option<String>,
}

impl VcsWriteData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with [`vd_vcs_write_free`].
    pub(crate) unsafe fn into_c(self) -> VdVcsWriteResult {
        VdVcsWriteResult {
            op: self.op,
            ok: i32::from(self.ok),
            stdout_text: owned(Some(&self.stdout_text)),
            stderr_text: owned(Some(&self.stderr_text)),
            note: owned(self.note.as_deref()),
        }
    }
}

/// A rejected request or a tool that could not be started is a typed job error.
fn write_data(request: &vd_vcs::write::WriteRequest) -> Result<VcsWriteData, VdError> {
    match vd_vcs::write::write(request) {
        Ok(outcome) => Ok(VcsWriteData {
            op: outcome.op.code(),
            ok: outcome.success,
            stdout_text: outcome.stdout,
            stderr_text: outcome.stderr,
            note: None,
        }),
        Err(error) => {
            let code = if error.is_invalid() {
                VD_ERR_INVALID_ARGUMENT
            } else {
                VD_ERR_SERVICE
            };
            Err(VdError::new(code, error.message()))
        }
    }
}

/// Reads an optional C string; a non-null pointer must be valid UTF-8.
///
/// # Safety
///
/// `pointer` must be null or a valid NUL-terminated string.
unsafe fn optional_c_string(pointer: *const c_char, name: &str) -> Result<Option<String>, VdError> {
    if pointer.is_null() {
        return Ok(None);
    }
    match crate::capi::c_str(pointer) {
        Some(text) => Ok(Some(text.to_string())),
        None => Err(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            &format!("{name} must be a valid UTF-8 string"),
        )),
    }
}

/// Starts one write job.
///
/// Returns the job id, or `0` when the request itself was rejected (null
/// pointer, unknown operation code, or a non-UTF-8 string). A request that is
/// well-formed but violates the operation's contract (no repository, missing
/// message, unsafe path) starts a job that fails with
/// `VD_ERR_INVALID_ARGUMENT`, so no file-system work runs on the calling
/// thread.
///
/// # Safety
///
/// `service` must be null or live; `request` must be null or point to a
/// [`VdVcsWriteRequest`] whose pointers are valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_write_job(
    service: *mut VdService,
    request: *const VdVcsWriteRequest,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(raw) = (request).as_ref() else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "request must not be NULL",
        ));
        return 0;
    };
    let Some(op) = vd_vcs::write::WriteOp::from_code(raw.op) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            &format!(
                "operation code {} is not on the VCS write allow-list",
                raw.op
            ),
        ));
        return 0;
    };
    let Some(root) = crate::capi::c_str(raw.root) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root must be a valid UTF-8 path",
        ));
        return 0;
    };
    let (message, author) = match (
        optional_c_string(raw.message, "message"),
        optional_c_string(raw.author, "author"),
    ) {
        (Ok(message), Ok(author)) => (message, author),
        (Err(error), _) | (_, Err(error)) => {
            state.record_error(error);
            return 0;
        }
    };
    let (name, target, url, mode) = match (
        optional_c_string(raw.name, "name"),
        optional_c_string(raw.target, "target"),
        optional_c_string(raw.url, "url"),
        optional_c_string(raw.mode, "mode"),
    ) {
        (Ok(name), Ok(target), Ok(url), Ok(mode)) => (name, target, url, mode),
        (Err(error), _, _, _)
        | (_, Err(error), _, _)
        | (_, _, Err(error), _)
        | (_, _, _, Err(error)) => {
            state.record_error(error);
            return 0;
        }
    };
    let (credential_user, credential_password) = match (
        optional_c_string(raw.credential_user, "credential_user"),
        optional_c_string(raw.credential_password, "credential_password"),
    ) {
        (Ok(user), Ok(password)) => (user, password),
        (Err(error), _) | (_, Err(error)) => {
            state.record_error(error);
            return 0;
        }
    };
    let Some(paths) = crate::capi::c_str_array(raw.paths, raw.count) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "paths must be valid UTF-8 strings (and not NULL while count > 0)",
        ));
        return 0;
    };
    let request = vd_vcs::write::WriteRequest {
        root: PathBuf::from(root),
        op,
        paths,
        message,
        author,
        amend: raw.amend != 0,
        name,
        target,
        url,
        credential_user,
        credential_password,
        set_upstream: raw.set_upstream != 0,
        mode,
    };
    state.start(move || write_data(&request).map(JobValue::VcsWrite))
}

/// Polls a write job started by [`vd_vcs_write_job`].
///
/// `VD_STATUS_READY` with `out` filled means the tool really ran (read `ok` for
/// its exit status). A rejected request or a tool that could not be started
/// yields `VD_STATUS_FAILED` plus a typed error in `vd_service_error`.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdVcsWriteResult`].
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_write_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdVcsWriteResult,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    match state.poll(job) {
        Polled::Unknown => unknown_job(state),
        Polled::Pending => VD_STATUS_PENDING,
        Polled::Failed => VD_STATUS_FAILED,
        Polled::Ready(JobValue::VcsWrite(data)) => {
            *out = data.into_c();
            VD_STATUS_READY
        }
        Polled::Ready(_) => {
            state.record_error(VdError::new(
                crate::capi::VD_ERR_WRONG_JOB_KIND,
                "this job id belongs to a different service",
            ));
            POLL_MISUSE
        }
    }
}

/// Releases the strings of a [`VdVcsWriteResult`]; null-safe.
///
/// # Safety
///
/// `result` must be null or a struct returned by [`vd_vcs_write_poll`] that was
/// not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_write_free(result: *mut VdVcsWriteResult) {
    let Some(result) = (if result.is_null() {
        None
    } else {
        Some(&mut *result)
    }) else {
        return;
    };
    for field in [
        &mut result.stdout_text,
        &mut result.stderr_text,
        &mut result.note,
    ] {
        if !field.is_null() {
            free_string(*field);
            *field = std::ptr::null_mut();
        }
    }
    result.op = 0;
    result.ok = 0;
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::capi::VdError;
    use crate::service::{vd_service_create, vd_service_destroy, vd_service_error, POLL_MISUSE};
    use std::time::{Duration, Instant};

    fn temp_dir(tag: &str) -> Result<PathBuf, String> {
        let mut path = std::env::temp_dir();
        path.push(format!("vd-ffi-vcs-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&path);
        std::fs::create_dir_all(&path)
            .map_err(|error| format!("cannot create {}: {error}", path.display()))?;
        Ok(path)
    }

    fn c_path(path: &Path) -> Result<std::ffi::CString, String> {
        std::ffi::CString::new(path.display().to_string())
            .map_err(|error| format!("path contains a NUL: {error}"))
    }

    fn poll_info(service: *mut VdService, job: u64) -> (i32, VdVcsInfo) {
        let mut out = VdVcsInfo {
            backend: 0,
            branch: std::ptr::null_mut(),
            revision: std::ptr::null_mut(),
            note: std::ptr::null_mut(),
            ref_count: 0,
            refs: std::ptr::null_mut(),
            remote_count: 0,
            remotes: std::ptr::null_mut(),
            upstream: std::ptr::null_mut(),
            operation: 0,
        };
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
            let state = unsafe { vd_vcs_info_poll(service, job, &mut out) };
            if state != VD_STATUS_PENDING || Instant::now() >= deadline {
                return (state, out);
            }
            std::thread::sleep(Duration::from_millis(2));
        }
    }

    fn poll_diff(service: *mut VdService, job: u64) -> (i32, VdVcsDiff) {
        let mut out = VdVcsDiff {
            path: std::ptr::null_mut(),
            text: std::ptr::null_mut(),
            binary: 0,
            note: std::ptr::null_mut(),
        };
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
            let state = unsafe { vd_vcs_diff_poll(service, job, &mut out) };
            if state != VD_STATUS_PENDING || Instant::now() >= deadline {
                return (state, out);
            }
            std::thread::sleep(Duration::from_millis(2));
        }
    }

    #[test]
    fn info_job_reaches_ready_for_a_plain_directory() -> Result<(), String> {
        let dir = temp_dir("info")?;
        let root = c_path(&dir)?;
        let service = vd_service_create();
        let job = unsafe { vd_vcs_info_job(service, root.as_ptr()) };
        assert_ne!(job, 0, "the start call must return a job id");
        let (state, mut out) = poll_info(service, job);
        assert_eq!(state, VD_STATUS_READY, "the job must complete");
        assert_eq!(out.backend, 0, "a plain directory has no backend");
        assert!(!out.note.is_null(), "the empty state must carry a note");
        let note = unsafe { std::ffi::CStr::from_ptr(out.note) };
        assert!(!note.to_bytes().is_empty());
        unsafe { vd_vcs_info_free(&mut out) };
        unsafe { vd_service_destroy(service) };
        let _ = std::fs::remove_dir_all(&dir);
        Ok(())
    }

    #[test]
    fn diff_job_reaches_ready_for_a_plain_directory() -> Result<(), String> {
        let dir = temp_dir("diff")?;
        let root = c_path(&dir)?;
        let path = c_path(Path::new("rtl/top.sv"))?;
        let service = vd_service_create();
        let job = unsafe { vd_vcs_diff_job(service, root.as_ptr(), path.as_ptr()) };
        assert_ne!(job, 0);
        let (state, mut out) = poll_diff(service, job);
        assert_eq!(state, VD_STATUS_READY);
        assert!(
            out.text.is_null() || unsafe { *out.text } == 0,
            "no diff off a repository"
        );
        assert!(!out.note.is_null(), "the reason must be reported");
        unsafe { vd_vcs_diff_free(&mut out) };
        unsafe { vd_service_destroy(service) };
        let _ = std::fs::remove_dir_all(&dir);
        Ok(())
    }

    #[test]
    fn polling_a_job_with_the_wrong_kind_is_misuse() -> Result<(), String> {
        let dir = temp_dir("wrong-kind")?;
        let root = c_path(&dir)?;
        let service = vd_service_create();
        // Start a status job, then poll it through the info poll: the id belongs
        // to another service, so the call must be rejected (never silently read).
        let status_job = unsafe { vd_vcs_status(service, root.as_ptr()) };
        assert_ne!(status_job, 0);
        let mut out = VdVcsInfo {
            backend: 0,
            branch: std::ptr::null_mut(),
            revision: std::ptr::null_mut(),
            note: std::ptr::null_mut(),
            ref_count: 0,
            refs: std::ptr::null_mut(),
            remote_count: 0,
            remotes: std::ptr::null_mut(),
            upstream: std::ptr::null_mut(),
            operation: 0,
        };
        // Poll with the WRONG poller until the status job is terminal: reading a
        // status payload through the info poll must be refused, never returned.
        let deadline = Instant::now() + Duration::from_secs(10);
        let mut state = VD_STATUS_PENDING;
        while state == VD_STATUS_PENDING && Instant::now() < deadline {
            state = unsafe { vd_vcs_info_poll(service, status_job, &mut out) };
            std::thread::sleep(Duration::from_millis(2));
        }
        assert_eq!(state, POLL_MISUSE, "wrong-kind poll must be refused");
        let mut error = VdError {
            code: 0,
            message: [0; crate::capi::VD_ERROR_MESSAGE_CAPACITY],
        };
        assert_eq!(unsafe { vd_service_error(service, &mut error) }, 1);
        assert_eq!(error.code, crate::capi::VD_ERR_WRONG_JOB_KIND);
        unsafe { vd_vcs_info_free(&mut out) };
        unsafe { vd_service_destroy(service) };
        let _ = std::fs::remove_dir_all(&dir);
        Ok(())
    }

    #[test]
    fn invalid_arguments_return_no_job() {
        let service = vd_service_create();
        assert_eq!(unsafe { vd_vcs_info_job(service, std::ptr::null()) }, 0);
        assert_eq!(
            unsafe { vd_vcs_diff_job(service, std::ptr::null(), std::ptr::null()) },
            0
        );
        let mut error = VdError {
            code: 0,
            message: [0; crate::capi::VD_ERROR_MESSAGE_CAPACITY],
        };
        assert_eq!(unsafe { vd_service_error(service, &mut error) }, 1);
        assert_eq!(error.code, VD_ERR_INVALID_ARGUMENT);
        unsafe { vd_service_destroy(service) };
    }
}
