#pragma once
#include <stdint.h>
#include "ui_draw.h"

// The small records on the chooser's playlist arc, after the Claude Design
// mock: a black disc with the album cover as its label, a spindle hole, a
// thin light rim and a soft drop shadow. With UI_RECORD_GROOVES the disc also
// carries the mock's two gradients: a faint repeating radial ramp (the
// grooves, one every 3.4 px) and a fixed conic highlight (two glare wedges,
// upper right and lower left, that do not turn). They are computed per pixel
// from the distance and angle to the centre, not drawn as rings.
//
// The label is the exact cover: draw() samples the art in flash directly,
// one lookup per pixel, the same picture build_albums.py wrote and the same
// crop the disc view plays. Nothing is downsampled, filtered or cached. No
// Arduino dependencies.
namespace MiniRecord {

  // Unscaled sizes in screen px (the mock's 72 px record at 1.875x).
  constexpr float RADIUS       = 67.5f;
  constexpr float LABEL_RADIUS = 43.125f;

  struct Spec {
    float   cx, cy;   // centre on screen
    float   scale;    // 1 = full size (the selected playlist), 0.7 = neighbours
    uint8_t alpha;    // whole record, shadow included
    uint8_t art;      // ALBUMS[] index for the label
  };

  // Builds the lookup tables. Returns the bytes allocated (for the heap
  // print in setup(); always 0, kept for that call site).
  uint32_t init();

  // Draws one record into one band.
  void draw(const UiDraw::Band &b, const Spec &s);

  // Screen rows the record (with its shadow) can touch.
  void rowSpan(const Spec &s, int &y0, int &y1);

}
