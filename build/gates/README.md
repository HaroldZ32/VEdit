# Verification gates

These scripts are the repository's real gates, wired to the MPD gate ids through
`package.json` (`bun run verify:<id>` -> `build/gates/<id>.sh`). The tool that
records verification evidence runs `bun run verify:<id>` in the repository root.

Rules:

- **A gate never fakes green.** When the project has no such gate yet, the script
  prints `本项目无此门禁` plus the reason and exits 1.
- `gates` is the only gate that is green at the slang-spike milestone: it builds
  the slang artifact with one command (`build/slang/build.sh`, a fresh CentOS 7
  container) and asserts the ELF needs no symbol newer than `GLIBC_2.17` and no
  `libstdc++`/`libgcc_s`.
- **Two lanes are covered.** The Rust/GTK product (T7) and the Qt6/KF6 C++ product
  being built for the rewrite (T10) both run here while both exist. The C++ lane
  is `build/gates/qt6-cxx.sh`: it builds the pinned Qt6 image
  (`scripts/docker/Dockerfile.build`, stage `qt6-build`), then configures,
  compiles, runs ctest and an xcb + software-rendering window smoke **inside that
  image** (`scripts/build-qt.sh`). `typecheck` runs its `--no-test` form, which is
  the C++ static check (the lane's own code is compiled with
  `-Wall -Wextra -Wpedantic -Werror`).
- `tests`, `typecheck`, `manifest` need the Rust workspace that T7 creates
  (`Cargo.toml`, `crates/*`). Until that lands the Rust half exits 1 with the
  reason, so a reviewer sees an honest red rather than a skip.
- `docs`, `vendor`, `dist`, `pack` check for the artifacts their owning tasks
  (T29, T23, T25) must produce; they turn green on their own once those exist.
- `comments` and `rows` have no meaning in this product (they are MPD-bundle
  gates), so they always exit 1 naming that fact.

`GATES_REBUILD=1 bun run verify:gates` forces a full rebuild instead of
re-verifying the existing `build/slang/out/slang`.

The Qt6 lane has its own switches (see `scripts/build-qt.sh`):
`VEDIT_SKIP_IMAGE_BUILD=1` reuses an existing `vedit-qt6-build:local`
(a missing image is a `FATAL`, never a skip), and
`VEDIT_QT6_BUILD_IMAGE_PULL=1` pulls a published image instead of building.

## Exit-0 rule for `gates`

`gates.sh` may exit 0 only after it verified something that really exists: the
slang artifact, its `GLIBC <= 2.17` symbol bound, and its offline smoke test. The
folded-in `cargo test --workspace` step is allowed to print
`N/A: no Cargo workspace yet (T7)` **only while `Cargo.toml` is absent**. The
moment the Rust workspace exists, the same step becomes a hard requirement: a
test failure fails the gate. Printing N/A and exiting 0 after T7 is fake green
and is treated as a violation.

## Reachability in the MPD ledger

The evidence tool's gate table uses fixed argv. Measured reachability:

| gate id | argv the tool runs | reachable |
|---|---|---|
| `gates`, `docs`, `manifest`, `comments`, `rows` | `bun run verify:<id>` | yes |
| `typecheck` | `bun run typecheck` | yes, via the `typecheck` alias in package.json |
| `tests` | `bun test packages` (hardcoded) | **no** - a Rust product can never match; the ledger-visible test run is folded into `gates` |
| `vendor`, `dist`, `pack` | `node scripts/verify-*.ts` (hardcoded path) | **no** - node hits ENOENT before our script; no evidence can be produced from them |

The unreachable scripts stay in the tree for humans running them by hand; they are
not evidence sources and must not be claimed as such in a verification record.
