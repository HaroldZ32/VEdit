# T66 — folding evidence corrected: measurements now on the real F2 library

C9 header

- collected_at_utc: **2026-10-31T11:29:00Z** (after) / **2026-10-31T11:29:12Z** (before) /
  **2026-10-31T11:32:14Z** (negative control) — exact headers are inside each log.
- git_rev_parse_HEAD: `95c496e682e3841add1a081edd78d29c34957aac`
- git_status_porcelain (this task's paths at recording time):
  - `M resources/syntax/vedit-systemverilog.xml` — T45's fix, **not touched by T66**
  - `M resources/syntax/vedit-verilog.xml` — T45's fix, **not touched by T66**
  - `?? evidence/syn-kde-data/folding-probe/` — the corrected runner + probe + `before/` XML

## Defects (both measured, not inferred)

1. **The first logs measured the DISTRO library under a `library=F2` label.**
   The old runner computed `F2_DIR=$(dirname $(dirname "$LIB"))`, i.e.
   `<prefix>/lib`, so `-I<prefix>/lib/include` and `-L<prefix>/lib/lib` did not exist;
   g++/ld silently used `/usr/include/KF6` + `/usr/lib/x86_64-linux-gnu`. Negative
   control (`14-ldd-guard-negative-control.log`) with the **same old flags**:
   `old_flags_ldd_resolves=/usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6`
   (DISTRO). The real F2 layout is `<prefix>/lib/x86_64-linux-gnu` (library) and
   `<prefix>/include/KF6/KSyntaxHighlighting` (headers).
2. **The SystemVerilog sample branch never ran.** `probe.cpp` selected the sample with
   `definitionName == "SystemVerilog"`, but the runner passes `VEditProbeSystemVerilog`,
   so both runs used the 18-line Verilog sample (the first `11-folding-after.log` shows
   `text-lines=18` twice). The delivered "SV 7/7" therefore did not cover
   `interface`/`program`/`class`/`package`.

## Fixes (in the lane's evidence scripts)

`evidence/syn-kde-data/folding-probe/run.sh`

- Derives the F2 prefix from the installed library: `LIBDIR=<prefix>/lib/x86_64-linux-gnu`,
  `PREFIX=<prefix>`, `INCDIR=<prefix>/include/KF6/KSyntaxHighlighting`; asserts the
  F2 repository header and `ksyntaxhighlighting_version.h` exist and that the prefix is
  `target/ksh-selfbuild/F2/install`.
- **ldd guard**: after compiling, resolves `libKF6SyntaxHighlighting` with `ldd` and
  asserts it starts with the F2 `LIBDIR`; otherwise `exit 1`. The log prints
  `# ldd guard: …` and a `LDD library=F2 resolves=<path>` line immediately before every
  result block, so no `library=F2` result line exists without an ldd proof.
- Compiles with **only** the F2 include dir (the system `-I/usr/include/KF6/...` is gone),
  so a wrong include path cannot silently compile against distro headers.
- Supports `before|after`: `after` measures `resources/syntax/*.xml`; `before` measures
  `folding-probe/before/*.xml` (the pre-T45 XML extracted from `git HEAD`, sha256 printed
  in the log).
- The sample language is an explicit third argument (`verilog|systemverilog`); the runner
  passes it for both probes.

`evidence/syn-kde-data/folding-probe/probe.cpp`

- Replaced the definition-name special case with a typed `SampleLanguage` argument parser;
  `SAMPLE` now prints `language=<verilog|systemverilog>` and the SV run uses the 26-line
  sample (package / interface / program / class / module / generate / begin / case /
  function / task).

## Re-recorded measurements (real F2 library, ldd-verified)

| mode | sample | begins/ends | notes |
|---|---|---|---|
| before (pre-T45 XML from HEAD, sha256 `3d1ec41f…` / `dc6e0a47…`) | Verilog 18 lines | **0 / 0** | 37 `Unknown keyword list` warnings total |
| before | SystemVerilog **26 lines** | **0 / 0** | `SAMPLE language=systemverilog text-lines=26` |
| after (current fixed XML, sha256 `c6adc763…` / `2abba334…`) | Verilog 18 lines | **7 / 7** | `ours=1 author=VEdit section=Hardware` |
| after | SystemVerilog **26 lines** | **11 / 11** | same assertions |

Every result line is preceded by
`LDD library=F2 resolves=/…/target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6`.
Logs: `10-folding-before-F2.log`, `11-folding-after-F2.log`. Both runners finished with
`RUNNER-EXIT=0` and `PROBE-SUMMARY … all-checks=PASS`.

## Old logs preserved, not silently overwritten

`10-folding-before.log` and `11-folding-after.log` keep their original content and now start
with `# INVALID (T66): this log measured the DISTRO library (F2_DIR path bug) and the SV
sample branch never ran; superseded by <new file>.` The measurement of record is the `-F2` pair.

## Corrections applied to documents that cited the old numbers

- `evidence/syn-kde-data/FOLDING-fix-T45.md` — table now points at the `-F2` logs; SystemVerilog
  **7/7 → 11/11** (26-line sample); added the T66 correction section.
- `evidence/syn-kde-data/06-conclusion.md` — correction now cites the `-F2` logs and
  `Verilog 7/7、SystemVerilog 11/11（26 行样例）`; the `03-folding.log` table row explains that its
  7/7、9/9 came from KDE's built-ins.
- `evidence/syn-kde-data/03-folding.log` — banner now points at the `-F2` logs and records that the
  first `10`/`11` recording was also distro-measured.
- `docs/decisions/r2-license.md` §9 item 5 — T45 fix + T66 numbers (A4.5 view-level still separate).
- `docs/handover/appendix/amendments-rev2.md` — §1 execution status now states the measured
  Verilog 7/7 / SV 11/11 and the ldd proof; the stale "existing XML gives 0 folds" risk statement
  is replaced by the T45/T66 facts; facts B/C in §4 are marked as the pre-T45 historical state;
  U-1 now names the `-F2` logs and the measured counts.
- `requirements.md` is **not** in this task's write scope and was not edited: C11/C14 still carry
  the pre-T45 sha256 `3d1ec41f…`/`dc6e0a47…` and sizes. Current fixed files are
  `c6adc763c7b280d7493f48d06e5779d0b1e2068ac82f75fe7f31a07b0788a6c2` (85 lines / 5,111 B) and
  `2abba3342d0878e71b99d358d96e510f857cd7f232786951be8602f513e0b992` (90 lines / 5,492 B) —
  the planner should update C11/C14 when the T45 commit lands.

## Verification commands and observed results

| command | observed |
|---|---|
| `bash evidence/syn-kde-data/folding-probe/run.sh after` | `PROBE-SUMMARY mode=after expected=fold all-checks=PASS`, `RUNNER-EXIT=0`; ldd → F2 path; Verilog 7/7, SV 11/11 |
| `bash evidence/syn-kde-data/folding-probe/run.sh before` | `PROBE-SUMMARY mode=before expected=zero all-checks=PASS`, `RUNNER-EXIT=0`; ldd → F2 path; 0/0 with 37 warnings |
| negative control with the **old** flags (`14-ldd-guard-negative-control.log`) | old flags resolve the DISTRO library (`/usr/lib/…`), guard **REJECTED exit=1** |
| `ctest --test-dir target/qt6-build -R 'syntax\|syn-grammars' --output-on-failure` | `5/5 passed`, `CTEST-SYNTAX-EXIT=0` |

## Not verified / honest limits

- Folding is user-visible; this host has no display, so only the unit-level
  `applyFolding` counts through the real F2 library are proven. **A4.5's view-level
  observation is still separate** (amendments rev2 U-2 / T29) and is not claimed here.
- The distro-fallback path (`LIBRARY=DISTRO`) was removed from the runner: F2 is now
  mandatory for this probe, and its absence is a hard failure instead of a mislabeled run.
- `resources/syntax/*.xml` were not modified (T45's fix is correct); `requirements.md`
  hash correction is the planner's follow-up.
