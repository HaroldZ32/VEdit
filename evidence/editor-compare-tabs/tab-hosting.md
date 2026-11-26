# Fixed editor tabs and auxiliary page hosting

The Qt6 worktree build started from `78093bf6d584533a17e38258b2139fa3bffe38fb` with the feature edits applied. It used Qt 6.10.2 and KF6 6.24.0 in the pinned runner. See [runner identity](baseline.md) for the image digest.

The tab strip keeps the existing native tab controls in a horizontally clipped viewport. Each tab occupies 160 logical pixels. The horizontal scrollbar appears on overflow. The scrollbar and wheel change the visible range while preserving the selected file. Selecting or opening a tab reveals it. The full pathname remains available on hover.

Auxiliary pages keep their identity, widget and callbacks across pane moves. They participate in Save, Save All and the unsaved-changes guard. The shell leaves them out of the ordinary file path and session archive. Closing a shared comparison can preserve the dirty buffer in another live view. Save As remains available for ordinary documents.

## Checks

| Run | Observed result |
| --- | --- |
| `bash target/editor-compare-qa/run-qt.sh ctest --test-dir target/qt6-build --verbose -R '^vedit-(editor-area-selftest\|editor-tab-drag-tests\|editor-pane-activation-tests\|app-reset-layout-guard-selftest\|app-batch-layout-selftest)$'` | Five CTest targets passed. Editor-area selftest passed 65 checks and tab-drag tests passed 103 checks. The single, first and last tabs each measured 160 pixels. The scrollbar maximum was 1455. Dragging its thumb moved the value to 679 without switching files. A drop used visible tab index 10 and landed at index 10. [Focused log](../../target/editor-compare-qa/tabs-worker-focused.log) |
| `bash target/editor-compare-qa/run-qt.sh env QT_QPA_PLATFORM=xcb QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1 timeout 120 xvfb-run -a -s '-screen 0 1440x900x24 -nolisten tcp' target/qt6-build/src/app/vedit-editor-area-selftest` | Independent native X11 run passed the same 65 checks. [X11 log](../../target/editor-compare-qa/current-xcb-wQsDgsZ8/editor-area.log) |

The test uses Qt-delivered pointer, wheel and drag events. It checks the widget geometry, overflow behavior and selection, active tab reopening, split and move semantics, provider isolation, session exclusion, undo dispatch, save and save failure, and close veto and cancellation.
