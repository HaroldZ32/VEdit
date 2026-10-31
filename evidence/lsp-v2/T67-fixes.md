# T67 — T9 F-1/F-2/F-3 fixes

## C9 header
collected_at_utc=2026-10-31T11:31:08Z
git_rev_parse_HEAD=95c496e682e3841add1a081edd78d29c34957aac
git_status_porcelain (this task's paths):
?? evidence/lsp-v2/
?? src/workspace/diagnostics/

## F-2 — wrong expectation fixed (not relaxed)

The assertion at `diagnostics_ffi_selftest.cpp:234` hard-asserted
`BackendState::Failed` for "verible missing". The controller's aggregation
(`src/workspace/diagnostics/diagnostics_controller.cpp`, `finaliseState()`,
lines ~234-252) is:

```
if (!lspReady && !slangComplete)        state_ = Failed;
else if (!lspReady || !slangComplete)   state_ = Degraded;
else                                    state_ = Ready;
```

A missing verible binary can never make the LSP leg ready, so the state depends on
the SLANG leg: **SLANG usable → Degraded** (one leg good, one bad), SLANG also
missing → Failed. `Degraded` is exactly the contract's "降级可诊断"; the Failed-only
expectation was coupled to a SLANG-less host.

* (a) pre-fix run in the SLANG-available environment:
  `CHECK FAIL degrade.missing-verible-failed  degraded` — `checks=22 failures=1`,
  exit 1 (`14-f2-before-prefix.log`, both environments).
* (b) post-fix run, BOTH environments:
  * SLANG available: `degrade.missing-verible-state PASS degraded`,
    `degrade.missing-verible-not-ready PASS`, message PASS → `checks=23 failures=0`, exit 0;
  * SLANG unavailable: state `failed`, same message PASS → `checks=23 failures=0`, exit 0
  (`15-f2-after-fix-both-envs.log`).
* (c) why `Degraded` is the semantically correct value: the state is the aggregation of
  the two legs; with the LSP leg unable to start and the SLANG leg complete, exactly one
  leg is unhealthy → `Degraded` per the branch above. The check now accepts the explicit
  enum set `{Degraded, Failed}` (both values come from `BackendState`), additionally
  asserts the state is **not** `Ready`, and keeps the message assertion
  (`degrade.missing-verible-message`) — nothing was deleted and the state assertion was
  not dropped.

## F-1 — SLANG success path proven (`16-f1-slang-success-path.log`)

`build/slang/out/slang` existed before the original evidence collection; pointing
`VEDIT_SLANG` at it and running the FFI selftest gives:
`a3c.slang-got-every-file requested=7 completed=7 failures=0`, `a3c.lsp-opened-whole-set opened=7/7`,
terminal `ready (ready: lsp 7/7 file(s), diagnostics 21)` — the same 7-file set feeds both legs (A3c).
The retained fact: in the **default** environment (no `VEDIT_SLANG`) the SLANG leg reports a typed
message (`slang: slang binary not found (tried 4 location(s); set VEDIT_SLANG)`) — degradation,
not a crash. `09-report.md` was corrected accordingly.

## F-3 — C9 porcelain headers

`01-08` and `10-13` (and the new 15/16 logs) now carry
`collected_at_utc` + `git_rev_parse_HEAD` + `git_status_porcelain`.
`00-context.txt` already had one. Other lanes' files were not touched.

## Verification

* diagnostics FFI selftest: green in both environments (above); inside ctest (no
  `VEDIT_SLANG`) it is `23/23` too.
* `bash build/gates/typecheck.sh`: **PASS** (exit 0) — the Rust half runs in the pinned
  image and clippy is clean (`17-typecheck-after-t67.log`).
* root ctest on THIS shared worktree: 14/16. The two failures are
  `vedit-terminal-selftest` and `vedit-app-shell-selftest`, whose lanes
  (`src/terminal/**`, `src/app/**`, new `src/editor/**`) are being edited right now
  (uncommitted in the same worktree). They are outside this task's write scope and
  unrelated to the diagnostics change; the diagnostics test itself passes.
