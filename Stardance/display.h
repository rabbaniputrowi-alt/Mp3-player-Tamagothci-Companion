#pragma once
#include <stdint.h>

class TFT_eSPI;   // forward declared — only a reference is handed out, so
                  // including TFT_eSPI.h here (and in everything downstream
                  // of this header) isn't necessary.

// Tier 3 -> Tier 4: owns the TFT_eSPI instance and the band buffer,
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

  // The one TFT_eSPI instance. Exposed so other modules can push to it (the
  // pet view does) — there must never be a second instance, since each one
  // would try to own the same SPI bus and CS pin. Only valid after init().
  TFT_eSPI &panel();

  // The one band buffer, BAND_PIXELS (SCREEN_W x BAND_H) RGB565 pixels.
  // Shared by whichever view is drawing: drawDisc() uses it, the pet view
  // renders into it, and the chooser uses it as two 24-row halves (one being
  // drawn while the other is pushed). Only one view draws at a time, and
  // every push is blocking, so the buffer is free again once a draw returns.
  uint16_t *bandBuffer();

  // Pushes screen rows y0..y1 of `band` (whose first row is screen row
  // bandTop) to the panel. With UI_PUSH_ROUND only the part of each row
  // inside the round glass is sent, which saves about a fifth of the SPI
  // time of a full frame. Plain RGB565, like everything in the band buffer.
  void pushRows(const uint16_t *band, int bandTop, int y0, int y1);

}
