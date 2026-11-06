# U2b — hover Fix button i18n repair — independent review (T16)

Reviewer: reviewer-1 (read-only lane; nothing under `src/**` or `translations/**` was
edited by this review). Repair reviewed: T15, `evidence/ux-batch-2/u2b-final/`.
Basis: `evidence/ux-batch-2/u2-final/README.md` §4 finding F1 (this reviewer's own
T10 finding) and `.mpd/plans/ux-batch-2/00-plan.md` §0 item 3.

Risk level: **Low** — a two-line semantic change (one source literal, one catalogue
`<source>`), independently reproduced; no product code is otherwise touched.

## 1. Result

**PASS.** The button's source string is English (`Fix`), the `VEdit` catalogue row is
`<source>Fix</source><translation>修复</translation>` with the location the code
actually compiles from, the compiled catalogue is green (0 unfinished) and round-trips
the `Fix → 修复` row, exactly one catalogue message changed, and the hover regression
was strengthened (one strict check added, none removed or weakened).

## 2. What I ran (independent re-derivation)

1. Source and catalogue (item 1):

```
$ grep -n 'fixButton_ = new QPushButton' src/workspace/diagnostics/diagnostic_hover.cpp
112:    fixButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Fix"), this);

$ grep -n -A2 -B2 '<source>Fix</source>' translations/vedit_zh_CN.ts
4229-    <message>
4230-        <location filename="../src/workspace/diagnostics/diagnostic_hover.cpp" line="112"/>
4231:        <source>Fix</source>
4232-        <translation>修复</translation>
4233-    </message>

$ grep -c '<source>修复</source>' translations/vedit_zh_CN.ts   -> 0
```

The code's context (`VEdit`) and the catalogue context agree; the `<location>` names
the same file/line the call is on.

2. Compiled catalogue (item 2), `00-lrelease.log`:

```
$ /usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/u2b.qm
Updating '/tmp/u2b.qm'...
    Generated 1186 translation(s) (1186 finished and 0 unfinished)
exit=0
$ /usr/lib/qt6/bin/lconvert -i /tmp/u2b.qm -o /tmp/u2b.ts     -> exit 0
    <message>
        <source>Fix</source>
        <translation>修复</translation>
```

