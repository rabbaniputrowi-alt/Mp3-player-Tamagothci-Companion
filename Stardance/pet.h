// Behaviour:
//   * Rests on the neutral face, then every few seconds plays an idle
//     animation: BLINK most of the time, LOOKAROUND now and then, and
//     BUBBLE_FACE rarely.
//   * trigger() starts an interactive animation right away, cutting off an
//     idle one. While an interactive animation is playing, further triggers
//     are ignored so a button mash doesn't restart it every frame.
//   * play() starts an animation no matter what, for clips the app needs to
//     run from start to finish (MUSIC_SPIN as the way into the chooser).
//   * setShaking() overrides everything: SHAKE_START, then SHAKE_LOOP for as
//     long as the shaking goes on, then DIZZY once it stops. Except PENGUIN:
//     shaking is ignored while it plays.
//   * PENGUIN loops until stop() (or play() of another clip) ends it. The
//     sketch does that on a second PLAY or on MENU.
//   * When any other animation finishes, the pet goes back to resting.
#pragma once
#include <stdint.h>
#include "pet_anims.h"

namespace Pet {

// Call once. `seed` feeds the idle randomiser (e.g. micros() or an ADC read).
void begin(uint32_t nowMs, uint32_t seed);

// Advance the clock. Returns true when the frame to show has changed since
// the last call, including a change made by trigger(), play() or
// setShaking() in between.
bool update(uint32_t nowMs);

// Frame index into PET_FRAME_OFFSETS that should be on screen now.
uint16_t currentFrame();

// Start an animation now. Returns false if it was ignored because an
// interactive animation is still playing.
bool trigger(PetAnimId id, uint32_t nowMs);

// Start an animation now, no matter what is playing: unlike trigger() this
// also cuts off an interactive clip.
// Most clips open with a hold on the neutral face (MUSIC_SPIN: 25 ticks,
// 0.8 s). With skipLeadIn that hold is cut to one tick, so the motion starts
// on the button press instead of looking like the press was missed.
void play(PetAnimId id, uint32_t nowMs, bool skipLeadIn);

// Stop whatever is playing and go back to resting on the neutral face. This
// is how a looping clip (PENGUIN) ends.
void stop(uint32_t nowMs);

// Tell the pet whether the device is being shaken. Call every loop, before
// update(). Safe to call with the same value over and over. While PENGUIN
// plays this counts as not shaking, so a shake that is still going on when
// the penguin is stopped starts then.
void setShaking(bool shaking, uint32_t nowMs);

// PET_ANIM_COUNT while resting on the neutral face.
PetAnimId currentAnim();

// True while the face is in its normal spot (resting, blinking, winking), so
// it can be moved around by the tilt sensor. False during clips that move or
// fill the screen themselves.
bool tiltAllowed();

}  // namespace Pet
