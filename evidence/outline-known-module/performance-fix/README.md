# Outline parsing and session scope repair

Tested product `347143645c888aa836cc869855ec5afaac503b4f` in Release with the pinned Qt6 container, Slang 12.1.0+0 and slang-server 0.3.1+. The watcher runs one dependency computation at a time using snapshots of bounded buffer and catalog inputs. An open tab reuses its file list while its document revision, disk snapshot and project scope match. Invalidating a background tab's supplement refreshes the running session through the watcher debounce. The session reserves room for the active closure and excludes shadowed or nonmember tabs before adding their children. [Acceptance contract](CONTRACT.md) · [Build results](qt-build.log) · [Artifact hashes](build-hashes.log)

## Desktop timing

The byte-identical 458,791-byte and 1,811,791-byte files were selected four times each with real X11 input. The table compares the largest recorded GUI timer gap by file. The timer sampled at 10 ms and logged gaps from 35 ms. The preload probe also identified calls to the parser on the GUI thread and on workers. [Original measurements](../verification/ui-timing.log) · [Independent async repair repeat](../verification/84c9e0c1/README.md) · [Final build timing](ui-timing.log)

| Saved input | Dev `7202846` | Original candidate `6744f231` | Final `3471436` |
| --- | ---: | ---: | ---: |
| 12,003 lines | 84 ms | 365 ms | 105 ms |
| 45,003 lines | 53 ms | 1,305 ms | 62 ms |

The first catalog-backed analysis of the large tab moved from a 1,072 ms GUI parser call and 1,383 ms GUI gap on `6744f231` to a 1,203 ms worker parser call on `3471436`. No GUI gap reached the 35 ms threshold between catalog readiness and the first switch. The small tab used the worker once on its first selection. The remaining selections reused their computed file lists. [Final build timing](ui-timing.log)

An actual first open after catalog readiness used a Module Tree jump with only the small RTL file already open. Opening the 45,003-line file had a 1,399 ms GUI gap on dev and a 1,392 ms gap on the final build. The native editor still parsed the file on the GUI thread for 1,125 ms and 1,112 ms respectively. The final build then ran its Outline dependency parse on a worker for 1,134 ms, with no additional timer gap at or above 35 ms in that interval. These are synthetic fixtures on one Xvfb setup. The user's project and release package were outside this run. [Cold-open timing and navigation](ui-timing.log)

## Functional checks

- Final focused Qt checks passed 13 of 13, exit 0. The named-top sibling and Auto generate cases rendered a bad-port error for the known `alternate` and an unknown-module error for `genuinely_missing`. Dirty replacement, saved reopen, macro occurrences and overrides, top scope, filelist authority, session reuse and the analysis-size cap passed. The ABI smoke passed 20 of 20, exit 0. [Final checks](background-invalidation-green.log)
- The original watcher left stale dependencies from tabs that became empty or oversized. Before the background refresh follow-up, the same active observer kept the stale owner on background edit and close. The final test passed all four checks and verified the real Slang operand list. [Original watcher control](old-watcher-invalidation.log) · [No-refresh control](background-invalidation-red.log) · [Final checks](background-invalidation-green.log)
- The independent Qt boundary run on `84c9e0c1` verified exact 64-file selection and recognized parameterized, multi-instance and escaped types. The focused diagnostics unit gate on the final build passed the 64/128-file admission and shadowed rules. [Boundary checks](../boundaries/checks.log) · [Final checks](background-invalidation-green.log)
- Full CTest on `84c9e0c1`, before the final background-refresh change, recorded 96 passes, two SVN skips and two failures, exit 8. The annotation-tooltip check also fails on the dev control with Slang's different wording. Signal trace's live shortcut failed in the parallel run and passed in serial on both the final build and the dev control. The final follow-up ran the scoped suite and the signal-trace rerun. [Full CTest excerpt](qt-full.log) · [Dev tooltip control](../verification/README.md) · [Signal trace control](signal-trace-baseline.log) · [Final checks](background-invalidation-green.log)

## Reproduction

Use the offline Qt container and HDL tool environment in the [build recipe](../implementation/README.md). The Qt image digest is `sha256:809c8072ac65be85a15bfc010400f1a0b2cffa10966cfebd4e7dcd0b418360c1`.

```sh
# Inside the pinned Qt image
cmake --build target/outline-qa/qt --parallel 7
ctest --test-dir target/outline-qa/qt -V -R '^vedit-realtime-context-e2e$'
ctest --test-dir target/outline-qa/qt -j 2 --output-on-failure \
  -R '^vedit-(workspace-diagnostics-selftest|active-watcher-cap-selftest|realtime-depth-switch-e2e|filelist-authority(-[a-z]+)?-e2e|rm(2-project-switch|17b-top-scope)-e2e|module-closure-e2e|editor-open-bench)$'
target/outline-qa/qt/src/app/vedit-app --selfcheck-ffi crates/vd-ffi/tests/fixtures/project

# From the isolated checkout on the host, using fresh Xvfb and app state per run
bash target/outline-qa/reviewer-app/run-gui.sh perf
bash target/outline-qa/performance-fix/run-cold.sh perf target/outline-qa/baseline/qt/src/app/vedit-app
bash target/outline-qa/performance-fix/run-cold.sh perf
```

The cold driver reuses the switch fixture generator and timer probe. It omits `large_b.sv` from the startup operands and replaces the switch sequence with two clicks on the Module Tree's `large_b` row, spaced 70 ms apart plus input delivery. The scratch relink controls use the final test and archives with only the watcher object rebuilt from the respective older commit. Full raw logs are retained locally under `target/outline-qa/performance-fix/raw-evidence/`.
