> **Legacy GTK product packaging.** This directory still packages the legacy
> Rust/GTK `vd-app` (kept by requirements non-goal 5). The **Qt delivery**
> (single-file AppImage of `vedit-app`) is produced by
> `packaging/runtime/build.sh`; do not present this pipeline as the final
> Qt AppImage.

# AppImage packaging (T23)

One command: `bash packaging/appimage/run-evidence.sh` — builds the package
environment from the pinned build image, assembles the AppDir from pinned
sources (verible URL+sha256, slang artifact pin, GtkSourceView from the build
image), runs the GLIBC and RUNPATH gates, emits
`out/VEdit-<version>-x86_64.AppImage`, then runs the offline clean-room
acceptance (`Dockerfile.test`, no GTK, no CJK font, Xvfb).

**Do not make this script build its own `vd-app`.** It consumes the shared
release artefact `build/artifacts/vd-app` (produced by `scripts/build.sh` with
`RELEASE_RUSTFLAGS`) and asserts byte equality with the packaged copy; that is
what guarantees the AppImage, tar.gz and `.run` install the same binary (T27
asserts it across products). Payload fixtures come from a `git archive HEAD`
export for the same reason: the artefact must correspond to a commit, not to
whatever the worktree contained at build time.

Evidence, decisions and handoffs: `evidence/appimage/README.md`.


## appimagetool (legacy pipeline too)

The legacy GTK builder now also prefers
`packaging/appimage/vendor/appimagetool-x86_64` (sha256 `793fca0c…`, the
`versions.env` pin) instead of the drifting upstream `continuous` download; the
sha check stays fail-closed. See `packaging/runtime/README.md` for the measured
drift (`ac7fd56f…` on 2026-11-01).
