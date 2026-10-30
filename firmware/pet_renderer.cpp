// pet_renderer.cpp -- RLE decoder plus the "face on a ball" drawing.
//
// RLE format, see build_pet_anims.py:
//   byte = CCC LLLLL
//     CCC   = palette index 0..7
//     LLLLL = 0..30 -> run of 1..31 pixels
//             31    -> run of 32 + next byte (32..287 pixels)
//
// Which 8 colours those indices stand for is looked up per frame:
// PET_FRAME_PALETTE[frame] picks a row of PET_PALETTES. Row 0 is the pet's
// own palette; a clip that is not drawn in the pet's colours (the penguin
// gif) has a row of its own.
//
// RAM: 57.6 KB for the unpacked frame + 14.4 KB for the ball table.
#include "pet_renderer.h"
#include <math.h>
#include <string.h>

namespace {

//  tuning 
// Size of the imaginary ball the face is printed on, in pixels. The panel is
// 120 px in radius: a ball only just bigger squashes the face hard at the
// rim, a much bigger one makes it slide almost flat. Must be 121..255.
#ifndef PET_BALL_R
#define PET_BALL_R 128
#endif
const int   BALL_R = PET_BALL_R;
// Turns smaller than this are drawn flat (and cost nothing extra).
const float FLAT_BELOW_RAD = 0.0015f;
// 

const int W = PET_W;
const int H = PET_H;
const int HALF = PET_W / 2;

const uint8_t* s_p = nullptr;     // next RLE byte
const uint8_t* s_end = nullptr;   // one past this frame's last byte
uint16_t s_runLeft = 0;           // pixels still owed from the current run
uint16_t s_colour = 0;            // RGB565 of the current run
const uint16_t* s_pal = PET_PALETTES[0];   // the current frame's 8 colours

// ball mode
uint8_t  s_tex[PET_W * PET_H];    // current frame as palette indices
int32_t  s_texFrame = -1;         // which frame s_tex holds, -1 = none
uint8_t  s_bg = 0;                // palette index around the frame's rim
uint8_t  s_z[HALF * HALF];        // ball height above each pixel, one quarter
uint16_t s_ring[96];              // frame offsets of a ring just inside the rim
uint8_t  s_chord[PET_H];          // first visible column of every row
bool     s_zReady = false;
float    s_rx = 0, s_ry = 0, s_rz = 0;   // requested turn
bool     s_ball = false;                 // this frame is drawn in ball mode
// screen -> frame mapping for this frame, 16.16 fixed point (see startFrame)
int32_t  s_ax, s_bx, s_cx, s_ay, s_by, s_cy, s_aw, s_bw, s_cw;

void rewindFlat(uint16_t frameIndex) {
  s_p = PET_RLE + PET_FRAME_OFFSETS[frameIndex];
  s_end = PET_RLE + PET_FRAME_OFFSETS[frameIndex + 1];
  s_runLeft = 0;
  s_pal = PET_PALETTES[PET_FRAME_PALETTE[frameIndex] % PET_PALETTE_COUNT];
}

// Unpack frames[frameIndex] into s_tex and work out its background colour.
void loadTexture(uint16_t frameIndex) {
  const uint8_t* p = PET_RLE + PET_FRAME_OFFSETS[frameIndex];
  const uint8_t* end = PET_RLE + PET_FRAME_OFFSETS[frameIndex + 1];
  uint32_t o = 0;
  const uint32_t total = (uint32_t)W * H;
  while (o < total && p < end) {
    const uint8_t b = *p++;
    uint32_t run = b & 31;
    run = (run == 31) ? 32u + (p < end ? *p++ : 0) : run + 1;
    if (run > total - o) run = total - o;
    memset(s_tex + o, (b >> 5) % PET_PALETTE_SIZE, run);
    o += run;
  }
  if (o < total) memset(s_tex + o, 0, total - o);

  // Background = the commonest colour on a ring just inside the rim. Used
  // for the part of the ball the stored frame does not cover.
  uint16_t votes[8] = {0};
  for (uint16_t offset : s_ring) votes[s_tex[offset] & 7]++;
  s_bg = 0;
  for (uint8_t c = 1; c < 8; c++)
    if (votes[c] > votes[s_bg]) s_bg = c;

  // The corners outside the round panel hold filler, not art. Paint them
  // with the background so a turned ball never drags filler into view.
  for (int y = 0; y < H; y++) {
    const int xl = s_chord[y];
    memset(s_tex + y * W, s_bg, xl);
    memset(s_tex + y * W + (W - xl), s_bg, xl);
  }
  s_texFrame = frameIndex;
}

}  // namespace

