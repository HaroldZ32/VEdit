# T31 phase 2 — provisional final artifact (baseline `206033c` + required compile fix)

C9 header: see `00-C9.txt` (collected_at + HEAD + `git status --porcelain` + the
clean worktree statuses). The raw build logs in `build/` were produced by the same
run; this report anchors them. **T31 is not completed** — this artifact is
provisional; a final rebuild is required after T109 lands (see §6).

## 1. Artifact

| item | value |
|---|---|
| AppImage | `packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage` (also built in `target/wt-206033c/packaging/runtime/out/`) |
| size | **95,246,840 bytes** (90.8 MiB) |
| sha256 | **`85df9545bf4a93211a435ccf3dd29fabefc17d8ef3208100133a9dfe53d2aa11`** |
| `packaging/runtime/out/SHA256SUMS` | `85df9545…  VEdit-0.1.1-x86_64.AppImage` |
| appimagetool | from `.mpd/recon/appimagetool.AppImage`, sha256 `793fca0c…` = the `versions.env` pin (upstream URL now serves a different binary, see §5) |
| product binary | `vedit-app` built from a clean worktree at `206033c` + T31 packaging; installed as `bin/vd-app` |

## 2. Clean-container selfcheck on the **AppImage** (stock centos:7)

`06-clean-container-appimage.log` — container `docker.m.daocloud.io/library/centos:7`,
no KF6/Qt/verible/slang; it extracts the AppImage with
`APPIMAGE_EXTRACT_AND_RUN=1` (no FUSE) and runs `AppRun --selfcheck`:

```
CHECK PASS  APP-SHELL-SELFTEST ok=1            (200 checks, 0 failures)
CHECK PASS  toolchain source=bundled, missing=[]
CHECK PASS  our syntax definitions loaded (both XMLs, order-insensitive)
CHECK PASS  KTextEditor kernel
CHECK PASS  folded 1k .sv with packaged XML    (fold.wiring.closed.hidden-975)
CHECK PASS  packaged loader in process maps    (/proc/<pid>/maps)
CHECK PASS  no system loader in process maps
CHECK PASS  packaged libc in process maps
SELFCHECK PASS
```

The loader proof quotes the real maps: the packaged
`…/squashfs-root/ld-linux-x86-64.so.2` and `…/lib/libc.so.6` are mapped and
`/lib64/ld-linux-x86-64.so.2` is absent.

## 3. Four-host matrix (containers; honest limits)

`matrix/summary.tsv`, `matrix/*.log`. **All rows are containers and share the
host kernel `7.0.0-38-generic`; a real CentOS 7 machine with kernel 3.10 was NOT
available/tested.**

| host image | container glibc | uname -r | selfcheck | startup | bundled lint | highlight |
|---|---|---|---|---|---|---|
| `docker.m.daocloud.io/library/centos:7` | 2.17 | 7.0.0-38-generic | PASS | PASS | PASS | PASS |
| `docker.m.daocloud.io/library/ubuntu:24.04` | 2.39 | 7.0.0-38-generic | PASS | PASS | PASS | PASS |
| `docker.m.daocloud.io/library/rockylinux:9` | 2.34 | 7.0.0-38-generic | PASS | PASS | PASS | PASS |
| `docker.m.daocloud.io/library/ubuntu:26.04` | 2.43 | 7.0.0-38-generic | PASS | PASS | PASS | PASS |

Per-row details: `--selfcheck` reports `APP-SHELL-SELFTEST ok=1 checks=200
failures=0`, `TOOLCHAIN source=verible:bundled slang:bundled missing=[]`,
`own-xml=[vedit-verilog.xml,vedit-systemverilog.xml] verilog=1`, and
`fold.wiring.closed.hidden-975 PASS` (highlight proof); startup is
`AppRun --hold 300` (offscreen) exit 0; lint is the **bundled**
`verible-verilog-lint` on `crates/vd-lsp/fixtures/project/bad_line_length.sv`,
exit 1 with `Line length exceeds max: 100; is: 108 [Style: line-length]`.
`highlight_lines=3` Verilog edit-core highlight checks pass in each row.

**Do not read this as "four-host verified":** it is a four-container matrix with
a shared modern host kernel; the real CentOS 7 / kernel 3.10 row is UNTESTED.

## 4. What was built from what (provenance — important)

- `HEAD` = `206033c1faa91ac6b89cca0d575d36283ab69d7f` (the baseline the Lead gave;
  local and `origin/feat/qt6-ktexteditor-migration` match).
