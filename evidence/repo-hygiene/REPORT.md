generated: 2026-10-31T10:58:16Z
git_head: 1f47e56421bbeaab64f8082df3cfc104a6a2c2ef
git_status_porcelain:
 M .gitignore
 M build/evidence/12-gates-image-build.log
 M build/gates/gates.sh
 M build/gates/typecheck.sh
 M build/slang/evidence/00-build-console.log
 M build/slang/evidence/01-yum-install.log
 M build/slang/evidence/02-toolchain.txt
 M build/slang/evidence/02b-python-build.log
 M build/slang/evidence/02c-python.txt
 M build/slang/evidence/03-source.txt
 M build/slang/evidence/03b-patches.txt
 M build/slang/evidence/04-configure.log
 M build/slang/evidence/05-deps.txt
 M build/slang/evidence/05-elf-check.txt
 M build/slang/evidence/06-build.log
 M build/slang/evidence/06-offline-console.log
 M build/slang/evidence/07-artifact.txt
 M build/slang/evidence/08-timings.txt
 M build/slang/evidence/09-offline-smoke.txt
 M crates/vd-config/tests/project_model_fixture.rs
 M crates/vd-ffi/include/vd_ffi.h
 M crates/vd-ffi/src/lsp.rs
 M crates/vd-lsp/src/session.rs
 M crates/vd-lsp/tests/session_degradation.rs
 M crates/vd-lsp/tests/session_file_set.rs
 M evidence/ffi-abi/logs/10-rust-image.txt
 M evidence/ffi-abi/logs/19-rust-stage-console.log
 M evidence/ffi-abi/logs/20-cargo-build-release.log
 M evidence/ffi-abi/logs/21-staticlib.txt
 M evidence/ffi-abi/logs/23-cargo-test.log
 M evidence/ffi-abi/logs/26-vd_ffi.h
 M evidence/ffi-abi/logs/27-vd_ffi-comment-free.txt
 M evidence/m0-build/build-logs/20-qt6-image-build.log
 M evidence/m0-build/build-logs/21-qt6-image-id.txt
 M evidence/m0-build/build-logs/31-qt6-configure.log
 M evidence/m0-build/build-logs/32-qt6-build.log
 M evidence/m0-build/build-logs/33-qt6-ctest.log
 M evidence/m0-build/build-logs/34-qt6-elf.txt
 M evidence/m0-build/build-logs/37-qt6-rust-abi.txt
 M evidence/m0-ksh-selfbuild/build-logs/10-configure.log
 M evidence/m0-ksh-selfbuild/build-logs/11-build.log
 M evidence/m0-ksh-selfbuild/build-logs/12-install.log
 M evidence/m0-ksh-selfbuild/build-logs/21-library-identity.txt
 M evidence/m0-ksh-selfbuild/build-logs/32-abi-loading.txt
 M evidence/m0-ksh-selfbuild/build-logs/40-smoke-configure.log
 M evidence/m0-ksh-selfbuild/build-logs/41-smoke-build.log
 M resources/syntax/vedit-systemverilog.xml
 M resources/syntax/vedit-verilog.xml
 M src/app/CMakeLists.txt
 M src/app/editor_area.cpp
 M src/app/editor_area.h
 M src/app/editor_provider.cpp
 M src/app/editor_provider.h
 M src/app/main.cpp
 M src/workspace/integration.cpp
 M src/workspace/search/CMakeLists.txt
 M src/workspace/search/search_integration.cpp
 M src/workspace/search/search_panel.h
 M src/workspace/search/search_selftest.cpp
 M src/workspace/search/search_types.h
?? evidence/fix-search-jump/
?? evidence/gate-infra/
?? evidence/highlight-v2/00-collect.txt
?? evidence/highlight-v2/01-after-link.txt
?? evidence/highlight-v2/03-build-qt-gate.log
?? evidence/highlight-v2/04-syntax-product-tests.log
?? evidence/highlight-v2/06-standalone-lanes.log
?? evidence/highlight-v2/07-negative-control.log
?? evidence/highlight-v2/08-ctest.log
?? evidence/highlight-v2/09-screenshot-L2.log
?? evidence/highlight-v2/10-licensing-and-embargo.txt
?? evidence/highlight-v2/README.md
?? evidence/highlight-v2/screenshots/
?? evidence/lsp-v2/
?? evidence/project-model/
?? evidence/repo-hygiene/
?? evidence/syn-kde-data/10-folding-before.log
?? evidence/syn-kde-data/11-folding-after.log
?? evidence/syn-kde-data/f2-build.log
?? evidence/syn-kde-data/folding-probe/
?? evidence/verdicts/fix-filetree-open/
?? src/editor/CMakeLists.txt
?? src/editor/kernel/
?? src/workspace/diagnostics/
?? src/workspace/filetree/tools/generate-testdata.sh
?? src/workspace/project/
?? src/workspace/search/search_jump_target.cpp
?? src/workspace/search/search_jump_target.h
---
# repo hygiene: `filetree-testdata/` (T56)

## 1. The fact

The worktree carried `filetree-testdata/`: **10,007 files / 376 KB apparent**,
**10,006 of them zero bytes**. It is generated data, not authored input:

