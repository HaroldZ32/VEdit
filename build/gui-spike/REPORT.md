# T5 — gtk-rs (GTK3) + GtkSourceView on CentOS 7: feasibility spike and branch decision

- Cell `cell-20261030T020634155Z`, split revision 1, frozen contract `contracts/T2.md`
- Board task **T5** (split key **T2**), owner Deep Worker, verifier Reviewer
- Verification loop: `loop-20261030T022952-8d02af`
- Date: 2026-10-30 (Asia/Shanghai) · write scope used: `build/gui-spike/**` only
- Evidence: every text/log artifact lives under `build/gui-spike/evidence/`; the shipped
  products live under `build/gui-spike/out/`.

## 0. Decision (TL;DR)

**Branch A is proven and selected: gtk-rs 0.18 generation + GtkSourceView 4 built from source
(actual version 4.6.1) + upstream `verilog.lang` / `systemverilog.lang`.**

- A2 (independent criterion) = **PASS**: `libgtksourceview-4.so.0` was built in a
  `centos:7` container from the pinned upstream tarball, is linked by `sourceview4 0.5.0`
  through `pkg-config gtksourceview-4`, and the app runs under Xvfb.
- Both deliverables exist and pass the clean-room run:
  `VEdit-Spike-0.1.0-x86_64.AppImage` (18,430,456 B) and
  `vedit-spike-0.1.0-linux-x86_64.tar.gz` (19,169,299 B).
- The version deviation **4.8 → 4.6.1** is forced by an upstream constraint, not by this
  spike: GtkSourceView 4.8's `meson.build` requires `gtk+-3.0 >= 3.24` and CentOS 7 ships
  GTK 3.22.30. Section 3 records the raw meson error and the release matrix; the
  Verilog/SystemVerilog language definitions are **byte-identical** between 4.6.1 and 4.8.4.
- A′ (gsv 3.24.8 + gsv4 language files) was **not needed** and is not adopted; it is analysed
  in section 9 because the contract asks for the fallback decision. Branch B is void
  (captain's correction: it would be a self-written highlighting engine, which the
  requirements classify as a defect). Branch C (eframe/egui) was not used and requires
  captain approval that was never requested because gtk-rs links and runs.

## 1. Environment (measured)

Everything below was produced **inside `docker.m.daocloud.io/library/centos:7`**, i.e. the
deployment target itself, with the repository file from `.mpd/recon/build-recipe-centos7.md`
(TUNA centos-vault mirror, `https://mirrors.tuna.tsinghua.edu.cn/centos-vault/7.9.2009/...`).

`evidence/env-facts.txt`, `evidence/rustc-version.txt`:

```
centos_release=CentOS Linux release 7.9.2009 (Core)
glibc=glibc-2.17-326.el7_9.3.x86_64
gcc=gcc (GCC) 4.8.5 20150623 (Red Hat 4.8.5-44)
meson=0.59.4
ninja=1.11.1.git.kitware.jobserver-1
python3=Python 3.6.8
gtk3_pkg=3.22.30
glib_pkg=2.56.1
gsv4_pkg=4.6.1
gsv4_libdir=/opt/gsv4/lib64
gtk3_rpm=gtk3-3.22.30-8.el7_9.x86_64
language_specs=163
rustc 1.99.1 (58cf8f9c3 2026-10-13)
cargo 1.99.1 (5f94df478 2026-08-27)
```

Build image definition: `docker/Dockerfile.build` (CentOS 7 + `gtk3-devel` + `gdk-pixbuf2-devel`
+ `libxml2-devel` + `python3` + pip-installed meson/ninja + rustup stable + GtkSourceView
4.6.1 installed to `/opt/gsv4`). The GtkSourceView tarball is pinned by sha256 in the build
command:

```
sha256(gtksourceview-4.6.1.tar.xz) = 63f487a7ab59612ffa9e7be1883dbd274ab1016afcede3239b93b0fc384df874
```

GtkSourceView is LGPL-2.1-or-later; the tarball comes from `download.gnome.org` and the
version/sha256 are recorded here for the LGPL source-availability obligation (T11/T12 carry
the notices).

