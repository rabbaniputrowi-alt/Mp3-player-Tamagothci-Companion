//  idle/interactive/shake animation state machine.
#include "pet.h"

namespace {

//  tuning 
const uint32_t REST_MIN_MS = 2500;            // neutral-face pause between idles
const uint32_t REST_MAX_MS = 6000;
const uint32_t REST_AFTER_INTERACTIVE_MS = 1500;
const uint8_t  LOOKAROUND_CHANCE_PCT = 30;
const uint8_t  BUBBLE_CHANCE_PCT = 8;         // the rare one; the rest is BLINK
const uint8_t  BUBBLE_MIN_GAP = 5;            // idles between two bubble faces
const uint32_t SHAKE_MIN_MS = 900;            // a short jolt still gets a proper shake
// 

PetAnimId s_anim = PET_ANIM_COUNT;            // PET_ANIM_COUNT == resting
uint16_t  s_step = 0;
uint32_t  s_stepEnd = 0;                      // ms when the current step ends
uint32_t  s_restUntil = 0;
uint16_t  s_frame = 0;
uint16_t  s_reported = 0xFFFF;                // frame update() last told the caller about
uint16_t  s_restFrame = 0;
PetAnimId s_lastIdle = PET_ANIM_COUNT;
uint8_t   s_sinceBubble = 0;                  // idles played since the last bubble face
bool      s_shaking = false;
uint32_t  s_shakeMinUntil = 0;
uint32_t  s_rng = 0x9E3779B9u;

uint32_t rnd() {                              // xorshift32
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}

uint32_t rndRange(uint32_t lo, uint32_t hi) { return lo + rnd() % (hi - lo + 1); }

// Wrap-safe "a is at or after b" for millis().
bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

void rest(uint32_t now, uint32_t ms) {
  s_anim = PET_ANIM_COUNT;
  s_frame = s_restFrame;
  s_restUntil = now + ms;
}

void start(PetAnimId id, uint32_t now, bool skipLeadIn = false) {
  s_anim = id;
  s_step = 0;
  s_frame = PET_ANIMS[id].steps[0].frame;
  uint32_t hold = PET_ANIMS[id].steps[0].hold;
  // Only a first step that sits on the neutral face is a lead-in. A clip
  // that opens on something else keeps its timing.
  if (skipLeadIn && s_frame == s_restFrame) hold = 1;
  s_stepEnd = now + hold * PET_TICK_MS;
}

PetAnimId pickIdle() {
  const uint32_t r = rnd() % 100;
  PetAnimId id = PET_ANIM_BLINK;
  if (r < BUBBLE_CHANCE_PCT) {
    if (s_sinceBubble >= BUBBLE_MIN_GAP) id = PET_ANIM_BUBBLE_FACE;
  } else if (r < (uint32_t)BUBBLE_CHANCE_PCT + LOOKAROUND_CHANCE_PCT) {
    if (s_lastIdle != PET_ANIM_LOOKAROUND) id = PET_ANIM_LOOKAROUND;  // never twice running
  }
  if (id == PET_ANIM_BUBBLE_FACE) s_sinceBubble = 0;
  else if (s_sinceBubble < 255) s_sinceBubble++;
  s_lastIdle = id;
  return id;
}

bool inShake() { return s_anim == PET_ANIM_SHAKE_START || s_anim == PET_ANIM_SHAKE_LOOP; }

// What follows `finished`, starting at time `t`. Returns false if nothing
// does and the pet should rest.
bool chain(PetAnimId finished, uint32_t t) {
  if (finished == PET_ANIM_SHAKE_START || finished == PET_ANIM_SHAKE_LOOP) {
    const bool keepGoing = s_shaking || !reached(t, s_shakeMinUntil);
    start(keepGoing ? PET_ANIM_SHAKE_LOOP : PET_ANIM_DIZZY, t);
    return true;
  }
  // The penguin dances on until the sketch stops it (second PLAY or MENU).
  // Starting it again at `t`, the moment the last step ended, keeps the loop
  // seamless: the gif's last frame runs straight into its first.
  if (finished == PET_ANIM_PENGUIN) {
    start(PET_ANIM_PENGUIN, t);
    return true;
  }
  return false;
}

// Move the state machine on to `now`.
void advance(uint32_t now) {
  if (s_anim == PET_ANIM_COUNT) {
    if (reached(now, s_restUntil)) start(pickIdle(), now);
    return;
  }

  // The shaking stopped: cut the loop short and go dizzy.
  if (s_anim == PET_ANIM_SHAKE_LOOP && !s_shaking && reached(now, s_shakeMinUntil)) {
    start(PET_ANIM_DIZZY, now);
    return;
  }

  // Advance; if we fell behind (slow draw), skip frames to stay in time.
  while (reached(now, s_stepEnd)) {
    const PetAnim& a = PET_ANIMS[s_anim];
    if (++s_step >= a.stepCount) {
      if (chain(s_anim, s_stepEnd)) continue;
      bool wasInteractive = a.kind == PET_KIND_INTERACTIVE;
      rest(now, wasInteractive ? REST_AFTER_INTERACTIVE_MS
                               : rndRange(REST_MIN_MS, REST_MAX_MS));
      return;
    }
    s_frame = a.steps[s_step].frame;
    s_stepEnd += (uint32_t)a.steps[s_step].hold * PET_TICK_MS;
  }
}

} 

