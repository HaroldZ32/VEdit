# Backend capability matrix (T11 / AC-1.3)

Two language-server backends and two CLI lint engines can be selected at runtime
(Tools page → `settings.toml` → live switch, no application restart). This page is
the human-readable counterpart of the C ABI table
(`vd_lsp_backend_capabilities(backend)` / `vd_lsp_session_capabilities(session)`);
the UI **reads that table** and never keeps a second copy of these values.

## How a row is established (`basis`)

| basis | meaning |
|---|---|
| `measured` | the backend's `initialize` response was recorded in this cell's spike (`evidence/w1-rust-engine/ls-capabilities.json`, `evidence/w2-spike/`) |
| `live` | the running session's own advertisement, after its `initialize` handshake; it **overrides** the measured row while the session lives |
| `declared` | no measurement exists in this environment; a row is never guessed |

`source` names the exact binary/version or the live handshake the rows came from;
`note` carries the one-sentence asymmetry. A row is `supported = 1 | 0 | -1`
(`-1` = **unknown: connect to know**); an unsupported row always explains why.

## The table (measured)

> **T50 re-measurement (2026-11-02).** T30's review found this table contradicted the
> live servers on two rows. Every row below was re-measured with a real `initialize`
> handshake (`evidence/w5-backend/harness/capability_probe.py`; raw output
> `cap-verible-live.txt`, `cap-slang-live.txt`), and cross-checked against the C ABI
> table the UI actually reads (`CAP-ROW`/`CAP-GATE` lines in `w5-probe-t50.log`).
> Two rows were wrong and are corrected here: **verible `codeAction` is yes**, not no
> (it advertises `codeActionProvider: true`, and the gate enables it with
> `source=backend` — the document was the wrong side, the product was right), and
> **verible `workspaceSymbol` is no**, not yes (it does not advertise
> `workspaceSymbolProvider`; slang does). A capability is counted only when its value
> is truthy: verible publishes `hoverProvider: false` rather than omitting the key.

