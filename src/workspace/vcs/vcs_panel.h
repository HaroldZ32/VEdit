#pragma once

// VCS panel (T23; batch 3b adds the SourceGit-style three-column baseline, the
// Git basic-write operations and automatic refresh).
//
// Layout (left to right):
//   * branches / tags column  — the refs the read seam reports;
//   * changes / staged column — the working-tree changes and this session's
//                               staged changes;
//   * diff column             — the unified diff of the selected path.
// Below the columns sits the operation bar (Refresh / Stage / Unstage /
// Discard… / Commit…). Reads and writes both go through VcsSource, which is
// asynchronous in production, so no git/svn subprocess runs on the GUI thread.
//
// The panel reports FOUR visible states: empty (not a working tree), loading (a
// status job is in flight), error (the backend failed) and ready. Every string
// the user can see goes through QCoreApplication::translate("VEdit", ...).
#include "vcs_capabilities.h"
#include "vcs_source.h"

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <memory>

class QCheckBox;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTimer;
class QTreeWidget;

namespace vedit::workspace::vcs {

// Commit entry dialog: message + optional author + amend flag (AC-3.3). The
// panel only calls it from the Commit… button; the probe drives the dialog's
// fields directly and the panel's commit() path without a modal loop.
class CommitDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CommitDialog(QWidget *parent = nullptr);

    QString message() const;
    QString author() const;
    bool amend() const;
    void setFields(const QString &message, const QString &author, bool amend);

private:
    QPlainTextEdit *message_ = nullptr;
    QLineEdit *author_ = nullptr;
    QCheckBox *amend_ = nullptr;
};

// Branch entry dialog (batch 4a): create (optional start point, optional switch)
// or rename. Probe-drivable like CommitDialog, so the operations themselves stay
// headless and the dialog is only the interactive shell.
class BranchDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Mode { Create, Rename };
    explicit BranchDialog(Mode mode, QWidget *parent = nullptr);

    QString name() const;
    QString startPoint() const;
    bool switchAfterCreate() const;
    void setFields(const QString &name, const QString &startPoint = QString(),
                   bool switchAfterCreate = false);

private:
    Mode mode_;
    QLineEdit *name_ = nullptr;
    QLineEdit *start_ = nullptr;
    QCheckBox *switch_ = nullptr;
};

// Tag entry dialog (batch 4a): create a tag at an optional start point.
class TagDialog final : public QDialog {
    Q_OBJECT

public:
    explicit TagDialog(QWidget *parent = nullptr);

    QString name() const;
    QString target() const;
    void setFields(const QString &name, const QString &target = QString());

private:
    QLineEdit *name_ = nullptr;
    QLineEdit *target_ = nullptr;
};

// Remote entry dialog (batch 4a): add a remote or change its URL. The URL is the
// only network location the panel ever asks for; credentials are NOT entered
// here (they never persist in configuration).
class RemoteDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Mode { Add, Edit };
    explicit RemoteDialog(Mode mode, QWidget *parent = nullptr);

    QString name() const;
    QString url() const;
    void setFields(const QString &name, const QString &url);

private:
    Mode mode_;
    QLineEdit *name_ = nullptr;
    QLineEdit *url_ = nullptr;
};

// Credential dialog (batch 4a, AC-4.5): shown only when a network operation
// failed because the transport could not obtain credentials. The values live in
// this dialog and in the in-flight request only: no file, no log, no evidence.
class CredentialDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CredentialDialog(const QString &prompt, QWidget *parent = nullptr);

    QString userName() const;
    QString password() const;
    void setFields(const QString &userName, const QString &password);

private:
    QLineEdit *user_ = nullptr;
    QLineEdit *password_ = nullptr;
};

