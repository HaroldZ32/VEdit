# VEdit 0.5.0 publication verification

**PASS.** GitHub published stable release `459521284` as **VEdit 0.5.0** on `2026-11-26T14:28:54Z`. The anonymous GitHub API returned `draft=false`, `prerelease=false`, and exactly four uploaded assets. The public release notes matched `docs/releases/0.5.0.md` at the tagged commit after trimming trailing whitespace. [Public release](https://github.com/HaroldZ32/VEdit/releases/tag/v0.5.0)

## Source and publication

| Record | Verified identity |
|---|---|
| Release PR | #26, merged at `2026-11-26T13:35:27Z` |
| Corrected release head | `edfb00756dd3516854f7512e6489ea9d77636f82` |
| Certified PR merge-test commit | `388689d892ce438e9fbf4870191b4bf19eafbf91` |
| `main` merge commit and tag target | `144acadab77cb45d1bd680beeee0108bedc21f21` |
| Annotated `v0.5.0` tag object | `d5f1466cba6db1561faf1ead13835c3023693b36` |
| Shared source tree | `cd9235ce95f026c40157b765111f7b8abdb65412` |
| Tag workflow | `release`, run `45830664993`, attempt 1, event `push`, all seven jobs passed |
| Tag packaging and publisher | Jobs `139966257617` and `139974375078`, both checked out the tag target and passed |

The `main` merge has parents `8e94667e129327436b20569ea6f2996b36a8505e` and `edfb00756dd3516854f7512e6489ea9d77636f82`. Its tree matches the corrected PR merge-test tree. The tag workflow checked the annotated tag and `main` first-parent history, built its own packages, and verified all four uploaded assets before making the release public. [PR #26](https://github.com/HaroldZ32/VEdit/pull/26) · [Corrected PR certification](pr-certification.md) · [Tag workflow](https://github.com/HaroldZ32/VEdit/actions/runs/45830664993) · [Publisher job](https://github.com/HaroldZ32/VEdit/actions/runs/45830664993/job/139974375078)

## Public download verification

All four files were downloaded through the anonymous browser download URLs on `2026-11-26` UTC. Each returned HTTP 200. The independently computed raw file size and SHA-256 matched both the anonymous release API and the tag packaging job for every row.

| Public asset | GitHub asset ID | Bytes | SHA-256 |
|---|---:|---:|---|
| `VEdit-0.5.0-x86_64.AppImage` | 796268415 | 1076799992 | `20ec8f4c8a245ca09935e82c5e823e44d22def07168352780013a7f1e684692b` |
| `vedit-0.5.0-linux-x86_64.tar.gz` | 796268388 | 1616689803 | `dec63eb98c6f6b5407c52cb5ce4d6eaaf95d1226204e4f593f657a0e13081fdf` |
| `VEdit-0.5.0-x86_64.run` | 796268414 | 1616691102 | `a13f41b3ecf431922abacd3d351b54ade97cfcbefac588d594bd3bac2509f73c` |
| `SHA256SUMS` | 796268409 | 281 | `60629769165be79fddece8cba578799d05c5b165422048313b88bf7d456ed65e` |

```text
$ sha256sum --check --strict SHA256SUMS
VEdit-0.5.0-x86_64.AppImage: OK
VEdit-0.5.0-x86_64.run: OK
vedit-0.5.0-linux-x86_64.tar.gz: OK
```

The check exited 0. `cmp` confirmed the public `SHA256SUMS` is byte-identical to Actions artifact `15125519181` from the tag run. Recomputing sizes and hashes through `bash scripts/ci-qt6.sh release-assets` matched the four rows above. The raw packages and verification logs are in the reviewer checkout's ignored `target/release-cert/public/` directory. [Tag packaging job](https://github.com/HaroldZ32/VEdit/actions/runs/45830664993/job/139966257617) · [Tag manifest artifact](https://github.com/HaroldZ32/VEdit/actions/runs/45830664993/artifacts/15125519181)

## Compatibility tested

- The tag packaging job passed all three FUSE-less launch routes and confirmed all package contents matched the staged AppDir. The `.run` test preserved the expected exit 2 on an invalid fixture and removed its temporary extract.
- F2 isolation passed 25 checks each on the staged AppDir and extracted AppImage. The CentOS 7.9 offline probes passed smoke, no-Git/SVN startup, all 20 C ABI checks, and the SpinalHDL and Chisel examples. The container verifies glibc 2.17 userspace under the runner's kernel. Native CentOS 7 kernel 3.10 coverage remains outside this result.
- The public AppImage launched with `--appimage-extract-and-run` on x86_64 Linux under xcb/Xvfb and exited 0. Its CLI displayed `vedit-app 0.5.0` and About displayed `Version 0.5.0`. This run covers the documented FUSE-less route under X11. [Release notes and compatibility limits](../../docs/releases/0.5.0.md)
