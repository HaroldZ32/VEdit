# T3 — slang 12.1 built for CentOS 7 (glibc 2.17): build spike report

Cell `cell-20261030T020634155Z` · task **T3** (slang build spike) · owner **Senior Engineer**
Write scope: `build/slang/**` + the captain-approved additions `package.json`,
`build/gates/**`, `.gitignore`, `AGENT.md §4`.
Governing criteria: the T3 dispatch brief's four observable success criteria (the
cell record's `contracts/T3.md` is the split-key T1 skeleton contract, i.e. not
this task; the captain confirmed the mapping gap and the dispatch brief governs).

## 0. Verdict

**All four criteria are met.** `bash build/slang/build.sh` (one command, fresh
`docker.m.daocloud.io/library/centos:7` container) exits **0** and produces:

- `build/slang/out/slang` — 9,526,400 bytes (stripped; 12,244,592 unstripped),
  sha256 `63cb60a05f6ca57f648b8e2ef6de61ee0a3132c1c9d481f831dba52388832252`,
  highest versioned GLIBC symbol **`GLIBC_2.17`** (the CentOS 7 baseline, i.e. no
  headroom), no `libstdc++`/`libgcc_s` dependency (both static).
- Evidence: every file in `build/slang/evidence/`, indexed with the exact command
  and exit code in **`build/slang/evidence/00-INDEX.md`**.
- Full clean run: **204 s** wall clock on 16 CPUs (yum 46 s, configure 16 s,
  compile+link 97 s, rest CPython build + clone + patch + artifact).

| criterion (dispatch brief) | result | evidence |
|---|---|---|
| 1. `build/slang/build.sh` reproducible in a clean container | PASS — stock centos:7 with `--rm`, no pre-baked image, exit 0 | `00-build-console.log`, `01-yum-install.log`, `02-toolchain.txt` |
| 2. `objdump -T` highest GLIBC symbol ≤ 2.17 | PASS — `GLIBC_2.17` (all referenced: 2.2.5, 2.3, 2.3.2, 2.4, 2.6, 2.14, 2.17) | `05-elf-check.txt`, `12-host-elf-verification.txt` |
| 3. offline stock centos:7: `--version` ok, and `--lint-only -Weverything --diag-json` on an undeclared-identifier fixture yields a JSON array | PASS — `--version` exit 0 (`slang version 12.1.0+0`), fixture lint exit **1**, JSON **array** with 1 diagnostic (`UndeclaredIdentifier`), clean fixture exit 0, `--network none` container | `09-offline-smoke.txt`, `10-clean.out`, `11-diag-text.out`, `11-diag.json` |
| 4. REPORT records commands, toolchain versions, build time, size, sha256 and raw output | PASS — this file + `00-INDEX.md` | — |

## 1. What the build does (exact commands)

Build driver: `bash build/slang/build.sh` — stage 1 runs
`scripts/build-in-container.sh` inside a fresh stock `centos:7`; stage 2 re-checks
the ELF on the host; stage 3 starts a **second** fresh stock `centos:7` with
`--network none` and runs `scripts/verify-offline.sh`.

Inside stage 1 (all pinned in `versions.env`):

1. Vault mirror `https://mirrors.tuna.tsinghua.edu.cn/centos-vault/7.9.2009`
   (stock repo files are dead) → `yum install devtoolset-11-gcc-c++ devtoolset-11-binutils
   devtoolset-11-runtime devtoolset-11-libstdc++-devel git make tar gzip which file
   ca-certificates zlib-devel openssl-devel bzip2-devel xz-devel libffi-devel`.
2. Build-time CPython 3.12.8 from the pinned python.org tarball
   (`--prefix=/opt/python3 --without-ensurepip`) — see §3.
3. CMake 3.31.6 from the pinned Kitware x86_64 tarball (CentOS 7 has CMake 2.8;
   slang v12.1 requires ≥ 3.28).
4. `git clone --depth 1 --branch v12.1` and a hard check
   `git rev-parse HEAD == 42710c788ce76cd08ce2080773e0b8286c428a05` (`03-source.txt`).
5. Apply `patches/0001-gcc11-libstdcxx-path-hash.patch` verbatim (`git apply
   --check` first; failure is fatal) — see §3.
