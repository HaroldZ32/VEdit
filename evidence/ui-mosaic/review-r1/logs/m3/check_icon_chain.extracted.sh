check_icon_chain() { # <label> <root>
  local label=$1 root=$2 required png magic
  if [ -s "$root/vedit.desktop" ]; then
    grep -qx 'Icon=io.github.vedit.VEdit' "$root/vedit.desktop" \
      && pass "$label: vedit.desktop carries Icon=io.github.vedit.VEdit" \
      || fail "$label: vedit.desktop lacks Icon=io.github.vedit.VEdit"
    grep -qx 'StartupWMClass=io.github.vedit.VEdit' "$root/vedit.desktop" \
      && pass "$label: vedit.desktop carries StartupWMClass=io.github.vedit.VEdit" \
      || fail "$label: vedit.desktop lacks StartupWMClass=io.github.vedit.VEdit"
  else
    fail "$label: vedit.desktop missing or empty"
  fi
  # Every size assemble-appdir.sh installs must be asserted here: asserting a
  # subset let 24/32/48/128 disappear from a packaged AppDir while this probe
  # stayed green (R1/M3). The list is the SAME eight tiers the installer writes.
  for required in io.github.vedit.VEdit.png .DirIcon \
                  usr/share/icons/hicolor/16x16/apps/io.github.vedit.VEdit.png \
                  usr/share/icons/hicolor/24x24/apps/io.github.vedit.VEdit.png \
                  usr/share/icons/hicolor/32x32/apps/io.github.vedit.VEdit.png \
                  usr/share/icons/hicolor/48x48/apps/io.github.vedit.VEdit.png \
                  usr/share/icons/hicolor/64x64/apps/io.github.vedit.VEdit.png \
                  usr/share/icons/hicolor/128x128/apps/io.github.vedit.VEdit.png \
                  usr/share/icons/hicolor/256x256/apps/io.github.vedit.VEdit.png \
                  usr/share/icons/hicolor/512x512/apps/io.github.vedit.VEdit.png; do
    if [ -s "$root/$required" ]; then
      pass "$label: icon artifact present: $required ($(stat -c%s "$root/$required") bytes)"
    else
      fail "$label: icon artifact missing or empty: $required"
    fi
  done
  # A 0-byte or non-PNG launcher icon only shows up as a generic placeholder on
  # the user's desktop, so the signature is asserted here, not just the path.
  for png in io.github.vedit.VEdit.png .DirIcon; do
    magic=$(head -c 8 "$root/$png" 2>/dev/null | od -An -tx1 | tr -d ' \n')
    [ "$magic" = "89504e470d0a1a0a" ] \
      && pass "$label: $png is a PNG (magic ok)" \
      || fail "$label: $png is not a PNG (magic=${magic:-<absent>})"
  done
}
