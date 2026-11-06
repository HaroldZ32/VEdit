# batch 5b — Git workspace management: stash / worktree / submodule (AC-5.2 / AC-5.3 / AC-5.4)

Contract: `.mpd/cellwork/cell-20261103T123639841Z/contracts/git-workspace-mgmt.md`.
Board task: `T21`. Verifier: `review-git-workspace`.

This directory is the evidence for that contract: the git-baseline probes, the
lane selftest, the xcb screenshot, the seven-gate run and the generated logs.
Every claim below quotes the check name the lane prints.

## What was built (and where)

| Layer | Path | What changed |
| --- | --- | --- |
| Write seam (Rust) | `crates/vd-vcs/src/write.rs` | 8 new operations (codes 32–39): `GitStashSave` (`git stash save [--] [<message>]`), `GitStashPop` / `GitStashApply` / `GitStashDrop` (`[<stash>]`), `GitWorktreeAdd` (`<path> [<branch>]`), `GitWorktreeRemove` (`<path>`, **never** `--force`), `GitSubmoduleInit`, `GitSubmoduleUpdate` (`--init`). The write allow-list gained `stash`, `worktree`, `submodule`. `stash drop` and `worktree remove` are marked irreversible (double confirmation). A worktree path is validated as a PATH (absolute / `..` legitimate, `-…` and control characters refused), a stash ref as a revision. |
| Workspace reads (Rust) | `crates/vd-vcs/src/workspace.rs` (new), `crates/vd-vcs/src/lib.rs` | `stashes()` (`git stash list --format=%gd%x09%H%x09%s`) and `worktrees()` (`git worktree list`) through the SAME read-only runner gate. The worktree parser locates the **abbreviated** commit id (git prints `215cde1`, not 40 hex) so a path containing spaces survives; the first row is the main worktree. A failed listing is `error = true` + git's real reason, never an empty list. |
| Read gate (Rust) | `crates/vd-vcs/src/runner.rs` | `stash` and `worktree` joined `GIT_READ_ONLY` as read/write SHARED tokens (like `remote`): the read gate allows exactly `stash list` / `worktree list` (options allowed after `list`, no positional), and refuses every write form and the bare token before any process exists. `submodule` stays write-only. |
| C ABI | `crates/vd-ffi/src/vcs.rs`, `crates/vd-ffi/include/vd_ffi.h` | Op codes 32–39; `VD_VCS_WORKSPACE_STASHES` / `VD_VCS_WORKSPACE_WORKTREES`; `VdVcsWorkspaceEntry` + `VdVcsWorkspaceResult`; two start functions (`vd_vcs_workspace_{stashes,worktrees}_job`) + ONE `vd_vcs_workspace_poll` / `vd_vcs_workspace_free`. **Superseded by batch 7 (T45)**: the listings now use their own typed `JobValue::VcsWorkspace` payload (and the history reads five typed `VcsHistory*` variants) in `crates/vd-ffi/src/service.rs`; the batch-5b `VcsInfoData::Workspace` slot workaround is gone. The header is cbindgen-generated. |
| Panel seam (C++) | `src/workspace/vcs/vcs_source.{h,cpp}` | `WorkspaceKind` / `WorkspaceEntry` / `WorkspaceData` / `WorkspaceRequest`, `workspaceCapable()/workspace()/requestWorkspace()`; `FfiVcsSource` runs the job on its QTimer (several may be in flight), `StubVcsSource` records every request and answers canned data. `WriteOp` gained codes 32–39; `writeOpIsIrreversible()` now covers stash drop and worktree remove. |
| Panel UI (C++) | `src/workspace/vcs/vcs_panel.{h,cpp}` | `stashSave/pop/apply/drop`, `worktreeAdd/remove`, `submoduleInit/Update`, `refreshWorkspace()`, the `Workspace…` action-bar button and the `WorkspaceDialog` (stash list + message, worktree list + path/branch, submodule buttons, status line). `stashDrop` and `worktreeRemove` use the existing `VcsPanel::confirmTwice`; a decline at EITHER step writes nothing. |
| Tests | `crates/vd-vcs/tests/workspace.rs` (new), `crates/vd-vcs/tests/{readonly,write}.rs`, `crates/vd-ffi/tests/abi.rs`, `src/workspace/vcs/vcs_selftest.cpp` | Real-git Rust E2E (13 tests), the shared-token read/write gate assertions, the C ABI round trip, and 89 new lane checks (stub exact requests, double confirmation, dialog, real E2E, i18n). |
| Translations | `translations/vedit_zh_CN.ts` | `lupdate` added 46 messages, all 46 translated (`tools/translations-5b.json` + `tools/merge-i18n-5b.py`), `lrelease` reports **880 finished and 0 unfinished** (834 at batch 5a + 46). |

