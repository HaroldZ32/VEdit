# Independent verification of the final Outline repair

Product commit `347143645c888aa836cc869855ec5afaac503b4f` in `target/outline-context-pr`. The scoped xcb and timing runs started from the product SHA. The app SHA-256 was `4db2d731ae6cd51dc4189537288794ff2413e07c9bcf697968841a625cfd1c7c`, the focused test SHA-256 was `3a848e3f7e94b81a27392778df1d493429104386285fc339ed0ccef76e4bb003`, and both matched before and after the runs. The pinned network-disabled image was `sha256:809c8072ac65be85a15bfc010400f1a0b2cffa10966cfebd4e7dcd0b418360c1`. The original review and async-repair checkpoint remain at [6744f231](../README.md) and [84c9e0c1](../84c9e0c1/README.md).

## Focused X11 checks

`QT_QPA_PLATFORM=xcb` with a dedicated `Xvfb -displayfd` server ran `target/outline-qa/qt/src/app/workspace-lane-diagnostics/vedit-realtime-context-e2e`. It exited 0. The rendered scoped sibling and Auto generate Outline controls both passed. They included `lib/alternate.sv` in Slang's exact inputs, reported the deliberate bad port, retained `unknown module 'genuinely_missing'` and showed no unknown-module row for `alternate`. The run also passed dirty replacement and correction, saved reopen, the opaque macro case and project macro controls. [Focused xcb log](focused-x11.log)

`INFERRED-SCOPE-INVALIDATION empty=1 oversized=1 inactive-edit=1 inactive-close=1`. The test confirmed `lib/alternate.sv` in the running session while `original.sv` was active, edited or closed the background tab, then confirmed the owner disappeared with `original.sv` still active. `ActiveFileWatcher::noteDocumentEdited` and the document destruction handler in `src/workspace/diagnostics/diagnostics_integration.cpp` now schedule a debounced scope refresh when they evict a cached closure. I found no remaining blocking issue in the reviewed path. [Focused xcb log](focused-x11.log)

## Saved-file response time

The final standalone `vedit-app` was driven through four real X11 pane selections per file, with byte-identical saved fixtures and a live `slang-server` process. The command was `bash target/outline-qa/reviewer-app/run-final-verifier-gui.sh perf`, a copy of the prior driver with a guard for the final quit fallback. The first run completed all eight switches and encountered a driver race when its fallback key targeted an already-exiting container. The guarded repeat returned `APP_EXIT=0`. [Final timing log](ui-timing.log)

| Saved input | Dev `7202846` | Original `6744f231` | Final `3471436`, clean repeat |
| --- | ---: | ---: | ---: |
| 12,003 lines, 458,791 bytes | 43–84 ms | 351–365 ms | 43–81 ms |
| 45,003 lines, 1,811,791 bytes | 40–53 ms | 1,292–1,305 ms | 42–61 ms |

The first catalog-backed parse of the large file took 1,172 ms on a worker and produced no Qt timer gap at or above the 35 ms logging threshold before the first click. The first switch to the smaller file produced a single 279 ms worker parse. No subsequent switch triggered another parse of either saved file. The probe still recorded GUI-thread syntax parsing during pre-catalog three-file startup. That existing startup path was not the switch regression. The writer separately ran the cold-open-after-catalog control. [Baseline timing log](../ui-timing.log) · [Writer timing log](../../performance-fix/ui-timing.log)

## Full pinned CTest

I ran `ctest --test-dir target/outline-qa/qt --output-on-failure -j 2` with `QT_QPA_PLATFORM=offscreen` and the same pinned image and HDL tools. The checkout had advanced to `9a3d831d63d12eb1307e175b3a41fe04a259727b`, which adds evidence only. Before and after the run I checked the exact tracked tree outside `evidence/` against `3471436`, the working-tree diff and the two binary hashes. [Full CTest excerpt](full-ctest.log)

CTest exited 8 in 182.76 seconds with 97 passed, two SVN skips and one failure. The sole failure was `vedit-diagnostics-product-seam` at `seam.annotation-tooltip-has-syntax`. Slang emitted `expected ';'` and `expected a declaration name`. The dev control fails the same wording assertion. `vedit-signal-trace-selftest` passed in this parallel run, as did the focused Outline, depth-switch and filelist-authority checks. [Full CTest excerpt](full-ctest.log) · [Dev wording control](../qt-checks.log)

## Verdict and bounds

**Approve the reviewed fix.** The final focused xcb run, scope invalidation and large saved-file timing controls passed. The full suite remains red on the reproduced baseline wording assertion.

The exact Qt6 binaries were reused from the pinned build. Review covered the final async invalidation changes and connected watcher and test paths. The actual user's HDL project and packaged deployment were outside this run. The workspace-level MPD gate targets the separate checkout, so this report uses direct isolated-worktree evidence.

### Declined to judge

- The annotation tooltip wording failure as a patch defect. The independent dev control reproduced the same assertion failure.
- The prior parallel signal-trace shortcut failure as a defect in this final revision. The exact final parallel suite passed that test.
- Pre-catalog startup parsing as part of the repaired switch regression. The earlier dev run already showed synchronous startup parsing, and the writer's separate cold-open comparison found comparable timings.
