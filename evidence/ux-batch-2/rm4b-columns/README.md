# RM-4b — the catalog's paths and columns (T28)

**The headline is not the byte column.** This row's main deliverable is the **RM-4 root
cause**: `slang` reports every path relative to its own working directory, and the catalog
resolved it against the wrong base, so a project whose sources live in a subdirectory
(`rtl/`, `tb/`, …) handed the editor paths that do not exist. Every jump was then refused —
in the interface panel AND the module tree, on a project that loads and renders perfectly.
That is the user's report, reproduced end-to-end below.

Contents: §1 the root cause (red→green) · §2 the byte→character columns (reviewer H2) ·
§3 the path rule (RM-9) · §4 the LOW reason guard · §5 the producer-vs-consumer decision ·
§6 what the base analysis found · §7 the lint side · §8 evidence and bounds.

All C++ runs use the real product wiring (`ApplicationShell` + `installPanels()` + the real
`search::EditorJumpTarget` + the real panels); no fake target anywhere.

---

## 1. ROOT CAUSE of "every jump refused" — a wrong path base

### The mechanism

1. `crates/vd-deeplint/src/hierarchy.rs::elaborate_impl` spawns `slang` with
   `.current_dir(project_root(files))`, where `project_root(files)` is **the parent of the
   FIRST file** (`hierarchy.rs:505`), e.g. `<root>/rtl`.
2. `slang` reports `source_file` **relative to its own cwd** — measured, and it does so even
   when it is given absolute file arguments:
   `cwd=/tmp` + `/fx/top.sv` → `"../fx/top.sv"`; `cwd=<root>/rtl` + `<root>/tb/b.sv` →
   `"../tb/b.sv"`.
3. The catalog's `source_file` therefore was `<relative>`, and
   `src/workspace/module_tree/catalog_client.cpp::absoluteSourcePath` resolved it against the
   **folder root** — losing the subdirectory: `<root>/rtl/child.sv` became
   `<root>/child.sv`.
4. The catalog renders (it shows basenames), but every jump asks the editor to open a file
   that does not exist → `EditorPane::openFile()` fails → `JumpStatus::Rejected` → the panel
   line below. Same for the module tree, because it uses the same catalog positions.

### Red → green (probe, real seam)

Fixture `fixture-nested/` — the reviewer's shape: two files in `rtl/`, opened folder = the
parent.

**RED (pre-fix producer, `logs/02-red-path-bug.out`)** — 25 checks, **9 failures**:

```
PASSED   : .../fixture-nested/child.sv:1:34 (status=rejected)
RECEIVED : .../fixture-nested/child.sv:1:34
SEAM-ERR : cannot open .../fixture-nested/child.sv: not a regular file
           [absolute path opened as given; files outside the workspace are allowed;
            workspace root: .../fixture-nested]
PANEL    : port clk declaration: the editor rejected the jump to child.sv:1:34 — cannot open
           .../fixture-nested/child.sv: not a regular file [...]
TREE PANEL: child definition: the editor rejected the jump to child.sv:1:8 — cannot open
           .../fixture-nested/child.sv: not a regular file [...]
```

That is the **exact user-facing line**, and it is the same shape as the user's report
(`编辑器拒绝了跳转到 <file>:<line> 的请求`) — now with the reason attached.

**GREEN (after, `logs/03-green-cases.out`)** — 25 checks, 0 failures:

```
PASSED   : .../fixture-nested/rtl/child.sv:1:34 (status=delivered)
PASSED   : .../fixture-nested/rtl/top.sv:3:9   (status=delivered)
TREE PASSED: .../fixture-nested/rtl/child.sv:1:8 (status=delivered)
checks=25 failures=0
```

Multi-directory fixture (`fixture-multidir/`: `rtl/` + `tb/`) — RED 30 checks / **11
failures** (`<root>/child.sv`, `<root>/../tb/tb.sv`), GREEN **30/0** with
`rtl/child.sv`, `rtl/top.sv` and `tb/tb.sv` all correct from BOTH panels.

### The fix (producer, one rule, one place)

New `crates/vd-deeplint/src/tool_paths.rs`:

