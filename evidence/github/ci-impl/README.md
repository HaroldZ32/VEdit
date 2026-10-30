# T3 — CI seam: image ref / pull / skip + BuildKit (evidence)

Contract: `.mpd/plans/ci-and-followups-contract.md` AC-A3 (script seam) and
AC-A4 (BuildKit compatibility). Workflows `.github/**` are the captain's and were
not touched. No git commit was made.

Changed / added (write scope `scripts/`, `packaging/`):

| File | Change |
|---|---|
| `scripts/lib.sh` | `BUILD_IMAGE_TAG="${VEDIT_BUILD_IMAGE_REF:-vedit-build:local}"`; new `ensure_build_image_pulled()` (`VEDIT_BUILD_IMAGE_PULL=1`); `build_build_image()` honours `VEDIT_SKIP_IMAGE_BUILD=1` (reuse or FATAL) and no longer forces `DOCKER_BUILDKIT=0`; BuildKit config-dir fallback when `$HOME/.docker` is not writable |
| `scripts/build-image.sh` | **new** (mode 755): build/fetch only the pinned image, no workspace build; `source lib.sh` + `load_versions` + `build_build_image` |
| `scripts/docker/Dockerfile.build` | repo file is now a context file `COPY vault.repo.in` rendered with `sed` from the pinned `CENTOS_VAULT_BASE` argument (no legacy printf, no heredoc); builds under BuildKit and the classic builder |
| `scripts/docker/vault.repo.in` | **new**: the vault repo file template |
| `packaging/tarball/build.sh` | dropped the forced `DOCKER_BUILDKIT=0` (clean-room image builds with BuildKit; `DOCKER_CONFIG` stays workspace-local) |
| `packaging/appimage/run-evidence.sh` | dropped both `DOCKER_BUILDKIT=0`; sources `scripts/lib.sh`; `--build-arg BUILD_IMAGE="$BUILD_IMAGE_TAG"` instead of the hardcoded `vedit-build:local` (release.yml runs this step with the GHCR ref and without a local `vedit-build:local`, so the old literal would fail) |

## `bash scripts/build-image.sh` — usage, exit codes, missing-image text

Usage: `bash scripts/build-image.sh` (from the repo root; any cwd works).
Environment: `VEDIT_BUILD_IMAGE_REF` (default `vedit-build:local`),
`VEDIT_BUILD_IMAGE_PULL=1` (pull first), `VEDIT_SKIP_IMAGE_BUILD=1` (never build).
Exit 0 when the image exists after the run (built / reused / pulled); exit 1 with
a `FATAL:` line when a pinned input cannot be fetched, the build fails, the pull
fails, or the skip flag points at a missing ref.

`VEDIT_SKIP_IMAGE_BUILD=1` with a missing ref prints exactly:

```
FATAL: VEDIT_SKIP_IMAGE_BUILD=1, but the build image 'vedit-build:definitely-missing' does not exist locally.
       Pull it first (docker pull vedit-build:definitely-missing, or VEDIT_BUILD_IMAGE_PULL=1), or unset VEDIT_SKIP_IMAGE_BUILD to build it.
```

and exits 1 (observed in `01-seam-paths.txt` §2).

## Verification (all commands run in this workspace, pinned image `vedit-build:local`)

