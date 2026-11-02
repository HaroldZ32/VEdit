# T115 — locale/CJK startup + startup noise

C9 header

- collected_at_utc: see `00-C9.txt` (session snapshot) and the per-log backfill
  headers; HEAD `60c7ae9c95fe769e2bd6c86dd4c56e1514603e44`.
- git_status_porcelain_sha256: see `00-C9.txt`.
- All tests ran in stock `docker.m.daocloud.io/library/centos:7`, which has **no
  KF6/Qt/fonts installed**; the runtime is the packaged AppDir/AppImage.

## 1. Locale: root cause and fix

**Before** (`02-startup-before.log`, LANG=C): Qt prints
`Detected locale "C" … failed to switch to one` and the product starts.

The important measurement: setting `en_US.utf8` (a UTF-8 locale the CentOS 7 host
advertises) does **not** help, because the package ships **glibc 2.39** and the
host locale archive is **glibc 2.17** data: `setlocale(LC_ALL, "")` returns NULL
(`locale_setprobe`: `setlocale=(null)` for `en_US.utf8`; `C` works). The same
probe under `LOCPATH=<packaged>/usr/lib/locale` with the build image's tiny
`C.utf8` directory (416 KiB) prints `setlocale=C.UTF-8`.

Fix (packaged, not yet in the AppImage):
- `assemble-appdir.sh` ships `/usr/lib/locale/C.utf8` from the build image (the
  compatible locale data) and adds it to the delivery invariants.
- `AppRun` detects that the host charmap is not UTF-8 **or** that the host UTF-8
  locale is not usable by the packaged glibc, then passes `LANG/LC_ALL=C.UTF-8`
  and `LOCPATH=$APPDIR/usr/lib/locale` to the product. It announces the fallback
  in one line; if no packaged locale exists it keeps the original and announces
  that (never silent). The locale is passed through `/usr/bin/env` only to the
  product, so the host shell (glibc 2.17) never warns; `LD_LIBRARY_PATH` travels
  in the same env block so the host `env` does not load the package libc.
- Initial `AppRun` fallback to `en_US.utf8` was wrong and is recorded here: with
  the host archive it still produced the Qt warning.

**After** (`03-startup-after.log`, LANG=C):
```
AppRun: locale 'C' (ANSI_X3.4-1968) -> 'C.UTF-8' (bundled UTF-8 fallback)
vcs: installVcs projectRoot=''
settings: installSettings file='/tmp/.config/VEdit/vedit-app/settings.toml'
This plugin does not support propagateSizeHints()
```
The Qt locale warning is gone. `LANG=en_US.utf8` also falls back to C.UTF-8
(`08-locale-enUS.log`, no warning).

### CJK paths and content

`01-probe-locale.log` / `04-probe-after-locale.log` (same Qt/fontconfig as the
product, bundled loader):
- `QString::fromLocal8Bit("测试模块.sv")` and `fromUtf8` produce identical UTF-8;
- creating/opening a file named `测试模块.sv` works, `QDir` returns the same UTF-8
  name;
- product `--selfcheck` with `TMPDIR=/tmp/测试目录` under LANG=C:
  `APP-SHELL-SELFTEST ok=1 checks=200 failures=0`, exit 0
  (`05-selfcheck-cjk-tmpdir.log`).

**Remaining CJK defect (not locale): the package ships only DejaVu fonts.**
The probe reports `FONT default_family=DejaVu Sans … inFont_CJK=0
inFontUcs4_CJK=0`, and the AppDir fontconfig exposes only 5 DejaVu/Sans aliases.
Chinese text will render as missing-glyph boxes even with the locale fixed;
stock CentOS 7 has no fonts at all, and the AppDir fontconfig deliberately hides
host fonts. This needs a bundled CJK font (e.g. Noto Sans CJK, OFL) plus the
THIRD_PARTY_NOTICES entry and a repackage — **not done in T115** (out of the
locale-fallback task; flag for the Lead/user decision).

## 2. Startup noise

