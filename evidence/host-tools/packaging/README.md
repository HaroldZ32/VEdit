# Host tools packaging verification

This batch covers the AppDir and AppImage delivery criteria in `host-svn-shell-20261125.md`. The packaged application uses host Git, SVN and shells. The package keeps its private Qt and glibc runtime, HDL tools, GNU diff and offline Scala payload. Package scripts run through host interpreters.

## Build and delivery

The final package uses launcher commit `33a857224011daa1aadbb5b534a8d28eaa4a338c` and the Qt binary built from `8643809224affc9fc2c32be1fd73315b57075ab4`. The release file is `packaging/runtime/out/VEdit-0.4.0-x86_64.AppImage` (972,487,160 bytes, SHA-256 `1944cb913bab1e1938b9e1641d9543dfb37e2318a093d5aea0d9a72233e22993`). The intermediate AppDir and `SHA256SUMS` are in the same directory. [Package receipt](package.log)

The worktree used the cached pinned `vedit-build:local` image with Rust 1.99.0 and `vedit-qt6-build:local` with Qt 6.10.2 and KF6 6.24.0. The slang, Verible and slang-server binaries and packing tools passed the packer's SHA-256 checks. F2 KSyntaxHighlighting, VHDL and Scala inputs came from existing local build caches. The initial Rust test failed before the pinned slang binary was staged. The build preflight log preserves that result and the artifact pin. The rebuild passed, along with pinned Rust formatting and Clippy and all seven focused Qt tests. [Build preflight](build-preflight.log) · [Pinned build and tests](build.log)

```sh
VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh --no-test
VEDIT_PACK_EVIDENCE="$PWD/target/packaging-build-logs/release" \
VEDIT_F2_EVIDENCE="$PWD/target/packaging-build-logs/release" \
  bash packaging/runtime/build.sh
(cd packaging/runtime/out && sha256sum --check --strict SHA256SUMS)
```

The build wrapper runs the Rust service tests even with `--no-test`. The packaging wrapper reported `steps_failed=0`. Both AppDir and AppImage selfchecks and F2 checks passed, and the pinned CentOS 7 userspace launched the extracted package with an empty PATH. The exact CI post-package checks ran locally and passed. The extracted application, probe, library and translations match the compiled inputs byte for byte. The shipped tool versions and both offline Scala examples passed. [Pinned CI package checks](pinned-ci.log)

## Runtime coverage

- The launcher probe passed 14 checks. It covers exact caller `PATH` restoration, including empty and unset cases, existing Verible and slang overrides, and the extracted versus ordinary mounted AppImage relaunch path. [Launcher probe](launcher.log)
- The host-path run supplies native Git 2.53.0 and SVN 1.14.5 outside the AppDir in an offline disposable container. Test wrappers assert the exact filtered `PATH`, the current `APPDIR`, and the absence of `LD_LIBRARY_PATH`, `LOCPATH` and `VEDIT_HOST_PATH` before running the native commands. The inherited path contains a real AppDir entry and an outside symlink into it. The host-path and empty-path launches both passed. [Host path and legacy payload checks](host-path.log)
- A separate copy gets legacy Git, SVN and bash launchers. The payload gate rejects that copy. The real app starts with both clients missing even when the caller points the PATH, shell and overrides at those launchers. The focused Rust `host_execution` test verifies both missing-client errors. A real AppImage with `PATH=` also displayed both messages in Source Control on isolated backend marker folders. [Packaged UI messages](missing-tools-ui.log)
- The real X11 AppDir and final AppImage relaunched from Preferences with an empty host PATH and no VCS clients. The final AppImage run used `--appimage-extract-and-run` on the command line, without setting extract mode in the calling environment. The pinned CentOS 7 cross-mount probe starts the production Qt restart entry through the old mount's loader, then starts the actual AppRun in a second mount. It checks that loader, locale, AppDir, tool and translation paths from the old mount are cleared. The negative controls reproduce stale old-mount resolution, CentOS loader failure and the lost CLI extraction mode. The focused Qt test also covers unset PATH, paths with spaces and non-UTF-8 bytes. [Restart evidence](restart.log)

The evidence includes selected command output and exit codes. Personal workspace paths are replaced with `<WORKTREE>`. Network access was disabled for the host tool and CentOS launch probes. FUSE-free execution and CentOS 7 userspace were exercised on the host kernel.
