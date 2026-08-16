#pragma once
#include <stdint.h>

// Bridges AudioPlayer's (playlist, track) position to which entry in
// ALBUMS[] should be shown as cover art. Some playlists show one
// consistent cover throughout (Hamilton); others cycle through several
// covers as the tracks change (a compilation like the MCR playlist,
// cycling through 3 covers).
//
// ALBUMS[] itself just holds unique art assets now — it's no longer
// assumed to be one entry per playlist. albums.json should list each
// unique cover once (Hamilton's cover, then MCR's 3 covers = 4 total
// entries), and this table says which index applies to which playlist/
// track combination.
namespace PlaylistArt {

  // Given a 0-based playlist index (matches AudioPlayer::currentPlaylist())
  // and a 1-based track number (matches AudioPlayer::currentTrack()),
  // returns the ALBUMS[] index to load as cover art.
  uint8_t artIndexFor(uint8_t playlist, uint8_t track);

}