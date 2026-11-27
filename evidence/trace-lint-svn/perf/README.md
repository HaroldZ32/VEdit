# Large-file open, editor input and realtime diagnostics

The cold-open probe exercised the native Qt6/KTextEditor tab in Xvfb with the xcb platform plugin. It opened the same generated 46,126-line SystemVerilog file on each run and posted a genuine Qt key event to its editor input widget immediately after `openFile()` returned. A 10 ms precise timer measured the longest event-loop gap until the breadcrumb arrived. This measures the editor's own event delivery. The key event was injected at the Qt layer.

| Three serial trials per variant | Baseline | Background editor analysis |
| --- | ---: | ---: |
| Synchronous `openFile()` duration | 19–21 ms | 19–25 ms |
| First queued key to visible document change | 226–255 ms | 14–22 ms |
| Largest GUI timer gap | 198–255 ms | 19–32 ms |
| First queued key to breadcrumb | 226–255 ms | 543–922 ms |
| Seven 10,240-line opens, p95 range | 10–16 ms | 11–14 ms |

All six runs exited 0. The baseline benchmark was relinked with the `752c8ab8e2e22528f4e863da8318d81e177203e8` versions of `fold_breadcrumb.cpp`, `signature_help.cpp` and `ktexteditor_provider.cpp` and the same benchmark object, compiler flags, and dependency libraries as the candidate. This retained the old editor object's layout in the baseline provider. The published pin was `vedit-qt6-build:local` image `809c8072ac65`, Qt 6.10.2 and KF6 6.24.0, with an isolated XDG state directory and software rendering. [Raw comparison](native-comparison.log)

```sh
QT_QPA_PLATFORM=xcb QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1 \
  VEDIT_NO_ENV_AUTOCAPTURE=1 xvfb-run -a \
  target/qt6-build/src/app/vedit-editor-open-bench
```

Large breadcrumb and signature computations share one background analysis slot. A busy slot admits no queue of text copies. The active diagnostics watcher separately captures one bounded document revision, then reads and compares saved bytes and resolves dependency context in one worker pass. The watcher validates the active document, revision and project scope before giving the snapshot to LSP. The editor invalidates older results when text changes. The LSP session's worker owns subprocess messaging, and one diagnostics batch at a time can queue for the active analysis. The default project-open path continues to use the asynchronous project service. Full-project lint stays behind its command.

The Xvfb realtime stress run used a second 45,003-line synthetic file and actual Slang 0.3.1+ behind a wrapper that waited two seconds before startup. The key arrived immediately after opening, then a four-event burst extended the unsaved identifier during startup. The final diagnostic contained `missing_first_a_b_c_latest`. Exactly one analysis reached the controller after the burst. The probe observed zero earlier identifiers, checked that another edit immediately cleared the diagnostic, closed the dirty tab while switching files, and verified the new tab's own diagnostic. The original disk bytes stayed intact. The project lint invocation count stayed at zero. [Realtime stress output](realtime-stress.log)

| Delayed-server stress measurement | Observed |
| --- | ---: |
| Synchronous open | 37 ms |
| Slowest key dispatch | 22 ms |
| Largest GUI timer gap | 56 ms |
| Final key to matching diagnostic | 2,914 ms |

For reproduction, point `VEDIT_SLANG_SERVER` at a wrapper containing `sleep 2` followed by `exec /absolute/path/to/slang-server "$@"`. Set `VEDIT_SLANG` to the repository-pinned Slang CLI and use the isolated XDG paths from the Qt test setup.

```sh
QT_QPA_PLATFORM=xcb QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1 \
  VEDIT_NO_ENV_AUTOCAPTURE=1 xvfb-run -a \
  target/qt6-build/src/app/workspace-lane-diagnostics/vedit-realtime-context-e2e --stress
```

The two pinned servers used unversioned `publishDiagnostics` in the direct protocol probe, even with the client requesting version support. For those servers the client rejects snapshots received before the current buffer update, and Slang uses the ordered compile acknowledgment after queuing the in-memory change. A server that sends versions must match the current document version. The direct probe covers the observed notification order of the pins. An arbitrary unversioned server can send a delayed earlier publication that has no protocol identifier tying it to an edit. [Wire version observations](wire-versions.log) · [Large Slang wire observations](wire-large-slang.log)

The independent editor and diagnostics checks passed. The editor run covered open timing and signature help, including its debounce and cursor cache. The final diagnostics run covered the watcher cap, the workspace controller, real one-level scope and depth switching, and the project context with its settings and manual lint behavior. [Focused checks and artifact hashes](focused-checks.log)

```sh
ctest --test-dir target/qt6-build -V -R \
  '^vedit-(editor-open-bench|signature-help-selftest|workspace-diagnostics-selftest|active-watcher-cap-selftest|realtime-depth-switch-e2e|realtime-context-e2e)$'
```

The fixtures use local storage, one window and small project metadata. Editor document loading and native highlighting remain on the GUI thread, as does obtaining a bounded snapshot of a live editor buffer. The measurements describe this fixture and the included tool builds.
