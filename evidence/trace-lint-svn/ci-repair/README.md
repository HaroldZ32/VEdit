# PR #30 native CI regressions

At `15b8d29`, the hosted Qt run failed three of 101 registered CTests. At `7512463`, the next full hosted test pass ran each of those three successfully and identified one additional document-URI failure. The first attempt at that second run had stopped during package installation with HTTP 503 from the Ubuntu snapshot, before reaching CTest. [Original CTest run](https://github.com/HaroldZ32/VEdit/actions/runs/45978580740) · [Hosted rerun and Qt logs](https://github.com/HaroldZ32/VEdit/actions/runs/46004060211/attempts/2)

| Test | Regression and local control |
| --- | --- |
| `vedit-verdict-retention-selftest` | The fake language service rejected edited buffers. After implementing its buffer acceptance and publication behavior, all 29 checks passed, including retained errors across file switches. |
| `vedit-diagnostics-ffi-selftest` | An unpublished snapshot ended the pending diagnostics check before Verible published the syntax error. The real client now reports the error in an unsaved edit, preserves the file on disk and clears the error on restore. All 35 checks passed. |
| `vedit-terminal-selftest` | A real PTY can deliver all 4 MiB without any single activation reaching the 64 KiB cap. The deterministic fixture supplies 65,552 bytes to the production drain function and checks for the final 16 bytes on a subsequent activation. The real PTY still verifies the per-activation bound and exact delivery. Disabling the cap or removing the queued continuation fails the fixture. |
| `vedit-toolchain-ux-selftest` | With `..` in the fixture path, the client looked up a different URI for the incoming Verible publication than for the outgoing open. The identical registered test went from failing in 5.13 s to passing 43 checks in 0.18 s, with an explicit empty diagnostic set and five symbols. Startup, buffer edits and requests use consistent document URIs. |

The Rust `buffer_change` regression also opens two unsaved files under missing directories, one addressed through a symlink followed by `..`. Verible returns a distinct module symbol from each buffer. The physical alias resolves the first symbol, and both files stay off disk. The pinned local run passed all 78 `vd-lsp` tests and strict Clippy. Eight related Qt CTests and the 135-check Xvfb recovery probe passed. The depth-switch test skips its module-tree-only assertion in the diagnostics-only executable.

```sh
cargo test --locked --offline -p vd-lsp --lib --tests
cargo clippy --locked --offline -p vd-lsp --all-targets -- -D warnings
ctest --test-dir target/qt6-build --output-on-failure -R '^vedit-(verdict-retention|diagnostics-ffi|terminal|toolchain-ux)-selftest$'
```

The native probes used the repository's pinned Qt/KF6 image, the pinned Verible binary and the local slang compiler. The additional Slang language-server checks were run with the server available. Timing measurements are from the synthetic fixtures on that local environment.