| # | Command | Exit | Observed | Evidence |
|---|---|---|---|---|
| 1 | `bash scripts/build-image.sh` (no env) | 0 | `[image] ref=vedit-build:local`, `image_id=sha256:7ca31de0…`, 0.481 s (BuildKit cache) | `01-seam-paths.txt` §5 |
| 2 | `VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-image.sh` | 0 | `reusing the existing image vedit-build:local`, prints ref + image id | `01-seam-paths.txt` §1 |
| 3 | `VEDIT_SKIP_IMAGE_BUILD=1 VEDIT_BUILD_IMAGE_REF=vedit-build:definitely-missing bash scripts/build-image.sh` | 1 | the FATAL block above | `01-seam-paths.txt` §2 |
| 4 | `VEDIT_BUILD_IMAGE_PULL=1 VEDIT_SKIP_IMAGE_BUILD=1 VEDIT_BUILD_IMAGE_REF=docker.m.daocloud.io/library/centos:7 bash scripts/build-image.sh` | 0 | `pulling …` then `reusing …` | `01-seam-paths.txt` §3b |
| 5 | same with `VEDIT_BUILD_IMAGE_REF=alpine:latest` (Docker Hub) | 1 | pull failed (host network reset); script reported `FATAL: cannot pull the build image alpine:latest` — the pull-error path, not a product failure | `01-seam-paths.txt` §3 |
| 6 | **BuildKit cold rebuild**: `VEDIT_BUILD_IMAGE_REF=vedit-build:buildkit-check bash scripts/build-image.sh` | 0 | `buildkit_build_elapsed=219.636 s`, image `sha256:274a6f50…`; `#8` (yum install, i.e. the new COPY+sed repo step) succeeded | `02-buildkit-rebuild.txt` |
| 7 | Rebuilt image facts | 0 | rustc 1.99.0 / cargo 1.99.0 / gtksourceview-4 4.6.1 / gtk3 3.22.30 / glibc 2.17; `/etc/yum.repos.d/vault.repo` identical URLs | `04-rebuilt-image-facts.txt` |
| 8 | RPM set equivalence | 0 | legacy-built vs BuildKit-built: **identical, 382 packages**; both match committed `scripts/docker/rpm-baseline.txt` (`LC_ALL=C diff` clean) | `05-rpm-equivalence.txt` |
| 9 | `VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/gates.sh` (existing image) | 0 | `[image] VEDIT_SKIP_IMAGE_BUILD=1: reusing the existing image vedit-build:local`; `PASS[lock]`; fmt/clippy/test; `GATES PASS (local)` + `GATES PASS (host: … in the pinned image)` | `03-gates-skip-existing.txt` |
| 10 | `VEDIT_SKIP_IMAGE_BUILD=1 VEDIT_BUILD_IMAGE_REF=vedit-build:buildkit-check bash scripts/gates.sh` | 0 | gates pass inside the **BuildKit-rebuilt** image | `06-gates-buildkit-image.txt` |
| 11 | `bash scripts/gates.sh` (no new variables) | 0 | `[image] building vedit-build:local from the pinned inputs` → BuildKit build (cache) → both gate runs pass; 3.841 s warm | `07-gates-no-vars.txt` |
| 12 | Packaging entry `docker build` commands under BuildKit | 0 / 0 / 0 | tarball clean-room 45.3 s; appimage clean-room 59.9 s; appimage package (FROM pinned image) 5.3 s | `08-packaging-buildkit.txt` |

GitHub Actions runs are AC-A7 and belong to the captain's workflow/real-run
evidence; this task verified the script seam locally only.

## Not verified / notes

