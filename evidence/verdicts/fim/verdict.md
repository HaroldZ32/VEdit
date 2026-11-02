# Verdict — FIM (T28, independent review of T27)

## Binding declaration

- **HEAD**: `d36bff9470dca6bb94ee6c5529c7fb5328f344c9` (Lead commit landed during the evidence capture;
  T27's work is uncommitted in the shared tree).
- **Reviewed T27 change set / sha256**:
  `crates/vd-ffi/src/fim.rs 1ca08772b007…` (M),
  `src/editor/fim/fim_backend.{h,cpp} ee0224a1…/d3cbd1de…`,
  `fim_inline_completion.{h,cpp} 9ebcf91b…/396f1c68…`,
  `fim_selftest.cpp 7693de25…`, `src/editor/fim/CMakeLists.txt 1c208e28…`,
  `evidence/slice3-fim/**` (untracked).
- Other working-tree modifications (`crates/vd-ffi/src/service.rs`, `include/vd_ffi.h` = VCS/T99;
  `src/editor/CMakeLists.txt` = an in-flight editor-wiring task after 15:31) are **not** T27's
  (`crates/vd-fim/**` itself is the pre-existing T19 library and was not touched by T27).
- Shared tree is moving: `src/editor/CMakeLists.txt` was modified at 15:31 (another lane) and the
  build tree now registers a `vedit-fim-wiring-selftest` (#12) whose executable was not built yet;
  all T27 runs below were taken **before** that drift (the evidence's 23/23 root ctest and my own
  23/23 reproduce that state).
- Loop: bound `loop-20261031T095153-d2cacf` (T27 contract, writer Senior Engineer). Gate evidence:
  `ev-20261101T072855-533392` (`typecheck`, exit 0).

## Verdict: FAIL (evidence-only; the functional acceptance passes)

The default-off/zero-network proof, the configured Rust-service chain, and the
"no endpoint or key in the repository" requirement are independently reproduced and hold.
The evidence package itself violates the cell's binding C9 rule and is internally inconsistent
about which commit it binds: no evidence log carries the required
`git_status_porcelain`, one log has no C9 header at all, and the REPORT's commit/time do not match
the logs (the Lead's commit landed mid-collection without a binding note). Repair is evidence-only —
no FIM code change is requested.

## Documents / sources

- `.mpd/cellwork/cell-20261031T085543161Z/contracts/fim-service.md` (deliverable, write scopes,
  evidence expectation) and `rev-fim.md` (this review's deliverable).
- `.mpd/cellwork/cell-20261031T085543161Z/requirements.md` — **C9** (line 76: evidence head =
  capture time + `git rev-parse HEAD` + `git status --porcelain`), **AC-S3.2** (default off, zero
  network provable, configured works), and the FIM non-goal (no default endpoint/key).
- `AGENT.md` §4/§5 (declared gates, never weaken a gate).
- `crates/vd-config/src/model.rs:203-232` (`FimConfig` default), `crates/vd-fim/src/settings.rs:67-79`
  (`FimSettings::default` = disabled), `crates/vd-fim/src/lib.rs` ("no default endpoint, model or key").
- `evidence/slice3-fim/{REPORT.md,00-run-evidence.log,01…06b logs,run-evidence.sh,socket_probe.c}`.

## Evidence produced by the reviewer (commands + observed output)

| # | check | observed |
| --- | --- | --- |
| 1 | defaults in code: `crates/vd-config/src/model.rs:221-232`, `crates/vd-fim/src/settings.rs:67-79` | `FimConfig::default()` = `enabled=false, endpoint="", model="", api_key=None`; `FimSettings::default()` disabled, no endpoint/model/key. No default URL/key anywhere in the FIM production code (the only `http(s)://` matches are the scheme parser, test literals and error-redaction strings). |
| 2 | **zero network, lane**: `LD_PRELOAD=socket_probe.so vedit-fim-selftest --default-off-only` with isolated `HOME/XDG`, no `VEDIT_FIM_*` | `FIM-DEFAULT-OFF status="FIM 未启用" endpoint_requests=0 suggestion=0`; `FIM-SELFTEST ok=1 checks=28 failures=0`; probe **`SOCKETS=0 CONNECTS=0 GETADDRINFO=0`** |
| 3 | **zero network, product**: `LD_PRELOAD=… vedit-app --selfcheck-ffi crates/vd-ffi/tests/fixtures/project` (default config) | exit 0; `CHECK ffi.fim.disabled-by-default PASS`; `FFI-ABI-SELFTEST ok=1 checks=20 failures=0`; probe **`SOCKETS=0 CONNECTS=0 GETADDRINFO=0`** |
| 4 | **configured chain**: full lane selftest (in-process loopback fake endpoint, not a real model) under the same probe | exit 0; `FIM-RUST-REQUEST first_line="POST /v1/completions HTTP/1.1" user-agent: ureq/3.5.0 body_has_prompt=1 body_has_suffix=1 authorization_bearer_redacted=1`; `FIM-SELFTEST ok=1 checks=40 failures=0`; probe `SOCKETS=3 CONNECTS=1 GETADDRINFO=0` (2 loopback listeners + 1 Rust client; **positive control** that the probe sees traffic) |
| 5 | half-configured (`enabled` + endpoint, no model) | `FIM-HALF-CONFIGURED status="FIM 未配置模型" endpoint_requests=1` — no new connection (the request counter did not grow) |
| 6 | Rust tests | `cargo test -p vd-fim --test default_off` → **4 passed / 0 failed**; `cargo test -p vd-ffi fim::` → **5 passed / 0 failed** (incl. `default_config_is_disabled_and_sends_nothing`, `configured_loopback_request_reaches_the_rust_transport`, `unreachable_endpoint_is_an_explicit_error_without_the_key`, `mapping_carries_every_configured_value_without_leaking_the_key`) |
| 7 | no-FFI lane build | configure/build/run all 0; `FIM-BACKEND unavailable (no Rust service in this build)`, `SKIP fim.rust.*`; `FIM-SELFTEST ok=1 checks=25 failures=0` — explicit SKIP, no fake PASS |
| 8 | lane ctest / root ctest (before the in-flight wiring drift) | lane `1/1 Passed`; root **`100% tests passed, 0 tests failed out of 23`, `CTEST_EXIT=0`** |
| 9 | declared gate: `mpd_verify_evidence{gate:"typecheck", loop_id:"loop-20261031T095153-d2cacf"}` | **exit 0**, evidence id `ev-20261101T072855-533392` |
| 10 | keys/endpoints in the repository | no default endpoint/key in production code/config/docs found; key-like literals occur only in test fixtures and are obviously fake (`sk-secret-value`, `SECRET-LOOPBACK`, `TEST-SECRET-KEY`); the configured-chain log shows `authorization_bearer_redacted=1`, never a raw key |
| 11 | Qt side never performs HTTP | `grep -rniE 'http|ureq|curl|socket|QNetwork|ssl' src/editor/fim` → production `fim_backend.*` / `fim_inline_completion.*` contain no HTTP code (comments only); the loopback listener + `#include <sys/socket.h>` exist only in `fim_selftest.cpp`, the selftest executable (separate CMake target) |
| 12 | evidence C9 headers | 01–06b logs: `collected_at`+`git_head` present, **`git_status_porcelain` absent (0/11)**; `00-run-evidence.log` has **no C9 header at all**; `REPORT.md` has time+HEAD but no porcelain. HEAD values split: `4856960` (most files) vs `d36bff9` (03-lane-build, 04b-product) — the Lead commit landed mid-collection with no binding note. `REPORT.md` claims `collected_at 2026-11-01T15:22:00` but its mtime is **15:20:45**. |

## Findings

| id | severity | symptom | expected / repair | document source |
| --- | --- | --- | --- | --- |
| **F1** | **HIGH (C9 header)** | None of the 11 evidence logs carries the required `git status --porcelain`; `00-run-evidence.log` carries no C9 header at all (no capture time, no HEAD, no porcelain). This is the same defect class the cell already remediated (`fix-evidence-c9`). | Add the porcelain snapshot to every evidence log header (backfill-labelled if post-hoc, like the T93 precedent) and give `00-run-evidence.log` a full C9 header — or drop it from the evidence set and cite the individual logs. | requirements **C9** (line 76); `evidence/slice3-fim/0*.log`, `00-run-evidence.log` |
| **F2** | **MEDIUM (binding coherence)** | The evidence binds two different HEADs (`4856960` in REPORT/01/02/04/05/06*, `d36bff9` in 03/04b) with no note that the Lead commit landed mid-collection; REPORT says `collected_at 15:22:00` while its mtime is `15:20:45`, and 03/04b are later than the REPORT. A reader cannot tell which artifact was measured at which commit, and the REPORT's own timestamp is not coherent with its write time. | Either re-capture the four late logs on one frozen HEAD, or add a binding note (e.g. T93's `## backfill: … NOT the original collection time` style + `lane_sources`-style hashes) naming both HEADs and the file hashes measured; fix the REPORT's collection time to its real capture instant. | `evidence/slice3-fim/REPORT.md:3-4`, `03-lane-build.log:2`, `04b-product-default-off-sockets.log:2`, file mtimes |
| **F3** | INFO (bound, not T27's defect) | The product UI is **not** wired to the FIM client (REPORT §5 declares it; the kernel hook is a separate task). AC-S3.2's "configured usable" is proven at Rust-service + editor-lane level through the C ABI, not through a user action in the product. | Declared honestly; product-level wiring remains a follow-up task outside T27's write scope. | `evidence/slice3-fim/REPORT.md` §5; `src/editor/fim/CMakeLists.txt` comment |
| **F4** | INFO (bound) | The syscall probe is LD_PRELOAD interposition: it catches dynamically linked `socket`/`connect`/`getaddrinfo` calls (including library internals) but not raw-syscall/static paths or kernel-level traffic. | Accepted bound; the evidence says "not pcap". | `evidence/slice3-fim/socket_probe.c`; REPORT §2 |
| **F5** | INFO (external drift) | While reviewing, `src/editor/CMakeLists.txt` was modified by another in-flight lane (15:31) and the build tree registered `vedit-fim-wiring-selftest` (#12) whose executable was not built; current `ctest -N` shows 25 tests with that missing binary. This is **not** T27's change. | The wiring lane must build the target; a re-run of the root ctest after that lane settles is needed. My verified root run before the drift: 23/23. | `git status src/editor/CMakeLists.txt` (M, 15:31); `src/editor/CMakeLists.txt:146-156` |

## Functional acceptance (verified — the reason the findings are evidence-only)

- **AC-S3.2 default off / zero network**: product default disabled by code defaults; both the product
  and the lane prove syscall-level zero network (`SOCKETS=0/CONNECTS=0/GETADDRINFO=0`), with a
  positive control that the probe detects the configured request (`CONNECTS=1`).
- **Configured path via Rust**: the editor lane only splits text and calls the C ABI; the request is
  made by `vd_fim` (ureq) — the loopback fake endpoint receives `POST /v1/completions` with
  prompt/suffix and a redacted `Authorization`; half-config produces no connection; unreachable
  endpoint is an explicit error without the key.
- **No endpoint/key in the repository**: no default endpoint/model/key in production code; only fake
  test values; keys are redacted in logs/status.
- **Gates**: Rust tests 4/4 + 5/5, lane ctest 1/1, root ctest 23/23, `typecheck` exit 0
  (`ev-20261101T072855-533392`); no test/gate was modified by T27.

## Not verified / bounds

- Real model endpoint availability is explicitly not a gate (the fake loopback endpoint is used).
- Product-level user flow (key binding, ghost text, UI): not wired in this task; unverified by design.
- Root ctest on the *current* shared tree: external in-flight wiring changes (F5) prevent a clean
  re-run right now; the T27-state run is 23/23 (mine and the evidence's).
- The probe caveat in F4.