## 2. A2 — gsv4 builds and is correctly linked by gtk-rs 0.18

Commands (all inside the build image; run from `build/gui-spike`):

```
docker run --rm -v "$PWD:/work" -w /work vedit-gui-spike-build:local bash -lc 'bash scripts/build-app.sh'
```

Observed (`evidence/cargo-build.log`, `evidence/gsv4-lib.txt`, `evidence/bin-ldd.txt`):

- `pkg-config --modversion gtksourceview-4` → `4.6.1`
- built library: `/opt/gsv4/lib64/libgtksourceview-4.so.0` → `libgtksourceview-4.so.0.0.0`
- `readelf --version-info libgtksourceview-4.so.0` → highest GLIBC symbol version **`GLIBC_2.14`**
- `cargo build --release` → `Finished release profile ... in 23.78s` (exit 0)
- `ldd out/vedit-gui-spike` contains:
  `libgtksourceview-4.so.0 => /opt/gsv4/lib64/libgtksourceview-4.so.0`

Exact gtk-rs stack actually resolved (`evidence/cargo-lock-versions.txt`):
`gtk 0.18.2`, `gtk-sys 0.18.2`, `glib 0.18.5`, `gdk 0.18.2`, `gdk-pixbuf 0.18.5`,
`cairo-rs 0.18.5`, `sourceview4 0.5.0`, `sourceview4-sys 0.5.0`. No 0.19 crate is present.

Declared repo gates applicable to this standalone crate were run on the app
(`evidence/cargo-fmt.log`, `evidence/cargo-clippy.log`, `evidence/cargo-build.log`):

```
cargo fmt -- --check                        -> exit 0
cargo clippy --release -- -D warnings       -> exit 0 (no warnings)
cargo build --release                       -> exit 0
```

Note on the repository-wide gates in `AGENT.md` §4: they address the Cargo workspace that T7
creates (`cargo test --workspace`, `bun run verify:gates`, `build/slang/build.sh`). At this
milestone that workspace does not exist, and the spike is a standalone crate under
`build/gui-spike/app`; the same fmt/clippy/build gates were therefore run on that crate and
their exit codes are quoted above. This is a gate-availability statement, not a claim that
workspace gates were executed.

## 3. Version deviation: why GtkSourceView 4.6.1 and not 4.8

### 3.1 Raw failure of 4.8 on the target platform

`evidence/gsv4-4.8-meson-failure.txt` (meson 0.59.4, CentOS 7's GTK 3.22.30):

```
Run-time dependency gtk+-3.0 found: NO found 3.22.30 but need: '>= 3.24'
meson.build:86:0: ERROR: Invalid version of dependency, need 'gtk+-3.0' ['>= 3.24'] found '3.22.30'.
MESON_EXIT=1
```

GTK cannot simply be raised to 3.24 on the target: GTK 3.24 requires GLib ≥ 2.58 and
CentOS 7 has GLib 2.56.1, so it would drag a rebuilt GLib, Pango and the rest of the stack —
i.e. the "runs without any other environment" premise would be replaced by a home-grown
desktop stack. The version literal must yield to the platform constraint.

### 3.2 Release matrix (upstream `meson.build`, `evidence/gsv4-gtk-requirement-matrix.txt`)

| GtkSourceView | glib requirement | gtk+-3.0 requirement | builds on CentOS 7 (3.22.30)? |
|---|---|---|---|
| 4.4.0 | 2.48 | **3.22** | yes |
| 4.6.0 | 2.48 | **3.22** | yes |
| 4.6.1 | 2.48 | **3.22** | **yes — selected** |
| 4.8.4 | 2.48 | **3.24** | no (meson refuses) |

4.6.1 is the newest upstream gsv4 line that accepts the target's GTK.

### 3.3 The Verilog/SystemVerilog definitions are identical to 4.8.4

`evidence/language-spec-equality.txt`:

```
verilog.lang        4.6.1 = 4.8.4 = sha256 7670ddfd…  (475 lines, 282 <keyword>)
systemverilog.lang  4.6.1 = 4.8.4 = sha256 9d1f6741…  (262 lines, 197 <keyword>)
diff lines: 0 (both files)
total .lang files: 4.6.1 = 164 (163 installed; testv1.lang is build-only) vs 4.8.4 = 170
```

So the deviation costs **nothing** for this product's two languages; the six extra specs in
4.8.4 are other languages. 4.8's `.lang` files would also be usable as data if needed (same
`_version="2.0"` schema), but that is not necessary here.

