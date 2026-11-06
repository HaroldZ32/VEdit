# RM-7b — disabled-look selftest: no hangs, real Breeze coverage, state-aware dot

Follow-ups from the independent RM-7 review
(`evidence/ux-batch-2/review-rm7-final/REPORT.md`, verdict PASS / risk MEDIUM):

* **M1 (Medium, real)** — the disabled-look selftest hung on a host that offers
  Breeze/Oxygen (60 s and 240 s runs exited **124** with zero `PIXELS` lines),
  and the user’s own style (KDE/Breeze) was unmeasured.
* **M2 (Medium, latent)** — `FileStatusDotDelegate::paint` ignored
  `State_Enabled` (unreachable today, but RM-9 is adding states to that file).
* **N1 (Nit)** — the selftest header claimed a “disabled ink closer to background”
  assertion that the code did not make.

**Status: all three fixed.** No product behaviour changed except the disabled
file-tree dot (M2, latent-only). The selftest is now bounded and exits 0 in the
pinned image, and every style the environment offers is either measured or
reported as an explicit skip.

---

## M1 — the hang, reproduced and bounded

### What the old test did

`testPixels()` iterated `QStyleFactory::keys()` **in-process** and called
`QStyleFactory::create(styleName)` **once per theme per sample** — 30 themes ×
5 widget classes = **150 style objects per style**. Fusion/Windows construct and
paint in milliseconds; a KDE style plugin (Breeze/Oxygen) is heavy enough that
the loop never reached its first `PIXELS` line. The reviewer reproduced the real
Breeze/Oxygen case on a host (`review-rm7-final/03-host-style-timeout.log`:
`CHECK disabled.pixels.styles PASS 4 style(s)`, then 60 s / 240 s, **exit 124**,
**0** `PIXELS` lines). The old test source is saved byte-identically (sha256
`1309c7c2…`, the hash the reviewer reviewed) in
`prefix/disabled_look_selftest_prefix.cpp`.

Breeze/Oxygen are **not installed** in the pinned image (`QStyleFactory::keys()`
→ Windows/Fusion; no `styles` plugin dir, no `qt6-style-breeze` package,
`apt` has no package lists). To reproduce the same signature in-image, the
evidence ships a **real Qt style plugin that stalls** (`probe/slowstyle.cpp`:
every `drawControl`/`drawPrimitive` sleeps 200 ms; metadata key `SlowStyle`):

```
$ bash evidence/ux-batch-2/rm7b-disabled-followups/probe/reproduce-old-hang.sh old 30
STYLE-SAMPLE … (old binary prints the palette block)
CHECK disabled.pixels.styles PASS 3 style(s): SlowStyle, Windows, Fusion
=== OLD exit=124 ===
=== OLD TIMED OUT after 30s (exit 124) ===     0 PIXELS lines   [logs/01-old-hang.log]
```

That is the OLD binary against the OLD test source: exactly the reported shape.

### The fix

* **Each style is rendered in a CHILD PROCESS** of the test binary
  (`--render-style <name>`) with a hard **15 s** bound
  (`kStyleTimeoutMs`). On expiry the child is `kill()`ed; the parent prints
  `SKIP disabled.pixels.style.<name> no render within 15000 ms -> style skipped,
  NOT failed` and continues. The test never blocks on a style plugin again.
* **One style object per style, reused** for every sample. The child also makes
  it the *application* style (`QApplication::setStyle`), so a stalling style in
  the parent cannot leak construction/paint cost into a child that measures a
  different style.
* A style that exists but produces **no pixmap** prints
  `STYLE-UNRENDERABLE … reason=no-pixmap` and is an explicit SKIP too; a style
  that cannot be created is `reason=create-failed`.
* **Breeze coverage**: the application's own style gets the full sample
  (5 widget classes × 30 themes); every other offered style gets a reduced but
  still real sample (toolbar QAction + combo box × 30 themes) so a heavy KDE
  style is measured inside the bound. Where the style is not offered by the
  image, the output says so explicitly:
  `SKIP disabled.pixels.style.Breeze not offered by this image; the bounded
  child measures it where Qt exposes it (user-style unmeasured here)` (same for
  Oxygen). In the pinned image `PIXEL-STYLES … measured=[Windows, Fusion]
  skipped=[]`.
