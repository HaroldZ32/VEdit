# T5 — Independent verification: CI/CD + F1/F2 (contract v2, AC-A1..A7 / AC-B1..B4)

Reviewer · 2026-10-30 · **verdict: request changes** (AC-A7 FAIL, AC-A1 FAIL; AC-A2..A6, AC-B1..B4 pass)

| item | value |
|---|---|
| reviewed commits | `389ca44` (CI: workflows + seams + BuildKit + docs) and `9c315e5` (F1/F2 + regression assertions) |
| branch / PR | `ci/github-actions-and-followups` → `dev`, **PR #6**, HEAD `9c315e5` (both commits pushed) |
| tree at review | clean at `9c315e5` — `git status` shows only untracked `evidence/github/ci-verify/a/` (this verification) and the pre-existing `evidence/github/readiness.md` |
| CI run under review | **`41036153917`** (`pull_request`, headSha `9c315e5`) |
| frozen contract | `.mpd/plans/ci-and-followups-contract.md` |
| F1/F2 app.rs sha256 | `9fb8cada22b5a5d404574c62e6f0d87e40ae5e2f3b7338eb7fd7902966183c06` |
| reviewer footprint | **no product code / script / workflow was modified, and no git commit was made**; the two `build/evidence/*.log` files that the scripts rewrite during the AC-A3/A4 runs were restored to HEAD afterwards |

---

## 1. A-series (CI/CD)

### AC-A1 `image` workflow — **FAIL** (one explicit clause unmet)

Met (`.github/workflows/image.yml`), by line:
- trigger `push: branches [dev, main]` on `scripts/docker/**`, `scripts/versions.env`, `scripts/lib.sh`,
  `scripts/build-image.sh` + `workflow_dispatch` (lines 8–16);
- `permissions: contents: read, packages: write` (18–20); GHCR login (35–40);
- build and push `ghcr.io/<owner>/vedit-build:<GITHUB_SHA>` **and** `:latest` (42–52);
- BuildKit, no legacy flag (`scripts/lib.sh` builds with a plain `docker build`; no
  `DOCKER_BUILDKIT=0` remains in any script — see AC-A4).

**Unmet:** the contract requires **GHA layer cache**. There is none: `image.yml` has no
`actions/cache`, no `docker/setup-buildx-action`, no `docker/build-push-action`, and
`grep -rn 'type=gha\|cache-from\|cache-to\|setup-buildx\|build-push-action' .github/ scripts/` returns
**nothing**; `scripts/build-image.sh` passes no cache flags. On every trigger the pinned image is rebuilt
from scratch (see F2). *Direction (not applied):* `docker/setup-buildx-action` (container driver — the
default `docker` driver cannot export GHA cache) + build/push with
`cache-from: type=gha`, `cache-to: type=gha,mode=max`.

Runtime note: the `image` workflow has **never run** (no runs for it; it is only triggered by a push to
`dev`/`main`) and GHCR has no published image — the ci run's pull returned `manifest unknown`
(`30-run-41036153917.raw.txt`, `34-job-log-failure.txt`).

### AC-A2 `ci` workflow — **PASS** (wiring + runtime observed)

`.github/workflows/ci.yml`:
- triggers `pull_request: branches [dev, main]` and `push: branches [dev]` (+ dispatch) — lines 9–14;
- resolves the pinned image (pulls GHCR `:latest`, else builds it in-job) — 51–65;
- runs `bash scripts/gates.sh` (67–72) and `bash scripts/build.sh` (74–79) with
  `VEDIT_SKIP_IMAGE_BUILD=1` + the resolved `VEDIT_BUILD_IMAGE_REF` (so both run in the pinned image);
- uploads `build/evidence/` **and** `build/artifacts/vd-app` with `if: always()` (86–94);
- failure logs are retained: GitHub keeps the run log (fetched here, 3084 lines) and the evidence artifact
  is uploaded `if: always()` — on this failed run the artifact `evidence` (94 765 B) was still uploaded
  (`35-artifacts.txt`).

