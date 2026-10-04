// Behaviour:
//   * Rests on the neutral face, then every few seconds plays an idle
//     animation: BLINK most of the time, LOOKAROUND now and then, and
//     BUBBLE_FACE rarely.
//   * trigger() starts an interactive animation right away, cutting off an
//     idle one. While an interactive animation is playing, further triggers
//     are ignored so a button mash doesn't restart it every frame.
//   * setShaking() overrides everything: SHAKE_START, then SHAKE_LOOP for as
//     long as the shaking goes on, then DIZZY once it stops.
//   * When any animation finishes, the pet goes back to resting.
#pragma once
#include <stdint.h>
#include "pet_anims.h"

namespace Pet {

// Call once. `seed` feeds the idle randomiser (e.g. micros() or an ADC read).
void begin(uint32_t nowMs, uint32_t seed);

// Advance the clock. Returns true when the frame to show has changed.
bool update(uint32_t nowMs);

// Frame index into PET_FRAME_OFFSETS that should be on screen now.
uint16_t currentFrame();

// Start an animation now. Returns false if it was ignored because an
// interactive animation is still playing.
bool trigger(PetAnimId id, uint32_t nowMs);

// Tell the pet whether the device is being shaken. Call every loop, before
// update(). Safe to call with the same value over and over.
void setShaking(bool shaking, uint32_t nowMs);

// PET_ANIM_COUNT while resting on the neutral face.
PetAnimId currentAnim();

// True while the face is in its normal spot (resting, blinking, winking), so
// it can be moved around by the tilt sensor. False during clips that move or
// fill the screen themselves.
bool tiltAllowed();

}  // namespace Pet
