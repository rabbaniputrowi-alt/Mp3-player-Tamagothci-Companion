#!/usr/bin/env python3
"""
build_tracks.py - generates track_names.h / track_names.cpp for Stardance.

The DFPlayer only knows folder + file numbers (01/001.mp3) and can't hand
filenames or ID3 tags back to the RP2040, so song titles have to live in
firmware. tracks.json is the source of truth; this script turns it into
const tables in flash. Never hand-edit track_names.cpp, regenerate it.

Two modes:

  1. Build (normal):
       python build_tracks.py --manifest tracks.json --out "<flat sketch root>"

  2. Scan the SD card first, to fill tracks.json from the mp3s' ID3 titles:
       python build_tracks.py --manifest tracks.json --scan E:\\
     Reads every NN\\NNN.mp3 on the card, pulls the ID3 title (v2.2/2.3/2.4,
     falls back to ID3v1), and rewrites the "tracks" list of each folder it
     finds. Playlist "name"s already in tracks.json are kept. Review the
     result, fix any titles by hand, then run mode 1. Add --out to do both
     in one go.

tracks.json format - list order IS the playlist index, so entry 0 must be
folder "01", entry 1 folder "02", and so on (same rule as PLAYLIST_ART[]):

[
  { "folder": "01", "name": "Hamilton",
    "tracks": ["Alexander Hamilton", "Aaron Burr, Sir", ...] },
  ...
]

tracks[0] is 001.mp3, tracks[1] is 002.mp3, ... An empty string "" means
"no title known" and the UI falls back to "Track N". An empty tracks list
means the folder isn't described at all: the firmware then asks the
DFPlayer for the file count, same as before this file existed.

The panel fonts (FreeSerif GFX) are plain ASCII only, so titles are
transliterated here (smart quotes, accents, dashes) and anything left that
isn't printable ASCII is dropped with a warning.

No dependencies beyond the standard library.
"""

import argparse
import json
import re
import struct
import sys
import unicodedata
from pathlib import Path

MAX_TRACKS = 255          # curTrack is a uint8_t; DFPlayer tops out at 255 per folder
LABEL_CHARS = 39          # PlaylistUI Row::label is char[40]
FOLDER_RE = re.compile(r"^\d{2}$")
TRACK_RE = re.compile(r"^(\d{3})\.mp3$", re.IGNORECASE)

# Common non-ASCII punctuation in song titles, mapped before the generic
# accent strip.
ASCII_MAP = {
    "\u2018": "'", "\u2019": "'", "\u201A": "'", "\u2032": "'",
    "\u201C": '"', "\u201D": '"', "\u201E": '"', "\u2033": '"',
    "\u2013": "-", "\u2014": "-", "\u2015": "-", "\u2212": "-",
    "\u2026": "...", "\u00A0": " ", "\u00D7": "x", "\u00DF": "ss",
    "\u00C6": "AE", "\u00E6": "ae", "\u00D8": "O", "\u00F8": "o",
    "\u0141": "L", "\u0142": "l", "\u2605": "*", "\u2606": "*",
}


# --------------------------------------------------------------------------
# ASCII cleanup
# --------------------------------------------------------------------------

def to_panel_ascii(text, where, warnings):
    """Transliterate to printable ASCII the GFX fonts can draw."""
    if text is None:
        return ""
    s = "".join(ASCII_MAP.get(ch, ch) for ch in str(text))
    s = unicodedata.normalize("NFKD", s)
    s = "".join(ch for ch in s if not unicodedata.combining(ch))
    kept = []
    dropped = []
    for ch in s:
        if 0x20 <= ord(ch) <= 0x7E:
            kept.append(ch)
        elif ch in "\t\r\n":
            kept.append(" ")
        else:
            dropped.append(ch)
    out = re.sub(r"\s+", " ", "".join(kept)).strip()
    if dropped:
        warnings.append(f"{where}: dropped non-ASCII {''.join(dropped)!r} -> {out!r}")
    return out


# --------------------------------------------------------------------------
# Minimal ID3 title reader (no mutagen needed)
# --------------------------------------------------------------------------

def _syncsafe(b):
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3]


def _decode_text(payload):
    if not payload:
        return ""
    enc, data = payload[0], payload[1:]
    try:
        if enc == 0:
            s = data.decode("latin-1")
        elif enc == 1:
            s = data.decode("utf-16")
        elif enc == 2:
            s = data.decode("utf-16-be")
        elif enc == 3:
            s = data.decode("utf-8")
        else:
            return ""
    except UnicodeDecodeError:
        s = data.decode("latin-1", errors="replace")
    # v2.4 allows several NUL-separated values; the first one is the title.
    return s.split("\x00")[0].strip()


