#!/usr/bin/env python3
"""
Turns mp4 clips into flash-resident, run-length-encoded frames the
RP2040 can stream straight into TFT_eSPI band buffers. similair to
build_albums.py

    python build_pet_anims.py --manifest pet_anims.json --out "<flat sketch root>"

Writes pet_anims.h and pet_anims.cpp into --out. Both are GENERATED, never
hand-edit them; change the manifest or the clips and re-run this instead.

How it works
------------
1. Every clip is decoded with ffmpeg (bundled via the imageio-ffmpeg pip
   package, or a system ffmpeg on PATH) and downscaled to 240x240.
2. One shared palette of up to 8 colours is used across ALL clips, so every
   pixel becomes a 3-bit index. Anti-aliased edges keep a couple of in-between
   shades instead of turning jagged. The manifest's "palette" list sets the
   colours by hand; without it the palette is picked automatically.
3. Frames are de-duplicated across ALL clips. A frame that differs from an
   already-stored frame by <= --dup-px pixels is not stored again, it just
   points at the existing one. The neutral face is shared by every clip, and a
   held pose costs zero extra bytes.
4. Each stored frame is RLE-encoded as one stream running top-left to
   bottom-right (runs may wrap across rows):

       byte = CCC LLLLL
         CCC   = palette index 0..7
         LLLLL = 0..30  -> run of 1..31 pixels
                 31     -> run of 32 + next byte (32..287 pixels)

   Pixels outside the round panel are invisible, so before encoding each
   row's corners are painted with that row's edge colour. That merges them
   into the neighbouring runs instead of wasting bytes on hidden art.
5. Each clip becomes a timeline of (frame, hold) steps at the source frame
   rate, so a pose held for 20 video frames is one step, not 20.

Manifest (pet_anims.json)
-------------------------
    "defaults": framing applied to every clip (scale, offset_x, offset_y)
    "palette":  optional list of up to 8 "#RRGGBB" colours
    "anims":    one entry per animation:
        id, file, kind ("idle" / "interactive")
        scale / offset_x / offset_y   framing override for this clip
        from / to                     only use this inclusive source-frame
                                      range, so one mp4 can feed several
                                      animations (Dizzy.mp4 -> shake + dizzy)
        segments: [{from, to, scale, offset_x, offset_y}]
                                      different framing for part of the clip
                                      (source-frame numbers, inclusive)

Requirements:  pip install numpy imageio-ffmpeg
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

import numpy as np

SIZE = 240          # must match the panel / ART_W in config.h
MAX_COLORS = 8      # 3-bit palette index in the RLE byte
RUN_SHORT_MAX = 31  # LLLLL 0..30 -> 1..31
RUN_LONG_BASE = 32  # LLLLL == 31 -> 32 + ext byte
RUN_LONG_MAX = RUN_LONG_BASE + 255


# --------------------------------------------------------------------------
# Decoding
# --------------------------------------------------------------------------
def find_ffmpeg():
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        pass
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    sys.exit("ffmpeg not found. Run:  pip install imageio-ffmpeg")


def probe_fps(ffmpeg, path):
    """Parse the source frame rate out of ffmpeg's stderr banner."""
    out = subprocess.run([ffmpeg, "-i", path], capture_output=True, text=True).stderr
    for token in out.split(","):
        token = token.strip()
        if token.endswith(" fps"):
            try:
                return float(token.split()[0])
            except ValueError:
                pass
    return 30.0


