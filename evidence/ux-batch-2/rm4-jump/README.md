# RM-4 — interface-panel / module-tree jumps refused (T5)

Real-machine finding: **every** double-click and Ctrl+double-click in the interface panel
reports `编辑器拒绝了跳转到 <file>:<line> 的请求`, on a two-file project that loads normally.
The module tree is refused at the same moment (T5 lead, user-confirmed fact 2).

> **STATUS (superseded by T28): the root cause is PROVEN, not a hypothesis.**
> `slang` reports `source_file` **relative to its own cwd** (the parent of the first
> source file); the catalog resolved that relative path against the **folder root**, so
> the subdirectory segment (`rtl/`) was dropped and the editor was asked to open a file
> that does not exist — which is why **every** jump in **both** panels was refused with
> `cannot open <root>/child.sv: not a regular file`. Both fixes landed in the **producer**:
> `crates/vd-deeplint/src/tool_paths.rs` (resolve against the tool's real cwd) and its
> UTF-16 column conversion; the lint path was fixed the same way. Red→green: nested
> fixture 25 checks / 9 failures → 25/0; multi-directory (`rtl/` + `tb/`) 11/30 → 30/0.
> See `../rm4b-columns/README.md` and its `logs/02-…`, `03-…`, `05-…`. The sections below
> are the earlier reconnaissance and the diagnostic-only work, kept because they record
> what ruled out the other candidates.


This directory is the evidence for what was measured, what was fixed, and what is still
**not** proven. Nothing here was produced with a fake jump target: the probe links the
product's own static libraries and installs the real `search::EditorJumpTarget` through
`ApplicationShell` + `installPanels()` — the same call `src/app/main.cpp` makes.

---

## 1. The refusal chain (who says what, and where)

| # | user-visible sentence | source | fires when |
|---|---|---|---|
| 1 | `%1: no jump target is registered …` | `src/workspace/module_tree/interface_panel.cpp:361-367` | `JumpStatus::NoTarget` |
| 2 | `%1: the editor rejected the jump to %2` | `src/workspace/module_tree/interface_panel.cpp:368-379` | `JumpStatus::Rejected` |
| 3 | `%1: the jump request (%2) was refused` | `src/workspace/module_tree/interface_panel.cpp:380-387` | `JumpStatus::InvalidRequest` |

The user hits **#2**. `JumpStatus::Rejected` is set in exactly one place —
`src/workspace/jump/jump_registry.cpp:127-141` — when the registered `IJumpTarget::jumpTo()`
returns `false`. The registered target is the product's ONE target,
`search::EditorJumpTarget` (`src/workspace/search/search_jump_target.cpp`), installed by
`installSearch()` (`src/workspace/search/search_integration.cpp:29-35`).

Inside the target (`search_jump_target.cpp:36-189`) a refusal can only be one of:

| check | message it produces |
|---|---|
| no shell / invalid request | `no application shell` / `invalid jump request: …` |
| `!VEDIT_SEARCH_HAVE_EDITOR_CORE` | `no edit-core provider: …` (string is **absent** from the shipped binary — the macro is defined) |
| no active pane | `the shell has no active editor pane` |
| `EditorPane::openFile()` returned false | `cannot open <path>: not a regular file` / `cannot read …` / `… looks like a binary file` / `setText() failed` |
| pane widget is not a KTextEditor view | `the active editor pane is not a KTextEditor view …` |
| `setCursorPosition` returned false | `setCursorPosition(%1:%2) failed` |
| cursor did not land where asked | `the cursor landed at %1:%2 instead of %3:%4` |

**The panel used to throw that sentence away.** `deliver()` printed only its own
`the editor rejected the jump to X`, so every distinct cause looked identical to the user —
and to us. That is fixed (§4.1).

**Message #3 cannot fire from this panel** (checked, not assumed): `deliver()` first requires
`ModuleJump::isValid()` (`file` non-empty **and** `line >= 1`, `module_catalog.h:86`), and
`jumpFromPosition()` (`module_catalog.cpp:76-86`) always produces `column >= 1`. The
registry's `column < 1` pre-check therefore cannot trip from `jumpFromPort()` /
`jumpFromInstance()`. It is a real status for other callers, but from this panel it is
dead code. (`module_tree_panel.cpp:378` has the same dead branch.)

---

## 2. What was MEASURED on a normal two-file project (no repro)

`probe/rm4_probe.cpp`, built from `vedit-app`'s own compile/link lines
(`probe/build.sh`), offscreen and under Xvfb/xcb, with the full product wiring
(`installPanels()`).

### Gesture 1 — double-click → port declaration

```
PASSED   : .../fixture/child.sv:1:34 (status=delivered)
RECEIVED : .../fixture/child.sv:1:34
SEAM-ERR : <none>
RESOLVED : .../fixture/child.sv cursor=1:34
PANEL    : port clk declaration → child.sv:1:34
ok   cursor-on-identifier — identifier-under-cursor='clk'
```

### Gesture 2 — Ctrl+double-click → instantiation statement

```
PASSED   : .../fixture/top.sv:3:9 (status=delivered)
RECEIVED : .../fixture/top.sv:3:9
RESOLVED : .../fixture/top.sv cursor=3:9
ok   cursor-on-identifier — identifier-under-cursor='u_child' column=9
```

The ABI points at the **instance name** (`u_child`), not the module name, on the
instantiation line that already carries the `child u_child (` statement — the correct place,
and the check states it that way instead of pretending otherwise.

### Reproduction matrix — all NEGATIVE

| scenario (all with the real target) | result | log |
|---|---|---|
| fresh two-file project, first run | both gestures delivered | `logs/02-product-first-run.out` |
| second run: persisted `project-model.json` + session restore | delivered | `logs/03-product-second-run.out` |
| non-ASCII project path (`…/中文工程`) | delivered | `logs/04-nonascii-path.out` |
| Xvfb + real `xcb` platform plugin | delivered | `logs/05-xcb-run.out` |
| process cwd = `/` (AppImage-from-file-manager shape) | delivered | `logs/05b-cwd-root.out` |
| **restore project A (with a document open) → File▸Open Folder B → jump in B** | delivered; the pane still showed `fixture-a/alpha.sv` before the first jump | `logs/13-after-restore-switch.out` |
| **cwd = `/tmp` (= `$HOME`), bare launch → File▸Open Folder, BOTH panels** | delivered; the catalog handed **absolute** paths | `logs/20-case1-folder-cwd.out` |
| **cwd = `/`, bare launch → File▸Open Folder, BOTH panels** | delivered; absolute paths | `logs/20-case1-folder-cwd.out` |
| **real catalog from a `project-model.json` whose `files` are RELATIVE** (`["child.sv","top.sv"]`, `VEDIT_DEBUG` proves the model was the source), cwd=`/tmp` | delivered; slang still reports **absolute** paths | `logs/21-case2-before.out`, `logs/22-cases-after.out` |
| **sources in a SUBDIRECTORY of the opened folder (`rtl/`)** — the RM-4 root cause, fixed in T28 | RED 25/9 → GREEN 25/0, both panels | `../rm4b-columns/logs/02-red-path-bug.out`, `03-green-cases.out` |
| product acceptance test `vedit-module-tree-catalog-test` (real seam) | `acceptance.vi.declaration-cursor` / `.instantiation-cursor` PASS | `logs/23-after-suites.out` |

⇒ The lead's "the pane keeps the OLD project's root binding" hypothesis is **killed**: the
restore→switch sequence delivers, and the panes' root is display-only
(`EditorArea::setDisplayRoot`, `editor_area.cpp:2049-2058`).

---

## 3. Two defects that WERE found and fixed

### 3.1 A relative catalog path was resolved against the process cwd

`search_jump_target.cpp` passed the panel's path straight to `EditorPane::openFile()`, which
does `QFileInfo(path).absoluteFilePath()` — i.e. **relative paths are resolved against the
process working directory**. An AppImage started from a file manager runs in `$HOME`.

Observed BEFORE (real target, real panel path, `logs/10-before-position-path.out`):

```
FAIL rm4.seam.relative-path.delivered — cannot open /vedit-build/child.sv: not a regular file
```

…which is exactly the refusal class the user reports (`Rejected`, panel message #2, every
jump). After the fix the path is resolved under the **workspace root**:

```
RELATIVE   : requested='child.sv' delivered=1 opened='…/fixture/child.sv'
ok   rm4.seam.relative-path.under-workspace-root
```

An **absolute** path that does not exist is deliberately *not* re-rooted (renaming it into
the workspace would be a guess, and a wrong-but-successful jump is worse than a refusal); the
refusal names the path **and** the workspace root instead.

### 3.2 The catalog's column was a UTF-8 BYTE column → the cursor landed on the wrong place

> **CORRECTED AFTER T28 (this note replaces an overclaim).** The original text below said
> "UTF-8 byte columns are handled" — that was **wrong**: it converted a byte column only when
> it OVERFLOWED the line. The reviewer's fixture (a SHORT CJK prefix on a LONG line: byte 47,
> character 43, line 171) stayed wrong, and the jump was delivered four columns past `clk`.
> The conversion has been **moved to the PRODUCER** (`crates/vd-deeplint::tool_paths::Columns`,
> applied in `catalog.rs::catalog_position`) and the seam no longer guesses a unit at all. The
> full red→green and the boundary rules are in `../rm4b-columns/README.md` §2.

