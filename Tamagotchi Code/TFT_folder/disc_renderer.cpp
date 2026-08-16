#include "disc_renderer.h"
#include "album_store.h"
#include "config.h"
#include <math.h>
#include <string.h>

namespace DiscRenderer {

namespace {
  constexpr float TWO_PI_F = 6.28318530718f;

=
  constexpr float SPIN_DEG_S      = 25.0f;
  constexpr float SPIN_RAD_S      = SPIN_DEG_S * (TWO_PI_F / 360.0f);
  constexpr float EASE_MS         = 600.0f;      // spin-up / spin-down time
  constexpr float ACCEL_RAD_S2    = SPIN_RAD_S / (EASE_MS / 1000.0f);

  constexpr float REPLAY_DURATION_MS = 350.0f;

  // --- Vinyl look ---
  constexpr float ALBUM_SCALE     = 0.6f;
  constexpr int   GROOVE_COUNT    = 40;    // fine ring bands

  constexpr uint16_t VINYL_BLACK     = 0x0000;
  constexpr uint16_t VINYL_GROOVE    = 0x18C3; 
  constexpr uint16_t VINYL_HIGHLIGHT = 0x5ACB; 

  float    angle        = 0.0f;   
  float    angularVel    = 0.0f;   
  bool     spinning      = false;  
  uint32_t lastTickMs     = 0;

  bool     replaying         = false; 
  bool     replayPending     = false; 
                                       // used to capture the eased tween's start point
  bool     resumeSpinAfter   = false; 
  float    replayStartAngle  = 0.0f;
  uint32_t replayStartMs     = 0;


  uint8_t halfChord[SCREEN_H];

  int32_t albumRadiusSq;


  int32_t grooveThresholdSq[GROOVE_COUNT];

  uint16_t vinylColorForRadiusSq(int32_t rSq) {
    int band = 0;
    while (band < GROOVE_COUNT && rSq > grooveThresholdSq[band]) band++;
    if (band >= GROOVE_COUNT - 1) return VINYL_HIGHLIGHT; // outermost ring(s)
    return (band & 1) ? VINYL_GROOVE : VINYL_BLACK;
  }


  float easeOutBack(float x) {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float xm1 = x - 1.0f;
    return 1.0f + c3 * xm1 * xm1 * xm1 + c1 * xm1 * xm1;
  }
}

void init() {
  for (int y = 0; y < SCREEN_H; y++) {
    int dy = y - DISC_CY;
    int v  = DISC_R * DISC_R - dy * dy;
    halfChord[y] = (v <= 0) ? 0 : (uint8_t)sqrtf((float)v);
  }

  const float albumRadius = DISC_R * ALBUM_SCALE;
  albumRadiusSq = (int32_t)(albumRadius * albumRadius);

  for (int i = 0; i < GROOVE_COUNT; i++) {
    float r = albumRadius + (DISC_R - albumRadius) * (float)(i + 1) / (float)GROOVE_COUNT;
    grooveThresholdSq[i] = (int32_t)(r * r);
  }

  angle      = 0.0f;
  angularVel = 0.0f;
  spinning   = false;
  lastTickMs = 0;
  replaying  = false;
  replayPending = false;
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

    if (!half) {
      memset(row, 0, SCREEN_W * sizeof(uint16_t));
      continue;
    }

    const int xs = DISC_CX - half;
    const int xe = DISC_CX + half;

    if (xs > 0)            memset(row, 0, xs * sizeof(uint16_t));
    if (xe < SCREEN_W - 1) memset(row + xe + 1, 0, (SCREEN_W - 1 - xe) * sizeof(uint16_t));

    const int dy   = y - DISC_CY;
    const int dyDy = dy * dy;
    const int dxStart = xs - DISC_CX;

    int32_t sx = ((int32_t)DISC_CX << 16) + dxStart * ca - dy * sa;
    int32_t sy = ((int32_t)DISC_CY << 16) + dxStart * sa + dy * ca;

    for (int x = xs; x <= xe; x++) {
      const int dx = x - DISC_CX;
      const int32_t rSq = dx * dx + dyDy;

      if (rSq <= albumRadiusSq) {
        const int srcX = sx >> 16;
        const int srcY = sy >> 16;
        row[x] = srcPal[srcPixels[srcY * ART_W + srcX]];
      } else {
        row[x] = vinylColorForRadiusSq(rSq);
      }

      sx += ca;
      sy += sa;
    }
  }
}

} // namespace DiscRenderer