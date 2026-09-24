#pragma once
#include <stdint.h>

namespace PlaylistUI {

  void init();
  void openPlaylists(uint8_t playlist);
  void openTracks(uint8_t playlist, uint8_t track);
  void scroll(int8_t delta, uint32_t nowMs);

  uint8_t selectedIndex();
  uint8_t itemCount();
  uint8_t listPlaylist();

  void tick(uint32_t nowMs);
  bool needsRedraw();
  void draw();

}