## Baseline probes (the delivery image has git 1.8.3.1)

`logs/00-probe-git1831.log` (`tools/probe-git1831.sh`, run in `vedit-build:local`)
and `logs/01-probe-host-git253.log` / `logs/02-probe-focused-host253.log`
(`tools/probe-host-git.sh`, `tools/probe-focused.sh`, host git 2.53.0):

| Option | 1.8.3.1 | 2.53.0 | Decision |
| --- | --- | --- | --- |
| `git stash save <message>` | works | works | the message is POSITIONAL |
| `git stash save -m <message>` | **refused**: `unknown option for 'stash save': -m` | works | **never used** |
| `git stash save -- <message>` | works | works | used ONLY when the message starts with `-` |
| `git stash list --format=%gd%x09%H%x09%s` | works | works | the listing format |
| `git stash pop/apply/drop [<stash>]` | works | works | as documented |
| `git worktree list/add/remove` | **`git: 'worktree' is not a git command`** | works | implemented; on the baseline the read reports that REAL reason (`error = 1` + note) and the write reports git's exit message |
| `git submodule init` / `update --init` | works (local path) | works, but `file://` submodules are refused by default (CVE-2022-39253) | the FIXTURE opens the file protocol (see Boundaries); the product never does |
| `git worktree list` hash | — | **abbreviated** (`215cde1`) | the parser locates the short hash before ` [branch]` / ` (detached HEAD)` / EOL |

## Claims → evidence

Lane: `logs/10-selftest.log` — `WORKSPACE-VCS-SELFTEST ok=1 checks=551 failures=0`
(462 before this batch, all still present; 89 checks added). Screenshots:
`logs/20-screenshots.log` — `ok=1 checks=62 failures=0`.

### AC-5.2 — stash save / pop / apply / drop (real git, panel + backend)

| Claim | Checks |
| --- | --- |
| the panel issues the EXACT stash requests (message optional, ref positional) | `vcs.workspace.stash-save-request`, `.stash-save-empty-request`, `.stash-pop-request`, `.stash-apply-request`, `.stash-drop-request` |
| save → list → apply → drop → pop against a REAL repository | `vcs.workspace.e2e.stash-save`, `.stash-save-git` (`git stash list` itself), `.stash-list`, `.stash-ref`, `.stash-apply`, `.stash-apply-kept` (file content), `.stash-apply-count`, `.stash-drop`, `.stash-drop-count`, `.stash-save-two`, `.stash-pop`, `.stash-pop-content`, `.stash-pop-dropped` |
| the read lists the stash stack with ref + subject; a plain directory is empty with a note | `vcs.workspace.stashes-visible`, `.stash-text`, `stashes_on_a_plain_directory_are_empty_with_a_note_not_an_error` (Rust) |
| `stash save` never uses `-m` (the baseline rejects it) and prefixes `--` only for a leading-dash message | `stash_save_argv_is_positional_because_the_baseline_has_no_dash_m` (Rust, exact argv); `logs/00-probe-git1831.log` |
| a drop/pop of a named stash touches only that stash | `stash_drop_and_pop_target_the_named_stash_only` (Rust, two stashes) |
| an empty stack is a normal tool failure with git's real reason, not a parameter refusal | `stash_operations_on_an_empty_stack_report_the_real_git_reason` (Rust) |
| parameters are validated before any process | `stash_parameters_are_validated_before_any_process` (Rust: newline message, `--all` ref, whitespace ref, paths) |

### AC-5.2 — worktree add / remove / list

