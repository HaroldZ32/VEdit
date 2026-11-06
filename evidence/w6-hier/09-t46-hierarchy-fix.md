# T46 — hierarchy service: two walker defects, fixed and measured

Task: **T46** (cell `cell-20261102T050548232Z`). Owner: `se-i18n` (Senior Engineer),
because the Rust-heavy seats were fenced when the scale numbers exposed this.
Scope written: `crates/vd-deeplint/src/hierarchy.rs`, its tests + fixtures,
`evidence/w6-hier/`, `evidence/w4-lint/scale/`, `target/tmp/`.
No public C ABI struct changed (`vd-ffi` untouched).

## 1. The two defects (pre-fix line numbers)

File: `crates/vd-deeplint/src/hierarchy.rs` at HEAD `4e2d0a5` (pre-fix lines
from the file as read before the edit):

1. **Only the first design root was walked** — `parse_design`, **lines 625–633**:
   ```rust
   let root = design.get("members").and_then(Value::as_array).and_then(|members| {
       members.iter().find(|member| member.get("kind")… == Some("Instance"))
   }).ok_or_else(|| "the AST JSON has no elaborated top instance".to_string())?;
   ```
   A whole-project elaboration (`--ast-json` with **no** `--top`) serialises
   **every** design root as an `Instance` member of `design.members`; `.find()`
   returns exactly one of them. On the 1k fixtures the AST has **1000 roots**.

2. **`GenerateBlockArray` was never entered** — `walk_instance`, **lines 840–843**:
   ```rust
   for member in &members {
       if member.get("kind")… == Some("Instance") {
           walk_instance(member, &path, bodies, instances, signals);
       }
   }
   ```
   `slang` serialises `generate for (g=0; g<6; g++) begin : g_leaf leaf u (…) end`
   as `GenerateBlockArray(name="g_leaf")` → 6 × `GenerateBlock` (`branchKind =
   LoopIteration`, `name=""`, `constructIndex = 0..5`) → the instantiated
   `Instance`. Only direct `Instance` members were visited, so every instance —
   and every port endpoint — inside a generate loop was dropped. The same blind
   spot hid `if`/`case` generate blocks (`GenerateBlock` with a label) and
   instance arrays (`InstanceArray`).

## 2. Ground truth vs returned

Fixtures: `target/tmp/scale-flat-1k` and `target/tmp/scale-nested-1k`, generated
by `evidence/w4-lint/scale/generate.py` (1000 files, 10 design instantiations per
file). Ground truth is the AST node count from
`slang --ast-json` + `ast_count.py` on the same trees:

```
flat   AST-GROUND-TRUTH roots=1000 instance_nodes=11000 max_depth=2
nested AST-GROUND-TRUTH roots=1000 instance_nodes=11000 max_depth=6
```

| measurement | before T46 | after T46 | ground truth |
|---|---|---|---|
| nested 1k, whole project (`vd_hier_start`, no top) | **instances=5** | **instances=11000** | 11000 |
| flat 1k, whole project (no top) | 11 (one root) | **11000** | 11000 |
| flat 1k, `--top m_0_0` | 11 | 11 | 11 |
| nested 1k, `--top l0_m_0_0` | 5 | **11** | 11 |
| nested 100/250/500 files | 5 / 5 / 5 | **1100 / 2750 / 5500** | 11 × files |

The "before" numbers are `evidence/w4-lint/scale/logs/50-elaboration.log`
(reproduced live at the start of T46: `instances=5 … deepest=«l0_m_0_0.u1.u2.u3.u4»`).
The "after" numbers are `logs/50-elaboration-t46.log` (same harness, same
fixtures, real `slang 12.1.0+0`).

## 3. The fix

`crates/vd-deeplint/src/hierarchy.rs` (post-fix locations):

* `parse_design` now collects **all** `Instance` members of `design.members`
  and walks each one with its own name as the root path.
* `walk_instance(instance, path, …)` takes the already-computed comprehensive
  path; a new `scope_instances`/`push_generate_blocks` pair yields every instance
  of a scope **through** `GenerateBlockArray`, `GenerateBlock` and
  `InstanceArray`, with the path slang would print:
  `top.u_mid.g_leaf[0].u`, `top.u_mid.g_if.u_if`, `top.u_arr[3]`
  (labelled block → its name; unnamed loop iteration → `[constructIndex]`).
  The same list drives the port-connection pass, so inputs/outputs of generated
  instances are recorded on the **parent** scope's nets (AC-3.2), not lost.
* `expr_net` now resolves `ElementSelect`/`RangeSelect`/`MemberAccess`/
  `Parenthesized`/`Conversion` to the base net, because a generate loop
  normally binds `b[g]`; without it the driver endpoint of every loop output
  was still dropped even after the walk was fixed. The endpoint attaches to the
  base net (`…u_mid.t`), not to a bit.
