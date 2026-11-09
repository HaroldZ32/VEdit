#!/usr/bin/env python3
"""Build the VEdit application icon set from the reference artwork.

WHY this script exists: the reference PNGs (`.mpd/recon/icon-ref/`) are full
2160x2160 page renders — the mark sits on a flat #ECEFF3 page and carries no
usable alpha channel. Every delivery surface (in-process window icon, .desktop
entry, AppDir root icon and .DirIcon, per-size hicolor tree) needs a
transparent, cropped PNG at its own size, so the conversion is generated here
once instead of being hand-edited eleven times.

WHY the silhouette comes from the dark variant: the three renders share one
rounded-square outline (measured: identical flood-fill result for 1.png and
3.png), but the LIGHT render's own background gradient sits within 4..8 levels
of #ECEFF3 — i.e. inside the contract's tolerance of 12. Filling that variant
from the four corners therefore walks straight through the artwork (measured:
only 20.3 % of the canvas stays opaque instead of 95.8 %). The dark render has
maximum contrast, so its corner flood fill is the authoritative silhouette and
all three variants are masked with it; the light variant keeps its own pixels,
including the near-page-grey areas, and gets a clean transparent corner.

Build-time only: Pillow is a build tool here, never a product runtime
dependency. Re-runnable and idempotent — the outputs are a pure function of the
inputs (no timestamps, no randomised PNG chunks).
"""

from __future__ import annotations

import hashlib
import math
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / ".mpd" / "recon" / "icon-ref"
ICONS = ROOT / "resources" / "icons"

# The page colour of every reference render, per the frozen contract (§3.1).
PAGE_GRAY = (236, 239, 243)  # #ECEFF3
TOLERANCE = 12
PADDING = 0.02

# 1=dark page colour, 2=light page colour, 3=dark monochrome.
VARIANTS = {"dark": "1.png", "light": "2.png", "mono": "3.png"}
HICOLOR_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
APP_SIZE = 512
HICOLOR_APP = "io.github.vedit.VEdit.png"

LANCZOS = getattr(Image, "Resampling", Image).LANCZOS


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def opacity_fraction(alpha: Image.Image) -> float:
    """Fraction of pixels that are not transparent (used only for diagnostics)."""
    histogram = alpha.histogram()
    total = alpha.size[0] * alpha.size[1]
    return sum(histogram[255:]) / total if total else 0.0


def outer_alpha(rgb: Image.Image, tolerance: int = TOLERANCE) -> Image.Image:
    """Alpha 0 for every pixel 4-connected to a corner through page-grey pixels.

    The threshold is applied to the maximum per-channel difference from
    #ECEFF3, then a flood fill from each of the four corners marks the connected
    outside region. The fill value 128 cannot collide with the two mask values
    (0 = artwork, 255 = page-grey), so the pass is unambiguous.
    """
    page = Image.new("RGB", rgb.size, PAGE_GRAY)
    red, green, blue = ImageChops.difference(rgb, page).split()
    distance = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    near_page = distance.point(lambda value: 255 if value <= tolerance else 0)

    width, height = rgb.size
    for corner in ((0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1)):
        if near_page.getpixel(corner) == 255:
            ImageDraw.floodfill(near_page, corner, 128, thresh=0)
    return near_page.point(lambda value: 0 if value == 128 else 255)