| Claim | Checks |
| --- | --- |
| the panel issues the EXACT worktree requests (path in `target`, existing branch in `name`) | `vcs.workspace.worktree-add-request`, `.worktree-remove-request` |
| add → list → remove against real git (path with a space, branch checked out) | `vcs.workspace.e2e.worktree-add`, `.worktree-list`, `.worktree-list-text`, `.worktree-remove` |
| `worktree remove` never passes `--force`: a dirty worktree is refused with git's reason and stays on disk | `vcs.workspace.e2e.worktree-dirty` (lane) and `worktree_add_list_remove_round_trip` (Rust: `contains modified or untracked files`, directory still exists, then the clean removal succeeds) |
| the read reports the main worktree; on a git without `worktree` it reports git's REAL refusal | `worktree_list_reports_the_main_worktree_even_without_worktree_support` (Rust: takes the error branch on 1.8.3.1, asserts the note contains `not a git command`; `logs/40-rust-tests-1831.log`) |
| a path with spaces survives the listing parse; a detached row says so | `vcs.workspace.worktree-text-with-space`, `.dialog.detached-row`, `worktree_rows_survive_spaces_in_the_path` (Rust, abbreviated hashes) |
| an option-shaped / empty path is refused and writes nothing | `vcs.workspace.worktree-add-empty-path-refused`, `.worktree-add-empty-path-zero-write`, `.worktree-add-option-path-refused`, `.worktree-add-option-path-zero-write`, `.worktree-remove-empty-refused`; `worktree_parameters_are_validated_before_any_process` (Rust) |

### AC-5.4 — submodule initialize / update (local fixture, no network)

| Claim | Checks |
| --- | --- |
| the panel issues `submodule init` and `submodule update --init` | `vcs.workspace.submodule-init-request`, `.submodule-update-request` |
| a FRESH CLONE of a superproject with a local submodule is initialized and checked out at the pinned commit | `vcs.workspace.e2e.submodule-init`, `.submodule-update`, `.submodule-in-sync` (`git submodule status` starts with a space) |
| the same flow is proven in Rust against real git | `submodule_init_and_update_check_out_the_pinned_commit` (`crates/vd-vcs/tests/workspace.rs`); `logs/40-rust-tests-1831.log` (1.8.3.1) and `logs/41-rust-tests-host253.log` (2.53) |
| a repository WITHOUT submodules is a normal success | `submodule_on_a_repository_without_submodules_is_a_normal_result` (Rust) |

### AC-5.3 — double confirmation, cancel = zero writes, i18n