// Workspace dialog (batch 5b): the stash stack, the linked worktrees and the
// submodule entry points in one place. Probe-drivable like the other dialogs:
// the panel fills the two lists, and a probe can set an action without a modal
// loop (`setAction`); the buttons set the same action and accept the dialog.
class WorkspaceDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Action {
        None,
        StashSave,
        StashPop,
        StashApply,
        StashDrop,
        WorktreeAdd,
        WorktreeRemove,
        SubmoduleInit,
        SubmoduleUpdate,
    };
    explicit WorkspaceDialog(QWidget *parent = nullptr);

    Action action() const { return action_; }
    QString stashMessage() const;
    QString worktreePath() const;
    QString worktreeBranch() const;
    QString selectedStash() const;
    QString selectedWorktree() const;

    void setFields(const QString &stashMessage, const QString &worktreePath,
                   const QString &worktreeBranch);
    void setStashes(const std::vector<WorkspaceEntry> &stashes);
    void setWorktrees(const std::vector<WorkspaceEntry> &worktrees);
    void setStatusText(const QString &text);
    // A probe sets the action directly; a click does the same through a button.
    void setAction(Action action) { action_ = action; }

private:
    void choose(Action action);

    QLineEdit *stash_message_ = nullptr;
    QLineEdit *worktree_path_ = nullptr;
    QLineEdit *worktree_branch_ = nullptr;
    QListWidget *stashes_ = nullptr;
    QListWidget *worktrees_ = nullptr;
    QLabel *status_ = nullptr;
    Action action_ = Action::None;
};

class VcsPanel final : public QWidget {
    Q_OBJECT

public:
    explicit VcsPanel(QWidget *parent = nullptr);
    ~VcsPanel() override;

    void setSource(std::unique_ptr<VcsSource> source);
    void setRoot(const std::string &root);
    std::string root() const { return root_; }

    // Queries the source and re-renders. Safe with no source (renders the empty
    // state) and after the repository disappeared.
    void refresh();
    // Selects one changed file and renders its diff.
    void selectFile(const std::string &path);

    // --- three-column query surface (probe readable) -----------------------
    Backend backend() const { return backend_; }
    int fileCount() const;   // every change row, staged or not
    int changeCount() const; // working-tree rows
    int stagedCount() const; // staged rows
    QString fileText(int row) const;
    QString stagedText(int row) const;
    QString headerText() const;
    QString emptyText() const;
    QString loadingText() const;
    QString errorText() const;
    QString messageText() const;
    QString diffText() const;
    QStringList refTexts() const;
    QStringList actionNames() const;
    bool emptyStateVisible() const;
    bool loadingStateVisible() const;
    bool errorStateVisible() const;

    // --- write surface (AC-3.3) --------------------------------------------
    // Replaces the modal question box, so the "cancel means zero writes" rule is
    // testable headless. Returning false cancels the operation.
    using ConfirmProvider = std::function<bool(const QString &title, const QString &text)>;
    void setConfirmProvider(ConfirmProvider provider);

    QString selectedPath() const { return selected_path_; }
    bool stageSelected();
    bool unstageSelected();
    bool discardSelected();
    bool commit(const QString &message, const QString &author, bool amend);

    bool writeInFlight() const { return write_in_flight_; }
    WriteOp lastWriteOp() const { return last_write_op_; }
    bool lastWriteOk() const { return last_write_ok_; }

    // --- remote synchronisation, refs and credentials (batch 4a) ------------
    // Every operation goes through the same asynchronous write seam as the file
    // operations, so none of them blocks the GUI thread. They return whether the
    // request was ISSUED (a stubbed source answers synchronously; the production
    // one answers from its poll timer).
    bool fetch();
    bool pull();
    bool push(); // the current branch; the first push of a branch tracks it
    bool pushBranch(const QString &remote, const QString &branch, bool setUpstream);

    bool createBranch(const QString &name, const QString &startPoint = QString(),
                      bool switchAfterCreate = false);
    bool switchBranch(const QString &name);
    bool deleteBranch(const QString &name); // confirms first
    bool renameBranch(const QString &from, const QString &to);

    bool createTag(const QString &name, const QString &target = QString());
    bool deleteTag(const QString &name); // confirms first
    bool checkoutRef(const QString &ref);

    bool addRemote(const QString &name, const QString &url);
    bool removeRemote(const QString &name); // confirms first
    bool setRemoteUrl(const QString &name, const QString &url);
    bool setUpstream(const QString &branch, const QString &upstream);

