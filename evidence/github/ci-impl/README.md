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
