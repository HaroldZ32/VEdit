# Reproducibility handoff — current pin status and the T7 / T27 checklist

Audience: **T7** (owns the workspace skeleton and the reproducible build script,
including the AC-19 hardening the captain ruled on) and **T27** (acceptance:
second-build comparison). Written by T3 after the captain's ruling that "deps are
not sha256-pinned and only one clean build was run" is a **substantive** gap, not
an acceptable bound.

**This file is preparation only. No T7 code is landed here** — in particular no
`Cargo.toml`, no `scripts/`, no change to the slang build path. It records facts
that the T7 build script should assert, so T7 does not have to rediscover them.

## 1. What is pinned today (verified by the T3 build)

| input | how it is pinned today | evidence |
|---|---|---|
| slang source | git commit `42710c788ce76cd08ce2080773e0b8286c428a05`; the script compares `git rev-parse HEAD` after a shallow clone of tag `v12.1` and hard-fails on mismatch | `evidence/03-source.txt` |
| compatibility patch | sha256 `09e985ec43e1a8e62b634e9a2a115d0621ef50d24a6f2e2a5c99b7633c9790d4`; `git apply --check` runs before `git apply` | `evidence/03b-patches.txt` |
| CMake 3.31.6 | URL + sha256 `5a1133ff…` hard-coded in `versions.env`; `sha256sum -c` before use | `evidence/02c-python.txt`, build console |
| CPython 3.12.8 | URL + sha256 `5978435c…` in `versions.env`; `sha256sum -c` before use | `evidence/02b/02c` |
| base container image | **not asserted by the script** (uses the mutable tag `centos:7`); the local image resolves to digest `sha256:be65f488b7764ad3638f236b7b515b3678369a5124c47b8d32916d6487418ea4`, verified runnable as `docker.m.daocloud.io/library/centos@sha256:be65f488…` | §4 below |
| yum packages (95) | repo baseurl pinned to the vault snapshot `…/centos-vault/7.9.2009/...`; **no package version pin**; exact installed NEVRAs are in the log | `evidence/01-yum-install.log` |
| FetchContent deps | git **commits** recorded: fmt `12.2.0`=`1be298e`, boost::regex `boost-1.91.0`=`2b3ac08`, mimalloc `v3.5.3`=`d4881d3`, tomlplusplus `v3.4.0`=`3017243`; the cloned **archives are not checksummed** | `evidence/05-deps.txt` |

## 2. Gaps that matter for AC-19

1. **Base image by tag.** `centos:7` is mutable. The digest above must be asserted,
   otherwise the "same build" can silently run on a different image.
2. **FetchContent git clones.** Commit hashes are cryptographic and already
   recorded, but the script does not assert them after cloning, and the dependency
   *bytes* are not checksummed. A cheap equivalent control is to assert the four
   commits in the build script; a stronger one is to pre-fetch tarballs with sha256.
3. **yum package versions.** The vault is a static snapshot, but versions are
   resolved at install time and are not asserted. Record an `rpm -qa` snapshot and
   assert the critical NEVRAs.
4. **Single clean build.** Byte-level reproducibility was never measured (T27's
   acceptance action, procedure in §5).

## 3. T7 checklist (priority order)

a. **Pin the base image by digest** in the build script and fail if it differs:
   ```sh
   IMAGE='docker.m.daocloud.io/library/centos@sha256:be65f488b7764ad3638f236b7b515b3678369a5124c47b8d32916d6487418ea4'
   ```
   The tag currently resolves to exactly this digest
   (`docker image inspect --format '{{.Id}}' docker.m.daocloud.io/library/centos:7`).
b. **Snapshot and assert yum packages.** After the install step, write
   `rpm -qa --qf '%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}\n' | sort` to an
   `evidence/` file, and hard-assert at least:
   `devtoolset-11-gcc-c++-11.2.1-9.1.el7`, `devtoolset-11-binutils-2.36.1-1.el7.2`,
   `glibc-2.17-326.el7_9.3`, `git-1.8.3.1-25.el7_9`, `make-3.82-24.el7`,
   `zlib-1.2.7-21.el7_9`.
   (These are the NEVRAs the T3 build actually installed; see `01-yum-install.log`.)