    // --- workspace: stash / worktree / submodule (batch 5b) -----------------
    // Same asynchronous write seam as every other operation. `stashDrop` and
    // `worktreeRemove` confirm TWICE (AC-5.3) and a decline at either step writes
    // nothing. `stashSave` takes an optional message (git's own default subject
    // is used when it is empty). `worktreeAdd`'s branch is an EXISTING branch
    // (empty = detached HEAD at the current commit).
    bool stashSave(const QString &message = QString());
    bool stashPop(const QString &stash = QString());
    bool stashApply(const QString &stash = QString());
    bool stashDrop(const QString &stash = QString()); // confirms first
    bool worktreeAdd(const QString &path, const QString &branch = QString());
    bool worktreeRemove(const QString &path); // confirms first
    bool submoduleInit();
    bool submoduleUpdate();

    // --- Subversion: the same action bar, per-backend capabilities (batch 6b) ---
    // `removeSelected()` is the Subversion-only action behind the Remove button
    // (Git records a deletion by staging the deleted file). It confirms twice.
    bool removeSelected();

    // The capability table the actions are gated by (AC-6.3 / AC-6.5). The
    // summary is what the panel renders; the report is the probe/evidence form.
    QString capabilitySummaryText() const;
    QString capabilityReportText() const;
    QStringList disabledActionsText() const;
    // Probe surface: the real label / enabled state / tooltip of one action
    // button addressed by its objectName (`vcs-action-stage`, ...).
    QString actionLabel(const QString &objectName) const;
    bool actionEnabled(const QString &objectName) const;
    QString actionToolTip(const QString &objectName) const;
    // The Staged column is a Git concept (Subversion has no index); it is hidden
    // for Subversion and the reason is in the capability summary.
    bool stagedColumnVisible() const;

    // The two listings are read asynchronously (both jobs on the source's poll
    // timer). `refreshWorkspace()` starts them; the snapshots and the per-row
    // text are the probe-readable surface.
    void refreshWorkspace();
    int stashCount() const;
    QString stashText(int row) const;
    int worktreeCount() const;
    QString worktreeText(int row) const;
    const WorkspaceData &stashes() const { return stashes_snapshot_; }
    const WorkspaceData &worktrees() const { return worktrees_snapshot_; }
    QString workspaceErrorText() const;
    QString workspaceStatusText() const;
    int workspaceRequestCount() const { return workspace_request_count_; }
    // Opens the Workspace… dialog (prefilled from the current snapshots) and
    // executes the chosen action. Public so the probe can drive the real dialog.
    void promptWorkspace();

    // The refs / remotes / upstream the last info read reported.
    const std::vector<RefEntry> &refs() const { return info_snapshot_.refs; }
    const std::vector<RemoteEntry> &remotes() const { return info_snapshot_.remotes; }
    QString upstream() const { return QString::fromStdString(info_snapshot_.upstream); }
    QString remoteUrl(const QString &name) const;
    int refRowCount() const;

    // --- refs tree selection (batch 4a repair, F3) -------------------------
    // The dialog entry points prefill their target from the ref selected in the
    // tree; these are probe-readable so that prefill is asserted, not assumed.
    bool selectRef(const QString &name);
    QString selectedRefName() const;
    RefKind selectedRefKind() const;
    // Opens the "Change remote URL" dialog prefilled for `name`; the context menu
    // calls this, the probe drives it with a QTimer (F3).
    bool editRemote(const QString &name);
    // The dialog entry points the action bar and the context menu call. Public so
    // the lane can drive the REAL modal dialogs with a QTimer (F3).
    void promptBranch();
    void promptTag();
    void promptRemote();
    // The dialogs a probe drove (F1/F3); a normal click path also counts them.
    int modalDialogCount() const { return modal_dialogs_; }