| capability | verible-verilog-ls | slang-server 0.3.1 | UI rule |
|---|---|---|---|
| documentSymbol | yes | yes | enabled (outline) |
| workspaceSymbol | **no** | yes | informational, NOT a gated action — the outline is driven by `documentSymbol` (T50) |
| definition | yes | yes | enabled — both backends serve it |
| references | yes | yes | enabled |
| rename | yes | yes | enabled (preview; the editor applies) |
| documentSymbol-based jump / outline | yes | yes | follows the selected backend |
| documentFormatting / rangeFormatting | yes | **no** | **ignored for gating**: formatting ALWAYS runs `verible-verilog-format` (below) |
| hover | **no** | yes | greyed under verible with its reason; enabled under slang |
| completion | **no** | yes | same as hover |
| inlayHint | **no** | yes | same as hover |
| codeAction | **yes** | yes | enabled — verible advertises `codeActionProvider: true` (live, T50) |
| callHierarchy | **no** | yes | informational, **NOT a gated action**: there is no `callHierarchy` row in the 13-row ABI table, so no menu item is enabled or greyed by it (T50 checked this against the `CAP-ROW` dump) |
| signatureHelp | **no** | **no** | **not gated by this table** — the editor synthesises it locally (T16); absent, not broken, until the synthesiser is in the build |
| executeCommand | — | yes | informational (no gate row either; slang's command list is its own UI surface) |

The two backends are mirror images: verible is the formatting/symbol side, slang
is the hover/completion/action side. That asymmetry is exactly why the UI must
not claim a capability it cannot serve.

## Formatting is not a backend capability (AC-1.4)

Formatting never depends on the selected LSP backend, for **either** backend: the
action stays enabled under slang as well, and it is greyed only when the formatter
binary itself is missing. Evidence: `evidence/w5-backend/` records the same
formatted output under both backend selections.

The formatter is selected by **language**, not by backend (batch 20261110c):

| language | formatter | command | evidence |
|---|---|---|---|
| Verilog / SystemVerilog | `verible-verilog-format` | the Format page's 23 knobs | `evidence/batch-20261110c/rename-context/` / `docs/BUILD.md` §3 |
| VHDL | `vsg --fix`, behind a **GHDL syntax gate** | `Format Document` (`diagnostics.format`) is re-owned by the VHDL lane and dispatches by language; the result is ONE undoable buffer edit, never a save | `evidence/batch-20261110c/vhdl/04-format.log` |

The VHDL branch is not a backend capability either: it is the same user command,
routed by file language (`.vhd` / `.vhdl`). The VHDL lane replaces the handler of
`diagnostics.format`; the non-VHDL branch stays the verible path byte for byte.

## Unknown rows and the live table

Before a slang session has answered `initialize`, a row may be `-1`; the UI then
shows “connect to know” instead of pretending support or absence. Once the session
is ready, `vd_lsp_session_capabilities` replaces the row set with `basis = live`.
If a user points `tools.slang_server` at a different server build, the live table
wins over the measured one — no recompilation, no hardcoded list.

## Two engines, two paths (AC-1.6 — do not collapse them)

| path | engine | selection | endpoint |
|---|---|---|---|
| live diagnostics of an open file (outline, jumps, diagnostics) | LSP server | `tools.lsp_backend` = `verible` \| `slang` | `vd_lsp_session_start_v2(backend, …)` |
| project-wide batch lint | CLI linter | `tools.lint_engine` = `slang` \| `verible` | `vd_deeplint_lint_v2(engine, …)` / `vd_deeplint_batch_start` |

The two selections are independent and take effect independently: changing one
must not silently change the other. De-duplication between the streams uses
`vd_diagnostic_key` (origin LSP / CLI / both), never string comparison in the UI.

## Parameter echo (AC-2.5)

The exact argv a session/lint run would use is observable without spawning
anything: `vd_lsp_command_line(...)` / `vd_lsp_session_command_line(...)` and
`vd_lint_command_line(...)`. The product emits one machine line per switch:

```
BACKEND-SWITCHED backend=slang lint=verible-verilog-lint lsp=<path> engine=<path> basis=measured argv='<argv>'
```

## Switching rules (AC-1.2)

1. Changing either selector reconfigures the seams and re-runs the analysis over
   the SAME widgets; the application is never restarted and the panel is never
   recreated.
2. Before the new backend answers, the previous rows are cleared explicitly — a
   stale diagnostic must never be shown under the new backend's name.
3. The session status line always names its producer: `backend=verible|slang`.
4. A switch that cannot resolve the selected binary is refused with the tool name
   and the three fixes; the previous selection stays active.

## Workspace capabilities (batch 20261110c) — verified / proven unavailable / not verified

The rows above answer "what can the selected LSP backend do". The rows below answer
"what did this batch verify in the product". They use the **same three-state
vocabulary** as the Metals matrix (AC-7.9 / AC-7.15): **verified** (a run exists and
its log is cited), **proven unavailable** (a failure is logged and no shipped
mitigation exists), **not verified** (advertised or plausible, nobody drove it end to
end — neither claimed available nor called unavailable). A capability is never counted
from a code reading alone. A row whose capability this batch made **available** is
marked `verified — available` (driven end to end; `available` is the user-facing state).

| capability | state | what was measured | evidence |
|---|---|---|---|
| Diagnostics badge: `<req>→<eff> · lint=… · basis=… · reason=…` | verified | real `QLabel::text()` read back; the same text is still there 8 s later and after a restart | `evidence/batch-20261110c/diag-ui/badge/01-persist.log` (E1–E3) |
| Badge shows a refused switch instead of hiding it | verified | request `slang` with no `slang-server` → `slang→verible · lint=slang · basis=measured · reason=tool-missing` | same log (E1) |
| Badge `warnings` marker ` · ⚠ N` + settings load warning text | verified | constructed `settings.toml` with `tools.lsp_backend = "emacs"`: one `SETTINGS-WARNING` sentence carries the illegal spelling, the legal list **and** the value actually used; badge gains `· ⚠ 1`; `reason=ok` stays truthful for the selection | same log (AC-N4 case iii) |
| Waiver — verible (append per-diagnostic line) | verified at lane level | real `waive` line written, deduped, re-fed to the engine and the diagnostic suppressed | `evidence/batch-20261110c/diag-ui/waiver/01-live.log` §4–§6 |
| Waiver — slang (`[[waivers]]` TOML) | verified, product level | real file + registration in one async job; duplicate not re-added; a row without an option name is refused, never widened to a whole-file waiver; re-feed suppresses the diagnostic | same log §1, §2, §6 |
| Waiver context-menu click (GUI) | not verified | the menu is attached to the real panel tree and the env-gated probe calls the **same** handler; no xdotool/WM in the image, so the right-click itself was not driven | same log, “Honest boundaries” |
| Autofix mode classification (`--autofix`) | verified | all eight stored values classified by the real product; `patch-interactive` / `inplace-interactive` unavailable with the terminal reason; unknown spelling refused | `evidence/batch-20261110c/diag-ui/autofix/01-modes.log` |
| Autofix hover `Fix` click | not verified (code path only) | the button's gate and the generate-waiver route compile in; the click/confirmation dialog were not driven (no xdotool/WM) | same log, “Honest boundaries” |
| Environment import: capture → diff → checkboxes → staged import → atomic save → restart round-trip | verified | 47 panel checks + 20 real hit-test click checks; default unselected; project layer default; no wholesale overwrite; broken file degrades to empty + warning | `evidence/batch-20261110c/env-ui/01-dialog.md` |
| Environment import: secret masking / four failure modes | verified | secret-shaped names painted `<masked>`, not persisted without an explicit reveal; rc/shell/timeout/cancel/non-NUL failures each user-visible with no half-apply | `evidence/batch-20261110c/env-ui/02-mask.log`, `03-failures.log` |
| Imported environment reaches the FIRST tool child | verified, product level | three real restarts against one state dir; the child's own env dump shows `SEAM_PROBE=1` only with the file, absent without it, absent + one warning with a broken file | `evidence/batch-20261110c/env-apply/README.md`, `02-product-probe.log` |
| Project-wide rename (slang AST engine) | verified | 27 index tests + 11 apply tests green; four measured unresolvable categories refused WHOLE; real-spawn path regression | `evidence/batch-20261110c/rename-core/README.md` |
| Project-wide rename UI (preview / apply / cancel / rollback) | verified | real product path: per-file preview rows, atomic apply with sha256 checks, failure → all files rolled back, cancel writes nothing | `evidence/batch-20261110c/rename-ui/02-preview.log`, `03-apply.log`, `04-cancel.log` |
| Project-wide rename on projects needing `-I` / `-D` | **verified — available** (T72) | the rename C ABI now carries the project's effective `-I`/`-D` (additive `include_dirs`/`defines` at the END of `VdRenameOptions`; that T72 append landed at `VD_ABI_VERSION` 1, and the version is **2** since batch 20261120 — a provider-filled struct growth also bumps it, see `docs/BUILD.md` §7.5). The C++ lane reads them from the same `vedit.diagnostics.projectEnv` expansion the lint/LSP children receive, and a property published for a DIFFERENT root contributes nothing. Negative control on the include-based fixture with empty lists: `not_nameable` (the declaration is not in the indexed set); with `-I inc`: index/plan across 3 files, apply `verified=1`, sha256 changes only the 3 expected files, `inc/defs.vh` unchanged — and the probe's header-copy workaround is deleted with an assertion that it stays absent. Boundary: the project environment must be published for the CURRENT root; before that, the preview fails with the engine's reason instead of guessing an include dir | `evidence/batch-20261110c/rename-include/` (README §4.1–§4.3, `01-rust-abi-test.log`, `02-product-probe-preview.log`, `03-product-probe-apply.log`, T74 `06-fake-green-fix.log`, `09-t74-fake-green-and-abi-note.md`) |
| VHDL: `vhdl_ls` session (initialize, diagnostics), files do not touch verible/slang | verified | real `vhdl_ls` session, `VHDL-ROUTE … verilog-backends=not-contacted`, process sample `1 vhdl_ls` / `0` verible+slang | `evidence/batch-20261110c/vhdl/README-T13.md`, `01-lsp.log` |
| VHDL: hover / go-to-definition in the PRODUCT UI | not verified | the session answers both (probe PASS), but the product has no menu/action wiring for them | same README §6.2 / §6 |
| VHDL: `vhdl_ls.toml` priority, generated default mapping, explicit refusal | verified | user file wins and is never rewritten; generated single-library mapping; broken config refused with one reason, server keeps running; multi-library boundary recorded (not a whole-project elaborate) | `evidence/batch-20261110c/vhdl/02-config.log` |
| VHDL: merged GHDL+VSG lint | verified | one row list, per-row `source`, documented severity map (unknown kept as 0), counted dedupe, clean file = 0 rows, tool refusal = explicit note | `evidence/batch-20261110c/vhdl/03-merged-lint.log` |
| VHDL: `vsg --fix` formatting with GHDL gate | verified | temp copy → GHDL syntax gate → VSG `--fix` → GHDL re-check → one undoable buffer edit; never saves; failures are typed stages | `evidence/batch-20261110c/vhdl/04-format.log` |
| Scala one-click run (sbt / scala-cli shapes) | verified | shape detection + argv (`sbt -Dsbt.offline=true run`; `scala-cli run --offline --server=false --jvm system <root>`), async stream, visible exit code, process-tree cancel, neither-shape explicit error | `evidence/batch-20261110c/scala/01-run.log`, `scala-run/README.md` |
| Scala examples offline in the delivered form | verified | read-only AppDir + `--network none`: SpinalHDL → `Counter.v`, Chisel → `Counter.sv`, both exit 0; two prewarmed homes never mix | `evidence/batch-20261110c/scala-payload/README.md`, `01-appimage-offline.log` |
| Metals LSP session offline (bundled launcher) | verified at wrapper/payload level | real session, read-only AppDir + `--network none`: initialized, BSP connected 7.25 s, compiled 2.9 s, 6 × `publishDiagnostics`, 0 external TCP 443/80 | `evidence/batch-20261110c/scala-metals-launch/README.md`, `01-launch-session.log` |
| Metals in the EDITOR (LSP selection / hover / definition) | not verified / not claimed | the product's LSP selectors are `verible` / `slang` (+ `vhdl_ls` for VHDL); no product path starts a Metals session in this version | `docs/USER_GUIDE.md` §9.5; matrix §C |
| Offline "zero network" | **bounded** | 0 successful external TCP (443/80), but DNS (UDP 53) attempts still occur, all `ENETUNREACH`; a literal zero-syscall claim needs BSP-level `--offline` and is **not** made | `evidence/batch-20261110c/scala/05-network-audit.md`, `scala-metals-launch/README.md` |
| `vedit-rm9-status-dots-e2e` gate row | known flake (≈11%) | final-consistency surface asserted against a 400 ms poll; product behaviour is by design, test not edited | `evidence/batch-20261110c/gate-fixes/README.md` §2.3 |

## Metals / Scala offline capability matrix (AC-7.9 / AC-7.14 / AC-7.15)

Three states, never two-and-a-half: **verified offline** (a run in this batch or the
spike did it, with a log), **proven unavailable** (a failure with a log, and no
mitigation exists), **not verified** (the server advertises it, nobody drove it end to
end — it is *neither* claimed available *nor* called unavailable). The full tables,
including the per-point preload audit of the eight T20 gaps, are in
`evidence/batch-20261110c/scala/metals-matrix.md` (this page mirrors its verdicts; the
evidence file stays the source of truth).

**A — verified offline** (evidence is a run, not a claim): Metals LSP `initialize`;
Metals ↔ scala-cli BSP connection; build import; compile of a Scala 2.13.18 project;
the diagnostics channel (6 × `publishDiagnostics`); `metals --version` returning the
release-time list with exit 0; a Scala 3 default project via `scala-cli compile
--offline`; the `scala-cli run` demonstration; no `~/.metals` extraction needed.
Evidence: `evidence/batch-20261110c/scala/04-metals.log`, `05-network-audit.md`.

**B — proven unavailable offline** (failure logged; no preload can fix it): the live
*network* supported-Scala-version list fetched by `metals --version` (it parses the
live HTML with jsoup; no cache key exists that it would read — the release-time
fallback is the shipped behaviour and is byte-identical for 1.6.9); resolving a
dependency that is not in the shipped caches (the honest behaviour is the
named-coordinate error).

**C — not verified in this batch** (advertised, never driven end to end, NOT called
unavailable): Metals hover / definition / references / rename; Metals on a Scala
2.13.12 (SpinalHDL) project; Metals on a Scala 3 project; Metals + sbt BSP; debug
adapter / worksheets / scalafix.

**AC-7.15 branch**: the batch lands on **(a)** for editor capabilities — every editor
capability driven end to end worked offline, and the one proven-unavailable item is a
CLI diagnostic (`metals --version`), not something the editor calls. **No capability
page / status area is added.** The user-facing wording is one line, mirrored verbatim
into `docs/USER_GUIDE.md` §9.5 and here:

> 首次启动 `metals --version` 之类的诊断命令会尝试抓取 Maven 索引；离线时会显式报一行
> `Could not check supported Scala versions, returning list available at release` 并回退到随包的
> 发布版 Scala 列表（exit 0）。编辑器里的 Metals 会话不发起该请求：编译与诊断在完全断网下可用。

> The first `metals --version`-style diagnostic run tries to scrape the Maven index; offline it logs
> `Could not check supported Scala versions, returning list available at release` and falls back to the
> release-time Scala list shipped in the jar (exit 0). The editor's Metals session never makes that
> request: compile and diagnostics work fully offline.

The old wording — "Metals' supported-Scala-version check still attempts one HTTP
request **when the server starts**" — is **wrong about the server start** and must not
be reused (spike S4 conclusion, re-confirmed by the batch's audit).