c. **Assert FetchContent commits** after configure, before build — for each of
   `fmt-src`, `boost_regex-src`, `mimalloc-src`, `tomlplusplus-src`:
   `( cd _deps/<name> && git rev-parse HEAD )` must equal the recorded commit, else
   hard fail. This is two lines and closes gap 2 with a commit-level (cryptographic)
   control. If AC-19 later demands an offline build, prefetch the four tarballs,
   sha256 them, and pass `-DFETCHCONTENT_SOURCE_DIR_<NAME>=<path>`.
d. **Everything in `versions.env`.** Every downloaded archive gets a URL + sha256;
   the script verifies before use and fails on mismatch (already true for CMake and
   CPython; extend to the rest).
e. **Document the pins** (BUILD doc, T29): the values above are the build of record,
   so a future rebuild can diff against them.

## 4. Verified facts for the image pin

```console
$ docker run --rm docker.m.daocloud.io/library/centos@sha256:be65f488b7764ad3638f236b7b515b3678369a5124c47b8d32916d6487418ea4 \
      sh -c 'cat /etc/centos-release; rpm -q centos-release glibc'
CentOS Linux release 7.9.2009 (Core)
centos-release-7-9.2009.0.el7.centos.x86_64
glibc-2.17-317.el7.x86_64
```
(`glibc-2.17-317` is the base image's copy; the build's yum transaction upgrades it
to `2.17-326.el7_9.3`, which is why that NEVRA is the one worth asserting.)

## 5. T27 procedure: second-build comparison (AC-19 acceptance)

1. Build once, keep the artifact and the evidence of that run.
2. Rebuild from scratch (`rm -rf build/slang/out`; the container is fresh anyway)
   and compare:
   ```sh
   cmp -s runA/slang runB/slang && echo "BYTE-IDENTICAL"
   sha256sum runA/slang runB/slang
   ```
3. If they differ, localise before judging: `cmp -l runA/slang runB/slang | head`,
   `readelf -S` on both, `readelf -x .comment` on both, and
   `strings -a runA/slang | grep -E '/build/|/work/|20[0-9]{2}-' | head`.
   Expected sources of non-determinism: `__DATE__`/`__TIME__`, build-path strings,
   link order, and `.note.gnu.build-id` (which is content-derived, so it changes
   only when contents change). The build path is already fixed to `/build/slang-src`
   by the container script, which helps.
4. If byte-equality is unattainable, record the differing bytes and their cause and
   define the equivalence criterion explicitly (e.g. identical `.text`/`.rodata`
   sizes, identical dynamic symbol tables via `objdump -T` diff, only the
   build-id differs). "Not proven" alone is not an acceptable AC-19 result.
5. Optional determinism aids: `SOURCE_DATE_EPOCH`, `-ffile-prefix-map=/build=…`,
   and keeping the clone path fixed.

## 6. Environment pitfalls (each cost a build cycle in T3; carry them into T7)

- `set -o pipefail` + `cmd | head -1` aborts the whole script when `head` exits
  first (SIGPIPE, exit 141). Use `sed -n '1p'` (it reads to EOF) instead.
- The agent shell's `/tmp` is **not** the docker daemon's `/tmp`; a
  `docker run -v /tmp/...` mount fails with "no such file". Keep scratch files
  inside the workspace.
- CentOS 7 `git` is 1.8.3: no `git -C`, no `rev-parse --git-path`. Use
  `( cd dir && git … )`. The missing `--git-path` is also why slang's version
  string is `12.1.0+0` instead of `12.1.0+<hash>` (`evidence/03-source.txt` has the
  real commit).

## 7. Captain rulings that T7 must apply

- **AGENT.md §6:** commits go directly to the default branch, linear history;
  branch-per-task is not used (board + heterogeneous review replaces the PR step;
  write-scope exclusivity provides "one writer per working tree"); history is never
  rewritten. T7 edits §6 to say this.
- **AGENT.md §4** was filled by T3 at the captain's request: T7 should **review it,
  not rewrite it** (it already names the real commands and the gate caveats).
- **T23/T25** must repeat the `GLIBC <= 2.17` assertion inside their packaged roots
  (T3 verified it for the standalone binary; the zero-headroom fact means a
  different glibc would break it).
- **T29** should note in BUILD/THIRD_PARTY_NOTICES that the `+0` version suffix is
  a CentOS 7 git limitation, with the source commit referenced from
  `build/slang/evidence/03-source.txt`.