`fixture-c/child.sv` puts CJK text before the port:

```
module child (/* 时钟输入 */ input  logic       clk,
```

The catalog reported `clk` at **column 53** (bytes); the KTextEditor cursor column is 45
(characters). The seam's overflow-only heuristic clamped `53` to the end of the line:

```
BEFORE: PASSED …:1:53 → RESOLVED cursor=1:49 → identifier-under-cursor=''   (FAIL)
AFTER : PASSED …:1:53 → RESOLVED cursor=1:45 → identifier-under-cursor='clk' (PASS)
```

That green was real but **incomplete** (it only covers the overflow case). The producer-side
conversion now makes both the overflow case and the reviewer's short-prefix case correct for
every consumer (interface panel, module tree, and the next catalog consumer), and the seam
only clamps.

## 3.3 The refusal now carries WHAT and WHY (the RM-4 reporting defect)

`IJumpTarget` gained `virtual QString lastError() const` (default empty, additive),
`JumpResult` gained `reason` (`jump_registry.h`), the registry fills it for every status, and
the panel prints it:

```
BEFORE: port p declaration: the editor rejected the jump to ghost.sv:1:1
AFTER : port p declaration: the editor rejected the jump to ghost.sv:1:1 — cannot open
        /nonexistent-rm4/ghost.sv: not a regular file [workspace root: …/fixture]
```

