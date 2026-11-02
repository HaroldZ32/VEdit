# Verdict — T32: final packaging & AppImage (T31)

## Binding declaration

- Shared-tree HEAD: `eb7bee9ae7d4edadff17efd2783a5a09a80fa1d3`; the T31 packaging scripts are
  workspace changes (`packaging/runtime/**`, `packaging/runtime/hostmatrix.sh`, docs).
- Deliverable baseline: `55a874da076d605c6b620e0bb20f9ac5932d5f46` — source-equivalent to HEAD
  (`git diff 55a874d eb7bee9` = only `evidence/slice1-stop/95/96` logs).
- Final artifact verified: `packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage`,
  **95,349,240 B**, sha256 **`e7ae067fe66c8e271ba72da974795a7ee3db0efa59bda45079b5a048b4a09558`**
  (identical to `target/wt-55a874d/.../VEdit-0.1.1-x86_64.AppImage`, `SHA256SUMS`, `00-C9.txt`).
- Loop: bound `loop-20261031T095153-bf8202` (T31 contract, writer Senior Engineer). Declared gate
  evidence: `ev-20261101T084526-177379` (`typecheck`, exit 0).
- Companion audit with raw outputs: `evidence/verdicts/pack/audit.txt`.

## Verdict: PASS — packaging acceptance (AC-S3.3 / AC-S0.4 / glibc ≤2.17) verified

The delivered AppImage is the correct payload, is self-contained, passes the headless selfcheck /
delivery gates in **four container hosts including glibc 2.17**, carries the Rust FFI statically, and
its sha256 matches the recorded evidence. Four **non-blocking evidence/documentation findings**
(N1–N4) must be fixed before the final handoff; none changes any measured result or the artifact.

## AC-S3.3 checklist (my own reruns, not the builder's logs)

| item | verification |
| --- | --- |
| `assemble-appdir.sh` installs the product as `bin/vd-app` | `install -m 0755 "$APP" "$OUT/bin/vd-app"` (assemble-appdir.sh:94); the extracted AppImage's `bin/vd-app` sha256 = **`b93f8cf4c43dada6f041905bcee264fd4349966af5d4248028f17093c5e4520d`** = the clean-worktree `target/qt6-build/src/app/vedit-app`; `readelf -d` NEEDED = libKF6TextEditor/libKF6Parts/libQt6Widgets/libtree-sitter/libKF6SyntaxHighlighting/libQt6* → the Qt/KTextEditor deliverable, not the legacy GTK binary |
| single-file AppImage + recorded sha256 | one file, 95,349,240 B, sha `e7ae067f…`; `SHA256SUMS` matches; extracted in 4 bare containers (no Qt/KF6/verible/slang) |
| selfcheck green | my clean-container runs: `APP-SHELL-SELFTEST ok=1 checks=200 failures=0`, `TOOLCHAIN source=verible:bundled slang:bundled missing=[]`, `definitions=3`, `own-xml=[vedit-systemverilog.xml,vedit-verilog.xml]`, `verilog=1`, `fold.wiring.closed.hidden-975 PASS`, loader/libc proof lines present |
| `verify-not-shipped.sh --expect-clean` green | on the extracted AppDir: **pass=8 fail=0 expected_red=0, DELIVERY_BLOCKED=no, exit 0** (KDE XMLs absent by sha/name, no `syntax-bundled/`, whitelist-only definitions, embedded-defs scan PASS, no probe stamp, packaged loader+libc) |
| four-host matrix (L2, headless) | `hostmatrix.sh` rerun by me: **pass=4 fail=0** — centos:7 (glibc 2.17), ubuntu:24.04 (2.39), rockylinux:9 (2.34), ubuntu:26.04 (2.43); each row selfcheck 200/0, `startup_exit=0`, bundled lint exits 1 with the real `Line length exceeds max: 100; is: 108 [line-length]` diagnostic |
| F2 isolation | extracted-AppDir run: pass=5 fail=0; positive `defs=2` whitelist-only; reverse control reproduces the host leak `defs=403` |
| 202-file payload | extracted AppDir file count = 202 = `build/appdir-files.txt` |

## AC-S0.4 (Rust in the package)

`readelf -d bin/vd-app` has **0** `libvd_ffi` NEEDED entries and `nm bin/vd-app` has **44**
`T vd_*` C-ABI symbols → the Rust FFI library is linked statically into the delivered binary.
The same readelf/nm evidence is in `build/15-delivery-invariants.log`.

## glibc ≤ 2.17 (no regression)

- centos:7 (glibc **2.17**) row of the matrix passes selfcheck + startup + bundled lint on the real
  delivered AppImage; the private runtime (packaged loader + libc) is what makes the
  `GLIBC_2.34`-requiring inner binary run on 2.17.
