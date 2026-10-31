generated: 2026-10-31T11:27:15Z
git_head: 95c496e682e3841add1a081edd78d29c34957aac
git_status_porcelain:
 M .gitignore
 M THIRD_PARTY_NOTICES.md
 M build/evidence/12-gates-image-build.log
 M build/gates/gates.sh
 M build/gates/pack.sh
 M build/gates/typecheck.sh
 M build/slang/evidence/00-build-console.log
 M build/slang/evidence/01-yum-install.log
 M build/slang/evidence/02-toolchain.txt
 M build/slang/evidence/02b-python-build.log
 M build/slang/evidence/02c-python.txt
 M build/slang/evidence/03-source.txt
 M build/slang/evidence/03b-patches.txt
 M build/slang/evidence/04-configure.log
 M build/slang/evidence/05-deps.txt
 M build/slang/evidence/05-elf-check.txt
 M build/slang/evidence/06-build.log
 M build/slang/evidence/06-offline-console.log
 M build/slang/evidence/07-artifact.txt
 M build/slang/evidence/08-timings.txt
 M build/slang/evidence/09-offline-smoke.txt
 M crates/vd-config/tests/project_model_fixture.rs
 M crates/vd-ffi/include/vd_ffi.h
 M crates/vd-ffi/src/lsp.rs
 M crates/vd-lsp/src/session.rs
 M crates/vd-lsp/tests/session_degradation.rs
 M crates/vd-lsp/tests/session_file_set.rs
 M docs/USER_GUIDE.md
 M docs/handover/appendix/amendments-rev2.md
 M evidence/docs-amendments/00-context.txt
 M evidence/docs-amendments/02-sha256-check.txt
 M evidence/docs-amendments/verify.sh
 M evidence/ffi-abi/logs/10-rust-image.txt
 M evidence/ffi-abi/logs/19-rust-stage-console.log
 M evidence/ffi-abi/logs/20-cargo-build-release.log
 M evidence/ffi-abi/logs/21-staticlib.txt
 M evidence/ffi-abi/logs/23-cargo-test.log
 M evidence/ffi-abi/logs/26-vd_ffi.h
 M evidence/ffi-abi/logs/27-vd_ffi-comment-free.txt
 M evidence/m0-build/build-logs/20-qt6-image-build.log
 M evidence/m0-build/build-logs/21-qt6-image-id.txt
 M evidence/m0-build/build-logs/31-qt6-configure.log
 M evidence/m0-build/build-logs/32-qt6-build.log
 M evidence/m0-build/build-logs/33-qt6-ctest.log
 M evidence/m0-build/build-logs/34-qt6-elf.txt
 M evidence/m0-build/build-logs/37-qt6-rust-abi.txt
 M evidence/m0-ksh-selfbuild/build-logs/10-configure.log
 M evidence/m0-ksh-selfbuild/build-logs/11-build.log
 M evidence/m0-ksh-selfbuild/build-logs/12-install.log
 M evidence/m0-ksh-selfbuild/build-logs/21-library-identity.txt
 M evidence/m0-ksh-selfbuild/build-logs/32-abi-loading.txt
 M evidence/m0-ksh-selfbuild/build-logs/40-smoke-configure.log
 M evidence/m0-ksh-selfbuild/build-logs/41-smoke-build.log
 M evidence/m0-runtime/runtime-manifest.txt
 M evidence/syn-kde-data/03-folding.log
 M evidence/syn-kde-data/04-licensing.txt
 M evidence/syn-kde-data/06-conclusion.md
 M packaging/appimage/clean-room-accept.sh
 M packaging/runtime/assemble-appdir.sh
 M packaging/runtime/build.sh
 M packaging/runtime/verify-not-shipped.sh
 M packaging/tarball/stage.sh
 M resources/syntax/vedit-systemverilog.xml
 M resources/syntax/vedit-verilog.xml
 M scripts/build-ksh-selfbuild-in-container.sh
 M scripts/build-qt-in-container.sh
 M src/app/CMakeLists.txt
 M src/app/editor_area.cpp
 M src/app/editor_area.h
 M src/app/editor_provider.cpp
 M src/app/editor_provider.h
 M src/app/main.cpp
 M src/terminal/pty_session.cpp
 M src/terminal/pty_session.h
 M src/workspace/filetree/filetree_integration.cpp
 M src/workspace/filetree/tools/rootcheck/CMakeLists.txt
 M src/workspace/filetree/tools/rootcheck/seamcheck.cpp
 M src/workspace/integration.cpp
 M src/workspace/palette/CMakeLists.txt
 M src/workspace/palette/palette_integration.cpp
 M src/workspace/palette/palette_integration_selftest.cpp
 M src/workspace/search/CMakeLists.txt
 M src/workspace/search/README.md
 M src/workspace/search/search_integration.cpp
 M src/workspace/search/search_panel.h
 M src/workspace/search/search_selftest.cpp
 M src/workspace/search/search_types.h
