# T23 — self-contained VEdit AppImage: evidence

Owner: Deep Worker · board **T23** (split key **T11**) · scope
`packaging/appimage/**` · evidence `evidence/appimage/`.

## Deliverable

`packaging/appimage/out/VEdit-0.1.0-x86_64.AppImage`
— **48,105,976 B** (payload 119,281,789 B extracted), built inside the pinned
CentOS 7 image from pinned sources only.

It starts in a **clean, offline `centos:7` container with no GTK/GtkSourceView
and no CJK font** under Xvfb, through `--appimage-extract-and-run` and
`APPIMAGE_EXTRACT_AND_RUN=1`, completes `--selftest all` with **51/51 checks**,
renders the UI with **Chinese text from its bundled font**, and leaves no
verible/slang/zombie process behind.

## Pinned inputs (all verified, nothing copied from `.mpd/recon/`)

| input | source | verification |
|---|---|---|
| Verible suite `v0.0-4298-g085eef05` | `VERIBLE_URL` in `scripts/versions.env` | tarball `VERIBLE_TARBALL_SHA256` + each binary's sha256 (`verible-verification.txt`) |
| slang (T3 artifact, never rebuilt here) | `build/slang/out/slang` | `SLANG_BIN_SHA256` (`payload-binaries.txt`) |
| GtkSourceView 4.6.1 | pinned build image (`/opt/gsv4`, `GSV_SHA256`) | bundled data + `.so` listed in `bundled-libs.txt` |
| GTK3 / glib / pango / … | CentOS 7.9.2009 rpms in the pinned image | recursive `ldd` closure, 67 libraries |
| appimagetool (continuous build) | `https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage` | `APPIMAGETOOL_SHA256` (`appimagetool-verification.txt`) |

The app binary is built from a **clean source state**: `source-tree.txt` records
`source_mode=worktree`, `git_head=12655203dd23c6dcd3d03825404d6941b2b895ef`,
`dirty_code_paths: none` (the packaging script falls back to a `git archive HEAD`
export when another lane has uncommitted code changes, and records that too).

## Payload layout

```
usr/bin/vd-app
usr/lib/                 67-library GTK3/GtkSourceView closure (glibc family excluded)
usr/lib/gtksourceview-4  never — data lives in usr/share/gtksourceview-4
usr/lib/vedit/bin/       verible-verilog-ls / -format / -lint (static), slang
usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/   13 loaders
usr/lib/gtk-3.0/3.0.0/immodules/         im-xim.so + im-ibus.so
usr/share/gtksourceview-4/{language-specs,styles}
usr/share/glib-2.0/schemas/              GTK schemas + gschemas.compiled
usr/share/fonts/dejavu/                  DejaVu Sans / Sans Mono
usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc
usr/share/icons/{Adwaita,hicolor}        complete icon themes
usr/share/vedit/testdata/definition/{a.sv,b.sv}   shared selftest fixtures (T25 convention)
etc/{loaders.cache.in,immodules.cache.in,fonts.conf.in}   @APPDIR@ placeholders
AppRun, io.github.vedit.VEdit.desktop, io.github.vedit.VEdit.png, .DirIcon
```

Binary identities are in `payload-binaries.txt` (name, bytes, sha256, static?):
verible trio = the pinned hashes, `slang = 63cb60a0…` (T3 pin).

## AppRun environment (no host path)

`LD_LIBRARY_PATH`, `GDK_PIXBUF_MODULE_FILE`, `GTK_IM_MODULE_FILE`,
`GSETTINGS_SCHEMA_DIR`, `GTK_DATA_PREFIX`, `GTK_EXE_PREFIX`, `GTK_PATH`,
`FONTCONFIG_FILE`/`FONTCONFIG_PATH`, `XDG_DATA_DIRS`, `VEDIT_GSV_DATADIR`,
`VEDIT_SLANG` (default = payload path), `VEDIT_SELFTEST_FIXTURES`
(default = `$HERE/usr/share/vedit/testdata`) — every path under `$HERE`. The
three `@APPDIR@` templates are expanded into `${TMPDIR:-/tmp}/vedit-runtime` at
launch. When `/etc/machine-id` is not a 32-hex id (bare containers) the AT-SPI
bridge is disabled so libdbus cannot abort the process; a real CentOS 7 desktop
keeps accessibility. The full launcher is captured in `AppRun.txt`.

## Gates

**GLIBC** (`glibc-gate.txt`): every ELF in the payload (95 files: main binary,
GTK stack, loaders, immodules, verible statics, slang) —
`ELFS_SCANNED=95`, `MAX_REQUIRED_GLIBC=2.17`, `RESULT=PASS`.

**RUNPATH** (`rpath-gate.txt`, data file `packaging/appimage/rpath-allowlist.txt`):
the allowlist is a checked-in data file, one justified line per exception. Scan
of all 95 ELFs: `ELF_SCANNED=95`, `ELF_WITH_RUNPATH=1`,
`ALLOWLIST_HITS=1`, `UNKNOWN=0`, `RESULT=PASS`.

```
ALLOWED  /usr/lib64/elfutils  usr/lib/libdw.so.1
```

Known absolute path, recorded and not patched (per the agreed decision):
CentOS 7's `libdw.so.1` carries `RUNPATH=/usr/lib64/elfutils`; the ebl backends
are only loaded lazily by dwfl (never by this editor), and RUNPATH is searched
after `LD_LIBRARY_PATH`, so the bundled `libdw` is the one that loads.

**Host-path audit** (`host-path-audit.txt`): the binary is rebuilt with
`--remap-path-prefix` so no `/vedit-build` or `/root/.cargo` strings are
embedded; the launch templates carry `@APPDIR@` placeholders only; the main
binary has no RPATH/RUNPATH.

