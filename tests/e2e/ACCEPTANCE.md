# VEdit v0.1.0 — E2E acceptance report (T27)

Frozen product baseline: **`f1f96223025ecb51124557e5efabd6bee2af6210`** (frozen2).
Canonical CLI binary: **`41d02dcac944c7bff614ab6c44a51f243cece7692afc4d9dda1fc644adef1611`**,
6,002,112 bytes, stripped, reproduced 3/3 by clean builds
(`build/evidence/determinism/build-{1,2,3}.txt`).

## 1. Frozen artifacts and the four `vd-app` copies

| product | path | bytes | sha256 |
|---|---|---:|---|
| AppImage | `packaging/appimage/out/VEdit-0.1.0-x86_64.AppImage` | 47,745,528 | `4f800ab97368de6f379efdc15a582bfbb615bcc3e2b7a0a3f5a19935de21ce44` |
| tar.gz | `packaging/tarball/out/vedit-0.1.0-linux-x86_64.tar.gz` | 40,493,353 | `ddf389733b3bc82d68c4f22d6bc2a09ab87b76aaf2eaa3bfbd10655ded9e6e81` |
| `.run` | `packaging/tarball/out/VEdit-0.1.0-x86_64.run` | 40,496,151 | `0ef44b3576905c2ae09547eeaa74a9d5d7f2426e92bce7a999633938fee788ea` |

`vd-app` inside AppImage (`usr/bin/vd-app`), tar.gz (`VEdit/bin/vd-app`) and
`build/artifacts/vd-app` are all **`41d02dca…def1611`**, 6,002,112 bytes
(harness check `AC-19 payload vd-app == canonical` PASS for both extracted
products). The `.run` payload is documented by the packaging lane as the same
binary; the harness did **not** execute the `.run` extractor in this run (see §3).

## 2. Old → frozen hashes (rebuild reasons)

| product | old | frozen |
|---|---|---|
| AppImage | `b589416e…` / 48,105,976 B | `4f800ab9…` / 47,745,528 B |
| tar.gz | `5d7e1e96…` | `ddf38973…` / 40,493,353 B |

Reasons: shared canonical `vd-app` (all products consume `build/artifacts/vd-app`),
T21 single-window fix (duplicate-activation ghost window), runtime fixture-root
discovery (removes `env!` source path), shared `RELEASE_RUSTFLAGS`, and the
AppRun runtime-directory contract from T25.

**Container hash disclaimer:** the AppImage/tar.gz container hashes are *not* a
reproducibility criterion — squashfs/gzip metadata can vary between rebuilds.
Equality is judged by `vd-app` sha256 + size (and that is what AC-19 uses here).

## 3. Launch paths / extraction

- AppImage: `--appimage-extract` + `AppRun` executed — PASS (selftest/window).
- AppImage direct FUSE execution is not used in the clean room (no `/dev/fuse`); the `--appimage-extract-and-run` and `APPIMAGE_EXTRACT_AND_RUN=1` variants were executed and both exit 0 (§4/§runtime-repro.txt).
- tar.gz: `VEdit/bin/vedit` (official launcher, sets payload env) — PASS.
- `.run`: `sh VEdit-0.1.0-x86_64.run --selftest all` exit 0, JSON `ok: true`,
  and the extraction directory is cleaned up (before=0, after=0) — PASS.
- AppImage `--appimage-extract-and-run --selftest all` and
  `APPIMAGE_EXTRACT_AND_RUN=1 ... --selftest all`: both exit 0 — PASS.
  Direct execution without FUSE is expected to fail with 127 (no `/dev/fuse`);
  that path is recorded from the packaging lane rather than re-run here.

## 4. AC results (real runs, exit codes in `tests/e2e/out/<product>/results.tsv`)

