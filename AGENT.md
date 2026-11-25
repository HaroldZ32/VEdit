<!-- Generated once by mpd-project-init (my-power-dsh) on 2026-10-30T02:06:34.209Z.
     This file is a starting point: edit it freely. The plugin never overwrites it,
     and it will not create another instruction file in this workspace. -->

# AGENT.md

Operating instructions for any AI agent working in this repository. Replace every
`<fill in: ...>` placeholder with this project's real value; never guess one.

## 1. Read first

- At the start of every session, read this file, then the paths it names below.
- Prefer this repository's own docs, tests and build files over assumptions. When a
  document and the code disagree, report the conflict instead of silently picking one.

## 2. Plan before acting

- State the goal in one sentence, then list the steps you will take.
- Keep a todo list for any task with two or more steps, and update it as steps finish.
- Surface open questions and assumptions before making a large change.

## 3. Small diffs

- Make the smallest change that satisfies the request; no drive-by refactors and no
  reformatting of code you did not need to touch.
- Match the style, naming and structure already in the tree.

## 4. Commands

VEdit ships a Qt6/KF6 desktop application backed by Rust services. The build
uses a pinned Ubuntu image. The Linux x86_64 packages include the application,
its private runtime and HDL tools, with CentOS 7 (glibc 2.17) as the target
userspace baseline. `package.json` maps MPD gate IDs to `build/gates/*.sh`.

- Build the Qt6 product with `bash build/slang/build.sh` followed by
  `bash scripts/build-qt.sh`. CI can use the hash-checked `slang` artifact.
  The Rust service step needs `build/slang/out/slang` even with the Qt
  `--no-test` option. The bare binary is `target/qt6-build/src/app/vedit-app`.
  See [BUILD.md](docs/BUILD.md) §12 for the KF6/Qt6, `libtree-sitter` and
  platform-plugin build and runtime dependencies.
- Package the Qt6 product with `bash packaging/runtime/build.sh`. The AppImage,
  `.tar.gz`, `.run` and shared `SHA256SUMS` are in `packaging/runtime/out/release/`.
  Follow [RELEASING.md](docs/RELEASING.md) for promotion checks and publication.
- Rust C ABI selfcheck of the product binary (T97): `target/qt6-build/src/app/vedit-app
  --selfcheck-ffi crates/vd-ffi/tests/fixtures/project` (offscreen:
  `QT_QPA_PLATFORM=offscreen`). `--selfcheck-ffi` asserts the **C ABI smoke
  fixture's** file set (3 rtl files, `+incdir+inc`, `WIDTH=8`/`SIM`), so it must
  be given `crates/vd-ffi/tests/fixtures/project`. It is **not** the
  project-model fixture: `tests/fixtures/verilog-project` (T14) has 7 files and
  its own expectations under `expected/`, validated by
  `cargo test -p vd-ffi -p vd-config` (and by `scripts/verify-fixture.sh`).
  Passing the T14 folder to `--selfcheck-ffi` is refused with a hint instead of
  four misleading `ffi.project.*` failures.
- Build the **legacy Rust/GTK3 product**: `bash scripts/build.sh` (one command:
  pinned image -> `cargo build --release --workspace` -> ELF assertion that the
  highest GLIBC symbol is <= 2.17 -> Xvfb `--selftest smoke` window check). It
  writes `target/release/vd-app` (copied to `build/artifacts/vd-app`) and
  **never produces `vedit-app`**.
  Gate wrapper: `bun run verify:gates`.
- Test: `bash scripts/gates.sh` runs `cargo test --workspace` (in the pinned
  image when the host has no GTK development files).
- Type check and lint: `cargo clippy --workspace --all-targets -- -D warnings`
  (through `bash scripts/gates.sh`).
- Format: `cargo fmt --all`; check form `cargo fmt --all -- --check`
  (through `bash scripts/gates.sh`).

The slang deep-lint binary has its own one-command build:
`bash build/slang/build.sh`.

### VCS panel: asynchronous reads AND writes, with a write allow-list (T99 + batch 3–6)

