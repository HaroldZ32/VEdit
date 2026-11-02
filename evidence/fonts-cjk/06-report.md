# C9 (T119): collected_at=2026-11-01T11:04:46Z; git_head=60c7ae9c95fe769e2bd6c86dd4c56e1514603e44; git_status_porcelain_sha256=29b3dde0b45cdcacccc86852294fbc0129b0c730cb73dfe4dcaf1227cd8f2606; anchor=00-C9.txt
# T119 — packaged CJK font + Mono fallback + licence registration

C9 header: `00-C9.txt` (HEAD + full porcelain + font/licence sha256).

## Font

| item | value |
|---|---|
| file | `NotoSansCJK-Regular.ttc` (TTC: SC/TC/JP/KR/HK + Mono CJK) |
| vendored at | `packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc` |
| bytes | **19,484,784** |
| sha256 | **`b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a`** |
| source | Ubuntu `fonts-noto-cjk 1:20240730+repack1-1build1`; upstream https://github.com/notofonts/noto-cjk |
| licence | **OFL-1.1**, Copyright 2010-2012, Google Corporation |
| pin | `scripts/versions.env` `NOTO_CJK_FONT_SHA256` / `NOTO_CJK_FONT_SIZE`; assemble fails closed on mismatch |

Installed by `assemble-appdir.sh` as `usr/share/fonts/NotoSansCJK-Regular.ttc`, together with
`usr/share/doc/vedit/licenses/Noto-CJK-OFL-1.1.txt`. The existing
`etc/fonts/fonts.conf` `<dir>../../share/fonts</dir>` now finds it, so no host font
is needed (stock CentOS 7 has none).

Fontconfig aliases (UI proportional; editor monospace):
`DejaVu Sans → Noto Sans CJK SC`; `DejaVu Sans Mono → Noto Sans Mono CJK SC`.
The Latin face stays DejaVu; the CJK face is only a fallback.

Estimated delivery size: 95,349,240 + 19,484,784 + 5,563 = **114,839,587 B**
(actual AppImage compression is measured only by a real build; AppImage not rebuilt here).

## Evidence: inFont and fallback family

`01-font-probe.log`, stock centos:7, same Qt/fontconfig as the product:

```
# after (T119)
FONT default_family=DejaVu Sans pixel_size=14 families=15
FONT inFont_CJK=1 inFontUcs4_CJK=1 inFont_A=1
RUNS[UI-DejaVuSans] family=Noto Sans CJK SC glyphs=2 family=DejaVu Sans glyphs=1
RUNS[EDITOR-DejaVuSansMono] family=Noto Sans Mono CJK SC glyphs=2 family=DejaVu Sans Mono glyphs=1
FONT[EDITOR-DejaVuSansMono] ... inFont_CJK=1 inFontUcs4_CJK=1 adv_A=10 adv_CJK=16 ratio=1.60 ascii_adv=190
FONT[EXPLICIT-NotoSansMonoCJKSC] ... adv_A=8 adv_CJK=16 ratio=2.00 ascii_adv=152
```

- `inFont_CJK` / `inFontUcs4_CJK`: **0 → 1** (before: DejaVu only).
- The editor's CJK glyph runs come from **`Noto Sans Mono CJK SC`** (not the
  proportional face).
- **Latin is unchanged**: same pure-ASCII advances before/after — UI 159, editor
  190, default 120 (`01-font-probe.log` before/after).
- **Fontsize caveat**: with DejaVu Sans Mono as primary Latin (10 px) and Mono
  CJK fallback (16 px at the same size), the editor's measured CJK/Latin ratio is
  **1.60**, not 2.00. The **Mono CJK family itself is 2.00** (8 → 16). A 2.00
  ratio in the editor would require making the Latin primary font Noto Mono
  (changing pure-ASCII width), which conflicts with requirement B (Latin must
  not change). I kept B (ASCII width) and the Mono fallback; this trade-off is
  flagged for the Lead's decision, not hidden.

## Licence registration (OFL hard requirement)

- Root `THIRD_PARTY_NOTICES.md` §9.5 records font name/version, upstream, sha256,
  size, OFL-1.1, Google copyright, and both paths.
