#pragma once
#include "albums.h"

namespace AlbumStore {
    extern uint8_t  pixels[ART_PIXELS];
    extern uint16_t palette[PALETTE_SIZE];

    void init();
    void load(uint8_t index);

    uint8_t current();
}