| line | before | after default | VEDIT_DEBUG=1 |
|---|---|---|---|
| KSH-ENV / KSH-GUARD | printed | **gone** | printed |
| `syntax-product:` | printed | **gone** | printed |
| `TOOLCHAIN …` | printed | **gone** | printed |
| `filetree/search` install lines | printed | **gone** | printed |
| `qt.multimedia.symbolsresolver` (pipewire) | printed | **gone** (QT_LOGGING_RULES) | gone |
| `kf.sonnet.core` | printed | **gone** (QT_LOGGING_RULES) | gone |
| `vcs:` / `settings:` install lines | printed | still printed | printed |
| offscreen `propagateSizeHints` | printed | still printed (headless only) | printed |

`VEDIT_DEBUG=1` restores the full set (`06-debug-startup.log`), including the KSH
safety assertion. `qt.qpa.*` is **not** muted.

Residuals: `vcs:`/`settings:` are in `src/workspace/{vcs,settings}/**`, which this
task must not touch; they need the same `VEDIT_QUIET` gate in a follow-up. The
propagateSizeHints line comes from the offscreen platform plugin used only in
headless tests, not from a real xcb session.

## 3. `filetree: … dockInserted=0 placeholderReplaced=0`

**Normal, not a missing panel.** `installFileTree()` runs from `installPanels()`
**before** `ApplicationShell::startSession()`; at that moment the `file-tree`
dock has not been created, so both counters are 0. `startSession()` then shows
the default panels (`app_shell.cpp:93-95` calls `showPanel("file-tree")`), and the
factory creates the panel. With no project the panel shows its explicit empty
state — `file_tree_widget.cpp` `RootState::NoRoot` →
`No folder open. Use "Open..." to browse a project.` With `--project <dir>` the
tree is populated. What the user should see: the **File Tree dock on the left**
(empty-state text when no project), via the default layout or View menu.

## 4. Repackage

**Yes.** T115 changes `packaging/runtime/AppRun` (always shipped), `assemble-appdir.sh`
(new locale data), `selfcheck.sh` and five `src/**` logging points. None of that is
inside the existing AppImage. The current delivery AppImage remains
`e7ae067f…` / built from `55a874d`; a new build is required after the Lead commits
these changes. (I did not rebuild it: T115's brief says do not package.)

## 5. Gates / changed files

- root ctest: **27/27 passed**, `CTEST=0`; typecheck: **PASS** (`07-gates.log`).
- `selfcheck.sh --appdir target/t115-appdir`: **SELFCHECK PASS** (new AppRun +
  packaged C.utf8 + quiet binary; `10-selfcheck-testappdir.log`).
- Changed: `packaging/runtime/AppRun`, `packaging/runtime/assemble-appdir.sh`,
  `packaging/runtime/selfcheck.sh`; `src/syntax/product/syntax_product.cpp`,
  `src/workspace/{filetree,search,palette,diagnostics}/*_integration.cpp`
  (log lines only); `evidence/locale-and-noise/**`. No `vcs`/`settings` files,
  no tests/gates changed, no AppImage rebuilt, no commit/push.

## Evidence index

| file | content |
|---|---|
| `00-C9.txt` | C9 session header (HEAD + full porcelain) |
| `01-probe-locale.log` | CJK probe under LANG=C vs en_US.utf8 (host archive not usable) |
| `02-startup-before.log` | delivered AppDir startup noise (before) |
| `03-startup-after.log` | new AppRun + locale + quiet binary (after) |
| `04-probe-after-locale.log` | C.utf8 + LOCPATH: paths pass, `inFont_CJK=0` |
| `05-selfcheck-cjk-tmpdir.log` | CJK TMPDIR selfcheck: 200/0, exit 0 |
| `06-debug-startup.log` | VEDIT_DEBUG=1 full diagnostics |
| `07-gates.log` | root ctest 27/27 + typecheck PASS |
| `08-locale-enUS.log` | UTF-8 host locale also falls back to C.UTF-8, no warning |
| `10-selfcheck-testappdir.log` | selfcheck.sh against the T115 test AppDir |
| `probe/` | locale/cjk probe sources + compiled probe/setlocale probe |
