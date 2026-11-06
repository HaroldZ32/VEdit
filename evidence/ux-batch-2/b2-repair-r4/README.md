# B2 repair r4 — per-hit identity beyond the 400-character preview (review P5)

- Task: **T27**, Blocker from the terminal review `evidence/ux-batch-2/review-b2-r4/REPORT.md`
  (finding: *"the per-hit identity is the trimmed 400-character preview, so a preview-identical
  duplicate inserted above a hit is replaced instead of the scanned line"*; probe `P5`).
- Gate sentence for this lane: **替换 / Replace All must never write wrong characters into a file the
  user just saved.**
- Repair date: 2026-11-06. Write scope: `crates/vd-search/`, `src/workspace/search/`,
  `evidence/ux-batch-2/b2-repair-r4/`.
- Result: the reviewer's own P5 probe, **rebuilt unchanged from its source** against the repaired
  lane library, now reports `PROBE SUMMARY failures=0`; a new lane regression reproduces P5 and
  **fails on the pre-fix code** (5 failures) and passes after it; the 275-check baseline is intact
  (0 missing) and the lane suite is **299 checks / 0 failures / 0 skips**.

## 1. The defect, as reproduced before the fix

Review P5 (`evidence/ux-batch-2/review-b2-r4/logs/03-cap-probe.log`) on the reviewed tree:

```
PROBE P5.started=1 saves=1 ok=1 replacements=1 line0-right='XXXXXXTAIL_B' line1-equals-original=1
PROBE P5.no-wrong-target FAIL (BAR was written into the inserted near-duplicate, the scanned line is untouched)
PROBE SUMMARY failures=1
```

Code path: `GlobalSearch::runScan` sets `hit.lineFingerprint = matchedLineFingerprint(hit.text)`,
and `hit.text` is the Rust `snippet_of` preview (line **trimmed, capped at `TREE_SNIPPET_CHARS` =
400**, plus U+2026). `matchedLineFingerprint` hashes exactly that preview. The two guards that trust
it — `SearchPanel::replaceCurrentHit`'s pre-save buffer check and
`ProjectReplacer::applyReplaceRanges`' write-time check — therefore treat a line and a line that is
identical in its **first 400 trimmed characters** (and differs only after them) as the same line.
A dirty buffer that pastes such a line above the hit is accepted by the pre-save check; the panel
saves the buffer, re-captures the stamp it wrote itself, and 「替换」 writes the replacement into the
**inserted** line while the scanned line moves down untouched, reporting success.

This is not a theoretical bound: it is the same wrong-target class the lane exists to remove, and it
writes into a file the operation just saved.

## 2. Direction chosen, and why

The task offered (a) a full-line digest carried through the ABI, or (b) a conservative refusal when a
dirty buffer's identity cannot be proven. I implemented **a third variant of (a): full-line identity
without an ABI change**, and (b) only as the fallback when no buffer text is available.

Why not the literal (a): at scan time the C++ side only has the 400-character preview, so a full-line
digest would need a new `vd_search::TreeHit` field, a new `VdSearchHit` field (cbindgen ABI change,
`crates/vd-ffi` + every FFI consumer) **and** a hash on the Rust side — `crates/vd-search` has no hash
dependency and the workspace rule forbids a lane adding one. That is a cross-crate change for a
property the panel can already prove locally.

Why this variant is exact, and no ABI change is needed: before the save-first step the panel holds

1. the scan's **per-file stamp** (`size` + ms `mtime`, captured at scan finish, and re-checked
   immediately before this step), which proves the file on disk still holds the content the matcher
   read;
2. `OpenDocumentBridge::bufferText()`, the user's current buffer text;
3. `readDocumentLine()` — the disk file's line at the hit's line number, decoded with **the same
   decoder the writer uses** (`UTF-8`, `Stateless | ConvertInitialBom`) and split the same way.

So for a modified buffer whose hit preview is truncated, the panel now requires the buffer's line at
the hit position to be the **whole line** the scan read (`normalizedLineIdentity()`: BOM dropped and
trimmed, the same normalisation the fingerprint uses; everything after column 400 is compared in
full). If it is not, the replace is **refused before anything is saved**, with a reason that names
the file and line and says nothing was replaced. If the bridge cannot expose the buffer text, a
capped preview is refused too — no proof, no write.