A genuine refusal is still a refusal (nothing retried, nothing swallowed), and the refusal
now names the path **and** the root.

`logs/11-before-panel-reason.out` is the failing-before half (`panel-carries-the-reason`
FAILS); `logs/12-after-project-and-seam.out` is the passing-after half.

---

## 3.4 Root cause or unknown — the T25 verdict

T25 asked for the user's runtime condition specifically: **the process cwd is not the
project directory** (AppImage / AppDir launched from a file manager, then File▸Open Folder,
after the previous project was restored). That condition was exercised directly, with the
module-tree gesture added so BOTH panels are measured.

### Case 1 — cwd ≠ project root, Open Folder, BOTH panels → NO refusal (fixed code)

`folder` mode: a bare launch (no project), then the same public seam the Open Folder dialog
uses (`ApplicationShell::setProjectRoot`). Both panels, real mouse double clicks.

```
cwd=/tmp shellRoot(initial)=<none>
== folder: no project -> '/vedit-build/.../fixture' (cwd='/tmp')
PASSED      : /vedit-build/.../fixture/child.sv:1:34 (status=delivered)   <- interface panel
RESOLVED    : /vedit-build/.../fixture/child.sv cursor=1:34
PASSED      : /vedit-build/.../fixture/top.sv:3:9   (status=delivered)   <- Ctrl+double-click
TREE PASSED : /vedit-build/.../fixture/child.sv:1:8 (status=delivered)   <- module tree
checks=25 failures=0
```

