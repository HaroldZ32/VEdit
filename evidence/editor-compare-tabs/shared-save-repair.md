# Shared comparison save repairs

Save As to a different path creates a new ordinary editor document whenever the current file is backed by a comparison. The original document keeps its views, canonical path, undo history, dirty flag and disk fingerprint. The new tab still occupies the same place and keeps its caret and scroll position. Saving either file clears only that file's modified flag. Reopening the original path attaches to its existing document while a view is still open.

The canonical path and guarded save stay on the shared document after the comparison tab closes. Save All refuses an external change for either tab order and leaves the dirty text in memory. Save As refuses a target backed by a different live comparison document, including an externally removed target. It checks the readability of an existing target before writing. If a committed destination cannot be reopened after a concurrent filesystem change, the error identifies that the destination was saved.

The model tracks individual native line boundaries for comparison-managed files. Saves preserve untouched CRLF and LF boundaries, the UTF-8 BOM, and the presence or absence of a final newline. New boundaries adopt the nearby convention. An undo of a recent line join can recover the removed boundary's original format even with another edit still dirty.

## Observed verification

The pinned Qt build passed all four focused CTests in 16.75 seconds. The Xvfb/xcb run of the same executable exited 0 and reported `FILE-COMPARE-SELFTEST failures=0`. The new regressions include these observations.

```text
CHECK save-as.does-not-write-unreadable-target PASS
CHECK save-as.refuses-disappeared-compared-destination PASS
CHECK save-as.saving-a-preserves-b-dirty-state-and-disk PASS
CHECK mixed.eof-hunk-preserves-other-line-endings PASS
CHECK mixed.undo-join-restores-original-crlf PASS
CHECK save-all.ordinary-first-refuses-external-overwrite PASS
CHECK save-all.compare-first-refuses-external-overwrite PASS
CHECK save-all.closed-compare-still-refuses-external-overwrite PASS
CHECK native.syntax-and-diff-coexist PASS
CHECK native.historical-syntax PASS
```

The syntax checks inspect rendered editor line attribute spans for a SystemVerilog keyword, module definition and comment on changed lines of both working views and a historical view.

- [Compilation and four focused CTests](../../target/editor-compare-qa/t9-extended.log)
- [Xvfb/xcb log](../../target/editor-compare-qa/t9-extended-xcb.log)
