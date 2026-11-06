# T11 — 双后端在线切换与能力门控（Qt 侧）：evidence map

Contract: `.mpd/cellwork/cell-20261102T050548232Z/contracts/backend-switch.md` (loop
`loop-20261102T063252-aa3764`). Code scope (7 files) + `docs/capability-matrix.md` + this directory.
Takeover note: the task was finished from `handoff/se-ui-1.md`; this README is the missing piece.
**No test and no gate was edited.**

## AC → evidence

| AC | Evidence (file → observed line/check) | Status |
|---|---|---|
| **AC-1.2** switch ≤5 s, no restart, status line `backend=…`, outline/jumps follow the new backend | `w5-probe-takeover.log`: `SWITCH-A backend=verible ready=1 ready-ms=441 … diagnostics=5` and `switch.a.applied / switch.a.cleared-before-restart / switch.a.ready / switch.a.session-names-backend / switch.a.diagnostics` = PASS; `SWITCH-A-ARGV 'backend=verible program=…/verible-verilog-ls'`; product-level status badge `product-status-badge-zoom.png` = `LSP backend=verible · lint=slang · live` | **Half-proven**: verible-switch PROVEN (`ready-ms=441`); switching TO slang EXTERNALLY BLOCKED (§2.1). User-facing wording: “可以切到 verible；切到 slang 仍被一个客户端 bug 阻塞” — never “backend switching implemented” (`switch.b.*` ×3 FAIL in `w5-probe.log`, owner = dw-rust/T3, see §2.1) |
| **AC-1.3** per-backend capability table + gated actions with reasons, UI never fakes one | `docs/capability-matrix.md` (measured table, `basis` = measured/live/declared, `-1` = “connect to know”); probe `gate.formatting-*`, `gate.signature-help-*`, `gate.unknown-*` = PASS (`w5-probe.log`) | PASS |
| **AC-1.4** formatting always `verible-verilog-format` (both backends) | `format-input.sv` / `format-output.sv`; probe `format.under-slang`, `format.matches-verible-cli`, `gate.formatting-enabled-under-verible`, `gate.formatting-source` (`source=always-verible-format`) = PASS | PASS |
| **AC-1.6** live diagnostics = selected LSP backend, project batch = selected lint engine, independent | live side: `switch.a.diagnostics` (verible, 5) PASS; batch side: `pass.slang.terminal` + `pass.slang.diagnostics` PASS (`engine=slang → 'slang' … 4 条 CLI 诊断`); **verible-lint engine**: `pass.verible-lint.diagnostics` FAIL (batch returns 0 CLI diagnostics) — external, §2.2 | **slang engine PROVEN**, **verible-lint engine EXTERNALLY BLOCKED** |
| **AC-2.5** backend argv echo | `SWITCH-A-ARGV` (program + backend), `TOOLCHAIN-PATHS verible=… slang=… format=… lint=…`, `vd_lsp_session_start_v2(backend, -I/-D)` + `vd_lsp_command_line`/`vd_lsp_session_command_line` (code), project `-I/-D` reach both paths | PASS |

Also part of this task: the 3-field annotation fill (`column`/`endLine`/`endColumn`) + stale-marker
clearing, and item 5's payoff at product level: `product-token-span.png` (+ zooms). **Re-verified by the
takeover reviewer**: the red squiggle sits under `really_long_unknown_signal_name` ONLY — a span, not a
whole line — while the diagnostics panel shows Errors 1 / Warnings 1.

## 2. External reds (owners, reproduction) — do not “fix” them in this lane

1. **`vd-lsp` drops slang-server on `initialize` — owner dw-rust / T3 (`crates/vd-lsp/src/client.rs`, mainloop router ≈ lines 573–600).**
   slang-server v0.3.1 sends a server→client notification `slang/setConfig` BEFORE the initialize response;
   the client registers only publishDiagnostics/logMessage/showMessage, and the unknown notification kills
   the mainloop → `server.request::<Initialize>` returns “service stopped”. This is the 3 `switch.b.*` FAILs.
   Repro: `w5-probe` (the run stops at the slang switch; `w5-probe-takeover.log` shows the verible direction
   complete and then no further check lines, which is the same death).
2. **`verible-verilog-lint` batch engine returns 0 diagnostics — owner T8/T3 (`crates/vd-deeplint` / the
   `crates/vd-ffi` batch path).** The CLI reports findings on the same fixture; the batch snapshot finishes
   with 0 CLI diagnostics (`tool='verible-verilog-lint'`). This is `pass.verible-lint.diagnostics`.
   Repro: `vd_deeplint_batch_start(engine=1, verible_binary=.mpd/recon/verible/bin/verible-verilog-lint)`
   on a file with a tab/overlong line.

## 3. Product-level and regression evidence

- `vedit-app --selfcheck` (offscreen, pinned image) → `APP-SHELL-SELFTEST ok=1 checks=245 failures=0`,
  `EDIT-CORE-SELFTEST 67/0`, exit 0 — recorded with the binary sha in
  `../i3-session/40-product-gates.log` (same tree, after the T11 integration change).
