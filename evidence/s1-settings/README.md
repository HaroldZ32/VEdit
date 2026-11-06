# T10 — settings-pages: evidence

Cell `cell-20261102T050548232Z` · board task `T10` · member `se-shell` (Senior Engineer)
Contract: `.mpd/cellwork/cell-20261102T050548232Z/contracts/settings-pages.md`
Loop: `loop-20261102T063252-9e28c4e4` (verifier: Reviewer, seat via `mpd_verify_seat`)

Deliverable in one line: the existing Preferences dialog (`SettingsDialog` /
`SettingsPanel`) now carries a `QTabWidget` with **Shortcuts / Tools / FIM**, every
page writes the SAME flat `settings.toml` through the Rust store, and the FIM API
key is never a store value.

## What changed

| path | change |
| --- | --- |
| `src/workspace/settings/settings_panel.h/.cpp` | three pages; Tools form (LSP backend, lint engine, four tool paths, rules file, `-I`, `-D`) with browse buttons; FIM form; write-only API key field + save/remove buttons + key-source line; `storeFimApiKey()` is the only programmatic writer and it never touches a widget; deferred-preview semantics extended to the new pages |
| `src/workspace/settings/settings_keys.h` | **new** — the Qt half of the key contract (see `KEY-CONTRACT.md`) |
| `src/workspace/settings/settings_dialog.cpp` | dialog grows to 900×760 so three pages + diagnostics fit |
| `src/workspace/settings/settings_selftest.cpp` | 113 checks: tabs, tool/FIM persistence + simulated restart, `-I/-D`, write-only key, exact-0600 mode (reported with the ambient umask), no temporary debris, loose-key-file replacement, no echo path, plaintext-key scrub, deferred preview (Cancel writes nothing / Apply writes), plus `--screenshot <dir>` and the FIM-key evidence record |
| `crates/vd-settings/src/schema.rs` | **new** — typed projection: `keys`, `ToolSettings`, `FimSettings`, `LspBackend`/`LintEngine`/`FimProtocol`, 0600 key-file read/write/resolve, redacting `ApiKey`; 13 tests |
| `crates/vd-settings/src/lib.rs` | module registration + doc pointer |
| `crates/vd-ffi/src/settings.rs` | `fim_settings_from_entries` / `fim_settings_from_store`: the store→`vd_fim::FimSettings` projection (API key included, env first); 4 tests. **No new C ABI symbol** — the generated header is untouched by this task |

## Commands and observed results

All commands were run on this working tree, inside the repository's pinned images
(the host has no working `cargo` for the pinned 1.99.0 toolchain and no Qt).

### Rust lane (pinned `vedit-build:local`)

```
source scripts/lib.sh && load_versions
run_in_build_container cargo test -p vd-settings -p vd-ffi --lib
    test result: ok. 32 passed; 0 failed; ...   (vd-ffi, includes the 4 settings-page tests)
    test result: ok. 19 passed; 0 failed; ...   (vd-settings, includes the 13 schema tests)
    TEST_EXIT=0
run_in_build_container cargo clippy -p vd-settings --all-targets -- -D warnings
    Finished `dev` profile ...                  CLIPPY_EXIT=0
rustfmt --edition 2021 --check crates/vd-settings/src/schema.rs \
    crates/vd-settings/src/lib.rs crates/vd-ffi/src/settings.rs
    FMT_EXIT=0
```
Log: `evidence/s1-settings/01-cargo.log` (three consecutive runs; one transient
failure was seen once while another lane rebuilt `vd-lsp`/`vd-project`, which the
log header records honestly — the three runs above are the measurement).

### Product build (documented one-command path, pinned Qt6/KF6 image)

```
bash scripts/build-qt.sh --no-test
    [rust-ffi] ... header check: no Rust type outside comments
    [qt6] PASS: Rust service layer + configure + build (warnings-as-errors)
                inside vedit-qt6-build:local; tests skipped (--no-test)
    BUILD_QT_EXIT=0
```
Log: `evidence/s1-settings/05-build-qt.log`. The C++ lane is compiled with
`-Wall -Wextra -Wpedantic -Werror` (see `build/gates/qt6-cxx.sh`), so this also
proves the new page code is warning-free.

> That whole-product run **predates the security repair** below (it is kept as the
> T10 build record, not as a claim about the current tree). After the repair the
> lane target was rebuilt alone in the same pinned image:
> `cmake --build target/qt6-build --target vedit_workspace_settings
> vedit-workspace-settings-selftest` -> `LANE_BUILD_EXIT=0`
> (`evidence/s1-settings/05b-lane-build.log`). A fresh whole-product link was not
> obtainable on the current tree because another lane's in-flight
> `src/workspace/project/project_service.cpp` does not compile — see Limits §4.