    // Credential seam. The provider replaces the modal dialog so the
    // "cancel aborts the operation" rule is testable headless; returning false
    // cancels. The strings it fills are used for the retry only.
    using CredentialProvider =
        std::function<bool(const QString &prompt, QString &userName, QString &password)>;
    void setCredentialProvider(CredentialProvider provider);
    int credentialPromptCount() const { return credential_prompt_count_; }
    QString lastCredentialPrompt() const { return last_credential_prompt_; }
    // True while a failed network operation is waiting for credentials.
    bool credentialPromptPending() const { return credential_prompt_pending_; }
    // F2: drive the REAL modal CredentialDialog under the offscreen platform. The
    // default keeps the headless refusal (inventing credentials is the one answer
    // that must never be assumed); the lane sets this and answers the dialog with
    // a QTimer, which is how the modal loop itself is under test.
    void setModalCredentialDialogForProbe(bool enabled) { modal_credential_dialog_ = enabled; }
    int modalCredentialDialogs() const { return modal_credential_dialogs_; }
    // True when no secret is left in the panel's in-flight request.
    bool credentialSecretCleared() const;

    // --- batch 5a: history operations and conflict resolution --------------
    bool merge(const QString &ref);
    bool rebase(const QString &onto);
    bool cherryPick(const QString &commit);
    bool revertCommit(const QString &commit);
    // `mode` is soft / mixed / hard; hard is double-confirmed.
    bool resetTo(const QString &commit, const QString &mode);
    // Removes the given untracked files (double-confirmed).
    bool cleanFiles(const QStringList &paths);
    // Continue/abort the operation in progress (merge/rebase/cherry-pick/revert).
    bool continueOperation();
    bool abortOperation();

    GitOperation operation() const { return info_snapshot_.operation; }
    QString conflictBannerText() const;
    bool continueAvailable() const;
    bool abortAvailable() const;
    // Probe-visible confirmation trail: the last text shown and how many times
    // the confirmation was asked (AC-5.3 double confirmation).
    QString lastConfirmText() const { return last_confirm_text_; }
    int confirmCalls() const { return confirm_calls_; }

    // --- automatic refresh (AC-3.5) ----------------------------------------
    // Triggers are coalesced on a trailing-edge timer of `coalesceWindowMs`
    // (1000 ms by default): any number of triggers inside one window produce ONE
    // refresh, so a save burst, a write and a window activation cannot storm the
    // backend. The Refresh button bypasses the coalescer (a user asked).
    static constexpr int kDefaultCoalesceMs = 1000;
    void scheduleRefresh();
    void scheduleRefresh(const QString &reason);
    void noteFileSaved(const QString &path);
    void noteActivated();

    int refreshCount() const { return refresh_count_; }
    int refreshRequestCount() const { return refresh_request_count_; }
    int mergedRefreshCount() const { return merged_refresh_count_; }
    QString lastRefreshReason() const { return last_refresh_reason_; }
    int coalesceWindowMs() const { return coalesce_ms_; }
    void setCoalesceWindowMs(int milliseconds);
    bool refreshPending() const;
    // Fires a pending coalesced refresh immediately (probe/teardown use).
    void flushRefreshForProbe();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void applyStatus(Status status);
    void applyInfo(Info info);
    void applyDiff(const std::string &path, Diff diff);
    void showEmptyState(const Status &status);
    void showErrorState(const Status &status);
    void renderChanges();
    void renderRefs(const Info &info);
    void watchPaths(const Status &status);
    void updateActions();
    void showDiff(const std::string &path);
    void manualRefresh();
    void onRefreshTimer();
    void promptCommit();
    void showRefContextMenu(const QPoint &position);
    // A second, stronger confirmation for irreversible operations; false when
    // either step is declined or cannot be asked (headless with no provider).
    bool confirmTwice(const QString &title, const QString &text, const QString &secondTitle,
                      const QString &secondText);
    void renderConflictBanner();
    bool issueWrite(const WriteRequest &request);
    bool issueWriteInternal(const WriteRequest &request, bool is_retry);
    void applyWriteResult(WriteResult result);
    void applyWorkspaceData(WorkspaceData data);
    void renderWorkspace();
    WorkspaceRequest workspaceRequest(WorkspaceKind kind) const;
    // Applies one capability row to one button: enabled = hasSource && writable
    // && idle && row.supported && `extra`; a disabled button carries the row's
    // real reason as its tooltip.
    void applyCapability(QPushButton *button, PanelAction action, bool extra);
    void rememberToolTip(QPushButton *button);
    bool askConfirm(const QString &title, const QString &text);
    // Asks for credentials after an authentication failure; false = cancelled.
    bool askCredentials(WriteOp op, const QString &prompt, QString &userName, QString &password);
    QString credentialPromptText(WriteOp op) const;
    QString sanitizeUrl(const QString &url) const;
    QString defaultRemote() const;
    void clearCredentials();

