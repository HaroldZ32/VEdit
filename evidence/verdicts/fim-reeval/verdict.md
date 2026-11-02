# Verdict — T107: re-review of T27 evidence after T105 repair

## Binding declaration

- Shared-tree HEAD at review: `90176858e5f8d5123c573826df8c0ccc7375c015`
  (T105 captured its evidence while HEAD was `d36bff9`; the lead commit landed mid-collection — that is
  exactly what F2 now documents).
- T27 code version reviewed (unchanged since my T28 review; see F0): `crates/vd-ffi/src/fim.rs =
  1ca08772b0079458affbb9a39d16eabe5b0aca6839106f4b5f160f3c0f09b4f8`; `src/editor/fim/{fim_backend.h,
  fim_backend.cpp,fim_inline_completion.h,fim_inline_completion.cpp,fim_selftest.cpp,CMakeLists.txt}` =
  `ee0224a1…, d3cbd1de…, 9ebcf91b…, 396f1c68…, 7693de25…, 1c208e28…`.
- Evidence under review: `evidence/slice3-fim/**` (main 11 logs + `repro/` 10 logs + `REPORT.md` +
  `backfill/`), repaired by T105.
- My audit artifacts: `evidence/verdicts/fim-reeval/audit.txt` (this file's companion).

## Verdict: PASS — **T27 证据可放行**

F1 (C9 headers) and F2 (binding coherence) both hold under independent re-checking: all 21 logs carry
the required fields, the original values are byte-for-byte the ones I recorded in T28, headerless logs
say `unknown` with a real mtime reference (no fabrication), the REPORT time is now coherent, and the
updated `run-evidence.sh` really writes C9 headers on every run (I executed it myself).
Two **non-blocking documentation errata** (N1/N2 below) should be corrected before T31 packaging;
neither changes any measured value or any binding conclusion.

## F0 — T105 did not change code (confirmed)

At T107 start (15:43) I compared sha256 of `crates/vd-ffi/src/fim.rs` and every `src/editor/fim/**` file
against the binding I recorded in the T28 review: **all identical**, `git status` shows no modification
of those paths, and `crates/vd-fim/**` is untouched. T105's writes are confined to `evidence/slice3-fim/**`.
Therefore the T28 functional conclusions remain valid and no functional re-run was needed.

*Note (not T105):* the shared tree then moved by **another lane** — `src/editor/fim/**` and
`src/editor/kernel/fim_wiring.*` were modified 15:44:42–15:47:58 (+526/−54, cf. the new `fim_wiring.*`
files in `git status`), making test #12 transiently red; after that lane's build it passes again (see
regression). This drift is outside T27/T105.

## F1 — C9 headers (PASS)

My own parser over all **21** logs (not `verify-headers.sh`): **bad=0**. Every log has
`collected_at` (real or `unknown`), `git_head` (real or `unknown`), a `git_status_porcelain` block,
a `backfill: 补记（T105）` note, and a `git_status_porcelain_sha256` that matches the block I recomputed
(21/21, 0 mismatches).

- **Original values preserved**: the 10 logs that had original headers keep exactly the values I recorded
  in T28: `01 15:12:51/4856960`, `02 15:13:21/4856960`, `03 15:23:14/d36bff9`, `04b 15:23:13/d36bff9`,
  `04 15:21:50/4856960`, `05 15:21:51/4856960`, `06b 15:21:34/4856960`, `06-lane 15:15:26/4856960`,
  `06-root 15:15:26/4856960`, `06-typecheck 15:21:34/4856960` — so the repair **added** porcelain/backfill
  fields without rewriting the collection facts.
- **Bodies untouched**: each original log grew by exactly **+2552 bytes** (the prepended header block;
  `00-run-evidence.log` +2525, its header differs slightly). The REPORT grew +7529 (documented additions:
  backfill header + the binding section); no original “collected_at 15:22:00” value survives as a claim.
- **`unknown` is honest, not lazy**: `00-run-evidence.log` cites mtime `15:19:50` — exactly the mtime I
  recorded at T28; `repro/*` cite `15:17:16–15:19:50`, consistent with “ran 15:16–15:19”. Neither invents
  a time or a HEAD; `git_head_at_backfill` is labelled as the backfill-time value.
