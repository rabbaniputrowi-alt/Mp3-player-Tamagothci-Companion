#!/usr/bin/env python3
"""
build_albums.py — generates albums.h / albums.cpp for the Stardance disc renderer.

This is the --album pipeline referenced in disc_renderer's design: each cover
image is resized to a square, corner pixels (which the renderer's radius mask
never samples) are blacked out so the quantizer doesn't waste palette slots on
them, then quantized to a 256-color indexed palette. Output is a flat
{palette[256], pixels[ART_W*ART_W]} pair per album, written as const arrays so
they live in flash — never hand-edit the .cpp, regenerate it instead.

Usage:
    python3 build_albums.py --manifest albums.json --out ./firmware --size 240

manifest format (albums.json):
[
  { "title": "Song Name", "artist": "Artist Name", "image": "covers/song.jpg" },
  ...
]

Requires: Pillow (pip install pillow --break-system-packages)
"""

import argparse
import colorsys
import json
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: pip install pillow --break-system-packages")


def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def correct_panel_hue(r: int, g: int, b: int, strength: float):
    """Pre-compensate for this panel's measured red/green hue bias.

    Eyeballed against the physical GC9A01 (not a colorimeter): pure red
    (hue 0) displays as orange and pure green (hue 120) displays as lime —
    both pulled toward yellow (hue 60) — while pure blue (hue 240) reads
    correctly. This rotates hues in the red/yellow/green arc away from 60
    before they reach the panel, so the *displayed* result lands back near
    the true hue. Effect tapers to zero by 120 degrees from either anchor
    (i.e. by the time it reaches blue), since blue measured correct and an
    untapered correction would visibly band the palette right at that
    boundary. `strength` is unitless: 0 disables it, ~0.25 is a first
    guess, raise/lower and regenerate to match what you actually see.
    """
    if strength == 0:
        return r, g, b

    h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    hue = h * 360.0

    def circ_dist(a, bdeg):
        d = abs(a - bdeg) % 360
        return min(d, 360 - d)

    dist = min(circ_dist(hue, 0.0), circ_dist(hue, 120.0))
    weight = max(0.0, 1.0 - dist / 120.0)
    if weight == 0.0:
        return r, g, b

    offset = (hue - 60.0 + 180.0) % 360.0 - 180.0  # wrap into [-180, 180]
    hue = (60.0 + offset * (1.0 + strength * weight)) % 360.0

    r2, g2, b2 = colorsys.hsv_to_rgb(hue / 360.0, s, v)
    return round(r2 * 255), round(g2 * 255), round(b2 * 255)


def prepare_image(path: Path, size: int, radius: int) -> Image.Image:
    """Center-crop to square, resize to `size`, and black out pixels outside
    `radius` of center — those never get sampled by the rotation renderer,
    so keeping them out of the source image improves quantization quality."""
    img = Image.open(path).convert("RGB")

    # Center-crop to square first so resize doesn't distort aspect ratio.
    w, h = img.size
    side = min(w, h)
    left = (w - side) // 2
    top = (h - side) // 2
    img = img.crop((left, top, left + side, top + side))
    img = img.resize((size, size), Image.Resampling.LANCZOS)

    cx = cy = size / 2.0
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).ellipse(
        (cx - radius, cy - radius, cx + radius, cy + radius), fill=255
    )
    black = Image.new("RGB", (size, size), (0, 0, 0))
    return Image.composite(img, black, mask)


def quantize(img: Image.Image, colors: int = 256, hue_correction: float = 0.0):
    """Median-cut quantize to `colors`, return (palette_rgb565[256], indices[])."""
    q = img.quantize(
        colors=colors,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.FLOYDSTEINBERG,
    )
    pal = q.getpalette()  # flat [r,g,b, r,g,b, ...], length up to 256*3
    if pal is None:
        sys.exit("Quantization returned no palette — unexpected, check the source image.")

    palette565 = []
    for i in range(colors):
        if i * 3 + 2 < len(pal):
            r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
        else:
            r, g, b = 0, 0, 0  # pad unused slots with black
        r, g, b = correct_panel_hue(r, g, b, hue_correction)
        palette565.append(rgb888_to_rgb565(r, g, b))

    # q is palette mode ("P") after quantize, so tobytes() gives one index
    # byte per pixel directly — avoids the ImagingCore/getdata() typing gap.
    indices = list(q.tobytes())
    return palette565, indices


