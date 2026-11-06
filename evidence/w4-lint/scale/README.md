# ADDENDUM A-12 · scale proof — 1k files / 10k instances, measured

Task **T43**. The requirement bound is **1k files / 10k instances stays
interactive**; the spike-only bound is **30 s / 1 GiB for a full pass**; a UI
stall above ~100 ms counts as not interactive. This directory is the measurement,
not an assumption.

Reproduce: `bash evidence/w4-lint/scale/run.sh` (regenerates the fixtures under
`target/tmp/`, builds the harness against the shipped lane library + the pinned
Rust service, and rewrites `logs/`). Toolchain of record: pinned
`vedit-qt6-build:local`, `slang 12.1.0+0` (`build/slang/out/slang`).

## Fixture shape (deterministic; never committed)

`generate.py --files 1000` writes 1000 `.sv` files under `rtl/d0..d9` (100 per
directory, 10 generated files per directory level in the 2k variant). Every file
defines one leaf module and instantiates it **10 times**, so the 1k tree carries
**exactly 10,000 instances** (generator prints `GENERATED … files=1000
instances=10000`). The filelists are the user's real shape and exercise A-9:
`project.f` uses `+incdir+$SCALE_ROOT/inc` and `-f lists/all.f`; `lists/all.f`
is a second nested level that imports `lists/dirs/dN.f`; each `dN.f` lists its
100 files relative to its own directory. Expansion of `project.f` on the 1k tree
reports `files=1001 skipped=0 missing=[]` (1000 sources + the scanned `.vh`), so
the `$VAR` + 3-level nested `-f` path really ran.

## Numbers (logs)

| measure | 1k files / 10k inst | 2k files / 20k inst | bound | verdict |
|---|---|---|---|---|
| project expansion (wall, whole process incl. Qt startup) | **0.067 s** (baseline 1-file 0.034 s; repeat runs 0.068 s) | 0.112 s | — | expansion is ~33–39 ms at 1k |
| expansion peak RSS | **26.2 MiB** | 27.0 MiB | 1 GiB | comfortably inside |
| project-level lint pass (real `slang`, 1000 child runs) | **5.73 s** (repeat 5.64 s), done 1000/1000 | 11.37 s (repeat 11.25 s), done 2000/2000 | 30 s | **inside** |
| UI tick during the pass (refresh + event loop, 10 ms cadence) | **worst 0 ms**, 568 ticks | worst 0 ms, 1126 ticks | ≤100 ms | **inside** |
| progress accuracy | monotonic, 568 samples, reaches 100 % | same | — | correct |
| cancel latency at scale | **0 ms** controller round-trip, `已取消 — 88/1000 文件`, zero diagnostics after | 2k: `已取消 — 52/2000 文件` | bounded | **true stop** |
| over-scale path (2k = 2×) | — | start message **`large pass: 2000 files — cancel is available`**, cancel lands in `Cancelled` in 0 ms, no hang | warn + cancel | **fires** |
| pass process peak RSS (harness: Qt + Rust service + diagnostics) | **16.4 MiB** | 16.4 MiB | 1 GiB | inside |
| one slang child (runs sequentially, one at a time) | 15.9 MiB, 5 ms | — | — | peak concurrent ≈ 32 MiB |

Commands are in `logs/10-expansion.log`, `logs/20-lint-1k.log`,
`logs/30-cancel-and-2k.log`, and the one-shot `logs/10-40-scale-run.log`
(the run.sh output). The harness prints `RESULT … worst_tick_ms=…` and exits 0
(5 or 7 checks PASS).

## ADDENDUM A-12 nested variant (T44): depth 6 + params + generate

The flat fixture measured **width**, not elaboration depth: each file defined a
leaf and instantiated it 10 times as plain instances (chain depth 2, no
`generate`, no parameter propagation). The nested variant adds the shape real
SystemVerilog has.

**Fixture shape** (`generate.py --variant nested`, 1000 files, still 10,000
design instantiations):

```
l0_N #(.W(W=3))            depth 1 / root
  └ l1_N #(.W(W))          depth 2
      └ l2_N #(.W(W))      depth 3
          └ l3_N #(.W(W))  depth 4
              └ l4_N #(.W(W))          depth 5
                  └ generate for (g=0..5) leaf_N #(.W(W)) ×6   depth 6
```

* **Longest instance chain**: `l0 → l1 → l2 → l3 → l4 → leaf` = **6 instance
  levels** (5 hops below the top module).
* **Instance distribution over depth** (measured on one file with
  `slang --ast-json --top l0_m_0_0`): `{depth 0: 1 (root), 1: 1, 2: 1, 3: 1,
  4: 1, 5: 6}` = 11 instance nodes, of which **10 are design instantiations**
  per file → 10,000 across 1000 files (plus 1000 roots = 11,000 AST nodes).
  The full AST is kept at `logs/ast-nested-one-file.json`.
* **Instantiation style**: 4 plain parameterised instantiations (`#(.W(W))`)
  plus **one `generate for` layer** (6 elaborated leaves). Parameters propagate
  l0 → l1 → l2 → l3 → l4 → leaf; the AST shows `W = 3` on all six leaf bodies
  (the value l0 was instantiated with), so overrides really flow down the chain.
* Generated leaves drive **distinct bits** (`b[g]`), so every file is clean
  (`0 errors, 0 warnings`) like the flat fixture — the comparison is not
  polluted by diagnostics.
* Tool of record: **real `slang`, `slang version 12.1.0+0`**
  (`build/slang/out/slang`), through the shipped FFI batch service — **not** the
  fake backend the T8 orchestration probe uses. The log records the version.

**Nested vs flat (same machine, same pass, real slang, 3 runs each):**

