# T23 — self-contained VEdit AppImage: evidence

Owner: Deep Worker · board **T23** (split key **T11**) · scope
`packaging/appimage/**` · evidence `evidence/appimage/`.

## Deliverable (current build)

`packaging/appimage/out/VEdit-0.1.0-x86_64.AppImage`
— **47,745,528 B**, sha256 **`60b111899444e9743a33750ebfdbbd6a5359fe690e6417120d9101bcede6b7ad`**
(frozen build; the container hash changes on a rebuild because squashfs stores
file timestamps — the payload `vd-app` sha256 does not).

The payload installs **the same `vd-app` binary as the tar.gz and `.run`**:
`sha256 809cf95326172fc58975b338cea390dcffd8d78d1261b4f1d75059a8950c6d77d`
(`payload-vd-app.txt`; the value equals `build/artifacts/vd-app`). `build-appimage.sh`
copies that artefact and **fails the build** unless the packaged copy has the same
sha256, so "one source, three products" is enforced, not just promised.

It starts in a **clean, offline `centos:7` container with no GTK/GtkSourceView
and no CJK font** under Xvfb, through `--appimage-extract-and-run` and
`APPIMAGE_EXTRACT_AND_RUN=1`, completes `--selftest all` with **52/52 checks**,
renders the UI with **Chinese text from its bundled font**, and leaves no
verible/slang/zombie process behind.

## Build discipline (do not "simplify" this away)

1. **The binary is not rebuilt here.** `scripts/build.sh` (release build, shared
   `RELEASE_RUSTFLAGS`) produces `build/artifacts/vd-app`; this lane copies it and
   asserts byte equality. Rebuilding a near-identical variant here would make the
   three products ship three different binaries and break the cross-product
   assertion in T27.
2. **Payload support data comes from `git archive HEAD`.** Fixtures are taken
   from a HEAD export (`archive_revision` is recorded in `source-tree.txt`), so a
   half-saved worktree cannot leak into the artefact.
3. `source-tree.txt` records `git_head`, the full `git_status_porcelain` of the
   build moment, and `archive_revision` (= `git_head`), which is what the freeze
   protocol asks for.

The frozen revision is
`e1e09d69033707adbb6f3cd7d7de4058c985613f`; `source-tree.txt` records
`git status --porcelain` = only the untracked readiness report, and
`archive_revision` = the frozen SHA. No script was changed after the freeze.

## Pinned inputs (all verified, nothing copied from `.mpd/recon/`)

| input | source | verification |
|---|---|---|
| Verible suite `v0.0-4298-g085eef05` | `VERIBLE_URL` in `scripts/versions.env` | tarball `VERIBLE_TARBALL_SHA256` + each binary (`verible-verification.txt`) |
| slang (T3 artefact, never rebuilt) | `build/slang/out/slang` | `SLANG_BIN_SHA256` (`payload-binaries.txt`) |
| `vd-app` | `build/artifacts/vd-app` (shared release build) | sha256 equality + `/vedit-build` leak gate (`payload-vd-app.txt`) |
| GtkSourceView 4.6.1 | pinned build image (`/opt/gsv4`) | bundled data + `.so` (`bundled-libs.txt`) |
| GTK3 / glib / pango / … | CentOS 7.9.2009 rpms | recursive `ldd` closure, 67 libraries |
| appimagetool | `APPIMAGETOOL_URL` + `APPIMAGETOOL_SHA256` | `appimagetool-verification.txt` |

## Payload layout

```
usr/bin/vd-app                      <- build/artifacts/vd-app (sha 809cf953…)
usr/lib/                            67-library GTK3/GtkSourceView closure
usr/lib/vedit/bin/                  verible-verilog-ls/-format/-lint (static), slang
usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/   13 loaders
usr/lib/gtk-3.0/3.0.0/immodules/    im-xim.so + im-ibus.so
usr/share/gtksourceview-4/          language-specs + styles
usr/share/glib-2.0/schemas/         GTK schemas + gschemas.compiled
usr/share/fonts/dejavu/, wqy-zenhei/wqy-zenhei.ttc
usr/share/icons/{Adwaita,hicolor}   complete icon themes
usr/share/vedit/testdata/definition/{a.sv,b.sv}   T25 fixture convention
etc/{loaders.cache.in,immodules.cache.in,fonts.conf.in}   @APPDIR@ templates
AppRun, io.github.vedit.VEdit.desktop, io.github.vedit.VEdit.png, .DirIcon
```

## AppRun

* Relocatable: every path is under `$HERE`; the three `@APPDIR@` templates are
  expanded into the runtime dir at launch.
* **Runtime directory** mirrors the tar.gz/.run launcher (T25):
  `$XDG_RUNTIME_DIR` → `$TMPDIR` (default `/tmp`) → explicit failure, directory
  name `vedit-runtime-$(id -u)`. A failed preferred path prints one line naming
  it and the fallback; when both fail, the error lists both paths and the
  remedy. With `XDG_RUNTIME_DIR` unset there is no extra warning.
