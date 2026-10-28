// Hot per-pixel loop (renderBand runs every spin frame, full screen): built
// with -O2 whatever the IDE's "Optimize" menu says, same reasoning as
// mini_record.cpp / backdrop.cpp / ui_draw.cpp.
#pragma GCC optimize("O2")

#include "disc_renderer.h"
#include "album_store.h"
#include "config.h"
#include <math.h>
#include <string.h>

namespace DiscRenderer {

namespace {
  constexpr float TWO_PI_F = 6.28318530718f;

  constexpr float SPIN_DEG_S      = 25.0f;
  constexpr float SPIN_RAD_S      = SPIN_DEG_S * (TWO_PI_F / 360.0f);
  constexpr float EASE_MS         = 600.0f;      // spin-up / spin-down time
  constexpr float ACCEL_RAD_S2    = SPIN_RAD_S / (EASE_MS / 1000.0f);

  constexpr float REPLAY_DURATION_MS = 350.0f;

  // Album art fills the whole disc, edge to edge — build_albums.py already
  // center-crops each cover to a square and masks it to a circle of radius
  // DISC_R, so the art's own edge *is* the disc's edge. With DISC_GROOVES a
  // faint vinyl sheen (grooves + a fixed glare) is blended over the art; see
  // below. No vinyl plate or outer ring either way — those stay rejected.
  //
  // The slight overscan (>1.0) shrinks the sampled source radius a couple
  // of pixels inside that mask. Without it, the outermost ring of screen
  // pixels can sample a source pixel sitting just outside the mask and
  // come back black, leaving a speckled rim.
  constexpr float ALBUM_SCALE = 1.03f;

  float    angle         = 0.0f;
  float    angularVel    = 0.0f;
  bool     spinning      = false;
  uint32_t lastTickMs    = 0;

  bool     replaying         = false;
  bool     replayPending     = false;
                                       // used to capture the eased tween's start point
  bool     resumeSpinAfter   = false;
  float    replayStartAngle  = 0.0f;
  uint32_t replayStartMs     = 0;

  uint8_t halfChord[SCREEN_H];

  float easeOutBack(float x) {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float xm1 = x - 1.0f;
    return 1.0f + c3 * xm1 * xm1 * xm1 + c1 * xm1 * xm1;
  }

  // ---- Vinyl sheen: the chooser's groove gradient (mini_record.cpp),
  // computed here at the big disc's own radius. Deliberately screen-space
  // (dx, dy from the disc's screen centre, not the rotated source position):
  // grooves are concentric rings around the spindle, so a rotation doesn't
  // change them, which means this doesn't need the rotation matrix either,
  // just the pixel's screen position the loop already has.
  //
  // The mock's glare wedges are NOT included here. They cost a per-pixel
  // angle (a division) over a wide swath of the disc, which is cheap on a
  // mini record only because grooveRun() there is already called on a thin,
  // pre-clipped ring; on the full disc it measured ~13 ms/frame extra on an
  // emulated M0+ (vs ~9 ms for the grooves alone), on top of an already
  // ~40 ms push. Revisit if that still feels worth it once this is actually
  // on hardware.
#if DISC_GROOVES
  constexpr float D_GROOVE_PERIOD = 1.8f * 1.875f;   // same physical pitch as the mini records
  constexpr float D_GROOVE_START  = 1.1f * 1.875f;
  constexpr float D_GROOVE_PEAK   = 1.4f * 1.875f;
  constexpr float D_GROOVE_A      = 0.07f;

  constexpr int D_GROOVE_STEP = 4;   // px^2 per LUT step (d2 is plain px^2 here, no
                                      // Q16 — the disc never scales, unlike a record)
  constexpr int D_GROOVE_SIZE = (int)((DISC_R + 2) * (DISC_R + 2) / D_GROOVE_STEP) + 2;
  uint8_t dGrooveLut[D_GROOVE_SIZE];   // alpha by d2, x2048 (so .07 = 143)

  float dGrooveAt(float r) {
    const float p = fmodf(r, D_GROOVE_PERIOD);
    if (p < D_GROOVE_START) return 0.0f;
    if (p < D_GROOVE_PEAK)  return D_GROOVE_A * (p - D_GROOVE_START) / (D_GROOVE_PEAK - D_GROOVE_START);
    return D_GROOVE_A * (D_GROOVE_PERIOD - p) / (D_GROOVE_PERIOD - D_GROOVE_PEAK);
  }

  // RGB565 in "lanes" (as in mini_record.cpp / backdrop.cpp): one multiply
  // blends all three channels toward white.
  constexpr uint32_t D_LANES = 0x07E0F81Fu;
  inline uint32_t dSpread(uint16_t c) { return (c | ((uint32_t)c << 16)) & D_LANES; }
  inline uint16_t dFold(uint32_t v)   { v &= D_LANES; return (uint16_t)(v | (v >> 16)); }
  inline uint16_t dBlendWhite(uint16_t bg, uint32_t a5 /*0..32*/) {
    const uint32_t b = dSpread(bg);
    return dFold(b + (((D_LANES - b) * a5) >> 5));
  }