| AC | result | evidence |
|---|---|---|
| AC-01 | PASS — `tool=readelf`, highest `GLIBC_2.14` (≤ 2.17) for both products | `out/*/results.tsv` |
| AC-02 | PASS (AppImage, tar.gz): selftest all exit 0, 51 checks (52 with `--screenshot`), single toplevel = 1, screenshot non-blank (AppImage 33,484 B `5daf5526…`; tar.gz 33,780 B `2f405f48…`) | `out/*/selftest-all.log`, `screenshot.png` |
| AC-03/04 | PASS within suite coverage (edit/save, undo, multi-tab, find/replace, goto, close confirm) | `tests/e2e/out/*/selftest-all.log` |
| AC-05 | PASS AUTO (language manager/cobalt) + MANUAL token-colour review | selftest highlight + screenshot |
| AC-06 | PARTIAL — diagnostics suite passes; ≤3s timing and click-to-line are not separately asserted | `tests/e2e/out/*/selftest-all.log` |
| AC-07 | PARTIAL — format applied in one undo step asserted; byte equality with the bundled `verible-verilog-format` not driven by a UI harness | `tests/e2e/out/*/selftest-all.log` |
| AC-08 | PARTIAL — F12 cross-file definition PASS; references/outline/rename preview wired but not asserted | `tests/e2e/out/*/selftest-all.log` |
| AC-09 | PASS — no verible-ls/vd-app left after suites | `out/*/results.tsv` |
| AC-10 | PASS within suite (SLANG diagnostics reach panel/gutter); direct-JSON count equality is lane-tested | selftest slang |
| AC-11 | PARTIAL — corrupt-binary degradation is lane-tested, not re-run here | lane test evidence (vd-deeplint) |
| AC-12 | PASS (SLANG is in the payload; lane evidence covers glibc/version/lint) | packaging evidence |
| AC-13 | PASS — FIM default off (worker_spawns unchanged, no endpoint) | selftest fim |
| AC-14 | MANUAL — real endpoint run in progress by the FIM lane; suite only proves default-off | steps below / `evidence/fim` when landed |
| AC-15 | PASS/PARTIAL — no default secrets in repo; log scan by config lane | — |
| AC-16 | **AUTO (Tier 2)** — real IME pinyin→Chinese commit in the offline payload: `GTK_IM_MODULE=ibus`, `ibus-daemon`+`libpinyin`, `xdotool type "nihao"` + space → buffer `你好` (hex `e4bda0e5a5bd`), candidate-window screenshot; Tier 1 (GtkIMContext is xim/ibus) and Tier 3 (`simple` + Unicode injection) also covered | `evidence/ime/logs/99-results.txt`, `evidence/ime/shots/tier2-before-commit.png`, `tier2-after-commit.png`, `tier3-unicode.png` |
| AC-17 | PASS — README/THIRD_PARTY_NOTICES/licences present (packaging/docs lanes) | — |
| AC-18 | RECORD — AppImage 47,745,528 B, tar.gz 40,493,353 B; cold-start recorded by packaging clean room | — |
| AC-19 | PASS — four `vd-app` copies equal `41d02dca…`; 3/3 reproducible builds | `build/evidence/determinism/` |

### Runtime-directory failures: harness misjudgement, product correct

The first harness run used `XDG_RUNTIME_DIR=/nonexistent TMPDIR=/nonexistent`
and saw exit 0. That was a **harness defect**: the clean room runs as root, so
`mkdir -p /nonexistent` succeeds (a creatable path is a valid runtime directory).
Repeating with genuinely unusable paths (`/proc/nonexistent`) gives the expected
product behaviour for both products: **exit 1** with
`vedit: cannot create a runtime directory; tried '/proc/nonexistent' and
'/proc/nonexistent'` + a remediation line. The product has distinct codes per
launcher stage (launcher after extract = 1, `.run` stub = 2, AppImage without
FUSE = 127); the harness now asserts the launcher code per product. Evidence:
`tests/e2e/runtime-repro.txt` (exact command/env/executable/stdout/stderr/rc).
After the fix the harness reports **0 product-level FAIL**. Frozen artifacts were
not rebuilt and their hashes are unchanged.

## 5. First run — harness defects (kept as evidence)

The first `run.sh both` invocation failed 6 checks because the harness used
`file(1)` (absent in the clean image) and guessed launcher paths, so the app was
not started for several checks. The first-run transcript was a transient `/tmp` capture and was not committed; this report records the defect, its two root causes and the fix in the harness, and `tests/e2e/runtime-repro.txt` holds the reproducible second finding.
Fix: ELF magic bytes instead of `file`, `readelf`/`objdump` with `tool=` in the
report and explicit FAIL when neither exists, explicit per-product launcher
selection, `.run` launcher + cleanup checks, and the runtime test now uses
`/proc/nonexistent` (a genuinely unusable path; `/nonexistent` is creatable by
root and caused the second-run misjudgement). Both harness defects are kept in
the reproducible second finding (`runtime-repro.txt`); the first-run defect is
documented by this report's root-cause and fix record; after the
fix **product-level FAIL = 0**.

