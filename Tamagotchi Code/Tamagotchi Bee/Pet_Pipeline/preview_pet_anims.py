#!/usr/bin/env python3
"""
preview_pet_anims.py  --  see exactly what the GC9A01 will show, no flashing.

Reads the GENERATED pet_anims.cpp (the same bytes that go into flash),
decodes every frame the way PetRenderer does, masks it to the round panel
and writes one GIF per animation plus a contact sheet.

    python preview_pet_anims.py --sketch "<flat sketch root>" --out previews

Requirements:  pip install pillow numpy
"""

import argparse
import os
import re

import numpy as np
from PIL import Image, ImageDraw

SIZE = 240


def parse_cpp(path):
    src = open(path).read()

    def array(name):
        m = re.search(name + r"\[[^\]]*\]\s*=\s*\{(.*?)\};", src, re.S)
        return m.group(1)

    palette = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", array("PET_PALETTE"))]
    rle = bytes(int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", array("PET_RLE")))
    offsets = [int(v) for v in re.findall(r"\d+", array("PET_FRAME_OFFSETS"))]
    anims = []
    for name, steps_name in re.findall(r'\{"(\w+)", (STEPS_\w+),', array("PET_ANIMS")):
        body = re.search(r"static const PetStep " + steps_name + r"\[\] = \{(.*?)\};", src, re.S)
        steps = [(int(a), int(b)) for a, b in re.findall(r"\{(\d+),(\d+)\}", body.group(1))]
        anims.append((name, steps))
    tick = int(re.search(r"PET_TICK_MS (\d+)", open(path.replace(".cpp", ".h")).read()).group(1))
    return palette, rle, offsets, anims, tick


def decode(rle, start, end):
    """Mirror of PetRenderer::fillPixels()."""
    out = np.empty(SIZE * SIZE, np.uint8)
    p, o = start, 0
    while o < out.size:
        b = rle[p]; p += 1
        run = 32 + rle[p] if (b & 31) == 31 else (b & 31) + 1
        if (b & 31) == 31:
            p += 1
        out[o:o + run] = b >> 5
        o += run
    assert o == out.size and p == end, "frame length mismatch"
    return out.reshape(SIZE, SIZE)


def rgb565_to_rgb(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sketch", required=True, help="folder with pet_anims.cpp/.h")
    ap.add_argument("--out", default="previews")
    args = ap.parse_args()

    palette, rle, offsets, anims, tick = parse_cpp(os.path.join(args.sketch, "pet_anims.cpp"))
    lut = np.array([rgb565_to_rgb(v) for v in palette], np.uint8)
    frames = [decode(rle, offsets[i], offsets[i + 1]) for i in range(len(offsets) - 1)]

    yy, xx = np.mgrid[:SIZE, :SIZE]
    mask = np.hypot(xx - 119.5, yy - 119.5) <= 120
    bezel = (40, 40, 48)

    def render(i):
        img = lut[frames[i]].copy()
        img[~mask] = bezel
        return Image.fromarray(img)

    os.makedirs(args.out, exist_ok=True)
    sheet_rows = []
    for name, steps in anims:
        imgs = [render(f) for f, _ in steps]
        durs = [max(20, h * tick) for _, h in steps]
        imgs[0].save(os.path.join(args.out, f"{name}.gif"), save_all=True,
                     append_images=imgs[1:], duration=durs, loop=0, disposal=1)
        pick = np.linspace(0, len(steps) - 1, 8).round().astype(int)
        sheet_rows.append((name, [imgs[k] for k in pick]))
        print(f"  {name:<16} {len(steps):3d} steps  {sum(durs)/1000:.2f}s")

    th = 120
    sheet = Image.new("RGB", (8 * th + 110, len(sheet_rows) * th), (20, 20, 24))
    d = ImageDraw.Draw(sheet)
    for r, (name, imgs) in enumerate(sheet_rows):
        d.text((6, r * th + th // 2 - 6), name, fill=(230, 230, 230))
        for c, im in enumerate(imgs):
            sheet.paste(im.resize((th, th)), (110 + c * th, r * th))
    sheet.save(os.path.join(args.out, "contact_sheet.png"))
    print(f"  wrote {len(anims)} GIFs + contact_sheet.png -> {args.out}")


if __name__ == "__main__":
    main()
