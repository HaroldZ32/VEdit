# Module tree lane (`module_tree`, task T17; rebuilt for UX batch 2 items 1+2)

Two docks over one project-wide **module catalog**, plus the navigation gestures
the user asked for.

## What it does

* Reads the project's effective file set from **the project model's persisted
  state** (`<state dir>/project-model.json`: root + the union of the imported
  `.f` files and the folder scan + the tools page's `-I`/`-D`). If that state
  does not exist yet, the lane starts the same `vd_project_expand_v2` job the
  project panel uses.
* **The watch converges, it never swallows an event.** A fresh state directory
  is the normal first open: the holder's first run is the root-scan fallback
  while the project lane expands asynchronously and only then saves
  `project-model.json`. The holder therefore tracks (a) the change token
  (`mtime|size`) it last observed, (b) the token the current catalog run
  actually read, and (c) a dirty flag for a change observed **while a run is in
  flight**; when the run answers or fails, one follow-up run is scheduled
  (`CatalogClient::start` loads the persisted model synchronously, so the token
  taken just before the call is the one the run evaluates). An unchanged model
  never re-runs, so this cannot loop. The result: the tree always ends on the
  persisted project model - imported `.f` lists, `-I` and `-D` included - with
  no manual Refresh.
* Starts `vd_catalog_start` and polls it from a `QTimer` — slang never runs on
  the GUI thread. The answer (`VdModuleCatalog`) is copied into plain values
  (`ModuleCatalog`) and the C memory is released immediately.
* **`module-tree` dock — a PURE MODULE HIERARCHY** (item 1):
  * roots are the modules with `is_top` (no module instantiates them; one or
    several);
  * children are the modules the parent body instantiates, one row per distinct
    child module with its instantiation count;
  * a module instantiated by two parents appears under **both** parents;
  * **no net / driver / load / chain row exists** — the old elaborated tree and
    its "nets without a reported instance" group were removed, not hidden;
  * depth is capped at **64** and a cycle is stopped by a visible marker; a row
    budget (20000) bounds a pathological module DAG. Every guard is a row, never
    a crash or a silent truncation.
* **`interface-panel` dock — the focused module's ports** (item 2), columns
  `Name / Direction / Type / Width / Location`:
  * width derives from the ABI's verbatim `type` string:
    `logic[3:0]` → 4, `logic[1:0][3:0]` → 8, no packed dimension → 1, a
    parameterised range (`logic[WIDTH-1:0]`) → **empty cell + tooltip**;
  * focus rule: (a) the module selected in the module tree, else (b) the module
    whose definition is the nearest one at or above the caret line in the same
    file (catalog definition positions);
  * it refreshes on selection, editor/pane switch, caret movement, project-root
    change and re-elaboration.
* **Every jump goes through `vedit::workspace::jump::jumpTo()`** — the panels
  have no file-opening code of their own:
  * double-click a module row → the module **definition**;
  * double-click a port row → the port **declaration**;
  * Ctrl+double-click a port row → the module's **instantiation statement**
    (several parents → a chooser). The ABI has no per-connection position, so
    the Ctrl+double-click target is the instance statement, never the connected
    port; the panel's tooltip states that.
* Empty states are one concise line naming the reason (no project / no modules /
  no focused module); service failures are one line, never per-file errors.

## Build / test

```
# inside the pinned Qt6 image (see scripts/versions-qt.env)
cmake -S . -B target/qt6-build -G Ninja
cmake --build target/qt6-build --target vedit-module-tree-selftest \
      vedit-module-tree-catalog-test
ctest --test-dir target/qt6-build -R 'module-tree' --output-on-failure
```

* `vedit-module-tree-selftest` — deterministic model + panel checks, no slang:
  root set, duplicate parents, cycle guard, depth cap, width derivation, the
  focus rule and both activation gestures against a recording jump target.
* `vedit-module-tree-catalog-test` — the REAL acceptance run: the lane fixture
  under `fixtures/ux-batch-2/` is catalogued by the real slang binary through the
  real product client, the product docks are used, and the jumps are verified on
  the product's KTextEditor jump target. It also carries the two project-model
  regressions (a seeded model whose file set differs from the root scan, and a
  model written while a catalog run is in flight) and the declaration-≠-
  instantiation check on `shared`, which has two instance sites. The fixture is
  in-tree and slang is a build dependency, so a missing one **fails** the test
  (exit 1) instead of printing SKIP and exiting 0; a green run also asserts that
  at least one check ran.

## Translation status (honest)

Every user-visible string in this lane is wrapped in
`QCoreApplication::translate("VEdit", …)`: the panels' headers, statuses and
tooltips, `ModuleCatalog::summaryLine`, the width-problem tooltips, the
guard/marker notes and `CatalogClient`'s failure messages. `translations/**`
belongs to the batch's integration step, so this lane does not edit the
catalogue: until the captain runs `lupdate`/`lrelease` those strings render as
their English source text. The repair report lists every extracted string.

## Fixture

`fixtures/ux-batch-2/` is deliberately small and shaped for the acceptance list:
`shared` is instantiated by **both** `top_a` and `top_b`, `orphan` is defined but
never instantiated (a third root), `bus_if` is an interface (excluded from
modules but present as an `InOut` port type), and `reflink` has a `Ref` port.
