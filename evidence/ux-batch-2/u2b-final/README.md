# U2b — hover Fix button: English source string + catalogue entry

- **Task:** T15 (`U2b hover Fix button: English source string + catalogue entry`), defect **F1** in
  `evidence/ux-batch-2/u2-final/README.md` §4.
- **Requirements behind it:** user requirement #3 (no untranslated UI) and #9 (one-click fix).
- **Write scope touched:** `src/workspace/diagnostics/diagnostic_hover.cpp` (one line),
  `src/workspace/diagnostics/selftests/b3_wiring_e2e.cpp` (one added check + its include),
  `translations/vedit_zh_CN.ts` (ONE message), and this directory. Nothing else.
- **Result:** the button's source string is ASCII `Fix`, the `VEdit` catalogue entry is
  `<source>Fix</source><translation>修复</translation>`, `lrelease` exits 0 with **0 unfinished**,
  and the hover suite passes with the new label assertion.

## 1. The defect (before)

`src/workspace/diagnostics/diagnostic_hover.cpp:112` created the button with a **Chinese source
string**:

```cpp
fixButton_ = new QPushButton(QCoreApplication::translate("VEdit", "修复"), this);
```

and the catalogue carried it verbatim (`VEdit` context, `diagnostic_hover.cpp:112`):

```xml
<source>修复</source>
<translation>修复</translation>
```

Consequence: the English (untranslated) UI still reads 修复, and there is no English source string
to keep in step. Fixing the catalogue alone could not help — the source itself was Chinese.

## 2. The fix (two files, one semantic change)

1. `src/workspace/diagnostics/diagnostic_hover.cpp:112` — same context/class as its neighbours, so
   `lupdate` keeps extracting it:

```cpp
fixButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Fix"), this);
```

2. `translations/vedit_zh_CN.ts` — the `VEdit` message at the `diagnostic_hover.cpp:112` location;
   only the `<source>` text changed, the translation is kept:

```xml
    <message>
        <location filename="../src/workspace/diagnostics/diagnostic_hover.cpp" line="112"/>
        <source>Fix</source>
        <translation>修复</translation>
    </message>
```

No `lupdate` was run. No other message was touched, and the invariant counts prove it (the
"before" numbers are the U2 tail's own verification, `evidence/ux-batch-2/u2-final/README.md` §5,
taken immediately before this edit):

```
messages       1186 -> 1186
type="unfinished"  0 -> 0
type="vanished"    0 -> 0
lines          6195 -> 6195
```

(`translations/` is untracked in git — F2 of `u2-final` — so a `git diff` for it cannot exist; the
counts above plus the quoted block are the reviewable proof.)

## 3. Acceptance evidence

### (1) source string and catalogue entry — quoted above (§2). Raw checks:

```
$ sed -n '112p' src/workspace/diagnostics/diagnostic_hover.cpp
    fixButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Fix"), this);

$ sed -n '4229,4233p' translations/vedit_zh_CN.ts
    <message>
        <location filename="../src/workspace/diagnostics/diagnostic_hover.cpp" line="112"/>
        <source>Fix</source>
        <translation>修复</translation>
    </message>
```

### (2) `lrelease` — exit 0, 0 unfinished (`logs/01-lrelease-and-roundtrip.log`)

```
$ /usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/x.qm
Updating '/tmp/x.qm'...
    Generated 1186 translation(s) (1186 finished and 0 unfinished)
lrelease exit=0
```

Round-trip of the **compiled** catalogue proves the row survives as a translation, not as a
vanished/duplicate entry (`lconvert -i /tmp/x.qm -o /tmp/x.ts`, same log):

```
11:        <source>Fix</source>
12-        <translation>修复</translation>
```

`xmllint --noout translations/vedit_zh_CN.ts` → `xml ok`.

### (3) the hover test still passes — and now locks the English label

```
CHECK PASS hover.fixable-after-async-probe  fixable=1
CHECK PASS hover.fix-button-label-english  text='Fix'
SUMMARY checks=71 failures=0 skips=0
```

`ctest -R '^vedit-b3-wiring-e2e$'` → **1/1 Passed (6.28 s)**; `vedit-lint-experience-selftest` →
**1/1 Passed**, direct `checks=98 failures=0 skips=0` unchanged (`logs/04-…`, `logs/05-…`).

**Test change, stated explicitly:** no existing assertion referenced the Chinese source (a
`grep -rn '修复' src/**/selftests` finds no hit), so nothing had to be updated for a changed
expectation. I **added one check** — `hover.fix-button-label-english` (wiring suite 70 → 71
checks) — because without it the English label is an unverified claim; it would have failed on the
old `修复` source. `logs/02-hover-wiring-green.log` is the full run.