Identical at `cwd=/`. **The catalog hands ABSOLUTE paths** in both runs — that is the datum
that matters: with absolute paths, the cwd cannot influence the jump, which is why all 12
earlier scenarios delivered.

### Case 2 — force a relative catalog path from a LEGITIMATE seam → catalog still normalises

A persisted `project-model.json` is a legitimate input (the catalog client prefers it). It
was given `"files": ["child.sv", "top.sv"]` with an absolute `projectRoot`, and
`VEDIT_DEBUG=1` proves the model was the source:

```
module-catalog: run source='project model (project-model.json)' files=2 kept=2 root='…/fixture'
PASSED      : /vedit-build/…/fixture/child.sv:1:34 (status=delivered)   <- both panels
checks=25 failures=0
```

**So the product's own catalog producer never emits a relative `file`**: even a relative
file SET becomes absolute (slang resolves the paths it is given). Mechanism (a) cannot be
triggered through this seam today.

### Case 2b — the SAME mechanism at the seam contract, pre-fix vs post-fix (proven)

The panel forwards whatever the catalog carries, so the seam contract is tested directly with
a hand-built catalog whose `file` is `child.sv` (a legitimate hand-off — the seam must not
assume the producer's spelling). cwd = `/tmp`:

| | observed |
|---|---|
| **pre-fix** (`logs/21-case2-before.out`) | `RELATIVE: requested='child.sv' delivered=0 error='cannot open /tmp/child.sv: not a regular file'` — panel: `port clk declaration: the editor rejected the jump to child.sv:1:1 — cannot open /tmp/child.sv: not a regular file` → **the refusal, exactly the reported shape** |
| **post-fix** (`logs/22-cases-after.out`) | `RELATIVE: requested='child.sv' delivered=1 opened='…/fixture/child.sv'`; panel: `port clk declaration → child.sv:1:1` |

That is a real, repeatable failing-before / passing-after pair for mechanism (a) — but it is
a **seam-contract** reproduction, NOT the user's: case 2 shows the product cannot hand that
path over. It is therefore recorded as defence in depth and as the regression that keeps the
seam honest, **not** as the RM-4 root cause.

### Case 3 — CJK text before the identifier (byte column), BOTH gestures

`fixture-c`, logs `logs/10-before-position-path.out` vs `logs/22-cases-after.out`:

| | double-click (declaration) | Ctrl+double-click |
|---|---|---|
| pre-fix | cursor `1:49`, `identifier-under-cursor=''` **FAIL** | correct line, identifier `u_child` |
| post-fix | cursor `1:45`, `identifier-under-cursor='clk'` **PASS** | unchanged PASS |

Pre-fix the catalog's column 53 (bytes) was clamped to the end of the line; a wrong-but-
successful jump, never a refusal.

### Case 4 — before/after matrix

| case | pre-fix | post-fix | log |
|---|---|---|---|
| 1: cwd=HOME / bare launch / both panels | delivered (absolute paths) | delivered | `logs/20`, `logs/22` |
| 2: real catalog, relative file set | delivered (catalog normalises) | delivered | `logs/21`, `logs/22` |
| 2b: seam handed a relative path | **REFUSED** (`cannot open /tmp/child.sv`) | delivered under the root | `logs/21`, `logs/22` |
| 3: CJK prefix → byte column | wrong column (49, empty identifier) | correct column (45, `clk`) | `logs/10`, `logs/22` |
| 4: refusal message | reason discarded | path + workspace root + reason | `logs/11`, `logs/22` |

### Decision: UNKNOWN root cause, COMPLETE diagnostic

**No case reproduces the user's refusal through the product's own code path**, so no root
cause is claimed and none is invented. What is now guaranteed instead:

1. The two mechanisms are fixed (§3.1, §3.2) — both were genuine, both are proven by a
   failing-before/passing-after pair.
2. The diagnostic is complete and TESTED (a test on the message, not on prose —
   `logs/22-cases-after.out`, checks `message-names-the-full-path`,
   `message-names-the-workspace-root`, `message-names-the-reason`; plus the registry
   contract `reason.*` in the jump lane's ctest, `SUMMARY checks=53 failures=0`). A refusal
   now prints, in the panel's own line: the **full path** the editor was asked for, the
   **workspace root**, and the **seam's reason**.
3. The registry-level reason contract is now covered by the lane's ctest
   (`src/workspace/jump/tests/jump_tests.cpp`, id `vedit-jump-tests`): a target's sentence
   survives, a silent target gets a default sentence, NoTarget/InvalidRequest explain
   themselves, a delivered jump has an empty reason.

**What the user should paste back** (one line, from the panel's status line after one refused
double-click — it now names its own cause):

> `port <name> declaration: the editor rejected the jump to <file>:<line>:<col> — <reason> [workspace root: <root>]`

The `<reason>` decides the next step:

| reason text | where the next fix lives |
|---|---|
| `cannot open <path>: not a regular file [workspace root: …]` | the path producer: `catalog_client.cpp::absoluteSourcePath` / the persisted project-model file set (`module_catalog.*` lane) |
| `the active editor pane is not a KTextEditor view …` | the editor provider / katepart plugin in the AppImage (packaging) |
| `the shell has no active editor pane` | `editor_area.*` (pane lifecycle; RM-5a lane) |
| `the cursor landed at A:B instead of C:D` | the position convention of whoever produced the request |
| `no jump target is registered …` (message #1, a different line) | lane install order / `installSearch` |

---

## 4. What is NOT proven — the RM-4 root cause itself

**I could not reproduce the user's refusal** on this machine (matrix in §2), so I will not
name a cause I did not observe. What is missing is one datum the panel now prints (and that
the user's next run will produce): the **reason sentence**.

**ROOT CAUSE FOUND AFTER THIS REPORT (T28, `../rm4b-columns/README.md`).** The refusal is
produced by a **wrong path base**: `slang` reports `source_file` relative to its own cwd
(`project_root(files)` = the parent of the first file), and the catalog resolved it against
the folder root, so a project whose sources live in a subdirectory (`rtl/`, `tb/`, …) handed
the editor `<root>/child.sv` instead of `<root>/rtl/child.sv`. The catalog still renders
(basenames), and EVERY jump in BOTH panels is refused — the user's exact report. Reproduced
with the real seam (nested fixture: 25 checks, 9 failures, `status=rejected`, the panel line
`… the editor rejected the jump to child.sv:1:34 — cannot open <root>/child.sv: not a regular
file …`) and fixed in the producer (`crates/vd-deeplint::tool_paths`), red→green in
`../rm4b-columns/logs/02-red-path-bug.out` vs `03-green-cases.out`.

Consequences for this report: §3.1/§3.2 remain true and fixed, but the **relative-path**
mechanism (§3.1) was defence in depth, not the trigger — the producer does not hand out
relative paths; it handed out **wrongly rooted absolute** ones. The byte-column mechanism
(§3.2) never refused; it mis-positioned. Both are now producer-side. The T25 case matrix and
the "what to paste back" table below stay valid as the tool for any FUTURE refusal.

Not claimed: the exact AppImage the user ran is not in this tree
(`target/release-assets/v0.2.0/…` predates the interface panel, `build/artifacts/vd-app` is
the legacy 6 MB artifact), so the binary could not be matched against this source.

**The module tree no longer hides the reason either:** `module_tree_panel.cpp` had the same
"reason discarded" defect (the user saw both panels fail), and it now prints the same
`— <reason>` suffix; both panels share one translation entry (see `translations/vedit_zh_CN.ts`).

---

## 5. Bounds

- Offscreen and Xvfb/xcb `QApplication`; no real display server, no real mouse hardware.
- Two-file SystemVerilog fixtures written for this task (`fixture/`, `fixture-a/`,
  `fixture-c/`), plus the product's in-tree 7-file fixture for the acceptance test.
- `slang 12.x` from `build/slang/out/slang` (`VEDIT_SLANG`); KTextEditor 6.24 (KF6).
- The probe exercises the GUI thread synchronously, exactly like a double-click handler; it
  does not exercise window-manager focus behaviour or a real compositor.

## 6. Files

| path | what |
|---|---|
| `fixture/` | two-file project: `top.sv` instantiates `child`; ports on line 1 |
| `fixture-a/` | the "previous project" for the restore→switch sequence |
| `fixture-c/` | the same project with CJK text before the port (byte-column case) |
| `probe/rm4_probe.cpp` | the probe: `project`, `seed`, `switch`, `folder`, `seam` modes |
| `probe/build.sh` | builds it against the current tree from `ninja -t commands src/app/vedit-app` |
| `logs/10-before-position-path.out` | failing-before: relative path + byte column |
| `logs/11-before-panel-reason.out` | failing-before: reason discarded |
| `logs/12-after-project-and-seam.out` | passing-after: both gestures, fixture-c, seam checks |
| `logs/13-after-restore-switch.out` | passing-after: restore A → Open Folder B → jump |
| `logs/17-final-all.out` | **final one-shot run**: project 20/0, fixture-c 20/0, seam 10/0, seed 7/0, restore→switch 21/0, jump tests 40/0, search selftest 106/0 (2 skipped), module-tree acceptance |
| `logs/20-case1-folder-cwd.out` | T25 case 1: cwd = `/tmp` and `/`, bare launch → Open Folder, both panels |
| `logs/21-case2-before.out` | T25 case 2/2b **pre-fix**: relative catalog path refused (`cannot open /tmp/child.sv`) |
| `logs/22-cases-after.out` | T25 case 1–3 **post-fix**, incl. the diagnostic-completeness assertions |
| `logs/23-after-suites.out` | jump tests 53/0 (13 new `reason.*` checks), search selftest 106/0, module-tree acceptance, lrelease |
| `logs/24-final-t25-all.out` | final probe (both panels): project 25/0, seed 7/0, restore→switch 26/0 |
| `logs/14-affected-suites.out`, `logs/15-final-verification.out` | earlier suite runs (same results) |

About the module-tree acceptance suite: it reports **3 failures** —
`h1.first-appearance.file-set`, `.uses-seeded-set`, `.source-is-model`. They are
**pre-existing and unrelated**: the identical 3 failures appear in
`logs/09-acceptance-test.out`, captured *before* this task's changes (that run predates
them), and they belong to the project-model freshness checks that another lane is
currently editing (`project_model.cpp`, `project_integration.cpp` are modified in the
worktree by that lane). Every interface-panel cursor check in that suite passes.

### Re-run

```bash
bash evidence/ux-batch-2/rm4-jump/probe/build.sh
VEDIT_SLANG=$PWD/build/slang/out/slang QT_QPA_PLATFORM=offscreen \
  target/rm4-jump-probe/rm4-jump-probe project evidence/ux-batch-2/rm4-jump/fixture
VEDIT_SLANG=$PWD/build/slang/out/slang QT_QPA_PLATFORM=offscreen \
  target/rm4-jump-probe/rm4-jump-probe project evidence/ux-batch-2/rm4-jump/fixture-c
VEDIT_SLANG=$PWD/build/slang/out/slang QT_QPA_PLATFORM=offscreen \
  target/rm4-jump-probe/rm4-jump-probe seam    evidence/ux-batch-2/rm4-jump/fixture
```
(inside the pinned Qt6 image, workspace mounted at `/vedit-build`; see `probe/build.sh`).