- Regression subset after the integration: `ctest -R "(diagnostics|toolchain-ux|edit-core|native-fold|breadcrumb|jump)"`
  → **9/9 passed, exit 0** (no T8/T9 regression).
- Full suite at the same time: 36/37, sole red `vedit-workspace-search-selftest` (multi-tab cache in the
  search probe; owned by the shell lane, unrelated to T11).

## 4. Files in this directory

| file | what |
|---|---|
| `w5-probe.log` | previous owner's full probe run: 38 checks, 4 FAIL — all four are §2.1/§2.2 |
| `w5-probe-takeover.log` | takeover re-run with the real `target/tmp/spike/slang-server`: verible direction green (`ready-ms=441`), then the process stopped at the slang switch — **this log was truncated by a missing SIGPIPE guard, now fixed (T50 §B); use `w5-probe-t50.log` for the complete run** |
| `harness/` | the probe (real binaries, same code paths as the product) |
| `format-input.sv`, `format-output.sv` | unformatted → `verible-verilog-format` output (AC-1.4) |
| `product-token-span.png` + `-zoom.png` | product window: span underline (not whole line) |
| `product-status-badge-zoom.png` | product status bar: `LSP backend=verible · lint=slang · live` (AC-1.2 status line) |

---

# T50 — repair of T30's review findings (2026-11-02)

Three findings, all evidence/documentation level; no product behaviour was changed.

## A. F1 (Medium) — the matrix contradicted the live servers, and the doc was the wrong side
Re-measured both backends with a real `initialize` handshake through a new, re-runnable tool
(`harness/capability_probe.py`, stdlib only; raw output `cap-verible-live.txt`, `cap-slang-live.txt`),
then cross-checked the rows the UI actually reads from the ABI table (`CAP-ROW`/`CAP-GATE` in
`w5-probe-t50.log`).

| row | was | measured (live) | ABI gate | correction |
|---|---|---|---|---|
| verible `codeAction` | no | **yes** (`codeActionProvider: true`) | `supported=1`, gate `enabled=1 source=backend` | **doc corrected to yes** — the product was right, the document was wrong. Gating untouched. |
| verible `workspaceSymbol` | yes | **no** (no `workspaceSymbolProvider` key) | no gate row exists for it | **doc corrected to no**, and the row is marked informational/not-gated |
| `callHierarchy` | “yes for slang” with a hover-style UI rule | slang yes / verible no (confirmed) | **no gate row at all** (13-row table: definition, references, rename, documentSymbol, documentHighlight, formatting, rangeFormatting, codeAction, diagnostics, hover, completion, signatureHelp, inlayHint) | row kept, UI rule corrected to “informational, not gated” |
| `executeCommand` | informational | slang yes | no gate row | note added |
| all other rows | — | confirmed unchanged | — | — |
Two measurement details now in the doc: a capability counts only when its **value** is truthy (verible
publishes `hoverProvider: false` instead of omitting the key), and `basis` for these rows is the live
handshake, not recollection.

**Choice made (the task asked for one, with its reason)**: correct the DOCUMENT, keep the gate. Reason:
the live server advertises `codeActionProvider: true`, the ABI table derives `supported=1` from that same
handshake, and the UI enables the action with `source=backend`. Gating it off would mean the product
pretending a capability the backend advertises does not exist — the opposite of AC-1.3's rule (“never claim
a capability it cannot serve” cuts both ways: never deny one it does serve). The single stale artefact was
the hand-written table row.

## B. Nit — the harness died of SIGPIPE (AGENT.md §T134 class)
First review run: `exit 141` at `switch.a.outline` — a write into a dead LSP child's stdin. `this process`
links the Rust service layer statically, so Rust's runtime `signal(SIGPIPE, SIG_IGN)` never ran in this
host. `w5_probe.cpp` now calls `std::signal(SIGPIPE, SIG_IGN)` before anything else (with the §T134
reference in a comment). Result: the run completes all checks instead of dying mid-way —
`w5-probe-t50.log`: **SUMMARY checks=38 failures=4**, exit 1, the 4 being the two external defects below.

## C. Nit — the formatting “diff” was pictures of nothing
`format-input.sv` was `module top;\nwire w=a b;\nendmodule\n` — a **syntax error**, so
verible-verilog-format emitted no output and input/output were byte-identical. The probe's sample is now
valid and genuinely reformatted (`module top;\n   logic   a ;\n  endmodule\n` →
`module top;\n  logic a;\nendmodule\n`), so the two files differ and `format.matches-verible-cli`
asserts something real.

## T50 result
`w5-probe-t50.log` is the complete run (38 checks, 4 external failures — unchanged set, owners in §2.1/§2.2);
`docs/capability-matrix.md` rows now match the live servers and the ABI table; `evidence/w5-backend/` is the
final state.
