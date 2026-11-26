# VEdit 0.5.0 release PR certification

PR #26 merged `release/0.5.0` into `main` after verification of head `edfb00756dd3516854f7512e6489ea9d77636f82` and merge-test commit `388689d892ce438e9fbf4870191b4bf19eafbf91`. Both have source tree `cd9235ce95f026c40157b765111f7b8abdb65412`. The merge test used base commit `8e94667e129327436b20569ea6f2996b36a8505e`. [Release PR #26](https://github.com/HaroldZ32/VEdit/pull/26)

| Check | Result |
|---|---|
| Corrected smoke version | Independently built the smoke target from `git archive` of `edfb00756dd3516854f7512e6489ea9d77636f82` in the pinned Qt6 image with network disabled. `vedit-m0-smoke --version` returned `vedit-m0-smoke 0.5.0`. Offscreen CTest passed 1/1, and the xcb/Xvfb smoke opened the window, editor, and document and exited 0. |
| Hosted CI | Run `45822647815`, attempt 1, passed all five jobs. The Qt6 job compiled `vedit-app 0.5.0`, passed xcb/Xvfb, translation, and documentation checks, and recorded 93 CTest passes, seven skips, and zero failures. |
| Hosted package gate | Packaging job `139933076661` built all three formats from the merge-test commit. Each matched the staged AppDir, launched without FUSE and reported `0.5.0`. F2 passed 25 checks each on the AppDir and extracted AppImage. CentOS 7.9 smoke and no-Git/SVN startup exited 0. The packaged C ABI selfcheck passed 20/20. |
| Independent artifact check | Downloaded the four Actions artifacts and matched their raw file sizes and SHA-256 to the packaging log. `sha256sum --check --strict SHA256SUMS` verified all three packages. The downloaded AppImage's documented `--appimage-extract-and-run` path launched on xcb/Xvfb. Its CLI and About dialog displayed `0.5.0`. |

[Corrected CI run `45822647815`](https://github.com/HaroldZ32/VEdit/actions/runs/45822647815) · [Package job `139933076661`](https://github.com/HaroldZ32/VEdit/actions/runs/45822647815/job/139933076661)

The seven CTest skips covered two SVN cases and five Slang or filelist E2E cases. The CentOS 7.9 container verified glibc 2.17 userspace with the runner's kernel. The checks apply to the corrected PR build. The tag workflow built the public release separately.