## 4. AC-05 behavioural evidence (highlighting actually renders)

Fixture: `build/gui-spike/fixture/top.sv` (module / interface / package / class / always_ff /
constraint / covergroup / macro / comment / string / number / system task / data type).

Command (clean-room, offline container):

```
docker run --rm --network none -v "$PWD/out:/art:ro" -v "$PWD/evidence:/evid" \
  -v "$PWD/scripts:/scripts:ro" vedit-gui-spike-test:local \
  /bin/bash /scripts/clean-room-accept.sh
```

`--selftest` output (`evidence/clean-room/appimage-selftest.log`,
`evidence/clean-room/targz-selftest.log`): `SELFTEST PASS`, exit 0.

Observed style per construct with the upstream **cobalt** scheme (GtkSourceView does not
expose `keyword` as a *context class* for this language definition — the SV spec uses
`style-ref="keyword"` on its keyword context — so the probe reads the highest-priority
styled text tag at the token: `foreground-rgba` + weight):

| construct | probe | colour | weight | GtkSourceView style |
|---|---|---|---|---|
| module, interface, package, class, always_ff, constraint, covergroup | first occurrence | `#ff9d00` | 700 (bold) | keyword |
| `$display` | `$`+1 | `#ff9d00` | 700 | system task |
| `` `define `` | backtick | `#cccccc` | 400 | preprocessor |
| comment | `COMMENT_TOKEN` | `#0088ff` | 400 | comment (context class `comment`) |
| string | `STRING_TOKEN` | `#3ad900` | 400 | string (context class `string`) |
| number | `42` | `#ff0044` | 400 | number |
| data type | `logic` | `#80ffbb` | 400 | type |

`DISTINCT_COLORS=6`; all seven keyword-family constructs share one style and every other
category is distinct from it (`evidence/clean-room.log`, CHECK lines PASS).

Screenshots (rendered on the offline container, `LIBGL_ALWAYS_SOFTWARE=1`):

- `evidence/screenshots/targz.png` — tar.gz deliverable, 900×700, 76,237 B
- `evidence/screenshots/appimage-1.png` … `-3.png` — AppImage, 900×700, 76,237 B each
- `evidence/screenshots/dev-selftest.png` — build-image run of `--selftest --screenshot`
- Window existence from the X server (`evidence/clean-room/window-found.txt`):
  `0x200002 "VEdit T5 spike": ("vedit-gui-spike" "Vedit-gui-spike") 900x700+0+0  +0+0`
- `.v/.vh → verilog`, `.sv/.svh/.svi → systemverilog` mapping is implemented and asserted by
  the app (`LANG id=Some("systemverilog") name=Some("SystemVerilog")` for `fixture/top.sv`).

The `--screenshot` implementation deliberately avoids `gdk_window_get_pixbuf`:
on CentOS 7's cairo 1.15 it aborts with
`cairo-surface.c:1734: cairo_surface_mark_dirty_rectangle: Assertion ... failed`
(SIGABRT, measured during this spike). The app now draws the `GtkSourceView` widget into an
image surface with the widget's own draw handler and encodes PNG via cairo — deterministic,
no compositor, no GL.

## 5. Packaging — self-contained AppDir

`scripts/package.sh` builds the AppDir, then AppImage and tar.gz. Layout:

```
usr/bin/vedit-gui-spike
usr/lib/                   67 libraries (recursive ldd closure, glibc/libgcc/libstdc++ excluded)
usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/   13 loaders
usr/share/gtksourceview-4/{language-specs,styles}   (163 .lang incl. verilog+systemverilog, upstream styles)
usr/share/glib-2.0/schemas/ 6 GTK schemas + gschemas.compiled
usr/share/fonts/dejavu/     13 TTFs (incl. DejaVu Sans Mono)
usr/share/icons/{Adwaita(16-48px,index),hicolor}
etc/loaders.cache.in, etc/fonts.conf.in      (@APPDIR@ placeholders, rewritten by AppRun)
AppRun, vedit-gui-spike.desktop, vedit-gui-spike.png, .DirIcon
```

Full library list with each library's build-image origin: `evidence/bundled-libs.txt`
(67 entries, e.g. `libgtk-3.so.0 /lib64/libgtk-3.so.0`,
`libgtksourceview-4.so.0 /opt/gsv4/lib64/libgtksourceview-4.so.0`).
Assets summary: `evidence/gtk-runtime-assets.txt`, `evidence/gdk-pixbuf-loaders.txt`,
`evidence/gsettings-schemas.txt`, `evidence/gtk-immodules.txt`.

`AppRun` at launch: rewrites the two `@APPDIR@` config templates into a per-user temp dir, sets
`LD_LIBRARY_PATH`, `GDK_PIXBUF_MODULE_FILE`, `GSETTINGS_SCHEMA_DIR`, `GTK_DATA_PREFIX`,
`GTK_EXE_PREFIX`, `GTK_PATH`, `FONTCONFIG_FILE`, `XDG_DATA_DIRS`,
`VEDIT_GSV_DATADIR` (the app sets the GtkSourceView language/style search paths from it), and
executes the binary. It also disables the AT-SPI bridge **only** when `/etc/machine-id` is not
a 32-hex id (bare containers): libdbus aborts there, and the bridge is not bundled. A normal
CentOS 7 desktop keeps accessibility.

Sizes and checksums (`evidence/sizes.txt`, `evidence/sha256.txt`):

```
AppImage 18,430,456 B  sha256 efb64eb7098bfd6db44ca7faebeefcee1fbc54d161da5ce25b1cae1562b5d1de
tar.gz   19,169,299 B  sha256 0c8488c3fa770eb2839769efb0b7b1691daa16e8ad162d80bbdf0fff7419c84c
AppDir   55 MB (on disk)   spike binary 622 KB (stripped)
```

Reproducibility hygiene: Rust embeds panic-location paths, so the build uses
`--remap-path-prefix=/root/.cargo=/cargo --remap-path-prefix=/work=/vedit-build`; the audit
`evidence/host-path-audit.txt` reports `count=0` build-machine paths in the binary, no
`RPATH`/`RUNPATH`, and `13` `@APPDIR@` placeholders in the loader cache (rewritten at launch).

## 6. Clean-room acceptance (offline, no network, clean CentOS 7)

Test image: `docker/Dockerfile.test` = `centos:7` + `xorg-x11-server-Xvfb`, `xorg-x11-utils`,
`mesa-demos`, `glx-utils`, `mesa-libGL`, `fontconfig` **only**; the image build asserts
`! rpm -q gtk3 gtksourceview3 gtksourceview4`.

Run (host): `bash scripts/build-all.sh` — its last step is the clean-room container:

```
docker run --rm --network none -v "$PWD/out:/art:ro" -v "$PWD/evidence:/evid" \
  -v "$PWD/scripts:/scripts:ro" vedit-gui-spike-test:local \
  /bin/bash /scripts/clean-room-accept.sh
```

Observed (`evidence/clean-room.log`), exit code 0, `RESULT=PASS`:

