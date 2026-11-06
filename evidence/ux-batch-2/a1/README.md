# ux-batch-2 / A1 — module catalog + search enhancement + lint-config ABI

Task: `T1` (plan `.mpd/plans/ux-batch-2/00-plan.md` §3.A1, scope expanded by the
captain on 2026-11-05 with the two lint-config ABI items).
Owner: Senior Engineer (team `ux-batch-2`, lane A1).
Write scope used: `crates/vd-deeplint/**`, `crates/vd-search/**`, `crates/vd-ffi/**`
(incl. the regenerated `include/vd_ffi.h`). `src/**` was not touched.

All positions in this document are **0-based** unless explicitly labelled
`(1-based)`, matching the crate/ABI convention.

## 1. What was delivered

### 1.1 Module catalog (Rust + C ABI)

| Layer | Path |
|---|---|
| Backend | `crates/vd-deeplint/src/catalog.rs` (new) |
| Shared `slang --ast-json` runner | `crates/vd-deeplint/src/hierarchy.rs` (`run_ast_json`, extracted from `elaborate`; behaviour preserved — the hierarchy suite stays green) |
| C ABI | `crates/vd-ffi/src/catalog.rs` (new), structs in `crates/vd-ffi/src/capi.rs` |
| Tests | `crates/vd-deeplint/tests/{catalog.rs}`, recorded AST + real-slang E2E; ABI test in `crates/vd-ffi/tests/ux_batch_2.rs` |
| E2E aid | `crates/vd-deeplint/examples/catalog_dump.rs` |

C surface (async job + poll + free, the `vd_hier_*` shape; own id range `>= 1 << 52`):

```c
uint64_t vd_catalog_start(VdService*, const VdStringList *files, const VdCatalogOptions*);
int32_t  vd_catalog_poll (VdService*, uint64_t job, VdModuleCatalog *out);   /* 0/1/2 */
int32_t  vd_catalog_release(VdService*, uint64_t job);
void     vd_module_catalog_free(VdModuleCatalog*);
```

`VdModuleCatalog { tool, elapsed_ms, peak_rss_kb, warning, VdModuleList modules }`,
`VdModule { name, file, line, column, is_top, VdModulePortList ports, VdModuleInstanceList children }`,
`VdModulePort { name, direction, type_name, file, line, column }`,
`VdModuleInstance { module, path, file, line, column }`.

Semantics (all asserted):

* `modules[]` is keyed by module **definition** (`InstanceBody`), sorted by name;
  `file/line/column` come from the `InstanceBody`.
* `is_top` = the module is never instantiated anywhere.
* Every parent module's `children` contains the edge: `shared` instantiated under
  `top_a` **and** `top_b` appears in both lists; two instances of the same module
  under one parent stay two edges (`top_b.u_shared`, `top_b.u_shared_2`).
* A module defined but never instantiated (`orphan`) is present and `is_top=1`.
* Multiple design roots are all catalogued (the run never passes `--top`).
* Generate-loop iterations of the same instantiation collapse to one edge (same
  source position); an instance array is one edge at the array's site.
* **Interfaces are not modules.** An interface instance (`bus_if bus();`) has its
  own `InstanceBody`; the catalog filters bodies by the AST
  `definitions[].definitionKind == "Module"` (an address with no definition entry
  is treated as a module, for older tools). Without that filter `bus_if` would
  appear in the module tree — verified against the real tool.
* An interface-typed **port** (`InterfacePort`, not `Port`) is kept:
  `direction = "InOut"` (bidirectional by construction), `type = <interface name>`.
  It does not crash and is not dropped.
* `ref` ports keep direction `Ref` (four-value ABI: In | Out | InOut | Ref).
* `type_name` is the tool's verbatim string (`logic`, `logic[3:0]`, `bus_if`).
* `path` is the elaborated hierarchical path of the instance (`top_a.u_shared`),
  taken from the first elaboration; a child edge is deduplicated by
  `(module, file, line, column)`.

### 1.2 Search enhancement (Rust + C ABI)

| Layer | Path |
|---|---|
| Backend | `crates/vd-search/src/lib.rs` |
| C ABI | `crates/vd-ffi/src/search.rs` (+ `VdSearchHit` range fields, `VdSearchOptions`) |

New entry point (legacy signatures unchanged):

