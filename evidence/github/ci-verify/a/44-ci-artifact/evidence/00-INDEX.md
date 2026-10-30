# Evidence index — T7 (workspace skeleton, build, GTK shell, gates)

Every file here is captured output, not a retelling. Each entry names the command
that produced it and its exit code so a verifier can re-run it.

Reproduce everything with:

```sh
bash scripts/build.sh          # full pipeline (image -> build -> ELF -> Xvfb smoke)
bash scripts/gates.sh          # host policy check + fmt/clippy/test in the pinned image
bun run verify:gates           # the MPD-reachable gate (also re-checks the slang artifact)
```

| file | produced by | shows | exit |
|---|---|---|---|
| `T7-REPORT.md` | written by T7 | the complete report for this task (criteria, design, bounds) | — |
| `00-pins.txt` | `bash scripts/build.sh` | every pinned input from `scripts/versions.env` as used in the run | 0 |
| `01-image-build.log` | `docker build` via `scripts/lib.sh:build_build_image` | image build: digest-pinned base, vault repo, GTK devel packages, rpm baseline check prep, pinned wheels, Rust 1.99.0, GtkSourceView 4.6.1 | 0 |
| `02-image-id.txt` | `docker image inspect` | image tag/id and the digest-pinned base reference | 0 |
| `03-build-console.log` | `bash scripts/build.sh` | full console of the one-command build and Xvfb smoke | 0 |
| `04-container-env.txt` | `build-in-container.sh` | container facts: CentOS 7.9.2009, glibc 2.17-326, gtk3 3.22.30, gsv4 4.6.1, rustc 1.99.0, meson/ninja | 0 |
| `04-elf-check.txt` | `scripts/build.sh` stage 3 (host) | `file`, `readelf -d` NEEDED, `readelf -V` GLIBC versions, `objdump -T` max, sha256, size | 0 |
| `05-artifact-copy.txt` | `scripts/build.sh` | copy of the artifact into `build/artifacts/vd-app` with sha256/size | 0 |
| `05-rpm-diff.txt` | `build-in-container.sh` | empty → the installed RPM set equals `scripts/docker/rpm-baseline.txt` (382 packages) | 0 |
| `05-rpm-snapshot.txt` | `rpm -qa` in the image | the full installed package set of the build image | 0 |
| `06-cargo-build.log` | `cargo build --release --workspace` | release build output (`Finished release profile`) | 0 |
| `06-external-inputs.txt` | `scripts/build.sh` stage 4 | the T3 slang artifact is consumed, not rebuilt: sha256 matches the pin, `--version` prints | 0 |
| `07-artifact-container.txt` | `build-in-container.sh` | artifact sha256/size/`file`/`ldd` inside the image | 0 |
| `08-xvfb.log` | `Xvfb :99` | X server log (empty = no errors) | 0 |
| `09-selftest-smoke.json` | `vd-app --selftest smoke --probe-hold-ms 2500` | structured report: `result: pass`, 10/10 checks (window, editor, status, commands, accels, roundtrip, toolkit) | 0 |
| `09-selftest-smoke.err` | same run, stderr | the harmless GLib-GIO critical (no session bus in the container) | 0 |
| `10-xwininfo-tree.txt` | `xwininfo -root -tree` during the hold | the X server lists `"Untitled - VEdit": ("vd-app" "Vd-app") 1000x720` | 0 |
| `11-xvfb-smoke.txt` | `build-in-container.sh` | `selftest_exit=0` and the VEdit window entry | 0 |
| `12-gates-host.log` | `bash scripts/gates.sh` (host) | host: `PASS[lock]`, `HOST-LACKS-GTK` delegation note, then container: fmt/clippy/test/all green, `GATES PASS` | 0 |
| `12-gates-image-build.log` | `scripts/gates.sh` | the image build performed by the gate path (cached) | 0 |
| `13-gates-container.log` | `run_in_build_container bash scripts/gates.sh` | inside the pinned image: `PASS[lock]`, fmt check, clippy `-D warnings`, `test result: ok. 4 passed`, `GATES PASS (local)` | 0 |
| `14-gate-gates.txt` | `bun run verify:gates` | MPD-reachable gate: slang ELF check then `scripts/gates.sh`, `[gates] PASS` | 0 |
| `15-determinism.txt` | two consecutive `bash scripts/build.sh` runs | both exit 0 and produce the **same** sha256 `7082ab7a…`; notes that the comparison used cached image/target and that T27 owns the cold comparison | 0 |

Notes:

- `target/` and `build/artifacts/` are gitignored (product+build outputs); the
  evidence above is what proves they were produced.
- The container runs as root inside a **rootless** Docker daemon, which maps to
  the calling host user, so files written through the mount stay host-owned.
- The host cannot compile the GTK stack itself (no development files, no root);
  `12-gates-host.log` records that delegation explicitly.
