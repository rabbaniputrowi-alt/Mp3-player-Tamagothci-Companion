#pragma once
#include <stdint.h>

// Wraps DFRobotDFPlayerMini for folder-based "playlist" playback, where
// each numbered SD card folder (01-99) is one playlist mapped 1:1 to an
// entry in ALBUMS[] (folder 01 = ALBUMS[0], folder 02 = ALBUMS[1], ...).
//
// Play/pause state is tracked in firmware rather than queried from the
// module — DFPlayer clones are unreliable at answering status queries,
// so we assume our own commands succeeded rather than asking the module
// to confirm.
namespace AudioPlayer {

  // Starts the UART to the DFPlayer and sets a default volume. Some
  // DFPlayer clones (MH2024K, GD3200B chips) never send the expected
  // ACK during handshake — this proceeds regardless of whether the
  // handshake succeeded, since those clones still work for playback
  // commands even when begin() reports failure.
  void init();

  // Starts playback of `track` (1-based) within `playlist` (0-based,
  // matching an ALBUMS[] index — internally converted to the 1-based
  // SD card folder number).
  void playPlaylist(uint8_t playlist, uint8_t track = 1);

  // Advance/rewind within the *current* playlist only — these do not
  // cross into a different playlist/folder.
  void next();
  void previous();

  void togglePlayPause();
  bool isPlaying();

  uint8_t currentPlaylist(); // 0-based, matches ALBUMS[] index
  uint8_t currentTrack();    // 1-based, matches SD card file numbering

}