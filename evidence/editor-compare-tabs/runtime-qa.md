# Independent Qt6 runtime verification

**PASS for product commit `304e1a29be4dbe2372daf81206a011a8a6b00675`.** The SHA-256 of the tested `vedit-app` is `1f80fe4c6fa79399cabde0b12e72b5959a3fd6a76f74d4221aadfea313f8424d`. The source manifest matched before and after the final build. Every one of its 22 files, including the embedded user guide, then matched the corresponding bytes in the product commit. [Source manifest](product-source.sha256) · [Build receipt](runtime-build.log)

| Check | Observed result | Receipt |
| --- | --- | --- |
| Pinned product build | The official Qt build stage exited 0. The build tree started empty and the final repairs rebuilt and relinked 98 targets. Qt 6.10.2, KF6 and KTextEditor 6.24.0, tree-sitter 0.25.9. The Rust library was built on the pinned Rust 1.99.0 runner and its hash remained stable. | [Build receipt](runtime-build.log) |
| Full CTest registry | 100 registered and 100 passed, zero skips. The native Git/SVN/HDL group passed 99/99 in 167.25 s. The diagnostics stub fixture passed 1/1 in 30.18 s with its own tool discovery. Both commands exited 0. | [Full suite receipt](runtime-full-ctest.log) |
| Related CTests | 24/24 passed in 28.08 s, exit 0. Includes file comparison, dirty and active-diff close/exit, stale child results, tabs, split and drag, editor shortcut and layout regressions, real SVN workbench and workspace tests. | [Related tests](runtime-ctest.log) |
| Real X11 checks | Six commands exited 0 under Xvfb with xcb and software rendering. Qt smoke, native comparison, Chinese comparison dialogs, 65 editor-area checks, and SVN workbench/history in English and Chinese. | [Native test output](runtime-xcb.log) |

## Acceptance observations

- File → Compare Files and Files → Compare With opened two KTextEditor views inside the active editor pane’s tab row. A separate recording frame shows a normal editor in the upper split and the comparison in the active lower split. HDL syntax attributes coexist with line and changed-character backgrounds. Unicode spans and unpaired hunk cues passed their named checks. [Native compare checks](runtime-xcb.log)
- Alt+PageDown and Alt+PageUp moved between the two differences in the actual desktop app. Ordinary-tab key handling stayed on its normal path. Native checks also covered synchronized scrolling, merge in both directions, typing, Undo and Save. The manual replay compared exact disk bytes after Discard and Save Right, then verified a typed marker on disk after Ctrl+S. [Native compare checks](runtime-xcb.log) · [X11 input and disk checks](runtime-gestures.log)
- Save As kept the original comparison document separate from the new destination. Invalid or vanished targets and external edits were guarded, including after a comparison closed. The FIFO case returned before its watchdog and preserved the source and FIFO. The large mixed-ending undo restored the expected bytes while retaining a separate dirty edit. [Native compare checks](runtime-xcb.log) · [Before and after save regression](runtime-save-regression.log)
- The native tab test measured `single=160 first=160 last=160`. With 20 tabs open in the real app, wheel and scrollbar thumb movement kept `compare_left.sv` active. Selecting the last file scrolled its tab into view. The tooltip showed the entire elided filename. [Native tab checks](runtime-xcb.log) · [X11 input and window titles](runtime-gestures.log)
- The SVN replay created a real repository and working copy with SVN 1.14.5. The File menu’s BASE comparison showed the committed revision read-only on the left and the unsaved editor text on the right. Disk bytes were unchanged, including after the successful cleanup replay. The workbench also checked revision pair and history actions, read-only snapshots, alternate checkout selection and stale-path rejection. [Native SVN checks](runtime-xcb.log) · [X11 input and SVN commands](runtime-gestures.log)
- Both history compare labels fit in English and Chinese. The measured English history width was 663 px, with 206 px available for 194 px of previous-revision text and 185 px for 172 px of working-copy text. Chinese resized from 784 px to 606 px, with 96 px available for each 84 px label. The requested 500 px width was below the persisted layout minimum. The Chinese renderer used the repository-pinned Noto Sans CJK SC font in the disposable container. [Label measurements](runtime-xcb.log)

## Local captures

The media lives under the worktree-local `target/editor-compare-qa/final-runtime-tdh2ILv5/`. The recordings are H.264 at 1440×900. [Media checksums](runtime-media.sha256)

| Surface | Files within the capture directory |
| --- | --- |
| Comparison and active split | `compare/compare.mp4` (92 frames), `compare/compare-open.png`, `compare/compare-next.png`, `compare/compare-previous.png`, `compare/split-compare.png` |
| Dirty close and disk writes | `compare/compare-dirty-close.png`, `compare/compare-close-cancel.png`, `compare/compare-close-discard.png`, `compare/compare-hunk-saved.png`, `compare/compare-direct-saved.png` |
| Tab overflow and file tree | `overflow/overflow.mp4` (62 frames), `overflow/overflow-tooltip.png`, `overflow/overflow-selection.png`, `overflow/file-tree-menu.png`, `overflow/file-tree-compare.png` |
| Unsaved SVN BASE | `svn-replay/svn-buffer-base.mp4` (56 frames), `svn-replay/svn-file-menu.png`, `svn-replay/svn-buffer-base.png` |
| Bilingual history and revision comparisons | `native/svn-en/editor-file-history.png`, `native/svn-en/editor-file-history-narrow.png`, `native/svn-zh/editor-file-history.png`, `native/svn-zh/editor-file-history-narrow.png`, `native/svn-en/editor-revision-pair.png` |

The commands use local fixtures and a network-disabled runner. In the text receipts, `$WORKTREE` and `$MAIN_CHECKOUT` stand for the two host mount roots. An early reused C++ cache caused a mixed-ABI test failure that disappeared in the newly configured build. The diagnostics fixture used a different stub environment because globally available real slang diagnostics differed from its expected message. The initial manual SVN cleanup timed out after the dirty quit prompt. The successful replay canceled the prompt, undid the fixture edit and exited 0.

The MPD verifier seats for the three implementation loops bound successfully. The gate tool is scoped to the separate team checkout, so this report uses the recorded build, CTest and desktop commands from the feature checkout as its verification evidence.
