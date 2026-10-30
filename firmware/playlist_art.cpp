#include "playlist_art.h"

namespace PlaylistArt {

namespace {
  struct PlaylistArtConfig {
    const uint8_t *artIndices;  // ALBUMS[] indices this playlist cycles through
    uint8_t        count;       // length of that array
  };

  // One array per playlist. A single-entry array means "one cover for the
  // whole folder"; add more indices to cycle art across tracks.
  constexpr uint8_t HAMILTON_ART[] = { 0 };
  constexpr uint8_t MCR_ART[]      = { 1 };
  constexpr uint8_t BUNII_ART[]    = { 2 };
  constexpr uint8_t SEMETARY_ART[] = { 3 };
  constexpr uint8_t YANKOVIC_ART[] = { 4 };
  constexpr uint8_t ALEXG_ART[]    = { 5 };
  constexpr uint8_t DIVINE_ART[]   = { 6 };

  // Index in this table == AudioPlayer playlist index == SD folder - 1.
  // The chooser lists exactly this many playlists (playlistCount()), so a
  // folder without a row here never shows up, whatever albums.cpp holds.
  constexpr PlaylistArtConfig PLAYLIST_ART[] = {
    { HAMILTON_ART, 1 },   // playlist 0 -> folder 01
    { MCR_ART,      1 },   // playlist 1 -> folder 02
    { BUNII_ART,    1 },   // playlist 2 -> folder 03
    { SEMETARY_ART, 1 },   // playlist 3 -> folder 04
    { YANKOVIC_ART, 1 },   // playlist 4 -> folder 05
    { ALEXG_ART,    1 },   // playlist 5 -> folder 06
    { DIVINE_ART,   1 }    // playlist 6 -> folder 07
  };
  constexpr uint8_t PLAYLIST_COUNT = sizeof(PLAYLIST_ART) / sizeof(PLAYLIST_ART[0]);
}

uint8_t artIndexFor(uint8_t playlist, uint8_t track) {
  if (playlist >= PLAYLIST_COUNT) return 0;  // unconfigured playlist, fall back to art 0
  if (track < 1) track = 1;                  // track is 1-based; guard the subtraction

  const PlaylistArtConfig &cfg = PLAYLIST_ART[playlist];
  // track is 1-based; (track-1) % count cycles 0,1,2,...,count-1,0,1,2,...
  uint8_t i = (uint8_t)((track - 1) % cfg.count);
  return cfg.artIndices[i];
}

uint8_t playlistCount() {
  return PLAYLIST_COUNT;
}

} // namespace PlaylistArt