* **The dot probe is bounded the same way** (`--probe-dot`, 10 s) because the
  delegate paints through `QApplication::style()`: if the application style
  itself stalls, the dot check is skipped, never hung. The headless Xvfb+xcb
  real-window path was not needed and is not produced: Breeze is absent from the
  image, so a window grab here would show Fusion. The bounded child *is* the
  real-style path on any host that ships Breeze.

Measured (logs quoted):

```
# pinned image, ctest, no stalling plugin
$ ctest -R vedit-app-disabled-look-selftest -V
16: PIXEL-STYLES app_style_class=QFusionStyle app_style_measured=1 measured=[Windows, Fusion] skipped=[]
16: SKIP disabled.pixels.style.Breeze not offered by this image; …
16: SKIP disabled.pixels.style.Oxygen not offered by this image; …
16: DISABLED-LOOK-SELFTEST failures=0
1/1 Test #16: vedit-app-disabled-look-selftest ...   Passed    0.29 sec   exit 0
                                                        [logs/05-green-ctest.log]

# stalling style offered as a third plugin (the SlowStyle probe)
$ reproduce-old-hang.sh new 120
SKIP disabled.pixels.style.SlowStyle no render within 15000 ms -> style skipped, NOT failed
PIXEL-STYLES … measured=[Windows, Fusion] skipped=[SlowStyle:timeout]
CHECK disabled.pixels.measured-at-least-one-style PASS measured=2 skipped=1
DISABLED-LOOK-SELFTEST failures=0
=== NEW exit=0 ===                                   [logs/03-new-bounded-with-stalling-style.log]

# WORST CASE: the stalling style IS the application style (the KDE shape)
$ QT_STYLE_OVERRIDE=SlowStyle reproduce-old-hang.sh new-app-style 120
SKIP disabled.pixels.style.SlowStyle no render within 15000 ms -> style skipped, NOT failed
PIXEL-STYLES app_style_class=QProxyStyle app_style_measured=0 measured=[Windows, Fusion] skipped=[SlowStyle:timeout]
SKIP disabled.dot no dot render within 10000 ms -> dot check skipped (style too slow)
DISABLED-LOOK-SELFTEST failures=0
=== NEW-APP-STYLE exit=0 ===                        [logs/06-new-bounded-app-style-stall.log]
```

**Regression (a) is proven:** the pinned-image run exits 0 in 0.29 s; a host
style that stalls costs at most 15 s and becomes an explicit SKIP; the test
**never** reaches 124.

---

## M2 — the file-status dot honours `State_Enabled`

`src/workspace/filetree/file_status_delegate.cpp` painted
`lastDotColor_ = dotColor(status)` unconditionally; the icon and the text
already used `State_Enabled`, so a disabled row would have kept a full-strength
dot. Fix: when `State_Enabled` is clear the brush/pen colour is
`blendToward(dotColor(status), row surface, 55%)` — the same rule the theme
palette uses for its Disabled group (55 % toward what the dot sits on; the row
surface is `Base`, or `Highlight` when selected). Hue is preserved, so the RM-9
status vocabulary stays readable. No RM-9 state was changed: filled dots stay
filled, the NotAnalyzed state stays a hollow ring and its *ring colour* dims,
Unknown still draws nothing.

Numeric proof is in the same selftest (`disabled.dot.*`, isolated in a child):

```
# BEFORE (pre-M2 delegate, prefix/…file_status_delegate_prefix.cpp, 120 samples)
PIXEL-DOT themes=30 samples=120 channel_delta=0 (Atom One Dark) luminance_delta=0.000 … drop=1.00
CHECK disabled.dot.error FAIL theme=Atom One Dark enabled=#d32f2f disabled=#d32f2f delta=0 …
… 120 × FAIL (every theme × status)          [logs/04-dot-red-prefix.log, exit 1]

# AFTER
PIXEL-DOT themes=30 samples=120 channel_delta=34 (Tokyo Night Light)
          luminance_delta=0.080 (Catppuccin Frappé) contrast_drop=1.16 (Tokyo Night Light)
CHECK disabled.dot.disabled-differs-from-enabled PASS samples=120 channel_delta=34 …
CHECK disabled.dot.unknown-draws-nothing PASS
                                                        [logs/05-green-ctest.log]
```

