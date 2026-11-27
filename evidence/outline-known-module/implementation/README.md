# Known modules in the realtime Outline

On base `72028461bafb9598ec9322e709b94572311dea46`, two synthetic SystemVerilog scenarios showed `alternate` in the module tree and `unknown module 'alternate'` in Outline. The Qt regression observes the rendered Outline rows and the exact Slang session source list. Both scenarios keep `genuinely_missing` as a negative control. [Baseline output](baseline.log)

| Scenario | Catalog view | Realtime view |
| --- | --- | --- |
| Named top, multiple modules in one source | `scoped_top` reaches `alternate` through `scoped_bridge`. The unelaborated sibling shares the top's source file. | Opening the clean source also analyzes the sibling, which directly instantiates `alternate`. |
| Auto, parameterized generate | `variant_driver` passes `USE_ALTERNATE=0`, so the catalog edges for `variant` lead to `original`. `alternate` appears elsewhere in the tree. | Opening `variant.sv` alone uses its default `USE_ALTERNATE=1` and requires `alternate`. |

The active-buffer scan now supplements the elaborated dependency list using literal HDL instances and the existing catalog ownership map. For unchanged sources, elaborated edges stay first when the scope reaches the cap. The parser probe using the vendored grammar showed `interface_instantiation (interface_identifier …) (hierarchical_instance …)` for the generated `alternate` and `original` references. The query now recognizes that form alongside module and checker instances. The owner of the fixture's header definition is `lib/alternate.sv`, which the filelist explicitly imports. Source membership, shadowed-file handling, one-level traversal, 64-file document and 128-file session caps remain enforced by the existing scope filters.

After the patch, both scenarios add exactly `lib/alternate.sv` and show a deliberate bad-port error and `unknown module 'genuinely_missing'`. The existing unsaved, saved/reopen and mixed literal and macro checks also pass. The application smoke returned `FFI-ABI-SELFTEST ok=1 checks=20 failures=0`. The real `vedit-app` run on X11 accepted filelist import and displayed the known module beside the two genuine errors after switching from the filelist back to the saved HDL tab. The initial startup view had not yet published the active-file errors for this launch sequence. [Offscreen output](patched-offscreen.log) · [X11 regression](realtime-x11.log) · [Application X11 log](vedit-app-x11.log) · [Application screenshot](variant-outline-x11.png)

## Reproduce

Run from the isolated checkout. Use the pinned images and hash-checked HDL tools documented in the [previous toolchain evidence](../../realtime-slang-context/toolchain/README.md). The commands below run inside those containers with this checkout mounted at `/src` and the pinned tools exposed inside the mounts. The Qt job uses the repository's Release Ninja configuration.

```sh
# vedit-build:local
source scripts/versions.env
export RUSTUP_TOOLCHAIN="$RUST_TOOLCHAIN" RUSTFLAGS="$RELEASE_RUSTFLAGS"
export CARGO_TARGET_DIR=/src/target/outline-qa/rust CARGO_INCREMENTAL=0
cargo build --offline --locked --release -p vd-ffi

# vedit-qt6-build:local, network disabled
export VEDIT_NO_ENV_AUTOCAPTURE=1 QT_QPA_PLATFORM=offscreen
export VEDIT_SLANG=/src/build/slang/out/slang
export VEDIT_SLANG_SERVER=/src/target/outline-qa/bin/slang-server
export VEDIT_VERIBLE_LS=/tools/verible/verible-verilog-ls
export VEDIT_VERIBLE_BIN_DIR=/tools/verible
export VEDIT_VERIBLE_FORMAT=/tools/verible/verible-verilog-format
export VEDIT_VERIBLE_LINT=/tools/verible/verible-verilog-lint
cmake -S /src -B /src/target/outline-qa/qt -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DVEDIT_RUST_FFI_LIB=/src/target/outline-qa/rust/release/libvd_ffi.a \
  -DVEDIT_RUST_FFI_INCLUDE=/src/crates/vd-ffi/include
cmake --build target/outline-qa/qt --parallel 7
ctest --test-dir target/outline-qa/qt -V -R '^vedit-realtime-context-e2e$'
ctest --test-dir target/outline-qa/qt --output-on-failure -j 2
QT_QPA_PLATFORM=xcb xvfb-run -a -s '-screen 0 1500x1080x24 -nolisten tcp' \
  target/outline-qa/qt/src/app/workspace-lane-diagnostics/vedit-realtime-context-e2e
target/outline-qa/qt/src/app/vedit-app --selfcheck-ffi crates/vd-ffi/tests/fixtures/project
```

To reproduce the baseline, keep the final selftest and bind-mount the `HEAD` contents of `diagnostics_integration.cpp` and `realtime_module_references.cpp` over the two source paths in a separate build copy before compiling `vedit-realtime-context-e2e`. The baseline CTest exited 8. The patched real X11 test and application exited 0. Temporary fixture roots were shortened in the logs.

| Artifact | SHA-256 |
| --- | --- |
| Patched `vedit-app` | `9f4dbdba77d36a9cb10725165fc650e62f9f5e49134f2c461bdb1f87213b092a` |
| Patched `vedit-realtime-context-e2e` | `319ff34eb7c78a32704d4d405a9a8d199d8513b7103f75bd3f134ed79d00c0fc` |
| Baseline production with final regression fixture | `43fc6c67594c09e59e5a5334bbb0107c37c292a47f30f2d72db78f672e45bcde` |
| Slang CLI 12.1.0+0 | `63cb60a05f6ca57f648b8e2ef6de61ee0a3132c1c9d481f831dba52388832252` |
| slang-server 0.3.1+ | `134f4d0b495bb271bf701b109346ee0b28a348da4388cc408e0b85c191b66459` |
| Rust static archive, Rust 1.99.0 | `8bffe924b7624ebff8301ce94f6799740d493601c7636c897e7458a55ce99af5` |
| Application X11 screenshot | `9d42fa242cf4e6e7a7747f5e168133358c42f6b553737ee1fac6c60796707b7c` |

## Gate results and limits

The full pinned Qt build succeeded. CTest ran 100 entries, including all 12 focused gates. It recorded 97 passes, two explicit SVN tool skips and one failure (exit 8). `vedit-diagnostics-product-seam` expects the text `syntax error` in an annotation. Slang returned `expected ';'` and `expected a declaration name` on both the patched and baseline binaries. With the Verible fallback the patched seam passed all 20 checks (exit 0). The full run passed the included-header navigation gate. In earlier repeats, its second jump timed out on both production versions. The exact baseline repeat passed four of five runs and failed once with the same behavior. [Full gate results](qt-gates.log) · [Baseline repeat](baseline.log) · [Patched repeat](patched-offscreen.log)

The user's actual project and release package were unavailable for this test. Macro- or include-only instantiations without a catalog site still depend on the compiler's existing context. The lexical fallback can add a one-level candidate from an inactive conditional branch, so the unchanged-file path prioritizes the catalog's elaborated dependencies. The existing switch gate measured 10 ms p95 across 10 small-fixture switches. We did not benchmark newly parsed files near the 4 MiB analysis cap.
