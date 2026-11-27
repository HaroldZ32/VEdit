# Large-design signal trace

The baseline is `752c8ab8e2e22528f4e863da8318d81e177203e8`. The reproduction uses synthetic SystemVerilog and the repository’s pinned slang 12.1.0+0.

## Reproduction setup

Three possible failure points were examined. A compiler failure would leave a nonzero status or no usable AST. The trace runner’s size rejection would leave a valid AST above 100,663,296 bytes and report the exact threshold. An editor-side issue would instead leave the Rust navigation index ready with incorrect or missing endpoints.

The existing `evidence/ux-batch-2/u9a-catalog-scale/gen_design.py` generated a 1,176-file design with `--signals 20 --segment 10`. From the fixture directory, `slang --ast-json "$scratch/ast.json" --ast-json-source-info --Mall "$scratch/deps.txt" "$PWD/"*.sv` completed with zero errors and wrote **155,706,629 bytes**. The compiler binary SHA-256 was `63cb60a05f6ca57f648b8e2ef6de61ee0a3132c1c9d481f831dba52388832252`. [Compiler log](ast-size.log)

A release-build Rust probe called `elaborate_navigation` with the same options as the editor trace, then queried one cached `TraceIndex` for the declaration of `acc` in `mod_0.sv`. The baseline returned the `100663296 byte limit` failure with no index. The fixed run found 28,224 signals and followed `din` → `acc` to the procedural driver and `acc` → `q` to the continuous load. Their 0-based locations were `(3, 27)` and `(4, 13)` in `mod_0.sv`. [Baseline](baseline.log) · [Fixed](fixed.log)

A diagnostic control passed a 256 MiB raw byte limit only to the standalone baseline probe, allowing the old whole-document parser to finish on the same fixture and return the same locations. Linux `/proc/self/status` `VmHWM` was **517,396 KiB** for the whole-document control versus **242,460 KiB** for streaming navigation, about 53% lower for the Rust process. The fixed run took 2.65 s wall time versus 2.23 s for the whole-document control. The separate slang process had peak RSS readings of 628,220 KiB and 520,944 KiB respectively. These single runs establish the Rust memory difference on the fixture. The separately sampled compiler peaks and elapsed times varied across runs. [Whole-parser control](whole-parser-control.log)

## Product test

The native selftest imports a real filelist with a larger generated design, triggers the registered editor actions for the same signal, and asserts the expected source file and cursor position in each direction. A wrapper delegates to pinned slang and counts only trace ASTs by recording the size of each actual `--ast-json` file produced with `--Mall`. The test requires a dump above 100,663,296 bytes and exactly one elaboration across both requests. It also samples a 10 ms GUI event-loop timer while the cold trace is pending. Run from the repository root after building the test against the current Rust archive.

```sh
ctest --test-dir target/qt6-build -R '^vedit-signal-trace(-oversize)?-selftest$' -V
```

The independent native reviewer passed the small CTest and the oversize selftest (direct `--oversize` invocation of its test binary). The small test took 1.02 s and covered chains, repeated instances, an empty result, and source-change invalidation. On the oversize fixture the trace dumped **138,188,283 bytes**. The driver opened `leaf.sv:1:57` and the load opened `top.sv:6:19` in editor coordinates. The cold action took 2,497 ms and the 10 ms timer fired 235 times with a maximum observed gap of 19 ms. The warm action completed within the Qt timer's millisecond precision and the wrapper observed one elaboration total. The highest single-process RSS measured by GNU time was 528,516 KiB and the fresh test cgroup peak including page cache was 1,443,057,664 bytes. [Small Qt test](qt-regular.log) · [Oversize Qt test](qt-oversize.log)

## Rust coverage

With `VEDIT_SLANG` set to the pinned binary and an isolated Cargo target directory, `cargo +stable test --locked --offline -p vd-deeplint --lib --test hierarchy --test real_slang --test catalog` passed 186 tests and skipped 3 existing opt-in tests. An initial isolated run without `VEDIT_SLANG` failed the existing lint fallback test because that executable could not resolve the checkout's local slang. The successful run supplied the pinned fallback through `VEDIT_SLANG`. The new parser tests compare the full trace graph of the captured small design and verify cancellation and malformed input. `cargo +stable clippy --locked --offline -p vd-deeplint --all-targets -- -D warnings` also passed. [Rust test log](rust-tests.log) · [Clippy log](clippy.log)

## Limits

The byte size of the dump no longer gates editor tracing. The projection retains only navigation data and refuses above its charged retention budget. The expanded graph has a separate budget for instances, signals, edges and their indices. The accounting is a bound on retained work, with RSS recorded separately. The compiler still produces a temporary AST with its own resource use. The existing source-file cap, compiler timeout, JSON nesting guard and bounded trace results remain in effect. No customer project was available for these measurements.
