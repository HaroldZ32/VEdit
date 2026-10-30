# T7 — workspace skeleton, reproducible build, GTK shell, gates

Cell `cell-20261030T020634155Z` · task **T7** · owner **Senior Engineer**
Write scope used: `Cargo.toml`, `Cargo.lock`, `rust-toolchain.toml`,
`crates/*/Cargo.toml`, `crates/vd-app/**`, `scripts/**`, `AGENT.md`, plus the two
gate-wiring files the captain's rulings made necessary (`build/gates/gates.sh`
delegating to `scripts/gates.sh`).
Governing contract: the cell record's skeleton contract (`contracts/T3.md`,
matching the dispatched T7 text) — branch A locked by T5
(`build/gui-spike/REPORT.md`): gtk-rs **0.18** generation + GtkSourceView
**4.6.1** built from source.

## 0. Verdict

**All three success criteria are met.**

| criterion | result | evidence |
|---|---|---|
| host and container gates green | `bash scripts/gates.sh` **exit 0**; container run **exit 0**; MPD gate `bun run verify:gates` **exit 0** | `12-gates-host.log`, `13-gates-container.log`, `14-gate-gates.txt` |
| a main window starts under Xvfb in the container | `--selftest smoke` **exit 0**, 10/10 checks pass; `xwininfo` lists `"Untitled - VEdit" 1000x720` | `09-selftest-smoke.json`, `10-xwininfo-tree.txt`, `11-xvfb-smoke.txt` |
| binary highest GLIBC symbol <= 2.17 | **GLIBC_2.16** (readelf -V and objdump -T agree); no `libstdc++` NEEDED | `04-elf-check.txt` |

One-command build: `bash scripts/build.sh` — pinned CentOS 7 image, release
build, ELF assertion, Xvfb smoke. Warm re-run **6 s**; cold run (image build +
first compile) ≈ 4 min on 16 CPUs. Two consecutive builds produced a
**byte-identical** binary (`7082ab7a…`) — preliminary determinism evidence; the
formal cold-rebuild comparison belongs to T27 (`15-determinism.txt`).

## 1. What exists now

Workspace (root `Cargo.toml`, `resolver = "2"`, edition 2021, MSRV 1.99):

```
crates/vd-app        GTK3 shell (this task, real code)
crates/vd-editor     stub (T9)      crates/vd-syntax    stub (T11)
crates/vd-config     stub (T13)     crates/vd-lsp       stub (T15)
crates/vd-deeplint   stub (T17)     crates/vd-fim       stub (T19)
```

`rust-toolchain.toml` pins **1.99.0** with rustfmt+clippy; `Cargo.lock` is
committed (191 packages).

`crates/vd-app` is the shell the later lanes extend:

- `main.rs` — `--selftest <suite>`, `--file`, `--version`, `--help`;
- `cli.rs` — argument parsing (+ 4 unit tests);
- `app.rs` — `GtkApplicationWindow`, `GtkSourceView` editor, toolbar, status
  bar, open/save wiring, initial-file load;
- `commands.rs` — **the command-dispatcher seam**: `CommandRegistry` +
  `CommandSpec { id, label, accels }`, installed as `app.<id>` `SimpleAction`s
  with accelerators. The shell's open/save/quit commands are built through it,
  so later lanes register shortcuts without touching `app.rs`;
- `document.rs` — thin `GtkTextBuffer` <-> file helpers (deliberately naive:
  T9 owns undo/redo, CRLF policy and atomic save);
- `selftest.rs` — `--selftest smoke` drives the **real** shell (same
  `Shell::new`, same activation handler, same command registry), prints one JSON
  report and exits 0 only when every check passes. `--probe-hold-ms <ms>` keeps
  the window alive for an external X probe.

`--selftest smoke` checks (all pass, `09-selftest-smoke.json`):
`window_created`, `window_mapped`, `window_title`, `editor_widget`,
`status_bar`, `commands_registered` (open/save/quit with accelerators),
`actions_installed`, `accelerator_applied`, `document_roundtrip` (write → load
into buffer → save → compare), `toolkit_versions` (GTK 3.22.30, GLib >= 2.56).

Out of scope here, as the contract says: highlighting semantics, LSP, slang,
FIM, panels/gutter (they are T9/T11/T13/T15/T17/T19/T21).

## 2. The build (one command, AC-19 oriented)

`scripts/build.sh` stages:

1. build the pinned image from `scripts/docker/Dockerfile.build`:
   `docker.m.daocloud.io/library/centos@sha256:be65f488…` (digest, not tag),
   CentOS 7.9.2009 vault mirror, GTK3 3.22.30 devel, GtkSourceView 4.6.1 from
   the sha256-pinned upstream tarball into `/opt/gsv4`, Rust 1.99.0 via a
   sha256-pinned `rustup-init`, meson 0.59.4 / ninja 1.11.1 from sha256-pinned
   PyPI wheels (no pip, no unpinned index);
