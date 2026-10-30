# AppImage packaging (T23)

One command: `bash packaging/appimage/run-evidence.sh` — builds the package
environment from the pinned build image, assembles the AppDir from pinned
sources (verible URL+sha256, slang artifact pin, GtkSourceView from the build
image), runs the GLIBC and RUNPATH gates, emits
`out/VEdit-<version>-x86_64.AppImage`, then runs the offline clean-room
acceptance (`Dockerfile.test`, no GTK, no CJK font, Xvfb).

Evidence, decisions and handoffs: `evidence/appimage/README.md`.
