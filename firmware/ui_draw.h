#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ui_fonts.h"

// Small anti-aliased drawing kit for the chooser. Everything draws into one
// band of the screen at a time (a SCREEN_W-wide slice of
// Display::bandBuffer()), clipped to that band, in plain RGB565. No Arduino
// dependencies.
//
// Angles are in degrees, clockwise from straight up, the way the mock's
// SVG helpers measure them (0 = top, 90 = right, 180 = bottom, 270 = left).
// Alphas are 0..255.
namespace UiDraw {

  struct Band {
    uint16_t *px;    // SCREEN_W pixels per row
    int       top;   // screen row of px[0]
    int       bot;   // last screen row in the band (inclusive)
  };

  uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);
  uint16_t blend(uint16_t bg, uint16_t fg, uint8_t a);

  // Multiplies rows y0..y1 by keep/256 (the mock's translucent black bars).
  void darken(const Band &b, int y0, int y1, uint16_t keep);

  void fillCircle(const Band &b, float cx, float cy, float r, uint16_t col, uint8_t a);
  void fillRoundRect(const Band &b, float x0, float y0, float x1, float y1, float r,
                     uint16_t col, uint8_t a);
  // Round-capped strokes, like SVG stroke-linecap="round".
  void strokeLine(const Band &b, float x0, float y0, float x1, float y1, float w,
                  uint16_t col, uint8_t a);
  // Arc of radius R around (cx, cy) from a0 clockwise to a1 (a1 > a0, at
  // most 180 degrees apart).
  void strokeArc(const Band &b, float cx, float cy, float R, float a0, float a1, float w,
                 uint16_t col, uint8_t a);

  // Text. Widths and pen positions are in 1/16 px.
  int32_t textWidth16(const UiFont &f, const char *s);
  // Shortens buf with a trailing ellipsis until it is at most maxW16 wide
  // (CSS text-overflow: ellipsis). Leaves it alone if it already fits.
  void fitEllipsis(const UiFont &f, char *buf, size_t cap, int32_t maxW16);
  // Draws s with its pen starting at x16 on `baseline`, clipped to columns
  // clipX0..clipX1. shadow adds the mock's soft dark drop shadow
  // (text-shadow 0 1px 2px rgba(0,0,0,.7) at 1.875x).
  void drawText(const Band &b, const UiFont &f, const char *s, int32_t x16, int baseline,
                uint16_t col, uint8_t a, int clipX0, int clipX1, bool shadow);

}
