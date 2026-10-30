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
- AppImage FUSE direct run / `--appimage-extract-and-run` / `APPIMAGE_EXTRACT_AND_RUN=1`: **NOT TESTED in this harness run** (clean room has no `/dev/fuse`; the extract-and-run variants are packaging-lane evidence, not re-run here).
- tar.gz: `VEdit/bin/vedit` (official launcher, sets payload env) — PASS.
- `.run`: **NOT TESTED** — harness extraction branch for the self-extracting
  stub did not produce results in this run; reported as a harness gap, not a
  product failure. The `.run` hash matches the frozen manifest.

## 4. AC results (real runs, exit codes in `tests/e2e/out/<product>/results.tsv`)

| AC | result | evidence |
|---|---|---|
| AC-01 | PASS — `tool=readelf`, highest `GLIBC_2.14` (≤ 2.17) for both products | `out/*/results.tsv` |
| AC-02 | PASS (AppImage, tar.gz): selftest all exit 0, 51 checks (52 with `--screenshot`), single toplevel = 1, screenshot non-blank (AppImage 33,484 B `5daf5526…`; tar.gz 33,780 B `2f405f48…`) | `out/*/selftest-all.log`, `screenshot.png` |
| AC-03/04 | PASS within the suite coverage (edit/save, undo, multi-tab, find/replace, goto, close confirm) | selftest smoke checks |
| AC-05 | PASS AUTO (language manager/cobalt) + MANUAL token-colour review | selftest highlight + screenshot |
| AC-06 | PARTIAL — diagnostics suite passes; the ≤3s timing and click-to-line are not separately asserted | — |
| AC-07 | PARTIAL — format applied in one undo step is asserted; byte equality with the bundled `verible-verilog-format` is not driven by a UI harness | — |
| AC-08 | PARTIAL — F12 cross-file definition PASS; references/outline/rename preview wired but MANUAL | selftest lsp |
| AC-09 | PASS — no verible-ls/vd-app left after suites | `out/*/results.tsv` |
| AC-10 | PASS within suite (SLANG diagnostics reach panel/gutter); direct-JSON count equality is lane-tested | selftest slang |
| AC-11 | PARTIAL — corrupt-binary degradation is lane-tested, not re-run here | — |
| AC-12 | PASS (SLANG is in the payload; lane evidence covers glibc/version/lint) | packaging evidence |
| AC-13 | PASS — FIM default off (worker_spawns unchanged, no endpoint) | selftest fim |
| AC-14 | MANUAL — needs a local mock endpoint and UI driver | steps below |
| AC-15 | PASS/PARTIAL — no default secrets in repo; log scan by config lane | — |
| AC-16 | MANUAL — see steps below | — |
| AC-17 | PASS — README/THIRD_PARTY_NOTICES/licences present (packaging/docs lanes) | — |
| AC-18 | RECORD — AppImage 47,745,528 B, tar.gz 40,493,353 B; cold-start recorded by packaging clean room | — |
| AC-19 | PASS — four `vd-app` copies equal `41d02dca…`; 3/3 reproducible builds | `build/evidence/determinism/` |

### Product-level FAIL found (reported to captain, not silently fixed)

`XDG_RUNTIME_DIR` valid and unset→`TMPDIR` paths PASS. The third contract path —
**both `XDG_RUNTIME_DIR` and `TMPDIR` unusable** — returned **exit 0** for both
products instead of a loud non-zero failure (`runtime_dir loud failure FAIL,
rc=0`). This is a product/launcher behaviour question against the T25 runtime
contract and is reported for a decision; the acceptance harness was not changed
further after this second FAIL, per the freeze discipline.

## 5. First run — harness defects (kept as evidence)

The first `run.sh both` invocation failed 6 checks because the harness used
`file(1)` (absent in the clean image) and guessed launcher paths, so the app was
not started for several checks. Log: `tests/e2e/first-run-harness-defect.log`.
Fix: ELF magic bytes instead of `file`, `readelf`/`objdump` with `tool=` in the
report and explicit FAIL when neither exists, and explicit per-product launcher
selection (`AppRun`, `bin/vedit`, `.run`). After the fix: **product-level
FAIL = 1 contract path** (above), all other executed checks PASS.

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

`--selftest all` = **51 checks**. Adding `--screenshot` appends one conditional
check (`--screenshot produces a non-blank PNG`), so the same run reports **52**
checks. Both numbers describe the same suite; neither is contradictory.

## 9. Gates — re-run for this report

`tests/e2e/gates-frozen-run.txt`: `timestamp_utc=2026-10-30T05:35:46Z`,
`HEAD=463d96562a5045b004bc8b8d4f5281b95d97ae80` (evidence-only commits after
frozen2; product source still `f1f9622`), `cargo fmt --all -- --check` exit 0,
`cargo clippy --workspace --all-targets -- -D warnings` exit 0,
`cargo test --workspace` exit 0. Working tree at that moment had uncommitted
T27 report/harness files (this report is committed next).

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
- **Open item:** the runtime-directory loud-failure contract path (`XDG_RUNTIME_DIR`
  + `TMPDIR` both unusable) returns exit 0; reported for a product/launcher
  decision before release.
