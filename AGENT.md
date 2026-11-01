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

### VCS panel: read-only, asynchronous, Git verified / SVN stub-only (T99)

The VCS panel is a read-only view (backend, branch/revision, changed files, one
file's diff). All three reads are **asynchronous jobs** in the C ABI
(`vd_vcs_status` / `vd_vcs_info_job` / `vd_vcs_diff_job` + poll); the panel
polls them from a QTimer, so no `git`/`svn` subprocess ever runs on the GUI
thread. There is no write entry point in the seam or the C ABI, and the runner
rejects write subcommands before spawning.

Real-subprocess evidence: Git is covered end to end (the lane selftest starts a
job against a repository and proves the event loop keeps ticking while a slow
`git` wrapper sleeps; `vedit-app --selfcheck-ffi` also exercises the VCS status
job). **SVN is stub-only**: neither the host nor the pinned image ships
`svn`/`svnadmin`, so the real SVN subprocess path has no end-to-end test — the
lane tests drive `StubVcsSource`/canned runners. Do not describe SVN support as
verified until a real `svn` working-copy smoke exists.

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

## 8. Security

- Never commit or echo secrets, tokens or credentials.
- Keep generated logs and evidence free of secret material.
