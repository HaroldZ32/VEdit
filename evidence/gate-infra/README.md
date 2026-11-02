# gate-infra evidence — T54 (T5 Medium #1: gates must run where the toolchain is)

## The defect (proof: `00-before-typecheck-host.log`)

`bash build/gates/typecheck.sh` on this host:

1. the C++/Qt6 half runs in the pinned image and **PASSES**;
2. the Rust half ran `cargo` **on the host**, where the pinned `1.99.0` rustup
   toolchain has no cargo binary:
   `error: the 'cargo' binary ... is not applicable to the '1.99.0-...' toolchain`;
3. it then fell back to `cargo check --workspace` — which fails the same way.
   (That fallback was itself a *downgrade*; it is gone.)

Result: `TYPECHECK-EXIT=1` while the same tree's Rust crates are clippy-clean on
the pinned toolchain — i.e. the gate was measuring the host, not the product.

## The change

`build/gates/typecheck.sh`:
* the Rust half now uses the **same delegation as `scripts/gates.sh`**: try the
  host toolchain only if `cargo clippy --version` really works; otherwise
  `source scripts/lib.sh; load_versions; build_build_image; run_in_build_container`
  and run the **identical** command inside the pinned CentOS 7 image:
  `cargo clippy --workspace --all-targets -- -D warnings`.
* the `cargo check` fallback is removed. There is no `|| true`, no `set +e`, no
  skipped item and no pass-on-failure path: the severity and scope are unchanged,
  only the execution site moved to the image that owns the pinned toolchain.
* a container banner (`[typecheck] container facts: CentOS ... | rustc ... |
  cargo ...`) makes the delegation visible in the raw log.

`build/gates/gates.sh` (finding #2): when `build/slang/out/slang` is missing the
gate already ran the documented one-command build; it now (a) says up front that
the from-source build will take minutes and where its logs go, and (b) if that
build fails, prints an actionable error (missing path, the exact command, the log
directory) and **fails** — it never skips and never passes without the artifact.

## Evidence

| file | what it shows |
|---|---|
| `00-before-typecheck-host.log` | pre-fix: Qt half PASS, Rust half dies on the host cargo + the old `cargo check` downgrade |
| `01-after-typecheck-delegated.log` | post-fix: Rust half runs **inside the pinned image** (container banner), same command, exit 101 because of a **real** clippy finding |
| `02-falsification-injected.log` | a deliberate clippy error (`format!` with no args) in `crates/vd-search` ⇒ gate RED with `error: useless use of format!` |
| `03-falsification-restored.log` | probe removed (`git diff -- crates/vd-search` empty) ⇒ that error is gone from the log; the Rust half still runs in the image |
| `20-slang-build-attempt.log` | the documented `bash build/slang/build.sh` run (artifact produced) |
| `21-gates-slang-missing-path.log` | `gates` with a broken docker environment: the artifact step is passed, and the failure is reported, never hidden |
| `22-gates-with-artifact.log` | `gates` with the artifact present (version printed from a centos:7 container), then the remaining red for a reason outside this task |
| `30-other-gates.log` | `docs` PASS, `manifest` PASS, `rows`/`comments` are the repository's declared N/A→RED ids (unchanged by this task) |

## Honest boundary

The final green run of `typecheck`/`gates` is blocked by **other lanes' in-flight
work**, not by the gate infrastructure:

* `crates/vd-config/tests/project_model_fixture.rs` → `clippy::needless_question_mark`
  (T6 / SE3, in progress) — makes `typecheck` RED after the delegation;
* `crates/vd-lsp/tests/session_file_set.rs` → `cargo fmt --all -- --check` diff
  (T8 / SE4, in progress) — makes `gates` RED at the fmt step.

Both files are outside T54's write scope; they are reported, not touched. What the
evidence proves is the infrastructure: the Rust half really executes in the pinned
image (container banner), really applies `-D warnings` (the injected probe turned
it RED), and the gates slang step now either builds the artifact with the
documented one-command path or fails with an actionable error.
