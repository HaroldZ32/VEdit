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