* `Bodies` (address-based body de-duplication) is unchanged: the walker expands
  a deduplicated body once **per instance**, so `arr.u_arr[0]` and
  `arr.u_arr[1]` each get their generated children even though the AST stores
  `mid`'s body once.

## 4. Regression tests (all pass)

`crates/vd-deeplint/tests/hierarchy.rs` — four new tests, plus a real project
fixture:

* `every_design_root_is_elaborated_not_only_the_first` — captured AST
  (`tests/fixtures/hierarchy/multi-root-generate.json`, real slang output): roots
  `["arr","top1","top2"]`, **19** elaborated instances (3 roots + 2 array
  elements + 3×3 generated instances + 2×3 generated instances).
* `generate_constructs_and_instance_arrays_are_walked` — `top1.u_mid.g_leaf[1].u`
  exists, module `leaf`, `mid.sv` line 5 (0-based), `g_if.u_if` exists, and the
  two array elements resolve to `mid`.
* `ports_inside_a_generate_block_reach_the_parent_signal` — `top1.u_mid.t` lists
  `top1.u_mid.g_leaf[0].u.b` and `g_leaf[1].u.b` as drivers; `top1.u_mid.a`
  lists `g_leaf[0].u.a` and `g_if.u_if.a` as loads.
* `the_real_tool_elaborates_every_root_and_generate_instantiation` — re-runs
  real slang on `tests/fixtures/hierarchy/project/` and asserts the same 19
  instances and the generate signal, so a slang shape change cannot pass
  silently.

Gate output: `evidence/w6-hier/09-t46-gate-tests.txt`
(`cargo test --offline -p vd-deeplint -p vd-ffi` → 43 + 12 + 2 + 10 tests pass,
exit 0), `09-t46-gate-clippy-fmt.txt` (`clippy -D warnings` exit 0,
`cargo fmt -p vd-deeplint -- --check` exit 0).

## 5. Re-measured elaboration numbers (they replace the T44 block)

Full table in `evidence/w4-lint/scale/README.md` §"T46 correction"; the old
`logs/50-elaboration.log` block is marked **VOID** there and kept for the
record. New log: `evidence/w4-lint/scale/logs/50-elaboration-t46.log`.

Headline: flat 1k whole project **11000 instances, 0.69 s, 265 MiB**; nested 1k
whole project **11000 instances, 1.34 s, 527 MiB**; nested 500 files
**5500 instances, 0.51 s, 227 MiB**. `max_instances` (10 000) now raises the
AC-3.5 "module tree may be slow" warning instead of truncating.

## 6. What is verified and what is not

Verified:
* counts against an independently produced AST ground truth (above), twice
  (harness + Rust test on a real project);
* the four regression tests and the crate gates (exit codes quoted);
* the C ABI path end-to-end through `vd_hier_start`/`vd_hier_poll` (the harness
  in `evidence/w4-lint/scale/scale_harness.cpp` uses the shipped ABI and the
  relinked `target/release/libvd_ffi.a`).

Not proven:
* **timing deltas**: the after-fix wall times were taken on a shared machine
  with other agents building; noise is tens of percent. No speed claim is made —
  only the instance counts are exact.
* **bit-level signal attribution**: a `b[g]` connection is recorded on the base
  net `b`; per-bit buckets are not modelled.
* **Qt/module-tree rendering** of the new `g_leaf[0]` path segments: the C++
  lane (T17) consumes the same `InstanceNode.path` strings but was not built or
  clicked in this task; `cmake` configure of the root project passes, the panel
  itself is T17's evidence.
* `max_depth` reported by the harness (7) counts path segments; the AST's
  instance-ancestor depth is 6 — same design, different definition.

## 7. Reproduce

```bash
# fix + tests (host, pinned toolchain via stable, offline)
CARGO_HOME=$PWD/target/cargo-home RUSTUP_TOOLCHAIN=stable \
  cargo test --offline -p vd-deeplint
# measurement
CARGO_HOME=$PWD/target/cargo-home RUSTUP_TOOLCHAIN=stable \
  cargo build --offline --release -p vd-ffi
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local bash -lc '
  g++ -std=c++20 -O2 -fPIC -I src/workspace/diagnostics -I crates/vd-ffi/include \
    $(pkg-config --cflags Qt6Core) evidence/w4-lint/scale/scale_harness.cpp \
    target/qt6-build/src/app/workspace-lane-diagnostics/libvedit_workspace_diagnostics.a \
    target/release/libvd_ffi.a $(pkg-config --libs Qt6Core) -lpthread -ldl -lm \
    -o target/tmp/scale/scale_harness'
QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg \
  target/tmp/scale/scale_harness --root "$PWD/target/tmp/scale-nested-1k" \
  --slang "$PWD/build/slang/out/slang" --mode elaborate
# expect: ELABORATE … instances=11000 … warning=«the design has 11000 instances …»
```
