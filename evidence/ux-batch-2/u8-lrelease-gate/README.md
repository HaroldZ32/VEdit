# U8a — real `lrelease` gate (fails on unfinished/untranslated translations)

Task: T33 — the frozen contract §5.1 requires an `lrelease` gate, but
`package.json` had no `verify:lrelease` and `build/gates/` had no lrelease
invocation (review RF4 in `evidence/ux-batch-2/review-u2-final/REPORT.md`:
`qt_add_lrelease` compiles the `.qm` but does NOT fail on unfinished entries, so
"0 unfinished" was a manual command).
Write scope: `build/gates/`, `package.json`, this directory.
Result: **done** — two files changed (`build/gates/lrelease.sh` added,
`package.json` gained one script line).

## 1. What the gate does

`build/gates/lrelease.sh` follows the shape of `build/gates/docs.sh`: sources
`_common.sh` (repo root discovery, `no_gate`), prints a clear FAIL with the fix,
and exits non-zero. It never skips and never passes when it cannot prove the
counts:

1. runs `/usr/lib/qt6/bin/lrelease <ts> -qm <tmp>/<name>.qm` (`LRELEASE=` may
   point elsewhere; the path defaults to the host pin in AGENT.md §4);
2. FAILS if the tool is missing, if the catalogue is missing/empty, if lrelease
   exits non-zero, if lrelease produces no `.qm`, or if the counts line cannot be
   parsed;
3. FAILS on **any** unfinished entry **and** on **any** untranslated entry
   (`Ignored N untranslated source text(s)`), and on `0 generated`;
4. on success prints the observed counts
   (`N translation(s) (M finished, K unfinished, U untranslated)`) and the `.qm`
   size + sha256 for the record.

Usage: `bash build/gates/lrelease.sh [catalogue.ts]` — the optional argument
exists only for the negative control below; the delivered run uses
`translations/vedit_zh_CN.ts`. `package.json`:
`"verify:lrelease": "bash build/gates/lrelease.sh"` (next to `verify:docs`; the
file is still valid JSON, checked with `python3 -c 'json.load(...)'`).

`bash -n build/gates/lrelease.sh` → OK.

## 2. Verification (quoted, with exit codes)

### (1) The real catalogue PASSES — exit 0

```
$ bash build/gates/lrelease.sh
[lrelease] /usr/lib/qt6/bin/lrelease <root>/translations/vedit_zh_CN.ts -qm /tmp/tmp.Kw6CvwHxV5/vedit_zh_CN.qm
    Generated 1186 translation(s) (1186 finished and 0 unfinished)
[lrelease] counts: 1186 translation(s) (1186 finished, 0 unfinished, 0 untranslated)
[lrelease] qm: 125621 bytes sha256=34cdc5bc09d38810c4070eb1b2e19e6c8de99943e3316f30ba388ba13fe23ac9
[lrelease] PASS: 0 unfinished and 0 untranslated in <root>/translations/vedit_zh_CN.ts
gate_exit=0
```

Catalogue under test (read-only for the gate):
`sha256 55261d0136028fc0a87b8c766a4ad84ee4027ab72ff0c082571fba02362f9779`,
`mtime 2026-11-06 10:25:21`, `size 302280`.
Log: `01-real-catalogue-passes.log`. The `.qm` sha256 is reproducible (two
consecutive runs under `LC_ALL=C.UTF-8` give the same 125621 bytes / same hash).

### (2) NEGATIVE CONTROL — the gate exits NON-ZERO naming the counts — exit 1

A catalogue with the historical 182 new texts was regenerated with `lupdate`
(the same command the handover records, `01-lupdate.log`):

```
$ cp evidence/ux-batch-2/integration-translations/00-catalogue-before.ts target/u8-lrelease-neg/neg.ts
$ lupdate src -locations absolute -ts target/u8-lrelease-neg/neg.ts      (in the pinned Qt6 image)
Scanning directory 'src'...
Updating 'target/u8-lrelease-neg/neg.ts'...
    Found 1186 source text(s) (182 new and 1004 already existing)
    Kept 14 obsolete entries
    Same-text heuristic provided 5 translation(s)

$ bash build/gates/lrelease.sh target/u8-lrelease-neg/neg.ts
    Generated 1009 translation(s) (1004 finished and 5 unfinished)
    Ignored 177 untranslated source text(s)
[lrelease] FAIL: 5 unfinished and 177 untranslated translation(s)
       (1004 finished of 1009 generated).
       ...
neg exit=1
```

