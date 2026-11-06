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

The product is Rust (GTK3 + GtkSourceView 4.6.1) and is built inside a pinned
CentOS 7 image, because the deliverable must run on CentOS 7 (glibc 2.17).
`package.json` is verification orchestration ONLY (it maps the MPD gate ids to
`build/gates/*.sh`) and is never a product runtime or build dependency.

- Install dependencies: none on the host. The pinned image (base digest, Rust
  1.99.0, GtkSourceView 4.6.1, meson/ninja) is built by `bash scripts/build.sh`;
  every download's sha256 is pinned in `scripts/versions.env`.
- Build the **delivered Qt6 product** (v2): `bash scripts/build-qt.sh` ->
  `target/qt6-build/src/app/vedit-app`. Its runtime needs the KF6/Qt6 shared
  libraries, `libtree-sitter` 0.25 and the Qt platform plugins; the measured
  package names are in `evidence/runtime-deps/REPORT.md` §1 and the build matrix
  in `docs/BUILD.md` §12. **If you are verifying the v2 deliverable, use this
  command.**
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

Toolchain: the delivered AppDir bundles `git 2.53.0` and `subversion 1.14.5`
under `usr/lib/vcs/` (57-library ldd closure, mutually non-shadowing with the
product's own `lib/`); resolution is **bundled > PATH > env** and observable as
`VCS-TOOLCHAIN git=… version=… svn=… version=… source=bundled`. `svnadmin` is
**build-only** (used to create the E2E fixture, deliberately not shipped).

Evidence: reads and writes are covered against REAL subprocesses — Git end to
end (including `file://` bare-repo remotes, a 401 credential transport, history
and workspace operations) and SVN with a real `svnadmin create` + `svn
checkout` working copy driven by the bundled `svn` (`evidence/vcs-write/`,
`evidence/vcs-toolchain/`, `evidence/vcs-git-remote/`, `evidence/vcs-git-history/`,
`evidence/vcs-git-ops/`, `evidence/vcs-git-workspace/`, `evidence/svn-e2e/`).
SVN is **no longer stub-only**: the `.svn` project path runs the bundled client.
Still not claimed: real network / HTTPS / SSH transports — those are limited to
local bare repositories and the local 401 auth fixture.

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
  issue; a death there is a visible test crash, not a user crash, but a shared
  `vedit_runtime_init()` should be used when those entry points grow one.

### Runtime toolchain discovery (diagnostics / outline)

The diagnostics and outline panels need `verible-verilog-ls` (LSP) and `slang`
(deep lint). Resolution order is **bundled > local checkout > PATH > env**, and
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

- Work lands through pull requests; never commit directly to `main` or `dev`.
  `fix/<slug>`, `feat/<slug>`, `chore/<slug>` and `docs/<slug>` branch off `dev`,
  the PR merges into `dev` (a merge commit is allowed), and a release promotes
  `dev` through `release/<ver>` into `main`. `dev` is the integration line and
  `main` is the release line: `main` only moves when a release merges into it.
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
- Evidence lives in a per-batch directory under `evidence/` (one README plus its
  raw logs), and every acceptance claim points at it. The final acceptance
  matrix for a delivery is `evidence/final-acceptance-2/AC-MATRIX.md`: it lists
  each requirement id, its verdict and its evidence pointer, and it carries the
  known limitations and the carried (non-defect) exemptions. Do not overwrite an
  older batch's evidence to make a new claim fit — add a new directory.
- Gate evidence rows live under `.mpd/verify/evidence/` and verdict records under
  `.mpd/verify/records/`; a PASS names its sources (frozen contract, requirements,
  the batch evidence) and the gate evidence ids it rests on.

## 8. Security

- Never commit or echo secrets, tokens or credentials.
- Keep generated logs and evidence free of secret material.

## 9. KTextEditor 语法定义仓库事实（T127，重要）

KTextEditor 的 `KSyntaxHighlighting::Repository` **不认**我们随包的两份 XML
（`resources/syntax/vedit-*.xml`）：解析 `SystemVerilog` / `Verilog` 时它指向 **KDE 自带的定义**
（例如 SystemVerilog 的 author 是 `Sean O'Boyle (seanoboyle@intelligentdv.com)`）。
`Repository::addCustomSearchPath()` 与私有 XDG 目录都改变不了这一点，
因为 `KTextEditor::Editor::repository()` 返回 const，没有公共注入路径。

**后果**：编辑器里实际生效的高亮/原生折叠来自 KDE 自带定义；我们的 XML 只被面包屑的语法扫描
（`syntax_breadcrumb_source.*`，读 `resources/queries/*/structure.scm`）使用（T29 删掉折叠 lane 后
已无第二个消费者）。
所以“高亮是好的”与“我们的 XML”没有关系；要改我们的定义，必须走我们自己的扫描路径，
不能指望 KTextEditor 加载它。

**xcb 实测（T127）**：不加载任何 `FoldInjector`，在 Xvfb+xcb 下真实鼠标点 gutter 折叠标记，
模块行可以**双向**折叠/展开（`hidden 0→38→0`、`scroll_max 939→901→939`），非 region 行同列负控制无变化。
→ KDE 原生折叠可用；VEdit 自建的 `exec("fold")` 注入层是与它打架的根源，应删除。

## 10. 折叠策略：KDE 原生（T128）

VEdit **不再注入折叠**（T128 删掉了 `FoldInjector` 的 `exec("fold")` 循环、registry、
`unfoldRegistered()` 和所有绕光标的 unfold 逻辑）。文件打开时**不自动折叠**（VS Code/Kate/CLion 同款行为），
折叠由 KTextEditor 从 KSyntaxHighlighting 定义计算，用户点 gutter 标记按需折叠/展开。

- 依据：T127 的 xcb 真点击实验（`evidence/fold-gutter-click/`）在**零注入**下证明标记存在且双向可用
  （`hidden 0→38→0`、`scroll_max 939→901→939`，非 region 行负控制无变化）。
- 旧判据 `hidden-975` / `registry-200` / `firstopen-full-count` **已废弃**，替换为真交互判据：
  `vedit-fold-gutter-click`（xcb+Xvfb，实际点 marker 两个方向；无 xvfb 时显式 SKIP）与
  `vedit-native-fold-tests`（10k 行 open ≤2 s、二次打开不崩、折叠时移动光标不崩、打开无自动折叠）。
- 删除清单：`src/syntax/folding/fold_injector.{h,cpp}`、`tests/folding_tests.cpp`、
  以及 `FoldBreadcrumbBinding` 里的折叠字段/方法；面包屑保留不变。
- **T29 已把这条 lane 整体删除**：`src/syntax/folding/`（`fold_range.*`、`fold_source.*`、
  `syntax_fold_source.*`、`ksyntax_fold_source.*`、`README.md`）、静态库 `vedit_syntax_folding`、
  它在 `src/editor/CMakeLists.txt` 的 `add_subdirectory`/链接，以及 2 个测试可执行一并删除；
  `grep -r vedit_syntax_folding`（源码/CMake/脚本）= 0。证据 `evidence/dead-code/`。
- **真实点击判据没有随之消失**：`vedit-fold-gutter-click`（xcb+Xvfb 下真实鼠标点 KDE 自带 gutter
  标记，双向折叠，非 region 行负控制）的源码迁到 `src/editor/kernel/fold_gutter_click_probe.cpp`，
  在 `src/editor/CMakeLists.txt` 重新注册，**ctest id 不变**；实测 `hidden 0→38→0`、
  `scroll_max 939→901→939`，`NATIVE-GUI result ok=1`。它只依赖 KTextEditor/Qt，不依赖已删的库。
- `resources/queries/**` 保留：`highlights/folds/structure.scm` 被 `src/syntax/grammars` 嵌入构建产物，
  `structure.scm` 还被面包屑产品路径消费，删除需要改 grammars 的 CMake（越界）。`src/syntax/common/`
  里只服务那条 lane 的两个 helper（definition-file-name mapper 与 application-directory fallback）已无调用点，
  随 T49 删除。
