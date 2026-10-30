# T14 — Final verification: AC-A1 layer cache + GHCR publish (merge `5b818bd`)

Reviewer · 2026-10-30 · **verdict: approve** — T12's two A1 blockers are closed and proven end-to-end;
A5 stays INCONCLUSIVE by design until a tag.

| item | value |
|---|---|
| reviewed state | `5b818bd` = PR #6 merged into `dev` (parents include `2b3e540`); branch head `2b3e540` |
| new commits | `9038785` ci(image): persist the buildx layer cache through actions/cache · `2b3e540` ci(slang): retry the image pull and fall back to Docker Hub by tag · `5b818bd` merge |
| product code | **untouched since the verified F1/F2 tree** (`git diff --stat 9c315e5 5b818bd -- crates/` empty; app.rs sha256 `9fb8cada…`) |
| real runs | **image `41056011790`** (push→dev) success 5m52s · **ci `41056011838`** (push→dev) success 14m55s · PR image `41053154474` (2b3e540) success **1m40s** · PR ci `41053154326` (2b3e540) success 17m16s |
| contract | AC-A6 **amended by the captain at 19:52** to allow build-input caches; all other AC text unchanged |
| evidence | `evidence/github/ci-verify/a/60-image-dev-run-41056011790.log`, `61-ci-dev-run-41056011838.log`, `62-image-pr-run-41053154474.log`, `63-actions-caches.txt`, `64-runs-summary.txt` |

---

## AC-A1 — `image` workflow: BuildKit + GHA-persisted layer cache + GHCR publish · **PASS**

**Publish (previously unexercised, now proven).** The run triggered by the merge push to `dev`
(`41056011790`, event `push`) executed the push step successfully:

```
5b818bd8ecb32e2738f2957f857a96f9c2844a3c: digest: sha256:00596edd1fc5a7ef867a87f39414bf5c2b8d422ad51024f9999d8b673ac9bbde size: 2215
latest:                                     digest: sha256:00596edd1fc5a7ef867a87f39414bf5c2b8d422ad51024f9999d8b673ac9bbde size: 2215
published ghcr.io/haroldz32/vedit-build:5b818bd8ecb32e2738f2957f857a96f9c2844a3c and ghcr.io/haroldz32/vedit-build:latest
```
PR runs still skip the push (`Push to GHCR … skipped` on `41047244151`), so the "never push from a pull
request" condition is intact.

**Layer cache (previously ineffective, now proven).** The `type=gha` buildx backend is replaced — with the
reason documented in the workflow — by `actions/cache` persisting `/tmp/vedit-buildx-cache` and buildx
`type=local` import/export. Evidence:
- PR image run `41053154474` (2b3e540): `#4 importing cache manifest from local:12571…` followed by
  **seven `#7…#13 CACHED`** lines; the run took **1m40s** versus **6m14s** for the otherwise identical cold
  run `41051285965` immediately before it.
- The repository's Actions cache now holds **two dedicated `buildx-Linux-e3212029…` entries**
  (566,104,506 B for `refs/heads/dev` and 566,567,272 B for `refs/pull/6/merge`) — total cache list is
  9 entries (`63-actions-caches.txt`).
- The dev run itself exported the cache for the dev scope: `#16 exporting cache to client directory`.
  It shows **0 `CACHED`** because GitHub does not let `refs/heads/dev` read caches created under a PR ref;
  that is normal platform scoping (N3), not a cache failure — the PR run above proves the hits.

BuildKit is still used: `docker/setup-buildx-action@v3` with the `docker-container` driver, and the build
line is `#0 building with "builder-…" instance using docker-container driver`.

*Judgement note:* the AC wording says "含 GHA layer cache". The cache is persisted in the GitHub Actions
cache service and demonstrably reused, via `actions/cache` + `type=local` instead of buildx's own `type=gha`
backend (which produced no cache entry on these runners — the T12 finding). I judge the clause **met in
substance**; the implementation choice is documented in `image.yml`.

## AC-A7 — real runs succeed · **PASS**

- PR-triggered ci runs: `41047244806` (000ecee) success 15m43s and `41053154326` (2b3e540) success 17m16s.
- The merge push on `dev`: ci `41056011838` success 14m55s, every step success (resolve → slang → verible →
  gates → release build → chown → upload).
- Gates in the dev run: **225 passed** (21 `test result:` lines), `real_slang*` all ok.
- Raw `gh run list` / `gh run view --json` output: `64-runs-summary.txt` (+ earlier logs in this directory).

## AC-A2 — `ci` workflow used the published image end-to-end · **PASS**

The dev ci run resolved the published image instead of rebuilding it:
```
Digest: sha256:00596edd1fc5a7ef867a87f39414bf5c2b8d422ad51024f9999d8b673ac9bbde
Status: Downloaded newer image for ghcr.io/haroldz32/vedit-build:latest
using the published image ghcr.io/haroldz32/vedit-build:latest
```
That digest is **exactly** the one the image workflow pushed. The gates step then ran with
`VEDIT_SKIP_IMAGE_BUILD: 1`, `VEDIT_BUILD_IMAGE_REF: ghcr.io/haroldz32/vedit-build:latest`
(`reusing the existing image ghcr.io/…`). The release-build step reported
`selftest JSON valid: suite=smoke ok=True checks=29`, `artifact=build/artifacts/vd-app`, and
`OK: highest GLIBC symbol is GLIBC_2.16 (<= 2.17); no libstdc++ dependency`. The `evidence` artifact
(2,702,381 B) was uploaded. This closes the previous fallback path (every PR used to rebuild the image
locally, ≈11.5 min).

