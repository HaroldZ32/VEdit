# T46 recheck — fresh gate evidence on the CURRENT tree (for independent review)

Task: **prep-t46** (cell `cell-20261102T100600176Z`, board `T4`), owner `dw-rust`.
Purpose: hand an independent reviewer fresh evidence for T46 **without touching a
line of code**. Nothing under `crates/` or `tests/` or any gate was edited; the
only writes are the four files listed in §6.

Read first (as the dispatch required): `evidence/w6-hier/09-t46-hierarchy-fix.md`,
`09-t46-gate-tests.txt`, `09-t46-gate-clippy-fmt.txt`.

## 1. Tree state measured

```
git HEAD: 4e2d0a57ca50db9568db5a5b040cc66f04c9a05f
git status --porcelain entries: 158
```

This is the same HEAD the 09 record names (`09-t46-gate-tests.txt` header:
`git_head: 4e2d0a5…`), with the T46 work still **uncommitted** on top of it
(`crates/vd-deeplint/src/hierarchy.rs` and its tests are untracked; the 09
measurement header recorded `dirty-paths=156`, now 158 — two more paths, none of
them in `crates/vd-deeplint/` or `crates/vd-ffi/`, see
`git status --porcelain` for the live list). Toolchain/container: pinned
`vedit-qt6-build:local` image
`sha256:fbc499583bc70ed66f56fe6a5e19144a033ca258d23fe4f72e3ff0ecd0ff161a`
(created 2026-11-02T04:49:12Z); host cargo via
`CARGO_HOME=$PWD/target/cargo-home RUSTUP_TOOLCHAIN=stable`; slang
`12.1.0+0` (`build/slang/out/slang`).

## 2. Commands and observed results

| # | command | observed | log |
|---|---|---|---|
| 1 | `CARGO_HOME=$PWD/target/cargo-home RUSTUP_TOOLCHAIN=stable cargo test --offline -p vd-deeplint -p vd-ffi` | **exit=0**; 38+4+13+10+6+2 deeplint + 43+12+1 ffi tests all ok | `10-t46-recheck-tests-20261102T104309Z.log` |
| 2 | `… cargo clippy --offline -p vd-deeplint -p vd-ffi --all-targets -- -D warnings` | **clippy-exit=0** (`Finished dev profile … in 7.55s`, no warnings) | `10-t46-recheck-clippy-fmt.txt` |
| 3 | `… cargo fmt -p vd-deeplint -p vd-ffi -- --check` | **fmt-exit=0** | `10-t46-recheck-clippy-fmt.txt` |
| 4 | container relink of `evidence/w4-lint/scale/scale_harness.cpp` against the current `target/release/libvd_ffi.a` + 4 × `--mode elaborate` runs | **docker-exit=0**, 4× `ELABORATE-HARNESS ok=1 checks=4 failures=0` | `10-t46-recheck-elaborate-20261102T104444Z.log` |
| 5 | independent AST ground truth: `slang --ast-json --ast-json-source-info` on both 1k trees + `evidence/w4-lint/scale/ast_count.py` | **docker-exit=0**, both `slang-exit=0` | `10-t46-recheck-groundtruth-20261102T104512Z.log` |

`target/release/libvd_ffi.a` used by the harness: rebuilt from the current tree
at 2026-11-02T18:44:26+08:00, sha256
`a7931d4c8cdd4521c7316616dc4a64cccd1682ab1fee5698093dfbd6f1d40e0c`
(`cargo build --offline --release -p vd-ffi` → `build-exit=0`, no `.rs` newer
than the archive before that build).

## 3. Instance counts (the T46 claim)

Fresh service output, shipped C ABI `vd_hier_start`/`vd_hier_poll`, real
`slang 12.1.0+0`:

```
flat   1k, whole project (no top)   instances=11000  warning=«the design has 11000 instances (limit 10000)…»
flat   1k, --top m_0_0              instances=11
nested 1k, whole project (no top)   instances=11000  warning=«the design has 11000 instances (limit 10000)…»
nested 1k, --top l0_m_0_0           instances=11
```

Fresh, independently produced AST ground truth on the same trees:

```
flat   AST-GROUND-TRUTH roots=1000 instance_nodes=11000 max_depth=2
nested AST-GROUND-TRUTH roots=1000 instance_nodes=11000 max_depth=6
```

So: **flat whole-project 11000/11000, nested whole-project 11000/11000, nested
`--top` 11/11** — all three checks of the dispatch hold, and the ground-truth
legs are reproduced in this session rather than merely quoted from 09.

The nested `max_depth` differs between the two definitions (harness 7 path
segments vs AST 6 instance ancestors) — the same, already documented difference
`09-t46-hierarchy-fix.md` §6 records; not a contradiction.