- `70-centos7-probe.log`: CentOS 7.9 userland, glibc 2.17, `APP-SHELL-SELFTEST ok=1 checks=200
  failures=0`, `centos7_smoke_exit=0`.
- Bound (declared in the report and honest): the matrix rows are containers sharing the host kernel
  `7.0.0-38-generic`; a **real CentOS 7 kernel 3.10** host is UNTESTED, and FUSE mount / GUI
  double-click is not exercised (`APPIMAGE_EXTRACT_AND_RUN=1`, headless).

## Other verified claims

- clean-worktree build: `BUILD-QT-EXIT=0` (01-build-qt.log) and I independently ran the ctest in
  `target/wt-55a874d`: **100% tests passed, 0 failed out of 27, CTEST_EXIT=0**.
- full pipeline: `PACK-EXIT=0`, `steps_failed=0` (`02-build-final.log`), same sha/bytes in
  `99-summary.txt`.
- appimagetool: vendored binary sha `793fca0c…` = `scripts/versions.env` pin; build log
  “appimagetool sha256 OK (fail-closed)”.
- declared gate: `mpd_verify_evidence{gate:"typecheck"}` → exit 0, id
  `ev-20261101T084526-177379`.

## Findings (non-blocking; fix before final handoff)

| id | severity | symptom | expected / repair | doc source |
| --- | --- | --- | --- | --- |
| **N1** | MEDIUM | `04-report.md` §4/§6 says `01-build-qt.log` = “clean build + ctest 27/27”, but that log ran `bash scripts/build-qt.sh --no-test` and explicitly says “ctest … were not run”; **no ctest log exists anywhere in `evidence/slice3-pack/`**. The 27/27 number is true — I reran it — but it is not evidenced by the cited file. | Add the clean-worktree ctest log (or cite this verdict's rerun) and correct §4/§6; the report must not cite a log for a gate it does not contain. | `04-report.md:74,101`; `01-build-qt.log:18,1338`; C8 (evidence layer) |
| **N2** | MEDIUM | C9 per-file headers are missing: of 69 files under `evidence/slice3-pack/`, **52 lack `collected_at`+`git_head`** (only `00-C9.txt` anchors the session with time/HEAD/porcelain/baseline; `01-build-qt.log`/`02-build-final.log` carry time+HEAD but no porcelain). C9 requires each evidence file head to carry capture time + `git rev-parse HEAD` + `git status --porcelain`. | Add per-file C9 headers (backfill-labelled where post-hoc), or make the anchor policy explicit in the report and C9-compliant per file. | requirements C9 (line 76); `evidence/slice3-pack/**` |
| **N3** | LOW | the main workspace `packaging/runtime/out/AppDir/bin/vd-app` is a stale phase-1 payload (`61e19acd…`, mtime 15:48) while the delivered AppImage/wt-AppDir carry `b93f8cf4…` (16:32/16:33); `packaging/runtime/README.md` documents `out/AppDir/` as a product. A verifier following the README would inspect the wrong AppDir. | Rebuild/refresh `out/AppDir` from the final tree, or remove it and mark `out/` as “AppImage only” (report says the AppImage is the artifact). | `packaging/runtime/README.md` product table; `out/AppDir` |
| **N4** | INFO | T31 packaging scripts are still workspace changes (the report declares the Lead must commit them); the AppImage is reproducible only from `55a874d` + this exact script state. | Commit the scripts unchanged before triggering the delivery handoff; any later source change invalidates `e7ae067f…`. | `04-report.md` §5; `00-C9.txt` porcelain |
| **N5** | INFO | bounds: real CentOS 7 kernel 3.10, FUSE mount, GUI double-click and the appimagetool network fallback are untested (extraction-mode/headless only). | declared honestly in the report; remains user/L3-level. | `04-report.md` §5; matrix README |

## Not verified / bounds

- Real CentOS 7 kernel (3.10) host, FUSE-mounted double-click, GUI interaction (C6: headless server;
  L2 evidence only), and the appimagetool network fallback — declared as untested, not claimed.
- The four “hosts” are containers sharing the host kernel; the matrix proves userspace/glibc
  compatibility (2.17/2.34/2.39/2.43), not kernel-level behaviour.
- No push/commit/tag was performed by this review; the only writes are
  `evidence/verdicts/pack/{verdict.md,audit.txt}`.

## Release statement

**Packaging acceptance: PASS.** The delivered single-file AppImage is the correct Qt product installed
as `bin/vd-app`, its recorded sha256 is reproducible, it passes the headless selfcheck and the
delivery/not-shipped/F2 gates in four hosts including glibc 2.17, and the Rust FFI is statically
inside the binary. Apply N1–N3 (evidence index/headers, stale `out/AppDir`) and commit the packaging
scripts (N4) before the final delivery handoff; the artifact itself needs no change.