The workflow wiring is correct; its first real execution failed inside the gates step — that is the
AC-A7 finding (F1), not a wiring defect here.

### AC-A3 script seams — **PASS** (executed)

| run | command | exit | observed |
|---|---|---|---|
| default | `bash scripts/gates.sh` | **0** | `[image] building vedit-build:local from the pinned inputs` → `GATES PASS` |
| skip | `VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/gates.sh` | **0** | `[image] VEDIT_SKIP_IMAGE_BUILD=1: reusing the existing image vedit-build:local` → `GATES PASS` |
| skip + missing ref | `VEDIT_SKIP_IMAGE_BUILD=1 VEDIT_BUILD_IMAGE_REF=vedit-build:definitely-missing bash scripts/build-image.sh` | **1** | `FATAL: VEDIT_SKIP_IMAGE_BUILD=1, but the build image 'vedit-build:definitely-missing' does not exist locally.` |
| pull | `VEDIT_BUILD_IMAGE_PULL=1 VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-image.sh` | **1** | `[image] pulling vedit-build:local (VEDIT_BUILD_IMAGE_PULL=1)` → `FATAL: cannot pull the build image vedit-build:local` |

Seam definitions: `scripts/lib.sh` `BUILD_IMAGE_TAG="${VEDIT_BUILD_IMAGE_REF:-vedit-build:local}"` (line 11),
`ensure_build_image_pulled` (63–70), `VEDIT_SKIP_IMAGE_BUILD` branch with the FATAL + exit 1 (82–90).
With no variables set the local path behaves exactly as before (git history: only the BuildKit flag was
dropped). The pull-*success* path needs a published registry ref, which does not exist yet (see AC-A1).

### AC-A4 BuildKit compatibility — **PASS**

- The three forced `DOCKER_BUILDKIT=0` are gone:
  `git grep 'DOCKER_BUILDKIT=0' 40d344d -- scripts packaging` → 4 lines in `scripts/lib.sh`,
  `packaging/tarball/build.sh`, `packaging/appimage/run-evidence.sh` (×2);
  at HEAD → **none** in those scripts (only two historical log lines in `packaging/*/evidence/` that quote
  the old warning).
- Local BuildKit build (`DOCKER_BUILDKIT=1 VEDIT_BUILD_IMAGE_REF=vedit-build:ac4-verify bash scripts/build-image.sh`):
  exit **0**, `buildx v0.38.1`, log starts `#0 building with "default" instance using docker driver`,
  41 BuildKit markers, **0** deprecation/legacy-builder lines, `image_id=sha256:1f1083dc…`.
- Real-run confirmation: the CI job built the same image on the runner —
  `[image] building vedit-build:local from the pinned inputs`, `#0 building with "default" instance using
  docker driver`, `image_id=sha256:56c2f808…`, step **success** (`34-job-log-failure.txt`).
- Dockerfile: the inline `printf` repo file was replaced by `COPY vault.repo.in` + `sed` from the pinned
  `CENTOS_VAULT_BASE` (both builders accept it).

### AC-A5 `release` workflow — **INCONCLUSIVE** (statically reviewed, cannot be executed here)

Static review against `.github/workflows/release.yml` — every clause matches:
- trigger `push: tags: ['v*']` (+ dispatch) — 9–12;
- `permissions: contents: write` — 14–16; `fetch-depth: 0` — 28–31;
- order: `build/slang/build.sh` (67–69) → `scripts/build.sh` (71–76) → `packaging/appimage/run-evidence.sh`
  (78–83) → `packaging/tarball/build.sh` (85–90) → SHA256SUMS (92–108) → release upload (115–124);
- SHA256SUMS covers the three artefacts **and** the in-package `verible-verilog-ls/-lint/-format` + `slang`
  (102–107); 4 files are uploaded (118–122).

Not executed: there is no tag for this work, and a `workflow_dispatch` run would publish a real GitHub
Release and requires the GHCR image; neither is appropriate from a verifier. Judged **INCONCLUSIVE**.

### AC-A6 runner hygiene — **PASS**