### (4) CJK-source inventory — see §4.

## 4. CJK-source inventory (`logs/06-cjk-source-inventory.log`)

Method: a Python scan of every `.cpp/.h/.hpp/.cc` under `src/` with comments stripped, that
(A) follows each `translate(…)` / `tr(…)` / `QT_TR_NOOP` / `QT_TRANSLATE_NOOP` call and inspects
its **FIRST argument**, and (B) lists every CJK string literal. Section (D) of the log is a scanner
self-check: it flags `QCoreApplication::translate("VEdit", "修复")` — i.e. the pre-fix line.

### A) i18n-call first argument containing CJK — **0** after the fix

Before the fix there was exactly **one**, and it is this task's F1:
`src/workspace/diagnostics/diagnostic_hover.cpp:112` (fixed, see §2).

### B) All CJK string literals under `src/` with comments stripped — **62**

- **Same user-visible defect class, but NOT `translate()`-wrapped (different fix, not done here):**
  the two diagnostics-lane status messages, i.e. raw Chinese literals whose English UI output is
  Chinese today:
  - `src/workspace/diagnostics/diagnostics_controller.cpp:156` — `"已取消 — %1/%2 文件"`
    (`lintCancelledMessage`, shown by the panel/status line)
  - `src/workspace/diagnostics/diagnostics_controller.cpp:560` —
    `"%1/%2 文件已扫描 — %3 条 CLI 诊断"` (finished-pass status line)

  They are in this task's write scope, but fixing them is a **different** change: they are not
  `translate()` sources, so their repair needs two NEW `VEdit` catalogue messages, and T15 says
  "do not touch any other message". Reported for a follow-up rather than smuggled in.
- **Product, other lanes (out of scope), same class:** FIM status/error strings —
  `src/editor/fim/fim_backend.cpp:51,71,79,91,127,130`;
  `src/editor/fim/fim_inline_completion.cpp:18,19,28,29,48,65,79,83,97,108,111,114,117,135,150,153,156,159,173,184,196,202`;
  `src/editor/kernel/fim_wiring.cpp:282`. They are un-translated CJK literals shown to the user.
- **Intentional:** `src/workspace/settings/settings_panel.cpp:540` — `"简体中文"`, a language
  endonym in a language selector (names are conventionally not translated).
- **Not UI:** `src/app/main.cpp:612` (a `--selfcheck` fixture text) and 27 test-expectation
  literals in `*_selftest.cpp` (vcs/search/ux/fim).
- Full file:line list: `logs/06-cjk-source-inventory.log`.

### C) Catalogue `<source>` entries containing Han — **0** after the fix.

## 5. Bounds — what is NOT proven

- **No full build was run** (captain's rule). `vedit-app` was not relinked, so the English label is
  asserted on the real popup widget in the offscreen wiring suite (windowless), not in an
  xcb/Xvfb window screenshot.
- The wiring suite installs **no QTranslator** (English environment), so `tr("Fix")` returns
  `"Fix"`; the **Chinese** rendering is proven by compiling the catalogue and reading the `Fix`
  row back with `lconvert` (§3.2), not by loading the `.qm` into the running test.
- `lrelease` used the host tool (`/usr/lib/qt6/bin/lrelease`, Qt 6.10.2 — the pinned host pin); the
  `build/gates/lrelease.sh` gate itself was **not** invoked (the captain runs the gates).
- The two `diagnostics_controller.cpp` raw-Chinese messages and the FIM strings are **reported,
  not fixed** (reasons above). The inventory is exhaustive for `src/` at the stated method; it does
  not cover generated/vendored trees or `resources/`.

## 6. Evidence index (`evidence/ux-batch-2/u2b-final/`)

| path | content |
|---|---|
| `logs/01-lrelease-and-roundtrip.log` | `lrelease` exit 0 / 0 unfinished, compiled-catalogue round-trip of `Fix → 修复`, XML check, invariant counts |
| `logs/02-hover-wiring-green.log` | full wiring run: `hover.fix-button-label-english text='Fix'`, `SUMMARY checks=71 failures=0` |
| `logs/03-lint-experience-green.log` | lint-experience direct run, `checks=98 failures=0 skips=0` |
| `logs/04-wiring-ctest.log`, `logs/05-lint-exp-ctest.log` | official `ctest` runs (1/1 Passed each) |
| `logs/06-cjk-source-inventory.log` | the CJK-source scan (A/B/C/D) behind §4 |
