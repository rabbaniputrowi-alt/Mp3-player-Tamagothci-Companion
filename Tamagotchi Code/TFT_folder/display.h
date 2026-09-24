#pragma once
#include <stdint.h>

class TFT_eSPI;   // forward declared — only a reference is handed out, so
                  // including TFT_eSPI.h here (and in everything downstream
                  // of this header) isn't necessary.

// Tier 3 -> Tier 4: owns the TFT_eSPI instance and the two band buffers,
// drives the render->push loop that puts DiscRenderer's output on screen.
namespace Display {

  // Initializes TFT_eSPI, sets rotation, clears the screen.
  // Call after DiscRenderer::init() and AlbumStore::init().
  void init();

  // Renders and pushes all NUM_BANDS bands for one frame.
  // Call once per loop() iteration (or on a fixed frame timer).
  void drawDisc();

  // Step-2-only helper: pushes AlbumStore's current art straight to screen
  // with a plain palette lookup, no rotation math at all. Use this to prove
  // the flash->RAM copy and palette are correct before trusting renderBand().
  void drawFlat();

  // The one TFT_eSPI instance. Exposed so other modules can build sprites
  // against it (PlaylistUI does) — there must never be a second instance,
  // since each one would try to own the same SPI bus and CS pin.
  // Only valid after init().
  TFT_eSPI &panel();

}
