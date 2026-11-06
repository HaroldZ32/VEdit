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