| check | command | observed |
|---|---|---|
| no system GTK/gsv in test image | `rpm -q gtk3 gtksourceview3 gtksourceview4` | all absent → PASS |
| Xvfb | `xdpyinfo` on `:99` | PASS |
| tar.gz selftest | `.../vedit --file top.sv --selftest` | exit **0**, `SELFTEST PASS` |
| tar.gz screenshot | `.../vedit --file top.sv --screenshot …` | exit **0**, PNG 76,237 B |
| AppImage direct exec | `./VEdit-Spike…AppImage --selftest` | exit **127**: `No suitable fusermount binary found on the $PATH` / `fuse: device not found` (host has no `/dev/fuse`; see §7) |
| AppImage extract-and-run | `./…AppImage --appimage-extract-and-run --selftest` | exit **0**, `SELFTEST PASS` |
| AppImage env form | `APPIMAGE_EXTRACT_AND_RUN=1 ./…AppImage --selftest` | exit **0** |
| dependency resolution | `LD_LIBRARY_PATH=<extracted>/usr/lib ldd <extracted>/usr/bin/vedit-gui-spike` | `not found`: none; 67 deps resolved inside the payload; the 8 remaining are the glibc runtime (`libc`, `libm`, `libpthread`, `libdl`, `librt`, `libresolv`, `libgcc_s`, `ld-linux`) |
| window exists | `xwininfo -root -tree` | `"VEdit T5 spike" … 900x700+0+0` |
| cold start (wall) | 3× `--appimage-extract-and-run --screenshot` | 823 / 833 / 823 ms (`evidence/clean-room/cold-start-wall.txt`) |
| cold start (exec→window) | app-printed `WINDOW_MAPPED_MS` | 31 / 34 / 31 ms (`evidence/clean-room/window-mapped.txt`) |

The 8 host libs are exactly the glibc C runtime, which must come from the target OS; nothing
else is taken from the host.

## 7. Software rendering conclusion

`LIBGL_ALWAYS_SOFTWARE=1` is exported for every smoke run. The CentOS 7 `Xvfb` server provides
no GLX (`evidence/clean-room/glxinfo.txt`):

```
command: xdpyinfo | grep -i glx   -> GLX extension: absent
command: glxinfo -B               -> Error: couldn't find RGB GLX visual or fbconfig
```

Conclusion: **GTK3 + GtkSourceView render on this target entirely through cairo's X11 software
path; no GL/GLX context is created and llvmpipe is not needed.** The screenshots in §4 are the
proof that rasterisation is correct, and the timing numbers are measured with that path.
This also means AC-18's "llvmpipe cold start" cannot be measured with this Xvfb build — the
e2e lane (T27) should either record the same "no GLX required" conclusion or provide an X
server with GLX; it is not a blocker for the editor because no feature depends on GL.

## 8. GLIBC gate (all bundled ELFs)

`scripts/glibc-gate.sh` walks every ELF in the AppDir and takes
`readelf --version-info | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1`
(`evidence/glibc-gate.txt`):

```
ELFS_SCANNED=81
MAX_REQUIRED_GLIBC=2.17
RESULT=PASS
```

Individual numbers: `libgtksourceview-4.so.0` → **GLIBC_2.14**, spike binary → **GLIBC_2.16**
(`evidence/gsv4-lib.txt`, `evidence/bin-glibc-floor.txt`). `readelf -d` on the binary shows no
`RPATH`/`RUNPATH`; the AppImage runtime is the static-pie ELF emitted by appimagetool.

appimagetool used: the recon AppImage, `sha256 793fca0ce4bcc309dc65d3f66669f58e3755643e4dadbe03932e7b436128494f`,
`continuous build (git b082d2e), build 333 built on 2026-10-29`; invoked as
`--appimage-extract-and-run --no-appstream` inside `centos:7` (`evidence/appimagetool.log`).
T11 should pin a released appimagetool version instead of the continuous build and keep the
sha256 in the repo.

## 9. A′ fallback assessment (answered because the contract requires the decision)

A′ = CentOS 7's GtkSourceView 3.24.8 + upstream gsv4 language definitions. It was not chosen
(A2 passed), and it is strictly more expensive. The two questions the captain asked:

**9.1 Can gsv3.24.8 consume a gsv4 `.lang` file?** Yes, measured:
`evidence/a-prime/probe.c` + `evidence/a-prime/probe-result.txt`

