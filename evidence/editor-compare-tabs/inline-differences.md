# Inline comparison changes

Paired changed lines retain the existing hunk background. A second, stronger background covers the differing substring between each pair's shared prefix and suffix. The native KTextEditor ranges are local to the comparison view and leave token foregrounds, document contents and undo intact. Supplementary Unicode characters stay whole. Added or deleted lines retain the block cue, and synchronized navigation uses the original document positions.

The inline comparison pairs lines by their position within each hunk. It shows one contiguous span per paired line, up to 4,096 UTF-16 units per line and 10,000 differing pairs per comparison. The block cue still covers longer lines and additional pairs. The comparison keeps source rows at their actual positions.

Previous and Next are available through `Alt+PageUp` and `Alt+PageDown` while focus is in the comparison page. The buttons list these bindings in their tooltips. VEdit's native editors already use `Alt+Up` and `Alt+Down`, the keys used by WinMerge for this action. [WinMerge comparison manual](https://manual.winmerge.org/en/Compare_files.html)

## Observed verification

Four focused CTests passed. An Xvfb/xcb run of `vedit-file-compare-selftest` exited 0 with `FILE-COMPARE-SELFTEST failures=0`. The native range assertions check exact span coordinates, preservation of both surrogate boundaries, background without a foreground override and original buffer text. Qt key events advance and return from the left and right editors. The shortcut does not act on the hidden comparison when an ordinary tab has focus.

- [Pinned build and CTest receipt](../../target/editor-compare-qa/t11-early-EqlQ1PFr/driver.log)
- [X11 functional receipt](../../target/editor-compare-qa/t11-early-EqlQ1PFr/xcb.log)
- [Native SystemVerilog comparison and inline backgrounds](../../target/editor-compare-qa/t11-early-EqlQ1PFr/native-inline.png)
