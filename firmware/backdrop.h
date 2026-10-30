#pragma once
#include <stdint.h>

// Blurred album-art backdrop behind the chooser, after the Claude Design mock:
// the cover drawn 1.25x larger than the screen, blurred, saturated 1.15x, and
// darkened by one of two shades. No Arduino dependencies.
namespace Backdrop {

  // How the backdrop is darkened (the mock's "shade" layer).
  enum class Shade : uint8_t {
    TOP_DOWN,   // playlists: light at the top, dark from 70% down (title and ticks sit there)
    VIGNETTE    // tracks: dark all over, darkest towards the rim
  };

  // Allocates the thumbnail cache (3 slots, about 9 KB at UI_BG_CELL 8).
  void init();

  // Shows art `artA` blended `t`/256 of the way to art `artB`. The chooser
  // calls it with the two albums either side of its scroll position, so the
  // backdrop crossfades as the list slides.
  void setBlend(uint8_t artA, uint8_t artB, uint8_t t);

  void setShade(Shade s);

  // Writes rows y0..y1 (screen rows) into `band`, a SCREEN_W-wide buffer whose
  // first row is screen row `bandTop`. Pixels are plain RGB565 (not
  // byte-swapped), ready for a pushImage() with setSwapBytes(true).
  // Outside the DISC_R circle the rows are black.
  void renderRows(uint16_t *band, int bandTop, int y0, int y1);

}
