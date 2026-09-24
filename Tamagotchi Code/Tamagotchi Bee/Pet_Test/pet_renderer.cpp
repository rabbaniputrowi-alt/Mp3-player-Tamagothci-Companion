// pet_renderer.cpp -- RLE decoder, see build_pet_anims.py for the format:
//
//   byte = CCC LLLLL
//     CCC   = palette index 0..7
//     LLLLL = 0..30 -> run of 1..31 pixels
//             31    -> run of 32 + next byte (32..287 pixels)
#include "pet_renderer.h"

namespace {
const uint8_t* s_p = nullptr;     // next RLE byte
const uint8_t* s_end = nullptr;   // one past this frame's last byte
uint16_t s_runLeft = 0;           // pixels still owed from the current run
uint16_t s_colour = 0;            // RGB565 of the current run
}  // namespace

namespace PetRenderer {

void startFrame(uint16_t frameIndex) {
  if (frameIndex >= PET_FRAME_COUNT) frameIndex = 0;
  s_p = PET_RLE + PET_FRAME_OFFSETS[frameIndex];
  s_end = PET_RLE + PET_FRAME_OFFSETS[frameIndex + 1];
  s_runLeft = 0;
}

bool fillPixels(uint16_t* dst, uint32_t count) {
  while (count) {
    if (s_runLeft == 0) {
      if (s_p >= s_end) {
        // Out of data: blank the rest so a bad frame shows as black,
        // not as leftovers from the previous band.
        while (count--) *dst++ = 0;
        return false;
      }
      const uint8_t b = *s_p++;
      s_colour = PET_PALETTE[(b >> 5) % PET_PALETTE_SIZE];
      const uint8_t len = b & 31;
      if (len == 31) {
        s_runLeft = 32 + (s_p < s_end ? *s_p++ : 0);
      } else {
        s_runLeft = len + 1;
      }
    }
    uint32_t n = s_runLeft < count ? s_runLeft : count;
    s_runLeft -= n;
    count -= n;
    const uint16_t c = s_colour;
    while (n--) *dst++ = c;
  }
  return true;
}

}  // namespace PetRenderer