```c
uint64_t vd_search_tree_query_ex(VdService*, const char *root, const char *needle,
                                 const VdSearchOptions*);   /* poll: vd_search_tree_poll */
```

`VdSearchOptions { case_sensitive, whole_word, regex, all_matches, _reserved[4],
VdStringList extensions, max_results, max_file_bytes, max_duration_ms }`;
a zeroed struct / null pointer is the old behaviour.

* Options: case sensitivity, whole word (`\b`), regular expression, extension
  filter (case-insensitive, leading dot accepted, blank entries ignored; it takes
  precedence over `verilog_only`), and `all_matches` (every occurrence on a line
  as its own hit — the replace-all path).
* Each hit carries its exact range: `column` (1-based start character),
  `end_column` (1-based character column immediately AFTER the match, exclusive),
  `match_length` (characters) — so `end_column - column == match_length`. These
  are character units, not bytes (a CJK prefix is asserted).
* Literal and regex queries go through the **same** `regex` engine (literal is
  `regex::escape`d): one matcher, one set of byte offsets, and no `to_lowercase`
  copy whose byte length can shift columns. The engine is RE2-style (no
  backreferences / lookaround) — plan §2.
* An uncompilable pattern is rejected before the walk (typed error, job FAILED,
  message `invalid search query: …`); zero-width matches are not hits (nothing to
  replace) and cannot loop.
* The walk stays in Rust: Qt never traverses the tree and never re-matches.
  No file writing is implemented — the caller replaces using the returned range.
* Job/poll/cancel semantics are the service registry's existing ones
  (`JobValue::Search`), so `vd_job_cancel` behaviour is unchanged.

### 1.3 Lint-config ABI (captain's scope addition)

| Item | Path |
|---|---|
| `VdProjectInfo.rules_path` | `crates/vd-ffi/src/{capi.rs,project.rs}` |
| `VdLintBatchOptions` typed knobs | `crates/vd-ffi/src/{capi.rs,deeplint.rs}` |
| argv + validation | `crates/vd-deeplint/src/{lib.rs,engine.rs}` |
| echo for the panel | `vd_lint_batch_command_line`, `vd_lint_unsupported_knobs` |

**Field report for A4 (also sent to the captain):**

| Field | Type | Semantics | Accepted values / charset | Engine |
|---|---|---|---|---|
| `ruleset` | `uint8_t` | Verible `--ruleset=` | `0` = default → **NO flag**; `1` → `--ruleset=all`; `2` → `--ruleset=none`; anything else rejected with code 1 | verible only; **no-op for slang** |
| `rules` | `const char*` | Verible `--rules=` (one argv value) | comma-separated `[+|-]rule`, rule = `[A-Za-z0-9_-]+`; blank/null = no flag; empty entry, invalid sign or other character rejected | verible only; **no-op for slang** |
| `waiver_files` | `const char*` | Verible `--waiver_files=` (one argv value) | comma-separated paths; non-empty, no control characters; a comma cannot appear inside a path | verible only; **no-op for slang** |
| `slang_suppressions` | `const char*` | extra slang `-W…` tokens, each its OWN argv entry | whitespace/comma separated, each `-W[A-Za-z0-9_=-]+` (e.g. `-Wno-unused`, `-Werror=foo`); blank/null = none | slang only; **no-op for verible** |
| `error_limit` | `uint32_t` | slang `--error-limit=N` | `0` = **no flag**; `N>0` → `--error-limit=N` | slang only; **no-op for verible** |

No shell anywhere: every value is validated, then appended to a `Command` argv
array (the waiver list is joined with commas into ONE `OsString` argument).

`vd_lint_unsupported_knobs(engine)` returns the documented no-ops
(`slang` → `ruleset,rules,waiver_files`;
`verible` → `slang_suppressions,error_limit,include_dirs,defines`) so the
settings page can grey those controls out with the reason (plan §0.1 R1).

`VdProjectInfo.rules_path` is the absolute path of the `.rules.verible_lint` the
project loader DISCOVERED (T114 semantics unchanged; `rules.search_stopped` is
still dropped by the projection). Null when none was found. For the v2 expansion
path the loader is asked once more on the worker thread, because
`expand_with_variables` does not carry the discovery result; this is deterministic
and never turns "not found" into an error.

