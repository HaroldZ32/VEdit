# Pre-review: instrument audit + pre-fix baselines

Reviewer · 2026-10-30 · tree **clean at `c5216c1`** (= `origin/main`), T1 (fix) **not yet landed**.
Nothing in this directory is a verdict on the fix: it freezes the baselines and audits the
measuring instruments **before** the change exists, so the eventual AC-1..AC-8 evidence cannot
be biased by reading the implementation first.

## 0. State proof at audit time

```
$ git status --short            ->  ?? evidence/github/        (only the captain's untracked evidence dir)
$ git diff HEAD --stat           ->  (empty)
$ git rev-parse --short HEAD     ->  c5216c1
$ grep -rn grab_focus crates/    ->  crates/vd-app/src/app.rs: self.find_entry.grab_focus()   (Ctrl+F only)
$ grep -n 'starts_with("Verible LSP")' crates/vd-app/src/app.rs ->  922  (head test; append at tail)
```

So R1/R2 are still present in the tree. Board: T1 `in_progress` (Senior Engineer), T2 (this
review) `pending`, blocked by T1.

## 1. Frozen pre-fix baselines (from committed evidence, not from a rebuild)

| baseline | value | source | validity for `c5216c1` |
|---|---|---|---|
| `--selftest all` | **51 checks**, 6 suites (smoke 20 / highlight 8 / config 6 / lsp 6 / slang 6 / fim 5), `ok=true` | `evidence/wiring/selftest-all.json` @ `b6c31d3` | `git diff b6c31d3 HEAD -- crates/vd-app/src crates/vd-app/tests` is **empty** → the tree the JSON came from is this tree |
| `--selftest all` (cross-check) | 51/51 on all three release artifacts, `vd-app` sha256 `41d02dca…1611` | `evidence/release-verification/README.md` (commit `c5216c1`) | the packaged binary is this HEAD's build |
| `cargo test --workspace` | **221 passed / 0 failed / 0 ignored** (21 `test result:` lines) | `tests/e2e/gates-frozen-run.txt` @ `d9f6911`, `tracked_modified=0` | `c5216c1` adds only `evidence/release-verification/**` |
| `cargo fmt --check`, `clippy -D warnings` | exit 0 | same record | same |

Per-check inventory (name :: detail) of the 51 selftest checks: `baseline-checks.txt` (this dir).
AC-8 rule that follows: the post-fix `--selftest all` must be a **superset** of these 51 names
with every old name still passing, and no old assertion weakened.

`grep -c 'report\.check(' crates/vd-app/src/selftest.rs` = 68 **call sites**, but only **51** execute
in an `all` run (suite gating + conditionals). The runtime JSON is the authoritative baseline; the
source count alone is not comparable.

## 2. `probe-cpu.sh` — AC-7 instrument (4 findings)

**I1 · Blocker for AC-7 evidence.** The script's sampling covers app age **23 s** only:
`3 s` startup wait + `sleep 2` (baseline read) + windows `3 s + 5 s + 10 s`. AC-7 requires
"运行 ≥ 60 秒" and "**最后 20 秒**平均占用 ≤ 5%"; the script has no 20 s window at all. Running it
unmodified therefore **cannot** produce AC-7 evidence, no matter what it prints.

**I2 · High (comparability trap).** `APP_BIN` defaults to `/vedit-build/build/artifacts/vd-app`.
Today that is byte-identical to `target/release/vd-app` (both sha256 `41d02dca…`), so the recorded
pre-fix baseline is legitimate — but after the fix this default measures the **old** binary.
Post-fix runs must pass `APP_BIN=/vedit-build/target/release/vd-app` explicitly.

**I3 · Medium (exit code is not the verdict).** The script exits 0 on the normal path *and* on
"RESULT: app died during sampling" (the loop `break`s, then `exit 0`). AC-7 evidence must quote the
`ticks=/delta=/pct` lines, never `$?`.

**I4 · Low (documented, no action).** `pct = delta / nominal_sleep`; real elapsed exceeds the
nominal sleep by scheduling overhead, so the percentage is slightly **over**-estimated — i.e.
conservative against the ≤5% threshold. Direction is safe.

**Proposed resolution (contract-owner decision, captain):** extend the *same* script to windows
`2 3 5 10 20 20` → app age 63 s, the final window is exactly the required "last 20 s average", and
first-vs-last window growth is the "≤ 2 pp" trend rule. Re-run the **before** column with the same
extended script against the pre-fix artifact `build/artifacts/vd-app` (sha256 `41d02dca…`, = HEAD's
`target/release/vd-app`), so before/after differ only by the fix. The reviewer should not author its
own yardstick; an identical-method extension under `fix-verify/` is only a fallback and must be
declared as a deviation from "同一脚本".

## 3. `probe-typing.sh` + `xtype.c` — AC-1 e2e instrument (3 findings)

**I5 · High (false `TYPING_BROKEN`).** `RESULT:` is computed from `cmp` alone. If the injection never
happened (`xtype` exits 3 because no window matched, or `XSetInputFocus` was ineffective), the file is
unchanged and the script still prints `RESULT: TYPING_BROKEN`. AC-1 e2e evidence must require
`xtype_exit=0` **and** the RESULT line; a non-zero `xtype_exit` is an instrument failure, not a
product finding.

