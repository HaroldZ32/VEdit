# UI-Mosaic A4: an installed launcher whose icon files are absent is worse than
# no icon at all — the desktop shows a generic placeholder and never says why.
# Missing or EMPTY artifacts in the icon chain therefore fail the build.
for icon_required in vedit.desktop io.github.vedit.VEdit.png .DirIcon \
                     usr/share/icons/hicolor/16x16/apps/io.github.vedit.VEdit.png \
                     usr/share/icons/hicolor/64x64/apps/io.github.vedit.VEdit.png \
                     usr/share/icons/hicolor/256x256/apps/io.github.vedit.VEdit.png \
                     usr/share/icons/hicolor/512x512/apps/io.github.vedit.VEdit.png; do
  [ -s "$OUT/$icon_required" ] || {
    echo "ERROR: icon-chain artifact missing or empty: $icon_required" >&2; exit 1; }
done
png_magic=$(head -c 8 "$OUT/io.github.vedit.VEdit.png" | od -An -tx1 | tr -d ' \n')
[ "$png_magic" = "89504e470d0a1a0a" ] || {
  echo "ERROR: io.github.vedit.VEdit.png is not a PNG (magic=$png_magic)" >&2; exit 1; }
grep -qx 'Icon=io.github.vedit.VEdit' "$OUT/vedit.desktop" || {
  echo "ERROR: vedit.desktop lacks Icon=io.github.vedit.VEdit" >&2; exit 1; }
grep -qx 'StartupWMClass=io.github.vedit.VEdit' "$OUT/vedit.desktop" || {
  echo "ERROR: vedit.desktop lacks StartupWMClass=io.github.vedit.VEdit" >&2; exit 1; }
log "icon invariants: vedit.desktop + StartupWMClass + root/16/64/256/512 PNGs present"