  void initSheen() {
    for (int i = 0; i < D_GROOVE_SIZE; i++) {
      const float r = sqrtf((float)(D_GROOVE_STEP * i + D_GROOVE_STEP / 2));
      float s = 0.0f;
      for (int k = 0; k < 16; k++) s += dGrooveAt(r - 0.5f + (k + 0.5f) / 16.0f);
      dGrooveLut[i] = (uint8_t)lroundf(s / 16.0f * 2048.0f);
    }
  }
#endif
}

void init() {
  for (int y = 0; y < SCREEN_H; y++) {
    int dy = y - DISC_CY;
    int v  = DISC_R * DISC_R - dy * dy;
    halfChord[y] = (v <= 0) ? 0 : (uint8_t)sqrtf((float)v);
  }

  angle      = 0.0f;
  angularVel = 0.0f;
  spinning   = false;
  lastTickMs = 0;
  replaying  = false;
  replayPending = false;

#if DISC_GROOVES
  initSheen();
#endif
}

void setAngle(float radians) {
  radians = fmodf(radians, TWO_PI_F);
  if (radians < 0.0f) radians += TWO_PI_F;
  angle = radians;
}

float getAngle() {
  return angle;
}

void setSpinning(bool on) {
  spinning = on;
}

bool isSpinning() {
  return spinning;
}

void replay() {
  resumeSpinAfter = spinning;
  replaying       = true;
  replayPending   = true;  // tick() captures the actual start angle/time
                            // on its next call — replay() itself doesn't
                            // know "now" in tick()'s clock.
}

void tick(uint32_t nowMs) {
  if (lastTickMs == 0) {
    lastTickMs = nowMs;
    return;
  }

  float dt = (nowMs - lastTickMs) * 0.001f;
  lastTickMs = nowMs;
  if (dt <= 0.0f || dt > 0.25f) return;

  if (replaying) {
    if (replayPending) {
      replayStartAngle = angle;
      replayStartMs    = nowMs;
      replayPending    = false;
    }

    float t = (float)(nowMs - replayStartMs) / REPLAY_DURATION_MS;

    if (t >= 1.0f) {
      angle      = 0.0f;
      replaying  = false;
      spinning   = resumeSpinAfter;
      angularVel = 0.0f;  // clean start for normal easing from here
    } else {
      float eased = easeOutBack(t);

      angle = replayStartAngle * (1.0f - eased);
    }
    return;
  }

  float targetVel = spinning ? SPIN_RAD_S : 0.0f;

  if (angularVel < targetVel) {
    angularVel += ACCEL_RAD_S2 * dt;
    if (angularVel > targetVel) angularVel = targetVel;
  } else if (angularVel > targetVel) {
    angularVel -= ACCEL_RAD_S2 * dt;
    if (angularVel < targetVel) angularVel = targetVel;
  }

  angle += angularVel * dt;
  if (angle >= TWO_PI_F) angle -= TWO_PI_F;
  if (angle < 0.0f)      angle += TWO_PI_F;
}

void renderBand(uint16_t *out, uint8_t bandIndex) {
  const int y0 = bandIndex * BAND_H;

  const float invScale = 1.0f / ALBUM_SCALE;
  const int32_t ca = (int32_t)(cosf(angle) * 65536.0f * invScale);
  const int32_t sa = (int32_t)(sinf(angle) * 65536.0f * invScale);

  const uint8_t  *srcPixels = AlbumStore::pixels;
  const uint16_t *srcPal    = AlbumStore::palette;

  for (int y = y0; y < y0 + BAND_H; y++) {
    uint16_t *row = out + (y - y0) * SCREEN_W;
    const int half = halfChord[y];

    // Rows above/below the circle are entirely off-disc.
    if (!half) {
      memset(row, 0, SCREEN_W * sizeof(uint16_t));
      continue;
    }

    const int xs = DISC_CX - half;
    const int xe = DISC_CX + half;

    // Black out the corners of the panel that fall outside the circle.
    if (xs > 0)            memset(row, 0, xs * sizeof(uint16_t));
    if (xe < SCREEN_W - 1) memset(row + xe + 1, 0, (SCREEN_W - 1 - xe) * sizeof(uint16_t));

    const int dy      = y - DISC_CY;
    const int dxStart = xs - DISC_CX;

    int32_t sx = ((int32_t)DISC_CX << 16) + dxStart * ca - dy * sa;
    int32_t sy = ((int32_t)DISC_CY << 16) + dxStart * sa + dy * ca;

#if DISC_GROOVES
    // This row's incremental d2 = dx^2 + dy^2 (screen space, plain px^2),
    // updated a step at a time instead of squaring dx every pixel.
    int32_t d2     = dxStart * dxStart + dy * dy;
    int32_t twodx1 = 2 * dxStart + 1;
#endif

    // Every pixel between xs and xe is inside the circle, so it is album
    // art — no per-pixel radius test needed any more.
    for (int x = xs; x <= xe; x++) {
      const int srcX = sx >> 16;
      const int srcY = sy >> 16;
      const uint16_t px = srcPal[srcPixels[srcY * ART_W + srcX]];

#if DISC_GROOVES
      uint32_t gi = (uint32_t)d2 >> 2;   // /D_GROOVE_STEP
      if (gi >= (uint32_t)D_GROOVE_SIZE) gi = D_GROOVE_SIZE - 1;
      const int w = dGrooveLut[gi] * 2;                                // Q12 white
      row[x] = w ? dBlendWhite(px, (uint32_t)w >> 7) : px;
      d2 += twodx1;
      twodx1 += 2;
#else
      row[x] = px;
#endif

      sx += ca;
      sy += sa;
    }
  }
}

} // namespace DiscRenderer
