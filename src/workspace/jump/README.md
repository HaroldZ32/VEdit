# Jump lane - one jump contract for every panel (AC-7.7)

A *jump* sends the editor to `file:line:column`. Every surface that can do that
(outline/diagnostics rows, module tree, project file list, search results, file
tree, breadcrumb bar) uses this lane instead of opening files itself.

## The contract

```cpp
#include "jump/jump_registry.h"

// Fire a jump (1-based line/column, like the UI shows them):
vedit::workspace::jump::JumpResult result =
    vedit::workspace::jump::jumpTo(filePath, line, column);

// A panel that wants to reflect the no-target state (stand-alone build):
if (!vedit::workspace::jump::hasTarget()) { /* grey out / status line */ }
```

`IJumpTarget::jumpTo(file, line, column)` returns `true` only when the jump was
performed. The registry reports what happened (`NoTarget`, `InvalidRequest`,
`Delivered`, `Rejected`) and keeps counters plus `lastResult()` for evidence and
a future status-bar hint.

## Why a jump failed (`JumpResult::reason`)

A refusal used to be a bare `Rejected`: the panel printed "the editor rejected
the jump to X" with no clue which check refused it, so every distinct cause
(missing file, no editor view, a cursor that did not land, ...) looked identical
(RM-4, real-machine finding). `JumpResult::reason` therefore carries one sentence
saying WHY, filled by the registry for every status: the target's own
`IJumpTarget::lastError()` for `Rejected`, and a fixed sentence for `NoTarget` /
`InvalidRequest`. A panel shows it, and nothing else changes - a refusal is still
a refusal, with no retry and no silent fallback.

```cpp
if (result.status == vedit::workspace::jump::JumpStatus::Rejected) {
    show(result.reason); // "cannot open <path>: not a regular file [workspace root: …]"
}
```

`IJumpTarget::lastError()` has a default (empty) implementation, so a target that
cannot explain itself keeps compiling and the caller falls back to its generic
sentence.

## The path rule

A jump names a file with an ABSOLUTE path. The rule for the one target the
product installs (`search::EditorJumpTarget`, `src/workspace/search`) is:

| the request | what happens |
|---|---|
| **absolute** path | opened **as given**. It may point OUTSIDE the workspace on purpose — a `.f` filelist may name files above the opened folder — so it is never re-rooted and never rejected for being outside. |
| **relative** path | resolved against the **workspace root** (`shell->projectRoot()`), never the process working directory: an AppImage started from a file manager runs in `$HOME`, and resolving there is what produced "cannot open $HOME/x.sv" instead of opening the file (RM-4). A producer is expected to send absolute paths; this is the ONE recovery for a producer that does not. |
| path that exists under neither reading | **refused**, and the reason names the rule that was applied (`absolute path opened as given; files outside the workspace are allowed` / `relative path resolved under the workspace root` / `relative path, not found under the workspace root`) plus the workspace root. |

The producer's half of the rule: `crates/vd-deeplint::tool_paths` resolves every
path it takes from `slang` (`--ast-json` `source_file` and `--diag-json` `file`)
against the directory the tool actually ran in, because slang reports paths
relative to its own cwd. A catalog that hands out a relative path is a producer
bug, not something this seam can guess around.

## The no-target rule

A jump with no registered target is a supported **no-op**: the registry records
it (`NoTarget`), dispatches nothing and never crashes. A panel therefore cannot
require the shell in order to paint or to handle a click.

## Registering the real target (shell/editor lane)

The shell owns one target; the jump lane deliberately contains no file-opening
code.

```cpp
class EditorJumpTarget final : public QObject, public vedit::workspace::jump::IJumpTarget {
    bool jumpTo(const QString &file, int line, int column) override {
        // open through the ONE shell-level open seam, then position the cursor
        // (convert 1-based line/column to the kernel's 0-based cursor)
    }
};
vedit::workspace::jump::setTarget(target);          // after installPanels()
vedit::workspace::jump::clearTargetIf(target);      // in the target's destructor
```

A QObject target is weak-guarded by the registry: even without `clearTargetIf`,
a destroyed target is dropped and a later jump reports `NoTarget` instead of
dereferencing a dead object. A non-QObject target must clear itself.

`installJump()` (called by `src/workspace/integration.cpp`) only resets the
registry for the new shell, so a target belonging to a destroyed shell is never
reused.

## Consumers in this cell

- `src/workspace/breadcrumb/breadcrumb_bar.[hc]*` - each breadcrumb segment is a
  clickable button that jumps to the container's `Entry::line` (AC-7.6).
- diagnostics-presentation / module-tree / project-model / shell-integration:
  they depend on this header and registry only; no second jump implementation.

## Test

`ctest -R vedit-jump-tests` - no-target no-op, invalid request, delivery,
replacement, `clearTargetIf`, and the destroyed-QObject guard. The log is kept
under `evidence/jump/`.
