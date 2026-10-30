#pragma once
#include <stdint.h>

// Playlist and track chooser, after the Claude Design mock "Vinyl & barrel".
//   Playlists: small records ride an arc along the top of the screen; the
//     selected one is full size, its neighbours peek in at 70% on either
//     side. The list wraps. The playlist's name sits under the record and a
//     row of ticks on the bottom rim shows where it is in the list.
//   Tracks: titles roll on a barrel through a dark band across the middle,
//     narrowing with the circle. The selected title scrolls sideways if it
//     does not fit. The left rim shows the position in the list, the footer
//     "n / N".
// Draws in 24-row bands into Display::bandBuffer(); no sprite.
namespace PlaylistUI {

  // Allocates the backdrop thumbnail cache (the record labels have no cache
  // any more, MiniRecord::init() returns 0). Returns the bytes MiniRecord
  // allocated (always 0), for the heap check in setup().
  uint32_t init();

  void openPlaylists(uint8_t playlist);
  void openTracks(uint8_t playlist, uint8_t track);   // track is 1-based
  bool inTracks();

  // PREV / NEXT.
  void scroll(int8_t delta, uint32_t nowMs);

  uint8_t selectedIndex();   // 0-based row in the open list
  uint8_t itemCount();
  uint8_t listPlaylist();    // playlist the open list belongs to (or the selected one)

  // Redraw on the next frame.
  void refresh();

  void tick(uint32_t nowMs);
  bool needsRedraw();
  void draw();

  // With UI_DUAL_CORE: core 1's side of draw(). Waits for a band to draw,
  // draws it, reports back. Called over and over from loop1().
  void core1Service();

}