?? build/evidence/typecheck/
?? evidence/build-infra/
?? evidence/edit-core-regression/
?? evidence/edit-core-v2/
?? evidence/evidence/
?? evidence/fix-search-jump/
?? evidence/gate-infra/
?? evidence/highlight-v2/00-collect.txt
?? evidence/highlight-v2/01-after-link.txt
?? evidence/highlight-v2/03-build-qt-gate.log
?? evidence/highlight-v2/04-syntax-product-tests.log
?? evidence/highlight-v2/06-standalone-lanes.log
?? evidence/highlight-v2/07-negative-control.log
?? evidence/highlight-v2/08-ctest.log
?? evidence/highlight-v2/09-screenshot-L2.log
?? evidence/highlight-v2/10-licensing-and-embargo.txt
?? evidence/highlight-v2/README.md
?? evidence/highlight-v2/screenshots/
?? evidence/licensing-hash-sync/
?? evidence/lsp-v2/
?? evidence/project-model/
?? evidence/repo-hygiene/
?? evidence/syn-kde-data/10-folding-before.log
?? evidence/syn-kde-data/11-folding-after.log
?? evidence/syn-kde-data/12-regression-after-xml.log
?? evidence/syn-kde-data/FOLDING-fix-T45.md
?? evidence/syn-kde-data/f2-build.log
?? evidence/syn-kde-data/folding-probe/
?? evidence/verdicts/edit-core/
?? evidence/verdicts/evidence/
?? evidence/verdicts/fix-filetree-open/
?? evidence/verdicts/fix-search-jump/
?? evidence/verdicts/fix-xml-folding/
?? evidence/verdicts/lsp/
?? evidence/verdicts/project-model/
?? evidence/verdicts/syntax/
?? src/editor/CMakeLists.txt
?? src/editor/kernel/
?? src/workspace/diagnostics/
?? src/workspace/filetree/tools/generate-testdata.sh
?? src/workspace/project/
?? src/workspace/search/search_jump_target.cpp
?? src/workspace/search/search_jump_target.h
---
# licensing provenance sync — T45 hashes, retest wording, USER_GUIDE root notes (T62)

Source: the T46 review's M1 (stale hash registration) and M2 (retest wording), plus
the captain's follow-up holding the folding counts until T66 re-records them.

## 1. M1 — registrations now carry the post-T45 values

Recomputed on this host with `sha256sum` / `wc -l` / `stat -c%s` (never copied from
the task text); both files match the dispatch's numbers:

| file | sha256 | lines / bytes |
| --- | --- | --- |
| `resources/syntax/vedit-verilog.xml` | `c6adc763c7b280d7493f48d06e5779d0b1e2068ac82f75fe7f31a07b0788a6c2` | 85 / 5,111 |
| `resources/syntax/vedit-systemverilog.xml` | `2abba3342d0878e71b99d358d96e510f857cd7f232786951be8602f513e0b992` | 90 / 5,492 |

Updated **current registrations** (each with a one-line change record saying the hash
changed after T45 rewrote the folding rules, 2026-10-31):

- `THIRD_PARTY_NOTICES.md` §9.1 rows `行数 / 字节` and `sha256`, plus a change-note
  block below the table.
- `docs/handover/appendix/amendments-rev2.md` §1 item 2 (C14 data line), §2 table
  rows `行数 / 字节` and `sha256（C14；T45 重写后）`, a change-note block, and §2
  item 3 (which used to claim identity with the old `02-sha256-check.txt`).

