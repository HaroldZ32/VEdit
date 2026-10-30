# Evidence index — T3 (slang 12.1 build for CentOS 7 / glibc 2.17)

Every file in this directory is output captured on disk, not a retelling. Each
entry below names the exact command that produced it and its exit code, so a
verifier can re-run the command and compare byte for byte.

Reproduce everything with:

```sh
bash build/slang/build.sh                    # full build + ELF assertion + offline smoke (~3.5 min)
GATES_REBUILD=1 bun run verify:gates         # same, through the ledger-reachable gate id
```

| file | produced by | what it shows | exit |
|---|---|---|---|
| `00-build-console.log` | `bash build/slang/build.sh` (host) | full console of the one-command build, from a fresh `centos:7` container to the offline smoke | 0 |
| `01-yum-install.log` | `yum -y install devtoolset-11-* git make tar ...` (container) | toolchain packages installed from the CentOS 7.9.2009 vault mirror; ends with `Complete!` | 0 |
| `02-toolchain.txt` | build script | container `CentOS Linux release 7.9.2009`, glibc 2.17, GCC 11.2.1, binutils 2.36.1, make 3.82, git 1.8.3.1, static `libstdc++.a` found, 16 cpus | 0 |
| `02b-python-build.log` | `./configure --prefix=/opt/python3 --without-ensurepip && make -j16 && make install` | CPython 3.12.8 build (build-time code generator only) | 0 |
| `02c-python.txt` | build script | system Python is 2.7.5; build-time Python is 3.12.8 at `/opt/python3/bin/python3` | 0 |
| `03-source.txt` | build script | source provenance: `v12.1` tag, pinned commit `42710c788ce76cd08ce2080773e0b8286c428a05`, checked-out commit identical, clean tree | 0 |
| `03b-patches.txt` | build script | the one compatibility patch applied verbatim, its sha256 `09e985ec...`, and the resulting diff | 0 |
| `04-configure.log` | `cmake -S … -B …` | configure: GNU 11.2.1, Python 3.12.8 found, static `svlang`, deps fetched (fmt 12.2.0, boost::regex, mimalloc 3.5.3, tomlplusplus 3.4.0) | 0 |
| `05-deps.txt` | build script | exact commits of the four fetched dependencies | 0 |
| `05-elf-check.txt` | `build.sh` stage 2 | `objdump -T` highest GLIBC = `GLIBC_2.17`; NEEDED = libpthread/libm/libc/ld-linux only; sha256; stripped size | 0 |
| `06-build.log` | `cmake --build … --target slang_driver -j16` | 141 C++ compiles, `[100%] Linking CXX executable ../../bin/slang`, `[100%] Built target slang_driver` | 0 |
| `06-offline-console.log` | `build.sh` stage 3 (tee of `09`) | console copy of the offline smoke | 0 |
| `07-artifact.txt` | build script (in container) | `file(1)`: ELF 64-bit x86-64, dynamically linked, stripped; sizes 12,244,592 (unstripped) → 9,526,400 (stripped); sha256; `ldd` inside the build container | 0 |
| `08-timings.txt` | build script | total 204 s = yum 46 s + configure 16 s + compile/link 97 s (+ CPython build, clone, patch, artifact) | 0 |
| `09-offline-smoke.txt` | `/scripts/verify-offline.sh` in a **stock** `centos:7` container with `--network none` | `/proc/net/route` has no default route; `slang --version` exit 0 → `slang version 12.1.0+0`; clean fixture lint exit 0; undeclared fixture lint exit 1; `--diag-json` parses as JSON array; `ldd` has neither libstdc++ nor libgcc_s; final line `OFFLINE SMOKE: PASS` | 0 |
| `10-clean.out` | `/out/slang --lint-only /fixtures/clean.sv` (offline container) | `Build succeeded: 0 errors, 0 warnings` | 0 |
| `11-diag-text.out` | `/out/slang --lint-only -Weverything --diag-json … /fixtures/undeclared.sv` (offline container) | human-readable error: `use of undeclared identifier 'undefined_count'` at `fixtures/undeclared.sv:6:19`, `Build failed: 1 error` | 1 |
| `11-diag.json` | same command, JSON output | JSON **array** with 1 diagnostic (`severity`, `message`, `code: UndeclaredIdentifier`, `location`, `ranges`, `symbolPath`) | 0 (file write) |
| `12-host-elf-verification.txt` | explicit host re-run (commands and exit codes are printed inline) | `readelf -V` max `GLIBC_2.17`; `objdump -T` max `GLIBC_2.17`; no `GLIBCXX_*`/`CXXABI_*`; no NEEDED libstdc++/libgcc_s; sha256 and size | 0 |
| `13-gate-gates.txt` | `bun run verify:gates` (the MPD-reachable gate id) | gate asserts `GLIBC_2.17` + sha256, prints `N/A` for the not-yet-existing Cargo workspace (only legal before T7), `PASS` | 0 |
| `14-gates-honest-red.txt` | `bun run verify:<id>` / `bun run typecheck` | 9 gates each exit 1 with `本项目无此门禁…` + the owning task; `typecheck` alias reaches our script | 1 each |

Notes for a verifier:

- The artifact `build/slang/out/slang` (sha256 `63cb60a05f6ca57f648b8e2ef6de61ee0a3132c1c9d481f831dba52388832252`) is
  intentionally **not committed** (`.gitignore`: `build/slang/out/`); rebuild it
  with `bash build/slang/build.sh`. The committed evidence is the build's output.
- `build/slang/cache/` (fetched tarballs) is also ignored; the build re-downloads
  and sha256-checks its inputs.
- The build container is `docker.m.daocloud.io/library/centos:7`, started with
  `--rm` from the stock image: no host toolchain and no pre-baked layer is used.