The panel shows backend/branch/revision, changed files and diffs, plus the
History tab, the three-column working tree and the workspace (stash / worktree)
dialog. **Every subprocess — read or write — is an asynchronous C ABI job**
(`vd_vcs_status` / `vd_vcs_info_job` / `vd_vcs_diff_job` and the write
`vd_vcs_write_job` variants, each paired with poll/free); the panel polls them
from a QTimer, so no `git`/`svn` process ever runs on the GUI thread.

The safety boundary is now "reads still refuse every write subcommand; writes
are allowed only through a per-operation allow-list":

- **Read gate refuses writes.** The read runner rejects every write subcommand
  *before spawning* (`commit`, `push`, `checkout`, `reset`, `revert`, `switch`,
  `update`, …), and the two tables are disjoint — asserted by
  `crates/vd-vcs/tests/readonly.rs` (positive and negative cases) and the lane
  selftest (`vcs.ffi.read-only-surface` was strengthened, not relaxed).
- **Write allow-list, default deny.** Nine typed operations (4 Git, 5 SVN) are
  permitted; an unknown operation code, a non-repository, a backend mismatch or
  a path outside the repository is refused before any process starts.
- **Confirmations.** Destructive operations (`reset --hard`, `clean`, `discard`,
  delete branch/tag, remote remove, `worktree remove`) ask **twice**
  (`confirmTwice`); declining at either step writes nothing.
- **Credentials never persist.** When no helper/SSH is available the credential
  dialog passes the secret to the child through its **environment only** — never
  argv, never a file, never a log or an evidence artefact; the evidence greps for
  secret material stay at zero. Cancelling aborts the operation with git's real
  reason.

The integrated terminal and VCS share the host shell selected in
**Preferences → Tools → Host shell**. VCS resolves executable `git` and `svn`
on the effective host `PATH`, then accepts absolute paths from `VEDIT_GIT` or
`VEDIT_SVN`. All three packages launch without either client. VCS operations
report how to install or locate a missing client. Use the system `svnadmin`
for SVN E2E fixtures. The host-tools acceptance in `evidence/host-tools/`
records the source revisions and AppImage tested for PR #20. Earlier local
repository and credential fixtures are under `evidence/vcs-write/` and
`evidence/svn-e2e/`.

### Project discovery: optional files are not errors (T114)

`.rules.verible_lint` is **optional**: when no file is found the built-in Verible
rule set applies, so nothing is reported (a missing optional file is the normal
case, not a defect — the same rule T85 applied to a missing toolchain). Only a
rules file that **exists but cannot be read** is reported (`rules.unreadable`);
that message stays path-free (no `file` field, the text names the file relative
to the project root) and says the built-in rules keep working meanwhile.

The upward search **intentionally stops at `$HOME`** (or at the filesystem root
when the project lives outside the home tree), so it can never walk out of the
user's tree. `filelist.missing` and the other `filelist.*` / `config.*` codes
stay: they describe things the user explicitly configured or wrote wrong.

### Rust is statically linked into a C++ host (T134)

`vedit-app` links the Rust service layer (`crates/vd-ffi` → `vd-lsp`, `vd-config`,
…) **into the C++ process**. Rust's runtime startup — the part of a Rust binary
that installs process-wide defaults — does **not** run in that host. Known
consequences, and the rule they imply:

- **`SIGPIPE`**: a Rust binary ignores it at startup; in this host it would stay
  at its default (terminate) and a write to a dead child (e.g. the verible LSP
  pipe while switching files) killed the GUI with `Broken pipe`, with **no crash
  report** (SIGPIPE is not in the reporter's signal set). `src/app/main.cpp` sets
  `std::signal(SIGPIPE, SIG_IGN)` before anything else; the write then returns
  `EPIPE`, which the Rust side must record as a state (never panic/abort).
- Other process-level Rust defaults (e.g. stack-overflow handling) are **not**
  installed either. Any new behaviour that relies on Rust's runtime init must be
  set up explicitly by the C++ host and documented here.
- Lane selftests that spawn real LSP children and link Rust have the same latent
  issue; a death there is a visible test crash, not a user crash. They call the
  shared `vedit_runtime_init()` (`src/common/runtime_init.h`, header-only) as the
  first statement of `main()`; any new entry point that links Rust does the same.

### Runtime toolchain discovery (diagnostics / outline)

The diagnostics and outline panels need `verible-verilog-ls` (LSP) and `slang`
(deep lint). Resolution order is **bundled > local checkout > PATH > env**, where
`env` includes the imported environment layer (see the environment section below), and
the chosen source is reported (`TOOLCHAIN source=verible:... slang:... missing=[...]`
from `--selfcheck`; the same line names the local candidates as `local=[...]`).
Three ways to satisfy it, in delivery order:

1. **bundled** — put `verible-verilog-ls` / `slang` in the product's `bin/` next
   to `vd-app` (the AppDir/AppImage form; nothing else is needed);
2. **local checkout** — in a source tree, `bash scripts/fetch-verible.sh`
   (produces `target/verible-pin/extract/verible-*/bin/`, also linked as
   `.mpd/recon/verible/bin/`) and `bash build/slang/build.sh` (produces
   `build/slang/out/slang`). A **bare build**
   (`target/qt6-build/src/app/vedit-app`) picks both up automatically: the
   resolver walks up from the binary to the checkout root (marker
   `scripts/fetch-verible.sh`) and probes those artifacts;
3. **env** — `VEDIT_VERIBLE_LS=/path/to/verible-verilog-ls`,
   `VEDIT_SLANG=/path/to/slang` (or the `_DIR` forms `VEDIT_VERIBLE_LS_DIR`,
   `VEDIT_SLANG_DIR` naming the directory).

When a tool is missing, the panels show **one** concise status line (what is
missing, what it disables, the three fixes); the outline stays an empty table
with a one-line reason — a missing environment is never rendered as per-file
errors.

### Environment import (batch-20261115)

Startup now captures the login shell environment **once**: `vd_env_autocapture_start`
detects `$SHELL` (bash / zsh / sh / ksh / fish / csh / tcsh), keeps only variables the
shell changed, and stores them in the **global** layer (marker
`<stateDir>/environment.auto.json`, no names/values in it; manual selections are never
overwritten). `VEDIT_NO_ENV_AUTOCAPTURE=1` disables it. The imported layer now reaches
(a) filelist variable expansion, (b) deeplint/slang children, (c) verible LSP children
(`LspConfig.environment`), (d) the integrated terminal PTY, and (e) C++ toolchain
resolution — the latter through the C ABI `vd_env_effective_snapshot` /
`vd_env_snapshot_free` (values may be secrets: never print or log them). VCS
jobs use the same effective host environment as the terminal. AppRun preserves
the inherited `PATH` in `VEDIT_HOST_PATH`, including an empty value. The host
child environment filters AppDir entries and removes `LD_LIBRARY_PATH` and
`LOCPATH`. Changing the imported layer requires a restart.

### Project mode is filelist-gated; LINT is split (batch-20261115)

Project mode requires an **explicitly imported filelist** (`ProjectModel` state
`filelists` non-empty). Opening a folder does **no** project-wide work: no expansion,
no `slang` catalog, no full-project lint. Importing a filelist runs a **catalog-only**
pass (no lint); setting the top module re-runs the catalog with `--top` and re-roots the
module tree. Full-project lint is **manual only** (`project.lint`); the save-triggered
and open-triggered passes are gone. Its results live in the bottom `project-lint` dock,
never in the `Outline` diagnostics. Realtime lint is the only automatic lint: it analyzes
the current file plus the **one-level** child-module definition files (caps: 64 files per
file, 128 total) and displays only the current file. With an imported filelist,
realtime lint and Ctrl+click use its source set and module ownership, including
legitimate includes. Preserve slang's compiler settings without adding source
operands beyond the filelist. Reuse LSP sessions when switching files within
an existing analysis scope (`vd_lsp_session_change`). Regression ctests: `vedit-realtime-depth-switch-e2e`,
`vedit-app-batch-layout-selftest`, `vedit-editor-open-bench`.

Dependency ownership: the root `Cargo.toml`, `Cargo.lock` and
`rust-toolchain.toml` are workspace-level and owned by the skeleton task. The
lane crates ship with their dependencies pre-installed, so a lane must not add a
dependency or edit those files; ask the captain if one is genuinely missing.

### CJK font fallback in the editor (T119)

The editor keeps DejaVu Sans Mono for Latin (so ASCII metrics are unchanged) and
falls back to Noto Sans Mono CJK for CJK glyphs. Because the two fonts have
different em advances, a CJK glyph is **1.60×** a Latin advance rather than the
typographic 2.0×; mixed Chinese/Latin comment alignment is therefore approximate,
while CJK runs are internally monospaced. This is a **deliberate trade-off**: ASCII
width stability (the bulk of code) was chosen over a 2.0 mixed ratio.

Back door, if strict 2.00 is ever required: set the editor's primary font family
to `Noto Sans Mono CJK SC` (one configuration line). The cost is that the Latin
advance changes from **10px to 8px** (DejaVu Sans Mono → Noto Sans Mono CJK SC),
so all pure-ASCII metrics change.

## 5. Verification

- Re-run the checks that cover your change and quote the observed output.
- Never edit a test or a gate to make a change pass; fix the cause or report the defect.

## 6. Git discipline

- 每次修改代码，都要在交付前将本次改动提交并推送到远端任务分支，向 `dev` 创建 PR。若该任务已有打开的 PR，就推送到对应分支更新该 PR。交付时附上 PR 链接。
- Work lands through pull requests; never commit directly to `main` or `dev`.
  `fix/<slug>`, `feat/<slug>`, `chore/<slug>` and `docs/<slug>` branch off `dev`,
  the PR merges into `dev` (a merge commit is allowed), and a release promotes
  `dev` through `release/<ver>` into `main`. `dev` is the integration line and
  `main` is the release line. After the release PR merges, push the matching
  annotated `v<ver>` tag on the `main` merge commit to start Actions publication.
  Verify the public packages and sync `main` back to `dev` through a PR.
  Published history is never rewritten (no amend/rebase/force-push).
- Stage only your own task's declared write scope, with explicit paths; never
  `git add -A` and never `git commit -a`. If `index.lock` contends, retry instead
  of deleting the lock.
- Use a commit format such as `<type>(<scope>): <summary>` and put the role
  identity on the commit (`-c user.name="…" -c user.email="…"`).
- Record evidence (command, observed result, path) on disk under a path that
  contains `evidence/`, so a verifier can read it without reading implementation.

## 7. Delegation and evidence

- Split large work into bounded tasks with a written contract and a separate review.
- Keep requirement, implementation and review as separate records.
- Record evidence (command, observed result, path) on disk; a claim without evidence is
  incomplete.
- Keep each batch's evidence under `evidence/` with a README and the raw logs.
  Record the tested commit, output hashes, limits and links per acceptance
  criterion. `evidence/final-acceptance-2/AC-MATRIX.md` records the 2026-11-03
  acceptance. A new release requires evidence from its own build and downloads
  under `evidence/release-<ver>/`.
- Gate evidence rows live under `.mpd/verify/evidence/` and verdict records under
  `.mpd/verify/records/`; a PASS names its sources (frozen contract, requirements,
  the batch evidence) and the gate evidence ids it rests on.

## 8. Security

- Never commit or echo secrets, tokens or credentials.
- Keep generated logs and evidence free of secret material.

## 9. KTextEditor 实际使用的语法定义

`KTextEditor::Editor::repository()` 返回只读仓库。Qt6 开发构建通过 `src/editor/kernel/rendered_languages.qrc` 将 HDL 定义注册到 KDE 的 `syntax-addons` 资源入口；精简的 AppDir 从 `usr/share/org.kde.syntax-highlighting/syntax` 读取随包定义。修改 HDL 定义时同步核对 `THIRD_PARTY_NOTICES.md` 的资源哈希。Markdown 在开发构建中可使用 KDE 自带定义，精简包使用随包定义。

用真实编辑器的 `View::lineAttributes()` 验证高亮，用 gutter 点击验证原生折叠。两个运行环境的命令和观测结果见 [编辑器语言验收](evidence/desktop-ux-20261124/languages/README.md)。面包屑继续使用 `resources/queries/*/structure.scm`。

**xcb 实测（T127）**：不加载任何 `FoldInjector`，在 xcb+Xvfb 真图形路径下用命中测试合成事件（`widgetAt`+`sendEvent`）
点 gutter 折叠标记（`QApplication::widgetAt(global)` 命中 widget，再 `QApplication::sendEvent` 投递 press/release 的
`QMouseEvent`；这是 Qt 级合成事件，**不是**操作系统级指针注入），
模块行可以**双向**折叠/展开（`hidden 0→38→0`、`scroll_max 939→901→939`），非 region 行同列负控制无变化。
→ KDE 原生折叠可用；VEdit 自建的 `exec("fold")` 注入层是与它打架的根源，应删除。

## 10. 折叠策略：KDE 原生（T128）

VEdit **不再注入折叠**（T128 删掉了 `FoldInjector` 的 `exec("fold")` 循环、registry、
`unfoldRegistered()` 和所有绕光标的 unfold 逻辑）。文件打开时**不自动折叠**（VS Code/Kate/CLion 同款行为），
折叠由 KTextEditor 从 KSyntaxHighlighting 定义计算，用户点 gutter 标记按需折叠/展开。

- 依据：T127 的 xcb+Xvfb 命中测试合成事件实验（`evidence/fold-gutter-click/`）在**零注入**下证明标记存在且双向可用
  （`hidden 0→38→0`、`scroll_max 939→901→939`，非 region 行负控制无变化）。
- 旧判据 `hidden-975` / `registry-200` / `firstopen-full-count` **已废弃**，替换为交互判据
  （合成点击：`widgetAt` 命中 + `sendEvent` 投递，非 OS 级指针注入）：
  `vedit-fold-gutter-click`（xcb+Xvfb，按命中点向 marker 两个方向投递合成点击；无 xvfb 时显式 SKIP）与
  `vedit-native-fold-tests`（10k 行 open ≤2 s、二次打开不崩、折叠时移动光标不崩、打开无自动折叠）。
- 删除清单：`src/syntax/folding/fold_injector.{h,cpp}`、`tests/folding_tests.cpp`、
  以及 `FoldBreadcrumbBinding` 里的折叠字段/方法；面包屑保留不变。
- **T29 已把这条 lane 整体删除**：`src/syntax/folding/`（`fold_range.*`、`fold_source.*`、
  `syntax_fold_source.*`、`ksyntax_fold_source.*`、`README.md`）、静态库 `vedit_syntax_folding`、
  它在 `src/editor/CMakeLists.txt` 的 `add_subdirectory`/链接，以及 2 个测试可执行一并删除；
  `grep -r vedit_syntax_folding`（源码/CMake/脚本）= 0。证据 `evidence/dead-code/`。
- **点击判据没有随之消失**：`vedit-fold-gutter-click`（xcb+Xvfb，以命中测试合成事件（`widgetAt`+`sendEvent`）
  点 KDE 自带 gutter 标记，双向折叠，非 region 行负控制）的源码迁到 `src/editor/kernel/fold_gutter_click_probe.cpp`，
  在 `src/editor/CMakeLists.txt` 重新注册，**ctest id 不变**；实测 `hidden 0→38→0`、
  `scroll_max 939→901→939`，`NATIVE-GUI result ok=1`。它只依赖 KTextEditor/Qt，不依赖已删的库。
- `resources/queries/**` 保留：`highlights/folds/structure.scm` 被 `src/syntax/grammars` 嵌入构建产物，
  `structure.scm` 还被面包屑产品路径消费，删除需要改 grammars 的 CMake（越界）。`src/syntax/common/`
  里只服务那条 lane 的两个 helper（definition-file-name mapper 与 application-directory fallback）已无调用点，
  随 T49 删除。