## Clean-room acceptance (offline, no network, Xvfb)

Image: `packaging/appimage/Dockerfile.test` = `centos:7` + Xvfb/xwd/glxinfo
**only**; it asserts it has no gtk3/gsv packages. Harness sets a
32-hex `/etc/machine-id` (a real CentOS 7 desktop always has one).

| check | observed |
|---|---|
| no system GTK/gsv | PASS (negative assertion) |
| **no system CJK font** | `host CJK fonts available before the payload runs: 0` → PASS |
| Xvfb | PASS |
| `--selftest all` via `--appimage-extract-and-run` | **exit 0, 51 checks, 0 failed** (`selftest-all-summary.txt`) |
| `APPIMAGE_EXTRACT_AND_RUN=1 --selftest smoke` | exit 0 |
| direct execution (no `/dev/fuse`) | exit 127, FUSE error recorded (expected; extract-and-run is the documented CentOS 7 path) |
| input method modules | cache has `im-xim` **and** `im-ibus` with payload paths (`clean-room/immodules-cache.txt`); effective `GTK_IM_MODULE_FILE=/tmp/vedit-runtime/immodules.cache`; `GTK_IM_MODULE=xim --selftest smoke` exit 0 with no `Failed to load module` (`immodule-xim.log`) |
| bundled CJK font seen by fontconfig | `PASS: fontconfig resolves the bundled wqy-zenhei CJK font` (`clean-room/fonts.txt`) |
| **CJK actually rendered** | `screenshots/cjk-window.png` (22,033 B): the UI shows `// 中文注释：你好，世界` and `logic 信号;` rendered with the bundled font — visually confirmed |
| dynamic dependencies of `vd-app` | all resolve inside the payload; 67 from payload, 8 from the host glibc family only (`clean-room/ldd-extracted.txt`) |
| residual processes | no verible/slang process, no zombie (`clean-room/processes.txt`) |
| sizes / cold start | AppImage 48,105,976 B; payload 119,281,789 B; exec→window **198 / 194 / 197 ms** (`sizes.txt`) — well under the AC-18 targets |

## Known finding for the T21 lane: `--screenshot` is blank in this Xvfb

`--selftest all --screenshot` and `--selftest highlight --screenshot` write a
1,120×780 PNG of **5,270 B that is entirely black** in the clean room
(`screenshots/app-selftest-highlight.png`). The same binary writes a proper
image in the build image, and an **X-server capture of the visible window**
(`screenshots/cjk-window.png`) shows the fully rendered UI. The window tree
contains two `VEdit` toplevels (one 1,120×780 black, one 1,120×780 rendered), so
`App::capture_screenshot` appears to draw a different (hidden) window than the
one the user sees in this environment. T23 therefore uses the X capture as its
visual evidence and reports the defect to the T21 owner; no product code outside
`packaging/appimage/**` was changed.

## Handoff to T27 (e2e lane)

Payload paths for the acceptance scripts (all under the extracted AppDir):

```
usr/lib/vedit/bin/verible-verilog-ls      07bf6b45…  (static)
usr/lib/vedit/bin/verible-verilog-format  b0a7b1ae…  (static)
usr/lib/vedit/bin/verible-verilog-lint    b9d56f9a…  (static)
usr/lib/vedit/bin/slang                   63cb60a0…  (dynamic, glibc ≤ 2.17)
```

- run the binary as `usr/bin/vd-app` (AppRun sets the environment); `--selftest
  all` is the one command that exercises LSP, formatting, slang and FIM off-path;
- `VEDIT_SELFTEST_FIXTURES` defaults to `usr/share/vedit/testdata` (same
  convention as T25's tar.gz); `definition/a.sv` + `definition/b.sv` are the
  cross-file pair;
- AC-07 comparisons must use **this** `verible-verilog-format` (sha256 above);
- `ps` after a session must show no verible/slang child (the client reaps them).

## Handoff to T29 (docs/notices lane)

- CJK font: **WenQuanYi Zen Hei 0.9.46**, licence **GPLv2 with exceptions**,
  upstream http://wenq.org/enindex.cgi, file `wqy-zenhei.ttc` (17,083,583 B).
- GTK input-method modules: `gtk3-immodule-xim` → `im-xim.so`,
  **`ibus-gtk3` → `im-ibus.so`**; there is **no** `gtk3-immodule-ibus` package.
- Emoji font is deliberately **not** bundled (size/licence; emoji is outside
  Verilog code). GTK falls back to whatever the host provides for emoji.
- appimagetool is the continuous AppImage build identified by
  `APPIMAGETOOL_SHA256`; suggest pinning `APPIMAGETOOL_URL` next to it in
  `scripts/versions.env` (T7 owns that file).

## Reproduce

```bash
bash packaging/appimage/run-evidence.sh
# -> package image (vedit-build + immodules + wqy-zenhei)
# -> build-appimage.sh (pinned downloads, AppDir, gates, AppImage)
# -> clean-room image + offline Xvfb acceptance
# all logs under evidence/appimage/
```

## Limitations

- Direct AppImage execution cannot be tested here: the host has no `/dev/fuse`;
  `--appimage-extract-and-run` and `APPIMAGE_EXTRACT_AND_RUN=1` are verified
  instead, and the failure is recorded verbatim.
- The AppImage carries the full Adwaita icon theme (44 MB) because a partial
  theme made GTK warn about missing symbolic icons; this is the largest single
  payload item after slang.
- `libdw.so.1` keeps its absolute RUNPATH as decided; it is the only allowlisted
  absolute path and the gate fails on any other.