Floors: channel ≥ 30/255, relative-luminance delta ≥ 0.06, contrast drop ≥ 1.10
for the *dot* (measured worst 34 / 0.080 / 1.16). They are lower than the text
palette floors (40 / 0.10 / 1.40) because a 9 px saturated brush on a matching
surface has less dynamic range than text through white — the floors sit at
~75–85 % of the measured worst case so a regression to “identical” fails
immediately (the red run is delta 0). The dot probe also proves the painted
centre pixel is the brush colour for filled states and stays background-hollow
for the NotAnalyzed ring, in both enabled and disabled states.

RM-9 regression check: `vedit-rm9-status-dots-e2e` (built from the current
diagnostics lane) passes — `RM9-E2E checks=33 failures=0`, exit 0
(`logs/07-rm9-status-dots-e2e.log`).

---

## N1 — header now matches the assertions

The stale sentence (“the disabled ink must be measurably closer to the
background than the enabled ink”) is gone (`grep -c "measurably closer"` → 0).
The header now states exactly what `testPixels()` asserts: for every theme and
every offered style the enabled/disabled renders must **differ**, the enabled
render must contain the palette's **Active** text colour and the disabled render
the palette's **Disabled** text colour (exact-colour hits on glyph-core pixels),
plus the palette delta floors; it also documents the bounded child-process
isolation, the explicit-skips rule, and the M2 dot check
(`src/app/disabled_look_selftest.cpp:12-36`).

---

## Bounds

1. **Real Breeze/Oxygen could not be rendered here** — the pinned image has no
   KDE style plugin and no package source. The real-host hang is the reviewer's
   `review-rm7-final/03-host-style-timeout.log` (exit 124); this evidence
   reproduces the same signature with a real stalling `QStyle` plugin and shows
   the bounded path. On a developer/KDE host the bounded child measures Breeze
   (full sample, 15 s bound) or reports it skipped; that run could not be
   performed in this image.
2. **Skipping is visible, not silent.** A total style skip still prints
   `SKIP disabled.pixels.all-styles …` and the palette assertions (which are
   style-independent) still ran; the pixel layer is supplementary, so a style
   the environment cannot render does not fail the gate. The `measured=[…]`
   line makes what was actually covered unambiguous.
3. **Timeout uses SIGKILL on the child**; a plugin that corrupts its own process
   cannot affect the parent. A style that is slow but eventually renders within
   the bound is measured normally.
4. The dot probe’s child renders through the *application* style; if that style
   stalls, the dot check is an explicit SKIP (10 s). The RM-9 states are proven
   by the RM-9 e2e test above, not re-derived here.
5. No real-window Xvfb+xcb screenshot was produced: with Breeze absent it would
   show the same Fusion pixels already measured; the existing
   `vedit-app-ux-screenshots` harness is unchanged.

---

## Files

| path | role |
|---|---|
| `src/app/disabled_look_selftest.cpp` | bounded child per style, explicit skips, dot check, N1 header |
| `src/workspace/filetree/file_status_delegate.cpp` | M2: disabled-row dot dims (55 % toward the row surface) |
| `src/app/CMakeLists.txt` | links the filetree lane into the selftest (end of file, after the lane target exists) |
| `probe/slowstyle.cpp` + `slowstyle.json` | real Qt style plugin that stalls (evidence only) |
| `probe/reproduce-old-hang.sh` | builds the plugin, the old test binary and runs old/new/app-style scenarios |
| `probe/run-prefix-dot-red.sh` | M2 red: pre-fix delegate object swapped into a scratch archive |
| `prefix/disabled_look_selftest_prefix.cpp` | old test source (sha256 `1309c7c2…`, the reviewed one) |
| `prefix/file_status_delegate_prefix.cpp` | pre-M2 delegate source |
| `prefix/m2-unguard.diff` | exact inverse patch of the M2 change |
| `logs/01-old-hang.log` | OLD binary + stalling style: exit 124, 0 `PIXELS` |
| `logs/03-new-bounded-with-stalling-style.log` | NEW + stalling style: exit 0, explicit SKIP |
| `logs/06-new-bounded-app-style-stall.log` | NEW + stalling *application* style: exit 0, skips |
| `logs/04-dot-red-prefix.log` | M2 red: 120/120 dots identical, exit 1 |
| `logs/05-green-ctest.log` | pinned-image ctest: failures=0, 0.29 s, exit 0 |
| `logs/02-green-in-image.log` | same binary run directly (full output) |
| `logs/07-rm9-status-dots-e2e.log` | RM-9 e2e after M2: 33 checks, 0 failures |
| `logs/00-source-sha256.txt` | hashes of the sources exercised |