**I6 · Medium (coverage).** The probe opens one file and never switches tabs → it can evidence
**AC-1 only**. AC-2 (focus follows open/switch) needs a second step (open a second fixture, switch
page, type again) or relies on the `smoke` assertion alone — the contract only *mandates* the
selftest assertion, so this is a coverage note, not a blocker.

**I7 · Medium (control pair required).** Xvfb has no window manager; the injector forces X input
focus onto the toplevel and GTK then routes keys to its internal focus widget — which is exactly the
mechanism AC-1 targets, so the probe is valid. But it only proves anything as a pair: the **pre-fix
control must show `xtype_exit=0` + `TYPING_BROKEN`**, and the post-fix run `xtype_exit=0` +
`TYPING_WORKS`. The control must be recorded before the fixed binary replaces the tree.

## 4. What the diff will be judged on (written before seeing it)

- AC-1/AC-2: `window.get_focus()` compared **by widget identity** to the current tab's `view`
  (both directions: equal to the new view, not equal to the previous tab's view) — never `is_some()`.
- AC-3: overlay getter (`overlay_pass_through(&ghost_fixed)`) assertion, not a comment/flag.
- AC-4: N ≥ 100 polls → status length **constant** + LSP substring **≤ 1** occurrence +
  a regression assertion that runs **without a display** (unit test, not a `smoke` check).
- AC-5: observable counter or mark identity proves no delete/rebuild when diagnostics are unchanged.
- AC-6: `state.lsp == None` still drains slang + FIM (no early `return`).
- AC-7: numbers from the extended protocol above, before/after side by side.
- AC-8: gates re-run by the reviewer in the pinned image; 51-name baseline superset; `git diff`
  inspected for deleted/skipped/weakened assertions; no edit outside `crates/vd-app/**`.

## 5. Declined to judge (this round)

- Contract §1's 33/48/73 % vs the reproduced 36.3/50.2/77.6 % — same protocol, run-to-run variance;
  not a defect. The comparison table must use the reproduced numbers.
- `tests/e2e/gates-frozen-run.txt` contains two `fatal: Invalid pathspec magic 'exclude'` lines from
  its own capture script (pre-existing, outside the fix's scope; the record still shows
  `tracked_modified=0`).
- Child processes (`verible-verilog-ls`, `slang`) are not measured by `probe-cpu.sh`; AC-7 is about
  the UI main loop, and the pre-fix baseline used the same scope, so the comparison is valid.

---

## 6. Re-audit of the revised instruments (2026-10-30T16:1x)

The captain revised `probe-cpu.sh` (windows `2 3 5 10 20 20`, `SUMMARY:` line, exit 5/6) and
`probe-typing.sh` (`INJECTION_FAILED` + exit 6), re-recorded both BEFORE columns, and updated
contract §1 to the extended protocol. **AC-1..AC-8 text is unchanged — no criterion was weakened**;
only the measurement protocol and the before-column numbers moved. Verified.

I re-ran both probes myself against the archived pre-fix binary
(`target/probe-prefix/vd-app`, sha256 `41d02dca…1611` — byte-identical to `build/artifacts/vd-app`):

- `10-typing-before-reproduced.txt`: `CONTROL_WORKS`, control buffer `a`, `xtype_exit=0`; then
  VEdit pre-fix `xtype: window=0x600002`, `xtype_exit=0`, `RESULT: TYPING_BROKEN`, file unchanged.
  Control pair holds → the probe discriminates, and issue #1 is reproduced.
- `11-cpu-before-reproduced.txt`: 37.3 / 52.2 / 79.4 / 96.5 / **97.9 %**, `growth_pp=60.6`,
  `SUMMARY: app_age=60s`, `RESULT: OK`, `probe_exit=0` — reproduces the captain's
  36.7 / 51.0 / 76.9 / 95.7 / 97.4 (growth 60.7) within run-to-run noise.

Residual findings on the instruments (none blocks AC-7/AC-1 evidence if read as below):

**N1 · Low (label).** `SUMMARY: app_age=60s` sums the sampling windows only; the app is actually
alive ≈ **63 s** (3 s startup wait + 60 s). Label it `sampled_span` or add the startup wait;
AC-7's "≥ 60 s" is satisfied either way and my evidence states the derivation.

**N2 · Medium (semantics).** `RESULT: OK` means *sampling completed*, not *AC-7 passed*: the
pre-fix run prints it next to 97.9 % of a core and +60.6 pp growth. Rename to `RESULT: SAMPLED` or
emit a separate threshold verdict. **My AC-7 verdict will be computed from `SUMMARY`, never from
this line.**

**N3 · Medium (evidence integrity).** Both captain-produced BEFORE files are curated rather than
verbatim: `typing-before-fix.txt` omits the injector's `xtype: window=0x…` line, which the tool
always prints on success (my rerun has it), and both CPU captures omit the `xwininfo` toplevel dump
(my rerun has it, including the app window `0x600002 "VEdit" 1120x780`). The conclusions are
unaffected — I reproduced both — but the **after** column must be a raw capture.

**N4 · Medium (measurement environment).** A process's measured CPU share can only be *lowered* by
host contention, so a loaded host could make a spinning app read < 5 %. The AC-7 after-run must
record `nproc` + `/proc/loadavg` next to the numbers and be taken on a quiet host. (The pre-fix
sanity run above was taken at loadavg ≈ 7.8 on 16 cores and still measured 97.9 %, so contention
was not material there.)

Instrument status for the fix review: **fit for purpose** (AC-1 e2e, AC-7), with N1/N2 read as above.
