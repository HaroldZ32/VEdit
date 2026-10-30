# T12 — Re-verification of AC-A7 / AC-A1 after the CI rework (commit `000ecee`)

Reviewer · 2026-10-30 · **verdict: request changes** — **A7 is now PASS** (T5's blocker is fixed and
verified on a real run); **A1 still FAILs** on the GHA layer cache and the unexercised GHCR push;
**A6 literal deviation** recorded. A2/A3/A4 and B1..B4 pass.

| item | value |
|---|---|
| reviewed commit | `000ecee` on `ci/github-actions-and-followups` (PR #6), five commits since `9c315e5`: `c6a701e`, `82d1011`, `82e321e`, `ea638b3`, `000ecee` |
| product code | **unchanged since `9c315e5`** (`git diff --stat 9c315e5 000ecee -- crates/` empty) → the T5/T6 AC-B verdicts stand; app.rs sha256 `9fb8cada…` |
| real runs | **ci `41047244806` → success (15m43s)**; **image `41047244151` → success (4m39s)**; both `headSha=000ecee`, event `pull_request` |
| evidence | `evidence/github/ci-verify/a/40-image-run-41047244151.log`, `41-ci-run-41047244806.log`, `43-image-run-41043881105.log`, `44-ci-artifact/` (downloaded run artifact), `50-seams-rework.log` |

Supersedes the T5-REPORT.md verdicts for **A7 (was FAIL → now PASS)** and **A1 (still FAIL, narrower cause)**.

---

## AC-A7 — real run must succeed · **PASS**

`gh run view 41047244806`: `status=completed`, **`conclusion=success`**, 15m43s; every step success:

| step | conclusion |
|---|---|
| Resolve the pinned build image | success |
| Cache/Build the slang deep-lint binary | success |
| Cache/Fetch the pinned Verible binaries | success |
| **Gates (lock policy, fmt, clippy, test)** | **success** |
| Release build, Xvfb selftest and GLIBC assertion | success |
| Hand the workspace back to the runner user / Upload build evidence | success |

- The T5 root cause is gone: `real_slang_*` **6/6 run and pass** (not skipped, not filtered):
  `running 6 tests` → `test real_slang_implicit_net_has_no_ranges_but_keeps_its_location ... ok` …
  `test result: ok. 6 passed`.
- Gates: 21 `test result:` lines, **225 passed, 0 failed** (same count as the local run).
- Slang is genuinely built in CI (from source): `-- slang version: 12.1.0+0`, cmake 3.31.6, Python 3.12.8
  inputs verified (`/work/cache/Python-3.12.8.tgz: OK`, `cmake-3.31.6…tar.gz: OK`).
- Verible is fetched and **sha256-verified** (`verible.tar.gz: OK`, then each of the three binaries `: OK`).
- Build step: `selftest_exit=0`, `selftest JSON valid: suite=smoke ok=True checks=29`;
  the suite collection in the uploaded artifact (`12-selftest-suites.json`) reports
  **`all` = 62 checks, ok=true** (smoke 30, highlight 8, config 6, lsp 6, slang 6, fim 6) — identical
  check names to the local `--selftest all` run (verified by diff, zero differences).
- GLIBC ≤ 2.17 assertion passed; `artifact=build/artifacts/vd-app`; the uploaded `evidence` artifact
  (2,726,176 B) contains both `evidence/` and `artifacts/vd-app`.

## AC-A1 — `image` workflow · **FAIL** (build+cache plumbing is right; the cache does not work and the push path is unexercised)

Now correct and verified: `pull_request` trigger added (build only), `push` dev/main + `workflow_dispatch`
still publish; `docker/setup-buildx-action@v3` with **`driver: docker-container`**, buildx **v0.38.1**,
BuildKit v0.33.1, builder capability `"Cache export": true`; the step exports
`VEDIT_BUILD_CACHE_FROM: type=gha`, `VEDIT_BUILD_CACHE_TO: type=gha,mode=max`; the new seam logs
`[image] build cache: from=type=gha to=type=gha,mode=max (docker buildx --load)` and the build runs on
`builder-… instance using docker-container driver`, ending with `#14 exporting to docker image format`
and `image_id=sha256:5340f0b5…`.

**Unmet 1 — the GHA layer cache has no effect.** Three independent observations:
1. Neither cache-enabled image build (10:52 `ea638b3` and 11:19 `000ecee`) contains any
   `exporting cache` / `importing cache` progress line — only the docker-image exporter/importer.
2. `GET /repos/HaroldZ32/VEdit/actions/caches` → `total_count=3`, `active_caches_size_in_bytes=70,724,484`,
   and the only entries are the three `actions/cache@v4` ones (`cargo-Linux-…`, `slang-Linux-…`,
   `verible-Linux-…`). **No buildx/buildkit cache entry exists.** (`?key=buildkit` → 0.)
3. The two builds are consecutive, same Dockerfile/inputs, yet the second shows **0 `CACHED`** lines and
   the same duration (4m31s → 4m39s), i.e. the full ~4.5 min build is paid every time.

**Unmet 2 — the GHCR push path is still not exercised.** Every `image` run so far is a `pull_request`
run, and the push step is deliberately `if: github.event_name != 'pull_request'` → skipped. No
`workflow_dispatch` run exists; GHCR has no package (the ci resolve step logs
`Error response from daemon: manifest unknown` → `##[warning]published image unavailable; building it in
this job`). Consequently the ci job rebuilds the pinned image locally every PR
(11:19:39 → 11:31:06, ≈ 11.5 min) — the very cost the image workflow exists to remove.

*Direction (not applied):* make the cache actually land — e.g. the supported
`docker/build-push-action` with `cache-from: type=gha`, `cache-to: type=gha,mode=max` (and verify an entry
appears in the Actions cache list), or pass the Actions cache environment to the raw buildx step
explicitly. Then trigger one `workflow_dispatch` image run to publish `:<sha>` + `:latest`.
Acceptance: (a) a buildx cache entry appears, (b) a second image run logs `CACHED`, (c) a later ci run
logs `using the published image ghcr.io/haroldz32/vedit-build:latest`.

## AC-A2 — `ci` workflow · **PASS**

Wiring unchanged and now exercised end-to-end: `pull_request`/`push: dev` triggers, pinned-image resolve
(pull or local build fallback), `scripts/gates.sh` + `scripts/build.sh` with `VEDIT_SKIP_IMAGE_BUILD=1` and
the resolved ref, artifact upload of `build/evidence/` **and** `build/artifacts/vd-app` with `if: always()`.
The real run executed every step successfully and the artifact was uploaded. Failure-log retention is
unchanged (upload is `if: always()`; the run log is retained by GitHub).
*Note:* this run also proves the new pre-gates steps (slang build, verible fetch) are correctly ordered.

## AC-A3 — script seams · **PASS** (re-verified on `000ecee`)

`evidence/github/ci-verify/a/50-seams-rework.log`:

| case | exit | observed |
|---|---|---|
| `bash scripts/build-image.sh` (no vars) | **0** | `[image] building vedit-build:local from the pinned inputs` … `[image] ref=vedit-build:local` — default path unchanged |
| `VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/gates.sh` | **0** | `reusing the existing image vedit-build:local` → `GATES PASS (local)` / `GATES PASS (host: …)` |
| skip=1 + `VEDIT_BUILD_IMAGE_REF=vedit-build:definitely-missing` | **1** | the exact `FATAL: VEDIT_SKIP_IMAGE_BUILD=1, but the build image … does not exist locally.` |
| `VEDIT_BUILD_IMAGE_PULL=1` + unpublished local tag | **1** | `[image] pulling vedit-build:local (VEDIT_BUILD_IMAGE_PULL=1)` → `FATAL: cannot pull the build image …` |
| `VEDIT_CONTAINER_USER=1` (rootless local daemon) | **0** | `[image] container user: 1000:1000` and the container ran as that uid |
| cache vars + unusable builder (default docker driver) | **1** | buildx refuses, script prints `FATAL: the buildx build failed (exit 1); the requested cache was not usable.` + the builder hint — **no silent fallback** |

The cache vars are additive: with neither set, `lib.sh` still runs the plain `docker build` (the echo/fallback
asymmetry is documented in the function). Default local behaviour is unchanged.

## AC-A4 — BuildKit · **PASS**

The three forced `DOCKER_BUILDKIT=0` are still gone. BuildKit builds are now demonstrated twice on real
infrastructure: the image workflow (docker-container driver, above) and the ci job's in-job image build
(`#0 building with … using docker driver`). `Dockerfile.build` now takes `ARG BASE_IMAGE` with the pinned
reference as its default, so a normal build is identical; the retry/Docker Hub fallback reuses the same
`@sha256` digest.

## AC-A5 — `release` workflow · **INCONCLUSIVE** (unchanged)

Static review still matches every clause (trigger `tags: v*`, `contents: write`, `fetch-depth: 0`, order
slang → build → AppImage → tar.gz → SHA256SUMS incl. the bundled verible trio + slang, 4 files uploaded;
`VEDIT_CONTAINER_USER: '1'` and the Verible fetch were added to this workflow too). No tag exists for this
work → not executed; a `workflow_dispatch` would publish a real release, so it is not attempted.

## AC-A6 — runner hygiene · **FAIL (literal deviation, Low)**

The chown steps are present and ran successfully in the failed T5 run and again here. **Cache scope
changed:** `ci.yml` now caches `build/slang/out` and `target/verible-pin` in addition to
`target/cargo-home` + `target/home`. The AC's text says *"缓存只覆盖 `target/cargo-home` 与 `target/home`"*,
so the literal clause is not met; the intent clause (*"不缓存 release 产物与打包输出"*) **is** met — these are
build inputs (the deep-lint binary and the pinned Verible download), not release artefacts.
*Direction:* either amend AC-A6 to list the two build-input caches, or drop them (which would make A7's
slang/verible steps re-download/rebuild every run). No functional risk either way.

## AC-B1..B4 — unchanged and still **PASS**

`git diff 9c315e5 000ecee -- crates/` is empty, so the F1/F2 code and assertions are exactly the ones
verified in T5/T6 (`app.rs 9fb8cada…`, R1's segment-equality tightening included). Cross-checked again
through the CI artifact: the CI `--selftest all` reports **62/62** with **exactly the same check names**
as the local run (diff: zero differences) and `cargo test --workspace` = **225 passed**.

---

## Findings

### R1 · Blocker (AC-A1) — the GHA layer cache does not materialise
See AC-A1 above: no cache exporter/importer lines, no buildx cache entry in the Actions cache API, no
`CACHED` on a repeat build. The workflow and seam are correct in shape, but the clause "含 GHA layer cache"
is not delivered. Acceptance test stated above.

### R2 · High (AC-A1) — GHCR publish is still unexercised; every PR pays an 11.5-minute local image build
No `workflow_dispatch` image run, no package in GHCR, `if: github.event_name != 'pull_request'` skips the
push on PRs. Trigger one manual run (or merge to `dev`, whose push matches `image.yml` paths) and then
confirm a subsequent ci run pulls the image.

### R3 · Low (AC-A6) — two additional cache paths vs the frozen AC wording
`build/slang/out` + `target/verible-pin`; intent met, literal text not. Amend the contract or drop the
caches.

### R4 · Nit — standalone Xvfb smoke reports 29 checks, the `all` run 30
The difference is exactly the smoke check `VEDIT_SLANG is configured for the SLANG suite`, which is
conditional on `VEDIT_SLANG` being exported before the window smoke runs. Not a skip and not a weakening:
the `all` run contains all 30 smoke checks and passes.

## Declined to judge

- The platform-level cause of the missing buildx cache (could not reproduce locally: the container-driver
  builder cannot pull `moby/buildkit` from this host — Docker Hub connection resets; the observable
  absence of any cache entry/hit is the evidence used).
- AC-A5 runtime — needs a tag and would publish a real release.
- Whether the extra caches should be allowed — the captain owns the contract text.
- The `slang`/`verible` build-time cost itself (a cache miss is expensive by design; the cache is the fix).

## Verdict

**Request changes** — AC-A7 is fixed and independently verified on the real run (`41047244806`, success);
AC-A2/A3/A4 and AC-B1..B4 pass; AC-A5 remains INCONCLUSIVE. Two CI-side items still block A1: make the
GHA layer cache actually export/import (and show a `CACHED` second run), and exercise the GHCR publish
path once. AC-A6 needs either a contract amendment or the extra caches removed. Nothing was fixed by the
reviewer; no product code, script or workflow was modified and no commit was made.
