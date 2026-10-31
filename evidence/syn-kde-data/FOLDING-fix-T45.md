# T45 (Blocker) — folding rules rewritten to the KDE keyword-list idiom

C9 header: collected_at_utc=2026-10-31T10:59:14Z
git_rev_parse_HEAD=1f47e56421bbeaab64f8082df3cfc104a6a2c2ef
git_status_porcelain (this task's paths only):
 M evidence/syn-kde-data/03-folding.log
 M evidence/syn-kde-data/06-conclusion.md
 M resources/syntax/vedit-systemverilog.xml
 M resources/syntax/vedit-verilog.xml
?? evidence/syn-kde-data/10-folding-before.log
?? evidence/syn-kde-data/11-folding-after.log
?? evidence/syn-kde-data/12-regression-after-xml.log
?? evidence/syn-kde-data/FOLDING-fix-T45.md
?? evidence/syn-kde-data/f2-build.log
?? evidence/syn-kde-data/folding-probe/

## The defect (measured, not inferred)

`resources/syntax/vedit-verilog.xml` and `vedit-systemverilog.xml` wrote folds as
`<keyword String="module" beginRegion="moduleEndmodule"/>`. KSyntaxHighlighting 6.24
resolves the `String` value **as a keyword-list name**; with no `<list name="module">`
the loader logs `Unknown keyword list "module"` and drops the rule, so the shipped
definitions produced **begins=0 ends=0** (`10-folding-before.log`).

## The fix

Both files now follow the KDE idiom: the fold words live in two `<list>`s
(`veditFoldBegin` / `veditFoldEnd`) and two `<keyword>` rules reference those lists
by name with `beginRegion`/`endRegion`:

* Verilog list: module, begin, case, casex, casez, function, task, generate (+ end* closers)
* SystemVerilog list: the same **plus** interface/program/class/package and randcase.

## Measurement (the contract's hard constraints)

* library: **F2** — the self-built KSyntaxHighlighting (`QRC_SYNTAX=OFF`, zero
  embedded KDE definitions), `target/ksh-selfbuild/F2/install`; printed as
  `library=F2` in every line.
* **author/section assertion**: the probe refuses to count unless the resolved
  definition is `author=VEdit`, `section=Hardware` — so a KDE built-in can never be
  measured as ours. All measurement lines show `ours=1`.
  (Without that assertion the very same lookup resolves KDE's definition:
  `author=Yevgen Voronenko (ysv22@drexel.edu), Ryan Dalzell (ryan@tullyroan.com),
  version=8` — exactly what the old `03-folding.log` measured: 7/7 and 9/9.)
* because this image also carries the distro data and the F2 build is
  `NO_STANDARD_PATHS=OFF`, the probe measures a copy of our two XMLs under unique
  definition names (`VEditProbeVerilog`/`VEditProbeSystemVerilog`): byte-identical
  content and folding rules, `name=` changed only (`folding-probe/run.sh`).

| log (T66 re-recorded, real F2) | library | resolved definition | sample | begins/ends |
|---|---|---|---|---|
| `10-folding-before-F2.log` | F2 (`ldd`-verified) | name=VEditProbeVerilog author=VEdit section=Hardware **ours=1** | 18-line Verilog | **0 / 0** (37 `Unknown keyword list` warnings total) |
| `10-folding-before-F2.log` | F2 (`ldd`-verified) | name=VEditProbeSystemVerilog author=VEdit section=Hardware **ours=1** | **26-line SystemVerilog** | **0 / 0** |
| `11-folding-after-F2.log` | F2 (`ldd`-verified) | Verilog: name=VEditProbeVerilog author=VEdit section=Hardware **ours=1** | 18-line Verilog | **7 / 7** |
| `11-folding-after-F2.log` | F2 (`ldd`-verified) | SystemVerilog: name=VEditProbeSystemVerilog author=VEdit section=Hardware **ours=1** | **26-line SystemVerilog** | **11 / 11** |

No `Unknown keyword list` warning appears in the after log.

> **T66 correction (2026-10-31) — the first logs were measured on the WRONG library.**
> `10-folding-before.log` / `11-folding-after.log` (the first recording) are **invalid**:
> the runner derived `F2_DIR=$(dirname $(dirname "$LIB"))`, which is `<prefix>/lib`
> instead of `<prefix>/lib/x86_64-linux-gnu` + `<prefix>/include/KF6/KSyntaxHighlighting`,
> so `-I`/`-L` did not exist and the binary silently linked the **DISTRO**
> `libKF6SyntaxHighlighting` while still printing `library=F2`. The probe's SV branch was
> also dead (`definitionName == "SystemVerilog"` never matched `VEditProbeSystemVerilog`),
> so both runs used the 18-line Verilog sample. The corrected runner
> (`folding-probe/run.sh`) now:
> (1) uses the real F2 lib/include dirs (verified with `find`),
> (2) **fails with exit 1 unless `ldd` resolves `libKF6SyntaxHighlighting` inside the F2
> install tree**, and echoes that `ldd` path before every `library=` result line, and
> (3) takes the sample language as an explicit `verilog|systemverilog` argument.
> Both logs were re-recorded on the real F2 library: **Verilog 7/7, SystemVerilog 11/11**
> (26-line sample covering package/interface/program/class/module/generate/begin/case/
> function/task). The old files are kept with an `# INVALID (T66)` banner pointing here;
> they are superseded by `10-folding-before-F2.log` / `11-folding-after-F2.log`.

## Regression (the same definitions in the product paths)

`12-regression-after-xml.log`: the product acceptance test (19/19 checks, own XML
loaded, highlight attributes applied), the syntax-data validator ctest (29/29
checks), and the product binary's startup line all pass with the rewritten files.

## Evidence corrections (explicitly requested)

* `03-folding.log` now carries a top banner: it measured KDE's built-in Verilog
  definition (and KDE's **GPLv3+** SystemVerilog one), so its 7/7 and 9/9 are NOT
  evidence about `resources/syntax/vedit-*.xml`.
* `06-conclusion.md` (c) now carries the same correction inline, pointing at the
  before/after logs above.

## For the planner (requirements wording, not edited here)

The requirements line "FoldingProbe 已证属性形态可产出折叠区" is **wrong as written**:
the attribute form alone produced 0 regions; what the earlier probe's control group
showed is that the *keyword-list-reference* form produces regions (6/6 on its
control). The correct statement for C14: "自有 XML 的折叠用 KDE 关键字列表引用惯用法
（veditFoldBegin/veditFoldEnd + beginRegion/endRegion，region 名 veditFold），在 F2
交付库上实测 begins/ends 非 0（Verilog 7/7，SV **11/11**（26 行真样例），author=VEdit 断言，
  ldd 证明指向 F2）；旧 03-folding.log
的 7/7、9/9 来自 KDE 内嵌定义（SV 为 GPLv3+），不作自有 XML 的证据。"

## T66 — corrected measurement (distro-mislabeled logs superseded)

C9 header for this correction: collected_at_utc=2026-10-31T11:29:12Z
git_rev_parse_HEAD=95c496e682e3841add1a081edd78d29c34957aac
(mode=after at 11:29:00Z same HEAD; exact headers are inside the two new logs)

* **Defect 1 (library)**: the old runner's `F2_DIR` was `<prefix>/lib`, so the compile
  fell back to `/usr/include/KF6` + `/usr/lib/x86_64-linux-gnu`; the `library=F2` label was
  a shell decision ("an F2 .so file exists"), not a measurement. Corrected: lib dir
  `<prefix>/lib/x86_64-linux-gnu`, include `<prefix>/include/KF6/KSyntaxHighlighting`,
  and an **ldd guard** that exits 1 unless `libKF6SyntaxHighlighting` resolves under the
  F2 install tree. Each result block is preceded by `LDD library=F2 resolves=<path>`.
* **Defect 2 (sampling)**: `definitionName == "SystemVerilog"` never matched
  `VEditProbeSystemVerilog`; both runs measured the 18-line Verilog sample. Corrected:
  the probe takes an explicit `verilog|systemverilog` argument; the SV run now reports
  `SAMPLE language=systemverilog text-lines=26`.
* **Re-measured on the real F2 library** (`QRC_SYNTAX=OFF`, no embedded definitions):
  * before (pre-T45 XML, extracted from git HEAD; sha256 `3d1ec41f…` / `dc6e0a47…`):
    Verilog 0/0, SystemVerilog 0/0, 37 `Unknown keyword list` warnings;
  * after (current fixed XML; sha256 `c6adc763…` / `2abba334…`):
    Verilog **7/7**, SystemVerilog **11/11** (26-line sample).
* Old logs `10-folding-before.log` / `11-folding-after.log` keep their content and gain an
  `# INVALID (T66)` banner; the measurement of record is the `-F2` pair.