```
gsv3=3.24.8
command: gcc -std=gnu99 probe.c $(pkg-config --cflags --libs gtksourceview-3.0) -o probe
COMPILE_EXIT=0
command: NO_AT_BRIDGE=1 xvfb-run -a ./probe /specs   (specs = gsv 4.8.4 data/language-specs)
LANG id=systemverilog name=SystemVerilog section=Source
STYLE_IDS=4
COMPAT_OK
RUN_EXIT=0
```

Also, CentOS 7's gsv3.24.8 **already ships** `systemverilog.lang` and `verilog.lang`
(132 `.lang` total, spec `_version="2.0"`), and its own SV spec contains the
`interface` / `always_ff` / `constraint` / `covergroup` / `class` keywords. So an A′-2
(gsv3 with its own definitions) needs no `.lang` porting at all.

**9.2 The binding gap.** There is no `sourceview3` crate, so gsv3 has to be reached through
hand-written FFI. Minimal surface for the editor (not just the spike):
`GtkSourceLanguageManager`, `GtkSourceLanguage`, `GtkSourceBuffer`, `GtkSourceView`,
`GtkSourceStyleScheme(Manager)`, `GtkSourceCompletion*` (FIM/word completion),
`GtkSourceGutter*` + `GtkSourceMark*` (diagnostics gutter), `GtkSourceSearchContext`
(search), `GtkSourceFile(Loader/Saver)`. That is roughly 10–12 types and 40–60 functions,
≈400–800 lines of `unsafe` bindings plus a `build.rs` pkg-config check, plus a compatibility
risk surface (gsv3 is EOL; LGPL notices still apply). Estimated effort M (~2–4 days) versus
A, which is already green. A′ is therefore recorded as feasible-but-worse and remains unused
unless A later breaks.

## 10. Deviations, limitations, and hand-offs

1. **GtkSourceView 4.6.1 instead of 4.8** — required by the target's GTK 3.22.30 (§3).
   Approved by the captain; A2's definition was restated as "gsv4 (actual 4.6.1) builds,
   links through gtk-rs 0.18, runs under Xvfb".
2. **Direct AppImage execution not provable here**: the build host has no `/dev/fuse`, so the
   direct-run path fails with the AppImage runtime's FUSE message. The two documented CentOS 7
   paths were proven instead: `--appimage-extract-and-run`, `APPIMAGE_EXTRACT_AND_RUN=1`, and
   the tar.gz fallback. A real CentOS 7 desktop with FUSE can run the AppImage directly; the
   evidence for that is not claimed.
3. **Icons are a deliberate subset** (Adwaita 16–48 px + hicolor; no cursors, no 256/512 px,
   no SVG scalable set). The spike draws no toolbars. T11 should widen the set for the real
   editor and re-measure size.
4. **No input-method modules were bundled** (`/usr/lib64/gtk-3.0/3.0.0/immodules` is empty in
   the base `gtk3` package; `gtk3-immodule-xim` / ibus immodules are separate packages).
   AC-16 (Chinese input) needs them; T11 must add `gtk3-immodule-xim` (and an ibus module if
   the target uses ibus) to the payload.
5. **No external theme files**: CentOS 7's GTK 3.22 compiles Adwaita into `libgtk-3`
   (`/usr/share/themes` is empty), so the AppDir carries no theme directory; GTK still renders
   with Adwaita. GtkSourceView style schemes (`kate`, `cobalt`, `classic`, …) are bundled
   under `usr/share/gtksourceview-4/styles`.
6. **AT-SPI bridge disabled only in degenerate environments** (`/etc/machine-id` invalid),
   otherwise left to the host session bus (§5).
7. `appimagetool` is the continuous build the recon provided; T11/T12 must pin a release and
   record its sha256, as they must for verible (`verible-verilog-ls/-lint/-format`) and slang.
8. Slang is **not** part of this lane; the AppImage payload here contains GTK3 + GtkSourceView
   + the spike only. Bundling slang/verible (with per-binary sha256) belongs to T23.

## 11. Reproduce

