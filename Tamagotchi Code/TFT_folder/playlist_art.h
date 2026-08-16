#include "playlist_art.h"

namespace PlaylistArt {

namespace {
  struct PlaylistArtConfig {
    const uint8_t *artIndices;  // ALBUMS[] indices this playlist cycles through
    uint8_t        count;       // length of that array
  };

  // Playlist 0 (folder 01, Hamilton): every track shows the same cover.
  constexpr uint8_t HAMILTON_ART[] = { 0 };

  // Playlist 1 (folder 02, MCR compilation): explicit per-track mapping,
  // not a repeating cycle — the actual assignment doesn't follow a
  // clean pattern. Index 0 (Hamilton) never appears here; 1 = Sweet
  // Revenge, 2 = Black Parade, 3 = Bullets, matching albums.json's order.
  //
  // If more tracks get added past 15, artIndexFor()'s modulo wraps back
  // to the start of this list and reuses it — update this array to
  // extend it rather than relying on that wraparound being correct.
  constexpr uint8_t MCR_ART[] = {
    1, // track 1  - Sweet Revenge
    1, // track 2  - Sweet Revenge
    2, // track 3  - Black Parade
    3, // track 4  - Bullets
    1, // track 5  - Sweet Revenge
    1, // track 6  - Sweet Revenge
    1, // track 7  - Sweet Revenge
    2, // track 8  - Black Parade
    3, // track 9  - Bullets
    1, // track 10 - Sweet Revenge
    2, // track 11 - Black Parade
    1, // track 12 - Sweet Revenge
    1, // track 13 - Sweet Revenge
    1, // track 14 - Sweet Revenge
    1, // track 15 - Sweet Revenge
  };

  constexpr PlaylistArtConfig PLAYLIST_ART[] = {
    { HAMILTON_ART, 1  },
    { MCR_ART,      15 },
  };
  constexpr uint8_t PLAYLIST_COUNT = sizeof(PLAYLIST_ART) / sizeof(PLAYLIST_ART[0]);
}

uint8_t artIndexFor(uint8_t playlist, uint8_t track) {
  if (playlist >= PLAYLIST_COUNT) return 0;  // unconfigured playlist, fall back to art 0

  const PlaylistArtConfig &cfg = PLAYLIST_ART[playlist];
  // track is 1-based; (track-1) % count cycles 0,1,2,...,count-1,0,1,2,...
  uint8_t i = (uint8_t)((track - 1) % cfg.count);
  return cfg.artIndices[i];
}

} // namespace PlaylistArt