- `src/workspace/filetree/tests/filetree_tests.cpp` uses
  `$VEDIT_FILETREE_SCRATCH` when it is set and otherwise falls back to
  `$PWD/filetree-testdata` (`scratchRoot()`, `main()`);
- `testLaziness()` creates the 10,000 `big/module_%05d.v` files and the two child
  folders, and the other tests create `plain.txt`, `tree/*` and `ops/*`.

So the directory is simply the suite's scratch tree, left in the repository root
by a run whose working directory was the root. It is reproducible from code and
must not be committed.

## 2. The decision and what changed

Captain's ruling: **do not commit the generated data, guarantee reproducibility.**

- `.gitignore`: new "generated test data" section with the rule
  `filetree-testdata/`. A pattern without a leading slash matches that directory
  at any depth, so a lane run from any working directory stays out of git.
- `src/workspace/filetree/tools/generate-testdata.sh` (new, executable):
  deterministic, toolchain-free reproduction of **exactly this layout** — 10,007
  files (10,000 zero-byte `big/module_%05d.v`, `plain.txt` = `x`, `tree/top.v`,
  `tree/alpha/a1.v`, `tree/alpha/deep/d1.sv`, `tree/beta/b1.f`,
  `ops/external.v`, `ops/locked/keep.v`) and all 13 subdirectories, including the
  intentionally empty ones (`empty/`, `locked/`, `tree/zeta/`, `ops/rtl/`).
  Default destination `$PWD/filetree-testdata`; a non-empty destination is
  refused unless `--force` is passed; it prints two manifest hashes as the
  determinism anchors.
- Perf entry: **no code change was needed** — both lane entries already generate
  their own data and never skip on absence (verified end to end, §3):
  - `target/filetree-build/vedit-filetree-tests` wipes its scratch and rebuilds
    it on every run; started with **no dataset at all** it generated 10,007 files
    and passed with `checks=111 failures=0`, exit 0;
  - `vedit-filetree-demo --prepare <dir> --big-count 10000` builds the
    screenshot/perf fixture: `PREPARE ok=1 … big_files=10000`, exit 0.
  Nothing was modified there, so no entry behaviour could regress.

Changed paths: `.gitignore`, `src/workspace/filetree/tools/generate-testdata.sh`,
`evidence/repo-hygiene/**`. No product code, no `resources/syntax/**`, no other
lane's in-flight file, no test or gate was touched. **No commit, no push** — the
Lead commits.

## 3. Acceptance checks (observed)

| check | command | observed |
| --- | --- | --- |
| rule in effect | `git check-ignore -v filetree-testdata/plain.txt filetree-testdata/big/module_00000.v` | `.gitignore:53:filetree-testdata/` for both paths (`01-ignore-proof.txt`) |
| 10k files out of status | `git status --porcelain \| grep -c filetree-testdata` | `0` |
| generator runs | `bash src/workspace/filetree/tools/generate-testdata.sh <dir>` | `files=10007 dirs=13 file_bytes_total=1` |
| determinism | two runs + `--force` re-run, structure/content manifests diffed | byte-identical, anchors below (`02-generator-determinism.txt`) |
| layout == existing residue | `diff` of structure + content manifests vs `filetree-testdata/` | no differences |
| layout == entry's own scratch | `diff` of manifests vs the tree the test entry generated | no differences |
| entry auto-generates and passes | test binary run with a deleted/absent scratch | exit 0, `checks=111 failures=0`, 10,007 files created (`03-entry-autogen.log`) |
| demo fixture auto-generates | `vedit-filetree-demo --prepare … --big-count 10000` | `PREPARE ok=1`, `big_files=10000`, exit 0 (`04-demo-prepare.log`) |
| script syntax | `bash -n src/workspace/filetree/tools/generate-testdata.sh` | ok |
| repository gate | `bash build/gates/docs.sh` | `[docs] PASS`, exit 0 |

## 4. Determinism anchors

```
structure_manifest_sha256=8746ffe0472cfe084d393b59b037900779aa6d2d6c4d3526a14d45e7a7b489ac
content_manifest_sha256=9b1cd041e8a37685490d176123b1a6ac4a52fed8ce344a4133a557acf727b0b5
```

- `structure_manifest`: `find . -mindepth 1 -printf '%P\t%y\t%s\n' | LC_ALL=C sort`
  with directory sizes normalised to `-` (directory sizes are filesystem
  metadata and vary with allocation history).
- `content_manifest`: `find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum`.
- The same two hashes come out of: the checked-out residue, a fresh generator
  run, a second generator run, and the scratch the perf entry generated itself.

## 5. What remains on disk / hand-over

The generated tree stays on disk but ignored: `git status` no longer lists it.
When committing, stage only `.gitignore`, the generator script (keep mode 100755)
and `evidence/repo-hygiene/**`. The dataset itself must never be staged.

## 6. Limits and notes

- The two `SKIPPED running-as-root` lines in `03-entry-autogen.log` are the DAC
  permission-denied cases (rootless docker runs effectively as root). They are
  printed as SKIPPED and are not counted as passes; they are unrelated to the
  dataset. Every `lazy.*` perf check in that run ran and returned `ok=1`, so no
  data-missing skip could have hidden behind them.
- `shellcheck` is not installed on this host; the script was checked with
  `bash -n` plus the runs recorded here.
- Directory sizes are deliberately normalised out of the structure manifest so
  the anchor is portable between filesystems.