Kept as **historical values, not rewritten** (a note pointing at the current values
was appended to each):

| file | why it keeps the old values |
| --- | --- |
| `evidence/highlight-v2/10-licensing-and-embargo.txt` | capture-time snapshot of the tree |
| `evidence/syn-kde-data/04-licensing.txt` | T33 capture |
| `evidence/docs-amendments/00-context.txt` | pre-edit baseline hashes |
| `evidence/docs-amendments/02-sha256-check.txt` | historical CHECK log (`pass=33`) |
| `evidence/m0-runtime/runtime-manifest.txt` | M0 packaged snapshot (built before T45) |
| `evidence/docs-amendments/verify.sh` | pins the NEW source values and sizes; keeps the OLD values only in the check of the historical packaged manifest, with a comment explaining why |

`evidence/verdicts/**` (syntax, docs, fix-xml-folding) is deliberately untouched:
a verdict is a frozen audit record of what was true when it was written. Rewriting
one would destroy the trail; the current values live in `THIRD_PARTY_NOTICES.md` §9.1.

The sweep and its per-hit classification: `02-grep-sweep.txt`. The current-value
registration and the docs gate: `03-notices-and-gate.txt`.

## 2. M2 — "retested" wording, counts deliberately not written

Per the captain's follow-up (T45's 10/11 logs were shown to have linked the DISTRO
library through a bad F2 path derivation, and the re-record is T66's job), the
amendments now say:

> 已在 F2 交付库（`QRC_SYNTAX=OFF`）上重测；探针断言 `author=VEdit / section=Hardware`。
> Verilog 与 SystemVerilog 均能产出折叠区。具体计数与 `ldd` 库指向证明见
> `evidence/syn-kde-data/`（原 `10`/`11` 日志已被证明测的是 DISTRO 库，由重录版取代；
> **数字以重录版为准**）。

No folding counts are asserted in this task's documents. `A4.5` (the view-level
criterion) is still recorded as unmet — the unit-level probe is only a positive
control per §4.8, and its view-level evidence must be re-collected after the
re-record. The documents say so explicitly, so this task did not turn M2 into an
"AC-S3.5 done" claim.

Note on the 10/11 logs: at the time of this task they did **not** yet carry the
correction banner (T66 has not landed); the documents therefore say only that those
logs measured the DISTRO library and are superseded by the re-record — they do not
claim a banner exists.

## 3. USER_GUIDE additions (same write scope)

`docs/USER_GUIDE.md` §2 now also states:

- the project-root start differs per frontend: the legacy GTK3 frontend
  (`crates/vd-app`) starts from the **process working directory**
  (`std::env::current_dir()`, `crates/vd-app/src/app.rs:1475-1480`), while the Qt6
  product starts from the **directory of the opened file**
  (`crates/vd-config/src/lib.rs:93-103`, `Loader::new(start_dir)` says start_dir is
  not the process working directory);
- the `*.f` marker is case-insensitive: `.F` counts as a filelist too
  (`crates/vd-config/src/project.rs:267-271`, `eq_ignore_ascii_case("f")`).

## 4. Verification (observed)

- `bash build/gates/docs.sh` → `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`, exit 0.
- `bash evidence/docs-amendments/verify.sh` → `pass=33 fail=0`, exit 0 (the script's
  source checks now pin the recomputed values).
- `grep -rln '3d1ec41f\|dc6e0a47' …` → every remaining hit is either a labelled
  historical snapshot, a change-note line, a reviewer verdict, or a packaged/binary
  artefact outside this write scope; no current registration still pins the old value.

## 5. Scope

Changed: `THIRD_PARTY_NOTICES.md`, `docs/handover/appendix/amendments-rev2.md`,
`docs/USER_GUIDE.md`, the five annotated evidence snapshots, and
`evidence/docs-amendments/verify.sh` (evidence harness; not a gate). Not touched:
`resources/syntax/*.xml`, product code, tests/gates, `requirements.md`,
`evidence/verdicts/**`. No commit and no push (the Lead commits).