Log: `03-negative-182-unfinished-fails.log`. `grep -c 'type="unfinished"'` on
that file = **182** — the 182 are reported by lrelease as **5 unfinished + 177
untranslated**, which is why the gate checks both counters.

### (3) Extra negative control — `0 unfinished` alone is NOT enough — exit 1

The same 182 entries with their translations emptied (lupdate's own shape for a
new string: `<translation type="unfinished"></translation>`) produce a catalogue
that lrelease reports as finished:

```
$ bash build/gates/lrelease.sh target/u8-lrelease-neg/neg-untranslated-only.ts
    Generated 1004 translation(s) (1004 finished and 0 unfinished)
    Ignored 182 untranslated source text(s)
[lrelease] FAIL: 0 unfinished and 182 untranslated translation(s)
       (1004 finished of 1004 generated).
untranslated-only exit=1
```

Log: `05-zero-unfinished-but-182-untranslated-fails.log`. A gate that asserted
only `0 unfinished` would have PASSED this file.

### (4) FAIL-CLOSED paths — exit 1 each

```
$ bash build/gates/lrelease.sh does/not/exist.ts
[lrelease] FAIL: the translation catalogue is missing or empty: <root>/does/not/exist.ts
missing-cat exit=1

$ LRELEASE=/nonexistent/lrelease bash build/gates/lrelease.sh
[lrelease] FAIL: the Qt6 lrelease tool is not available: /nonexistent/lrelease
missing-tool exit=1
```

Logs: `04a-missing-catalogue-fails.log`, `04b-missing-tool-fails.log`.

## 3. Correction — the fixture named in the task does not carry 182 unfinished

The task (and handover §3.U2) says
`evidence/ux-batch-2/integration-translations/00-catalogue-before.ts` "has 182
unfinished". **Measured, it has none** — it is the catalogue *before* the
lupdate that added the 182 new strings (`01-lupdate.log`: "Found 1186 source
text(s) (182 new and 1004 already existing)"):

```
$ bash build/gates/lrelease.sh evidence/ux-batch-2/integration-translations/00-catalogue-before.ts
    Generated 1018 translation(s) (1018 finished and 0 unfinished)
[lrelease] PASS: 0 unfinished and 0 untranslated ...
claimed exit=0
$ grep -c 'type="unfinished"' evidence/.../00-catalogue-before.ts
0
```

Log: `02-claimed-fixture-actually-passes.log`. The 182-unfinished state is only
reproducible by running lupdate on that file, which is exactly what §2(2) does —
so the negative control is real, but it comes from the regenerated catalogue, not
from that backup file. Reported plainly rather than presented as the named
fixture.

## 4. Reproduce

```
# positive
bash build/gates/lrelease.sh

# negative (needs the pinned Qt6 image for lupdate)
cp evidence/ux-batch-2/integration-translations/00-catalogue-before.ts target/u8-lrelease-neg/neg.ts
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -lc '/usr/lib/qt6/bin/lupdate src -locations absolute -ts /vedit-build/target/u8-lrelease-neg/neg.ts'
bash build/gates/lrelease.sh target/u8-lrelease-neg/neg.ts    # exit 1: 5 unfinished + 177 untranslated

# fail-closed
bash build/gates/lrelease.sh does/not/exist.ts                # exit 1
LRELEASE=/nonexistent/lrelease bash build/gates/lrelease.sh   # exit 1
```

## 5. Notes and bounds

- `translations/vedit_zh_CN.ts` was **not** modified by this task (the gate reads
  it); its hash/mtime are recorded in `01-real-catalogue-passes.log`. Its mtime
  changed at 10:25 while this task was running — that is another lane's edit,
  not this one; the gate was re-run after it and still passes (1186/0/0).
- Nothing else was touched: no build was run, `translations/`, `packaging/` and
  `src/` are untouched. `AppRun` is irrelevant here and untouched.
- The gate is a standalone id like `docs` (`bun run verify:lrelease`); it is not
  folded into `build/gates/gates.sh` because `docs` is not either. There is no
  `.mpd/mpd-gates.json` in the workspace, so `mpd_verify_evidence` cannot run it
  by id — adding such an entry (or folding it into `gates.sh`) is a captain
  decision outside this task's write scope.
- Known limit, measured: lrelease counts an empty `<translation></translation>`
  **without** `type="unfinished"` as *finished* (a converted copy reported
  "1186 finished, 0 unfinished, 0 untranslated"). lupdate never emits that shape
  — it writes `type="unfinished"`, which lrelease reports as unfinished or
  untranslated — so both classes the toolchain can produce are covered.
