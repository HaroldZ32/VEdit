# jump-contract — evidence (cell `cell-20261102T050548232Z`, task `T5`)

Frozen contract: `.mpd/cellwork/cell-20261102T050548232Z/contracts/jump-contract.md`.
Deliverable: one application-wide jump contract + process-wide registry, a clickable
breadcrumb bar using it, and the regression evidence for the file tree / search
double-click paths.

## What landed (all inside the frozen write scopes)

| path | role |
|---|---|
| `src/workspace/jump/jump_target.h` | `IJumpTarget::jumpTo(file, line, column)` — the ONE jump interface |
| `src/workspace/jump/jump_registry.h/.cpp` | process-wide registry: `setTarget` / `clearTarget` / `clearTargetIf` / `hasTarget` / `jumpTo`; `NoTarget` / `InvalidRequest` / `Delivered` / `Rejected` + counters; QObject targets are weak-guarded |
| `src/workspace/jump/jump_integration.h/.cpp` | lane entry `installJump()` (resets the registry for a new shell; contains no file-opening code, per the contract) |
| `src/workspace/jump/tests/jump_tests.cpp` | lane self-check (40 assertions) |
| `src/workspace/jump/README.md` | consumer contract for the panel lanes + target-registration recipe for shell-integration |
| `src/workspace/breadcrumb/breadcrumb_bar.h/.cpp` | clickable breadcrumb bar (AC-7.6): each semantic segment jumps via the registry |
| `src/workspace/breadcrumb/tests/breadcrumb_click_tests.cpp` | real QTest mouse clicks over the real structure query (35 assertions) |
| `src/workspace/breadcrumb/CMakeLists.txt` | adds the bar/test and configures the jump lane when the editor lane includes it first |
| `src/workspace/integration.cpp/.h` | `installJump()` is the FIRST install step of `installPanels()` |
| `src/app/CMakeLists.txt` | `jump` added to `VEDIT_WORKSPACE_LANES` (first, so panel lanes can link it) |

No other path was modified by this task. `src/app/app_shell.*` and every other lane
directory are untouched.

## Evidence

| log | what it proves |
|---|---|
| `jump-selftest.log` | 40 checks, 0 failures: no-target no-op, invalid request, delivery, replacement, `clearTargetIf`, destroyed-QObject guard (`SUMMARY checks=40 failures=0`, exit 0) |
| `breadcrumb-click.log` | 35 checks, 0 failures: real left-button clicks on the real semantic path deliver `file:line:column` to the target; a no-target click is `no-target` (no crash). Recorded rows: `BREADCRUMB-CLICK segment=1 text='inc' line=4 ... target-line=4 target-column=1`, `BREADCRUMB-CLICK segment=0 text='m0' line=1 ...` |
| `ac-7.5-regression.log` | `vedit-filetree-tests` passes (incl. `tree.dblclick.file-opens`, `tree.dblclick.file-path`); `vedit-workspace-search-selftest` passes (incl. `jump.click.recorded/file/line/column`) — both lane suites built from the current tree |
| `root-wiring.log` | root configure: `workspace lane 'jump' included`, `workspace lane 'jump' wired (vedit_workspace_jump)`, `workspace integration enabled`; root ctest `(jump|breadcrumb)` = 3/3 passed (exit 0) |
| `integration-tu-compile.log` | `src/workspace/integration.cpp` compiled with the product build's own flags/definitions (`-DVEDIT_WORKSPACE_HAVE_JUMP=1`) and the object references `installJump` (`U vedit::workspace::installJump(...)`) |
| `structural-check.log` | AC-7.7 structural check: exact definition sites, consumption site, lane registration; residual `search`-lane interface noted |
| `root-build-status.log` | product build state: app shell lib, integration lib and `vedit-app` all link (exit 0); `--selfcheck-ffi` PASS (20 checks, 0 failures); `--selfcheck` teardown crash (see below) |

Commands (all inside the pinned Qt6 image, workspace mounted at `/vedit-build`):

```
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  cmake -S src/workspace/jump -B target/lane-jump -G Ninja && \
  cmake --build target/lane-jump && ctest --test-dir target/lane-jump
# same for src/workspace/breadcrumb (which also configures the jump lane)
# root: cmake -S . -B target/qt6-build ... ; ctest --test-dir target/qt6-build -R "(jump|breadcrumb)"
```

## AC mapping

- **AC-7.6** — `vedit::breadcrumb::Bar` renders the semantic path (the syntax source's
  real containers) as one clickable segment per container; a click calls
  `vedit::workspace::jump::jumpTo(file, entry.line + 1, 1)`. Proven with real mouse
  clicks in `breadcrumb-click.log`.
- **AC-7.7** — exactly one `IJumpTarget` for new code (`src/workspace/jump/jump_target.h`);
  the registry is the only jump seam; the breadcrumb bar has no second jump path.
  Structural evidence: `structural-check.log`.
- **AC-7.5** — the existing double-click suites still pass; the jump lane does not
  modify the file-tree/search code.

## Gaps / handoffs (reported to the captain, not silently absorbed)

1. **Mounting the clickable bar in the pane (AC-7.6 user-visible part).** The pane bar
   is still the plain `QLabel` built in `src/app/editor_area.cpp` (write scope of the
   tabs-dirty lane). Swapping it is a ~4-line edit there:
   `Bar *bar_` instead of `QLabel *breadcrumb_`; in `EditorPane::updateBreadcrumb()` call
   `bar_->setPath(currentFilePath(), entries)`. The entries are the same vector the
   binding's `Model` holds; `FoldBreadcrumbBinding` currently exposes only the display
   strings, so the editor lane needs a small `pathEntries()` accessor. Until that edit
   lands, the component + contract + tests exist, but the product pane shows text only.
   The exact recipe is in `src/workspace/jump/README.md` and was mailed to the captain.
2. **One leftover second interface.** `src/workspace/search/search_types.h` still
   declares its own `IJumpTarget`/`EditorJumpTarget`; the search lane is outside this
   task's write scope, so it was NOT rerouted. It is the one place where AC-7.7's "no
   second jump implementation" is not yet true; a follow-up task should replace it with
   `setJumpTarget`→ the process-wide registry (or make `EditorJumpTarget` implement the
   shared interface).
3. **Root shell self-check teardown crash (not this task).** `vedit-app --selfcheck`
   dies with SIGSEGV between `panel-seam.seed-session` and `panel-seam.deferred-nothing-inserted`
   — i.e. destroying an `ApplicationShell` at `src/app/main.cpp:669`. `installPanels()`
   (and therefore `installJump()`) is called only on the interactive path
   (`src/app/main.cpp:1519`), not in `--selfcheck`, so the jump lane is not on that
   path. The same teardown crash ends the root search selftest after all its checks
   PASS. Both live in the shell/editor code the tabs-dirty lane is currently rewriting.
   Not fixed here: `src/app/editor_area.*` / `src/app/app_shell.*` are outside this
   task's write scope.

## Verified vs uncertain

- Verified: lane build + ctest (standalone and root), AC-7.6 click delivery, AC-7.5
  double-click regression suites, jump lane registration/install sequence, product
  link, `--selfcheck-ffi`.
- Not verified / uncertain: `vedit-app --selfcheck` end-to-end (blocked by the teardown
  crash above, unrelated to this lane); the pane-mounted breadcrumb (handoff above).