```
# one command from build/gui-spike (builds the image, the app, both products, then the
# offline clean-room run; requires docker, network only while building)
bash scripts/build-all.sh

# individually
DOCKER_BUILDKIT=0 docker build -f docker/Dockerfile.build -t vedit-gui-spike-build:local \
  --build-arg GSV_SHA256=63f487a7ab59612ffa9e7be1883dbd274ab1016afcede3239b93b0fc384df874 docker/
docker run --rm -v "$PWD:/work" -w /work vedit-gui-spike-build:local bash -lc 'bash scripts/build-app.sh'
docker run --rm -v "$PWD:/work" -w /work -v "$PWD/../../.mpd/recon:/recon:ro" \
  vedit-gui-spike-build:local bash -lc 'bash scripts/package.sh'
docker run --rm --network none -v "$PWD/out:/art:ro" -v "$PWD/evidence:/evid" \
  -v "$PWD/scripts:/scripts:ro" vedit-gui-spike-test:local \
  /bin/bash /scripts/clean-room-accept.sh
```

## 12. Evidence index

| file | what it proves |
|---|---|
| `evidence/env-facts.txt`, `evidence/rustc-version.txt`, `evidence/pkg-versions.txt` | exact toolchain/GTK/gsv versions inside CentOS 7 |
| `evidence/gsv4-4.8-meson-failure.txt` | raw meson rejection of 4.8 on GTK 3.22.30 (exit 1) |
| `evidence/gsv4-gtk-requirement-matrix.txt` | per-release glib/gtk requirements |
| `evidence/language-spec-equality.txt` | verilog/systemverilog `.lang` byte-identical 4.6.1 vs 4.8.4 |
| `evidence/gsv4-lib.txt`, `evidence/bin-glibc-floor.txt` | GLIBC floors (2.14 / 2.16) |
| `evidence/cargo-build.log`, `cargo-clippy.log`, `cargo-fmt.log`, `cargo-lock-versions.txt` | gtk-rs 0.18 stack and clean gates |
| `evidence/bin-ldd.txt` | binary links `/opt/gsv4/lib64/libgtksourceview-4.so.0` |
| `evidence/bundled-libs.txt` | the 67-library GTK3 closure (build origin per library) |
| `evidence/gtk-runtime-assets.txt`, `gdk-pixbuf-loaders.txt`, `gsettings-schemas.txt`, `gtk-immodules.txt` | bundled runtime assets (and the IME gap) |
| `evidence/glibc-gate.txt` | 81 ELFs, max GLIBC 2.17, PASS |
| `evidence/host-path-audit.txt` | 0 build paths, no RPATH/RUNPATH, 13 `@APPDIR@` placeholders |
| `evidence/sizes.txt`, `sha256.txt` | both product sizes and checksums |
| `evidence/appimagetool.log`, `appimagetool-sha256.txt` | AppImage tool and its version |
| `evidence/clean-room.log`, `clean-room/*` | negative assertion, both deliverables, dependency resolution, cold start, window |
| `evidence/screenshots/*.png` | rendered SystemVerilog highlighting (clean-room + build image) |
| `evidence/a-prime/probe.c`, `probe-result.txt` | gsv3.24.8 loads gsv4.8.4 systemverilog.lang |

## 13. Contract success criteria — self check

1. `build/gui-spike` app opens a GtkWindow — **PASS** (§6: window 900×700 from `xwininfo`,
   `WINDOW_MAPPED_MS` printed by the app).
2. Branch A: `.sv` shows `systemverilog.lang` highlighting — **PASS** (§4: per-construct
   colours, 6 distinct styles, screenshots).
3. AppImage and tar.gz start in a clean, offline `centos:7` container under Xvfb with
   screenshots/logs — **PASS** for tar.gz and for AppImage via `--appimage-extract-and-run` /
   `APPIMAGE_EXTRACT_AND_RUN=1`; direct FUSE mount is not testable on this host (no
   `/dev/fuse`) and is declared as such (§7.2 above).
4. REPORT records branch, build commands, actual rustc, GTK3 bundled-library list, both
   product sizes, cold-start time, software-rendering conclusion — **this document**, with
   evidence paths.
5. A2 as restated (gsv4 builds a `.so` and is linked by gtk-rs 0.18 / `sourceview4 0.5`) —
   **PASS** (§2).