- `ci.yml` 81–84 and `release.yml` 110–113: `if: always()` → `sudo chown -R "$(id -u):$(id -g)" target build`
  (both jobs write the workspace; `image.yml` writes no workspace state, so it needs none).
- Runtime: on the failed ci run that step ran and **succeeded** (step 8, `34-job-log-failure.txt`).
- Cache scope: `ci.yml` 33–41 and `release.yml` 33–41 cache **only** `target/cargo-home` + `target/home`;
  no release artefacts or packaging outputs are cached.

### AC-A7 real run must succeed — **FAIL**

Raw evidence: `evidence/github/ci-verify/a/30-run-41036153917.raw.txt`,
`34-job-log-failure.txt` (verbatim job log), `35-artifacts.txt`.

- `gh run view 41036153917 --json status,conclusion,…` → `"status":"completed"`,
  **`"conclusion":"failure"`**, `event=pull_request`, `headSha=9c315e5`, created 09:48:49Z,
  completed 09:54:10Z (~5m21s).
- Failing step: **“Gates (lock policy, fmt, clippy, test)”** —
  `2026-10-30T09:54:06.1898587Z ##[error]Process completed with exit code 1.`
  “Release build, Xvfb selftest and GLIBC assertion” was **skipped**.
- Root cause (verbatim):
  ```
  test real_slang_reports_undeclared_identifier_exactly ... FAILED        (and 5 more real_slang_* tests)
  thread '…' panicked at crates/vd-deeplint/tests/real_slang.rs:26:5:
  slang artifact not found: set VEDIT_SLANG or run `bash build/slang/build.sh`
    (looked at /vedit-build/crates/vd-deeplint/../../build/slang/out/slang)
  test result: FAILED. 0 passed; 6 failed; 0 ignored; 0 measured; 0 filtered out
  error: test failed, to rerun pass `-p vd-deeplint --test real_slang`
  ```
- Why: `cargo test --workspace` includes six `real_slang` integration tests that hard-require the slang
  build product (correctly — the module says it “never skips silently”). The `ci` workflow never runs
  `bash build/slang/build.sh`, and `build/slang/out/slang` is git-ignored, so a clean CI checkout has none.
  The same gates pass locally only because that binary happens to exist in this workspace.
- Per the brief: **judged FAIL, nothing fixed** (and no check was relaxed). *Direction:* build slang before
  the gates in `ci.yml` exactly as `release.yml` does (the image already carries meson/ninja); do **not**
  skip or weaken the tests.

---

## 2. B-series (F1/F2) — on the **committed final tree** `9c315e5`

`git status` clean at `9c315e5`; `crates/vd-app/src/app.rs` sha256 `9fb8cada…` (matches the R1-tightened
tree). Re-verified end-to-end rather than relying on the earlier hash-bound T6 run
(`evidence/github/ci-verify/a/32-b4-final-tree.log`, `32-selftest-all-final.json`).

| AC | verdict | evidence |
|---|---|---|
| **AC-B1** reason appears exactly once | **PASS** | `AC-B1 … at startup` → `reason="FIM 未启用", occurrences=1`; `AC-B1 … after a rejected request` → `occurrences=1`; no `FIM: FIM`; LSP column and `set_info` messages still rendered (`configuration warnings: 1`, `Verible LSP: generation 0`, `LSP running (pid 93)…`) |
| **AC-B2** caret at the document start | **PASS** | `cursor=Position { line: 0, character: 0 }`; status starts with `1:1`; LF + CRLF unit tests pass (both in `cargo test`, 2/2) |
| **AC-B3** new regression assertions | **PASS** | all three present and passing: LF/CRLF in `crates/vd-editor/src/document.rs` tests, smoke 1:1, `fim` suite reason-once |
| **AC-B4** nothing weakened, counts only grow | **PASS** | `--selftest all` = **62** (smoke 30, highlight 8, config 6, lsp 6, slang 6, fim 6), 0 failed; the 51 pre-fix names **and** all 58 names from `7acd487` are a subset; `cargo test --workspace` = **225** (observed twice in the AC-A3 gates runs, 225 each); `selftest.rs +43/−0`, `document.rs +42/−0` → **zero deletions in test files**; fmt/clippy/test exit 0 |