### 1.4 Dependency decision (regex)

The plan says: if `regex` is needed and the workspace has not preinstalled it,
stop and report Do not edit the root `Cargo.toml`. `regex` was absent from
`Cargo.lock`/`[workspace.dependencies]`, so this was reported to the captain
before implementing; the captain approved. Execution:

* `crates/vd-search/Cargo.toml` gained `regex = "1"` (lane manifest only).
* The **root** `Cargo.toml` is untouched. `Cargo.lock` gained exactly
  `regex 1.13.1`, `regex-automata 0.4.18`, `regex-syntax 0.8.11`,
  `aho-corasick 1.1.5` (all present in the offline cargo cache; build verified).

## 2. Verification (commands and observed results)

Raw logs are in `evidence/ux-batch-2/a1/raw/`.

### 2.1 Unit/integration/ABI tests — PASS

```
$ CARGO_HOME=$PWD/target/cargo-home cargo +stable test -p vd-deeplint -p vd-search -p vd-ffi
… 52 + 3 + 4 + 15 + 12 + 6 + 2 (vd-deeplint) + 47 + 21 + 1 + 8 (vd-ffi) + 18 (vd-search) = 189 passed; 0 failed
EXIT=0
```
Log: `raw/cargo-test.txt`.

Note on the toolchain: this host's pinned `1.99.0` rustup toolchain has no
`cargo` binary (documented in `build/gates/typecheck.sh`); `+stable` is the SAME
compiler build (`rustc 1.99.0 (b940084d7)`), and `CARGO_HOME=target/cargo-home`
is the repository's own pinned-image cargo home. The workspace clippy gate below
was additionally run in the pinned CentOS 7 image.

New coverage:

* `crates/vd-deeplint/tests/catalog.rs` — recorded AST + **real slang** E2E:
  top set (`orphan`, `top_a`, `top_b`), ports type/direction (`In/Out/InOut/Ref`,
  `logic` / `logic[3:0]` / interface name), child edges (both parents, same-module
  two instances, generate collapse), interface exclusion.
* `crates/vd-search/src/lib.rs` tests — case, whole word, regex (incl. invalid
  pattern and zero-width), extension filter (incl. blank entry), exact ranges
  (ASCII + CJK characters), all-matches vs first-only.
* `crates/vd-ffi/tests/ux_batch_2.rs` — catalog ABI end to end through the C
  surface with the real `slang`; extended search ABI ranges/whole-word/extension/
  invalid-regex; lint defaults send no flag; per-engine argv; typed rejection of
  an invalid ruleset/rules/suppressions; unsupported-knob lists; project
  `rules_path` found/absolute and null when absent.
* `crates/vd-deeplint/src/engine.rs` tests — the same argv/matrix at the crate
  level, including "default value sends no flag".

### 2.2 clippy / fmt — PASS

```
$ cargo +stable fmt --all -- --check           → EXIT=0      (raw/fmt-check.txt)
$ (pinned CentOS 7 image) cargo clippy --workspace --all-targets -- -D warnings
  container: CentOS Linux release 7.9.2009 … | rustc 1.99.0 | cargo 1.99.0
  Finished `dev` profile …  EXIT=0                            (raw/clippy-workspace.txt)
```

### 2.3 Header consistency — cbindgen output verified

```
$ touch crates/vd-ffi/build.rs && cargo build -p vd-ffi
$ diff -u <saved header> crates/vd-ffi/include/vd_ffi.h
IDENTICAL: the working copy is exactly what cbindgen generates from the Rust side
```
Log: `raw/header-check.txt`. The C++ guard test
(`crates/vd-ffi/tests/qt_abi_guard.rs`) also compiles the header in C++ and passes.