## AC-A3 — script seams · **PASS** (carried)

`scripts/` is byte-identical to the state verified in T12 (`git diff --stat 000ecee 5b818bd -- scripts/`
empty): default path unchanged, `VEDIT_SKIP_IMAGE_BUILD=1` reuse, missing ref → FATAL exit 1,
`VEDIT_BUILD_IMAGE_PULL=1` → pull then FATAL on failure, `VEDIT_CONTAINER_USER=1` → `--user <uid>:<gid>`,
cache vars with an unusable builder → loud FATAL (no silent fallback). The seams are now exercised in the
real runs as well (skip+ref pulling the GHCR image).

## AC-A4 — BuildKit · **PASS**

No `DOCKER_BUILDKIT=0` in `scripts/`/`packaging/` (only two historical log lines mention it). The image
build runs on the buildx container driver (above) and both real workflows build successfully under BuildKit.
`Dockerfile.build` keeps the pinned `ARG BASE_IMAGE` default, and the main image keeps digest-based retry
+ Docker Hub **digest** fallback.

## AC-A5 — `release` workflow · **INCONCLUSIVE** (unchanged)

Text unchanged; static review still matches every clause (tags `v*`, `contents: write`, `fetch-depth: 0`,
slang → build → AppImage → tar.gz → SHA256SUMS incl. bundled verible trio + slang, 4 files). No tag exists
for this work, and a `workflow_dispatch` would publish a real release, so it is not attempted. This is the
only open item and it is closed by the first real tag.

## AC-A6 — runner hygiene · **PASS** (against the amended text)

Chown steps `if: always()` in ci/release (ran successfully in every observed run, including the failed
T5 one); cached paths are exactly the allowed build inputs — `target/cargo-home`, `target/home`,
`build/slang/out`, `target/verible-pin` — and **no release artifacts, `build/artifacts` or packaging
outputs** are cached (`63-actions-caches.txt`). The amendment (19:52) resolves the T12 deviation.
*N1:* the new buildx layer cache (`/tmp/vedit-buildx-cache` via `actions/cache`) is a **fifth** cache not
enumerated in the amended AC-A6 — recommend adding it to the list (same intent: build input, not a release
artifact).

## AC-B1..B4 — still **PASS**

`git diff --stat 9c315e5 5b818bd -- crates/` is empty: the F1/F2 implementation and the regression
assertions are exactly the ones verified in T5/T6/T12 (app.rs `9fb8cada…`, R1's segment-equality
assertion). The dev ci run re-confirms 225 tests and the smoke suite in the same binaries; the `--selftest
all` suite in the T12 artifact matched the local 62 check names exactly.

---

## Findings (all non-blocking)

### N1 · Low — AC-A6 enumeration does not list the buildx layer cache
`actions/cache` now persists `/tmp/vedit-buildx-cache` (566 MB per scope). It is a build input, consistent
with the amended intent, but the AC's explicit list should gain it so the contract matches the workflow.

### N2 · Low/Nit — slang build image is tag-pinned while the main image is digest-pinned
`build/slang/fetch-image.sh` falls back to `docker.io/library/centos:7` **by tag** (the primary
`docker.m.daocloud.io/library/centos:7` was already tag-based). The main build image falls back by the same
**@sha256 digest**; for consistency (and reproducible slang binaries) consider pinning the slang build image
by digest too, falling back by digest.

### N3 · Info — the dev image run shows 0 `CACHED` on purpose
GitHub does not allow `refs/heads/dev` to read caches created under `refs/pull/6/merge`; the dev run built
cold (5m52s) and **seeded** the dev-scoped cache. The PR run `41053154474` is the proof that the cache
works (7 `CACHED`, 1m40s vs 6m14s). The next push to `dev` should hit.

### N4 · Info — the dev ci job was queued ~24 min (12:31:41 → 12:55:52)
Runner scheduling, not a workflow defect; end-to-end wall time from push to green was ~39 min.

### N5 · Info — `9038785`'s ci run failed once (4m50s) before `2b3e540`
That was the transient base-image pull failure the next commit fixed with retry + Docker Hub fallback; all
runs after it are green (T12's earlier note stands).

## Declined to judge

- AC-A5 runtime — needs a real tag; a dispatch would publish a release (the one remaining open item).
- Whether `actions/cache`-persisted `type=local` satisfies the letter of "GHA layer cache" — judged met in
  substance (the cache lives in the Actions cache service and is reused); recorded for the captain.
- The runner queue time and the one transient pull failure — platform behaviour, already fixed.
- The slang build-image pinning policy — the owner's call; recorded as N2.
- The `v*` release flow itself — out of reach until a tag is cut.

## Verdict

**Approve.** T12's R1 (cache ineffective) and R2 (GHCR publish unexercised) are closed with end-to-end proof:
the image workflow pushes `:<sha>` + `:latest` (same digest), the layer cache is persisted in the GitHub
Actions cache and demonstrably reused (`CACHED`, 1m40s vs 6m14s), and the `ci` workflow pulls exactly that
digest and runs the gates/build in it. AC-A2/A3/A4/A6/A7 and AC-B1..B4 pass; AC-A5 remains INCONCLUSIVE
until a tag, as agreed. Nothing was modified by the reviewer; no commit was made.