def decode_clip(ffmpeg, path, scale, offset_x, offset_y):
    """Return (frames[N,240,240,3] uint8, fps)."""
    fps = probe_fps(ffmpeg, path)
    inner = max(2, int(round(SIZE * scale)))
    # Scale the (square) clip down, then pad it back to 240x240 on the clip's
    # own background colour, which we fill in afterwards from the corner pixel.
    vf = f"scale={inner}:{inner}:flags=area"
    raw = subprocess.run(
        [ffmpeg, "-v", "error", "-i", path, "-vf", vf,
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        capture_output=True, check=True).stdout
    small = np.frombuffer(raw, np.uint8).reshape(-1, inner, inner, 3)
    if inner == SIZE and offset_x == 0 and offset_y == 0:
        return small.copy(), fps

    frames = np.empty((len(small), SIZE, SIZE, 3), np.uint8)
    x0 = (SIZE - inner) // 2 + offset_x
    y0 = (SIZE - inner) // 2 + offset_y
    for i, f in enumerate(small):
        frames[i] = f[0, 0]                       # background from corner
        sx0, sy0 = max(0, -x0), max(0, -y0)
        dx0, dy0 = max(0, x0), max(0, y0)
        w = min(inner - sx0, SIZE - dx0)
        h = min(inner - sy0, SIZE - dy0)
        frames[i, dy0:dy0 + h, dx0:dx0 + w] = f[sy0:sy0 + h, sx0:sx0 + w]
    return frames, fps


# --------------------------------------------------------------------------
# Palette
# --------------------------------------------------------------------------
def build_palette(all_frames, n_colors, primary_dist=96):
    """Pick the flat 'ink' colours first, then spend leftover slots on the
    anti-aliasing shades between them.

    Generic median-cut is a bad fit here: it burns slots on the invisible
    near-black / near-white noise the video codec adds. Instead:
      1. Histogram the clips at 5 bits/channel.
      2. Primaries: most common colours that are >= primary_dist apart
         (black, white, the blush tan, ...).
      3. Remaining slots: midpoints between pairs of primaries, ranked by how
         many real pixels sit closer to that midpoint than to either end,
         i.e. how much edge anti-aliasing that pair actually has.
    """
    samples = []
    for frames in all_frames:
        step = max(1, len(frames) // 24)
        samples.append(frames[::step].reshape(-1, 3))
    px = np.concatenate(samples).astype(np.int32)
    bins = px >> 3
    keys = (bins[:, 0] << 10) | (bins[:, 1] << 5) | bins[:, 2]
    uniq, inv, counts = np.unique(keys, return_inverse=True, return_counts=True)
    # mean true colour of each bin
    means = np.zeros((len(uniq), 3))
    np.add.at(means, inv, px)
    means /= counts[:, None]

    order = np.argsort(-counts)
    primaries = []
    for i in order:
        c = means[i]
        if all(np.linalg.norm(c - p) >= primary_dist for p in primaries):
            primaries.append(c)
        if len(primaries) == n_colors or counts[i] < counts.sum() * 0.002:
            break
    pal = [np.round(p) for p in primaries]

    if len(pal) < n_colors and len(primaries) >= 2:
        cands = []
        for a in range(len(primaries)):
            for b in range(a + 1, len(primaries)):
                mid = (primaries[a] + primaries[b]) / 2
                d_mid = np.linalg.norm(means - mid, axis=1)
                d_a = np.linalg.norm(means - primaries[a], axis=1)
                d_b = np.linalg.norm(means - primaries[b], axis=1)
                score = counts[(d_mid < d_a) & (d_mid < d_b)].sum()
                cands.append((score, mid))
        cands.sort(key=lambda t: -t[0])
        for score, mid in cands[: n_colors - len(pal)]:
            if score > 0:
                pal.append(np.round(mid))

    pal = np.array(pal, np.int32)
    # sort dark -> light so index 0 is the darkest (usually the background)
    return pal[np.argsort(pal.sum(axis=1))]


def map_to_palette(frames, pal):
    """Nearest-colour index per pixel, no dithering (dither noise kills RLE)."""
    f = frames.astype(np.int32)
    best = None
    best_d = None
    for i, c in enumerate(pal):
        d = ((f - c) ** 2).sum(axis=-1)
        if best is None:
            best = np.zeros(d.shape, np.uint8)
            best_d = d
        else:
            m = d < best_d
            best[m] = i
            best_d = np.where(m, d, best_d)
    return best


def visible_chords(margin=0.75):
    """(x_left, x_right) of the visible circle for every row, inclusive."""
    c = (SIZE - 1) / 2
    r = SIZE / 2 + margin
    chords = []
    for y in range(SIZE):
        dy = y - c
        half = np.sqrt(max(0.0, r * r - dy * dy))
        xl = max(0, int(np.floor(c - half)))
        xr = min(SIZE - 1, int(np.ceil(c + half)))
        chords.append((xl, xr))
    return chords


def fill_corners(idx, chords):
    """Paint the invisible corners with each row's edge colour (in place)."""
    for y, (xl, xr) in enumerate(chords):
        if xl > 0:
            idx[y, :xl] = idx[y, xl]
        if xr < SIZE - 1:
            idx[y, xr + 1:] = idx[y, xr]
    return idx


def parse_palette(colours):
    """["#RRGGBB", ...] from the manifest -> int array, sorted dark to light."""
    pal = []
    for c in colours:
        c = c.lstrip("#")
        if len(c) != 6:
            sys.exit(f"bad palette colour '{c}', expected #RRGGBB")
        pal.append([int(c[i:i + 2], 16) for i in (0, 2, 4)])
    if not 2 <= len(pal) <= MAX_COLORS:
        sys.exit(f"palette needs 2..{MAX_COLORS} colours, got {len(pal)}")
    pal = np.array(pal, np.int32)
    return pal[np.argsort(pal.sum(axis=1), kind="stable")]


def rgb565(c):
    r, g, b = (int(v) for v in c)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# --------------------------------------------------------------------------
# RLE
# --------------------------------------------------------------------------
def rle_encode(idx):
    flat = idx.reshape(-1)
    out = bytearray()
    change = np.flatnonzero(flat[1:] != flat[:-1]) + 1
    starts = np.concatenate(([0], change))
    ends = np.concatenate((change, [flat.size]))
    for s, e in zip(starts, ends):
        colour = int(flat[s]) << 5
        n = int(e - s)
        while n > 0:
            if n <= RUN_SHORT_MAX:
                out.append(colour | (n - 1))
                n = 0
            else:
                take = min(n, RUN_LONG_MAX)
                out.append(colour | 31)
                out.append(take - RUN_LONG_BASE)
                n -= take
    return bytes(out)


def rle_decode(data, n_px=SIZE * SIZE):
    """Reference decoder, mirrors PetRenderer::fillPixels(). Used for the
    self-check below and by preview_pet_anims.py."""
    out = np.empty(n_px, np.uint8)
    p = 0
    o = 0
    while o < n_px:
        b = data[p]; p += 1
        c, L = b >> 5, b & 31
        if L == 31:
            run = RUN_LONG_BASE + data[p]; p += 1
        else:
            run = L + 1
        out[o:o + run] = c
        o += run
    assert o == n_px, "run overflow"
    return out.reshape(SIZE, SIZE)


# --------------------------------------------------------------------------
# Code generation
# --------------------------------------------------------------------------
def c_bytes(data, indent="  ", per_line=20):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append(indent + ",".join(f"0x{b:02X}" for b in data[i:i + per_line]) + ",")
    return "\n".join(lines)


def write_outputs(out_dir, palette565, blob, offsets, anims, fps_ms):
    h = []
    h.append("// pet_anims.h -- GENERATED by build_pet_anims.py, do not edit.")
    h.append("#pragma once")
    h.append("#include <stdint.h>")
    h.append("")
    h.append(f"#define PET_W {SIZE}")
    h.append(f"#define PET_H {SIZE}")
    h.append(f"#define PET_PALETTE_SIZE {len(palette565)}")
    h.append(f"#define PET_FRAME_COUNT {len(offsets) - 1}")
    h.append(f"#define PET_TICK_MS {fps_ms}   // one source video frame")
    h.append("")
    h.append("// One timeline step: show frames[frame] for `hold` ticks.")
    h.append("struct PetStep {")
    h.append("  uint16_t frame;")
    h.append("  uint16_t hold;")
    h.append("};")
    h.append("")
    h.append("struct PetAnim {")
    h.append("  const char*    name;")
    h.append("  const PetStep* steps;")
    h.append("  uint16_t       stepCount;")
    h.append("  uint8_t        kind;       // PET_KIND_IDLE / PET_KIND_INTERACTIVE")
    h.append("};")
    h.append("")
    h.append("#define PET_KIND_IDLE        0")
    h.append("#define PET_KIND_INTERACTIVE 1")
    h.append("")
    h.append("enum PetAnimId : uint8_t {")
    for a in anims:
        h.append(f"  PET_ANIM_{a['id']},")
    h.append("  PET_ANIM_COUNT")
    h.append("};")
    h.append("")
    h.append("extern const uint16_t PET_PALETTE[PET_PALETTE_SIZE];   // RGB565")
    h.append("extern const uint8_t  PET_RLE[];                        // all frames")
    h.append("extern const uint32_t PET_FRAME_OFFSETS[PET_FRAME_COUNT + 1];")
    h.append("extern const PetAnim  PET_ANIMS[PET_ANIM_COUNT];")
    h.append("")

    c = []
    c.append("// pet_anims.cpp -- GENERATED by build_pet_anims.py, do not edit.")
    c.append("// const data lives in flash (XIP) on the RP2040, not in RAM.")
    c.append('#include "pet_anims.h"')
    c.append("")
    c.append("const uint16_t PET_PALETTE[PET_PALETTE_SIZE] = {")
    c.append("  " + ", ".join(f"0x{v:04X}" for v in palette565))
    c.append("};")
    c.append("")
    c.append(f"// {len(blob)} bytes of RLE for {len(offsets) - 1} unique frames")
    c.append("const uint8_t PET_RLE[] = {")
    c.append(c_bytes(blob))
    c.append("};")
    c.append("")
    c.append("const uint32_t PET_FRAME_OFFSETS[PET_FRAME_COUNT + 1] = {")
    for i in range(0, len(offsets), 10):
        c.append("  " + ", ".join(str(o) for o in offsets[i:i + 10]) + ",")
    c.append("};")
    c.append("")
    for a in anims:
        c.append(f"static const PetStep STEPS_{a['id']}[] = {{")
        for i in range(0, len(a["steps"]), 8):
            c.append("  " + " ".join(f"{{{f},{h_}}}," for f, h_ in a["steps"][i:i + 8]))
        c.append("};")
    c.append("")
    c.append("const PetAnim PET_ANIMS[PET_ANIM_COUNT] = {")
    for a in anims:
        kind = "PET_KIND_IDLE" if a["kind"] == "idle" else "PET_KIND_INTERACTIVE"
        c.append(f'  {{"{a["id"].lower()}", STEPS_{a["id"]}, '
                 f'{len(a["steps"])}, {kind}}},')
    c.append("};")
    c.append("")

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "pet_anims.h"), "w", newline="\n") as f:
        f.write("\n".join(h))
    with open(os.path.join(out_dir, "pet_anims.cpp"), "w", newline="\n") as f:
        f.write("\n".join(c))


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", default="pet_anims.json")
    ap.add_argument("--out", required=True, help="flat Arduino sketch root")
    ap.add_argument("--colors", type=int, default=6,
                    help="palette size, 2..8 (fewer = smaller, more jagged edges)")
    ap.add_argument("--dup-px", type=int, default=60,
                    help="frames differing by <= this many pixels are merged")
    ap.add_argument("--dump", default=None,
                    help="optional .npz with decoded frames, for the preview script")
    args = ap.parse_args()

    if not 2 <= args.colors <= MAX_COLORS:
        sys.exit("--colors must be 2..8")

    base = os.path.dirname(os.path.abspath(args.manifest))
    with open(args.manifest) as f:
        manifest = json.load(f)
    defaults = manifest.get("defaults", {})
    ffmpeg = find_ffmpeg()

    # 1. decode
    cache = {}

    def load(e):
        """Decode one clip at one framing, once."""
        key = (e["file"], e.get("scale", 1.0), e.get("offset_x", 0), e.get("offset_y", 0))
        if key not in cache:
            cache[key] = decode_clip(ffmpeg, os.path.join(base, key[0]), *key[1:])
        return cache[key]

    clips, all_frames, fps_seen = [], [], []
    for entry in manifest["anims"]:
        e = {**defaults, **entry}
        frames, fps = load(e)
        n_src = len(frames)
        # Per-range framing overrides, in source-frame numbers (inclusive).
        segments = e.get("segments", [])
        if segments:
            frames = frames.copy()
            for seg in segments:
                alt, _ = load({**e, **seg})
                a, b = seg["from"], seg["to"]
                if not 0 <= a <= b < n_src:
                    sys.exit(f"{e['id']}: segment {a}..{b} is outside 0..{n_src - 1}")
                frames[a:b + 1] = alt[a:b + 1]
        # Optional sub-range, so one mp4 can feed several animations.
        lo, hi = e.get("from", 0), e.get("to", n_src - 1)
        if not 0 <= lo <= hi < n_src:
            sys.exit(f"{e['id']}: from/to {lo}..{hi} is outside 0..{n_src - 1}")
        frames = frames[lo:hi + 1]
        span = f" [{lo}..{hi}]" if (lo, hi) != (0, n_src - 1) else ""
        print(f"  {e['id']:<18} {len(frames):4d} frames @ {fps:g} fps  <- {e['file']}{span}")
        clips.append(e)
        all_frames.append(frames)
        fps_seen.append(fps)
    fps = fps_seen[0]
    if any(abs(x - fps) > 0.01 for x in fps_seen):
        print(f"  warning: clips have mixed frame rates {fps_seen}; timing uses {fps:g}")
    tick_ms = int(round(1000.0 / fps))

    # 2. palette
    if "palette" in manifest:
        pal = parse_palette(manifest["palette"])
    else:
        pal = build_palette(all_frames, args.colors)
    palette565 = [rgb565(c) for c in pal]
    print("  palette:", " ".join(f"#{r:02X}{g:02X}{b:02X}" for r, g, b in pal))

    # 3+4. index, dedupe, encode
    uniques = []          # list of index arrays
    blob = bytearray()
    offsets = [0]
    anims = []
    total_src = 0
    chords = visible_chords()
    yy, xx = np.mgrid[:SIZE, :SIZE]
    outside = np.ones((SIZE, SIZE), bool)
    for y, (xl, xr) in enumerate(chords):
        outside[y, xl:xr + 1] = False
    for e, frames in zip(clips, all_frames):
        idx_frames = map_to_palette(frames, pal)
        steps = []
        clipped_px = 0
        for idx in idx_frames:
            total_src += 1
            # Ink (anything but the frame's background colour) that falls in
            # the hidden corners is art the viewer will never see.
            bg = np.bincount(idx[outside]).argmax()
            clipped_px = max(clipped_px, int(np.count_nonzero(
                (idx != bg) & outside & (np.hypot(xx - 119.5, yy - 119.5) > 124))))
            fill_corners(idx, chords)
            match = None
            # Most likely match is the previous step's frame, check it first.
            order = ([steps[-1][0]] if steps else []) + list(range(len(uniques) - 1, -1, -1))
            for u in order:
                if np.count_nonzero(uniques[u] != idx) <= args.dup_px:
                    match = u
                    break
            if match is None:
                uniques.append(idx)
                blob += rle_encode(idx)
                offsets.append(len(blob))
                match = len(uniques) - 1
            if steps and steps[-1][0] == match and steps[-1][1] < 65535:
                steps[-1][1] += 1
            else:
                steps.append([match, 1])
        anims.append({"id": e["id"].upper(), "kind": e.get("kind", "idle"),
                      "steps": steps})
        dur = sum(h for _, h in steps) * tick_ms
        warn = f"  (!) {clipped_px} px drawn outside the round panel" if clipped_px > 200 else ""
        print(f"  {e['id']:<18} {len(steps):3d} steps, {dur/1000:.2f}s{warn}")

    # self-check: every stored frame decodes back exactly
    for i, u in enumerate(uniques):
        assert np.array_equal(rle_decode(blob[offsets[i]:offsets[i + 1]]), u), i

    write_outputs(args.out, palette565, blob, offsets, anims, tick_ms)

    flash = len(blob) + 4 * len(offsets) + 2 * len(palette565) + \
        sum(4 * len(a["steps"]) for a in anims)
    print(f"  {total_src} source frames -> {len(uniques)} unique frames")
    print(f"  RLE {len(blob)/1024:.1f} KB, avg {len(blob)/len(uniques):.0f} B/frame, "
          f"total flash ~{flash/1024:.1f} KB "
          f"(raw RGB565 would be {total_src * SIZE * SIZE * 2 / 1048576:.1f} MB)")
    print(f"  wrote pet_anims.h / pet_anims.cpp -> {args.out}")

    if args.dump:
        np.savez_compressed(args.dump, palette=pal.astype(np.uint8),
                            uniques=np.stack(uniques),
                            **{f"steps_{a['id']}": np.array(a["steps"]) for a in anims},
                            ids=np.array([a["id"] for a in anims]),
                            tick_ms=tick_ms)


if __name__ == "__main__":
    main()