2. `cargo build --release --workspace` inside the image, then strip and copy to
   `build/artifacts/vd-app`, then start `Xvfb :99` and run
   `vd-app --selftest smoke --probe-hold-ms 2500` while `xwininfo -root -tree`
   is captured;
3. on the host: `readelf`/`objdump` assertions (highest GLIBC <= 2.17, no
   libstdc++), sha256/size recording;
4. record external inputs — the T3 slang artifact is **consumed, not rebuilt**:
   `build/slang/out/slang` sha256 `63cb60a0…` matches the pin in
   `scripts/versions.env`.

Observed artifact: `build/artifacts/vd-app`, 562,168 bytes stripped,
sha256 `7082ab7a4e83cbbadbad05f9fe8862942c870190c625b882833a6097cdf2c666`,
NEEDED = the GTK3/GtkSourceView stack + `libgcc_s`, `librt`, `libpthread`,
`libdl`, `libc`, `ld-linux` (no `libstdc++`).

RPM reproducibility: the image's full installed set (382 packages) is committed
as `scripts/docker/rpm-baseline.txt` and **diffed on every build**
(`build-in-container.sh`); a difference fails the build
(`05-rpm-snapshot.txt`, `03-build-console.log` line "rpm set matches the
baseline (382 packages)").

`scripts/versions.env` is the single pin file (see §5); T25 extends it, never a
second copy.

## 3. Gates (host and container)

`scripts/gates.sh` runs:

1. **dependency policy** (pure shell, runs on the host too):
   `Cargo.lock` must not contain `tree-sitter*`, `openssl`, `openssl-sys` or
   `native-tls`, the whole gtk-rs family must stay on **0.18.x**, `sourceview4`
   on **0.5.x**;
2. `cargo fmt --all -- --check`;
3. `cargo clippy --workspace --all-targets -- -D warnings`;
4. `cargo test --workspace` (4 vd-app CLI tests, plus the stub crates).

Where it runs: this host has **no GTK development files and no root** to install
them, so the host invocation prints
`HOST-LACKS-GTK: …` and runs the three cargo gates in the pinned build image
(the deployment target), after doing the dependency-policy check locally.
Inside the image everything runs locally. No gate is skipped or faked; the
delegation is explicit in the log.

Observed (`12-gates-host.log`, `13-gates-container.log`, `14-gate-gates.txt`):
`PASS[lock]` on both, `cargo fmt` clean, `clippy -D warnings` clean,
`test result: ok. 4 passed; 0 failed`, `GATES PASS` — all exit 0.

The MPD-reachable gate `bun run verify:gates` now delegates its workspace part to
`scripts/gates.sh` (the `N/A` branch is only legal before this skeleton existed),
and also re-checks the slang artifact. It exits 0.

## 4. Dependency supersets (captain's addendum)

Pre-installed so the parallel lanes never race on `Cargo.lock` (which stays
owned by this task):

| crate | pre-installed deps |
|---|---|
| `vd-app` | gtk 0.18, glib 0.18, sourceview4 0.5, serde, serde_json, anyhow |
| `vd-config` | toml 1, serde (derive), serde_json, anyhow; dev-dep: tempfile 3 |
| `vd-lsp` | serde, serde_json, tokio 1 (rt-multi-thread/macros/process/io-util/time/sync), async-lsp 0.2, lsp-types 0.95 |
| `vd-deeplint` | serde, serde_json, tempfile 3, anyhow |
| `vd-fim` | ureq 3 (`default-features = false`, features `rustls` + `json`), serde, serde_json, anyhow |
| `vd-editor`, `vd-syntax` | **empty**, per the captain's list |

`async-lsp 0.2.4` depends on `lsp-types ^0.95.0`, so `lsp-types` is pinned to the
same 0.95 line and the lock contains exactly **one** `lsp-types` (0.95.1).

Hard constraints, mechanically proven by `PASS[lock]` and the lock contents:
`gtk 0.18.2` / `gtk-sys 0.18.2` / `glib 0.18.5` / `sourceview4 0.5.0`; **no**
`tree-sitter`; **no** `openssl`/`openssl-sys`/`native-tls` (FIM's TLS path is
`ureq 3.5.0` → `rustls 0.23.46` → `ring 0.17.14`).

**Stub API note:** the six lane stubs expose only `CRATE_VERSION` and a doc
comment; every API shape is intentionally left to its lane. Per the captain's
rule, a lane must **report to the captain before defining an API shape** (and
must not add dependencies or edit the root manifest / lock).

## 5. `scripts/versions.env` — pins (single source of truth)

| item | pinned value |
|---|---|
| base image | `docker.m.daocloud.io/library/centos@sha256:be65f488b7764ad3638f236b7b515b3678369a5124c47b8d32916d6487418ea4` |
| CentOS vault mirror | `https://mirrors.tuna.tsinghua.edu.cn/centos-vault/7.9.2009` |
| Rust toolchain | 1.99.0 |
| rustup-init | 1.29.2, sha256 `5be191e8…` |
| GtkSourceView | 4.6.1, sha256 `63f487a7…` |
| meson | 0.59.4, wheel sha256 `9db5bb89…` |
| ninja | 1.11.1, wheel sha256 `817e2aee…` |
| appimagetool (for T23/T25) | sha256 `793fca0c…` |
| slang artifact (consumed) | sha256 `63cb60a0…` |

The base image digest and the Yum snapshot were the two AC-19 gaps recorded in
`build/slang/REPRODUCIBILITY.md`; both are closed here, plus the FetchContent
equivalent for Rust (Cargo.lock checksums + `git rev-parse`-style commit
recording for the slang lane in build/slang/evidence/03-source.txt).

## 6. AGENT.md

- **§4 reviewed, not rewritten from scratch:** T3's commands were replaced by the
  real ones this task creates (`bash scripts/build.sh`, `bash scripts/gates.sh`,
  `cargo test/clippy/fmt`), plus the dependency-ownership rule and the slang
  build line.
- **§6 rewritten per the captain's ruling:** commits land directly on the default
  branch as linear history; branch-per-task is not used (board + heterogeneous
  review replace the PR step; disjoint write scopes carry "one writer per
  tree"); history is never rewritten; explicit-path staging only; role identity
  on commits; evidence under `evidence/`.
- AGENT.md is in this task's write scope, which is why the §6 edit lives here
  rather than in a separate task.

## 7. Findings other lanes need (carried from T5, now measured in the image)

- **AC-16 immodules ARE available in the vault**: `gtk3-immodule-xim` and
  `ibus-gtk3` install, giving `/usr/lib64/gtk-3.0/3.0.0/immodules/im-xim.so` and
  `im-ibus.so` (recorded in the image at `/opt/vedit/immodules.txt`). T23/T25 can
  bundle them; `gtk3-immodule-ibus` itself does not exist as a package.
- **No GL needed** (T5): Xvfb has no GLX and GTK/gsv render through cairo; this
  skeleton introduces no GL/EGL dependency.
- `appimagetool` is pinned in `scripts/versions.env` for T23/T25.
- The binary floor is **GLIBC 2.16**, i.e. headroom inside the 2.17 bound; T23/T25
  must repeat the assertion inside their packaged roots.
- A harmless GLib-GIO critical (`g_dbus_proxy_new_sync: assertion
  'G_IS_DBUS_CONNECTION (connection)'`) appears in `09-selftest-smoke.err` because
  the container has no session bus; it does not affect the selftest result.

## 8. What is NOT proven (honest bounds)

- **Host gate delegation**: the host has no GTK3/GtkSourceView development files
  and no root, so fmt/clippy/test run in the pinned image from the host command.
  The dependency-policy check does run on the host. If a reviewer insists on a
  host-native compile gate, the host needs `gtk3-devel` +
  `gtksourceview-4` development packages installed by an administrator.
- **Image reproducibility**: the image is built locally from pinned inputs, but
  Docker image IDs are not byte-reproducible (layer timestamps); the controls are
  the digest-pinned base, sha256-pinned downloads and the RPM baseline diff.
- **Cold-rebuild determinism** is not formally proven here: the two-build
  comparison used cached image + cargo target (T27 owns the cold comparison).
- The smoke test proves window creation/mapping and the shell wiring under
  Xvfb; it does not prove interactive behaviour (dialogs, typing, highlighting
  semantics) — those are T9/T11/T21 and the AC-02 packaging runs.
- Rust toolchain installation and crate downloads need network on the first
  image build (the contract allows build-time network).

## 9. Reproduce

```sh
bash scripts/build.sh     # one command: image -> build -> ELF <= 2.17 -> Xvfb smoke
bash scripts/gates.sh     # host + pinned-image gates (fmt/clippy/test + policy)
bun run verify:gates      # MPD-reachable gate (also re-checks the slang artifact)
```

Evidence: this directory (`00-INDEX.md` lists every file with its command and exit
code). Artifact: `build/artifacts/vd-app` (gitignored, rebuild with `build.sh`).
