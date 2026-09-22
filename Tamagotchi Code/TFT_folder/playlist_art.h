#pragma once
#include <stdint.h>

// Bridges AudioPlayer's (playlist, track) position to which entry in
// ALBUMS[] should be shown as cover art. Some playlists show one
// consistent cover throughout (Hamilton); others can cycle through
// several covers as the tracks change (a compilation, for instance).
//
// ALBUMS[] itself just holds unique art assets — it is NOT assumed to be
// one entry per playlist. albums.json lists each unique cover once, and
// the table in playlist_art.cpp says which index applies to which
// playlist/track combination.
//
// Current mapping (albums.json order = ALBUMS[] order):
//   playlist 0 -> SD folder 01 (Hamilton)      -> ALBUMS[0] "Hamilton"
//   playlist 1 -> SD folder 02 (MCR)           -> ALBUMS[1] "Sweet Revenge"
//   playlist 2 -> SD folder 03 (bunii)         -> ALBUMS[2] "bunii"
namespace PlaylistArt {

  // Given a 0-based playlist index (matches AudioPlayer::currentPlaylist())
  // and a 1-based track number (matches AudioPlayer::currentTrack()),
  // returns the ALBUMS[] index to load as cover art.
  uint8_t artIndexFor(uint8_t playlist, uint8_t track);

  // How many playlists are configured in the table. Used by the sketch to
  // wrap around when cycling playlists with the MENU button, so the wrap
  // point lives with the table instead of being hardcoded in the .ino.
  uint8_t playlistCount();

}
