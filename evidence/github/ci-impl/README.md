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
