# Independent Outline verification

Tested `6744f231d258b89daf9edf47d18112b262dbe6f8` against `72028461bafb9598ec9322e709b94572311dea46` in `target/outline-context-pr`. The MPD verifier seat bound to `loop-20261127T021037-a20167`. The reviewed worktree was clean at the start. The patched app and test binaries matched the implementation evidence SHA-256 values, and all runtime commands used the pinned offline Qt6 build image. See [the runtime and test recipe](../implementation/README.md).

## Black-box checkpoint

The scoped-top sibling and Auto generate controls **passed** in the rendered Qt6 Outline in both the focused offscreen CTest and the xcb/Xvfb selftest. The only unknown module in the two patched cases was `genuinely_missing`. Slang resolved `alternate`, proved by the deliberate bad-port error, and the realtime input lists contained its imported owner `lib/alternate.sv`. The baseline xcb control reproduced unknown `alternate` alongside unknown `genuinely_missing` in both cases. The first complete xcb selftest exited 1 after the separate macro-override native-fallback check timed out with empty diagnostic rows. The direct Xvfb repeat exited 0 with both Outline controls and the native fallback passing. The exact-head C ABI smoke exited 0 with `FFI-ABI-SELFTEST ok=1 checks=20 failures=0`. [Qt logs](qt-checks.log)

Full exact-head CTest exited 8 with 97 passes, two SVN skips and one failure. The annotation tooltip test expected `syntax error`, while the selected Slang backend returned `expected ';'` and `expected a declaration name`. The independent baseline seam rerun failed the same tooltip check. The patched tests exercised `vedit-realtime-context-e2e`, depth switching, filelist authority and included-header navigation within the full run. [Qt logs](qt-checks.log)

I launched the built `vedit-app` separately, clicked **Import** in the `.f` prompt, then checked the project panel, Module Tree and Outline for each fixture. The three-file project saved `scoped_top`, with `alternate` under `scoped_bridge`. The four-file project left Top on Auto, with `original` under `variant` and `alternate` reachable elsewhere in the tree. In both cases the saved HDL tab showed the deliberate bad-port error and the genuine missing-module error without an unknown `alternate` row. The app status and debug output reported an effective Slang backend, and `docker top` showed a live `slang-server` child. Both app runs exited 0. The first captures were taken while analysis was still starting. In these runs the rows appeared before switching away and remained correct after switching back. The implementation's prior standalone capture needed a tab switch to publish the rows. [Auto screenshot](auto-outline-x11.png) · [Scoped screenshot](scoped-outline-x11.png) · [App log](app-x11.log)

## High finding and verdict

**Request changes.** The functional fix passes, but `src/workspace/diagnostics/diagnostics_integration.cpp` `ActiveFileWatcher::updateBufferClosure` now calls `literalModuleReferences` from `ActiveFileWatcher::analyze` on every analysis of a clean file with catalog owners. `src/workspace/diagnostics/realtime_module_references.cpp` `literalModuleReferences` allocates a parser and builds a fresh tree on the GUI thread. The saved buffer and catalog were stable during the switches below. Each switch reparsed the incoming file while the event loop was stalled. Move the parse off the GUI thread and reuse its result for the same document revision. Apply results against current document and project scope revisions so a late result cannot change the wrong tab's analysis.

The comparison used byte-identical generated SystemVerilog in two already-open tabs. Both files contain ordinary continuous assignments and one direct child, and both are below the 4 MiB analysis cap. Each version received four X11 pane selections per file in one serial run. A 10 ms Qt timer measured GUI event-loop gaps. A preload probe independently timed `ts_parser_parse_string` and identified calls on the main thread. These are the largest gaps in each of the four switch windows, with the range across the four windows shown below. [Timing log](ui-timing.log)

| Saved input | Dev GUI gap | Patched GUI gap | Patched main-thread parse |
| --- | ---: | ---: | ---: |
| 12,003 lines, 458,791 bytes | 43–84 ms | 351–365 ms | 267–273 ms |
| 45,003 lines, 1,811,791 bytes | 40–53 ms | 1,292–1,305 ms | 999–1,017 ms |

The first analysis after the catalog arrived caused a separate 1,383 ms patched event-loop gap, including a 1,072 ms parse of the 45,003-line file. The comparable dev window had no recorded gap at or above the probe's 35 ms logging threshold. Both versions also incurred about 1.75 seconds of GUI work before the catalog arrived during initial multi-file startup. [Timing log](ui-timing.log)

## Commands, provenance and review bounds

All work ran from the isolated checkout. I reused the hash-matched Release binaries and static archive from `target/outline-qa`; I did not do a clean full rebuild. I verified the two baseline production source copies against `git show 7202846:<path>` and relinked the baseline app under the pinned image with these files bind-mounted at the two source paths. CMake reported a copied-cache path warning, but the command exited 0 and produced a distinct baseline app at the intended path. The baseline xcb regression executable matched its documented hash and independently reproduced the two failures. [Provenance](provenance.log)

```sh
# Inside the network-disabled pinned Qt6 container, with the documented HDL tools
ctest --test-dir target/outline-qa/qt -V -R '^vedit-realtime-context-e2e$'
ctest --test-dir target/outline-qa/qt --output-on-failure -j 2
QT_QPA_PLATFORM=xcb xvfb-run -a -s '-screen 0 1500x1080x24 -nolisten tcp' \
  target/outline-qa/qt/src/app/workspace-lane-diagnostics/vedit-realtime-context-e2e
QT_QPA_PLATFORM=offscreen target/outline-qa/qt/src/app/vedit-app \
  --selfcheck-ffi crates/vd-ffi/tests/fixtures/project
# The xcb repeat used Xvfb -displayfd and the same regression executable.

# From the isolated checkout, using the disposable X11 driver
bash target/outline-qa/reviewer-app/run-gui.sh auto
bash target/outline-qa/reviewer-app/run-gui.sh scoped
bash target/outline-qa/reviewer-app/run-gui.sh perf target/outline-qa/baseline/qt/src/app/vedit-app
bash target/outline-qa/reviewer-app/run-gui.sh perf
```

The timings apply to this pinned Qt6/Xvfb environment and these generated fixtures. Review covered the focused test, the two changed production files and the relevant filelist, catalog and analysis scope boundaries. I used synthetic projects. The user's actual project, a release package, other desktop platforms and files near the analysis limit were outside the runs. No MPD verdict was recorded. The built-in gate's workspace root resolves to the separate checkout, and there is no custom `mpd-gates.json` entry for the isolated one.

### Declined to judge

- Slang annotation wording as a patch defect. The exact-head and independent baseline seams failed the same tooltip assertion.
- The one xcb native-fallback timeout as a patch defect. The direct repeat and offscreen CTest passed it. The intervening `xvfb-run` setup attempt stalled before launching the test and was stopped.
- Initial Outline publication as a new defect. Both standalone controls populated after waiting in this run. The earlier capture records a startup sequence that needed a tab switch.
- Unobserved macro, inactive-branch and cap interactions. The run covered the supplied mixed literal/macro, one-level and filelist authority checks. It did not exhaust lexical patterns or cap collisions.
