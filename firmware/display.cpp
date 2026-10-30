#include "display.h"
#include "disc_renderer.h"
#include "album_store.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <math.h>

namespace Display {

namespace {
  TFT_eSPI tft = TFT_eSPI();

  // One band buffer, shared by whichever view is drawing (disc here, the pet
  // via bandBuffer()). There used to be a second one (bandB) to ping-pong
  // with DMA pushes; DMA was reverted, and with blocking pushImage() the
  // buffer is free again as soon as the push returns. Dropping it saves the
  // 23 KB the pet needs.
  uint16_t bandA[BAND_PIXELS];   // 23 KB
}

void init() {
  tft.init();
  tft.setRotation(0);           // adjust if the face/UI ends up upside down
  tft.setSwapBytes(true);       // RP2040 backend: pushImage() byte-swaps by
                                 // default while fillScreen() doesn't — this
                                 // makes them agree. Without this, album art
                                 // pushed via pushImage() comes out with
                                 // channels scrambled (wrong/oversaturated
                                 // colors) even though solid fills look fine.
  tft.fillScreen(TFT_BLACK);
}

TFT_eSPI &panel() {
  return tft;
}

uint16_t *bandBuffer() {
  return bandA;
}

void pushRows(const uint16_t *band, int bandTop, int y0, int y1) {
#if UI_PUSH_ROUND
  // Half-width of the glass per row, built on first use.
  static uint8_t half[SCREEN_H];
  static bool    halfReady = false;
  if (!halfReady) {
    const float r = SCREEN_W / 2.0f;
    for (int y = 0; y < SCREEN_H; y++) {
      const float dy = y + 0.5f - r;
      const float v  = r * r - dy * dy;
      int h = v > 0 ? (int)ceilf(sqrtf(v)) + 1 : 0;
      if (h > SCREEN_W / 2) h = SCREEN_W / 2;
      half[y] = (uint8_t)h;
    }
    halfReady = true;
  }
  // One SPI transaction for all the rows, so each row only costs its
  // address window, not a chip-select cycle.
  tft.startWrite();
  for (int y = y0; y <= y1; y++) {
    const int h = half[y];
    if (!h) continue;
    const int x0 = SCREEN_W / 2 - h;
    tft.pushImage(x0, y, 2 * h, 1, (uint16_t *)(band + (y - bandTop) * SCREEN_W + x0));
  }
  tft.endWrite();
#else
  tft.pushImage(0, y0, SCREEN_W, y1 - y0 + 1, (uint16_t *)(band + (y0 - bandTop) * SCREEN_W));
#endif
}


void drawDisc() {
  for (uint8_t b = 0; b < NUM_BANDS; b++) {
    DiscRenderer::renderBand(bandA, b);
    tft.pushImage(0, b * BAND_H, SCREEN_W, BAND_H, bandA);
  }
}

void drawFlat() {

  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      bandA[x] = AlbumStore::palette[AlbumStore::pixels[y * ART_W + x]];
    }
    tft.pushImage(0, y, SCREEN_W, 1, bandA);
  }
}

}