6. Configure:
   `cmake -S slang -B obj -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
   -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DSLANG_INCLUDE_TESTS=OFF
   -DSLANG_INCLUDE_DOCS=OFF -DSLANG_INCLUDE_TOOLS=ON -DSLANG_INCLUDE_PYLIB=OFF
   -DPython_EXECUTABLE=/opt/python3/bin/python3 -DPYTHON_EXECUTABLE=/opt/python3/bin/python3
   "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc"`
7. `cmake --build obj --target slang_driver -j16`, then `strip --strip-all` into
   `out/slang`.

Toolchain actually used (`02-toolchain.txt`): CentOS 7.9.2009, glibc 2.17, GCC/G++
11.2.1 (devtoolset-11), GNU ld 2.36.1, make 3.82, git 1.8.3.1, CMake 3.31.6,
Python 3.12.8 (build-time only).

Fetched dependencies (exact commits, `05-deps.txt`): fmt `12.2.0`
(`1be298e`), boost::regex `boost-1.91.0` (`2b3ac08`, header-only), mimalloc
`v3.5.3` (`d4881d3`), tomlplusplus `v3.4.0` (`3017243`); boost_unordered headers
are vendored in the slang tree. All are permissive open-source licenses (fmt MIT,
mimalloc MIT, tomlplusplus MIT, Boost BSL-1.0) — the formal notices are T29's
deliverable.

## 2. The two build obstacles and the decisions taken

### 2.1 slang code generators need Python ≥ 3.9 (CentOS 7 ships 2.7.5)

First attempt died in code generation: CMake found `/usr/bin/python2.7`, and
`scripts/syntax_gen.py` uses `str.removesuffix` (Python ≥ 3.9) → `SyntaxError`.
CentOS 7 base `python3` is 3.6.8 and the newest SCL Python is 3.8 — both too old.

Options checked, in the order the captain asked:

- **(a) pre-generated source tarball from GitHub releases — ruled out.** The v12.1
  release assets are only prebuilt binaries (`slang-linux-x86_64.tar.gz` etc.);
  there is no "make dist" source archive. The prebuilt Linux binary is also
  unusable: the recon binary in this repo (slang v11) needs `GLIBC_2.27`.
- **(b) minimal patch of the generator scripts for Python 3.6/3.8 — rejected.**
  It would mean carrying a diff against the pinned upstream codegen scripts and
  re-proving on every bump that no other ≥ 3.9 feature is used.
- **(c) chosen: compile CPython 3.12.8 from source inside the build container.**
  Pinned tarball + sha256 in `versions.env`, ~40 s build, **build-time only**: the
  produced binary has no Python dependency (`ldd` in `07-artifact.txt`/`09-offline-smoke.txt`).
  Cost accepted: any container that rebuilds slang also needs this step (relevant
  for T23/CI; `build.sh` already does it).

### 2.2 GCC 11.2.1 lacks `std::hash<std::filesystem::path>`

Second attempt: `include/slang/util/Hash.h` falls back to `std::hash<T>`, and
`flat_hash_map<std::filesystem::path, …>` instantiates it. libstdc++ gained that
specialization after GCC 11.2; devtoolset-11 is 11.2.1 and is the **newest compiler
available for CentOS 7** (no devtoolset-12+ for el7). Upstream issue
MikePopoloski/slang#1407 is the same failure on the same OS/compiler, and the
maintainer confirms GCC ≥ 11.3 fixes it.

Decision: one 15-line build-time compatibility patch,
`patches/0001-gcc11-libstdcxx-path-hash.patch` (sha256
`09e985ec43e1a8e62b634e9a2a115d0621ef50d24a6f2e2a5c99b7633c9790d4`), which adds
`slang::hash<std::filesystem::path>` specialization. It is the only modification
of upstream code, is applied to the pinned checkout by the build script, is
recorded verbatim in `03b-patches.txt`, and fails the build loudly if it ever
stops applying. Building GCC 12+ from source or switching to zig/clang would be a
much larger change for the same result.

## 3. Verification gates (deliverable 1 of the addendum)

`package.json` maps the MPD gate ids to `build/gates/*.sh`; it is verification
orchestration only (never a product runtime/build dependency; the product is Rust).