def _id3v2_title(f):
    f.seek(0)
    head = f.read(10)
    if len(head) < 10 or head[:3] != b"ID3":
        return None
    major, flags = head[3], head[5]
    size = _syncsafe(head[6:10])
    tag = f.read(size)

    if major < 4 and (flags & 0x80):          # whole-tag unsynchronisation
        tag = tag.replace(b"\xff\x00", b"\xff")

    pos = 0
    if flags & 0x40:                           # extended header
        if major == 3 and len(tag) >= 4:
            pos = 4 + struct.unpack(">I", tag[:4])[0]
        elif major == 4 and len(tag) >= 4:
            pos = _syncsafe(tag[:4])

    if major == 2:
        want, id_len, hdr_len = b"TT2", 3, 6
    else:
        want, id_len, hdr_len = b"TIT2", 4, 10

    while pos + hdr_len <= len(tag):
        fid = tag[pos:pos + id_len]
        if not fid.strip(b"\x00"):
            break                               # padding
        if major == 2:
            fsize = int.from_bytes(tag[pos + 3:pos + 6], "big")
        elif major == 4:
            fsize = _syncsafe(tag[pos + 4:pos + 8])
        else:
            fsize = struct.unpack(">I", tag[pos + 4:pos + 8])[0]
        body = tag[pos + hdr_len:pos + hdr_len + fsize]
        if fid == want:
            if major == 4 and len(tag) >= pos + 10 and (tag[pos + 9] & 0x02):
                body = body.replace(b"\xff\x00", b"\xff")   # per-frame unsync
            return _decode_text(body)
        if fsize <= 0:
            break
        pos += hdr_len + fsize
    return None


def _id3v1_title(f):
    try:
        f.seek(-128, 2)
    except OSError:
        return None
    block = f.read(128)
    if len(block) == 128 and block[:3] == b"TAG":
        return block[3:33].split(b"\x00")[0].decode("latin-1").strip() or None
    return None


def read_title(path):
    try:
        with open(path, "rb") as f:
            return _id3v2_title(f) or _id3v1_title(f) or ""
    except OSError:
        return ""


# --------------------------------------------------------------------------
# Manifest handling
# --------------------------------------------------------------------------

def load_manifest(path):
    if not path.exists():
        return []
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, list):
        sys.exit(f"{path.name}: top level must be a list of playlists.")
    return data


def validate(entries):
    """Checks that list order matches folder numbers. Returns cleaned entries."""
    if len(entries) > 99:
        sys.exit("More than 99 playlists - DFPlayer folders stop at 99.")
    for i, e in enumerate(entries):
        want = f"{i + 1:02d}"
        folder = str(e.get("folder", "")).zfill(2)
        if folder != want:
            sys.exit(f"Entry {i} has folder {e.get('folder')!r}, expected {want!r}. "
                     "List order is the playlist index (folder - 1); "
                     "keep them in folder order with no gaps.")
        e["folder"] = want
        e.setdefault("name", "")
        e.setdefault("tracks", [])
        if not isinstance(e["tracks"], list):
            sys.exit(f"Folder {want}: \"tracks\" must be a list of strings.")
        if len(e["tracks"]) > MAX_TRACKS:
            sys.exit(f"Folder {want}: {len(e['tracks'])} tracks, max is {MAX_TRACKS}.")
    return entries


