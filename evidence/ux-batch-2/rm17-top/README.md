# RM-17 — name a TOP module; the module tree AND the lint follow it

Status: **implemented and measured**. The user names a top in the module-tree
header; the catalog elaborates from it (slang `--top`), the tree shows that
hierarchy, the lint pass lints only the files reachable from it, and every file
outside it is visible as hollow-grey “not analysed” with an explicit count. The
setting is a per-project value persisted per root.

---

## 1. Where the user sets it (one control, three ways)

`src/workspace/module_tree/module_tree_panel.cpp` — header row, object name
`module-tree-top`:

* **Auto** (first item, data empty) = today's behaviour: every module slang finds
  uninstantiated is a root;
* the **detected candidate tops** (the modules the catalog reports as never
  instantiated — the tree's own roots, learned across Auto runs);
* **free text** for a name the list does not know; clearing the text returns to
  Auto.

The panel shows the EFFECTIVE value (the combo reflects the catalog that is on
screen, `setEffectiveTop`) and the summary line appends `· top: <name>`
(`module_catalog.cpp`). The Project panel shows nothing new: the module-tree
header is the single edit place (per the addendum).

Accepted by `vedit-module-tree-selftest` (PASS: 86 checks, 0 failures):

```
ok   top.control-exists — combo=1
ok   top.candidates-listed — count=3
ok   top.effective-value — top_a
ok   top.free-text-emits — seen=typed_name
ok   top.cleared-means-auto — effective='' seen=''
ok   top.exclusion-line — 3 file(s) outside the top top_a are not analysed
ok   top.state-roundtrip — top='top_a' a='top_a' b='top_b'
ok   top.per-root — a='top_a' b='top_b'
```

## 2. Persistence — per project root, same pattern as `variablesByRoot`

`src/workspace/project/project_state.h`:

```cpp
QString top;                  // the live entry of `root` ("" = Auto)
QMap<QString, QString> topByRoot;  // every root's own top
```

* `project-model.json` version **4** (`+ top/topByRoot`), written by
  `ProjectState::toJson/fromJson`; a version-3 file loads with empty values and
  the model seeds the live root's entry (the same additive rule RM-3 used).
* `ProjectModel::top()/setTop()` remember the value PER ROOT; `open()` restores
  the NEW root's own top and never the previous project's (`topByRoot` survives
  the wipe exactly like `variablesByRoot`). A→B→A therefore restores A, B never
  sees A.
* **One writer.** The module tree does not write `project-model.json`; it calls
  `ProjectModel::setTop()` (found through the shell child tree), so the model
  cannot overwrite the user's top on its next expansion save.

## 3. The slang mechanism — confirmed against the BUNDLED binary (measured)

`build/slang/out/slang --help`:

```
--top <name>   One or more top-level modules to instantiate (instead of
               figuring it out automatically)
```

On the two-top catalog fixture (7 definitions: interfaces + 6 modules):

| run | definitions listed | InstanceBodies | design roots | AST-JSON bytes |
|---|---|---|---|---|
| no top (Auto) | 7 | 7 | orphan, top_a, top_b | 44,622 |
| `--top top_a` | 7 (all) | **5** (top_a, consumer, reflink, shared, bus_if) | **top_a** | **32,234** |
| `--top no_such_module` | 7 | **0** | **none** | 3,672 |

**Bogus name** — exit 1 and a NAMED error on stderr:

```
error: 'no_such_module' is not a valid top-level module
warning: no top-level modules found in design [-Wmissing-top]
```

slang still writes a tiny AST document (definitions only, no bodies/roots), so
the reader must NOT present that as an empty project. The catalog maps it to
`LintFailure::UnknownTop { name }` — user text:

> the module 'no_such_module' is not a valid top-level module in this project:
> the module tree and the lint scope were not changed

The tree panel shows that error (`setFailure` path); there is **no silent
fallback to Auto** (asserted by `an_unknown_top_is_a_named_error_not_an_empty_tree`).

**Rejected alternative — dependency files.** `--Mmodule` / `--Mall` list every
file that was *parsed*, not what is reachable from the top (measured: identical
7-file list with and without `--top top_a`), so they cannot define the lint
scope.

## 4. One derivation, two consumers (the addendum's consequence B)

The reachable set is derived ONCE, inside the same catalog run that produces the
tree (`reachable_source_files` in `crates/vd-deeplint/src/catalog.rs`: the
`source_file` of every elaborated `InstanceBody`, so interfaces count, sorted +
deduped). The C++ catalog model carries it (`ModuleCatalog::reachableFiles`), and
the module-tree holder publishes exactly that list against the project file set:

```
analysed = project files ∩ reachable      excluded = project files − reachable
```

through the header-only seam `src/workspace/top_scope.h` (no link edge between
the two lanes; the diagnostics lane installs the sink). The tree's “N module(s)
outside the top … are not in this tree” warning (service side) and the panel's
“N file(s) outside the top … are not analysed” line both come from this one run.

## 5. The lint scope and the dots (the addendum's consequence A)

`DiagnosticsController::setTopScope(top, analysed, excluded)`:

* stores the scope; the NEXT project pass is filtered **inside
  `startProjectPass`** — the last boundary before the engine, after the
  language-registry gate — so no path can hand the engine a file outside the top;
* every excluded file is marked with `markNotAnalyzed(file, "outside-top:<name>")`
  → `FileStatus::NotAnalyzed` → the **hollow-grey ring** the RM-9 vocabulary
  defines. Such a file is never green and never “no mark” (it is in the project
  set, so it stays visible as excluded);
* clearing back to Auto removes exactly the marks this seam set (buffer “modified”
  marks survive) and removes the pass filter;
* the shell status gets the explicit line
  `N file(s) outside the top <name> are not analysed` (also rendered in the
  module-tree panel's status).

## 6. Measured cost — 1176-file fixture, Auto vs top

Fixture `evidence/ux-batch-2/u9a-catalog-scale/bigdesign-med` (1176 modules,
40 signals/module, 297,912,513 B AST JSON; chains of 10), top `mod_9`
(the last module of the first chain — its hierarchy is 10 modules).

| | Auto | top = mod_9 | change |
|---|---|---|---|
| catalog wall time | **3,016 ms** | **116 ms** | **26× faster** |
| modules in the tree | 1,176 | 10 | 117× fewer |
| files in the lint scope (same derivation) | 1,176 | **10** | 117× fewer |
| modules reported outside the top | 0 | 1,166 | stated, not dropped |
| real lint wall time (one `slang --lint-only -Weverything` per file, measured) | **3.99 s** | **0.047 s** | **≈85× faster** |

Heavy fixture `bigdesign` (1.16 GB AST JSON, 180 signals/module): Auto catalog
wall 14.2 s → top=mod_9 **262 ms** (≈54×), 10 modules, 10 reachable files.

Command: `VEDIT_U9A_BIGDIR=<dir> VEDIT_U9A_TOP=mod_9 <release test binary>
--ignored --nocapture the_top_selects_a_segment_of_a_real_scale_design`
(log line `u9a-top: files=1176 top=mod_9 modules=10 reachable_files=10
outside_modules=1166 elapsed_ms=115 …`). The lint timing used the product's own
per-file invocation (`--lint-only -Weverything --diag-json`), 1176 processes vs
10.

**Why the catalog does not drop to milliseconds on its own:** slang still PARSES
every file given on the command line (the `definitions` array keeps all of
them); `--top` scopes ELABORATION. The catalog saving is the smaller AST
document and the skipped elaboration; the lint saving is the file set itself.

## 7. Regression proof

* Rust (`crates/vd-deeplint/tests/catalog.rs`, real slang, two-top fixture):
  * `an_explicit_top_selects_the_hierarchy_and_reports_what_it_leaves_out` —
    top=top_a → modules `consumer, reflink, shared, top_a`; outside
    `orphan, top_b`; reachable files include `bus.sv` (the interface body);
    warning names “2 module(s) outside the top top_a”.
  * `auto_still_catalogues_every_root` — Auto returns all 6 modules, nothing
    outside, no top.
  * `an_unknown_top_is_a_named_error_not_an_empty_tree` — `UnknownTop` names the
    module and says “not a valid top-level module”; no “invalid/malformed” blame.
* Qt (`vedit-module-tree-selftest`): the 8 checks in §1.
* Real-slang end-to-end C++ catalog: `vedit-module-tree-catalog-test` (see
  `scratch/`). ABI: `top` appears exactly once in each generated struct; the
  cbindgen header regenerates idempotently (`sha256 dc11d38d…` twice) and
  `cargo test -p vd-ffi --test qt_abi_guard --test abi` = 21 + 1 passed.

**Red-first, honestly:** before this change the setting cannot be expressed at
all — the catalog request has no top argument and the panel has no control, so
the new checks cannot compile against the old code (the red is the absence of
the API, not an assertion tuning). The observable “before” is the recorded Auto
run: 1,176 modules / 1,176 lint files, with no way to scope them.

## 8. Bounds / not proven

1. **Reachable set = elaborated bodies.** A file that contributes only a
   definition with no elaborated body (typically a package instantiated by
   nothing) is not in the reachable set and would be EXCLUDED from the lint
   under a top. Documented; the fixture designs have no such file.
2. **Per-file reason text.** The reason stored for an excluded file is
   `outside-top:<name>`, but the hollow ring's tooltip comes from
   `fileStatusHint(status)` which takes only the status int, so the top's NAME
   appears in the aggregate line (panel status + shell diagnostic + the tree's
   service warning), not in the per-file hover.
3. **The controller's pass filter and the hollow mark are compile-verified and
   wired, not covered by a dedicated ctest in this slice.** The rust-side
   derivation (reachable/outside) and the panel/state behaviour are tested; the
   `startProjectPass` filter and `markNotAnalyzed` path need a diagnostics-lane
   test to be proven end to end. This is the one acceptance gap.
4. **Project panel** shows nothing new (per the addendum, the module-tree header
   is the only edit place); no read-only mirror was added.
5. The A→B→A switch is covered at the persistence layer (state round-trip +
   per-root map) and by `ProjectModel::open()`'s restore path, not by a full
   UI-level switch test.
6. The candidates list is learned from Auto catalog runs; on a first run WITH a
   persisted top the list starts with the persisted name only until the next Auto
   run (the name stays valid and the effective value is shown).
7. Full gate not run (captain's job); the app builds (`vedit-app` link), the
   module-tree selftest and the Rust crates are green.

## 9. Files

* Rust: `crates/vd-deeplint/src/{catalog.rs,lib.rs,hierarchy.rs,ast_prune.rs}`,
  `crates/vd-deeplint/Cargo.toml` (no new dependency),
  `crates/vd-deeplint/tests/catalog.rs`; `crates/vd-ffi/src/{capi.rs,catalog.rs}`
  + generated `include/vd_ffi.h`.
* C++: `src/workspace/project/{project_state.h,project_state.cpp,project_model.h,project_model.cpp}`,
  `src/workspace/module_tree/{module_catalog.h,module_catalog.cpp,catalog_client.h,catalog_client.cpp,module_tree_panel.h,module_tree_panel.cpp,module_tree_integration.cpp,module_tree_selftest.cpp}`,
  `src/workspace/diagnostics/{diagnostics_controller.h,diagnostics_controller.cpp,diagnostics_integration.cpp}`,
  new header-only seam `src/workspace/top_scope.h`.
