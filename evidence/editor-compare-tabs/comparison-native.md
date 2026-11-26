# Native comparison

The comparison opens in the current editor pane. It uses two KTextEditor views with their own scroll positions and view-local diff backgrounds. Open working files share their native documents, including unsaved changes and the editor undo history. A historical version uses its logical path to select the syntax definition and opens as read-only. GNU diff processes use a generation captured at launch. Editing invalidates merge controls until the latest generation has been parsed.

The comparison save path checks the loaded disk fingerprint before writing. Shared ordinary tabs saving the same path use the same guarded provider path. The provider preserves the file's CRLF convention and UTF-8 BOM and uses the normal atomic write. Closing a comparison tab leaves edits available in any other view of the same document. Closing the last comparison view with unsaved changes uses the shell's confirmation.

## Verification commands

```sh
bash target/editor-compare-qa/run-qt.sh ctest --test-dir target/qt6-build \
  --output-on-failure -R '^vedit-file-compare-(selftest|close-active-diff|exit-active-diff|stale-diff)$'
bash target/editor-compare-qa/run-qt.sh env QT_QPA_PLATFORM=xcb \
  VEDIT_COMPARE_SCREENSHOT=/qa/native-compare.png \
  VEDIT_COMPARE_SVN_SCREENSHOT=/qa/svn-compare.png \
  timeout 120 xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' \
  target/qt6-build/src/app/vedit-file-compare-selftest
```

## Observed results

The pinned runner passed all four focused CTests in 16.40 seconds. The xcb selftest exited 0 and reported `FILE-COMPARE-SELFTEST failures=0`.

```text
CHECK local.ordinary-save-shared-buffer PASS
CHECK local.save-crlf-and-final-newline PASS
CHECK local.external-change-refuses-save PASS
CHECK local.cancel-close-preserves-dirty-page PASS
CHECK native.syntax-and-diff-coexist PASS
CHECK native.scroll-aligns-unchanged-lines PASS
CHECK native.typing-stays-in-buffer PASS
CHECK native.ctrl-z-restores-baseline PASS
CHECK native.ctrl-s-persists-typed-edit PASS
CHECK native.historical-typing-blocked PASS
CHECK teardown.child-killed-and-reaped PASS
CHECK stale.first-result-rejected-and-latest-used PASS
```

- [Focused CTest log](../../target/editor-compare-qa/ctest-state-6O3JxzlD/Temporary/LastTest.log)
- [X11 test log](../../target/editor-compare-qa/t2-native-xcb.log)
- [Native SystemVerilog comparison](../../target/editor-compare-qa/t2-native-compare.png)
- [Read-only SVN revision comparison](../../target/editor-compare-qa/t2-svn-compare.png)

The focused screenshots show the editor in the light theme. The runner rendered some CJK characters in fixture filenames as fallback boxes even though the test opened the paths successfully. Packaged font rendering and dark-theme captures were outside this focused run.

## What the checks exercise

- The File menu chooser opens an in-page comparison. The same pair reactivates its existing page. A regular tab opened later adopts the comparison's live buffer.
- Both editors render SystemVerilog syntax at once with diff backgrounds. A real editor scrollbar maps lines across a length-changing hunk. Native keyboard events type into a working buffer and dispatch the shell's Ctrl+Z and Ctrl+S actions. Historical text rejects typing and merging into the historical side.
- Hunk copying works in both directions and remains in memory until saved. Tests cover insert, delete, EOF and CRLF, read-only local files, external disk conflicts, invalid inputs, tab close and undo history. Real local SVN checks cover revision comparisons, absent files, and restoration to a working copy.
- Delayed-child cases exercise closing the tab, application teardown, and invalidation of an old result when the document changes before GNU diff finishes.

During development, the first xcb run exited 139 while the selftest tried to address the shell's custom confirmation buttons as standard QMessageBox buttons. The corrected test uses the named Discard control and the dialog's reject path for Cancel. The same xcb flow then completed. No crash stack was captured from the replaced executable.