- The baseline **does not build as committed**: committed `crates/vd-ffi/src/vcs.rs`
  returns `JobValue::VcsInfo` / `JobValue::VcsDiff`, but committed
  `crates/vd-ffi/src/service.rs` has no such variants (`01-build-qt.log`,
  `02-blockers.md`). The Lead's green gates were measured with a 4-line
  uncommitted `service.rs` diff present in the main worktree. To produce an
  artifact at all, that **exact 4-line diff** was applied in the clean worktree
  (`03-diagnostic-patch-check.log`: `cargo check -p vd-ffi` → 0).
- Therefore this artifact = **baseline `206033c` + T31 packaging scripts + the
  required 4-line vd-ffi compile fix**. It is provisional; the fix must be
  committed (or a corrected baseline given) before this can be a formal release.
- The clean worktree (`target/wt-206033c`) isolates this build from the main
  worktree's other in-flight changes (T109 `fold_breadcrumb`/`syntax_directory`,
  etc.); none of those are in the artifact.
- The dual XML layout (T84 data root + a root copy for the pre-T109 fold probe)
  is still present in this provisional artifact; T109's fix allows dropping the
  root copy in the final rebuild.
- `build.sh` was fixed to `cd "$ROOT"`: previously a relative
  `packaging/runtime/build-appimage.sh` call could hash the **caller tree's**
  appimagetool cache (caught in this phase; see `05-build-final.log`).

## 5. appimagetool pin drift (important for the final rebuild)

The pinned URL is the upstream **`continuous`** AppImage. On 2026-11-01 the URL
served a different binary:

```
want 793fca0ce4bcc309dc65d3f66669f58e3755643e4dadbe03932e7b436128494f  (versions.env)
got  ac7fd56feade6002c2f57db39587113275d351fce911a5fbb29ea84cd0666cab  (two downloads, identical size 15,137,272)
```

Per the red line I did **not** change the pin, source, or skip validation. A
hash-matching copy exists at `.mpd/recon/appimagetool.AppImage` (sha = the pin);
that copy built this AppImage. A clean checkout that relies on downloading will
now fail closed. Resolution (the Lead chose the vendor option, see `08-appimagetool-vendor.md`):
the hash-matching binary is committed as
`packaging/appimage/vendor/appimagetool-x86_64` (sha256 `793fca0c…`, 15,104,504 B;
no `.AppImage` suffix so the repo ignore rule does not hide it). The builders
prefer it, keep the fail-closed sha check, and a reproduction run produced a
**byte-identical** AppImage (`85df9545…`). Cache/network remains only a
deliberate-refresh fallback.

## 6. Unverified / pending (not claimed)

- **Real CentOS 7, kernel 3.10: UNTESTED** (no such machine; container matrix
  shares the host kernel `7.0.0-38-generic`).
- **GUI double-click / FUSE mount: not exercised** — extraction uses
  `APPIMAGE_EXTRACT_AND_RUN=1`; no display is used. No screenshot is used as GUI
  proof.
- **Final baseline:** an artifact based on `206033c` + the 4-line fix is not the
  same as a commit; T109's final form + a Lead commit will change the SHA and
  must trigger a rebuild with the 4-line fix (or corrected baseline), removal of
  the dual XML root copy, and a fresh `SHA256SUMS`/matrix.
- `appimagetool` upstream fetch path is currently unreliable (pin drift), see §5.
- No commit/push; no tag/release.

## 7. Evidence index

| file | content |
|---|---|
| `00-C9.txt` | C9 header for this run (HEAD, porcelain, worktree, final sha) |
| `01-build-qt.log` | clean-worktree build failure on the unmodified baseline (4 errors) |
| `02-blockers.md` | baseline inconsistency + appimagetool sha mismatch, filed before proceeding |
| `03-diagnostic-patch-check.log` | baseline + 4-line fix → `cargo check -p vd-ffi` 0 |
| `04-build-qt-patched.log` | `build-qt.sh --no-test` with the fix: PASS |
| `05-build-final.log` | full `build.sh` run → `steps_failed=0`, `BUILD: F2 runtime OK` |
| `06-clean-container-appimage.log` | stock centos:7 selfcheck on the AppImage (summary + maps) |
| `matrix/summary.tsv`, `matrix/*.log`, `matrix/README.txt` | four-container matrix + caveat |
| `07-final-report.md` | this report |
| `build/SHA256SUMS` + `build/*-*.log` | raw packaging logs (same run) |