Difference from the literal (b): (b) would refuse **every** dirty-buffer replace of a >400-character
hit line, including the legitimate case. This refuses only when the buffer line actually differs from
the scanned line; `preview-cap-ok.*` is the positive control that a dirty edit **below** the hit still
replaces the scanned line.

The write-time guard (`applyReplaceRanges`) keeps the preview fingerprint as its own last line of
defence. For the dirty-buffer path it is now backstopped by the whole-line check; the remaining
exposure is the already-disclosed external-rewrite bound (size and mtime preserved), which the
terminal review placed outside the gate sentence.

## 3. Code changes (all inside `src/workspace/search/`)

- `search_types.h` / `search_types.cpp`
  - `matchedLinePreviewTruncated(preview)` — true exactly for the scanner's truncated form (401
    Unicode scalar values ending in U+2026, i.e. `snippet_of` appended its ellipsis after the 400 kept
    characters). A line that legitimately ends with '…' but is not capped is ≤400 values, so the two
    cases stay apart.
  - `normalizedLineIdentity(line)` — the whole-line comparison key (BOM stripped, trimmed).
  - `matchedLineFingerprint` refactored onto the same normaliser; behaviour unchanged (the existing
    fingerprint checks are untouched).
  - `SearchHit::lineFingerprint`'s comment corrected: it is a whole-line identity only while the line
    fits the cap.
- `project_replace.h` / `project_replace.cpp`
  - `readDocumentLine(path, line, error)` — one 1-based line decoded exactly like the replace writer
    (`QStringDecoder(Utf8, Stateless | ConvertInitialBom)`, NUL/invalid-UTF-8 refused) and split
    exactly like `applyReplaceRanges`. A null `QString` means "cannot prove", an empty non-null one is
    a real empty line.
- `search_panel.cpp` (`SearchPanel::replaceCurrentHit`, the modified-buffer branch, before
  `ensureBuffersSaved`)
  - Capped preview + modified buffer: compare the buffer's line with `readDocumentLine()` and refuse
    on mismatch (or when the buffer text is unavailable). Two distinct user-facing reasons.
- `search_replace_selftest.cpp` — new regressions (registered in `main`):
  - `testPreviewCapTruncationDetector` (4 checks) — pins the cap detector.
  - `testPreviewCapDuplicateRefused` (11 checks) — the P5 shape: a preview-identical duplicate
    inserted above the hit must be refused, **no save**, disk byte-identical; plus the same line with
    a changed tail.
  - `testPreviewCapLegitimateReplace` (9 checks) — positive control: an edit **below** the hit still
    replaces the scanned line and leaves the appended line intact.
- `README.md` §8 item 8 — the "重复行" and "替代桥" bullets now state the 400-character cap, the
  whole-line pre-save check and the narrowed residual (no longer "identical line = harmless").

No Rust or FFI file was changed: `crates/vd-search/` is untouched by this repair (`git status` shows
its two modified files dated 2026-11-05, from an earlier batch).

## 4. Evidence

