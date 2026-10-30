# AC-16 — Chinese input proven automatically in a container

Goal: turn "Chinese can be typed" from a MANUAL item into something a script proves, using **the
GTK and the immodules from the packaged payload** (`packaging/tarball/out/vedit-0.1.0-linux-x86_64.tar.gz`),
not a second GTK installed in the test image.

Everything runs offline (`docker run --network none`) in `vedit-ime:local`
(`docker/Dockerfile.ime`): CentOS 7.9.2009 + Xvfb + `dbus-x11` + `ibus` 1.5.17 +
`ibus-libpinyin` 1.6.91 + `xdotool` (EPEL/aliyun) + ImageMagick + `wqy-zenhei-fonts`.
The image **has no gtk3/GtkSourceView rpm** (negative assertion in the Dockerfile); the GTK that
runs is the payload's, and `ldd` of the probe shows `libgtk-3.so.0` resolving into
`/tmp/payload/VEdit/lib` (`logs/11-payload-gtk.txt`).

## Result

| Tier | What was done | Assertion | Result | Evidence |
|---|---|---|---|---|
| — | payload GTK is the one used | `ldd out/bin/im-probe` → `.../VEdit/lib/libgtk-3.so.0` | **PASS** | `logs/11-payload-gtk.txt` |
| **T1** | ask GTK which `GtkIMContext` it creates, `GTK_IM_MODULE=xim` and `=ibus`, payload `GTK_IM_MODULE_FILE` | context id is `xim` / `ibus`, not the `simple` fallback | **PASS** (`xim='xim'`, `ibus='ibus'`) | `logs/30-tier1-xim.txt`, `logs/31-tier1-ibus.txt` |
| **T2** | real IME: `ibus-daemon -drx`, engine `libpinyin`, `GTK_IM_MODULE=ibus XMODIFIERS=@im=ibus`, `xdotool type "nihao"` + `space` into a `GtkTextView` | the buffer contains the committed Chinese `你好` | **PASS** (`BUFFER_TEXT=你好`, hex `e4bda0e5a5bd`) | `logs/40-ibus-env.txt`, `logs/42-tier2-typed.txt`, `shots/tier2-before-commit.png` (candidate window: `ni hao` → 1.你好 2.你 3.尼 4.呢 5.泥), `shots/tier2-after-commit.png` |
| **T3** | X11 Unicode injection: `xdotool type --clearmodifiers "中文注释：你好，世界"` with `GTK_IM_MODULE=simple` | the buffer contains that exact string | **PASS** (`BUFFER_TEXT=中文注释：你好，世界`) | `logs/50-tier3-typed.txt`, `shots/tier3-unicode.png` |

`logs/99-results.txt` is the machine-readable list (one `RESULT <name>=PASS|FAIL` per check) and
`logs/90-run.log` is the full run; the driver exits non-zero if any check fails.

## What each tier actually proves — and what stays manual

* **Module loading (T1): proven.** The payload's `im-xim.so` / `im-ibus.so` are loaded by the
  payload's GTK and really used: GTK reports the instantiated slave context as `xim` and `ibus`.
  A missing module would make it fall back to `simple`, so this is behavioural, not a file listing.
* **IME conversion (T2): proven, at engine level.** A real IME daemon with a real Chinese engine
  (`ibus` + `libpinyin`) turns the typed pinyin `nihao` into `你好` — the candidate window is
  visible in the screenshot and the committed text is in the buffer. This is the
  "拼音 → 中文" path the manual test used to stand for.
* **Unicode on-screen (T3): proven.** The payload's GTK accepts and renders an injected Unicode
  string end to end. **T3 alone would not prove IME conversion** — it proves the editor part
  (event → buffer → glyphs); T2 is what proves the conversion.

**Still manual:** how a *specific* desktop's IME behaves (ibus/fcitx version, engine defaults,
candidate-window look, hotkey switching, focus quirks with a window manager) on a real CentOS 7
desktop, and CJK input on the user's own machine. Those are environment properties, not product
properties; the product-side path (immodule loaded → IME conversion → text committed → rendered)
is now automated.

## Reproduce

```sh
bash evidence/ime/run-all.sh          # builds vedit-ime:local, compiles the probes, runs all tiers
# or, after the first build:
SKIP_IMAGE_BUILD=1 bash evidence/ime/run-all.sh
```

The run is offline (`--network none`); the image build itself needs network (yum).

## Files

| Path | What |
|---|---|
| `src/im-probe.c` | Tier 1: prints the `GtkIMContext` id GTK actually creates |
| `src/input-probe.c` | Tier 2/3: `GtkTextView` that dumps `BUFFER_TEXT` / `BUFFER_HEX` on exit |
| `build-probes.sh` | compiles both in the pinned image (GTK 3.22.30 dev files) into `out/bin` |
| `docker/Dockerfile.ime`, `docker/tuna.repo` | the offline acceptance image (no GTK) |
| `run.sh` | the in-container driver (payload env, Xvfb, ibus, xdotool, screenshots, assertions) |
| `run-all.sh` | host entry: image build, probe build, offline run |
| `logs/*` | every command's raw output, plus `99-results.txt` |
| `shots/*.png` | candidate window / committed text / Unicode injection |

Bounds: `xdotool` comes from EPEL (a *test driver*, never shipped); the IME daemon and engine are
CentOS 7 vault packages, so this does not test any other engine or a newer ibus. The probes are
compiled against the pinned image's GTK 3.22.30 headers and executed against the payload's
3.22.30 libraries, which `ldd` verifies.
