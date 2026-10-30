# E2E acceptance (T27) — framework

Formal runs MUST use the frozen product artifacts. This directory only contains
the harness and the AC matrix; results are written under
`tests/e2e/out/<artifact>/` (git-ignored) and summarised in
`tests/e2e/ACCEPTANCE.md` **after** the freeze.

## Running

```sh
# inside a clean, network-less centos:7 image with the workspace mounted
bash tests/e2e/run.sh both      # appimage + tarball; exits non-zero on any FAIL
bash tests/e2e/run.sh appimage
bash tests/e2e/run.sh tarball
```

The harness never builds products: it consumes
`packaging/appimage/out/VEdit-0.1.0-x86_64.AppImage`,
`packaging/tarball/out/vedit-0.1.0-linux-x86_64.tar.gz` and
`packaging/tarball/out/VEdit-0.1.0-x86_64.run`.

## AC matrix (status of the harness)

| AC | check in the harness | status |
|---|---|---|
| AC-01 | `readelf --version-info` over every payload ELF, max GLIBC ≤ 2.17; `ldd` resolves | AUTO |
| AC-02 | extract/launch, window appears, `--selftest smoke` exit 0, `--screenshot` non-blank | AUTO |
| AC-03/04 | `--selftest all` (smoke 20 checks incl. edit/save/find/replace/goto/close confirm) | AUTO/PARTIAL |
| AC-05 | highlight suite + CJK screenshot; AUTO language mapping; token colours MANUAL | AUTO+MANUAL |
| AC-06 | LSP suite diagnostics; timing ≤3s and click navigation MANUAL/PARTIAL | PARTIAL |
| AC-07 | LSP suite format applied + one undo; byte equality needs a UI driver | PARTIAL |
| AC-08 | LSP suite F12 definition across files; references/outline/rename preview MANUAL | PARTIAL |
| AC-09 | no verible-verilog-ls/vd-app processes after the suite | AUTO |
| AC-10 | slang suite + direct payload slang `--diag-json` count equality | AUTO/PARTIAL |
| AC-11 | corrupted `VEDIT_SLANG` smoke: app still starts; full degradation MANUAL | PARTIAL |
| AC-12 | payload slang `--version`, real lint, GLIBC ≤ 2.17 | AUTO |
| AC-13 | FIM default-off suite (zero worker / no endpoint) | AUTO |
| AC-14 | mock endpoint, ghost text/Tab/Esc | MANUAL (needs UI driver) |
| AC-15 | `git grep` for secret patterns + logs/scans | AUTO/MANUAL |
| AC-16 | IME manual steps (README) | MANUAL |
| AC-17 | docs/licences/notices presence | AUTO/MANUAL |
| AC-18 | artifact sizes + cold start to window | AUTO (record) |
| AC-19 | one-command build evidence + `vd-app` sha256 identical across products | AUTO (after freeze) |

Machine-checkable items are recorded with command, observed output and exit code
in `tests/e2e/out/<artifact>/results.tsv` and must be re-run against the frozen
artifacts. The three AppRun runtime-directory paths are covered by
`runtime_dir_checks` (XDG_RUNTIME_DIR valid; unset→TMPDIR; both unusable→loud
error with the `-$(id -u)` directory name).