- **Root fix verified by execution**: `run-evidence.sh` now defines `c9_header()` (collected_at, UTC,
  `git rev-parse HEAD`, `git status --porcelain`) and calls it before every log. I ran it with
  `VEDIT_FIM_EVIDENCE_DIR=target/t107-repro` (scratch; the writer's evidence untouched): all 10 generated
  logs contain **real auto-written headers** (`collected_at 15:43:47–15:45:00`, `git_head 9017685…`,
  porcelain present) and the same body results (FFI `20/0`, FIM `28/0` default-off, `40/0` configured,
  `FIM-RUST-REQUEST`, `SOCKETS=0/CONNECTS=0` default, `CONNECTS=1` configured). The script therefore
  really does prevent the “no header” defect from recurring.

## F2 — binding coherence (PASS, two errata)

1. **Per-log HEAD attribution matches the logs**: REPORT’s table lists HEAD A `4856960` for
   `01,02,06-lane,06-root,06b,06-typecheck,04,05` and HEAD B `d36bff9` for `03,04b`, with `00/repro`
   as `unknown` — exactly what my per-log audit found; it also states the commit landed between
   15:21:51 (last A record) and 15:23:13 (first B record), which is consistent with my T28 records.
2. **REPORT time is coherent**: `collected_at 2026-11-01T15:41:23+0800`, file mtime `15:41:23` (≤, equal);
   the old `15:22:00` value is explicitly declared void with the reason (it was later than the report’s
   own mtime). The REPORT’s `git_status_porcelain_sha256: 08b7a6f9…` matches the block (I recomputed it).
3. **Artifact sha256 table is reproducible** for the measured artifacts: current lane binary
   `c2d73e64…`, product `61e19acd…`, `libvd_ffi.a 2ad5b187…`, worktree `fim.rs 1ca08772…`, the six
   `src/editor/fim/**` hashes, `socket_probe.c 48bed864…`, and d36bff9’s committed `fim.rs 070c849c…`
   all match my recomputation. The “recorded at collection” values are also present in the logs:
   `lane_sha256: adef16c7…` in `04`/`05` (line 51) and `product_sha256: 63a27a53…` in `04b` (line 51),
   matching the table. The lane/product “current” hashes differ from the recorded ones only because later
   T104 builds overwrote the binaries — explicitly explained in the table.

**Errata (non-blocking, fix before packaging):**

| id | severity | finding | suggested fix |
| --- | --- | --- | --- |
| **N1** | LOW | REPORT line 92 lists `evidence/slice3-fim/run-evidence.sh = 86eec25a…`, but the final file is `78477e777255524ff673e88d08b370b3e792dca793478873be656fda5b5b39bc` (facts.txt snapshot 15:37 predates the 15:40 script edit; the table was not refreshed). | Replace with `78477e77…` (or say “sha256 at backfill 15:37: 86eec25a…, final: 78477e77…”). |
| **N2** | LOW | REPORT line 99 explains the `fim.rs` hash difference (`070c849c…` at d36bff9 vs `1ca08772…` measured) as “clippy 修复的格式差异”. Wrong: d36bff9’s committed file is the **pre-T27 stub** (0 × `settings_from_config`, 1 × `pub(crate) fn complete`); the difference is the T27 landing (+365/−14). Both hashes are correct. | Reword as “d36bff9 里是 T27 落地前的 stub；被测版本是 T27 工作树版（后由 98704dc 提交，sha 一致）”. |

## F3/F4 — boundaries preserved (PASS)

`REPORT.md` §5 (“未接线（明确声明）”) and the new §8 (“边界声明（F3/F4，评审要求保留）”) are present and
**stronger** than before: §8 states the product UI is unwired, that other lanes’ wiring files are not
validated by this evidence, and that `LD_PRELOAD` covers only dynamically resolved
`socket()/connect()/getaddrinfo()`, not raw syscalls, static linking or pcap-level traffic. Nothing was
weakened.

## Regression

- `bash build/gates/typecheck.sh` → **exit 0** (`[qt6] PASS` warnings-as-errors; `[typecheck] PASS`
  pinned image `vedit-build:local`).
- Root ctest (current baseline 25 tests) → **25/25 passed, CTEST_EXIT=0** (run after the concurrent lane’s
  build; #12 `vedit-fim-wiring-selftest` passes). At 15:45, during that lane’s in-flight edits, #12 was
  transiently red (`ok=0 checks=50 failures=16`, async poll/timeout checks) — not the known folding flake;
  it was green after the lane’s rebuild. Lane ctest `vedit-fim-selftest` 1/1 passed.

## Not verified / bounds

- Real model endpoint, product user flow (UI wiring is another task’s scope), kernel-level packet capture —
  unchanged bounds, declared in the evidence.
- The errata N1/N2 are textual; all verified values are in `audit.txt`.

## Release statement

**T27 证据可放行。** F1/F2 are satisfied under independent re-checking (21/21 headers, original values
preserved, `unknown` honest, root fix proven by running the script, binding coherent, artifact hashes
reproducible), the T105 repair touched no code, and the declared gates are green. Only the two
documentation errata above should be corrected before the final T31 packaging.