## 6. `/vedit-build` embedded-path counts (methods stated)

- Canonical `build/artifacts/vd-app`: **0** occurrences using
  `strings vd-app | grep -c '/vedit-build'` (`build/evidence/13-remap-check.txt`,
  method: plain line count). Classification: panic source paths are remapped to
  `/vedit`; the only `env!` literal (old fixture fallback) was removed in favour
  of runtime discovery.
- AppImage (packaging lane): **NUL-separated count = 0**; separately
  `SANDBOX_SOURCE_PATH_LITERALS=508` are panic source paths remapped to `/vedit`
  (not host paths, never read at runtime). The two numbers use different
  counting methods and are not comparable; both methods are recorded here.

## 7. Double window — a real product defect (not just a screenshot bug)

Interactive launch activated the GApplication twice (`register_and_activate()`
followed by `run()`), so GTK built **two mapped 1120×780 `VEdit` toplevels**;
the first was never painted (the black frame the screenshot tool read). Fixed:
`run()` owns activation, the activate handler presents the existing window
instead of building another. Clean-room `xwininfo` **before = 2 → after = 1**.
The blank screenshot was a secondary symptom plus `gtk_widget_draw` being called
with a pending allocation (`alloc_needed`); the capture now forces
`size_allocate`, refuses to write a uniform image and reports sampled pixels.
Canonical screenshot: **33,022 B / `006db6a0…` / sampled_colours=17 /
channel_range=255**.

## 8. `--selftest` counts

`--selftest all` = **51 check objects** (the harness counts `"name":` entries and
asserts the expected 51; `"ok": false` must be 0). The JSON contains **58**
occurrences of `"ok": true`: the 51 checks plus 6 suite-level `ok` fields plus the
top-level `ok` field. Adding `--screenshot` appends one conditional check
(`--screenshot produces a non-blank PNG`), making the expected check count 52 for
such a run. The harness asserts the count, not a hard-coded sentence.

## 9. Gates — re-run for this report

**Release-head run:** `tests/e2e/gates-frozen-run.txt` — `timestamp_utc=2026-10-30T05:53:53Z`,
`HEAD=8698a5edcdf47439c3d3007fb912bd1f2af71d7c`, `tracked_modified=0` (only an
untracked lane-owned evidence directory). `cargo fmt --all -- --check` exit 0;
`cargo clippy --workspace --all-targets -- -D warnings` exit 0;
`cargo test --workspace` exit 0 with per-binary result lines
(11/31/14/4/25/13/6/42/40 passed plus the remaining 0-test targets) recorded in
the file. The product source is still frozen2 `f1f9622`; only tests/evidence
changed, so rebuilding gives the identical canonical **41d02dca…**, 6,002,112 B
and the three frozen artifacts remain valid.

## 10. MANUAL acceptance steps

- **AC-16 (Chinese IME):** on CentOS 7 GNOME with ibus/fcitx, enable the Chinese
  input method, type a Chinese comment and a Chinese find term in a `.sv` buffer;
  verify the candidate window and committed text in the editor; record a
  screenshot/screen capture. (Container automation cannot judge IME UI.)
- **FIM real endpoint (AC-14):** configure `[fim] enabled=true` with an
  OpenAI-compatible endpoint/model or llama.cpp `/infill`; press `Alt+\`; verify
  ghost text; `Tab` inserts through `Document`; `Esc` cancels; typing cancels the
  in-flight request; simulate timeout/500/unreachable and check the status bar.
- **Real modal close dialog:** edit a file, press Ctrl+W, verify the modal
  Save/Discard/Cancel dialog; the suite only exercises the injected confirmation
  seam (refusal + recorded request).

## 11. Release readiness

- `git log --oneline | wc -l` = **45**; `git rev-list --merges --count HEAD` = **0**
  (linear history, no merge commits).
- `git ls-files packaging/appimage/out packaging/tarball/out build/slang/out`
  = **empty** (no product artifact is tracked).
- Three artifacts exist with the hashes in §1; four `vd-app` copies equal
  `41d02dca…` (§1/§4).
- No open product-level FAIL: the earlier runtime finding was a harness defect
  (creatable path under root); with `/proc/nonexistent` both products return the
  expected code and message. Frozen artifacts unchanged.