def scan_card(card_root, entries):
    """Fills each folder's tracks list from NN/NNN.mp3 ID3 titles."""
    card_root = Path(card_root)
    if not card_root.is_dir():
        sys.exit(f"--scan path not found: {card_root}")

    folders = sorted(p for p in card_root.iterdir()
                     if p.is_dir() and FOLDER_RE.match(p.name))
    if not folders:
        sys.exit(f"No 01..99 folders found under {card_root}.")

    by_folder = {str(e.get("folder", "")).zfill(2): e for e in entries}
    problems = []

    for folder in folders:
        numbered = {}
        for p in folder.iterdir():
            if not p.is_file() or p.name.startswith("._"):
                continue                       # skip macOS resource-fork junk
            m = TRACK_RE.match(p.name)
            if m:
                numbered[int(m.group(1))] = p
            elif p.suffix.lower() in (".mp3", ".wav"):
                problems.append(f"{folder.name}/{p.name}: name isn't exactly NNN.mp3, "
                                "the DFPlayer won't play it")

        if not numbered:
            problems.append(f"{folder.name}/: no NNN.mp3 files")
            titles = []
        else:
            last = max(numbered)
            if last > MAX_TRACKS:
                sys.exit(f"{folder.name}/: file {last:03d}.mp3 is past {MAX_TRACKS}.")
            titles = []
            for n in range(1, last + 1):
                if n not in numbered:
                    problems.append(f"{folder.name}/{n:03d}.mp3 missing (numbering gap)")
                    titles.append("")
                    continue
                t = read_title(numbered[n])
                if not t:
                    problems.append(f"{folder.name}/{n:03d}.mp3: no ID3 title, left blank")
                titles.append(t)

        e = by_folder.get(folder.name)
        if e is None:
            e = {"folder": folder.name, "name": "", "tracks": []}
            by_folder[folder.name] = e
            problems.append(f"{folder.name}/: new folder, give it a \"name\" in the manifest")
        e["tracks"] = titles
        print(f"  {folder.name}/  {len(titles):3d} track(s)  {e.get('name') or '(no name)'}")
        for n, t in enumerate(titles, 1):
            print(f"      {n:03d}  {t or '-'}")

    # Rebuild in folder order; folders in the manifest but not on the card stay.
    nums = sorted(int(k) for k in by_folder if k.isdigit())
    merged = []
    for n in range(1, (max(nums) if nums else 0) + 1):
        key = f"{n:02d}"
        merged.append(by_folder.get(key, {"folder": key, "name": "", "tracks": []}))
        if key not in by_folder:
            problems.append(f"{key}/: missing on the card and in the manifest - "
                            "folders must be contiguous")
    return merged, problems


def write_manifest(path, entries):
    # One title per line keeps diffs and hand edits readable.
    lines = ["["]
    for i, e in enumerate(entries):
        lines.append("  {")
        lines.append(f'    "folder": {json.dumps(e["folder"])},')
        lines.append(f'    "name": {json.dumps(e.get("name", ""), ensure_ascii=False)},')
        tracks = e.get("tracks", [])
        if not tracks:
            lines.append('    "tracks": []')
        else:
            lines.append('    "tracks": [')
            for j, t in enumerate(tracks):
                comma = "," if j < len(tracks) - 1 else ""
                lines.append(f"      {json.dumps(t, ensure_ascii=False)}{comma}")
            lines.append("    ]")
        lines.append("  }" + ("," if i < len(entries) - 1 else ""))
    lines.append("]")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# --------------------------------------------------------------------------
# Code generation
# --------------------------------------------------------------------------