* `absolute_path(file, base)` / `absolute_path_str` — resolve a reported path **against the
  directory the tool actually ran in**; lexical normalisation (`..`/`.` collapsed), never
  canonicalisation (the user's spelling and symlinks survive).
* `absolutize_ast(&mut Value, base)` — rewrite `source_file`, `source_file_start`,
  `source_file_end` in place at the parse boundary.
* Wired at: `catalog.rs:330` (the catalog's pruned document, base = `project_root(files)`,
  the same value passed to `.current_dir()`), `hierarchy.rs:817`
  (`parse_design_at`, same base), `diagnostic.rs:550` + `service.rs:274` (the lint side, §7).
* `parse_catalog_json_in(text, tool_dir)` is the public whole-string entry that can carry the
  same base; the streaming path and the parity test use it.

---

## 2. The byte→character columns (reviewer H2) — moved to the producer

`slang`'s `source_column` is a **UTF-8 byte** column. The previous fix converted it only when
it overflowed the line (`column > lineLength + 1`), so the reviewer's fixture — a SHORT CJK
prefix on a LONG line (`/* 中文 */` before `clk`, line = 171 characters) — stayed wrong: byte
column 47 inside the line, character 43, cursor delivered at 47 on no identifier.

Fix at the producer, not the consumer: `Columns::char_column(file, line0, byte_column0)`
(`tool_paths.rs`) converts **every** catalog position to the editor's unit — the **UTF-16
code unit**, the same unit the diagnostics lane's `SourceLines` already uses. Applied once in
`catalog.rs:591` (`catalog_position`), so the interface panel, the module tree and any future
catalog consumer all receive editor columns.

Boundary rules (documented on the function, unit-tested):

| situation | rule |
|---|---|
| file unreadable / not UTF-8 / line out of range | the byte column is returned **unchanged** (the consumer's clamp is the fallback; no position is invented) |
| byte offset **past the end of the line** | clamped to one past the line's last character |
| byte offset **inside a multi-byte sequence** | rounded **down** to the character that contains it (a symbol cannot start mid-character) |
| empty line | column 0 → 0 |

Red → green (`fixture-bytecol/`, the reviewer's fixture copied verbatim):

| | double-click (declaration) | Ctrl+double-click | module tree |
|---|---|---|---|
| RED (`review-rm4-final/07-bytecol-probe.log`) | cursor `1:47`, identifier `''` **FAIL** | `1:9` `u_child` ok | ok |
| GREEN (`logs/03-green-cases.out`) | cursor `1:43`, identifier `clk` **PASS** | `1:9` `u_child` | `1:8` `child` |

Rust regression: `crates/vd-deeplint/tests/catalog.rs::a_cjk_prefix_does_not_shift_the_reported_column`
(asserts the catalog hands 42 for `clk` and 168 for `y`, 0-based; the byte columns are 46/172).

The seam's overflow-only heuristic in `search_jump_target.cpp` is **REMOVED** (it is now the
wrong layer: a byte column, an LSP UTF-16 column and a search byte offset are three different
conventions, and RM-4 showed an overflow-only guess is silently wrong inside the line). The
seam only clamps.

---

## 3. The path rule (MEDIUM from the review) — stated and tested

`src/workspace/jump/README.md` now carries the rule, and the target implements it
(`search_jump_target.cpp:86`):

| request | behaviour |
|---|---|
| **absolute** | opened **as given**; it may be OUTSIDE the workspace on purpose (a `.f` filelist may name files above the opened folder), so it is never re-rooted and never rejected for being outside |
| **relative** | resolved against the **workspace root** (never the process cwd — an AppImage started from a file manager runs in `$HOME`); ONE recovery for a producer that hands out relative paths |
| exists under neither reading | **refused**, and the reason names the rule that was applied + the workspace root |

Tested in `logs/03-green-cases.out` (seam mode, 20 checks / 0 failures):

```
OUTSIDE : requested='/tmp/rm4b-outside/outside.sv' delivered=1 opened='/tmp/rm4b-outside/outside.sv' error=''
MISSING : requested='/tmp/rm4b-outside/absent.sv' delivered=0
          error='cannot open /tmp/rm4b-outside/absent.sv: not a regular file
                 [absolute path opened as given; files outside the workspace are allowed;
                  workspace root: …/fixture]'
```

(Also still tested: a relative path resolves under the workspace root — `child.sv` →
`<root>/child.sv`.)

---

## 4. The LOW finding — the invalid-jump guard now carries its reason

`interface_panel.cpp:353`'s backstop (`!jump.isValid()`) used to print only the description.
It now reports what the catalog gave it:
`%1: the catalog reports no usable position (file=%2, line=%3, column=%4), so nothing was opened`
(new zh_CN rows; `lrelease` 1244 finished / 0 unfinished / 0 untranslated).

---

## 5. Producer vs consumer — what was done and why

Both fixes are at the **producer** (`crates/vd-deeplint`), because the producer is the only
place that knows the tool's cwd and the tool's column unit:

* a consumer cannot recover a lost subdirectory (it does not know the tool's cwd), and
* a consumer cannot know whether a column is bytes or UTF-16 (its callers are heterogeneous).

The consumer keeps exactly one safety net, and it is explicit: a relative path is resolved
against the workspace root, and an absolute path is never re-rooted.

## 6. What the base analysis found (the lead's question)

* **Is resolving against `project_root(files)` exact?** Yes: it is the **real** cwd that was
  passed to the child, so `cwd.join(relative)` is exact for ANY layout — measured with files
  in two different directories: from `cwd=<root>/rtl`, `child.sv` → `<root>/rtl/child.sv`
  and `../tb/tb.sv` → `<root>/tb/tb.sv`. The base is arbitrary as a *choice*, not as a
  *resolution base*; the multi-dir Rust test and the multi-dir probe fixture prove it.
* **Is there a way to avoid depending on the cwd at all (absolute in → absolute out)?** No —
  measured: `slang` relativises even absolute arguments (`cwd=/tmp` + `/fx/top.sv` →
  `../fx/top.sv`). Resolving against the real cwd is therefore the only correct mechanism.
* Would changing the base to something "meaningful" (the common ancestor of the files) help?
  It would not remove the need to resolve, and it would change the child's cwd semantics
  beyond this row; the resolution against the actual cwd is already exact, so the base was left
  unchanged.

## 7. The lint side — the same bug, confirmed and fixed

Measured with the real tool: `slang --lint-only --diag-column-unit byte --diag-json …` reports
`file` exactly like the AST does (`../vedit-build/…/warn.sv` for an absolute argument from a
foreign cwd). The lint child inherited the app's cwd (no `.current_dir()`), so diagnostics for
a project in a subdirectory carried paths that never matched the editor's absolute file — the
file dots could not attach. Fix:

* the lint child now runs **in the linted file's directory** (`service.rs:274`) — a directory
  we know and can resolve against, never the process cwd;
* `convert_diagnostic` resolves `location.file` and every range file against it
  (`diagnostic.rs:550`).

Test (`crates/vd-deeplint/tests/real_tools.rs::a_diagnostic_names_the_real_absolute_file`):
a source OUTSIDE the test's cwd with one real slang warning → the diagnostic's `file` is the
real absolute path (`logs/05-rust-suite.out`: `real_tools … 3 passed`). The verible path is
unaffected: verible echoes the path it was given, and the lint runner passes absolute paths.

## 8. Evidence, bounds, and what the user should paste back

**Logs** (this directory): `logs/02-red-path-bug.out` (RED, nested + multi-dir),
`logs/03-green-cases.out` (GREEN: nested 25/0, bytecol 25/0, multi-dir 30/0, seam 20/0, plain
25/0), `logs/04-affected-suites.out` (C++ suites + lrelease), `logs/05-rust-suite.out`
(`cargo test -p vd-deeplint`: lib 70/0, catalog 11/0, streaming 7/0, diagnostic 15/0,
engine 12/0, hierarchy 6/0, real tools 3/0, scale 2/0).
Fixtures: `fixture-nested/` (rtl/), `fixture-multidir/` (rtl/ + tb/), `fixture-bytecol/`
(the reviewer's fixture verbatim).
The probe lives in `../rm4-jump/probe/` (modes `project`, `multidir`, `seam`, `folder`,
`switch`, `seed`).

**Suites**: jump-lane ctest 53/0; search selftest 106/0 (2 skipped); module-tree acceptance —
every interface-panel cursor check passes, and the 3 `h1.first-appearance.*` failures are
pre-existing and belong to another lane's project-model work (present before this row);
lrelease 1244/0/0.

**Catalogue coordination (declared, not hidden).** `translations/vedit_zh_CN.ts` has ONE
declared writer in this batch (the i18n lane). This row had to add zh_CN rows for its own new
strings (4 path-rule sentences in `search_jump_target.cpp`, 2 in `interface_panel.cpp`), and
the `lrelease` gate was RED in the worktree because 8 entries belonging to OTHER lanes were
untranslated; I filled those 8 to make the gate green. They are DRAFT wording, not reviewed:
they are `document_io.cpp` "cannot open %1: the editor could not load the file",
`file_tree_model.cpp` "Files imported through a .f filelist…" / "From filelist (%1)",
`catalog_client.cpp` "the module catalog service did not answer within 300 s", and
`file_status.h` "Has errors" / "Warnings only" / "Analysed: no problems found" / "Not
analysed yet …". No `lupdate` was run (the XML was edited by `<source>` text, so no
`<location>` lines or ordering were rewritten), and the i18n lane should replace the 8 with
its own reviewed wording.

**Bounds**: offscreen and Xvfb/xcb `QApplication`; no real display server, no real mouse
hardware; fixtures written for this task plus the reviewer's; `slang 12.x`; the C++ probe
drives the panel synchronously exactly like a double-click handler.

**What the user should paste back after one refused jump** (the line now names its own cause):

> `port <name> declaration: the editor rejected the jump to <file>:<line>:<col> — <reason> [<path rule>; workspace root: <root>]`

With the path base fixed, a `cannot open … not a regular file` reason now means the catalog
named a path that does not exist for a reason that is NOT the subdirectory bug — and the
reason names the path and the rule, so the next fix is localised instead of guessed.