**Not proven:** the repository's drift gate (`build/gates/typecheck.sh`
`check_ffi_header_drift`) compares the working copy against `HEAD`, and this
workspace is a multi-lane batch tree where `crates/vd-ffi/include/vd_ffi.h` was
ALREADY modified before A1 started (other lanes' uncommitted ABI work). That check
therefore cannot be green until the batch is committed — a commit step that
belongs to the Lead/captain, not to this task. Evidence for the truthful part
(generated == source) is above.

### 2.4 Real-slang E2E — PASS

```
$ cargo run -p vd-deeplint --example catalog_dump -- crates/vd-deeplint/tests/fixtures/catalog/project build/slang/out/slang
6 module(s) in 24 ms
module consumer (defined at consumer.sv:1:8)
  port bus InOut bus_if @1:25
  port clk In logic @1:42
  port y Out logic[3:0] @1:66
module orphan (defined at orphan.sv:1:8) [top]
module reflink (defined at reflink.sv:1:8)
  port x Ref logic @1:27
module shared (defined at shared.sv:1:8)
module top_a (defined at top_a.sv:1:8) [top]
  port clk In logic @1:27
  port pad InOut logic @1:43
  port y Out logic[3:0] @1:67
  child shared path=top_a.u_shared @top_a.sv:4:10
  child consumer path=top_a.u_consumer @top_a.sv:5:12
  child reflink path=top_a.u_ref @top_a.sv:6:11
module top_b (defined at top_b.sv:1:8) [top]
  child shared path=top_b.u_shared @top_b.sv:2:10
  child shared path=top_b.u_shared_2 @top_b.sv:4:10
EXIT=0
```
Log: `raw/catalog-e2e.txt`; the captured AST the parser is tested against is
`raw/catalog-ast.json` (and the committed fixture
`crates/vd-deeplint/tests/fixtures/catalog/ast.json`). The top set, every port
(`type`/`direction`) and every child edge above are the hand-written expectation
in `tests/catalog.rs`, not the parser's own output.

### 2.5 The new argv is accepted by the REAL tools — PASS

`raw/real-tool-flags.txt`:

* verible-verilog-lint with `--ruleset=all --rules=+always-comb --waiver_files=<real
  file>` → runs and reports ordinary lint findings, exit 1 (no "unknown flag",
  no waiver-parse error once the waiver file uses the tool's syntax).
* slang with `-Wno-unused-variable --error-limit=5` → `Build succeeded`, exit 0.

This proves the flags the ABI emits are real tool options, not invented ones.

## 3. Known limits / not proven

1. **No Qt product consumption yet.** B1 (module tree), B2 (search panel /
   replace-all) and A4 (Lint page) consume these surfaces in later waves; this
   task delivers and tests the ABI only. `src/**` was untouched by design.
2. **Regex dialect differs from the editor's per-file Ctrl+F.** Project search is
   RE2-style (no backreferences/lookaround), per plan §2; Ctrl+F uses
   `QRegularExpression`. Must be documented in the user manual (B2).
3. **Header drift gate is red until the batch is committed** (see 2.3); it was red
   before this task because the batch workspace carries other lanes' uncommitted
   ABI changes.
4. **Catalog child `path` for a module instantiated in several places** is the
   first elaborated hierarchical path (`top1.u_mid…`), because the catalog is
   definition-keyed; the parent/edge identity is the source position. Anchoring
   this to one elaboration is deterministic (elaboration order).
5. **Definition kind fallback**: a document without a `definitions` entry for a
   body is treated as a module (older slang). Not exercised against an older
   binary — only the current pinned one.
6. **`ruleset`/`rules`/`waiver_files` validated regardless of the engine**: the
   values are checked even for an engine that ignores them (typed ABI, fail fast).
   A4's panel greys those controls out, so it will not send them.
7. **Waiver paths cannot contain commas** (comma is the list separator) — an
   explicit limitation of the comma-separated ABI form.

## 4. Files changed by this task

New: `crates/vd-deeplint/src/catalog.rs`,
`crates/vd-deeplint/tests/catalog.rs`,
`crates/vd-deeplint/tests/fixtures/catalog/{ast.json,project/*.sv}`,
`crates/vd-deeplint/examples/catalog_dump.rs`,
`crates/vd-ffi/src/catalog.rs`, `crates/vd-ffi/tests/ux_batch_2.rs`,
`evidence/ux-batch-2/a1/**`.
Modified: `crates/vd-deeplint/src/{lib.rs,hierarchy.rs,engine.rs,service.rs}`,
`crates/vd-search/{Cargo.toml,src/lib.rs}`,
`crates/vd-ffi/src/{capi.rs,search.rs,project.rs,deeplint.rs,lib.rs}`,
`crates/vd-ffi/include/vd_ffi.h` (regenerated), `Cargo.lock` (regex deps).
