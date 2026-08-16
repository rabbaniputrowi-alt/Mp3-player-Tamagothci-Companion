#pragma once
#include <stdint.h>

// Tier 2 -> Tier 3: reads AlbumStore::pixels (RAM working copy),
// writes rotated RGB565 output into a caller-supplied band buffer.
namespace DiscRenderer {

  // Call once at boot. Builds the chord table (per-row half-widths
  // of the disc circle) so renderBand() never calls sqrtf() at runtime.
  void init();

  // Jog-wheel mode: set the disc angle directly, no easing applied.
  void setAngle(float radians);

  // Current angle, radians, wrapped to [0, 2*PI).
  float getAngle();

  // Call once per frame with millis(). Advances angle by the current
  // angular velocity, and eases velocity toward the spin/stop target.
  void tick(uint32_t nowMs);

  // Play/pause. Triggers the ~600ms spin-up or spin-down ease in tick().
  void setSpinning(bool on);
  bool isSpinning();

  // Quick "rewind": animates the angle back down to 0 at high speed,
  // ignoring the normal spin-up/down easing entirely. Whatever play/pause
  // state was active before replay() is called is restored automatically
  // once the disc reaches 0 — call isSpinning() afterward if you need to
  // know which state it landed in.
  void replay();

  // Renders scanlines [bandIndex*BAND_H, +BAND_H) into `out` (BAND_H*240 px).
  // Reads the *current* angle — call once per band per frame.
  void renderBand(uint16_t *out, uint8_t bandIndex);

}