| measure | flat (A-12) | **nested (T44)** | bound | verdict |
|---|---|---|---|---|
| expansion wall (whole process) | 0.067–0.103 s | **0.078 s** | — | both ≈ 0.1 s; scan-bound |
| expansion peak RSS | 26.3 MiB | **26.3 MiB** | 1 GiB | inside |
| full lint pass (3 runs) | 5.86 / 5.93 / 8.74 s | **5.81 / 5.88 / 6.07 s** | 30 s | inside; nested is **not slower** (delta within run-to-run noise; the 8.74 s flat run was captured while the machine was loaded) |
| worst UI tick | 0 ms | **0 ms** | ≤100 ms | inside |
| progress | monotonic, reaches 100 % | monotonic, reaches 100 % | — | correct |
| cancel latency | 0 ms (A-12) | **0 ms** (`已取消 — 83/1000 文件`) | bounded | true stop |
| diagnostics after cancel | none | **none** (50 further polls) | — | true stop |
| peak RSS of the pass process | 16.4 MiB | **16.4 MiB** | 1 GiB | inside |

Conclusion for T22: the cost at this scale is dominated by **per-file process
spawn** (≈5.8–6 ms/file incl. `slang`), not by elaboration depth — the nested
depth-6/generate/parameter fixture lands in the same band. The number T22 can
cite is therefore **~5.9 s (median) for 1000 files / 10 000 design instances,
depth-6 hierarchy, real slang 12.1.0+0, worst GUI tick 0 ms, 0 ms cancel**, far
inside the 30 s / 1 GiB bound. A 2× (2k-file) pass remains inside the bound too
(11.3–11.4 s) and trips the over-scale warning at START.

## T46 correction — the elaboration block in `logs/50-elaboration.log` is VOID

`logs/50-elaboration.log` (the elaboration numbers recorded for T44/A-12) was
measured on the hierarchy service **before T46**. That service walked only the
first design root and never entered a `GenerateBlockArray`, so on these very
fixtures it returned **5 instance nodes** (nested, whole project) and **11**
(flat, one `--top`) where the elaborated design has **1000 roots / 11 000
instance nodes**. Every elapsed / RSS figure in that block describes ~0.1 % of
the work: **do not quote it**. The old numbers stay in place, visibly
superseded, instead of being deleted.

After the T46 walker fix (multi-root + generate/instance-array traversal, with
regression tests in `crates/vd-deeplint/tests/hierarchy.rs`), the same harness,
the same fixtures and real `slang 12.1.0+0` report
(`logs/50-elaboration-t46.log`):

| case | returned instances | ground truth | wall | peak RSS |
|---|---|---|---|---|
| flat 1k, `--top m_0_0` | 11 | 11 | 0.11 s | 91 MiB |
| flat 1k, whole project (no top) | **11000** | 11000 | 0.69 s | 265 MiB |
| nested 1k, `--top l0_m_0_0` | 11 | 11 | 0.10 s | 95 MiB |
| nested 1k, whole project (no top) | **11000** | 11000 | 1.34 s | 527 MiB |
| nested 100 files | 1100 | 110 × 10 | 0.11 s | 89 MiB |
| nested 250 files | 2750 | 275 × 10 | 0.25 s | 123 MiB |
| nested 500 files | 5500 | 550 × 10 | 0.51 s | 227 MiB |

Ground truth was measured independently on the same trees with
`slang --ast-json` + `ast_count.py`:
`flat roots=1000 instance_nodes=11000 max_depth=2`,
`nested roots=1000 instance_nodes=11000 max_depth=6`.

Two reading notes:

* the harness's `max_depth` counts dot-separated **path segments**, so the
  nested run prints 7 (`…l4.g_leaf[0].u`) while the AST's instance-ancestor
  depth is 6 — the same design, two definitions;
* wall times were taken on a shared machine while several agents were
  building; run-to-run noise is tens of percent, so **no before/after speed
  claim is made**. Only the counts are exact, and they now equal ground truth.

The C ABI conversion still passes **every** instance through (no cap); the
`max_instances = 10 000` oversize threshold now correctly raises the AC-3.5
warning at 11 000 instances ("the module tree may be slow") instead of silently
truncating the report.

## Honest comparison and limits

* **Bound met with wide margin**: 5.6–5.7 s vs 30 s, 16–32 MiB vs 1 GiB, 0 ms vs
  100 ms stall. The pass is dominated by spawning 1000 `slang` children
  (≈5.7 ms/file wall incl. spawn); the Rust worker runs them off the GUI thread,
  which is why the GUI tick stays at 0 ms.
* The 1k fixture's files are **clean** (0 diagnostics). A warning-heavy project
  would add diagnostics to the display layer and to the Rust result buffers; this
  measurement does not cover that memory growth (the per-file cap is applied by
  the ABI, `max_diagnostics`).
* Peak RSS for the **pass** is the harness process (Qt Core + Rust service +
  collected diagnostics); it does **not** include the parent shell/UI process,
  and child RSS is listed separately because children run one at a time.
* Expansion was measured through the Qt project-lane selftest process, so the
  0.068 s includes Qt startup (baseline 0.029 s); the expansion delta is ~0.039 s
  at 1k. A pure Rust figure would be lower — this is the product's real process.
* **Not measured**: file-tree/panel painting during the pass (headless
  environment, no display), the shell's toolbar buttons (T20 wiring), and a
  single file containing 10k instances (the fixture spreads 10k instances over
  1k files, which is the requirement's stated shape).
* No product-code change was needed; no defect was found. The numbers above are
  the whole finding.
* **Nested-variant caveat**: the 10,000 instances are still 10 per **file**; a
  single file with a 10k-deep/wide design is not measured. Elaboration cost at
  this scale is invisible next to 1000 `slang` spawns, which is why depth does
  not move the number.