**R1 from T6 is resolved:** the AC-4 unit test no longer uses a substring; it now asserts exact segment
equality — `composed.split("  |  ").any(|part| part == message)` in
`crates/vd-app/src/app.rs` `mod tests` (“assert that segment by equality (not as a substring)”).

---

## 3. Findings (ranked; nothing here was fixed by the reviewer)

### F1 · Blocker (AC-A7) — `ci` fails on a clean checkout: it never builds the slang binary
`crates/vd-deeplint/tests/real_slang.rs` requires `VEDIT_SLANG` or `build/slang/out/slang`; the latter is
git-ignored and `ci.yml` never runs `build/slang/build.sh` (only `release.yml` does). Every PR run therefore
fails `cargo test` with 6 failures. Local gates pass only by accident (the binary exists in this workspace).
**Repair direction:** add the slang build step to `ci.yml` before the gates; keep the tests failing loudly.

### F2 · High (AC-A1) — no GHA layer cache in the `image` workflow
No `actions/cache`, no buildx/build-push action and no `cache-from/type=gha` anywhere; the pinned image is
rebuilt from scratch on every trigger (~3m42s on the runner in this run — it built locally because GHCR had
no image). Contract AC-A1 explicitly requires the GHA layer cache. **Repair direction:** setup-buildx +
`cache-from: type=gha`, `cache-to: type=gha,mode=max` on the build/push step.

### F3 · Low — docs/code conflict in `docs/BUILD.md` §11
It states “CI 只跑 `smoke highlight config fim` 四个套件”, but the CI path (`scripts/build.sh` →
`scripts/build-in-container.sh`) runs **only** `--selftest smoke` (`--probe-hold-ms 2500`).
No AC is affected; the document should match the code.

### F4 · Nit — image fallback cost repeats per PR until `image.yml` publishes
Because GHCR has no image yet, each ci run pays the in-job image build (~3m42s). It becomes a one-off once
the image workflow runs after the first push to `dev` — which also depends on F2 being addressed for the
cache to help.

### F5 · Nit — `AC-A1` runtime path is unproven
The `image` workflow has never run (no runs, no package). Everything about A1 is static except the failed
pull. After merge, the first `push` to `dev` will exercise it; until then A1's runtime (push/tags) is
unverified.

---

## 4. Declined to judge (reason on the same line)

- AC-A5 runtime — needs a tag push and would publish a real release; static review matches every clause, so
  INCONCLUSIVE is the honest verdict.
- AC-A1 runtime push/tags — the workflow is only reachable from a push to `dev`/`main`; nothing to run from
  a PR branch.
- Whether AC-A2 should be downgraded because its first execution failed — the workflow wiring does what A2
  specifies (trigger, pinned image, two scripts, artefact upload, retained logs); the failure is the missing
  slang build, reported under A7/F1.
- `build/slang` build time and its inputs in CI — the release workflow already does it; I did not estimate
  the added CI cost.
- The `real_slang` tests themselves — they fail loudly by design and are correct; the defect is on the CI
  side.
- `packaging/*` BuildKit runtime (`appimage`/`tarball`) — AC-A4 only requires the image build; the T3
  evidence covers the packaging lanes.
- The two historical `DOCKER_BUILDKIT=0` mentions inside `packaging/*/evidence/*.log` — old captured logs,
  not code.

## 5. Verdict

**Request changes.** AC-A2, AC-A3, AC-A4, AC-A6 and AC-B1..B4 pass with the evidence above; **AC-A7 fails on
the real run** (`41036153917` → `conclusion=failure`, failing step “Gates (lock policy, fmt, clippy, test)”,
cause: the slang binary is never built in CI) and **AC-A1 fails** on the missing GHA layer cache (and its
publish path is still unexercised). AC-A5 is INCONCLUSIVE. The two fixes are CI-side only: build slang before
the gates in `ci.yml`, and add buildx + GHA cache to `image.yml` — do not weaken or skip any check.