namespace PetRenderer {

void init() {
  // Height of the ball above every pixel of one quarter of the screen;
  // the other three quarters are mirror images.
  for (int iy = 0; iy < HALF; iy++) {
    for (int ix = 0; ix < HALF; ix++) {
      const float dx = ix + 0.5f, dy = iy + 0.5f;
      const float h2 = (float)BALL_R * BALL_R - dx * dx - dy * dy;
      s_z[iy * HALF + ix] = h2 > 0 ? (uint8_t)(sqrtf(h2) + 0.5f) : 0;
    }
  }
  for (int i = 0; i < 96; i++) {
    const float a = i * (6.2831853f / 96);
    const int x = (int)(119.5f + 116.0f * cosf(a));
    const int y = (int)(119.5f + 116.0f * sinf(a));
    s_ring[i] = (uint16_t)(y * W + x);
  }
  for (int y = 0; y < H; y++) {
    const float dy = y - 119.5f;
    const float half = sqrtf(120.0f * 120.0f - dy * dy);
    const int xl = (int)(119.5f - half);
    s_chord[y] = (uint8_t)(xl < 0 ? 0 : xl);
  }
  s_zReady = true;
  s_texFrame = -1;
}

void setBall(float rx, float ry, float rz) {
  s_rx = rx;
  s_ry = ry;
  s_rz = rz;
}

void startFrame(uint16_t frameIndex) {
  if (frameIndex >= PET_FRAME_COUNT) frameIndex = 0;
  rewindFlat(frameIndex);

  const float rx = s_rx, ry = s_ry, rz = s_rz;
  const float angle = sqrtf(rx * rx + ry * ry + rz * rz);
  s_ball = s_zReady && angle >= FLAT_BELOW_RAD;
  if (!s_ball) return;

  if (s_texFrame != (int32_t)frameIndex) loadTexture(frameIndex);

  // Rotation matrix M for the turn (axis * angle = rx, ry, rz). A point q on
  // the unturned ball shows up at p = M q, so to find what belongs at screen
  // point p we look up q = M^T p. q's x and y pick the frame pixel; its z
  // says whether that spot was on the front of the ball to begin with.
  const float kx = rx / angle, ky = ry / angle, kz = rz / angle;
  const float c = cosf(angle), s = sinf(angle), t = 1 - c;
  const float m00 = c + kx * kx * t;
  const float m10 = ky * kx * t + kz * s;
  const float m20 = kz * kx * t - ky * s;
  const float m01 = kx * ky * t - kz * s;
  const float m11 = c + ky * ky * t;
  const float m21 = kz * ky * t + kx * s;
  // Screen x, y are handled in half-pixels (so pixel centres are whole
  // numbers), hence 32768 rather than 65536 for those two.
  s_ax = (int32_t)lroundf(m00 * 32768);
  s_bx = (int32_t)lroundf(m10 * 32768);
  s_cx = (int32_t)lroundf(m20 * 65536);
  // Frame rows count downward while y points up: flip the sign.
  s_ay = (int32_t)lroundf(-m01 * 32768);
  s_by = (int32_t)lroundf(-m11 * 32768);
  s_cy = (int32_t)lroundf(-m21 * 65536);
  s_aw = (int32_t)lroundf((kx * kz * t + ky * s) * 32768);
  s_bw = (int32_t)lroundf((ky * kz * t - kx * s) * 32768);
  s_cw = (int32_t)lroundf((c + kz * kz * t) * 65536);
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
      s_colour = s_pal[(b >> 5) % PET_PALETTE_SIZE];
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

bool renderBand(uint16_t* dst, uint16_t y0, uint16_t rows) {
  if (!s_ball) return fillPixels(dst, (uint32_t)W * rows);

  uint16_t pal[8];
  for (uint8_t i = 0; i < 8; i++) pal[i] = s_pal[i % PET_PALETTE_SIZE];
  const uint16_t bg = pal[s_bg];
  const int32_t centre = (int32_t)HALF << 16;
  const int32_t ax = 2 * s_ax, ay = 2 * s_ay, aw = 2 * s_aw;
  const int32_t cx = s_cx, cy = s_cy, cw = s_cw;
  const uint8_t* const tex = s_tex;

  for (uint16_t sy = y0; sy < y0 + rows; sy++, dst += W) {
    // Only the part of the row inside the round panel is worth working out.
    const int xl = s_chord[sy];
    for (int x = 0; x < xl; x++) dst[x] = dst[W - 1 - x] = bg;

    // Half-pixel coordinates from the screen centre: x2 = -239..239 left to
    // right, y2 = +239..-239 top to bottom.
    const int32_t y2 = (H - 1) - 2 * (int32_t)sy;
    const int32_t x2 = 2 * xl - (W - 1);
    int32_t u = centre + s_ax * x2 + s_bx * y2;
    int32_t v = centre + s_ay * x2 + s_by * y2;
    int32_t w = s_aw * x2 + s_bw * y2;
    const int iy = sy < HALF ? HALF - 1 - sy : sy - HALF;
    // Ball height: the table holds the right half of the row, the left half
    // is its mirror image, so walk in to the centre and back out again.
    const uint8_t* z = s_z + iy * HALF + (HALF - 1 - xl);
    int zStep = -1;
    uint16_t* out = dst + xl;
    for (int half = 0; half < 2; half++) {
      uint16_t* const end = out + (HALF - xl);
      while (out < end) {
        const int32_t h = *z;
        z += zStep;
        const uint32_t tx = (uint32_t)((u + cx * h) >> 16);
        const uint32_t ty = (uint32_t)((v + cy * h) >> 16);
        // w <= 0: this bit of the ball was round the back before the turn.
        uint16_t c = bg;
        if (tx < (uint32_t)W && ty < (uint32_t)H && w + cw * h > 0) c = pal[tex[ty * W + tx]];
        *out++ = c;
        u += ax;
        v += ay;
        w += aw;
      }
      z = s_z + iy * HALF;
      zStep = 1;
    }
  }
  return true;
}

}  // namespace PetRenderer
