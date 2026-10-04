// pet_renderer.h -- draws one RLE pet frame out of flash into band buffers.
//
// No Arduino dependencies, so it can be host-compiled and tested.
//
// Two ways to draw the same frame:
//   * Flat (ball centred): the RLE stream is unpacked straight into the band
//     buffers. Fast, and pixel-for-pixel what the pipeline stored.
//   * Ball (setBall() with a turn): the frame is unpacked into RAM once, then
//     painted as if it were printed on a ball that has been turned. The face
//     slides toward the rim and squashes as it gets there.
//
// Bands must be drawn top to bottom and must cover the whole frame.
#pragma once
#include <stdint.h>
#include "pet_anims.h"

namespace PetRenderer {

// Call once before anything else (builds the ball lookup table).
void init();

// How far the ball is turned, radians about the screen's x (right), y (up)
// and z (toward the viewer) axes, same convention as Motion::look().
// Takes effect from the next startFrame().
void setBall(float rx, float ry, float rz);

// Get ready to draw frames[frameIndex].
void startFrame(uint16_t frameIndex);

// Fill `rows` rows starting at screen row y0 (RGB565, native byte order).
// Returns false if the frame's data ran out early (corrupt / wrong offsets).
bool renderBand(uint16_t* dst, uint16_t y0, uint16_t rows);

// Low-level flat decoder: the next `count` pixels of the current frame.
bool fillPixels(uint16_t* dst, uint32_t count);

}  // namespace PetRenderer
