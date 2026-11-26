# VEdit 0.5.0 window smoke version

Tested release checkout `80ff10cbe46106c74785636434b3c028037776ad` with the changes to [`cmake/smoke/CMakeLists.txt`](../../cmake/smoke/CMakeLists.txt#L17) and [`cmake/smoke/qt6_window_smoke.cpp`](../../cmake/smoke/qt6_window_smoke.cpp#L129). The smoke target takes its version from the root CMake `PROJECT_VERSION` and exposes it through Qt’s `--version` option.

The build ran without network access in `vedit-qt6-build:local` (`sha256:809c8072ac65be85a15bfc010400f1a0b2cffa10966cfebd4e7dcd0b418360c1`). The image’s package lock matched `scripts/docker/qt6-locked-packages.txt` byte for byte. The target used Qt 6.10.2, KF6 6.24.0, tree-sitter 0.25.9, CMake 4.2.3 and GCC 15.2.0.

An initial full-checkout configure with `VEDIT_BUILD_RUST_FFI=OFF` stopped because the project workspace requires `vd_ffi`. The successful run copied the release checkout’s root `CMakeLists.txt`, `cmake/` and `scripts/versions-qt.env` into `/tmp/vedit-release-smoke-src` in the container and used the root CMake file’s existing smoke-only path. The standalone terminal was configured directly from the release checkout at `/vedit-build`. The application About and CLI version paths and package metadata were checked in source.

```sh
cmake -S /tmp/vedit-release-smoke-src -B /tmp/vedit-release-smoke -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVEDIT_BUILD_RUST_FFI=OFF
cmake --build /tmp/vedit-release-smoke --target vedit-m0-smoke --parallel 2
smoke_build=/tmp/vedit-release-smoke
smoke="$smoke_build/cmake/smoke/vedit-m0-smoke"
install -d -m 700 "$smoke_build/xdg-runtime"
export XDG_RUNTIME_DIR="$smoke_build/xdg-runtime"
QT_QPA_PLATFORM=offscreen "$smoke" --version
ctest --test-dir "$smoke_build" -R '^vedit-m0-smoke$' --output-on-failure
xvfb-run -a -s '-screen 0 1280x800x24' env QT_QPA_PLATFORM=xcb \
  QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1 "$smoke" --selfcheck --hold 250
cmake -S /vedit-build/src/terminal -B /tmp/vedit-release-terminal -G Ninja
```

| Check | Observed result |
| --- | --- |
| Smoke `--version` | `vedit-m0-smoke 0.5.0`, exact match to `bash scripts/ci-qt6.sh version` with the executable name prepended, exit 0 |
| Offscreen CTest | `100% tests passed, 0 tests failed out of 1` |
| Xvfb/xcb window | `M0-SMOKE ok=1 platform=xcb`, `window=1 editor=1 document=1`, exit 0 after the 250 ms hold |
| Root and standalone terminal CMake caches | Both contained `CMAKE_PROJECT_VERSION:STATIC=0.5.0` |
| Cargo manifest and lock | All 11 local packages inherit workspace version `0.5.0` and have matching lock entries |
| Qt application | `src/app/main.cpp` sets `0.5.0` and registers `--version`. About reads `QCoreApplication::applicationVersion()` in `src/app/app_shell.cpp`. |
| GTK CLI and package metadata | `vd-app` reads `CARGO_PKG_VERSION`. Package filenames and `X-AppImage-Version` derive from `Cargo.toml`. |

Smoke binary SHA-256 was `be977672f54708a274cdc3a5f38ea6d0d1f8515c2427a531a663529f575fb3ba`.
