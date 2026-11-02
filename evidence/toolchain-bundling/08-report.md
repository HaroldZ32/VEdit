# T82 — bundle verible + slang, explicit toolchain observability

C9 header

- collected_at_utc: **2026-10-31T14:4x** (each log carries its own exact header; the
  staged payload was assembled at 22:39 +08:00, acceptance runs immediately after)
- git_rev_parse_HEAD: see each log's `git_head:` line (the runs were made on the
  current working tree; exact values are inside `00-context.txt`, `01-pins.txt`,
  `02-assemble.log`, `04-toolchain-bundled.log`, `05-toolchain-missing.log`,
  `06-visible-status.log`, `07-notices.txt`, `09-typecheck.log`)
- git_status_porcelain: `10-scope-check.txt`
- Method: headless only (no display). GUI-visible behaviour is **not** claimed.

## 1. What was delivered

### 1.1 Bundling (the delivery form)

`packaging/runtime/assemble-appdir.sh` now installs the external toolchain into the
AppDir, pinned byte-for-byte:

| component | bundled path | pin | licence |
|---|---|---|---|
| Verible `verible-verilog-ls` | `bin/verible-verilog-ls` | `VERIBLE_LS_SHA256` | Apache-2.0 |
| Verible `verible-verilog-lint` | `bin/verible-verilog-lint` | `VERIBLE_LINT_SHA256` | Apache-2.0 |
| Verible `verible-verilog-format` | `bin/verible-verilog-format` | `VERIBLE_FORMAT_SHA256` | Apache-2.0 |
| slang | `bin/slang` | `SLANG_BIN_SHA256` | MIT |

- Options: `--toolchain-verible <dir>`, `--toolchain-slang <path>`, `--require-toolchain`
  (defaults probe `.mpd/recon/verible/bin` / `target/verible-pin/extract/*/bin` and
  `build/slang/out/slang`). A sha256 mismatch fails the assembly.
- It writes `usr/share/vedit/toolchain.txt` (installed identity + `missing=[...]`), copies
  the licence texts to `usr/share/doc/vedit/licenses/{Apache-2.0.txt,slang-LICENSE.txt}`,
  appends the toolchain rows to `RUNTIME-NOTICES.txt`, and everything enters
  `runtime-manifest.txt`.
- `THIRD_PARTY_NOTICES.md` §9.3 registers Verible (Apache-2.0) and slang (MIT) with the pins,
  bundle paths and licence locations.

### 1.2 Discovery order and observability

`src/workspace/diagnostics/toolchain.*` resolves each tool with the contract order
**bundled > PATH > env** and returns the chosen source + the missing list. It is used by
the diagnostics integration for both tools:

- `verible-verilog-ls` → the LSP session path;
- `slang` → passed explicitly to the Rust SLANG service (`vd_deeplint_lint` override), so
  the observable choice and the executed binary cannot diverge.

Visibility:

- `vedit-app --selfcheck` prints `TOOLCHAIN source=verible:<…> slang:<…> missing=[…]`.
- normal start prints the same line and appends a shell diagnostic (status bar; it also
  goes to the qWarning log) with **what is missing, what it disables, how to fix it**, e.g.
  `Diagnostics unavailable: verible-verilog-ls not found (bundled, PATH, env — all empty;
  probed …); … Impact: … Fix: put verible-verilog-ls in the product's bin/ … or set
  VEDIT_VERIBLE_LS; …`.
- the outline/diagnostics panel status shows the same sentence while a tool is missing.

### 1.3 Acceptance tests added

- `src/workspace/diagnostics/toolchain_selftest.cpp` (`vedit-toolchain-selftest`):
  * `--expect bundled`: bundled sources reported for both tools; runs the **bundled**
    `verible-verilog-lint` on the known-bad fixture and requires a real lint violation
    (exit 1 + `line-length`).
  * `--expect missing`: `source=…:missing missing=[verible, slang]`, the explicit
    user message (impact + fix), and no fabricated diagnostics.
- `vedit-toolchain-missing-selftest` ctest entry (deterministic: an empty staged AppDir).
- resolver precedence checks in `diagnostics_selftest.cpp`: bundled beats PATH, PATH
  beats env, env is last, all-missing reports both tools and the visible message.

## 2. Clean-container acceptance evidence

Container: `vedit-qt6-build:local`; **no verible/slang on PATH** (`NONE-ON-PATH`,
`01-pins.txt`); runs use `env -i PATH=/usr/bin:/bin QT_QPA_PLATFORM=offscreen` (no
`VEDIT_*` overrides).

### (a) bundled available → lint really produces results, `source=bundled`

`04-toolchain-bundled.log`

```
RESOLVED source=verible:bundled slang:bundled missing=[]
CHECK PASS bundled.verible-source  …/AppDir/bin/verible-verilog-ls [bundled]
CHECK PASS bundled.slang-source    …/AppDir/bin/slang [bundled]
LINT exit=1 output-lines=2 first=…/bad_line_length.sv:3:101-108: Line length exceeds max: 100; is: 108 [Style: line-length] [line-length]
CHECK PASS bundled.lint-reports-violation  exit=1
VER-TOOLCHAIN-SELFTEST expect=bundled checks=6 failures=0
TOOLCHAIN source=verible:bundled slang:bundled missing=[]
SELFCHECK-EXIT=0                    (APP-SHELL-SELFTEST checks=176 failures=0)
```

### (b) bundled binaries moved away → explicit missing, no silent emptiness