### Product binary smoke (unblocks the shell lane)

```
target/qt6-build/src/app/vedit-app            # 85,223,320 bytes, links
QT_QPA_PLATFORM=offscreen .../vedit-app --selfcheck
    EDIT-CORE-SELFTEST ok=1 checks=67 failures=0
    APP-SHELL-SELFTEST ok=1 checks=245 failures=0
    SELFCHECK_EXIT=0
```

### Settings lane test

```
QT_QPA_PLATFORM=offscreen .../vedit-workspace-settings-selftest --selfcheck \
    --screenshot evidence/s1-settings/screenshots
    SETTINGS-SELFTEST ok=1 checks=113 failures=0
```
Log: `evidence/s1-settings/selftest.log`. ctest registration is the same binary:
```
ctest --test-dir target/qt6-build -R vedit-workspace-settings-selftest
    1/1 Test #34: vedit-workspace-settings-selftest ... Passed    0.56 sec
    100% tests passed, 0 tests failed out of 1
```

### Security repair: the key file is 0600 **from birth** (umask-independent)

The first version created the file with `QFile::open` (mode `0666 & ~umask`) and
chmod-ed afterwards. Final mode was correct, but a local user who can read the
directory could open the still-empty 0644 file, keep the descriptor, and read the
secret after it was written. Both writers now create owner-only at creation
(`O_CREAT|O_EXCL`, mode `0600`), verify the mode with `fstat` **before** writing,
and `rename()` the temporary over the target.

```
# the lane test, run under a PERMISSIVE umask (a umask-relying writer would leave 0666 here)
(umask 000; QT_QPA_PLATFORM=offscreen .../vedit-workspace-settings-selftest --selfcheck)
    CHECK settings.pages.key-file-mode-exact PASS mode=63000 umask=000
    CHECK settings.pages.key-no-temp-debris PASS fim_api_key
    CHECK settings.pages.key-loose.replaced-0600 PASS mode=63000
    SETTINGS-SELFTEST ok=1 checks=101 failures=0        UMASK000_EXIT=0
```
(`mode=63000` is Qt's octal flag encoding: owner read+write only, no group/other,
no exec; the default-umask run prints `umask=022` with the same mode.)

Negative control — the same two creation paths, measured **at creation**, under
`umask 000` (`evidence/s1-settings/07-key-mode-probe.cpp|.log`):

```
OLD opened=1 mode-before-chmod=0666     <- QFile::open, the reported hole
NEW fd=3 mode-at-creation=0600          <- ::open(..., O_CREAT|O_EXCL|O_WRONLY, 0600)
```
So the acceptance reads the creating syscall's effect, not the end state: the new
path has no pre-0600 state to observe. Rust side has the same shape
(`create_new` + `mode(0600)` + `file_is_owner_only()` before the write + rename),
covered by `a_successful_write_leaves_only_the_key_file_behind`,
`an_existing_loose_key_file_is_replaced_by_an_owner_only_one` and the
umask-independent mode assertions.

Residual window and its mitigation, stated plainly: a crash between creating the
temp file and the `rename()` leaves a `0600` temp file **that contains the
secret** (no group/other access, but debris with a secret in it; the target still
holds the previous content). The next save sweeps stale `fim_api_key.tmp.<pid>.<n>`
files in the key directory, best effort, and leaves a temp alone whose `<pid>` is
still alive (another instance may be writing). Selftest:
`settings.pages.key-sweep.fixtures`, `.stale-removed`, `.live-kept`, plus
`settings.pages.key-loose.no-debris` after the replace path.

What is **code-verified, not execution-verified**: the forced-failure branches
(`fstat` mismatch, short write, `fsync` failure, `rename` failure) and the crash
window itself — the latter is untestable without a fault-injection seam, and this
task adds none. The happy path, the umask-000 discriminating case, the loose-file
replacement and the sweep are execution-verified above.

### Screenshots (three pages, real dialog, offscreen Qt)

