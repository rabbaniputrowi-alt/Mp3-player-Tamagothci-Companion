// pet_renderer.h -- streams one RLE pet frame out of flash into band buffers.
//
// No Arduino dependencies, so it can be host-compiled and tested.
//
// Bands must be filled in order and must cover the whole frame, because the
// RLE stream runs continuously across rows.
#pragma once
#include <stdint.h>
#include "pet_anims.h"

namespace PetRenderer {

// Rewind the decoder to the start of frames[frameIndex].
void startFrame(uint16_t frameIndex);

// Decode the next `count` pixels (RGB565, native byte order) into dst.
// Returns false if the frame's data ran out early (corrupt / wrong offsets).
bool fillPixels(uint16_t* dst, uint32_t count);

}  // namespace PetRenderer
