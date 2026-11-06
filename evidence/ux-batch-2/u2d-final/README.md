# U2d — close the 5 missing + 3 obsolete catalogue entries (lupdate + translate)

Task: T39 (`U2d`), plan §5.1's final lupdate step, closing the Medium gap from
`evidence/ux-batch-2/review-u3-final-r2/REPORT.md`.
Writer: Junior Engineer (`junior-engineer-2`), sole writer of
`translations/vedit_zh_CN.ts`.
Write scope touched: `translations/vedit_zh_CN.ts` and this directory only.

## 0. Result

- `lupdate -locations absolute -recursive src` in the pinned image:
  **`Found 1211 source text(s) (5 new and 1206 already existing)` / `Kept 3 obsolete entries`**.
- **Nothing lost**: 0 translation texts changed, 0 lost; every surviving
  (context, source) pair carries byte-identical Chinese.
- The 5 new sources translated (Chinese, `%1`/`%2` kept in order).
- The **3 obsolete** entries removed with a written reason each (they are the
  reworded predecessors of three of the five new rows).
- `bash build/gates/lrelease.sh` → **PASS**, `1211 translation(s) (1211 finished,
  0 unfinished, 0 untranslated)`.
- Side finding: the catalogue's `../src/...` locations are **correct** for
  `-locations absolute` on Qt 6 — see §6 (this corrects the earlier F3/F5 note).

## 1. Step 1 — backup and baseline (before any tool ran)

```
$ cp translations/vedit_zh_CN.ts evidence/ux-batch-2/u2d-final/00-catalogue-before.ts
before: 6321 lines, 1209 messages, 0 vanished, 0 unfinished
01181643d6705244b28450cefaada5ba0159bbe852558c42f5897cf665c417ac  translations/vedit_zh_CN.ts
```
(`01-before.txt`; the backup is the whole pre-lupdate file, so any loss is
restorable without a rebuild.)

## 2. Step 2 — lupdate (absolute locations, pinned image)

