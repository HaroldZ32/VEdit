# C9 backfill (T112/N2): header added after the fact; the body below is unchanged.
# original_collected_at_utc: 2026-11-01T09:22:44Z   (from the archived file mtime)
# original_git_head: 55a874da076d605c6b620e0bb20f9ac5932d5f46
# original_git_status_porcelain_sha256: 00b8d8a0956773d9246d1650a785ddf13c1b167806b44d77d7c9cb9003652e9b (hash of the porcelain block archived in final-55a874d/00-C9.txt)
# session_anchor: final-55a874d/00-C9.txt
# backfill_collected_at_utc: 2026-11-01T09:23:35Z
# backfill_git_head: 0c8ea9fe4ae18c386a59a55b8868f837f4f80a84
# backfill_git_status_porcelain_sha256: a7feab0230532310375511329de129cad97d49e690c7a9c7336394a1cce151fa (current tree at backfill time)
# T31 FINAL — AppImage built from baseline `55a874d`

C9 header: `00-C9.txt` (collected_at + HEAD + porcelain + the clean worktree).
This report is written against **baseline `55a874da076d605c6b620e0bb20f9ac5932d5f46`**.
The product was built in a clean worktree at that commit; only the T31 packaging
scripts (workspace paths below) were overlaid. The root-level XML duplicate is
**removed** (T84/T110 layout: one copy under `<root>/syntax/`).

## 1. Final artifact

| item | value |
|---|---|
| path | `packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage` |
| size | **95,349,240 bytes** |
| sha256 | **`e7ae067fe66c8e271ba72da974795a7ee3db0efa59bda45079b5a048b4a09558`** |
| built from | **`55a874da076d605c6b620e0bb20f9ac5932d5f46`** (`55a874d`) |
| `SHA256SUMS` | `e7ae067fe66c8e271ba72da974795a7ee3db0efa59bda45079b5a048b4a09558  VEdit-0.1.1-x86_64.AppImage` |
| appimagetool | vendored `packaging/appimage/vendor/appimagetool-x86_64`, sha256 `793fca0c…` (pin), fail-closed |
| AppDir | 202 files (down from 204 with the duplicate root XMLs), white list `SystemVerilog,Verilog` |

Any source change after `55a874d` invalidates this SHA; rebuilt only from that tree
plus these packaging scripts.

## 2. Clean-container selfcheck on the AppImage (stock centos:7)

`03-clean-container.log`, container has **no KF6/Qt/verible/slang**; extracts with
`APPIMAGE_EXTRACT_AND_RUN=1` and runs `AppRun --selfcheck`:

```
syntax-product: grammar=verilog backend=1 registered=[systemverilog,verilog] active='verilog'
  backend-available=1 private-dir='/tmp/…/squashfs-root/usr/share/org.kde.syntax-highlighting'
  definitions=3 own-xml=[vedit-systemverilog.xml,vedit-verilog.xml] themes=30
  verilog=1 systemverilog=1 kernel='KTextEditor 6.24.0' embargoed=[] startup-hook=1
CHECK PASS  APP-SHELL-SELFTEST ok=1               (200 checks, 0 failures)
CHECK PASS  toolchain source=bundled, missing=[]
CHECK PASS  our syntax definitions loaded (both XMLs, order-insensitive)
CHECK PASS  KTextEditor kernel
CHECK PASS  folded 1k .sv with packaged XML
CHECK PASS  packaged loader in process maps
CHECK PASS  no system loader in process maps
CHECK PASS  packaged libc in process maps
SELFCHECK PASS
```

**The `own-xml` line still lists both XMLs and `verilog=1` with the root copy
gone** — the T84/T109/T110 path logic is verified in the delivered layout.

The loader proof is the three `/proc/<pid>/maps` checks above: the packaged
`ld-linux-x86-64.so.2` and `libc.so.6` are mapped, the system loader is absent.

## 3. Four-container matrix on this AppImage

`matrix/summary.tsv`, `matrix/*.log`:

| host image | glibc | uname -r | selfcheck | startup | bundled lint | highlight |
|---|---|---|---|---|---|---|
| centos:7 | 2.17 | 7.0.0-38-generic | PASS | PASS | PASS | PASS |
| ubuntu:24.04 | 2.39 | 7.0.0-38-generic | PASS | PASS | PASS | PASS |
| rockylinux:9 | 2.34 | 7.0.0-38-generic | PASS | PASS | PASS | PASS |
| ubuntu:26.04 | 2.43 | 7.0.0-38-generic | PASS | PASS | PASS | PASS |

Each row: `APP-SHELL-SELFTEST ok=1` (200/0), bundled `TOOLCHAIN`, own XMLs +
`verilog=1`, `fold.wiring.closed.hidden-975 PASS`, `AppRun --hold 300` exit 0,
and the bundled `verible-verilog-lint` on the line-length fixture exits 1 with
`Line length exceeds max: 100; is: 108 [line-length]`.

**Honest limit: all rows are containers sharing the host kernel
`7.0.0-38-generic`; a real CentOS 7 (kernel 3.10) host is UNTESTED.** Do not read
this as four-real-host verification.

## 4. Build/gate evidence

- clean worktree `target/wt-55a874d` = `55a874d` + T31 packaging scripts (listed
  in C9). Build: `BUILD-QT-EXIT=0` — the build log (`01-build-qt.log`) is a
  `--no-test` build and does **not** contain a ctest run. The ctest gate is the
  separate, real run `09-clean-ctest-55a874d.log` on the same clean tree:
  **27/27 passed** (`CTEST-EXIT=0`).
- `build.sh` run: `PACK-EXIT=0`, `steps_failed=0`, `BUILD: F2 runtime OK`;
  AppImage step, not-shipped (`pass=8 fail=0 DELIVERY_BLOCKED=no`), F2 isolation
  (`pass=5 fail=0`, reverse control `defs=403`), AppDir selfcheck and AppImage
  selfcheck all green (`02-build-final.log` + `build/`).
- appimagetool: vendored hash-matching binary, sha check fail-closed; a
  reproduction with it produced the same bytes (`08-appimagetool-vendor.md` in
  the provisional phase).

## 5. Unverified / caveats (not claimed)

- **Real CentOS 7 / kernel 3.10: UNTESTED** (no such machine; containers share the
  7.0.0 host kernel).
- GUI double-click / FUSE mount: not exercised; extraction uses
  `APPIMAGE_EXTRACT_AND_RUN=1` and no display. No screenshots are used as GUI proof.
- appimagetool upstream `continuous` download is not reproducible (pin drift
  measured); the vendored copy is the supported path; the network fallback remains
  fail-closed and untested against the current upstream.
- The final T31 packaging scripts are still workspace changes on top of `55a874d`
  (Lead must commit them); any later source change requires a rebuild.

## 6. Evidence index

| file | content |
|---|---|
| `00-C9.txt` | C9 header, baseline + final sha |
| `01-build-qt.log` | clean build **`--no-test`** (no ctest evidence inside) |
| `09-clean-ctest-55a874d.log` | separate real ctest on the same clean tree: **27/27 passed**, `CTEST-EXIT=0` |
| `02-build-final.log` | full packaging run (steps_failed=0) |
| `03-clean-container.log` | stock centos:7 selfcheck on the AppImage |
| `04-report.md` | this report |
| `matrix/summary.tsv`, `matrix/*.log` | four-container matrix + caveat |
| `build/SHA256SUMS`, `build/*.log` | raw packaging logs from the same run |