def crop_and_pad(rgb: Image.Image, alpha: Image.Image) -> Image.Image:
    """Crop to the opaque bbox, keep 2 % padding, and square the canvas.

    The square canvas matters: hicolor entries are square by definition, and a
    non-square source would silently stretch when resized to NxN.
    """
    box = alpha.getbbox()
    if box is None:
        fail("alpha mask is empty — nothing opaque left after the flood fill")
    cropped_rgb = rgb.crop(box)
    cropped_alpha = alpha.crop(box)
    width, height = cropped_rgb.size
    side = max(width, height)
    pad = int(math.ceil(side * PADDING))
    canvas_side = side + 2 * pad

    content = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    content.paste(cropped_rgb, (0, 0))
    content.putalpha(cropped_alpha)

    canvas = Image.new("RGBA", (canvas_side, canvas_side), (0, 0, 0, 0))
    canvas.paste(content, (pad + (side - width) // 2, pad + (side - height) // 2))
    return canvas


def emit(path: Path, image: Image.Image, rows: list[tuple[str, str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PNG")
    relative = path.relative_to(ROOT).as_posix()
    rows.append((relative, f"{image.size[0]}x{image.size[1]}", sha256(path)))


def verify_png(path: Path, label: str) -> None:
    """Corner alpha 0 and centre opaque — the contract's A4 pixel criterion."""
    with Image.open(path) as image:
        rgba = image.convert("RGBA")
        width, height = rgba.size
        corners = [
            rgba.getpixel((0, 0))[3],
            rgba.getpixel((width - 1, 0))[3],
            rgba.getpixel((0, height - 1))[3],
            rgba.getpixel((width - 1, height - 1))[3],
        ]
        centre = rgba.getpixel((width // 2, height // 2))[3]
    status = "ok" if max(corners) == 0 and centre == 255 else "FAIL"
    print(
        f"verify {label}: {width}x{height} corner_alpha={corners} "
        f"centre_alpha={centre} -> {status}"
    )
    if status != "ok":
        fail(f"{label} failed the alpha criterion")


def main() -> None:
    missing = [str(SRC_DIR / name) for name in VARIANTS.values() if not (SRC_DIR / name).is_file()]
    if missing:
        fail("reference artwork missing: " + ", ".join(missing))
    if not (SRC_DIR / "1.png").is_file():
        fail("the dark variant (1.png) is required to derive the silhouette")

    rows: list[tuple[str, str, str]] = []

    # --- 1. byte-identical source copies (INPUT provenance, not deliverables) --
    print("== source copies (byte-identical originals) ==")
    for variant, name in VARIANTS.items():
        source = SRC_DIR / name
        target = ICONS / "src" / f"vedit-{variant}.png"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        source_sha = sha256(source)
        target_sha = sha256(target)
        if source_sha != target_sha:
            fail(f"copy is not byte-identical: {target}")
        rows.append((target.relative_to(ROOT).as_posix(), f"{Image.open(target).size[0]}x{Image.open(target).size[1]}", target_sha))
        print(f"  {target.relative_to(ROOT).as_posix()}  sha256={target_sha}  (from {name})")

    # --- 2. silhouette: corner flood fill on the dark variant ------------------
    print("== silhouette (four-corner flood fill, tolerance %d) ==" % TOLERANCE)
    rgb = {variant: Image.open(SRC_DIR / name).convert("RGB") for variant, name in VARIANTS.items()}
    alpha = {variant: outer_alpha(images) for variant, images in rgb.items()}
    for variant in VARIANTS:
        print(
            f"  {variant:5s} independent fill: opaque={opacity_fraction(alpha[variant]):.4f} "
            f"bbox={alpha[variant].getbbox()}"
        )
    if outer_alpha(rgb["mono"]).tobytes() != outer_alpha(rgb["dark"]).tobytes():
        fail("dark and mono silhouettes differ — the shared-silhouette assumption is broken")
    reference = opacity_fraction(alpha["dark"])
    if opacity_fraction(alpha["light"]) < 0.5 * reference:
        print(
            "  light: its own fill leaked into the artwork "
            f"({opacity_fraction(alpha['light']):.4f} vs {reference:.4f} opaque) — "
            "using the shared dark silhouette"
        )
    silhouette = alpha["dark"]
    if opacity_fraction(silhouette) < 0.5:
        fail("silhouette suspiciously small — the flood fill ate the artwork")

    # --- 3. masters: 512x512 app icons (qrc-embedded) -------------------------
    print("== app masters (512x512, :/icons/app/) ==")
    canvases = {variant: crop_and_pad(rgb[variant], silhouette) for variant in VARIANTS}
    masters: dict[str, Image.Image] = {}
    for variant in VARIANTS:
        master = canvases[variant].resize((APP_SIZE, APP_SIZE), LANCZOS)
        masters[variant] = master
        emit(ICONS / "app" / f"vedit-{variant}.png", master, rows)

    # --- 4. hicolor tree from the LIGHT variant -------------------------------
    print("== hicolor theme (light variant, Lanczos) ==")
    for size in HICOLOR_SIZES:
        if size == APP_SIZE:
            image = masters["light"]
        else:
            image = canvases["light"].resize((size, size), LANCZOS)
        emit(ICONS / "hicolor" / f"{size}x{size}" / "apps" / HICOLOR_APP, image, rows)

    # --- 5. assertions + report ----------------------------------------------
    required = [ICONS / "app" / f"vedit-{variant}.png" for variant in VARIANTS]
    required += [ICONS / "hicolor" / f"{size}x{size}" / "apps" / HICOLOR_APP for size in HICOLOR_SIZES]
    for path in required:
        if not path.is_file() or path.stat().st_size == 0:
            fail(f"required artifact missing or empty: {path.relative_to(ROOT).as_posix()}")
    for size in (16, APP_SIZE):
        path = ICONS / "hicolor" / f"{size}x{size}" / "apps" / HICOLOR_APP
        with Image.open(path) as image:
            if image.size != (size, size):
                fail(f"{path.name} in {size}x{size} is {image.size}")
    print(f"assert: hicolor 16x16 and {APP_SIZE}x{APP_SIZE} exist with the expected dimensions")

    print("== alpha criterion (corner 0 / centre 255) ==")
    for size in (16, 256, 512):
        verify_png(ICONS / "hicolor" / f"{size}x{size}" / "apps" / HICOLOR_APP, f"hicolor {size}x{size}")
    verify_png(ICONS / "app" / "vedit-light.png", "app light")

    print("== artifacts ==")
    for relative, dimensions, digest in rows:
        print(f"  {relative}  {dimensions}  sha256={digest}")
    print(f"OK: {len(rows)} artifacts written under {ICONS.relative_to(ROOT).as_posix()}/")


if __name__ == "__main__":
    main()