| gate id | argv the evidence tool runs | reachable | observed |
|---|---|---|---|
| `gates` | `bun run verify:gates` | yes | **exit 0** — `GLIBC_2.17`, sha256, `N/A` test step (pre-T7 only) — `evidence/13-gate-gates.txt` |
| `docs` / `manifest` / `comments` / `rows` | `bun run verify:<id>` | yes | exit 1 with the owning task or "本项目无此门禁" — `14-gates-honest-red.txt` |
| `typecheck` | `bun run typecheck` | yes, via alias added to `package.json` | exit 1 (no Cargo workspace yet) — same file |
| `tests` | `bun test packages` (hardcoded) | **no** — a Rust product cannot match; `cargo test --workspace` is folded into `gates.sh` | — |
| `vendor` / `dist` / `pack` | `node scripts/verify-*.ts` (hardcoded path) | **no** — ENOENT before our script; no evidence can be produced | — |

No gate fakes green: `gates.sh` exits 0 only after verifying an existing artifact
(ELF bound + smoke), and its folded-in `cargo test` becomes a hard failure the
moment `Cargo.toml` exists (`N/A` is legal only before T7). The unreachable ids
and their scripts are documented in `build/gates/README.md` and `AGENT.md §4`.

## 4. Git baseline, `.gitignore`, AGENT.md §4 (addendum deliverables 2–3)

- Commits (no amend/rebase, no `git add -A`, explicit paths only):
  - `a4d033c` — baseline `AGENT.md` + `.gitignore` (generic identity, as directed);
  - `6417881` — `.codegraph/` added to `.gitignore` (role identity);
  - `0619597` — `package.json` + `build/gates/**` + `AGENT.md §4` (role identity);
  - T3 delivery commit — `build/slang/**` (scripts, patch, fixtures, evidence, this
    report; run `git log --oneline -- build/slang`).
- `.gitignore`: `.mpd/` (nested memory repo), `.codegraph/`, `target/`,
  `build/artifacts/`, `*.AppImage`, `build/slang/{cache,out,tmp,slang-12.1}/`,
  editor/OS noise.
- `AGENT.md §4` now names the real commands: build `bash build/slang/build.sh`
  (+ `bun run verify:gates`, `GATES_REBUILD=1` to rebuild), test
  `cargo test --workspace`, lint `cargo clippy --workspace --all-targets -- -D
  warnings`, format `cargo fmt --all`; it also states that only `verify:gates` can
  be green at this milestone and lists the unreachable gate ids.
  **§4 was filled by T3 at the captain's request: T7 should review it, not rewrite it.**
- One conflict to flag: `AGENT.md §6` still says "work on a branch, never directly
  on main". This repository has a single `main` worktree and the team commits per
  captain instruction directly to it; the rule is left untouched (out of scope) so
  a human can decide.

## 5. What I did NOT verify (bounds of this evidence)

- **Byte-identical reproducibility across runs is not proven.** One full clean
  build was run end-to-end (exit 0); a second run would need the captured archive
  hashes compared. Inputs are pinned (commit, tarball sha256s, patch sha256) and
  dependency commits recorded in `05-deps.txt`, but the fetched dependency
  *archives* are not sha256-pinned.
- The build fetches source and dependencies over the network at build time (the
  contract allows this: "构建期可出网"). The offline container is only the
  runtime smoke test, not the build.
- `GLIBC_2.17` is the exact maximum, so there is **zero headroom**: the binary
  runs on CentOS 7.9's glibc and on anything newer, but nothing older. The gate
  asserts this on every run.
- The version string is `slang version 12.1.0+0`. CentOS 7's git 1.8.3 lacks
  `rev-parse --git-path`, so slang's version generator falls back to hash `0`.
  Cosmetic only: the checked-out commit is verified against the pin in
  `03-source.txt`. A newer git in the container would restore the hash suffix.
- The offline smoke covers `--version`, one clean fixture and one error fixture.
  The full AC-12 diagnostic matrix belongs to T17; the binary itself is the T3
  deliverable that T17 consumes.
- mimalloc is enabled (upstream default); its own test suite is not built or run.
- The AppImage lane (T23) must still bundle this binary and verify it inside its
  own root; that is not claimed here.

## 6. Reproduce

```sh
bash build/slang/build.sh          # ~3.5 min on 16 CPUs; exit 0 + artifact
bun run verify:gates               # ledger-reachable gate, exit 0
bun run verify:gates              # (GATES_REBUILD=1 forces the full rebuild)
```

Artifact: `build/slang/out/slang` (gitignored; rebuild it from the pinned inputs).
Evidence: `build/slang/evidence/` (index: `00-INDEX.md`).
