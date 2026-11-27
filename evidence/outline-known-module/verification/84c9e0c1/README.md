# Independent verification of the async Outline repair

Tested product commit `84c9e0c1b5f4fe0a7db84d5e7b7c6986852e2a59` in `target/outline-context-pr`. Before the commit was made, I hashed its diagnostics diff against `1d6dfe80bbc4278f693ab26d7e5615120fcf721d`. The committed diff reproduced the same SHA-256, `2886f5fb8b13e33d1777db238fafa3d04caa44f47af676acd59178520ea07ef7`, and tracked product code and tests were clean at the final SHA. The app and focused-test hashes matched before and after their runs. The earlier `6744f231` performance finding remains in [the original review](../README.md). This report covers the acceptance criteria in [the repair contract](../../performance-fix/CONTRACT.md) that I ran directly.

## Saved-file timing

The primary performance finding is resolved in this run. I used the unchanged X11 input driver and timer probe, the same byte-identical fixtures and the pinned Qt6 container. Both saved panes were already open. The driver selected each four times, in alternation, after the three-file catalog was ready. The table shows the largest logged Qt timer gap per switch and the range across each file's four switches. [Repaired timing log](ui-timing.log) · [Dev and original candidate timings](../ui-timing.log)

| Saved input | Dev `7202846` | Original `6744f231` | Repaired `84c9e0c1` |
| --- | ---: | ---: | ---: |
| 12,003 lines, 458,791 bytes | 43–84 ms | 351–365 ms | 42–78 ms |
| 45,003 lines, 1,811,791 bytes | 40–53 ms | 1,292–1,305 ms | 42–59 ms |

The first catalog-backed parse of the large file took 1,163 ms on a worker. The timer logged no GUI gap of 35 ms or more from catalog readiness to the first click. The first switch to the small file produced one 273 ms worker parse. The remaining switches reused the results and issued realtime requests for the correct active file. The pre-catalog startup path still contained GUI-thread parsing and a 2,149 ms gap during multi-file opening in this run. Dev also showed pre-catalog GUI parsing and a 1,748 ms gap in the earlier sample. This is one instrumented Xvfb run per revision, so the startup delta alone does not establish a regression. [Repaired timing log](ui-timing.log) · [Dev and original candidate timings](../ui-timing.log)

## Functional controls

Both standalone xcb app runs exited 0. I imported each `.f` through the prompt, explicitly selected and persisted `scoped_top` for the sibling case, and verified a live `slang-server` child for both the named-top and Auto projects. After switching focus out and back, both saved HDL files rendered the bad-port and `genuinely_missing` errors. The Module Tree contained `alternate` in both cases, and Outline had no unknown-module error for it. The focused xcb test also exited 0 and asserted the exact Slang source lists. It passed dirty replacement and correction, saved reopen, macro ownership and override, and the empty, oversized and close fixture sequence. [Scoped Outline](scoped-outline-x11.png) · [Auto Outline](auto-outline-x11.png) · [App log](app-x11.log) · [Focused xcb log](focused-x11.log)

## Review bounds

`ActiveFileWatcher::prepareBufferClosure` captures strings and catalog data by value for the worker. `ActiveFileWatcher::finishBufferClosure` checks the scope generation, live document, path, revision and disk metadata before caching a result. The focused close fixture changes tabs during the sequence. It does not prove that closing or editing a background tab updates the already running Slang session while the active tab remains fixed. That separate case was under investigation when this evidence was recorded. No new full CTest was run in this lane. The writer's 13 focused checks are in [the Qt log](../../performance-fix/qt-final.log). The writer owns the distinct cold-open-after-catalog run. The actual reported project and distributable packages remain outside this verification.

## Reproduction and identity

```sh
# From the isolated checkout. The driver uses a fresh Xvfb and state each time.
bash target/outline-qa/reviewer-app/run-gui.sh perf
bash target/outline-qa/reviewer-app/run-gui.sh auto
bash target/outline-qa/reviewer-app/run-gui.sh scoped
# Inside pinned xcb/Xvfb with the documented HDL tool environment.
target/outline-qa/qt/src/app/workspace-lane-diagnostics/vedit-realtime-context-e2e
```

| Item | SHA-256 |
| --- | --- |
| Repaired `vedit-app` | `0622c0c8bcedff8548aadb556233f590493d1bf197e6debb5880d41f88a631a0` |
| Repaired `vedit-realtime-context-e2e` | `1b4fd5a57e77f3f54f7bfcb6ac27bf98f72258054e4a17cbda1064add2b53b92` |
| Pinned Qt6 image | `809c8072ac65be85a15bfc010400f1a0b2cffa10966cfebd4e7dcd0b418360c1` |
| Timing probe | `273c20d9fb0e618111bab5240fab428b33dfbeba5b6eb6bbf61bb70729cbb3be` |

Runtime evidence comes directly from the isolated checkout. The workspace-level MPD gate does not certify these binaries.