* Exports `LD_LIBRARY_PATH`, `GDK_PIXBUF_MODULE_FILE`, `GTK_IM_MODULE_FILE`,
  `GSETTINGS_SCHEMA_DIR`, `GTK_DATA_PREFIX`, `GTK_EXE_PREFIX`, `GTK_PATH`,
  `FONTCONFIG_FILE`/`FONTCONFIG_PATH`, `XDG_DATA_DIRS`, `VEDIT_GSV_DATADIR`,
  `VEDIT_SLANG` and `VEDIT_SELFTEST_FIXTURES` (all defaulting to payload paths).

## Gates

**GLIBC** (`glibc-gate.txt`): every ELF in the payload (95 files) —
`ELFS_SCANNED=95`, `MAX_REQUIRED_GLIBC=2.17`, `RESULT=PASS`.

**RUNPATH** (`rpath-gate.txt`, data file `packaging/appimage/rpath-allowlist.txt`):
`ELF_SCANNED=95`, `ELF_WITH_RUNPATH=1`, `ALLOWLIST_HITS=1`, `UNKNOWN=0`,
`RESULT=PASS`. The only entry is the known CentOS 7 absolute path
`/usr/lib64/elfutils` on `libdw.so.1` (recorded, not patched; RUNPATH is searched
after `LD_LIBRARY_PATH` and dwfl is never used by the editor).

**Embedded build paths** (`host-path-audit.txt`) — the count is measured on
**NUL-delimited strings** (`tr '\0' '\n' < vd-app`), not on `strings`, because
`strings` glues adjacent literals and inflates both the count and the apparent
"path":

- `/vedit-build`: **0**
- `/root/.cargo`, `/home/...`: **0**
- `SANDBOX_SOURCE_PATH_LITERALS=508`: panic source locations remapped to the
  **sandbox root** `/vedit` (dependency sources under
  `/vedit/target/cargo-home/registry/...`, app sources under `/vedit/crates/...`).
  These are build metadata, not a host checkout path, and nothing reads them at
  runtime. They can be reduced to 0 by adding
  `--remap-path-prefix=/vedit/target/cargo-home=/cargo` to `RELEASE_RUSTFLAGS`
  (reported to the T21/T7 owners; not changed here).
