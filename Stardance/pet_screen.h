// pet_screen.h -- the pet view: motion sampling, ball easing and drawing.
//
// What used to be Pet_Test.ino, minus setup(), loop(), the buttons and the
// MENU test stepping. Stardance.ino owns the buttons and calls Pet::trigger()
// itself; this module only keeps the pet alive and on screen.
//
// Idle:  pet rests, then plays BLINK, LOOKAROUND or (rarely) BUBBLE_FACE.
// Tilt:  the face follows how the device is held, as if printed on a ball.
// Shake: shaken face while it lasts, dizzy once it stops.
// Leave: leaveWith() plays one clip as the pet's way out of the view
//        (MUSIC_SPIN into the playlist chooser).
#pragma once
#include <stdint.h>
#include "pet_anims.h"

namespace PetScreen {

  // Call once at boot, after Display::init(). PetRenderer::init(), first
  // MPU6050 probe, Pet::begin().
  void init(uint32_t nowMs);

  // The pet view just became visible. Whatever way the device is held right
  // now becomes "straight", and the next tick() repaints the whole screen
  // (otherwise the previous view stays up until the next animation frame).
  void enter(uint32_t nowMs);

  // Call every loop() while the pet view is showing. Not frame-gated: it
  // samples motion at 100 Hz and only draws when the frame changed or the
  // face moved.
  void tick(uint32_t nowMs);

  // Play `id` as the pet's way out of the pet view. It starts at once (the
  // clip's neutral-face lead-in is skipped) and cuts off whatever the pet
  // was doing. tick() keeps drawing as usual until the clip has run
  // through, then stops drawing: leaveDone() turns true and the clip's last
  // frame stays on screen for the caller to cover with the next view.
  // Shaking is ignored while it plays. enter() clears all of this.
  void leaveWith(PetAnimId id, uint32_t nowMs);
  bool leaving();     // a leave clip is playing and has not finished yet
  bool leaveDone();   // it has finished

}
