# T84 — F2 AppDir lost its own syntax definitions: VEDIT_SYNTAX_DIR was one level too deep

C9 header

- collected_at_utc: each log carries its own line; the experiment and the `before`
  run were made on **HEAD `c682694`** and the rebuilt tarball on the same tree
  (`00-context.txt` has HEAD + status).
- Container: `docker.m.daocloud.io/library/centos:7` (stock; no KDE data).
  `kernel.is-3.10` therefore FAILs by construction (the container shares the host
  kernel) — the captain explicitly accepts that one FAIL.
- Method: headless; the windowed `highlight.mode` check stays SKIP, with the
  equivalent `editcore.highlight.*` evidence captured from the same package.

## 1. Root cause, proven before changing anything

The product expects **the data root** — the directory that *contains* `syntax/`:

- `src/syntax/kde/ksyntax_data.h:6-10`: `VEDIT_SYNTAX_DIR = AppDir/share/ksyntaxhighlighting`,
  XMLs live at `<VEDIT_SYNTAX_DIR>/syntax/*.xml`, and the app calls
  `Repository::addCustomSearchPath(<VEDIT_SYNTAX_DIR>)`.
- `src/syntax/kde/ksyntax_data.cpp`: `addSearchPath` requires `<path>/syntax` to exist,
  then calls `addCustomSearchPath(path)` and records `path/syntax`.

`packaging/runtime/AppRun` set `VEDIT_SYNTAX_DIR=$APPDIR/usr/share/org.kde.syntax-highlighting/syntax`
(the **subdirectory**), so the product looked for `…/syntax/syntax`, found nothing, and
reported `own-xml=[] verilog=0 definitions=3` → `.v` fell back to plain text.
`build/centos7-verify/verify-centos7.sh` repeated the same wrong value (two places),
so the package's own verification measured the broken value.

Two-value experiment (`01-two-values.log`, same binary, only the env value differs):

| `VEDIT_SYNTAX_DIR` | product report |
|---|---|
| `…/org.kde.syntax-highlighting` (data root) | `private-dir='…/org.kde.syntax-highlighting' own-xml=[vedit-systemverilog.xml,vedit-verilog.xml] definitions=3 verilog=1 systemverilog=1` |
| `…/org.kde.syntax-highlighting/syntax` (subdir) | `private-dir='…/syntax' own-xml=[] verilog=0 systemverilog=0` |

**Conclusion: the data root is the correct value.** The subdirectory is wrong by the
product's own documented contract.

## 2. Fix (both sides, so the next packager cannot repeat it)

1. **Packaging value corrected**: `packaging/runtime/AppRun` now exports the data root;
   `build/centos7-verify/verify-centos7.sh` (both `run_app` and the loader check) and
   `build/centos7-verify/STEPS.md` use the same root. **No check logic was changed** —
   only the env value; the FAIL/PASS criteria are untouched (see `07-code-contract.txt`).
