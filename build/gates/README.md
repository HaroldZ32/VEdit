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
- `tests`, `typecheck`, `manifest` need the Rust workspace that T7 creates
  (`Cargo.toml`, `crates/*`). Until that lands they exit 1 with the reason, so a
  reviewer sees an honest red rather than a skip.
- `docs`, `vendor`, `dist`, `pack` check for the artifacts their owning tasks
  (T29, T23, T25) must produce; they turn green on their own once those exist.
- `comments` and `rows` have no meaning in this product (they are MPD-bundle
  gates), so they always exit 1 naming that fact.

`GATES_REBUILD=1 bun run verify:gates` forces a full rebuild instead of
re-verifying the existing `build/slang/out/slang`.