def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def generate(entries, out_dir, manifest_name):
    warnings = []
    playlists = []   # (name, [titles])
    for e in entries:
        f = e["folder"]
        name = to_panel_ascii(e.get("name", ""), f"{f} name", warnings)
        titles = []
        for n, t in enumerate(e.get("tracks", []), 1):
            clean = to_panel_ascii(t, f"{f}/{n:03d}", warnings)
            if len(clean) > LABEL_CHARS:
                warnings.append(f"{f}/{n:03d}: {len(clean)} chars, the list shows the first "
                                f"{LABEL_CHARS} and fits the rest with '...'")
            titles.append(clean)
        playlists.append((name, titles))

    header = f"""#pragma once
#include <stdint.h>

// GENERATED by build_tracks.py from {manifest_name} - do not hand-edit.
// Regenerate with: python build_tracks.py --manifest {manifest_name} --out <sketch root>
//
// Playlist index is 0-based (SD folder - 1), track is 1-based (NNN.mp3),
// matching AudioPlayer::currentPlaylist() / currentTrack().
namespace TrackNames {{

  // Number of playlists described in {manifest_name}.
  uint8_t playlistCount();

  // Display name of a playlist, or nullptr if it has none.
  const char *playlistName(uint8_t playlist);

  // Tracks listed for this playlist. 0 means "not described" - fall back to
  // asking the DFPlayer.
  uint8_t trackCount(uint8_t playlist);

  // Title of 1-based `track`, or nullptr if unknown (show "Track N").
  const char *title(uint8_t playlist, uint8_t track);

}}
"""

    out = [
        '#include "track_names.h"',
        "",
        f"// GENERATED by build_tracks.py from {manifest_name} - do not hand-edit.",
        "// const tables live in flash (XIP) on the RP2040, no RAM cost.",
        "",
        "namespace TrackNames {",
        "",
        "namespace {",
        "  struct Playlist {",
        "    const char        *name;",
        "    const char *const *titles;",
        "    uint8_t            count;",
        "  };",
        "",
    ]
    for i, (name, titles) in enumerate(playlists):
        if titles:
            out.append(f"  // folder {i + 1:02d}  {name}")
            out.append(f"  const char *const TITLES_{i + 1:02d}[] = {{")
            for n, t in enumerate(titles, 1):
                out.append(f'    "{c_escape(t)}",  // {n:03d}')
            out.append("  };")
            out.append("")
    out.append("  const Playlist PLAYLISTS[] = {")
    if not playlists:
        out.append("    { nullptr, nullptr, 0 }  // placeholder, manifest is empty")
    for i, (name, titles) in enumerate(playlists):
        arr = f"TITLES_{i + 1:02d}" if titles else "nullptr"
        comma = "," if i < len(playlists) - 1 else ""
        out.append(f'    {{ "{c_escape(name)}", {arr}, {len(titles)} }}{comma}  // folder {i + 1:02d}')
    out.append("  };")
    out.append(f"  constexpr uint8_t PLAYLIST_COUNT = {len(playlists)};")
    out += [
        "}",
        "",
        "uint8_t playlistCount() { return PLAYLIST_COUNT; }",
        "",
        "const char *playlistName(uint8_t playlist) {",
        "  if (playlist >= PLAYLIST_COUNT) return nullptr;",
        "  const char *n = PLAYLISTS[playlist].name;",
        "  return (n && n[0]) ? n : nullptr;",
        "}",
        "",
        "uint8_t trackCount(uint8_t playlist) {",
        "  if (playlist >= PLAYLIST_COUNT) return 0;",
        "  return PLAYLISTS[playlist].count;",
        "}",
        "",
        "const char *title(uint8_t playlist, uint8_t track) {",
        "  if (playlist >= PLAYLIST_COUNT || track < 1) return nullptr;",
        "  const Playlist &p = PLAYLISTS[playlist];",
        "  if (track > p.count) return nullptr;",
        "  const char *t = p.titles[track - 1];",
        "  return (t && t[0]) ? t : nullptr;",
        "}",
        "",
        "} // namespace TrackNames",
        "",
    ]

    (out_dir / "track_names.h").write_text(header, encoding="utf-8", newline="\n")
    (out_dir / "track_names.cpp").write_text("\n".join(out), encoding="utf-8", newline="\n")

    total = sum(len(t) for _, t in playlists)
    named = sum(1 for _, ts in playlists for t in ts if t)
    print(f"\nWrote track_names.h / track_names.cpp to {out_dir}")
    print(f"{len(playlists)} playlist(s), {total} track(s), {named} with titles.")
    return warnings


def main():
    ap = argparse.ArgumentParser(description="Build Stardance track-title tables.")
    ap.add_argument("--manifest", required=True, help="Path to tracks.json")
    ap.add_argument("--out", help="Flat sketch root to write track_names.h/.cpp into")
    ap.add_argument("--scan", metavar="SD_ROOT",
                    help="SD card root (or any folder with 01/, 02/...) to read ID3 titles from; "
                         "rewrites the manifest's track lists")
    args = ap.parse_args()

    if not args.scan and not args.out:
        ap.error("give --out to build, --scan to fill the manifest, or both")

    manifest = Path(args.manifest)
    entries = load_manifest(manifest)
    problems = []

    if args.scan:
        print(f"Scanning {args.scan}")
        entries, problems = scan_card(args.scan, entries)
        write_manifest(manifest, entries)
        print(f"\nUpdated {manifest}")

    if not entries and not args.scan and not manifest.exists():
        sys.exit(f"{manifest} not found.")

    entries = validate(entries)

    if args.out:
        out_dir = Path(args.out)
        if not out_dir.is_dir():
            sys.exit(f"--out folder not found: {out_dir}")
        problems += generate(entries, out_dir, manifest.name)

    if problems:
        print("\nCheck these:")
        for p in problems:
            print(f"  - {p}")

    print("\nReminder: playlist order here, PLAYLIST_ART[] in playlist_art.cpp and the "
          "SD folders must all line up (entry 0 = folder 01).")


if __name__ == "__main__":
    main()
