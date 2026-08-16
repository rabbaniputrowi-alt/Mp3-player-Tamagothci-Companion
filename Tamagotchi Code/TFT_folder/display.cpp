#include "display.h"
#include "disc_renderer.h"
#include "album_store.h"
#include "config.h"
#include <TFT_eSPI.h>

namespace Display {

namespace {
  TFT_eSPI tft = TFT_eSPI();

  uint16_t bandA[BAND_PIXELS];   // 23 KB
  uint16_t bandB[BAND_PIXELS];   // 23 KB
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

void drawDisc() {
  uint16_t *buf = bandA;

  for (uint8_t b = 0; b < NUM_BANDS; b++) {
    DiscRenderer::renderBand(buf, b);
    tft.pushImage(0, b * BAND_H, SCREEN_W, BAND_H, buf);
    buf = (buf == bandA) ? bandB : bandA;
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