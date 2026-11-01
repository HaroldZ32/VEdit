== appimagetool: vendored, hash-pinned, reproducible (T31 decision) ==
collected_at: 2026-11-01T08:16:30Z
git_head_at_collection: a4b8efd0890860ac5134e739c72965f9744d13d0
# note: the provisional AppImage itself was built from baseline 206033c (see 07);
# HEAD moved to a4b8efd (service.rs + T110 commits) while this pin evidence was collected.

## measured drift of the upstream 'continuous' asset (2026-11-01, two downloads):
got  ac7fd56feade6002c2f57db39587113275d351fce911a5fbb29ea84cd0666cab  (15,137,272 B)
want 793fca0ce4bcc309dc65d3f66669f58e3755643e4dadbe03932e7b436128494f  (scripts/versions.env pin)

## decision: vendor the hash-matching binary IN THE REPO
path: packaging/appimage/vendor/appimagetool-x86_64 (no .AppImage suffix, so the repo's '*.AppImage' ignore rule does not hide it)
sha256: 793fca0ce4bcc309dc65d3f66669f58e3755643e4dadbe03932e7b436128494f
bytes:  15104504
why: versioned upstream assets are not available for this tool; pinning 'continuous' fails closed whenever upstream republishes. The vendored copy is offline-reproducible and still fails closed on a sha mismatch.

## builders prefer the vendored copy (cache/network is only a deliberate-refresh fallback)
  packaging/runtime/build-appimage.sh:38:# Reproducibility: the tool is VENDORED with the pinned sha256, not downloaded
  packaging/runtime/build-appimage.sh:42:VENDORED="$ROOT/packaging/appimage/vendor/appimagetool-x86_64"
  packaging/runtime/build-appimage.sh:44:if [ -x "$VENDORED" ]; then TOOL="$VENDORED"; fi
  packaging/runtime/build.sh:35:VENDORED="$ROOT/packaging/appimage/vendor/appimagetool-x86_64"
  packaging/runtime/build.sh:37:if [ -x "$VENDORED" ]; then TOOL="$VENDORED"; fi

## reproduction with the vendored tool: byte-identical AppImage
cmd: packaging/runtime/build-appimage.sh --out target/t31-tool-test <AppDir>
APPIMAGE=/home/haroldzhao/MyProj/DshProj/VEdit/target/t31-tool-test/VEdit-0.1.1-x86_64.AppImage
APPIMAGE_SHA256=85df9545bf4a93211a435ccf3dd29fabefc17d8ef3208100133a9dfe53d2aa11
cmp with packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage: BIT-IDENTICAL=YES

## fail-closed retained
build-appimage.sh and build.sh still compare the chosen tool's sha256 with scripts/versions.env and abort on mismatch (no warn-and-continue).
## docs
packaging/runtime/README.md (appimagetool section) and packaging/appimage/README.md
