#include "display.h"
#include "disc_renderer.h"
#include "album_store.h"
#include "config.h"
#include <TFT_eSPI.h>

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