## 4. Difference against the 09 evidence

* **Test suite**: the fresh log's test-name set is **identical** to
  `09-t46-gate-tests.txt` (`diff` of the `^test …` lines → empty;
  per-binary pass/fail counts identical: 38/4/13/10/6/2 and 43/12/1, every
  `test result: ok`, `exit=0` both times). Only the compilation section differs
  (09 compiled the whole dependency graph from cold; this rerun was
  incremental), and timings inside test lines.
* **Gate breadth**: 09's clippy log covered `-p vd-deeplint` only; this recheck
  covers `-p vd-deeplint -p vd-ffi` (both crates whose files are dirty for this
  wave) and adds `fmt --check` for both. Both exit 0.
* **Measurement**: the returned counts are **byte-identical** to
  `evidence/w4-lint/scale/logs/50-elaboration-t46.log` (11 / 11000 / 11 / 11000,
  same `deepest=«…»` strings). Wall clock and RSS differ — e.g. nested whole
  project 1293 ms / 540 480 KiB here vs 1339 ms / 540 124 KiB in the T46 log;
  flat whole project 696 ms / 216 264 KiB vs 689 ms / 271 512 KiB. Those are
  run-to-run noise on a shared 16-core host; **no timing claim is made** (09
  §6 says the same). The counts are exact.
* The harness binary was relinked against the current archive; the 09 log's
  harness was linked 2026-11-02 17:08 against the then-current archive. Same
  source, same fixtures, same slang.

## 5. What is deliberately NOT proven here

* The dispatch's step ③ asks to read the 11000/11000 and `--top` 11 counts
  "from the log" — note the **cargo-test log cannot contain them**: `cargo test`
  pins the ground truth on the small captured fixture (`19` instances, plus the
  generate/array/signal assertions), never on the 1k trees. The 1k counts are a
  C-ABI harness measurement and are evidenced separately (§3, log #4/#5). This
  is a wording mismatch in the dispatch, not a missing measurement.
* No source, test, fixture or gate file was edited or even re-read at the
  implementation level (read-only review of the 09 documentation plus the
  evidence tooling's own CLI/scripts).
* Unrelated dirty paths (158 entries) belong to other lanes/tasks and were not
  touched, staged or committed.

## 6. Files written (all inside `evidence/w6-hier/`)

| file | sha256 |
|---|---|
| `10-t46-recheck-tests-20261102T104309Z.log` | `f27f8fc850f4c663e022b3f9c6518a9c9f31f4e244a719a2750602b2bca7032d` |
| `10-t46-recheck-clippy-fmt.txt` | `3b33c5624f611979e43af5079d92440d5f23ac5d87d3034665adb927648cda1a` |
| `10-t46-recheck-elaborate-20261102T104444Z.log` | `9638a6bcc4f0b4984e3a546621cd35e235cad22233df0ac22cee2a281365790d` |
| `10-t46-recheck-groundtruth-20261102T104512Z.log` | `31f2f1f9e5f9738f25b43378656263e6a6ffd7ff6b86cb278b77c607535ddb1c` |

## 7. Reproduce (exact)

```bash
CARGO_HOME=$PWD/target/cargo-home RUSTUP_TOOLCHAIN=stable \
  cargo test --offline -p vd-deeplint -p vd-ffi
CARGO_HOME=$PWD/target/cargo-home RUSTUP_TOOLCHAIN=stable \
  cargo clippy --offline -p vd-deeplint -p vd-ffi --all-targets -- -D warnings
CARGO_HOME=$PWD/target/cargo-home RUSTUP_TOOLCHAIN=stable \
  cargo fmt -p vd-deeplint -p vd-ffi -- --check
CARGO_HOME=$PWD/target/cargo-home RUSTUP_TOOLCHAIN=stable \
  cargo build --offline --release -p vd-ffi
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local bash -lc '
  g++ -std=c++20 -O2 -fPIC -I src/workspace/diagnostics -I crates/vd-ffi/include \
    $(pkg-config --cflags Qt6Core) evidence/w4-lint/scale/scale_harness.cpp \
    target/qt6-build/src/app/workspace-lane-diagnostics/libvedit_workspace_diagnostics.a \
    target/release/libvd_ffi.a $(pkg-config --libs Qt6Core) -lpthread -ldl -lm \
    -o target/tmp/scale/scale_harness
  export QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg
  H=target/tmp/scale/scale_harness; S=/vedit-build/build/slang/out/slang
  $H --root /vedit-build/target/tmp/scale-flat-1k   --slang $S --mode elaborate
  $H --root /vedit-build/target/tmp/scale-nested-1k --slang $S --mode elaborate
  $H --root /vedit-build/target/tmp/scale-nested-1k --slang $S --mode elaborate --top l0_m_0_0'
```
