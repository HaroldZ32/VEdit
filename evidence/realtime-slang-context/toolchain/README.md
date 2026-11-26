# Isolated toolchain and baseline reproduction

The build used `b614a4af1d57c6e4ce292a25e539722320189a4b` in the `realtime-slang-context` worktree. Docker mounted this checkout at `/src`. The Rust archive and all Qt executables were built into `target/qa/`. The reproduction added only the new test source and its CMake registration to the baseline production code.

| Component | Identity |
| --- | --- |
| `vedit-build:local` | `sha256:0798e93a6d3026424dfcda486014462298474a3e773e448e31856028f921a0e0`, Rust 1.99.0 |
| `vedit-qt6-build:local` | `sha256:809c8072ac65be85a15bfc010400f1a0b2cffa10966cfebd4e7dcd0b418360c1`, Qt 6.10.2, KF6 6.24.0, tree-sitter 0.25.9 |
| `build/slang/out/slang` | `12.1.0+0`, SHA-256 `63cb60a05f6ca57f648b8e2ef6de61ee0a3132c1c9d481f831dba52388832252` |
| `target/qa/bin/slang-server` | Shipped release `0.3.1+`, binary SHA-256 `134f4d0b495bb271bf701b109346ee0b28a348da4388cc408e0b85c191b66459`. The release archive passed the `scripts/versions.env` pin. |
| `.mpd/recon/verible/bin/verible-verilog-ls` | `v0.0-4298-g085eef05`, archive and three executables checked by `scripts/fetch-verible.sh` |

This command sequence runs from the isolated checkout after staging the tools above. `target/home` and `target/cargo-home` in the Rust container retain the local dependency cache. The Qt container runs offline. CTest and the application smoke use disposable configuration in each container.

```sh
mkdir -p target/qa target/home target/cargo-home
docker run --rm -e HOME=/src/target/home -e CARGO_HOME=/src/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust -e CARGO_TARGET_DIR=/src/target/qa/rust \
  -e CARGO_INCREMENTAL=0 -v "$PWD:/src" -w /src vedit-build:local \
  bash -ec 'source scripts/versions.env; export RUSTUP_TOOLCHAIN="$RUST_TOOLCHAIN" RUSTFLAGS="$RELEASE_RUSTFLAGS"; cargo build --locked --release -p vd-ffi'

docker run --rm --network none -v "$PWD:/src" -w /src vedit-qt6-build:local \
  bash -ec 'cmake -S /src -B /src/target/qa/qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DVEDIT_RUST_FFI_LIB=/src/target/qa/rust/release/libvd_ffi.a -DVEDIT_RUST_FFI_INCLUDE=/src/crates/vd-ffi/include; cmake --build target/qa/qt --parallel 8 --target vedit-app vedit-filelist-authority-e2e vedit-realtime-depth-switch-e2e'

docker run --rm --network none -e QT_QPA_PLATFORM=offscreen \
  -e VEDIT_NO_ENV_AUTOCAPTURE=1 -v "$PWD:/src:ro" -w /src \
  vedit-qt6-build:local target/qa/qt/src/app/vedit-app \
  --selfcheck-ffi crates/vd-ffi/tests/fixtures/project

# With the test-only change present
docker run --rm --network none -v "$PWD:/src" -w /src vedit-qt6-build:local \
  cmake --build target/qa/qt --parallel 8 --target vedit-realtime-context-e2e

docker run --rm --network none -e VEDIT_NO_ENV_AUTOCAPTURE=1 \
  -e VEDIT_SLANG=/src/build/slang/out/slang \
  -e VEDIT_SLANG_SERVER=/src/target/qa/bin/slang-server \
  -e VEDIT_VERIBLE_LS=/src/.mpd/recon/verible/bin/verible-verilog-ls \
  -e VEDIT_VERIBLE_BIN_DIR=/src/.mpd/recon/verible/bin \
  -v "$PWD:/src" -w /src vedit-qt6-build:local \
  ctest --test-dir target/qa/qt -V -R '^vedit-realtime-context-e2e$'
```

The baseline build exited 0 and the application reported `FFI-ABI-SELFTEST ok=1 checks=20 failures=0`. The app SHA-256 was `5a46dcdd052ab1250f332dca6863438025551637f1cfdba2f24a9cbf1970f980`.

The test-only build exited 0. The pinned-tool CTest exited 8. The refined fixture defines `alternate` in `include/alternate.svh`. Its imported owner `lib/alternate.sv` sets the macro that enables the declaration before including the header. These are selected runtime rows, with fixture paths shortened to their project-relative names.

```text
BEFORE-IMPORT pass=0 authored=[PROJECT_WIDTH=2] resolved=[] rows=[unknown module 'genuinely_missing'|use of undeclared identifier 'only_undefined_width']
AFTER-IMPORT pass=1 authored=[PROJECT_WIDTH=2] resolved=[PROJECT_WIDTH=2] rows=[unknown module 'genuinely_missing'|use of undeclared identifier 'only_width_two']
REPLACED pass=1 authored=[PROJECT_WIDTH=3] resolved=[PROJECT_WIDTH=3] rows=[unknown module 'genuinely_missing'|use of undeclared identifier 'only_changed_width']
REMOVED pass=1 authored=[] resolved=[] rows=[unknown module 'genuinely_missing'|use of undeclared identifier 'only_undefined_width']
DIRECT-CHILD tree=1 pass=0 inputs=[rtl/original.sv,rtl/top.sv] rows=[unknown module 'alternate'|unknown module 'genuinely_missing'|use of undeclared identifier 'only_undefined_width']
PROJECT-MANUAL-ONLY pass=1
```

The module tree located `alternate`. After the unsaved edit, realtime inputs retained `original.sv`, and the server reported both `alternate` and `genuinely_missing` as unknown. An independent rerun of the preserved baseline executable with the pinned tools in a read-only checkout mount reproduced the same rows and exited 1. Its SHA-256 was `a4308f444750b9343571b1dd45254ab7c45585308d2c4b5e3019d51a12473b75`.

The initial six-test baseline group using the upstream Slang 12.1 fallback returned five passes and one included-module navigation failure. The same baseline executable's included-module case passed in isolation with the project-pinned CLI and opened both the child and signal headers. The X11 attempt stalled in Xvfb startup before the application launched. The functional evidence here is from the offscreen Qt platform.

Raw local logs are in `target/qa/{rust-build,qt-build,baseline-abi,realtime-context-wrapper-red,realtime-context-wrapper-independent-red,baseline-include-pinned}.log`.
