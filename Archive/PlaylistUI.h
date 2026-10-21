#pragma once
#include <stdint.h>

namespace PlaylistArt {
    void init();
    void OpenPlaylist(uint8_t playlist);
    void OpenTrack(uint8_t track);
    void Scroll(uint8_t delta, uint32_t nowMs);
    uint8_t selectedIndex();
    uint8_t itemCount();
    uint8_t listplaylist();
    void tick(uint32_t nowMs);

    bool needsRedraw();
    
    void draw();
}