```
$ docker run --rm -v "$PWD:/vedit-build" -w /vedit-build vedit-qt6-build:local \
    /usr/lib/qt6/bin/lupdate -locations absolute -recursive src -ts translations/vedit_zh_CN.ts
Scanning directory 'src'...
Updating 'translations/vedit_zh_CN.ts'...
    Found 1211 source text(s) (5 new and 1206 already existing)
    Kept 3 obsolete entries
```
(`02-lupdate.log`. Note: an earlier attempt with `docker run --user $(id -u)`
could read nothing and wrote nothing — `Permission denied` on every source; it is
in the same log. The run above is the one as root, and it did not change the
file's owner.)

Post-lupdate state: 1214 messages = 1209 + 5, with 3 marked `type="vanished"`
and 5 `type="unfinished"`.

## 3. Step 3 — proof that nothing was lost

Machine comparison of the (context, source) → translation map before vs after
(`03-keyset-after-lupdate.txt`), run twice: once right after lupdate, once after
the translations were filled in.

| measurement | lupdate only | final |
|---|---|---|
| keys before / after | 1209 / 1214 | 1209 / 1211 |
| added | 5 | 5 |
| removed | 0 | 3 (the obsolete rows, §5) |
| **translation text changed** | **0** | **0** |
| **translations lost (non-empty → empty)** | **0** | **0** |

Every surviving translated pair is identical to the backup:
`every surviving translated pair identical: True`.

## 4. Step 4 — the five new messages, translated

Numbers and their order are kept; wording follows the neighbouring strings
(`当前引擎。` for "Active engine.", `Lint 任务` for "lint job", `应用到 Lint 任务`
for "applied to the lint job") and the user manual's phrasing
(`docs/USER_GUIDE.md:480,488,604,630`: 「整次替换被拒绝，不写任何东西」,
「当前引擎」「Lint 任务」「400 字符预览」).

| # | source (context) | translation |
|---|---|---|
| 1 | `Replace refused: the unsaved edits in "%1" cannot be checked against line %2 (the editor did not provide the buffer text), and that line is longer than the 400-character search preview, so the match cannot be re-identified; nothing was replaced. Save the document and run the search again.` (`SearchPanel`, `search_panel.cpp:1511`) | 替换被拒绝：“%1” 中的未保存修改无法与第 %2 行核对（编辑器未提供缓冲区文本），且该行长度超过 400 字符的搜索预览，无法重新定位匹配项；未替换任何内容。请保存文档后重新搜索。 |
| 2 | `Replace refused: the unsaved edits in "%1" changed where line %2 sits in the buffer, and that line is longer than the 400-character search preview, so the match cannot be re-identified from the preview alone; the document was not saved and nothing was replaced. Save it and run the search again.` (`SearchPanel`, `search_panel.cpp:1518`) | 替换被拒绝：“%1” 中的未保存修改改变了第 %2 行在缓冲区中的位置，且该行长度超过 400 字符的搜索预览，仅凭预览无法重新定位匹配项；文档未保存，也未替换任何内容。请保存后重新搜索。 |
| 3 | `Active engine. --ruleset, --rules, --rules_config and --waiver_files are applied to the lint job; --autofix is stored only (the lint job's C ABI has no field for it).` (`SettingsPanel`, `settings_panel.cpp:2196`) | 当前引擎。--ruleset、--rules、--rules_config 与 --waiver_files 会应用到 Lint 任务；--autofix 仅保存（Lint 任务的 C ABI 没有对应字段）。 |
| 4 | `Active engine. -W and --error-limit are applied to the lint job.` (`SettingsPanel`, `settings_panel.cpp:2205`) | 当前引擎。-W 与 --error-limit 会应用到 Lint 任务。 |
| 5 | `Stored but not applied yet (the lint job's C ABI has no field for them): %1. The values stay in settings.toml and are never sent as a silent no-op.` (`SettingsPanel`, `settings_panel.cpp:2217`) | 已保存但尚未应用（Lint 任务的 C ABI 没有对应字段）：%1。这些值保留在 settings.toml 中，不会作为静默空操作发送。 |

Round trip through the compiled catalogue (`06-roundtrip.txt`): all five come back
out of the `.qm` with exactly these translations, `missing from the compiled
catalogue: 0`.

## 5. Step 5 — the three obsolete entries, removed

Each was marked `type="vanished"` by lupdate (no source emits it any more) and is
the **reworded predecessor** of one of the five new rows; its Chinese was carried
over into the new row where the meaning survived.

| obsolete source | old translation | why dead | replaced by |
|---|---|---|---|
| `Active engine. --rules_config is applied; the other options are stored but not sent yet (transport pending).` | 当前引擎。--rules_config 已生效；其余选项已保存但尚未发送（传输待实现）。 | the transport gap closed: those Verible flags are now really applied, and `--autofix` is the only stored-only one → the sentence was rewritten | new row #3 |
| `Active engine. -W and --error-limit are stored but not sent yet (transport pending).` | 当前引擎。-W 与 --error-limit 已保存但尚未发送（传输待实现）。 | same: `-W`/`--error-limit` are sent now | new row #4 |
| `Stored but not applied yet (the lint job's C ABI does not carry them): %1. They stay in settings.toml and will be sent once the ABI is extended.` | 已保存但尚未应用（Lint 任务的 C ABI 还不承载它们）：%1。它们保留在 settings.toml 中，待 ABI 扩展后发送。 | reworded to the settled behaviour (the values are *never* sent as a silent no-op, rather than "will be sent later") | new row #5 |

All three `<message>` blocks were deleted (the U2 close-out rule: dead entries are
removed, not left silently vanished). Final file: **0 vanished, 0 unfinished**.

## 6. Step 6 — the gate

```
$ bash build/gates/lrelease.sh
[lrelease] /usr/lib/qt6/bin/lrelease …/translations/vedit_zh_CN.ts -qm /tmp/tmp.…/vedit_zh_CN.qm
    Generated 1211 translation(s) (1211 finished and 0 unfinished)
[lrelease] counts: 1211 translation(s) (1211 finished, 0 unfinished, 0 untranslated)
[lrelease] qm: 129051 bytes sha256=b7f2f50cd9d41041b0792f0fc5a2b1fa93b40ac27f384ad80899192b59f240b3
[lrelease] PASS: 0 unfinished and 0 untranslated in …/translations/vedit_zh_CN.ts
exit=0
```
(`05-lrelease-gate.log`.)

**Count arithmetic — 1211, not 1214.** The task expected `1209 + 5 = 1214`, which
assumes the three obsolete rows still count. They do not: lupdate found **1211**
source texts (1206 of the old 1209 still match, 5 new), and `lrelease` counts
active translated messages only. So `1206 + 5 = 1211`, and the 3 dead rows are
gone by rule. Message count in the file: 1214 after lupdate → **1211** after the
three removals; final sha256 of the catalogue:
`6e68ba0507e4c54a0ca7397f445e7004befdbd8ead82e2e7923d0a58de44d455` (`04-after.txt`).

## 7. Side finding — `-locations absolute` on Qt 6 means "path relative to the .ts"

The catalogue stores `../src/...`, and earlier reports (my own T2 `u2-final` F3 and
T36 `u2c-final` F5 included) read that as "the run did not use absolute locations".
That reading is wrong. `lupdate -help` in the pinned image:

```
-locations {absolute|relative|none}
       absolute: Source file path is relative to target file. Absolute line
                 number is stored.
       relative: Source file path is relative to target file. Line number is
                 relative to other entries in the same source file.
```

Both modes store the *source path relative to the target file*; they differ in how
the **line number** is stored. `../src/...` is therefore exactly what
`-locations absolute` produces, and this run used it. The "6 translations lost to
relative paths" story cannot be re-confirmed from the tool's semantics alone —
worth re-checking the original incident if it still matters, but it is not an
argument for changing the location mode.

## 8. Files and commands summary

| step | command | evidence |
|---|---|---|
| backup | `cp translations/vedit_zh_CN.ts …/00-catalogue-before.ts` | `01-before.txt` |
| extract | `docker run … lupdate -locations absolute -recursive src -ts translations/vedit_zh_CN.ts` | `02-lupdate.log` |
| proof | key-set comparison before/after | `03-keyset-after-lupdate.txt` |
| after | counts + sha256 | `04-after.txt` |
| gate | `bash build/gates/lrelease.sh` | `05-lrelease-gate.log` |
| round trip | `lrelease` + `lconvert` | `06-roundtrip.txt` |

`xmllint --noout translations/vedit_zh_CN.ts` → clean (exit 0). No `<source>` text
was edited, no location was hand-written (lupdate owns them now), and `lupdate`
was run exactly once, as the procedure requires.
