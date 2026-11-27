# SVN history and comparison backend

These runs used base `752c8ab8e2e22528f4e863da8318d81e177203e8` with the T5 worktree changes. The Rust fixture creates a fresh `file://` repository using real `svnadmin` and observes the normal `vd-vcs` read paths. The same fixture ran with actual Subversion **1.7.14** and with **1.14.5** behind a shell wrapper that rejects `--show-item`, `--no-newline`, `--search` and `cat --ignore-keywords`.

| Observation | Result |
| --- | --- |
| Baseline with rejecting client | The explicit `svn info --show-item url .` control failed, and the original backend returned the same error. [Baseline](baseline.log) |
| Genuine 1.7.14 client | The real CLI reported `invalid option: --show-item`. It also rejected the first implementation's `.@HEAD`. Resolving the local URL through `svn info --xml` and pinning the URL at `HEAD` fixed project history. [Intermediate failure](actual-svn17.log) · [Final result](svn17-final.log) |
| Working tree | The fixture checked status codes and exact `BASE` for a modification, a plain addition with no pristine contents, a copied addition and a scheduled deletion. The history/snapshot reads use the selected host client's read-only runner. [Legacy results](svn17-final.log) · [Modern results](vd-vcs-svn.log) |
| Project history | Opening a nested project shows r3 and r2. The sibling-only r4 and a sibling file changed in r3 stay out of the result. The seven r3 rows carry A/D/M/R actions, decoded paths, file kinds and exact copy/move source paths and revisions. [Legacy results](svn17-final.log) |
| File history and comparison | The fixture follows copy history, finds the pre-deletion peg, and checks both revisions around additions, deletions, replacements and moves. Paths contain spaces, `@`, `&`, `%` and Chinese characters. Both working-copy and URL snapshots return errors for invalid UTF-8 or embedded NUL. [Legacy results](svn17-final.log) |
| Stale working-copy lineage | A working copy at r4 initially showed only the unrelated r7 addition at the same URL. After checking repository ancestry against local `BASE`, both clients include the original node’s r5 edit and r6 deletion. The selected r5 snapshot contains the original node’s later contents and the r6 snapshot is absent. [Before lineage fix](lineage-baseline.log) · [Legacy results](svn17-final.log) · [Modern results](vd-vcs-svn.log) |
| Rust ABI | All 33 tests in `abi` and `abi_batch` passed with the pinned Slang executable available. The provider now reports ABI 3, which requires recompiling consumers against the generated header because `VdVcsFileStat` grew. [ABI results](vd-ffi-with-slang.log) |
| Clippy | `-p vd-vcs -p vd-ffi --all-targets -- -D warnings` passed. [Clippy results](clippy.log) |
| Broad VCS suite | 86 tests passed. `host_execution` failed at the `csh` SVN add case with `set: Variable name must begin with a letter.` The identical failure reproduced from a clean archive of the base revision. [Patched run](vd-vcs.log) · [Baseline control](baseline-host-execution.log) |

The existing SVN commit detail job carries the credential-free repository root URL in `from`, the selected repository subtree in `to`, and decoded repository-absolute paths in `files[i].path`. The new file row fields carry the ASCII action, node kind, and optional copy source path and revision. The separate history panel can still consume the textual changed-path list. Comparison calls can address each side with its own operative revision and peg through the existing SVN snapshot jobs.

Both snapshot paths enforce the existing 8 MiB UTF-8 text limit. Clients that lack `cat --ignore-keywords` use SVN's normal keyword handling. Clients without `log --search` search the most recent 1,001 log entries. For a stale working copy whose path no longer has the same ancestry at HEAD, deletion lookup examines up to 1,000 revisions after local BASE. If the deletion is outside that window, history stays pinned to BASE. These fixtures exercise local repositories. Qt verification is recorded by the separate integration lane. Miri was unavailable on the installed toolchain and the sandbox refused the nightly toolchain update.

## Commands

In the SVN data worktree, with `ROOT` pointing to the integration checkout and `native` to its `target/trace-lint-svn-evidence/native/modern-svn` directory:

```sh
export CARGO_HOME="$ROOT/target/cargo-home"
export CARGO_TARGET_DIR=/tmp/vedit-svn-data-target CARGO_INCREMENTAL=0
export VEDIT_SVN_BIN="$native/svn" VEDIT_SVNADMIN="$native/svnadmin"
export VEDIT_SLANG="$ROOT/build/slang/out/slang"
cargo +stable test --locked --offline -p vd-vcs \
  --test svn_compare_e2e --test svn_e2e -- --nocapture --test-threads=1
cargo +stable test --locked --offline -p vd-ffi \
  --test abi --test abi_batch -- --test-threads=1
cargo +stable clippy --locked --offline -p vd-vcs -p vd-ffi --all-targets -- -D warnings
PATH="$native:$PATH" cargo +stable test --locked --offline -p vd-vcs \
  --no-fail-fast -- --test-threads=1
```

For the genuine legacy run, the CentOS Vault RPMs were checked against the CentOS 7 signing key inside `vedit-build:local` and extracted into `target/svn17-fixture/stage` using `rpm2cpio`. The two launchers in `target/svn17-fixture/bin` set `LD_LIBRARY_PATH=/fixture/stage/usr/lib64` and exec the corresponding binary from `/fixture/stage/usr/bin`. The legacy `svnadmin` creates the repository itself. [CentOS Vault packages](https://vault.centos.org/7.9.2009/os/x86_64/Packages/)

```sh
docker run --rm --network none \
  -v "$PWD:/src" -v "$PWD/target/svn17-fixture:/fixture:ro" \
  -v "$ROOT/target/cargo-home:/cargo" -w /src \
  -e CARGO_HOME=/cargo -e CARGO_TARGET_DIR=/tmp/svn17-target \
  -e CARGO_INCREMENTAL=0 -e RUSTUP_HOME=/opt/rust -e RUSTUP_TOOLCHAIN=1.99.0 \
  -e LANG=en_US.utf8 -e LC_ALL=en_US.utf8 \
  -e VEDIT_SVN_BIN=/fixture/bin/svn -e VEDIT_SVNADMIN=/fixture/bin/svnadmin \
  -e VEDIT_TEST_LEGACY_SVN_DIRECT=1 --entrypoint /bin/bash vedit-build:local \
  -lc 'cargo test --locked --offline -p vd-vcs --test svn_compare_e2e -- --nocapture'
```

| Artifact | SHA-256 |
| --- | --- |
| `subversion-1.7.14-16.el7.x86_64.rpm` | `e879fbf37e3687a390aeb96edc8cf627f1eee15cd93b77de191de48d341bf75f` |
| `subversion-libs-1.7.14-16.el7.x86_64.rpm` | `290f354af035f3161b792f57a774970370e16bdc9f98bfa686363bd1481c6bf1` |
| `crates/vd-vcs/tests/svn_compare_e2e.rs` | `2e1648d07c3168745e183e775ca60a6aa84fff762f47f805d9cacac03423c73a` |
| Generated `crates/vd-ffi/include/vd_ffi.h` | `4dffb841399b0d97bedbe5f86c9d8b830b53b52424ef084f4274105145b32531` |