- The full `bash scripts/build.sh` (release build + Xvfb smoke + GLIBC) was not
  re-run against the rebuilt image; `scripts/gates.sh` (the image's gates) was.
  AC-A2/AC-A7 are the captain's / reviewer's checks.
- The packaging **pipelines** (AppImage assembly, tar.gz staging, clean-room
  acceptance) were not run end-to-end — only the `docker build` commands those
  entries issue, which is what the legacy-builder removal affects.
- `DOCKER_BUILDKIT=0` remains in two tracked historical/spike helpers that are
  **not** among AC-A4's three sites and are not on any build/gates/packaging
  path: `build/gui-spike/scripts/build-all.sh` (2) and `evidence/ime/run-all.sh` (1).
- Fresh BuildKit exports get a new image ID for the same inputs (config
  timestamp); layers, RPM set and image facts were verified identical.
- `scripts/lib.sh` falls back to `DOCKER_CONFIG=$ROOT/target/docker-config` only
  when `$HOME/.docker` is not writable. Pinned base images resolve from the local
  store, so no credentials are needed there; on a normal machine the default
  Docker config (and its registry credentials) is kept.
- Tracked `build/evidence/*.log` files rewritten by the test runs were restored
  with `git checkout --` so the working tree only carries the T3 changes.

---

# T7 — GHA layer cache for the pinned-image build (buildx + cache-from/to)

Review A1 (PR #6 request changes): the image workflow needs a GitHub Actions
layer cache; `docker build` cannot export one.

## Change (`scripts/lib.sh`)

- `build_build_image()` now assembles the 10 `--build-arg`s, `-f`, `-t` and the
   context **once** (`local args=(...)`) and dispatches:
  - **neither cache variable set** → `docker build "${args[@]}"` — identical
    command/output to before (the normal local/gates/release path);
  - **`VEDIT_BUILD_CACHE_FROM` and/or `VEDIT_BUILD_CACHE_TO` set** →
    `require_buildx`, then
    `docker buildx build --load [--cache-from <value>] [--cache-to <value>] "${args[@]}"`;
    the cache flag is only passed when its variable is non-empty.
- New `require_buildx()`: FATAL + exit 1 when `docker buildx version` fails
  ("need the buildx plugin … or unset both variables") or when
  `docker buildx inspect` finds no usable builder (suggests
  `docker buildx create --driver docker-container --name vedit-builder --use`).
- New `build_build_image_with_cache()`: prints
  `[image] build cache: from=… to=… (docker buildx --load)`, pipes through the
  same `tee "$log"`, and on a failed build prints a FATAL hint (builder driver /
  `type=gha` / `docker/setup-buildx-action`) and exits with the build's status —
  no silent fallback to `docker build`.
- `scripts/build-image.sh` and the workflows were not modified; the captain's
  `image.yml` already exports `VEDIT_BUILD_CACHE_FROM: type=gha`,
  `VEDIT_BUILD_CACHE_TO: type=gha,mode=max` and runs
  `docker/setup-buildx-action@v3` (container-driver builder) before the build.

## Verification (all actually run)

| # | Command | Exit | Observed | File |
|---|---|---|---|---|
| 1 | `bash scripts/build-image.sh` (no cache vars) | 0 | `[image] building vedit-build:local from the pinned inputs`, plain `docker build` path (no `[image] build cache:` line), `[image] ref=…` + `image_id=…` | `10-buildx-cache-no-vars.txt` |
| 2 | `VEDIT_BUILD_CACHE_FROM=type=local,src=/tmp/vedit-bx-cache VEDIT_BUILD_CACHE_TO=type=local,dest=/tmp/vedit-bx-cache VEDIT_BUILD_IMAGE_REF=vedit-build:bxcheck bash scripts/build-image.sh` (run 1, cold cache) | 0 | `[image] build cache: from=… to=… (docker buildx --load)`; `WARNING: local cache import … skipped` (dir absent, expected); cache exported: `/tmp/vedit-bx-cache` = 547 MB with `index.json`/`blobs`/`oci-layout`; 1.381 s (daemon cache warm) | `10-buildx-cache.txt` |
| 3 | same command again (run 2) | 0 | cache imported — no `local cache import … skipped` warning in the build output; 8 build steps `CACHED`; 0.703 s | `10-buildx-cache.txt` |
| 4 | cache wanted but buildx unavailable (fake `docker` failing `buildx version` on PATH) | 1 | `FATAL: VEDIT_BUILD_CACHE_FROM/VEDIT_BUILD_CACHE_TO need the buildx plugin, but 'docker buildx version' failed.` | `11-buildx-unavailable.txt` |
| 5 | `VEDIT_BUILD_CACHE_TO=type=does-not-exist bash scripts/build-image.sh` | 1 | buildx: `unknown cache exporter: "does-not-exist"` + the FATAL hint (container-driver builder / setup-buildx-action / unset) | `12-buildx-cache-error-hint.txt` |
| 6 | `bash scripts/gates.sh` (no cache vars) | 0 | `GATES PASS (local)` + `GATES PASS (host: … in the pinned image)`; 225 tests | `13-gates-regression.txt` |

Environment: `github.com/docker/buildx v0.38.1 0dbe87f3…`; the only builder is
`default*` (driver `docker`, BuildKit v0.33.1). **No `docker buildx create --use`
was needed locally**: on this Docker 29.8.2 (containerd image store) the default
`docker` driver already supports `type=local` cache export/import (probe:
`writing cache image manifest … done`, and run 2 above). For `type=gha` the CI
workflow uses `docker/setup-buildx-action`, which creates the container-driver
builder that supports the GitHub cache.

## Not verified / caveats

- `type=gha` was exercised locally **without** the GitHub cache environment:
  buildx v0.38.1 accepted the flag and silently did not write a cache (exit 0,
  no cache-export step). The real GHA cache path is only exercised in GitHub
  Actions (`image.yml`), not on this host.
- The daemon's own BuildKit cache was warm, so run 2's wall time alone does not
  isolate the imported cache; the evidence that the import path worked is
  (a) the `local cache import … skipped` warning present in run 1 and absent in
  run 2, and (b) run 1 writing a valid OCI cache layout. A fully isolated
  cold-cache proof would need a fresh builder (moby/buildkit image / network).
- No GitHub Actions run was made from here (AC-A7).

---

# T8 — build-in-container: illegal four-suite argv (clean-environment build.sh)

## Root cause

`scripts/build-in-container.sh` passed four suite names as **one argv**:
`"$BIN" --selftest "smoke highlight config fim"`, but
`crates/vd-app/src/cli.rs` accepts exactly one suite name (or `all`/`list`).
The old binary rejects it:

```
$ target/release/vd-app --selftest "smoke highlight config fim"
Error: unknown selftest suite 'smoke highlight config fim' (smoke, highlight, config, lsp, slang, fim, all, list)
exit=1
```

(`11-selftest-suites-bug.txt`). The four-suite path had never succeeded — the
committed `build/evidence/12-selftest-suites.txt` only ever held `suite=all`.

## Change (`scripts/build-in-container.sh`, only this file)

- `SELFTEST_SUITE` (string) → `SELFTEST_SUITES` (array):
  `(all)` when both `VEDIT_VERIBLE_LS` and `VEDIT_SLANG` are present, otherwise
  `(smoke highlight config fim)`.
- The summary file is initialised once with the timestamp and the
  `VEDIT_SLANG` / `VEDIT_VERIBLE_LS` / `VEDIT_GSV_DATADIR` fields, then the loop
  appends `suite=`, `exit=`, `report=`, `stderr=` per suite.
- Per-suite reports: `all` keeps the historical
  `12-selftest-suites.json`/`.err`; an individual suite writes
  `12-selftest-<suite>.json`/`.err`.
- Failure semantics preserved: the first non-zero suite prints
  `FATAL: --selftest <suite> exited <rc> (the JSON report is in …)` + the
  stderr tail and exits 1.
- No suite check was touched (the diff touches only the invocation).

## Verification (actually run; the two builds ran in a throwaway copy
## `target/t8-clean` so the shared `target/verible-pin` was not disturbed)

| # | Condition | Command | Exit | Observed | File |
|---|---|---|---|---|---|
| 0 | root cause | `vd-app --selftest "smoke highlight config fim"` | 1 | `unknown selftest suite '…'` | `11-selftest-suites-bug.txt` |
| A | slang present, **no** `target/verible-pin` | `cd target/t8-clean && SOURCE_DATE_EPOCH=… bash scripts/build.sh` | **0** | 49.539 s; `VEDIT_VERIBLE_LS=<unset>`; `suite=smoke exit=0`, `suite=highlight exit=0`, `suite=config exit=0`, `suite=fim exit=0`; per-suite JSON `ok=True` 30/8/6/6 checks | `11-selftest-suites-A.txt`, `-A-console.txt` |
| B | both binaries present | same copy + `target/verible-pin` | **0** | 8.012 s; single `suite=all exit=0`; `12-selftest-suites.json` `ok=True`, 62 checks (smoke 30/highlight 8/config 6/lsp 6/slang 6/fim 6) | `11-selftest-suites-B.txt`, `-B-console.txt` |
| C | failure semantics (fault injection in the throwaway copy: loop pointed at a non-existent suite) | `bash scripts/build.sh` | **1** | `FATAL: --selftest bogus-suite exited 1 (the JSON report is in …)`, CLI error printed, build exits 1 | `11-selftest-suites-failure.txt` |

## Not verified / notes

- No GitHub Actions run from here (AC-A7); the fix was exercised through the
  real `scripts/build.sh` in the pinned image, which is the same path CI runs.
- `target/t8-clean` (≈630 MB, gitignored) is the disposable clean environment
  used for conditions A/B; the reviewable record is under
  `evidence/github/ci-impl/`.
- The main tree's `build/evidence/` was not touched by these runs (the builds
  wrote into the copy).
- `--selftest all` (both binaries) is unchanged and still reports 62 checks.

---

# T9 — fetch-verible.sh: pinned Verible for a clean checkout

## Why

CI run 41039437922: slang built, `real_slang` passed, but **vd-lsp's
integration tests failed** on a clean checkout with

```
missing Verible binary at /vedit-build/crates/vd-lsp/../../.mpd/recon/verible/bin/verible-verilog-ls — set VEDIT_VERIBLE_BIN_DIR
```

`.mpd/` is not in git (harness state), so a fresh checkout has no Verible.
`VEDIT_VERIBLE_BIN_DIR` cannot be forwarded from the host: `run_in_build_container`
does not pass it, and a host path is invalid inside the container. The app's
selftest is affected too: without `VEDIT_VERIBLE_LS*` it can only run the four
binary-free suites instead of `all`.

## Script (`scripts/fetch-verible.sh`, mode 755, idempotent)

1. Downloads `VERIBLE_URL` to `target/verible-pin/verible.tar.gz`
   (`.part` + rename) and verifies `VERIBLE_TARBALL_SHA256`.
2. Extracts it to `target/verible-pin/extract/`, keeping the
   `verible-<version>/bin` layout that `scripts/build-in-container.sh` probes —
   that probe is what upgrades `--selftest` from four suites to `all`.
3. Verifies `verible-verilog-{ls,lint,format}` against the pinned
   `VERIBLE_{LS,LINT,FORMAT}_SHA256` (visible `…: OK` lines on every run).
4. Exposes the bin dir as `<repo>/.mpd/recon/verible/bin`:
   - absent → creates a **relative symlink**
     (`../../../target/verible-pin/extract/verible-<v>/bin`), so the same link
     resolves on the host and inside the pinned image (/vedit-build);
   - existing symlink → relinked;
   - existing directory → kept only when it already holds the pinned binaries,
     otherwise `FATAL` (the script never deletes harness state).
5. Reuses a verified tarball/extract (a re-run is a no-op); any
   download/checksum/extract/verification failure prints `FATAL:` and exits 1.

## Verification (actually run)

| # | Command | Exit | Observed | File |
|---|---|---|---|---|
| 1 | clean condition: `rm -rf target/verible-pin` + harness recon dir moved aside, then `bash scripts/fetch-verible.sh` | **0** | 5.197 s; downloaded the pinned tarball (17,582,801 bytes); `verible.tar.gz: OK`; `verible-verilog-{ls,lint,format}: OK`; linked `.mpd/recon/verible/bin -> ../../../target/verible-pin/extract/verible-v0.0-4298-g085eef05/bin` | `14-fetch-verible-clean.txt` |
| 2 | same command again | **0** | 0.083 s; `reusing the verified tarball`, `reusing the verified binaries`, relinked (idempotent) | `14-fetch-verible-clean.txt` |
| 3 | `bash scripts/gates.sh` | **0** | `GATES PASS (local)` + `GATES PASS (host: … in the pinned image)`; `tests/verible_ls.rs`: 8 passed; `tests/real_slang.rs`: 6 passed | `15-gates-after-fetch.txt` |
| 4 | `bash scripts/build.sh` | **0** | 7.749 s; `build/evidence/12-selftest-suites.txt`: `suite=all`, `exit=0`; `12-selftest-suites.json`: `ok=True`, 6 suites, 62 checks (smoke 30/highlight 8/config 6/lsp 6/slang 6/fim 6) | `16-build-after-fetch.txt` |

The two paths after the clean run:

```
$ ls -ld target/verible-pin/extract/verible-*/bin
drwx------ 2 … target/verible-pin/extract/verible-v0.0-4298-g085eef05/bin
$ ls -l .mpd/recon/verible/bin
lrwxrwxrwx 1 … .mpd/recon/verible/bin -> ../../../target/verible-pin/extract/verible-v0.0-4298-g085eef05/bin
```

## Not verified / notes

- No GitHub Actions run from here (AC-A7). The workflow step
  `bash scripts/fetch-verible.sh` already exists in the captain's
  `.github/workflows/ci.yml` (uncommitted working-tree change at the time);
  `.github/` was not touched by this task.
- The harness directory `.mpd/recon/verible/bin` that this dev machine already
  had was moved aside for the clean-condition test and **restored** afterwards;
  a developer machine with it present exercises the "keep the existing verified
  directory" branch instead.
- Host requirements: `curl`, `tar`, `sha256sum` (all present on GitHub's
  `ubuntu-latest`); no third-party dependency, no test/gate change. The symlink
  target is a fixed `../../../` prefix + the repo-relative path, so no
  `realpath`/platform-specific tool is needed.
- The 17.5 MB tarball is re-downloaded on every clean CI run (a GHA cache for
  `target/verible-pin` would be an optional follow-up).

---

# T10 — base-image pull: bounded retry + Docker Hub digest fallback

## Why

CI run 41042343838 failed in "Resolve the pinned build image" — not code:

```
[image] pulling pinned base image docker.m.daocloud.io/library/centos@sha256:be65f488…
Error response from daemon: … net/http: TLS handshake timeout
FATAL: cannot obtain the pinned base image …
```

The same step had succeeded in other runs, so the mirror is intermittently
unreliable; CI must not go red on that.

## Change

`scripts/lib.sh`:

- `BASE_IMAGE_OVERRIDE` captures an incoming `VEDIT_BASE_IMAGE` **before**
  `versions.env` is sourced (the pin can be pointed elsewhere for a test/mirror
  without editing the pin); with no override the pinned value is used unchanged.
- `VEDIT_BASE_IMAGE_FALLBACK` — fallback registry host, default `docker.io`.
- `BASE_IMAGE_PULL_ATTEMPTS=3`, `BASE_IMAGE_PULL_BACKOFF_SECONDS=2` (2 s / 4 s).
- `base_image_repository()` / `image_ref()` / `image_fallback_ref()` — the
  fallback ref keeps the **same `@sha256:<digest>`**, only the registry host
  changes (`docker.m.daocloud.io/library/centos@sha256:…` →
  `docker.io/library/centos@sha256:…`).
- `pull_base_image()` — `docker pull` with the bounded retry; every attempt is
  logged as `pulling <ref> (attempt N/3)` and a retry line with the delay.
- `ensure_base_image()` — present → unchanged (no pull); otherwise retry the
  primary, then retry the fallback with the same digest; both failing keeps the
  existing `FATAL:` + exit 1; on success it exports `BASE_IMAGE_RESOLVED_REF`.
- `build_build_image()` — passes `--build-arg BASE_IMAGE=<resolved ref>` and
  logs `[image] base image: <ref>`; a normal build passes the pinned ref, i.e.
  the Dockerfile default (identical build graph).

`scripts/docker/Dockerfile.build` (**necessary deviation, see below**):
`ARG BASE_IMAGE=<pinned centos digest>` + `FROM ${BASE_IMAGE}`.

### Why the Dockerfile had to change

The task asked to `docker tag` the fallback image back to the original ref.
That is impossible for a digest reference — proven:
`docker tag <image> repo@sha256:…` → `refusing to create a tag with a digest
reference` (exit 1). And BuildKit does **not** resolve a digest ref from the
local image store when its registry is unreachable — proven with a probe
Dockerfile `FROM localhost:9/library/centos@sha256:<digest>` whose content was
present locally: the build failed trying to reach `localhost:9`. So the resolved
fallback reference is carried into the build as `--build-arg BASE_IMAGE`
instead; the digest is unchanged, so the base layers and the layer cache are
identical.

## Verification (actually run)

| # | Path | Command | Exit | Observed | File |
|---|---|---|---|---|---|
| 1 | normal | `bash scripts/build-image.sh` | **0** | 0.429 s; no `pulling …`/retry lines (image present → unchanged behaviour); `[image] base image: <pinned mirror ref>` | `17-base-image-normal.txt` |
| 2 | fallback | `VEDIT_BASE_IMAGE=localhost:9/library/centos VEDIT_BASE_IMAGE_FALLBACK=docker.m.daocloud.io VEDIT_BUILD_IMAGE_REF=vedit-build:t10-check bash scripts/build-image.sh` | **0** | 6.677 s; primary `attempt 1/3, 2/3, 3/3` with 2 s/4 s backoff (connection refused); `primary registry failed after 3 attempts; falling back to docker.m.daocloud.io: …@sha256:be65…`; fallback pull ok; `the build will use …@sha256:be65… (same pinned @sha256 digest)`; build + tag exit 0 | `18-base-image-fallback.txt` |
| 3a | both down | `VEDIT_BASE_IMAGE=localhost:9/library/centos VEDIT_BASE_IMAGE_FALLBACK=localhost:10 VEDIT_BUILD_IMAGE_REF=vedit-build:t10-fail bash scripts/build-image.sh` | **1** | 3 attempts on each registry; `FATAL: cannot obtain the pinned base image … (primary registry and the localhost:10 fallback both failed)` | `19-base-image-both-fail.txt` |
| 3b | real Docker Hub | `VEDIT_BASE_IMAGE=localhost:9/library/centos VEDIT_BUILD_IMAGE_REF=vedit-build:t10-hub bash scripts/build-image.sh` (fallback unset → `docker.io`) | **1** | 3 primary attempts → `falling back to docker.io: docker.io/library/centos@sha256:be65…` → 3 fallback attempts all fail (`registry-1.docker.io` … `read: connection reset by peer`) → `FATAL … (primary registry and the docker.io fallback both failed)` | `21-base-image-dockerhub.txt` |
| 4 | build-arg proof | Dockerfile default *temporarily* mutated to the unreachable `localhost:9/...`, then `bash scripts/build-image.sh` | **0** | the build resolved `FROM docker.m.daocloud.io/library/centos@sha256:be65…` (the `--build-arg BASE_IMAGE` value), not the mutated default → the arg is genuinely used; Dockerfile restored (sha256 verified) | `20-base-image-buildarg-proof.txt` |

## Docker Hub fallback: really usable?

**Not from this host.** All `docker.io` attempts fail at the TCP level
(`registry-1.docker.io` … IPv6 `read: connection reset by peer`), independent of
this change (a plain `docker pull docker.io/library/centos@sha256:be65…` fails the
same way). So path 2 proves the *mechanism* with a reachable fallback registry
(the pinned daocloud mirror) and path 3b proves the failure semantics with the
real default.

Alternatives if a runner also cannot reach Docker Hub:
1. GitHub-hosted runners do reach Docker Hub, so the default fallback applies there.
2. `VEDIT_BASE_IMAGE_FALLBACK=<another mirror>` reuses any registry that serves
   the same digest (the repo's pin is a public CentOS 7 manifest digest).
3. The bounded retry alone already covers *transient* mirror failures (the
   observed CI error was a TLS handshake timeout), even without a fallback.

## Not verified / notes

- No real `docker.io` fallback *success* on this host (network) and no GitHub
  Actions run (AC-A7).
- The Dockerfile ARG is outside the task's listed write scope (`scripts/lib.sh`,
  `scripts/build-image.sh`); it is required for the fallback to actually make the
  build work, as proven above. A normal build passes the pinned ref, so the
  image workflow's build graph is unchanged.
- `VEDIT_BASE_IMAGE` / `VEDIT_BASE_IMAGE_FALLBACK` default to the pinned
  reference / `docker.io`; unset behaviour is the previous one (no extra pulls).
- The 3-attempt retry costs ~6 s of backoff only when the primary registry is
  down; nothing changes on the happy path.

---

# T11 — `VEDIT_CONTAINER_USER=1`: run the containers as the invoking uid

## Why

CI run 41043881104 passed the gates but failed at the end with

```
cp: cannot create regular file '/home/runner/work/VEdit/VEdit/build/artifacts/vd-app': Permission denied
```

The GitHub runner's docker daemon is **rootful**: the container runs as root and
writes root-owned files into `target/` and `build/`, so `scripts/build.sh`'s
host-side `cp` (as the runner) gets EACCES. Local runs never exposed this because
the local daemon is rootless (container root maps to the calling user).

## Change

`scripts/lib.sh :: run_in_build_container`:

- `VEDIT_CONTAINER_USER=1` → appends `--user "$(id -u):$(id -g)"` to the
  `docker run` (env vars, mounts, workdir unchanged); it also logs
  `[image] container user: <uid>:<gid> (VEDIT_CONTAINER_USER=1)`.
- Unset / `0` → the argv is **exactly** the previous one (no `--user`); the
  rootless-local path is untouched.
- `id -u` / `id -g` unavailable → `FATAL: VEDIT_CONTAINER_USER=1, but 'id -u' is
  unavailable; …` and no container is started (exit status 1).

`scripts/build-in-container.sh` (**necessary deviation from the listed
scope**): the `/etc/machine-id` preparation is now best effort. The image ships
`/etc/machine-id` as `-r--r--r-- root root` with 0 bytes; as a non-root container
user the write fails and `set -e` aborted the whole build. The app starts fine
with the empty file, so a failed write now logs
`[build] /etc/machine-id is not writable as uid …; continuing …`.

The captain's workflows already set `VEDIT_CONTAINER_USER: '1'` at job level
(`ci.yml`, `release.yml`); `.github/` was not touched here.

## Verification (actually run)

| # | Check | Command | Exit | Observed | File |
|---|---|---|---|---|---|
| 1 | default argv | fake `docker` wrapper on `PATH` logs the argv, `run_in_build_container true` | 0 | `docker run --rm -e HOME=… -e CARGO_HOME=… … vedit-build:local true` — **no `--user`** | `22-container-user-argv.txt` |
| 2 | new argv | same with `VEDIT_CONTAINER_USER=1` | 0 | `docker run --rm --user 1000:1000 -e …` — only `--user` added; the container `true` ran as uid 1000 | `22-container-user-argv.txt` |
| 3 | id unavailable | fake `id` returning 1, `VEDIT_CONTAINER_USER=1` | 1 | `FATAL: … 'id -u' is unavailable …`, docker never called | `22-container-user-argv.txt` |
| 4 | default gates | `bash scripts/gates.sh` (argv logged) | **0** | 3.944 s; logged argv has no `--user` | `23-container-user-gates-default.txt` |
| 5 | new path on this rootless daemon | `VEDIT_CONTAINER_USER=1 bash scripts/gates.sh` | **1** (expected) | argv shows `--user 1000:1000`; the container then fails at `scripts/lib.sh: line 43: /vedit-build/scripts/versions.env: Permission denied` — the subuid mapping makes the mounted workspace unreadable to uid 1000 (this is exactly why the default path has no `--user`) | `24-container-user-gates-rootless.txt` |
| 6 | machine-id write | `docker run --user 1000:1000 … printf … > /etc/machine-id` | 1 | `bash: /etc/machine-id: Permission denied` — the failure the tolerance removes | probe C |
| 7 | tolerance + app | same with the tolerance pattern; then `--selftest smoke` as uid 1000 with the empty machine-id | 0 / 0 | write skipped with a log line; the app's smoke selftest runs fine (`smoke_exit=0`, only the known harmless `GLib-GIO-CRITICAL … G_IS_DBUS_CONNECTION`) | probes D/B |
| 8 | syntax | `bash -n scripts/lib.sh scripts/build-in-container.sh` | 0 | — | — |

## Not verified / notes

- **The real effect — workspace files actually owned by the caller — can only be
  observed on a ROOTFUL daemon (GitHub runners).** This machine's daemon is
  rootless, so `--user 1000` maps to a subuid and the mounted workspace becomes
  unreadable (path 5); paths 1–3 prove the argv is assembled exactly as required.
- The machine-id tolerance is the one change outside the "`scripts/lib.sh`"
  scope; without it, `VEDIT_CONTAINER_USER=1` (already enabled in the captain's
  workflows) would abort `scripts/build.sh` in the container (probe 6).
- No GitHub Actions run from here (AC-A7).

---

# T13 — slang build image: bounded retry + Docker Hub same-tag fallback

## Why

CI run 41051285962 failed in "Build the slang deep-lint binary": the slang lane
is a **standalone script** (`build/slang/build.sh`) that pulled
`docker.m.daocloud.io/library/centos:7` itself, and hit the same transient
`net/http: TLS handshake timeout` the T10 fix covered only for
`scripts/lib.sh`.

## Change (`build/slang/`)

- **`build/slang/fetch-image.sh` (new, 755)** — makes the build image available:
  - image already present → **no pull at all**;
  - otherwise `docker pull` with **3 attempts** and 2 s / 4 s backoff, every
    attempt logged;
  - retries exhausted → the **same tag** is pulled from the fallback registry
    (`SLANG_BUILD_IMAGE_FALLBACK`, default `docker.io`; the ref is split into
    registry host + `repo:tag`, so only the host changes) and tagged back as the
    expected ref (`docker tag` with a tag target — valid here, unlike T10's
    digest case);
  - both failing → `FATAL:` + exit 1 (no silent continue).
- **`build/slang/versions.env`** — single source of truth for the two refs:
  `SLANG_BUILD_IMAGE="${SLANG_BUILD_IMAGE:-docker.m.daocloud.io/library/centos:7}"`
  and `SLANG_BUILD_IMAGE_FALLBACK="${SLANG_BUILD_IMAGE_FALLBACK:-docker.io}"`
  (env overrides work, defaults unchanged).
- **`build/slang/build.sh`** — `IMAGE="$SLANG_BUILD_IMAGE"`; the inline
  `image_present`/`docker pull` block is replaced by
  `== stage 0: … == https://… bash "$HERE/fetch-image.sh"`; header documents
  stage 0. Later stages still use `$IMAGE`, so the cache/refs are unchanged.

## Verification (actually run)

| # | Path | Command | Exit | Observed | File |
|---|---|---|---|---|---|
| 1 | normal | `bash build/slang/fetch-image.sh` | **0** | 0.026 s; `image … is already present; no pull needed` | `25-slang-image-normal.txt` |
| 2 | build.sh flow | `PATH=stub:$PATH bash build/slang/build.sh` (only `docker run` stubbed, to skip the multi-minute rebuild; stage 0 and the host ELF checks are real) | **0** | 0.102 s; stage 0 prints `already present; no pull needed`; the argv log has **no `docker pull`**; `BUILD OK: 63cb60a0…` (the pinned `SLANG_BIN_SHA256`, existing verified artifact) | `28-slang-build-flow.txt` |
| 3 | fallback | `SLANG_BUILD_IMAGE=localhost:9/library/centos:7 SLANG_BUILD_IMAGE_FALLBACK=docker.m.daocloud.io bash build/slang/fetch-image.sh` | **0** | 7.315 s; primary `attempt 1/3,2/3,3/3` + 2 s/4 s backoff (connection refused); `primary registry failed after 3 attempts; falling back to docker.m.daocloud.io: docker.m.daocloud.io/library/centos:7`; same-tag pull ok; `tagging … as localhost:9/library/centos:7`; both refs now resolve to `sha256:be65f488…` | `26-slang-image-fallback.txt` |
| 4 | both down | `SLANG_BUILD_IMAGE=localhost:9/library/centos:7 bash build/slang/fetch-image.sh` (fallback unset → `docker.io`) | **1** | 13.206 s; 3 attempts on each; `FATAL: cannot obtain the slang build image … (primary registry and the docker.io fallback both failed)` | `27-slang-image-fail.txt` |

## Not verified / notes

- Docker Hub is **not reachable from this host** (the same IPv6 reset T10
  documented), so path 3 proves the fallback *mechanism* with a reachable
  registry as the fallback, and path 4 records the real `docker.io` failure
  semantics. The real Docker Hub fallback can only be exercised on a GitHub
  runner.
- The full slang rebuild was **not** re-run (multi-minute); path 2 verifies the
  script flow with the container steps stubbed and the previously verified
  artifact, path 1/3/4 exercise the real acquisition logic.
- Tracked `build/slang/evidence/*` files rewritten by the stubbed flow were
  restored with `git checkout --`.
- No GitHub Actions run (AC-A7); `crates/`, `.github/`, `Cargo.*` and `scripts/`
  untouched; no git commit.
