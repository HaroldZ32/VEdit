# vd_vcs_info / vd_vcs_diff: synchronous-call boundary (T95 note)

collected_at: 2026-11-01T06:07:33Z
git_head: 31a7c2d10111a9ab6078a433b1aa8289b31197a6

## what is async and what is not
- `vd_vcs_status`: starts a job (`vd_search`-style worker) and is read with `vd_vcs_poll` — non-blocking.
- `vd_vcs_info`: synchronous C function; calls `ProcessVcsService::info()` -> `SystemRunner::run()` -> one `git rev-parse`/`svn info` subprocess on the CALLING thread.
- `vd_vcs_diff`: synchronous C function; one `git diff`/`svn diff` subprocess on the calling thread.
- In the product the panel's `vcs_source.cpp` calls info() when the root is set and diff() when a file is selected, so those calls run on the GUI thread.

## impact
- AC-S2.5 (read-only view) is satisfied; the calls are read-only and short on a normal local checkout.
- C2 (never block the UI thread on a filesystem/subprocess) is NOT fully met for these two calls: a slow/network working tree can stall the GUI while git/svn answers.

## follow-up plan (not done in T95)
1. Add two job arms to `crates/vd-ffi/src/service.rs` (`JobValue::VcsInfo`, `JobValue::VcsDiff`) and export
   `vd_vcs_info_start/poll` + `vd_vcs_diff_start/poll` in `crates/vd-ffi/src/vcs.rs` (mirroring `vd_vcs_status`).
2. Change `FfiVcsSource` to start a job and let the panel poll with a QTimer (or a QThread worker as the
   search lane does); keep `VcsSource`'s interface unchanged so the stub/panel tests stay valid.
3. Extend the lane selftest with a 'does not block' timing check (the diagnostics/search lanes already have
   the pattern) and regenerate the header via cbindgen (build.rs already does it).
4. Estimated size: ~150 lines Rust + ~80 lines C++ + one test; no change to the read-only gate.