- The earlier `env!()` fixture literal is **gone**: `vd-app` derives the fixture
  root at runtime (T21 `b6c31d3`), and the payload's `VEDIT_SELFTEST_FIXTURES`
  points at `usr/share/vedit/testdata`. The previous T23 wording ("no
  `/vedit-build` strings at all") was inaccurate — at the T24 build there was one
  `env!`-derived literal that `--remap-path-prefix` cannot rewrite; the audit now
  prints the count and its classification instead of claiming zero.

## Clean-room acceptance (offline, no network, Xvfb)

Image: `packaging/appimage/Dockerfile.test` = `centos:7` + Xvfb/xwd/glxinfo
only; it asserts it has no gtk3/gsv packages. Harness sets a 32-hex
`/etc/machine-id` (a real CentOS 7 desktop always has one).

| check | observed |
|---|---|
| no system GTK/gsv | PASS (negative assertion) |
| no system CJK font | `host CJK fonts available before the payload runs: 0` → PASS |
| `--selftest all` via `--appimage-extract-and-run` | **exit 0, 52 checks, 0 failed** (`clean-room/selftest-all-summary.txt`) |
| `APPIMAGE_EXTRACT_AND_RUN=1 --selftest smoke` | exit 0 |
| direct execution (no `/dev/fuse`) | exit 127, FUSE error recorded (expected) |
| runtime dir: unusable `XDG_RUNTIME_DIR` | exit 0 + `cannot use XDG_RUNTIME_DIR=/proc/nonexistent …; using /tmp instead` |
| runtime dir: both unusable | **exit 1** + `tried '/proc/nonexistent' and '/proc/nonexistent'` + remedy line |
| runtime dir: `XDG_RUNTIME_DIR` unset | exit 0, no fallback warning; `/tmp/vedit-runtime-$(id -u)` used |
| input method modules | cache has `im-xim` and `im-ibus` with payload paths; effective `GTK_IM_MODULE_FILE=/tmp/vedit-runtime-0/immodules.cache`; `GTK_IM_MODULE=xim --selftest smoke` exit 0 without `Failed to load module` |
| bundled CJK font | fontconfig resolves `wqy-zenhei`; `screenshots/cjk-window.png` (22,025 B) shows `// 中文注释：你好，世界` and `logic 信号;` rendered, not tofu |
| app `--screenshot` | **33,223 B real image** (`screenshots/app-selftest-highlight.png`); the T24-era black capture was fixed by T21's single-window/layout-safe capture |
| dynamic dependencies of `vd-app` | 67 from payload, 8 from the host glibc family only; no `not found` |
| residual processes | no verible/slang process, no zombie |
| cold start (exec → window) | 191 / 193 / 191 ms (`sizes.txt`) |

## Payload size: how it is measured

Three products report different payload numbers because they measure different
things. This evidence records **all** of them, with the command and the exact
method, instead of picking a number that happens to match:

| method (inside the extracted payload) | bytes |
|---|---|
| **canonical**: `find . -type f -printf '%s\n' \| awk '{s+=$1} END{print s}'` — regular files only | **115,285,986** |
| `du -sb .` / `du -sb --apparent-size .` — apparent size, **counts 4096 B per directory inode** and symlink targets | 117,392,073 |
| `tar -cf - . \| wc -c` — archive stream, adds 512 B headers/padding | 120,023,040 |
| counts | 5,825 regular files, 470 directories, 68 symlinks (743 B of targets) |

The difference between the canonical number and `du -sb` is 2,106,087 B ≈
470 × 4096 B (directory inodes) + symlinks/rounding, which is exactly what `du`
adds. The earlier T23 report quoted **119,281,789 B**; that was `du -sb` on the
then-current payload (a different build before T21's layout changes), using the
directory-inclusive method — not a different payload with the same method. The
canonical number is the one that corresponds to what an installed tree occupies;
`AppImage_bytes` (47,745,528 B) is the compressed file a user downloads.

## Hash chain (why the artifact changed)

| build | AppImage sha256 | bytes | reason |
|---|---|---|---|
| T24 acceptance | `b589416e9492c6a2df333d861163d71ba3674305788a77e5b1f9c541e72412a4` | 48,105,976 | first clean-room-accepted AppImage |
| intermediate | `a102aaf69338832204040dfdec361e49fc8b6694a17e1540ea292f5dcacc667e` | 48,110,072 | AppRun runtime-dir alignment with T25 + `/vedit` sandbox remap |
| pre-freeze | `b24d0bfb5f1945d86be108d9e4cf27ee942e7469af1e7574df4b69d108863119` | 47,745,528 | consumes shared `vd-app` 809cf953… + T21 single-window/runtime-fixture fixes + T25 shared remap |
| **frozen `e1e09d69`** | **`60b111899444e9743a33750ebfdbbd6a5359fe690e6417120d9101bcede6b7ad`** | **47,745,528** | rebuild from the frozen commit; same `vd-app` sha256 (`payload-vd-app.txt`) |

The **container** hash is not byte-reproducible across builds (squashfs stores
file timestamps); the **payload binary** is (`809cf953…` every time). T27/T28
should key on the frozen container hash plus the `vd-app` sha256, both recorded
above.

## Handoff to T27 (e2e lane)

```
usr/lib/vedit/bin/verible-verilog-ls      07bf6b45…  (static)
usr/lib/vedit/bin/verible-verilog-format  b0a7b1ae…  (static)
usr/lib/vedit/bin/verible-verilog-lint    b9d56f9a…  (static)
usr/lib/vedit/bin/slang                   63cb60a0…  (dynamic, glibc ≤ 2.17)
usr/bin/vd-app                            809cf953…  (same bytes as tar.gz/.run)
```

- run as `usr/bin/vd-app` via `AppRun`; `--selftest all` is the single command
  covering LSP/format/slang/FIM-off;
- `VEDIT_SELFTEST_FIXTURES` defaults to `usr/share/vedit/testdata` (same as T25);
- cross-product assertion: `vd-app` sha256 must equal the tar.gz/.run copies;
- `ps` after a session must show no verible/slang child.

## Handoff to T29 (docs/notices lane)

- CJK font: WenQuanYi Zen Hei 0.9.46, **GPLv2 with exceptions**, upstream
  http://wenq.org/enindex.cgi, `usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc`.
- GTK input-method modules: `gtk3-immodule-xim` → `im-xim.so`,
  **`ibus-gtk3` → `im-ibus.so`**; there is **no** `gtk3-immodule-ibus` package.
- Emoji font is deliberately **not** bundled (size/licence).
- Runtime directory now follows the T25 contract; document
  `XDG_RUNTIME_DIR` → `TMPDIR` fallback and `vedit-runtime-$(id -u)`.

## Reproduce

```bash
bash packaging/appimage/run-evidence.sh
# package image -> consume build/artifacts/vd-app -> pinned downloads, AppDir,
# gates, AppImage -> clean-room image + offline Xvfb acceptance
```

## Limitations

- Direct AppImage execution cannot be tested here (`/dev/fuse` absent);
  `--appimage-extract-and-run` and `APPIMAGE_EXTRACT_AND_RUN=1` are verified.
- The AppImage carries the full Adwaita icon theme (44 MB) because a partial
  theme made GTK warn about missing symbolic icons.
- `libdw.so.1` keeps its absolute RUNPATH as decided; it is the only allowlisted
  absolute path and the gate fails on any other.
- 508 panic-source strings under `/vedit/...` remain (see the audit section);
  they are sandbox build metadata, not host paths.