    std::unique_ptr<VcsSource> source_;
    std::string root_;
    Backend backend_ = Backend::None;
    std::vector<Change> changes_snapshot_;
    // The read ABI reports one change list (no index/worktree split), so the
    // staged column is an overlay the panel maintains from the write operations
    // it performed; see evidence/vcs-ui/README.md for that boundary.
    QSet<QString> staged_overlay_;
    QString selected_path_;

    QLabel *header_ = nullptr;
    QLabel *empty_ = nullptr;
    QLabel *loading_ = nullptr;
    QLabel *error_ = nullptr;
    QLabel *message_ = nullptr;
    QSplitter *splitter_ = nullptr;
    QTreeWidget *refs_ = nullptr;
    QListWidget *changes_ = nullptr;
    QListWidget *staged_ = nullptr;
    QPlainTextEdit *diff_ = nullptr;
    QPushButton *refresh_button_ = nullptr;
    QPushButton *stage_button_ = nullptr;
    QPushButton *unstage_button_ = nullptr;
    QPushButton *discard_button_ = nullptr;
    QPushButton *commit_button_ = nullptr;
    // Batch 4a action bars.
    QPushButton *fetch_button_ = nullptr;
    QPushButton *pull_button_ = nullptr;
    QPushButton *push_button_ = nullptr;
    QPushButton *branch_button_ = nullptr;
    QPushButton *tag_button_ = nullptr;
    QPushButton *remote_button_ = nullptr;
    // Batch 6b: the Subversion-only Remove action, the Staged-column title (hidden
    // for Subversion) and the capability summary line above the action bar.
    QPushButton *remove_button_ = nullptr;
    QLabel *staged_title_ = nullptr;
    QLabel *capability_line_ = nullptr;
    // The tooltip each action button was built with, so an enabled button shows
    // its normal hint again after a disabled state (reasons are not sticky).
    QHash<QString, QString> default_tooltips_;
    // Batch 5b: the workspace dialog entry point and the two listings.
    QPushButton *workspace_button_ = nullptr;
    WorkspaceData stashes_snapshot_;
    WorkspaceData worktrees_snapshot_;
    int workspace_request_count_ = 0;
    // Batch 5a: conflict banner + the two resolution entry points.
    QLabel *conflict_banner_ = nullptr;
    QPushButton *continue_button_ = nullptr;
    QPushButton *abort_button_ = nullptr;
    QString last_confirm_text_;
    int confirm_calls_ = 0;

    Info info_snapshot_;
    ConfirmProvider confirm_provider_;
    bool write_in_flight_ = false;
    WriteOp last_write_op_ = WriteOp::None;
    bool last_write_ok_ = false;
    WriteOp pending_op_ = WriteOp::None;
    QString pending_path_;
    // Batch 4a: the request as issued, kept so an authentication retry resends
    // the same operation with credentials added. Its credential fields are wiped
    // as soon as the operation finishes.
    WriteRequest pending_request_;
    bool retrying_with_credentials_ = false;
    bool credential_prompt_pending_ = false;
    // A second operation that only makes sense when the first one succeeded
    // (create a branch, then switch to it). Cleared on any failure.
    WriteRequest followup_request_;
    bool has_followup_ = false;
    CredentialProvider credential_provider_;
    int credential_prompt_count_ = 0;
    QString last_credential_prompt_;
    bool modal_credential_dialog_ = false;
    int modal_credential_dialogs_ = 0;
    int modal_dialogs_ = 0;

    QTimer *coalesce_ = nullptr;
    QFileSystemWatcher *watcher_ = nullptr;
    QPointer<QWidget> filtered_window_;
    int coalesce_ms_ = kDefaultCoalesceMs;
    int refresh_count_ = 0;
    int refresh_request_count_ = 0;
    int merged_refresh_count_ = 0;
    QString last_refresh_reason_;
    QString pending_reason_;
};

} // namespace vedit::workspace::vcs