2. **Product-side tolerance** (the "next packager" guard): `src/syntax/kde/ksyntax_data.cpp`
   now normalises a value that points at the `syntax` subdirectory to its parent (when the
   path's basename is `syntax` and `<path>/syntax` is missing), reports the *effective*
   data root in `private-dir`, and uses that root for the whitelist scan. Both values now
   produce `own-xml=[vedit-systemverilog.xml,vedit-verilog.xml]`, `verilog=1` (verified on
   the real AppDir in stock CentOS 7, `01-two-values.log` second half; the rebuilt binary
   was used). Header documentation updated (`src/syntax/kde/ksyntax_data.h`).

## 3. Before → after, same command

Same tarball layout, same container, same invocation:
`tar -xzf <tarball> && cd <dir> && bash ./verify-centos7.sh <report>`

| run | tarball sha256 | result |
|---|---|---|
| **before** (`02-before-report.txt`) | `6ecf90a258c2e8b2d1b82b114bd65d54dd0c3d38b883e155592555bd83803b51` (see `05-tarball-sha-before.txt`) | `RESULT FAIL highlight.verilog-definition … actual=verilog=0 own-xml=[] definitions=3`; `SUMMARY: PASS=14 FAIL=2 SKIP=2` (FAILs: `kernel.is-3.10`, `highlight.verilog-definition`) |
| **after** (`04-after-report.txt`) | **`acc58ee03206526342ef59edc1b4b23470c068bb6dbdd1aec634629ce85d9a0e`** | `RESULT PASS highlight.verilog-definition … actual=verilog=1 own-xml=[vedit-systemverilog.xml,vedit-verilog.xml] definitions=3`; `RESULT PASS highlight.stack`; `SUMMARY: PASS=15 FAIL=1 SKIP=2` — the only FAIL is the expected `kernel.is-3.10`.
  (The script exits 1 whenever any FAIL remains, so the after-run exit is also 1 by design;
  `04-after-exit.txt` records it.) |

New tarball: `build/centos7-verify/dist/vedit-centos7-verify-c682694.tar.gz`,
**size 104 MB, sha256 `acc58ee03206526342ef59edc1b4b23470c068bb6dbdd1aec634629ce85d9a0e`**
(`05-tarball-sha-after.txt`). The packaged `AppRun` and `verify-centos7.sh` were extracted
from the new tarball and grep-verified (`07-code-contract.txt`), so the rebuilt package
really carries the corrected value.

SKIPs (unchanged, reasons recorded):
- `highlight.mode` — no windowed session headless; the stack-level definition check plus
  the direct selfcheck below cover the evidence.
- `lint.product` — this build's diagnostics selftest expects its FFI fixture directory,
  which the tarball does not ship; not a package failure (same reason as T83).

## 4. T81's result is effective in the package

`06-selfcheck-after.log` (same new package, bundled loader):

```
syntax-product: … private-dir='…/AppDir/usr/share/org.kde.syntax-highlighting' definitions=3
  own-xml=[vedit-systemverilog.xml,vedit-verilog.xml] verilog=1 systemverilog=1 …
CHECK editcore.highlight.verilog.mode PASS mode='Verilog' expected='Verilog'
CHECK editcore.highlight.verilog.format-ranges PASS 30
CHECK editcore.highlight.verilog.folding-regions PASS begins=2 ends=2
CHECK editcore.highlight.systemverilog.mode PASS mode='SystemVerilog'
CHECK editcore.highlight.systemverilog.format-ranges PASS 29
CHECK editcore.highlight.systemverilog.folding-regions PASS begins=1 ends=1
```

## 5. Scope / notes

- Changed: `packaging/runtime/AppRun`, `build/centos7-verify/verify-centos7.sh`,
  `build/centos7-verify/STEPS.md`, `src/syntax/kde/ksyntax_data.cpp` (+ its header comment),
  and `evidence/appdir-syntax-dir/**`. No `resources/syntax/*.xml`, no `src/editor/**`,
  no gate weakening; no commit/push.
- `evidence/centos7-verify-package/README.md` (T83) still describes the old FAIL; it is
  outside this task's write scope, so this report is the superseding record for that line.
- The `--selfcheck` "private-dir" is now the effective root; the report therefore shows the
  same value for both a root-valued and a subdir-valued `VEDIT_SYNTAX_DIR`.

## 6. Evidence index

| file | content |
|---|---|
| `00-context.txt` | HEAD, status, container facts, T84 scope |
| `01-two-values.log` | data root vs syntax subdirectory, product fields (before + tolerance after) |
| `02-before-report.txt` / `.log` | FAIL run on the T83 tarball with the same command |
| `03-build-tarball.log` | rebuild with the corrected AppRun/verify |
| `04-after-report.txt` / `.log` | PASS run on the new tarball; only `kernel.is-3.10` FAIL |
| `05-tarball-sha-before.txt` / `05-tarball-sha-after.txt` | old vs new sha256 |
| `06-selfcheck-after.log` | T81 proof: modes, format ranges, folding regions from the package |
| `07-code-contract.txt` | product contract excerpt, full diff, packaged values grep |
| `08-report.md` | this report |
