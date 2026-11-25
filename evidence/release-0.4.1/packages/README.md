# Local v0.4.1 package verification

The shared release checkout built Qt6 against the pinned `vedit-build:local` and `vedit-qt6-build:local` images. The Rust build and service tests ran from the release checkout. Assembly reused the local F2, Verible, slang, Scala and VHDL build inputs. The assembly log records the shipped toolchain and pin checks. These checks exercised local candidate bytes before the hosted release build.

```bash
VEDIT_SKIP_IMAGE_BUILD=1 VEDIT_QT6_BUILD_JOBS=5 VEDIT_CI_LOG_DIR=target/ci-local-release \
  bash scripts/build-qt.sh --no-test
source scripts/lib-qt.sh
run_in_qt6_container ctest --test-dir target/qt6-build --output-on-failure \
  -R 'i18n|terminal|ffi|folder|shell'
VEDIT_PACK_EVIDENCE="$PWD/target/ci-local-release/package" bash packaging/runtime/build.sh
```

The build and all eight selected Qt tests exited 0. The Qt run covered the product C ABI, application shell, native terminal environment, host shell and restart routing. The compiled CLI reported `vedit-app 0.4.1`. Its SHA-256 was `571d04184757a57c6999e69c4a17ae048136511f772a4e4cc22670dde8f963e3`. [Focused Qt tests](qt-focused-ctest.log)

Baseline `bash -n` on the original runtime build, AppImage packer and self-check exited 0. The first full packaging pass found that the original preflight looked only in `.mpd/recon/verible/bin`, while the assembler also accepts `target/verible-pin/extract/*/bin`. The updated preflight accepted the same pinned Verible binary as the assembler. A subsequent version probe exposed that `VEDIT_STARTUP_TRACE` changes `--version` stdout even with a value of `0`. The verifier leaves it unset for exact CLI version checks and sets it for readiness checks.

The full packaging command exited 0. The shipping inventory gate passed 9 checks and F2 isolation passed 25 checks on both the source AppDir and the extracted AppImage. Runtime self-checks passed on both trees. The format verifier extracted each candidate without FUSE, matched the payload content and links against the same 13,665-file AppDir, and launched the Qt6 product to `launch-ready`. The actual `.run` forwarded a fixture path containing a space, returned the product's exit 2 and removed the temporary extraction. With an empty host `PATH`, the product reported both Git and SVN missing and reached `launch-ready`. [Gate results](gate-results.log) [Format smoke log](format-smoke.log)

| Local candidate | Bytes |
| --- | ---: |
| `VEdit-0.4.1-x86_64.AppImage` | 972,478,968 |
| `vedit-0.4.1-linux-x86_64.tar.gz` | 1,512,395,480 |
| `VEdit-0.4.1-x86_64.run` | 1,512,396,779 |
| `SHA256SUMS` | 281 |

`sha256sum --check --strict SHA256SUMS` exited 0 against the three files in `packaging/runtime/out/release/`. `cmp` confirmed the `.run` embedded gzip stream and the standalone tarball were byte-identical. The compiled Qt6 binary and F2 library also matched their extracted AppImage counterparts byte-for-byte. [Local package hashes](SHA256SUMS.local) [File sizes and checksum verification](asset-sizes-and-checks.log)

In the pinned CentOS 7 container (`glibc 2.17`), the executable AppImage reported its runtime version, and the actual tarball's `AppRun` and the `.run` both reached `launch-ready` with an empty host `PATH` and exited 0. The AppImage's extracted payload passed the CentOS runtime check in the full build. The standalone AppImage verifier reported 14 passes, one skip for optional project file-count output, and exit 1 for the host kernel check. A real CentOS 7 kernel 3.10 run remains unverified here. [CentOS format smoke](centos7-formats.log) [Standalone AppImage verification](standalone-appimage-checks.log)

A small package fixture gave the tarball a relative symlink and a hard-linked file. Extraction preserved both, and the `.run` payload compared byte-for-byte with the standalone `.tar.gz`. A symlink to the `.run` in a path with spaces preserved its argv and child exit 23 and selected the resolved archive as the restart program. An existing `--extract` directory was refused. Truncating the embedded archive produced a nonzero exit and removed the incomplete extract. The fixture also ran on CentOS 7 userspace with an empty `PATH`.