`xmllint --noout` exit 0. Counts: messages 1186, unfinished 0, vanished 0, lines 6195
(all unchanged from the U2-tail close-out — the writer's invariant claim holds).

3. Only that one message changed (item 3), `10-ts-diff.log` / `10b-ts-diff-normalized.log`:

- Against the authoritative pre-U2 baseline
  `evidence/ux-batch-2/integration-translations/_parsed.json` (1207 = 1004 finished +
  182 unfinished + 21 vanished): **0** of the 1004 finished rows changed or vanished;
  the only key removed is `(VEdit, 修复)` and the only key added is `(VEdit, Fix)`; the
  translation of the changed row is still `修复`.
- Against my own T10 snapshot of the 182 U2-tail rows (`11-new182.log`, the file this
  reviewer hashed at the U2 close-out): **182/182** accounted for — 181 present
  verbatim, the repaired row present as `Fix → 修复` (the T10 parser's `\n` escaping
  was normalised; without normalisation 4 rows show as false mismatches).
- Message count 1186 = 1004 baseline-finished + 182 U2-tail rows, so no row was added
  or removed besides the one source substitution.

4. Test strength (item 4), `11-inventory-and-test.log` / `12-rerun.log`:

- Check-name set: U1's green run `20-green-after-fix.log` has 70 names; the current
  binary prints 71. Removed: **none**; added: **`hover.fix-button-label-english`** only.
  No existing assertion was renamed or weakened.
- The new check (`src/workspace/diagnostics/selftests/b3_wiring_e2e.cpp`,
  `hover.fix-button-label-english`) reads the live `veditHoverFix` `QPushButton` from
  the real popup and requires `text() == QStringLiteral("Fix")`; a missing button
  fails too. My run: `CHECK PASS hover.fix-button-label-english text='Fix'`.
- Convinced-it-can-fail: the assertion compares the rendered widget text to the exact
  English literal; reverting the source to `修复` (or to any other label, or removing
  the button) makes it fail — and the writer's comment records that the pre-fix source
  was Chinese, so this is the regression for exactly this defect.
- `grep -rn '修复' src` now matches only comments/README prose (no code or test
  expectation); no existing test had to be updated for a changed expectation.
- Runs: `ctest -R '^vedit-b3-wiring-e2e$'` → 1/1 Passed (6.90 s); a first **direct**
  run exited 141 (SIGPIPE) *after* all 71 checks had passed, and the single re-run was
  clean (`SUMMARY checks=71 failures=0 skips=0`, exit 0). Per the shared-build-tree
  notice this is the documented transient SIGPIPE on lane selftests that spawn real
  LSP children; the ctest invocation passed both before and after.

5. CJK-source inventory plausibility (item 5), `11-inventory-and-test.log`:

- My own scan for a CJK character inside the first argument of `tr(` / `translate(`
  over `src/**` (comments included, so this is an over-approximation): **0** hits.
- Three reported items confirmed by grep:
  - `src/workspace/diagnostics/diagnostics_controller.cpp`
    `lintCancelledMessage` — `QStringLiteral("已取消 — %1/%2 文件")`;
  - same file, the finished-pass status line —
    `QStringLiteral("%1/%2 文件已扫描 — %3 条 CLI 诊断")`;
  - `src/editor/fim/fim_backend.cpp` — `QStringLiteral("FIM 请求未被 Rust 服务接受")`.
  (Also checked the intentional `src/workspace/settings/settings_panel.cpp`
  `"简体中文"` endonym — correctly classified as not a defect.)
- The inventory's claim A (0 `translate()`-wrapped CJK sources after the fix) is
  consistent with my scan; section (D) of the writer's log is a fair self-check.

## 3. Findings

### N1 — Nit — the new regression locks the rendered English label, not the translation reachability
`hover.fix-button-label-english` asserts `fixButton->text() == "Fix"`, which is exactly
the defect class being repaired, but it cannot distinguish
`translate("VEdit", "Fix")` from a bare `QStringLiteral("Fix")`: a future edit that
drops the `translate()` wrapper would keep this check green while silently breaking the
zh_CN rendering. The catalogue reachability is proven *independently* here (lrelease +
lconvert), and the U2b report discloses this split honestly, so this is not a weakened
test — only a coverage gap worth closing in the i18n lane when a translation-loading
selftest exists (assert `QCoreApplication::translate("VEdit","Fix") == "修复"` after
installing the compiled `.qm`).
Where: `src/workspace/diagnostics/selftests/b3_wiring_e2e.cpp`
`hover.fix-button-label-english`.

### Carried (reported by the writer, outside this task's declared scope, not a U2b defect)
The same "Chinese source string" class survives elsewhere: the two raw-Chinese
`diagnostics_controller.cpp` status messages (they are not `translate()` sources, so
repairing them needs two new `VEdit` catalogue messages, which T15's brief forbade
touching) and the FIM lane literals listed in the writer's inventory §4.B. They are
user-visible Chinese in an English UI; the writer reported them instead of smuggling a
broader change in. Direction for the captain: schedule a small follow-up (or fold into
the i18n lane) — this review does not treat them as a U2b failure.

## 4. Declined to judge

- The `简体中文` language-selector endonym — a language name shown in its own language
  is conventional, not an untranslated string.
- CJK literals used as test expectations and the `--selfcheck` fixture text in
  `src/app/main.cpp` — not UI output.
- Whether the zh_CN UI *renders* 修复 in a running product window — not verified by
  loading the `.qm` into the live test shell; proven at catalogue level (compile +
  round-trip). The U2b report declares the same bound.
- The FIM strings' quality/placement — outside the hover Fix defect and outside this
  task's review surface; only their existence in the inventory is checked.

## 5. Verdict

**PASS** — the source string is English with an unchanged `VEdit` context, the catalogue
row and the compiled catalogue agree (`Fix` → `修复`, 0 unfinished), exactly one message
changed in the `.ts`, and the regression was strengthened rather than weakened.

## 6. Bounds of the review

- No full build/relink and no product-binary run (shared build tree; the captain's gate
  is the only full run). The English label is asserted on the real popup widget in the
  offscreen wiring suite; the zh rendering is proven by `lrelease` + `lconvert`, not by
  a live `QTranslator` load.
- `lrelease` used the host tool (`/usr/lib/qt6/bin/lrelease`, Qt 6.10.2), not the
  project's `lrelease` gate script; the gate remains the captain's.
- One direct wiring run hit the documented transient SIGPIPE (141) after all checks
  passed; the single re-run and the ctest run were clean — reported as transient per
  the shared-build-tree notice.
- Did not re-run `lupdate` (deliberately: it would re-derive locations and disturb the
  close-out); the `.ts` change was verified by set comparison against the pre-U2
  baseline and my T10 snapshot.
- Raw evidence: `00-lrelease.log`, `10-ts-diff.log`, `10b-ts-diff-normalized.log`,
  `11-inventory-and-test.log`, `12-rerun.log` in this directory.