* `screenshots/01-shortcuts.png` — the existing table plus `lint.rerun` and
  `fim.trigger` (added by the selftest's registry to prove AC-10.2.2), conflict line.
* `screenshots/02-tools.png` — the two selectors, four tool paths with browse
  buttons, rules file, `-I`/`-D` multi-line editors, the "stored in settings.toml"
  hint, and the diagnostics box.
* `screenshots/03-fim.png` — enabled/ghost-text switches, protocol, endpoint,
  model, timeout, max tokens, and the API-key group: write-only field, Save/Remove,
  the key-source line and the "never written to settings.toml" hint.

### `api_key` never lands in `settings.toml`

`screenshots/fim-key-evidence.txt` (written by the selftest) records the observed
state after a key was typed and committed:

```
key source reported:       user-file
key stored:                yes
key file grants group/other access: no
key echoed in the UI field: no
secret present in settings.toml:   no
fim.api_key present in settings.toml: no
--- settings.toml (verbatim) ---     (fim.enabled/protocol/endpoint/model/timeout_ms/max_tokens/ghost_text only)
--- key file ---                     26 bytes (content not recorded)
```
`screenshots/fim-evidence-settings.toml` is that store sample, and the key file is
deleted after the record so no secret-shaped file stays in the evidence tree.
`07-key-mode-probe.*` is the creation-time negative control described above.
Corresponding selftest checks: `settings.pages.key-not-in-store`,
`settings.pages.key-file-0600`, `settings.pages.key-file-content`,
`settings.pages.key-no-echo`, `settings.pages.key-restart-no-echo`,
`settings.pages.plain.*`, `settings.pages.nested.*`.

## Acceptance mapping

| AC | where |
| --- | --- |
| AC-10.2.1 (three pages in the dialog) | `settings.pages.tabs-container`, `tabs-titles`, `tabs-object-names`, `dialog-tabs`; `02-tools.png`, `03-fim.png` |
| AC-10.2.2 (shortcut table reused; new commands listed) | the table is the same widget, moved not rewritten; `settings.keys.rows`, `settings.keys.new-commands` (lint.rerun / fim.trigger), `settings.conflict.*` (unchanged checks), `01-shortcuts.png` |
| AC-10.2.3 / AC-1.1 / AC-2.5 | `settings.pages.tools-fields`, `tools-stored`, `tools-restart`, `backend-selected`, `include-dirs-widget`; keys in `KEY-CONTRACT.md` |
| AC-10.2.4 | `settings.pages.fim-fields`, `fim-stored`, `fim-restart`; Rust `fim_settings_from_store` test `the_settings_pages_reach_the_vd_fim_settings_type` |
| AC-10.2.5 | the key checks above + `KEY-CONTRACT.md` §"The FIM API key is not a store value" + Rust tests `the_environment_key_wins_over_the_user_file`, `a_world_readable_key_file_is_refused_not_used`, `an_existing_loose_key_file_is_tightened_before_the_secret_is_written`, `a_successful_write_leaves_only_the_key_file_behind` |
| AC-10.1.1.1 container | `QTabWidget` + `SettingsPanel::toolsForm()` is the extension point the i18n lane adds the language row to; language selection itself is T14 (`i18n-runtime`) |

## Limits — what is NOT proven here

1. **Product FIM request path is not wired to this page yet.** The values reach
   `vd_fim::FimSettings` (`vd-ffi/src/settings.rs::fim_settings_from_store`, tested),
   but the product's `vd_fim_complete` still loads `vd_config`'s `[fim]` block
   (`crates/vd-ffi/src/fim.rs::load_settings`), which is outside this task's write
   scope. One line there (`fim_settings_from_store(&settings_path, env…)`) closes
   it. Until then AC-10.2.4 is proven at the library boundary, not end to end.
2. **Consumers of the selectors** (backend switch, lint engine, `-I`/`-D` passing to
   argv, ghost-text switch) are the T12/T13/T17 lanes; this task owns the editor-level
   keys and the Rust projection, not the wiring into the LSP/lint argv.
3. **`docs/` wording** for the 0600 key file is out of this task's write scopes; the
   rule is documented in `crates/vd-settings/src/schema.rs` (module doc),
   `settings_keys.h`, `KEY-CONTRACT.md` and the FIM page's own hint label. A
   `docs/USER_GUIDE.md` sentence is a follow-up for the documentation lane.
4. **Workspace-wide gates** (`cargo test --workspace`, the full clippy run, the
   whole-product link) are RED whenever another lane is mid-edit; during this task
   that was `vd-lsp`/`vd-deeplint`, and at the security repair it was
   `src/workspace/project/project_service.cpp` (`std::vector ... has no member
   named 'append'`). T10's own target builds (`cmake --build ... --target
   vedit_workspace_settings vedit-workspace-settings-selftest`, `-Werror`) and its
   ctest are green; the final tree is the final-acceptance lane's measurement.
5. `crates/vd-ffi/include/vd_ffi.h` is dirty in this tree from T138 (another lane's
   new ABI functions). T10 adds no C ABI symbol; the settings ABI
   (`vd_settings_load`/`vd_settings_save`/`vd_settings_poll`/`vd_settings_free`) is
   unchanged.