- In-package licence: `usr/share/doc/vedit/licenses/Noto-CJK-OFL-1.1.txt`;
  `selfcheck.sh` asserts the file exists and contains `SIL OPEN FONT LICENSE` and
  `Google Corporation` (`02-selfcheck.log`: both CHECK PASS, `SELFCHECK PASS`).
- Repo copy: `packaging/runtime/assets/licenses/Noto-CJK-OFL-1.1.txt`
  (5,563 B, sha256 `b130ca8c…`).

## AppDir counts

Old delivery AppDir **202** files → T119 test AppDir **216** files: +12 are the
T115 packaged `C.utf8` locale files, +2 are the font and the OFL text (T119); the
font/licence-only delta is **202 → 204** if the T115 locale layer is excluded.
Details: `05-appdir-counts.txt`.

## Startup noise cleanup

`vcs:` and `settings:` install lines are now behind the same `VEDIT_DEBUG` gate
(log only, logic untouched). Normal startup in stock centos:7 now prints only:

```
AppRun: locale 'C' (ANSI_X3.4-1968) -> 'C.UTF-8' (bundled UTF-8 fallback)
This plugin does not support propagateSizeHints()
```

The last line comes from the offscreen platform plugin used only in headless
tests; real xcb does not print it (documented, not changed). `03-startup-after.log`.

## Gates

- Clean worktree `target/wt-55a874d` (55a874d + T115/T119 changes):
  **ctest -j1 27/27 passed, CTEST-EXIT=0** (`03-gates.log`). The folding test is
  flaky only under parallel load; the serial full run is green.
- Clean-worktree `bash build/gates/typecheck.sh`: **PASS**.
- **Main worktree typecheck currently fails before reaching T119 code**: linker
  errors in `vedit-edit-core-selftest` / `vedit-fim-wiring-selftest`
  (`undefined reference to vedit::editor::yieldToEventLoop()` /
  `decideLargeFile(...)`) from another in-flight editor-lane change; the clean
  worktree typecheck passes. This is reported as an external blocker, not
  papered over.
- `selfcheck.sh --appdir target/t119-appdir`: **SELFCHECK PASS** (font + OFL
  assertions included).

## Not done / caveats

- No AppImage rebuild (per brief); the current delivery AppImage `e7ae067f…`
  does not contain this font.
- Full CJK **rendering** screenshot not produced (headless); the metric/glyph-run
  evidence above is the substitute. Once repackaged, verify in a real GUI.
- The 1.60 vs 2.00 editor ratio trade-off was accepted by the Lead (T119 ruling):
  ASCII width stability wins; the 2.00 back door and its cost are recorded above.

## Evidence index

| file | content |
|---|---|
| `00-C9.txt` | C9 header + font/licence sha |
| `01-font-probe.log` | before/after probe, glyph-run families, ASCII advances |
| `02-selfcheck.log` | selfcheck font/OFL assertions |
| `03-gates.log` | clean ctest 27/27 + typecheck PASS (+main failure note) |
| `04-source-licence.txt` | font/source/licence/sha/size + package paths |
| `05-appdir-counts.txt` | 202 → 216 breakdown |
| `06-report.md` | this report |
| `Noto-CJK-OFL-1.1.txt` | packaged OFL text copy |

## Accepted trade-off (Lead ruling, T119)

The editor keeps DejaVu Sans Mono for Latin (so ASCII metrics are unchanged) and
falls back to Noto Sans Mono CJK for CJK glyphs. Because the two fonts have
different em advances, a CJK glyph is **1.60×** a Latin advance rather than the
typographic 2.0×; mixed Chinese/Latin comment alignment is therefore approximate,
while CJK runs are internally monospaced. This is a **deliberate trade-off**, not
a bug: ASCII width stability was chosen over a 2.0 mixed ratio.

Back door, if strict 2.00 is ever required: set the editor's primary font family
to `Noto Sans Mono CJK SC` (one configuration line); the cost is Latin advance
10px → 8px (all pure-ASCII metrics change).

Recorded in `AGENT.md` (the project has no `AGENTS.md`) and
`packaging/runtime/README.md` (delivery notes).