All commands run inside the pinned image `vedit-qt6-build:local`, offscreen, against the shared build
tree `target/qt6-build`. Only `vedit-search-replace-tests` (and the reviewer's probe) were built —
the full gate `build/gates/qt6-cxx.sh` was **not** run (the captain runs it once).

### 4.1 Red before the fix — `logs/01-red-before.log`

The new regression, run against the **pre-fix** panel (the fix hunk reverted, everything else
unchanged). Exit code **1**:

```
CHECK preview-cap.refused FAIL (ok=1 replacements=1 error=)
CHECK preview-cap.no-save FAIL (saves=1)
CHECK preview-cap.disk-unchanged FAIL (BAR XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX)
CHECK preview-cap.error-names-file FAIL
CHECK preview-cap.tail-disk-unchanged FAIL (BAR XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX)
SUMMARY checks=299 failures=5 skips=0
```

`ok=1 replacements=1` with the file rewritten to `BAR …` is exactly the reviewer's P5 finding; the
five red checks are the wrong-target write, the save that should not have happened, the corrupted
disk file and the missing reason.

### 4.2 Green after the fix — `logs/02-green-after.log`

Same binary rebuilt from the repaired tree. Exit code **0**:

```
SUMMARY checks=299 failures=0 skips=0
```

The 275-name baseline is intact, by my own diff against the terminal review's name list
(`logs/05-parity.txt`): baseline 275, present 275, **missing 0**, added 24.

### 4.3 The reviewer's P5 probe, rebuilt unchanged — `logs/03-reviewer-p5-probe.log`

I compiled the reviewer's **unmodified** `probes/probe_cap.cpp`
(`evidence/ux-batch-2/review-b2-r4/probes/probe_cap.cpp`) against the repaired
`libvedit_workspace_search.a`, with a copy of the reviewer's own build script whose only change is
the output path (`probes/build-cap-probe.sh` in this directory). Exit code **0**, 68 `PROBE` lines,
0 `FAIL`:

```
PROBE P5.started=1 saves=0 ok=0 replacements=0 line0-right='XXXXXXTAIL_A' line1-equals-original=0
PROBE P5.no-wrong-target PASS (not reproduced)
PROBE SUMMARY failures=0
```

The single 「替换」 now refuses (`ok=0`), the buffer is **not** saved (`saves=0`), and the disk file
still holds the scanned line (`TAIL_A`). The probe's other 60+ checks (N1/N1b save-during-scan, N2
hand-off, N3 dirty-buffer single replace, N4 empty query, N5 control lock, …) also stay 0-failure.

### 4.4 The registered ctest entry — `logs/06-ctest.log`

```
1/1 Test #33: vedit-search-replace-tests .......   Passed    5.77 sec
100% tests passed, 0 tests failed out of 1
```

### 4.5 Warnings as errors — `logs/04-werror-recompile.log` (helper: `probes/recompile-werror.sh`)

The four changed translation units were recompiled from their exact
`compile_commands.json` command with `-Wpedantic -Werror` added (the project already compiles with
`-Wall -Wextra`). Exit 0, no output — no warning.

### 4.6 Reproduce

```
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e LANG=C.UTF-8 -e QT_QPA_PLATFORM=offscreen vedit-qt6-build:local bash -c '
    cmake --build target/qt6-build --target vedit-search-replace-tests &&
    target/qt6-build/src/app/workspace-lane-search/vedit-search-replace-tests --selfcheck &&
    ctest --test-dir target/qt6-build -R vedit-search-replace-tests --output-on-failure &&
    bash evidence/ux-batch-2/b2-repair-r4/probes/build-cap-probe.sh && /tmp/b2-r4-cap-probe'
```

The red run is reproduced by removing the `b2-repair-r4` block in
`SearchPanel::replaceCurrentHit` (everything else unchanged), rebuilding and re-running.

## 5. Disclosure wording — must change

The lane's own disclosure was **false**: it described the bound as an *identical* line inserted
above a hit, while the implemented identity was the trimmed 400-character **preview**. The user guide
repeats the same claim. Exact places:

| # | File:line | Current sentence (verbatim) | Required |
|---|---|---|---|
| 1 | `src/workspace/search/README.md` §8 item 8 (`重复行` bullet; was lines 290–292) | “若在命中行**上方**插入一行且其文本与命中行完全相同，行号处的文本仍相同，指纹无法区分…” | **Changed by this repair** — now states the 400-character cap, the whole-line pre-save check, the refused preview-identical duplicate, and the harmless fully-identical copy. |
| 2 | `evidence/ux-batch-2/b2-repair-r3/README.md:135` (item 1) | “Only an external rewrite that leaves every hit line **byte-identical** at the same line number is indistinguishable…” | The identity is the **trimmed first 400 characters**, not the byte-identical line: say “identical in its first 400 trimmed characters (the preview cap)”. (Evidence lane — not mine to edit.) |
| 3 | `evidence/ux-batch-2/b2-repair-r3/README.md:138–142` (item 2) | “**Duplicate line inserted above a hit.** … If a line **identical to the hit line** is inserted above it, the hit's line number still holds that text and the fingerprint matches; the replacement lands on the same coordinates in the same text (the copy). Detecting that needs a whole-file identity the ABI does not carry; …” | For a **preview-identical** line (identical in the first 400 trimmed characters, different later) the single 「替换」 on an unsaved buffer is now **refused**; only a **fully identical** copy remains indistinguishable, and then the replacement lands on the same text (harmless). (Evidence lane — not mine to edit.) |
| 4 | `docs/USER_GUIDE.md:610` | “在扫描窗口之外插入与命中行**完全相同**的文本行（逐命中指纹只看那一行的内容，不知道行号偏移）；” | “插入与命中行**前 400 个字符相同、之后不同**的文本行” — and note that for an unsaved buffer this is now refused. (Docs lane — not mine to edit.) |
| 5 | `docs/USER_GUIDE.md:611` | “非产品桥（宿主不提供 `bufferText()`）时，未保存缓冲区的预保存校验退化为"先保存，再让写入端的逐行指纹拒绝"。” | Add: for a hit line longer than the 400-character preview, such a bridge now **refuses** (nothing is saved, nothing is written) because the whole-line identity cannot be proven. (Docs lane — not mine to edit.) |
| 6 | `docs/USER_GUIDE.md:612` | “这些边界之外，替换在写每个文件前都会用冻结快照 + 每命中行指纹 + 文件戳校验；不符就拒绝并说明原因，不会写错字符。” | Still true, but incomplete: add the whole-line pre-save check for unsaved buffers. (Docs lane — not mine to edit.) |

## 6. Bounds of this repair (stated honestly)

- **Not run:** the full gate `build/gates/qt6-cxx.sh` / `scripts/gates.sh` (per the shared-build
  instruction; the captain runs the single gate at the end); `bash scripts/build-qt.sh` (only the lane
  test target and the reviewer probe were built); no relink of `vedit-app`; no real xcb GUI session.
- **Real KTextEditor bridge:** the dirty-buffer regressions inject `OpenDocumentBridge`
  (`BufferTextBridge`, the same shape the r3 repair used). The product bridge satisfies the two
  assumptions by construction — `bufferText()` returns `document->text()` and `save()` writes it — but
  the live-editor path was not re-exercised this round.
- **Trust in the bridge:** the pre-save whole-line check compares the buffer text with the disk line;
  it assumes `bufferText()` is the text `save()` writes. The existing fingerprint check already made
  that assumption; the product bridge is consistent.
- **Pre-existing read→save window:** the file can change between the panel's stamp check / disk-line
  read and its save-first write; the save overwrites such an external change. This window existed
  before this repair (the stamp check has always been followed by the save); the write side's stamp
  check remains the backstop. The terminal review explicitly declined to judge this race.
- **Write-time guard granularity:** `applyReplaceRanges` still uses the preview fingerprint. For the
  dirty-buffer path the panel's whole-line check now covers it; for a **clean** file the remaining
  exposure is the already-disclosed external rewrite that preserves size **and** mtime, which the
  terminal review placed outside the gate sentence.
- **No tests or gates were edited to obtain a pass** — the new checks were written first and observed
  failing on the pre-fix tree (§4.1), then passing on the fixed tree (§4.2).

## 7. Files changed

```
src/workspace/search/README.md                 (disclosure corrected)
src/workspace/search/search_types.h            (cap detector + whole-line key declarations, comment)
src/workspace/search/search_types.cpp          (implementations; shared normaliser)
src/workspace/search/project_replace.h         (readDocumentLine declaration)
src/workspace/search/project_replace.cpp       (readDocumentLine implementation)
src/workspace/search/search_panel.cpp          (whole-line pre-save check + two refusal reasons)
src/workspace/search/search_replace_selftest.cpp (3 regressions, 24 checks, registered in main)
evidence/ux-batch-2/b2-repair-r4/              (this README, logs/, probes/)
```

Nothing outside this scope was written by this repair. `crates/vd-search/` is unchanged (its
`git status` modifications are dated 2026-11-05, from an earlier batch); the other modified files in
the workspace belong to the other lanes that are active in the same shared tree and were not touched
here.
