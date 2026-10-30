#include "album_store.h"
#include <string.h>

namespace AlbumStore {

  uint8_t  pixels[ART_PIXELS];
  uint16_t palette[PALETTE_SIZE];

  namespace {
    uint8_t curIndex = 0xFF;   // 0xFF = nothing loaded yet
  }

  void init() {
    load(0);
  }

  uint8_t current() {
    return curIndex;
  }

  void load(uint8_t index) {
    if (index >= ALBUM_COUNT) return;   // out of range, ignore
    if (index == curIndex) return;      // already loaded, avoid redundant memcpy

    memcpy(pixels,  ALBUMS[index].pixels,  ART_PIXELS);
    memcpy(palette, ALBUMS[index].palette, PALETTE_SIZE * sizeof(uint16_t));
    curIndex = index;
  }

}