`05-toolchain-missing.log` — hardlink copy of the assembled AppDir, then
`rm bin/verible-verilog-ls bin/slang` (the pristine AppDir is untouched):

```
RESOLVED source=verible:missing slang:missing missing=[verible, slang]
CHECK PASS missing.status-line  source=verible:missing slang:missing missing=[verible, slang]
CHECK PASS missing.user-message  Diagnostics unavailable: verible-verilog-ls not found (bundled, PATH, env — all empty; probed …); slang not found (…); Impact: …; Fix: …
CHECK PASS missing.no-fabricated-diagnostics
VER-TOOLCHAIN-SELFTEST expect=missing checks=6 failures=0
TOOLCHAIN source=verible:missing slang:missing missing=[verible, slang]
SELFCHECK-EXIT=0
```

Normal-mode visibility (the user-facing surface): `06-visible-status.log` shows the
`[app-shell] Diagnostics unavailable: … Impact … Fix …` line and the stdout
`TOOLCHAIN source=verible:missing slang:missing missing=[verible, slang]` with
`APP-EXIT=0` (no crash).

`--selfcheck` source transitions: `04` bundled, `05` missing, and with explicit env
overrides `source=verible:env:VEDIT_VERIBLE_LS slang:env:VEDIT_SLANG missing=[]`
(observed in the T82 run; the resolver test also asserts env is the LAST source).

## 3. Commands and observed results

| command | observed |
|---|---|
| `bash packaging/runtime/assemble-appdir.sh … --require-toolchain` | `ASSEMBLE-EXIT=0`; three verible + slang installed with the pinned sha256; `toolchain: complete (verible + slang), missing=[]`; `APPDIR_FILES=203` |
| `vedit-toolchain-selftest --appdir <AppDir> --expect bundled --lint-file …` | 6/6 PASS; bundled lint exit 1 with a real `line-length` diagnostic |
| `vedit-toolchain-selftest --appdir <AppDir-no-tools> --expect missing` | 6/6 PASS; `missing=[verible, slang]`, impact+fix message, no diagnostics |
| `<AppDir>/bin/vedit-app --selfcheck` (clean env) | `TOOLCHAIN source=verible:bundled slang:bundled missing=[]`, `APP-SHELL-SELFTEST ok=1 checks=176` |
| `<AppDir-no-tools>/bin/vedit-app --selfcheck` (clean env) | `TOOLCHAIN source=verible:missing slang:missing missing=[verible, slang]`, selfcheck still ok (no crash) |
| `ctest --test-dir target/diagnostics-build` | 3/3 passed (`CTEST-LANE-EXIT=0`) |
| `ctest --test-dir target/qt6-build` | **17/17** passed (new `vedit-toolchain-missing-selftest`), `CTEST-ROOT-EXIT=0` |
| `bash build/gates/docs.sh` | `[docs] PASS` |
| `bash build/gates/typecheck.sh` | `[typecheck] PASS (pinned image vedit-build:local)`, `TYPECHECK-EXIT=0` |
| `vedit-app --hold 200` (clean env, normal start) | status-bar diagnostic emitted; `TOOLCHAIN …` printed; `APP-EXIT=0` |

## 4. Scope / shared-file notes (honest)

- Files changed: the diagnostics lane (`src/workspace/diagnostics/**`), the two app-shell
  hooks (`src/app/app_shell.h/.cpp`: public `addShellDiagnostic`), one `--selfcheck` print in
  `src/app/main.cpp`, `packaging/runtime/assemble-appdir.sh`, `THIRD_PARTY_NOTICES.md`, and
  `evidence/toolchain-bundling/**`. No Rust crate, test, gate, `resources/syntax/*.xml`,
  `requirements.md` or `evidence/verdicts/**` was touched.
- `src/app/main.cpp` already carried another lane's in-flight edits (T80); this task adds
  only the one `TOOLCHAIN` print at the start of `runSelfCheck()`. Flagged for the Lead.
- The staged AppDir is the current M0 assembly (it installs `vedit-m0-smoke`). To exercise
  the bundled `--selfcheck` path, the built `vedit-app` was copied into the staged
  `AppDir/bin/vedit-app` **for that run only**; the delivered AppImage (`bin/vd-app` via
  T31) is not claimed here. The toolchain payload, pins, discovery order and status output
  are what T31 will consume.
- GUI confirmation is not claimed (no display). The visible surfaces are exercised through
  their headless equivalents: `--selfcheck` output, the shell diagnostic list, and the lane
  selftests.

## 5. Evidence index (all with C9 headers)

| file | content |
|---|---|
| `00-context.txt` | time, HEAD, status, image facts |
| `01-pins.txt` | `versions.env` pins, source sha256, clean-container premise |
| `02-assemble.log` | full AppDir assembly with `--require-toolchain` (sha checks, `missing=[]`) |
| `03-appdir-toolchain.txt` | installed binaries, licences, `toolchain.txt`, manifest/notices entries |
| `04-toolchain-bundled.log` | (a) bundled selftest + `--selfcheck` (source=bundled, real lint) |
| `05-toolchain-missing.log` | (b) binaries removed in a hardlink copy → explicit missing |
| `06-visible-status.log` | normal-mode `[app-shell]` diagnostic + `TOOLCHAIN` line, exit 0 |
| `07-notices.txt` | `THIRD_PARTY_NOTICES.md` §9.3 + AppDir licence inventory |
| `09-typecheck.log` | typecheck gate PASS |
| `10-scope-check.txt` | changed-path classification |