namespace Pet {

void begin(uint32_t nowMs, uint32_t seed) {
  s_rng = seed ? seed : 0x9E3779B9u;
  // The neutral face is the first frame of BLINK.
  s_restFrame = PET_ANIMS[PET_ANIM_BLINK].steps[0].frame;
  s_lastIdle = PET_ANIM_COUNT;
  s_sinceBubble = 0;
  s_shaking = false;
  s_reported = 0xFFFF;                        // nothing drawn yet
  rest(nowMs, rndRange(REST_MIN_MS / 2, REST_MAX_MS / 2));
}

bool update(uint32_t now) {
  advance(now);
  // "Changed" is measured against the frame the caller was last told about,
  // not against the frame at the top of this call. trigger(), play() and
  // setShaking() put a clip's first frame in place before update() runs, and
  // comparing within the call missed that frame: it was only drawn once the
  // clip reached its second step. That did not show while every clip opened
  // on the neutral face; PENGUIN does not, and lost its first 0.1 s.
  const bool changed = s_frame != s_reported;
  s_reported = s_frame;
  return changed;
}

uint16_t currentFrame() { return s_frame; }

PetAnimId currentAnim() { return s_anim; }

bool trigger(PetAnimId id, uint32_t now) {
  if (id >= PET_ANIM_COUNT) return false;
  if (s_anim != PET_ANIM_COUNT && PET_ANIMS[s_anim].kind == PET_KIND_INTERACTIVE)
    return false;
  start(id, now);
  return true;
}

void play(PetAnimId id, uint32_t now, bool skipLeadIn) {
  if (id >= PET_ANIM_COUNT) return;
  start(id, now, skipLeadIn);
}

void stop(uint32_t now) {
  rest(now, REST_AFTER_INTERACTIVE_MS);
}

void setShaking(bool shaking, uint32_t now) {
  // Only the buttons end the penguin, so shaking does not count while it
  // plays. s_shaking stays false meanwhile, which means a shake still going
  // on when it is stopped is seen as a new one and starts straight away.
  if (s_anim == PET_ANIM_PENGUIN) shaking = false;
  const bool began = shaking && !s_shaking;
  s_shaking = shaking;
  if (!began || inShake()) return;
  // Shaken while still dizzy: straight back into the loop, no wind-up.
  start(s_anim == PET_ANIM_DIZZY ? PET_ANIM_SHAKE_LOOP : PET_ANIM_SHAKE_START, now);
  s_shakeMinUntil = now + SHAKE_MIN_MS;
}

bool tiltAllowed() {
  return s_anim == PET_ANIM_COUNT || s_anim == PET_ANIM_BLINK || s_anim == PET_ANIM_WINK;
}

} 
