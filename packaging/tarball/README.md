# T25 — tar.gz directory package + self-extracting `.run`

Cell `cell-20261030T020634155Z`, board task **T25** (split key T12), frozen contract
`.mpd/cellwork/cell-20261030T020634155Z/contracts/T12.md`, owner Junior Engineer, verifier
Reviewer (board T26).

Two relocatable products, built from the same payload, that run on a clean, offline CentOS 7
(glibc 2.17) without installing anything:

| product | bytes | sha256 |
|---|---|---|
| `out/vedit-0.1.0-linux-x86_64.tar.gz` | 40,493,353 | `ddf389733b3bc82d68c4f22d6bc2a09ab87b76aaf2eaa3bfbd10655ded9e6e81` |
| `out/VEdit-0.1.0-x86_64.run` | 40,496,151 | `0ef44b3576905c2ae09547eeaa74a9d5d7f2426e92bce7a999633938fee788ea` |

The `.run` is the tarball plus a POSIX-sh stub: the payload is stored **once**, so the
overhead is 2,798 bytes.

`bin/vd-app` is **the canonical release artifact** (`build/artifacts/vd-app`, built from the
frozen commit with `RELEASE_RUSTFLAGS` and stripped): this lane copies it verbatim so the
tar.gz/.run payload and the AppImage carry the same bytes. A local build is only a fallback
(and is stripped with the canonical pipeline's `strip --strip-all`); the identity, size and
stripped state are recorded in `evidence/01b-vd-app-identity.txt`. Rejected intermediate
binaries and why they were rejected: `evidence/18-abandoned-intermediates.txt`. (These numbers describe the 0.1.0 build in `evidence/16-products.txt`;
the next rebuild changes them, and the evidence file is the authority.)

## Layout (identical inside both products)

```
VEdit/
  vedit.sh                     launcher: every path derived from its own directory
  bin/vedit                    shim -> ../vedit.sh (the exact path the .run execs)
  bin/vd-app                   the editor
  bin/verible-verilog-{ls,lint,format}   Apache-2.0, from the pinned release URL
  bin/slang                    MIT, built by T3 for glibc 2.17, sha256 pinned
  lib/*.so                     GTK3/Pango/cairo/… closure (95 ELFs in the payload)
  lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so
  lib/gtk-3.0/3.0.0/immodules/{im-xim.so,im-ibus.so}
  share/gtksourceview-4/{language-specs,styles}   163 upstream .lang + schemes
  share/glib-2.0/schemas/*.gschema.xml + gschemas.compiled
  share/fonts/dejavu/*.ttf  share/fonts/wqy-zenhei/wqy-zenhei.ttc
  share/icons, share/themes
  share/vedit/testdata/definition/{a.sv,b.sv}      selftest fixtures
  share/doc/vedit/THIRD_PARTY_NOTICES.txt
  etc/{loaders.cache,immodules.cache,fonts.conf}.in   @APPDIR@ placeholders
```

The launcher resolves its runtime cache directory `XDG_RUNTIME_DIR` first and falls back to
`$TMPDIR` with a one-line explanation when that directory cannot be created (an unusable
`XDG_RUNTIME_DIR`, `/proc/...`, a read-only mount); only when both fail does it exit 1 listing
both attempted paths (`evidence/45-runtime-fallback-*.log`).

The launcher sets, relative to `$HERE`: `LD_LIBRARY_PATH`, `GDK_PIXBUF_MODULE_FILE`,
`GTK_IM_MODULE_FILE`, `GSETTINGS_SCHEMA_DIR`, `GTK_DATA_PREFIX`/`GTK_EXE_PREFIX`/`GTK_PATH`,
`FONTCONFIG_FILE`/`FONTCONFIG_PATH`, `XDG_DATA_DIRS`, `VEDIT_GSV_DATADIR`,
`VEDIT_VERIBLE_DIR`, `VEDIT_SLANG`, `VEDIT_SELFTEST_FIXTURES`. `VEDIT_PRINT_ENV=1` makes it
print that environment and exit (used by the evidence).

## Self-extracting `.run`

POSIX `sh` + `tar` + `gzip` only. It extracts to `mktemp -d "${TMPDIR:-/tmp}/vedit.XXXXXX"`,
writes a tiny probe there and **executes it**; if that fails (`noexec`) it retries in
`${XDG_CACHE_HOME:-$HOME/.cache}/vedit/run.XXXXXX` and probes again; if neither can execute it
exits 3 with an explicit message naming the alternatives (tar.gz, AppImage extract-and-run,
`TMPDIR`). The extracted tree is removed on exit via `trap … EXIT INT TERM`;
`VEDIT_KEEP_TMP=1` keeps it and prints the path.

Deviation from the literal request, stated on purpose: the payload is **not** started with
`exec`. `exec` replaces the shell, which makes the `EXIT` trap unreachable and would leak the
extracted tree on every run. It runs the launcher in the foreground instead, so the trap fires
and the tree is removed; the exit status is propagated unchanged.

## AC mapping

* **AC-01/02 (runs on clean CentOS 7)** — `evidence/20-cleanroom-full.log`: in a container whose
  only content is CentOS 7.9.2009 + Xvfb, with `--network none`, with a negative assertion that
  no `gtk3`/`gtksourceview3`/`gtksourceview4` rpm exists, both products pass `--selftest all`
  (51 checks, exit 0), the window is visible to `xwininfo`, and Chinese text renders.
* **AC-18 (size / cold start, record item)** — tar.gz 40.9 MB, `.run` 40.9 MB; window visible
  after **109 / 64 / 63 ms** (three runs, `evidence/36-cold-start.txt`). Target was ≤ 300 MB and
  ≤ 10 s, so both metrics are far inside it.
* **GLIBC ≤ 2.17 for every bundled ELF** — `evidence/11-glibc-rpath.txt`:
  `ELF_SCANNED=95 ELF_WITH_RUNPATH=1 ALLOWLIST_HITS=1 UNKNOWN=0 GLIBC_GATE=PASS`.
* **No absolute host paths** — `evidence/13-host-path-audit.txt`: the three launch-time templates
  contain only `@APPDIR@` placeholders (no `/vedit-build`, `/home`, `/root`, `/opt/gsv4`), and
  the shipped binaries carry no RPATH/RUNPATH.
* **Relocatability** — `evidence/34-relocated.log`: the tarball unpacked at an unrelated path
  (`/opt/relocated-*`) passes `--selftest smoke`.

## Known absolute path (accepted, not neutralised)

`libdw.so.1` carries `RUNPATH=/usr/lib64/elfutils`. That RUNPATH is what CentOS 7's upstream
elfutils packages ship; VEdit does not inject it. It is neutralised in effect rather than removed:

* `LD_LIBRARY_PATH` (set by the launcher to the payload `lib/`) is searched **before** RUNPATH,
  and `libdw`/`libelf` are both bundled, so the path is never consulted;
* the clean-room container has no `/usr/lib64/elfutils`, and the products start and pass
  `--selftest all` there — the assertion that the missing path is harmless;
* `patchelf` would have to be added to the build image (an offline-unavailable EPEL dependency)
  to remove a path that is never used.

The gate enforces this with a signed allowlist (`rpath-allowlist.txt`): **any** RUNPATH entry not
covered there fails the build (`UNKNOWN` must be 0).

## Input-method modules (explicit deliverable)

| requirement | evidence |
|---|---|
| files bundled | `im-xim.so`, `im-ibus.so` in `lib/gtk-3.0/3.0.0/immodules/` (`evidence/12-immodules-and-binaries.txt`) |
| cache generated | `gtk-query-immodules-3.0-64` output with `@APPDIR@` placeholders; both `im-xim.so` and `im-ibus.so` entries present (`evidence/12`, asserted again in the clean room) |
| cache actually used | `GTK_IM_MODULE_FILE=/tmp/xdg/vedit-runtime-0/immodules.cache` in `evidence/32-launcher-env.txt` |
| module actually loads | `GTK_IM_MODULE=xim … --selftest smoke` exit 0 and no `Failed to load module` in `evidence/33-immodule-xim.log` |

Package names: `gtk3-immodule-xim` (`im-xim.so`) and `ibus-gtk3` (`im-ibus.so`) — there is no
`gtk3-immodule-ibus` package on CentOS 7.

## Chinese font (functional requirement for the target users)

A minimal CentOS 7 install has no CJK font; Chinese comments would render as tofu. The payload
bundles **WenQuanYi Zen Hei** (`wqy-zenhei-fonts-0.9.46-11.el7`, GPLv2 **with font-embedding
exception**, 17 MB TTC, sha256 in `evidence/18-cjk-font.txt`) and points fontconfig at
`@APPDIR@/share/fonts`, so nothing is taken from the target machine.

Proof in the clean room (`evidence/40`–`44`): the image has zero CJK fonts (`fc-list :lang=zh`
empty, negative assertion), while inside the launcher environment fontconfig lists the bundled
WenQuanYi for `:lang=zh`, and the X-server capture
`evidence/shots/targz-cjk-x11.png` shows the editor rendering
`// 中文注释…`, `module 中文演示`, `时钟`, `计数` as real glyphs with syntax highlighting.
Emoji fonts are deliberately **not** bundled (size/licence); this is a documented bound.

## Selftest fixtures (shared convention with T27)

The `lsp` suite opens two Verilog fixtures. The app resolves them through
`selftest::fixture_root()`: `VEDIT_SELFTEST_FIXTURES` first, compiled-in source path as fallback
(fixed in `1265520` after this lane found the leak). The payload ships
`share/vedit/testdata/definition/{a.sv,b.sv}` and the launcher exports
`VEDIT_SELFTEST_FIXTURES=$HERE/share/vedit/testdata`; the app prints
`vd-app selftest: fixture_root=…` at startup (`evidence/41-cjk-screenshot.log`).

## Evidence index (`evidence/`)

| file | proves |
|---|---|
| `00-stage.log`, `01-cargo-build.log` | the payload build and the app build inside the pinned image |
| `02-verible-sha256.txt` | verible tarball + the three binaries verified against `scripts/versions.env` |
| `03-slang-sha256.txt` | slang artifact matches `SLANG_BIN_SHA256` |
| `10-payload-manifest.txt` | every file in the payload with its sha256 |
| `11-glibc-rpath.txt` | per-ELF GLIBC ceiling, RUNPATH scan, the four totals |
| `12-immodules-and-binaries.txt` | binary identities, immodule files and cache |
| `13-host-path-audit.txt` | templates, placeholders, RPATH/RUNPATH, embedded-string count |
| `14-run-selfcheck.txt` | `.run` stub/tar byte counts and payload round-trip |
| `15-launcher.sh.txt` | the shipped launcher, verbatim |
| `16-products.txt`, `37-sizes.txt` | product sizes and sha256 (build + clean room) |
| `17-selftest-fixtures.txt` | packaged fixture list |
| `18-cjk-font.txt` | CJK font package, licence and sha256 |
| `20-cleanroom-full.log` | the full clean-room run (`--selftest all` for both products) |
| `21-cleanroom-noexec.log`, `22-run-noexec.log` | `noexec /tmp` fallback and cleanup |
| `30-*`, `31-*`, `32-*`, `33-*` | per-suite selftests, launcher env, immodule runtime check |
| `34-relocated.log` | second-path relocation |
| `35-processes.txt` | no verible/slang process, no zombie |
| `36-cold-start.txt` | three window-visible timings |
| `38-known-limitations.txt` | the two recorded bounds below |
| `40`–`44`, `shots/targz-cjk-x11.png` | Chinese font match and the rendered Chinese screenshot |

## Reproduce

```sh
bash packaging/tarball/build.sh        # stage in the pinned image + clean-room acceptance
```
(`build.sh` builds `vedit-build:local` from the pinned inputs, runs `stage.sh` inside it, then
builds `vedit-tarball-cleanroom:local` from `docker/Dockerfile.test` — CentOS 7 + Xvfb +
ImageMagick, **no** GTK/verible/slang — and runs `clean-room.sh` twice, once with `--network none`
and once with `--network none --tmpfs /tmp:noexec`.)

## Bounds (recorded, not hidden)

1. **`--screenshot` used to be blank in a window-manager-less container.** The frozen2 `vd-app`
   fixes the capture ("layout-safe screenshots"): the clean-room run now writes non-blank
   `--screenshot` PNGs (34,550 / 33,880 bytes; the CJK shot at stddev ≈ 0.40), so this is no
   longer a bound. The X-server capture (`import -window root`) is kept as independent visual
   evidence of what a user actually sees.
2. **Build-tree strings in the app binary.** The canonical artifact is built with
   `RELEASE_RUSTFLAGS` (a shared `--remap-path-prefix` set from `scripts/versions.env`), so the
   panic locations read `/vedit/...`; the audit in `13-host-path-audit.txt` §4 reports the count
   and each distinct value instead of assuming 0, because `--remap-path-prefix` does not always
   cover compile-time literals such as an `env!()` default. Measured on frozen2: `0` and `508`.
3. **Determinism.** `tar` on CentOS 7 is 1.26 and has no `--sort=name`; entries are normalized by
   owner/group (0/0) and `gzip -n` drops the timestamp, but directory order follows the
   filesystem. Byte-identical rebuilds are therefore not claimed here.
4. **verible is fetched at build time** from the pinned URL and verified by sha256 (network during
   the build is allowed by AC-19); the payload itself never downloads anything.
5. **AppImage is T23's product**, not this lane's; the `.run`'s error message points at the
   AppImage extract-and-run path as one alternative when no directory can execute files.
