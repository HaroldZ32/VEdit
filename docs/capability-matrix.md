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

Formatting always uses `verible-verilog-format` (the user's decision), for **both**
LSP backends: the action stays enabled under slang as well, and it is greyed only
when the formatter binary itself is missing. Evidence: `evidence/w5-backend/`
records the same formatted output under both backend selections.

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