def c_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def format_array(values, per_line=16, hexfmt="0x{:02X}"):
    lines = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        lines.append("    " + ", ".join(hexfmt.format(v) for v in chunk) + ",")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", required=True, help="Path to albums.json")
    ap.add_argument("--out", required=True, help="Output directory for albums.h/.cpp")
    ap.add_argument("--size", type=int, default=240, help="Source art edge, must match ART_W in config.h (default 240)")
    ap.add_argument("--radius", type=int, default=118, help="Disc radius, must match DISC_R in config.h (default 118)")
    ap.add_argument("--hue-correction", type=float, default=0.25,
                     help="Compensation for the panel's measured red/green-toward-yellow hue bias. "
                          "0 disables it. Raise/lower and regenerate against what the panel actually shows (default 0.25)")
    args = ap.parse_args()

    manifest_path = Path(args.manifest)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    with open(manifest_path) as f:
        albums = json.load(f)

    if not albums:
        sys.exit("Manifest is empty — nothing to build.")

    manifest_dir = manifest_path.parent
    entries = []  # (title, artist, palette565, indices)

    for entry in albums:
        title = entry["title"]
        artist = entry.get("artist", "")
        image_path = manifest_dir / entry["image"]
        if not image_path.exists():
            sys.exit(f"Image not found: {image_path}")

        print(f"  {title} — {artist}  ({image_path.name})")
        img = prepare_image(image_path, args.size, args.radius)
        palette565, indices = quantize(img, hue_correction=args.hue_correction)
        entries.append((title, artist, palette565, indices))

    art_pixels = args.size * args.size

    # --- albums.h ---
    header = f"""#pragma once
#include <stdint.h>
#include "config.h"

// GENERATED by build_albums.py — do not hand-edit.
// Regenerate with: python3 build_albums.py --manifest {manifest_path.name} --out <firmware dir>

struct AlbumArt {{
  const char    *title;
  const char    *artist;
  const uint16_t palette[PALETTE_SIZE];
  const uint8_t  pixels[ART_PIXELS];
}};

extern const AlbumArt ALBUMS[];
extern const uint8_t  ALBUM_COUNT;
"""
    (out_dir / "albums.h").write_text(header)

    # --- albums.cpp ---
    parts = [
        '#include "albums.h"',
        "",
        "// GENERATED by build_albums.py — do not hand-edit.",
        "",
        "const AlbumArt ALBUMS[] = {",
    ]
    for title, artist, palette565, indices in entries:
        parts.append("  {")
        parts.append(f'    "{c_escape(title)}",')
        parts.append(f'    "{c_escape(artist)}",')
        parts.append("    {")
        parts.append(format_array(palette565, per_line=16, hexfmt="0x{:04X}"))
        parts.append("    },")
        parts.append("    {")
        parts.append(format_array(indices, per_line=20, hexfmt="0x{:02X}"))
        parts.append("    }")
        parts.append("  },")
    parts.append("};")
    parts.append(f"const uint8_t ALBUM_COUNT = sizeof(ALBUMS) / sizeof(ALBUMS[0]);")
    parts.append("")

    (out_dir / "albums.cpp").write_text("\n".join(parts))

    total_flash_kb = len(entries) * (art_pixels + 512) / 1024.0
    print(f"\nWrote albums.h / albums.cpp for {len(entries)} album(s) to {out_dir}/")
    print(f"Flash footprint: ~{total_flash_kb:.1f} KB ({art_pixels + 512} bytes/album)")
    print(f"Reminder: AlbumStore RAM working copy is {art_pixels/1024:.1f} KB + {512/1024:.1f} KB palette.")


if __name__ == "__main__":
    main()