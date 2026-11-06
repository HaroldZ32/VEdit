# U8b repair (T38) — the three stale Chinese assertions in `vedit-lint-orchestration-probe`

- **Task:** T38, the ONE red test of the full gate run (`evidence/ux-batch-2/u8-final/qt6-lane.log`,
  ctest #37): `vedit-lint-orchestration-probe` → `ok=0 checks=62 failures=3`, the three failing
  checks being `cancel.message-counts`, `widget.cancel-message`, `widget.finished-text`.
- **Cause (confirmed):** U2c wrapped the two hard-coded Chinese lint status strings in
  `QCoreApplication::translate("VEdit", …)` with **English source text**
  (`evidence/ux-batch-2/u2c-final/README.md` §1.1). With no `QTranslator` installed the widget
  renders the English source, so the probe's assertions still matching the old Chinese text fail.
  The gate log's other `FAIL unavailable tool=…` / `FAIL spawn phase=…` lines are the probe's own
  printed narrative, not CHECK lines; `failures=3` counts exactly the three CHECKs.
- **Result:** the probe is green again with the SAME check count — `LINT-ORCHESTRATION-PROBE
  ok=1 checks=62 failures=0` — and no other stale assertion of a U2c-wrapped string exists anywhere
  under `src/**`.

## 0. One scope correction (recorded, like T6's)

The probe source lived at `evidence/w4-lint/lint_orchestration_probe.cpp` and the lane's
`CMakeLists.txt` compiled it from there. That is **outside this task's write scope**
(`src/workspace/diagnostics/`, `evidence/ux-batch-2/u8-repair-37/`), and it is also the wrong home:
it is a **living ctest source**, not a closed evidence artefact (the repo rule says an older
batch's evidence must not be rewritten for a new claim).

So the source was **moved into the lane**: `src/workspace/diagnostics/selftests/lint_orchestration_probe.cpp`,
and `src/workspace/diagnostics/CMakeLists.txt` now points `VEDIT_LINT_PROBE_SOURCE` at it (the ctest
id `vedit-lint-orchestration-probe` is unchanged; the build log shows the new object path
`…/vedit-lint-orchestration-probe.dir/selftests/lint_orchestration_probe.cpp.o`). The old
`evidence/w4-lint/lint_orchestration_probe.cpp` is **left byte-for-byte untouched** (mtime
2026-11-02 16:50, not built any more) — removing it is the captain's call when consolidating
evidence; it is outside this writer's scope.

## 1. The three assertions — what each asserts now, and why it is the same guarantee

All three are in the moved probe (`src/workspace/diagnostics/selftests/lint_orchestration_probe.cpp`).

| check | before (old Chinese) | after (English source) | same guarantee |
|---|---|---|---|
| `cancel.message-counts` | `after.message.contains("已取消 —")` | `after.message.contains("cancelled") && after.message.contains("20/100")` | **Stronger, same fact.** It still requires a CANCEL message (the message that follows `cancelProjectPass()`), and it now also pins the done/total numbers the old phrase implied. Observed: `CANCEL after phase=4 running=0 message=«cancelled — 20/100 file(s)» diagnostics=0`. |
| `widget.cancel-message` | `strip.statusText().contains("已取消 —")` | `strip.statusText().contains("cancelled") && strip.statusText().contains("/100")` | Same fact: the strip renders a CANCEL message (not "idle", not a finished line) after `requestCancel()`; `/100` keeps the total-count shape the old phrase carried. |
| `widget.finished-text` | `strip.statusText().contains("CLI 诊断")` | `strip.statusText().contains("file(s) scanned") && strip.statusText().contains("CLI diagnostics")` | Same fact: the terminal status is the FINISHED/scanned sentence reporting the CLI diagnostics count (old `%1/%2 文件已扫描 — %3 条 CLI 诊断`); the check still fails if the text is a cancel message or a bare count. |

No check was deleted, renamed or weakened; the probe still reports **62 checks** (same as before —
only expectation strings changed, plus the count substring inside two existing checks).

## 2. Staleness sweep (the whole tree, before rebuilding)

Method: the U2c report §1 lists the **22** strings it wrapped (2 diagnostics status strings + 20 FIM
strings). I searched the tree for their old/Chinese forms — the exact needles were
`已取消`, `文件已扫描`, `条 CLI`, `CLI 诊断`, and each `FIM …` sentence — over every
`.cpp/.h/.hpp/.cc/.ts/.qml/.js`, excluding `.git/`, `target/`, `.mpd/`, `build/`, `scripts/`,
`docs/`, `.codegraph/` and `evidence/` (evidence is closed artefacts; the one exception is handled
in §0). Reproduce with the Python one-liner pattern in §5.

Findings, and what was done:

1. **`src/workspace/diagnostics/selftests/lint_orchestration_probe.cpp`** — the three stale
   assertions (the red test). **Fixed here** (§1).
2. **Three stale DOC comments** in the diagnostics headers, in this task's write scope — they still
   described the old Chinese text: `diagnostics_controller.h:129`, `diagnostics_types.h:214`,
   `lint_progress.h:13`. **Fixed** to the English source with the zh catalogue rendering noted
   (`cancelled — N/M file(s)` / `已取消 — N/M 文件`). Comments only; no code change.
3. **`src/editor/fim/fim_selftest.cpp:310,315,351,355`** — Chinese literals in FIM assertions, but
   **NOT stale**: those tests give a `FakeBackend` reply whose own `text` is Chinese and
   `fim_inline_completion.cpp:154/157` uses `reply.text` verbatim when it is non-empty, so the
   assertion checks the fake's own message round-trip (self-consistent by construction). Outside
   this task's write scope; **reported, not changed**.
4. **`src/workspace/diagnostics/selftests/lint_orchestration_probe.cpp` (comments)** — the new
   comments quote the OLD Chinese text on purpose, for traceability. Not assertions.
5. **`translations/vedit_zh_CN.ts:4446,4451,…`** — the zh translations U2c added for the 22 rows.
   Expected; not stale.
6. **No other literal Chinese assertion under `src/**` matches any string U2c wrapped.** In
   particular no other test asserts the cancelled/scanned lint sentence.

## 3. Verification (commands, observed output, exit codes)

Build (targeted, inside the pinned image; no full gate):

```
cmake --build target/qt6-build --target vedit-lint-orchestration-probe
  [82/83] Linking CXX executable …/vedit-lint-orchestration-probe   (from the NEW selftests/ source)
```

`ctest -R '^vedit-lint-orchestration-probe$' --output-on-failure`:

```
1/1 Test #37: vedit-lint-orchestration-probe ...   Passed    0.02 sec
100% tests passed, 0 tests failed out of 1
```

Direct run (`logs/03-probe-direct.log`), the three repaired checks and the summary:

```
CANCEL after phase=4 running=0 message=«cancelled — 20/100 file(s)» diagnostics=0
CHECK cancel.message-counts PASS
CHECK widget.cancel-message PASS
CHECK widget.finished-text PASS
LINT-ORCHESTRATION-PROBE ok=1 checks=62 failures=0        (exit 0)
```

`ctest -R 'vedit-b3-wiring-e2e|vedit-lint-experience' --output-on-failure` (the same diagnostics
wiring):

```
1/2 Test #42: vedit-lint-experience-selftest ...   Passed    0.51 sec
2/2 Test #45: vedit-b3-wiring-e2e ..............   Passed    6.59 sec
100% tests passed, 0 tests failed out of 2
```

## 4. Bounds — what is NOT proven

- **The full gate was not run** (captain's rule); only the probe target and the two related test
  binaries were rebuilt/run. `vedit-app` was not relinked.
- The **rest of the 50-test ctest suite** was not re-run here; the captain re-runs the one full gate.
  The sweep in §2 is the mitigation for a second stale assertion.
- The old `evidence/w4-lint/lint_orchestration_probe.cpp` still contains the stale Chinese
  assertions (left untouched by scope/rule); it is **no longer compiled** by
  `src/workspace/diagnostics/CMakeLists.txt`. A verifier reading evidence/ alone must not mistake it
  for the live test — the CMake path is the authority. Recommend the captain delete it when
  consolidating.
- The sweep is literal-substring based, so it cannot find an assertion that rephrases the old
  text rather than quoting it; it does cover every string U2c wrapped, and its needles are the
  identifying phrases plus the `FIM …` sentences.
- The probe's `FAIL unavailable tool=…` / `FAIL spawn phase=…` narrative lines in the gate log are
  **not CHECK lines** and are not failures (the probe's summary counts only `check()` calls); this
  repair changes neither them nor the fake backend.

## 5. Evidence index (`evidence/ux-batch-2/u8-repair-37/`)

| path | content |
|---|---|
| `01-ctest-probe.log` | `ctest -R '^vedit-lint-orchestration-probe$'` → 1/1 Passed (0.02 s) |
| `02-ctest-wiring-lintexp.log` | `ctest -R 'vedit-b3-wiring-e2e\|vedit-lint-experience'` → 2/2 Passed |
| `03-probe-direct.log` | full probe output: the three PASS lines + `LINT-ORCHESTRATION-PROBE ok=1 checks=62 failures=0` |

Files changed (sha256 at delivery): `selftests/lint_orchestration_probe.cpp` (new)
`64c2911b…f744c4`, `CMakeLists.txt` `f18ef3de…aa1a9720`, `diagnostics_controller.h`
`708df8aa…9256adf`, `diagnostics_types.h` `cd39c89a…9f3de0`, `lint_progress.h`
`5a518647…3d7397`. All are inside `src/workspace/diagnostics/`; nothing outside the write scope was
modified (the old evidence copy keeps its 2026-11-02 mtime).