| Claim | Checks |
| --- | --- |
| `stash drop` asks twice; the first decline writes nothing | `vcs.workspace.stash-drop-declined`, `.stash-drop-declined-zero-write`, `.stash-drop-first-confirm` (one call), `.stash-drop-double-confirmed` (three calls total = pop/apply-level single + drop's two) |
| `worktree remove` asks twice; the first decline writes nothing | `vcs.workspace.worktree-remove-declined`, `.worktree-remove-declined-zero-write`, `.worktree-remove-first-confirm`, `.worktree-remove-double-confirmed` |
| save/pop/apply/add/submodule do NOT ask twice (they are recoverable) | the same checks above assert the exact request count after ONE confirm for the others |
| the dialog drives the real actions (buttons → action, lists → selected ref/path) | `vcs.workspace.dialog.*` (fields, two lists, selected stash/worktree, detached row, click-selects-action, drop-action) |
| every new string is in the compiled catalogue | `vcs.i18n.workspace`; `lrelease` **880 finished / 0 unfinished** (`logs/gate-lrelease.log`) |
| the two read gate tokens are LISTING-only reads | `read_and_write_tables_are_disjoint_and_default_deny`, `every_git_write_subcommand_is_refused_before_spawn` (Rust: `stash save`/`pop`/`apply`/`drop`/`clear` and `worktree add`/`remove`/`prune` refused by the read gate; bare `stash`/`worktree` refused) |

## Screenshot (xcb, 1500×1000)

`bash evidence/vcs-git-workspace/screenshots.sh` (Xvfb :82, `--require-xcb`)
renders `screenshots/50-vcs-workspace-dialog-1500x1000.png` (**53887 B**): the
dialog with a REAL stash (`stash@{0}  On master: ui stash`), the REAL main
worktree and a REAL linked worktree (`…/linked worktree  [feature/ui]`), the
status line `1 stash(es), 2 worktree(s)`, and the filled stash message / worktree
path / branch fields. The probe asserts bytes > 4096, exact 1500×1000 and ≥ 8
sampled colours (349 measured), and `vcs.screenshot.workspace-listings` proves the
lists came from the real repository (`stashes=1 worktrees=2`).

## The seven gates

`bash evidence/vcs-git-workspace/run-gates.sh` (build-qt, ctest, --selfcheck,
clippy, fmt, docs, lrelease) plus the AC-5.2/5.4 evidence step. Logs:
`logs/gate-build-qt.log`, `logs/gate-ctest.log`, `logs/gate-selfcheck.log`,
`logs/gate-clippy.log`, `logs/gate-fmt.log`, `logs/gate-docs.log`,
`logs/gate-lrelease.log`, `logs/gate-ac52-evidence.log`, `logs/gate-summary.log`.
`logs/50-build-qt-iter1..4.log` are the development iterations (iter1–3 fixed
compile errors, iter4 is the first full green build); `logs/gate-build-qt.log` is
the authoritative gate log.

## Boundaries

* **The delivery baseline has no `git worktree`.** The four worktree operations
  are implemented and proven on git 2.53 (the Qt6 lane image / the host); on
  1.8.3.1 the listing reports git's real `'worktree' is not a git command`
  (`error = 1` + note) and the write reports git's exit message + stderr. This is
  stated rather than papered over: the baseline E2E in
  `logs/40-rust-tests-1831.log` shows the explicit `SKIP` with that reason.
* **The submodule file protocol is a FIXTURE decision, not a product one.** git
  2.53 refuses `file://` submodule clones by default (CVE-2022-39253) and reads
  the override from the PROCESS configuration, not the repository (measured in
  `logs/01-probe-host-git253.log`). The lane fixture therefore sets
  `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=protocol.file.allow
  GIT_CONFIG_VALUE_0=always` for the duration of the two submodule calls (and the
  Rust fixture uses the same variables); the product passes no `-c` and no config
  change. `submodule add` is never a product operation — the product only runs
  `init` and `update --init`.
* **`worktree add` checks out an EXISTING branch** (empty branch = detached HEAD
  at the current commit); creating a new branch is the separate `createBranch`
  operation. `-b` is never passed.
* **Double confirmation is a UI policy** (`confirmTwice` + the provider seam), as
  in batches 3b/5a; the backend does not enforce it. Headless with no provider the
  answer is a refusal, which is what the zero-write checks observe.
* **The stash message is positional** because the baseline has no `stash save -m`
  and no `stash push`; `--` is emitted only when the message begins with `-`.
* The translations `.ts` repair: `lupdate` was first run over the vcs lane only,
  which marked the OTHER lanes' 590 messages `vanished`; the final run scans
  `src` recursively (the same shape the file was generated with), so the file has
  880 live + 21 obsolete entries and no message lost.


## 批 6b 追加（AC-6.7）：捆绑 git 2.53.0 上的 worktree 真实 E2E

批 6a 把 git 2.53.0 随包进 AppDir（`<appdir>/bin/git`，loader 包装脚本），批 5b 遗留的
"worktree 只能在宿主 git 上验证"缺口因此关闭。新增
`crates/vd-vcs/tests/worktree_e2e.rs`（env-gated：`VEDIT_GIT_BIN=<appdir>/bin/git`），
**经产品路径**（`vd_vcs::write` 的 `GitWorktreeAdd` / `GitWorktreeRemove` +
`vd_vcs::workspace::worktrees`）在 /tmp fixture 上跑 add → list → remove：

- `logs/45-worktree-e2e-bundled.log`：`EVIDENCE bundled git version 2.53.0`，add 后
  `workspace::worktrees` 列出 2 项（主工作树 + `linked worktree`，branch `feature/bundled`），
  脏工作树删除被 git 真实拒绝（`contains modified or untracked files, use --force`），清理后
  删除成功、列表回到 1 项，参数校验（`--force` 形状的路径）在任何进程之前拒绝；
  `test result: ok. 1 passed`。
- 复跑：`bash evidence/svn-e2e/run-all.sh`（第 4 步）或
  `docker run … -e VEDIT_GIT_BIN=/vedit-build/packaging/runtime/out/AppDir/bin/git … \
   cargo test -p vd-vcs --test worktree_e2e -- --nocapture`。
- 基线的 `crates/vd-vcs/tests/workspace.rs` 仍保留（它在 git 1.8.3.1 上显式 SKIP worktree
  往返并断言"真实拒绝原因"）；两条断言都在，未删任何检查。
