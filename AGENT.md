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

The product is Rust (GTK3 + GtkSourceView); the CentOS 7 packaging lane builds
inside a container. `package.json` is verification orchestration ONLY (it maps
the MPD gate ids to `build/gates/*.sh`) and is never a product runtime or build
dependency.

- Install dependencies: no host install step — the system toolchain is pinned by
  `rust-toolchain.toml` (T7) and the slang artifact is built in a fresh
  `docker.m.daocloud.io/library/centos:7` container by `build/slang/build.sh`.
- Build: `bash build/slang/build.sh` (one command: build `build/slang/out/slang`
  in a CentOS 7 container, assert highest GLIBC symbol <= 2.17, run the offline
  smoke test). Gate wrapper: `bun run verify:gates`
  (`GATES_REBUILD=1` forces the full rebuild).
- Test: `cargo test --workspace` (gate wrapper: `bun run verify:tests`)
- Type check and lint: `cargo clippy --workspace --all-targets -- -D warnings`
  (`bun run verify:typecheck`; falls back to `cargo check --workspace` only when
  clippy is unavailable, and prints that it did so)
- Format: `cargo fmt --all` (check form: `cargo fmt --all -- --check`)

At this milestone (slang spike) only `bun run verify:gates` can be green: the
Cargo workspace does not exist until T7. The other `verify:*` gates exit 1 and
say why (they never fake a pass). `verify:gates` folds in `cargo test --workspace`
once `Cargo.toml` exists, and from then on a test failure fails the gate (the
`N/A` line is only legal before T7).

The evidence tool's fixed gate table can only reach `gates`, `docs`, `manifest`,
`comments`, `rows` (`bun run verify:<id>`) and `typecheck` (alias). `tests`
(`bun test packages`) and `vendor`/`dist`/`pack` (`node scripts/verify-*.ts`) are
unreachable in a Rust project, so they produce no evidence; those scripts exist
for humans only. Details: `build/gates/README.md`.

## 5. Verification

- Re-run the checks that cover your change and quote the observed output.
- Never edit a test or a gate to make a change pass; fix the cause or report the defect.

## 6. Git discipline

- Work on a branch, never directly on the main branch.
- Use a commit format such as `<type>(<scope>): <summary>`.
- Open a pull request describing what changed, the evidence, the checks run and any
  unverified bound.
- One writer per working tree; never rewrite published history.

## 7. Delegation and evidence

- Split large work into bounded tasks with a written contract and a separate review.
- Keep requirement, implementation and review as separate records.
- Record evidence (command, observed result, path) on disk; a claim without evidence is
  incomplete.

## 8. Security

- Never commit or echo secrets, tokens or credentials.
- Keep generated logs and evidence free of secret material.
