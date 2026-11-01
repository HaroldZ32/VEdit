# C9 backfill (T112/N2): header added after the fact; the body below is unchanged.
# original_collected_at_utc: 2026-11-01T07:53:37Z   (from the archived file mtime)
# original_git_head: 9017685 (phase-1 skeleton baseline)
# original_git_status_porcelain_sha256: not captured at collection time
# session_anchor: none: phase-1 logs recorded time+HEAD, not a full porcelain block
# backfill_collected_at_utc: 2026-11-01T09:23:35Z
# backfill_git_head: 0c8ea9fe4ae18c386a59a55b8868f837f4f80a84
# backfill_git_status_porcelain_sha256: a7feab0230532310375511329de129cad97d49e690c7a9c7336394a1cce151fa (current tree at backfill time)
# T31 — phase 1 (pipeline only): skeleton baseline `9017685`

C9 header: every log in this directory carries its own `collected_at`,
`git_head` and (in `01`/`05`) `git status --porcelain`. This report was written
on **HEAD `9017685`**, which is a **skeleton baseline, not the delivery
baseline** (T106's async-FIM change is still pending; Lead will freeze the final
SHA). **No final AppImage / SHA256SUMS / matrix was produced in this phase, and
T31 is not completed.**

## What changed (pipeline, phase 1)

| file | change |
|---|---|
| `packaging/runtime/assemble-appdir.sh` | installs the Qt product `target/qt6-build/src/app/vedit-app` as **`bin/vd-app`** (the M0 smoke binary and its testdata are gone); **requires** Qt platform plugins `libqxcb.so` + `libqoffscreen.so` (explicit copy; `ldd` cannot see dlopen’ed plugins); adds `bin/vd-app` to the dependency closure; requires the bundled toolchain (`--require-toolchain` in `build.sh`); asserts delivery invariants at the end |
| `packaging/runtime/AppRun` | starts **only** `bin/vd-app`; `--selfcheck` forwards to the product’s own self-check (the M0 smoke-specific `--testdata/--ksh-selfbuild/--expect-*` arguments are gone) |
| `packaging/runtime/selfcheck.sh` | delivery assertions: `APP-SHELL-SELFTEST ok=1`, `TOOLCHAIN source=verible:bundled slang:bundled missing=[]`, own XML + `verilog=1`, `EDIT-CORE-SELFTEST ok=1`, folding-975; plus a **real `/proc/<pid>/maps` loader proof** (packaged loader + libc, no system loader) |
| `packaging/runtime/build.sh` | preflight uses `vd-app`; assembles with `--require-toolchain`; writes evidence to `evidence/slice3-pack/`; adds a delivery-invariant step (plugins/toolchain/static FFI/readelf); fetches the pinned `appimagetool` when its cache is empty; writes `out/SHA256SUMS`; CentOS probe uses the product selfcheck |
| `packaging/runtime/verify-not-shipped.sh` | accepts the documented `--expect-clean` spelling (same delivery mode; no criterion change) |
| `packaging/runtime/verify-f2-isolation.sh`, `selfcheck.sh` | make the AppDir/AppImage path absolute before Docker mounts (a relative `--appdir` used to make the probe silently produce no output) |
| `packaging/runtime/README.md` | documents the delivery form, the explicit plugins, the dual XML layout and the phase distinction |
| `packaging/appimage/README.md` | clear banner: that pipeline packages the **legacy GTK `vd-app`**; the Qt delivery comes from `packaging/runtime/build.sh` |

### Discovered product/packaging mismatch (worked around, follow-up needed)

T84 fixed `VEDIT_SYNTAX_DIR` to the **data root** (the syntax-product contract
`<root>/syntax/*.xml`). The editor’s `FoldBreadcrumbBinding::definitionDirectory()`
still probes `<VEDIT_SYNTAX_DIR>/vedit-*.xml`, i.e. it assumes the **syntax
subdirectory**; with the root value the package produced `hidden=0 /
registry=0` and the product selfcheck failed. Packaging now installs our two
XMLs at **both** the data root and `<root>/syntax/` (inert for
KSyntaxHighlighting, which scans only `<root>/syntax`); the product-side probe
should be fixed in a follow-up (out of T31’s write scope). This is why
`fold.wiring.closed.hidden-975` passes in the package now.

## Phase-1 validation (skeleton, not a delivery claim)

| check | observed |
|---|---|
| assemble (`01-assemble-skeleton.log`) | `ASSEMBLE-EXIT=0`, 204 files / 283,399,121 bytes, `delivery invariants` logged |
| `selfcheck.sh --appdir` on the host (`02`) | `SELFCHECK PASS`, all assertions pass incl. loader/libc maps |
| `selfcheck.sh --appdir` in **stock centos:7** (`08`) | `SELFCHECK PASS`, same 9 checks; container is stock (no KF6/Qt/verible/slang) |
| `verify-not-shipped.sh --expect-clean` (`03`) | `pass=8 fail=0 expected_red=0`, `DELIVERY_BLOCKED=no` |
| F2 isolation (`04`, with simulated KDE host) | `pass=5 fail=0`, reverse control leaks `defs=403` |
| invariants (`05`) | `bin/vd-app` + `bin/verible-verilog-{ls,lint,format}` + `bin/slang` + `libqxcb.so`/`libqoffscreen.so` + katepart; FFI is static (`vd_*` symbols = 44, `libvd_ffi` NEEDED = 0); `VEDIT_SYNTAX_DIR` = data root; toolchain sha256 equals the `versions.env` pins |
| scripts / gates (`07`) | `bash -n` all OK; root `ctest` **25/25 PASS**; `typecheck` **PASS** |

GLIBC note: `vd-app` requests up to `GLIBC_2.39`; the AppDir ships `libc.so.6`
(sha256 `85e64f97…`) and `ld-linux-x86-64.so.2` (sha256 `bda779ea…`) and the
maps proof shows they are the ones used on stock CentOS 7. This is the
pre-existing bundled-runtime architecture (C2); no host glibc is used.

## Phase 2 (blocked on the Lead)

1. Wait for the final packaging baseline SHA (after T106 + Lead commit).
2. Re-run `bash packaging/runtime/build.sh` on that baseline → single-file
   `VEdit-0.1.1-x86_64.AppImage` + `out/SHA256SUMS`; record path/size/sha256 and
   re-run the clean-container selfcheck on the **AppImage** (not just the AppDir).
3. Four-host matrix: only container hosts are available here. **There is no
   real CentOS 7 (kernel 3.10) machine**; a container shares the host kernel, so
   it must be reported as “容器矩阵，真 3.10 真机未测”, not as four-host
   verification. The existing `tests/packaging/hostmatrix.sh` targets the M0
   smoke probe and is outside T31’s write scope; phase 2 needs its runtime
   adaptation/run. If the user’s VM is needed for the real 3.10 row, ask the
   Lead/user first.
4. No commit/push; T31 stays **in progress** until phase 2 is accepted.

## Evidence index

| file | content |
|---|---|
| `01-assemble-skeleton.log` | assemble with vd-app + toolchain + plugins + invariants |
| `02-selfcheck-appdir.log` | host selfcheck (all green) |
| `03-not-shipped.log` | `--expect-clean`, 8/0, not blocked |
| `04-f2-isolation.log` | isolation + reverse control |
| `05-delivery-invariants.log` | binaries, plugins, static FFI, readelf, glibc/loader, toolchain pins |
| `06-report-phase1.md` | this report |
| `07-gates.log` | bash -n, root ctest 25/25, typecheck PASS |
| `08-clean-container-selfcheck.log` | same selfcheck inside stock centos:7 |
