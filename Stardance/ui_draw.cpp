// Hot per-pixel loops: built with -O2 whatever the IDE's "Optimize" menu
// says (its default -Os spills registers in them and runs about 2x slower).
#pragma GCC optimize("O2")

#include "ui_draw.h"
#include "config.h"

#include <math.h>
#include <string.h>

namespace UiDraw {

namespace {

  inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

  // Pixel coverage from a signed distance (px, negative inside): a 1 px box
  // filter across the edge.
  inline float coverage(float d) { return clamp01(0.5f - d); }

  // Plots a pixel with coverage cov (0..1) of colour col at alpha a.
  inline void plot(uint16_t *p, float cov, uint16_t col, uint8_t a) {
    const int k = (int)(cov * a + 0.5f);
    if (k > 0) *p = blend(*p, col, (uint8_t)k);
  }

  // Rows of the band that the box y0..y1 touches, clipped.
  inline bool rowRange(const Band &b, float y0, float y1, int &r0, int &r1) {
    r0 = (int)floorf(y0);
    r1 = (int)ceilf(y1);
    if (r0 < b.top) r0 = b.top;
    if (r1 > b.bot) r1 = b.bot;
    return r0 <= r1;
  }

  inline void colRange(float x0, float x1, int &c0, int &c1) {
    c0 = (int)floorf(x0);
    c1 = (int)ceilf(x1);
    if (c0 < 0) c0 = 0;
    if (c1 > SCREEN_W - 1) c1 = SCREEN_W - 1;
  }

  // RGB565 spread into "lanes" (G in bits 21..26, R in 11..15, B in 0..4) so
  // one multiply blends all three channels with a 5-bit alpha.
  constexpr uint32_t LANES = 0x07E0F81Fu;
  inline uint32_t spread(uint16_t c) { return (c | ((uint32_t)c << 16)) & LANES; }
  inline uint16_t fold(uint32_t v)   { v &= LANES; return (uint16_t)(v | (v >> 16)); }

  const UiGlyph &glyphOf(const UiFont &f, char ch) {
    unsigned c = (unsigned char)ch;
    if (c < UI_FONT_FIRST || c > UI_FONT_LAST) c = '?';
    return f.glyphs[c - UI_FONT_FIRST];
  }

  // One pass of a string's glyphs at alpha a, shifted (ox, oy) px.
  void glyphPass(const Band &b, const UiFont &f, const char *s, int32_t x16, int baseline,
                 int ox, int oy, uint16_t col, uint8_t a, int clipX0, int clipX1) {
    // Glyph level (0..15) x alpha -> 5-bit blend weight.
    uint8_t lut[16];
    for (int i = 0; i < 16; i++) lut[i] = (uint8_t)((i * a * 32 + 15 * 128) / (15 * 255));
    const uint32_t fg = spread(col);

    int32_t pen = x16;
    for (const char *p = s; *p; p++) {
      const UiGlyph &g = glyphOf(f, *p);
      const int gx = ((pen + 8) >> 4) + g.dx + ox;
      const int gy = baseline + g.dy + oy;
      pen += g.adv16;
      if (!g.w || gy > b.bot || gy + g.h <= b.top) continue;
      if (gx > clipX1 || gx + g.w <= clipX0) continue;

      const int rowBytes = (g.w + 1) >> 1;
      const uint8_t *bits = f.bitmap + g.offset;
      int y0 = gy, y1 = gy + g.h - 1;
      if (y0 < b.top) y0 = b.top;
      if (y1 > b.bot) y1 = b.bot;
      int x0 = gx, x1 = gx + g.w - 1;
      if (x0 < clipX0) x0 = clipX0;
      if (x1 > clipX1) x1 = clipX1;
      if (x0 < 0) x0 = 0;
      if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;

      for (int y = y0; y <= y1; y++) {
        const uint8_t *row = bits + (y - gy) * rowBytes;
        uint16_t *out = b.px + (y - b.top) * SCREEN_W;
        for (int x = x0; x <= x1; x++) {
          const int i = x - gx;
          const uint8_t v = (i & 1) ? (row[i >> 1] & 0x0F) : (row[i >> 1] >> 4);
          if (!v) continue;
          const uint32_t w = lut[v];
          const uint32_t bg = spread(out[x]);
          out[x] = fold(bg + (((fg - bg) * w) >> 5));
        }
      }
    }
  }

}

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t blend(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a == 0)   return bg;
  if (a == 255) return fg;
  const uint32_t b = spread(bg);
  return fold(b + (((spread(fg) - b) * (uint32_t)((a + 4) >> 3)) >> 5));
}

void darken(const Band &b, int y0, int y1, uint16_t keep) {
  if (y0 < b.top) y0 = b.top;
  if (y1 > b.bot) y1 = b.bot;
  for (int y = y0; y <= y1; y++) {
    uint16_t *p = b.px + (y - b.top) * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x++) {
      const uint16_t c = p[x];
      if (!c) continue;
      const int r = ((c >> 11) * keep + 128) >> 8;
      const int g = (((c >> 5) & 0x3F) * keep + 128) >> 8;
      const int l = ((c & 0x1F) * keep + 128) >> 8;
      p[x] = (uint16_t)((r << 11) | (g << 5) | l);
    }
  }
}

void fillCircle(const Band &b, float cx, float cy, float r, uint16_t col, uint8_t a) {
  int r0, r1, c0, c1;
  if (!rowRange(b, cy - r - 1, cy + r + 1, r0, r1)) return;
  colRange(cx - r - 1, cx + r + 1, c0, c1);
  // Q4 coordinates; distance from the edge as (d^2 - r^2) / 2r.
  const int32_t CX = (int32_t)lroundf(cx * 16.0f), CY = (int32_t)lroundf(cy * 16.0f);
  const int32_t RR = (int32_t)lroundf(r * r * 256.0f);
  const int32_t inv2r = (int32_t)lroundf(65536.0f / (2.0f * r));
  for (int y = r0; y <= r1; y++) {
    uint16_t *row = b.px + (y - b.top) * SCREEN_W;
    const int32_t dy = y * 16 + 8 - CY;
    for (int x = c0; x <= c1; x++) {
      const int32_t dx = x * 16 + 8 - CX;
      const int32_t dq = (int32_t)(((int64_t)(dx * dx + dy * dy - RR) * inv2r) >> 16);
      int cov = 128 - dq;
      if (cov <= 0) continue;
      if (cov > 256) cov = 256;
      row[x] = blend(row[x], col, (uint8_t)((cov * a) >> 8));
    }
  }
}

void fillRoundRect(const Band &b, float x0, float y0, float x1, float y1, float r,
                   uint16_t col, uint8_t a) {
  int r0, r1, c0, c1;
  if (!rowRange(b, y0 - 1, y1 + 1, r0, r1)) return;
  colRange(x0 - 1, x1 + 1, c0, c1);
  // Signed distance to a rounded box, Q4 coordinates.
  const int32_t CX = (int32_t)lroundf((x0 + x1) * 8.0f), CY = (int32_t)lroundf((y0 + y1) * 8.0f);
  const int32_t HX = (int32_t)lroundf(((x1 - x0) * 0.5f - r) * 16.0f);
  const int32_t HY = (int32_t)lroundf(((y1 - y0) * 0.5f - r) * 16.0f);
  const int32_t RQ = (int32_t)lroundf(r * 16.0f);
  for (int y = r0; y <= r1; y++) {
    uint16_t *row = b.px + (y - b.top) * SCREEN_W;
    int32_t qy = y * 16 + 8 - CY;
    qy = (qy < 0 ? -qy : qy) - HY;
    for (int x = c0; x <= c1; x++) {
      int32_t qx = x * 16 + 8 - CX;
      qx = (qx < 0 ? -qx : qx) - HX;
      int32_t d;                                    // Q4
      if (qx <= 0 && qy <= 0)  d = (qx > qy ? qx : qy) - RQ;
      else if (qx <= 0)        d = qy - RQ;
      else if (qy <= 0)        d = qx - RQ;
      else                     d = (int32_t)sqrtf((float)(qx * qx + qy * qy)) - RQ;
      int cov = 128 - d * 16;
      if (cov <= 0) continue;
      if (cov > 256) cov = 256;
      row[x] = blend(row[x], col, (uint8_t)((cov * a) >> 8));
    }
  }
}

void strokeLine(const Band &b, float x0, float y0, float x1, float y1, float w,
                uint16_t col, uint8_t a) {
  const float hw = w * 0.5f;
  int r0, r1, c0, c1;
  if (!rowRange(b, fminf(y0, y1) - hw - 1, fmaxf(y0, y1) + hw + 1, r0, r1)) return;
  colRange(fminf(x0, x1) - hw - 1, fmaxf(x0, x1) + hw + 1, c0, c1);
  // Integer per pixel, Q4 coordinates: distance to the segment from the
  // projection (dot) and the perpendicular (cross) product.
  const int32_t AX = (int32_t)lroundf(x0 * 16.0f), AY = (int32_t)lroundf(y0 * 16.0f);
  const int32_t EX = (int32_t)lroundf((x1 - x0) * 16.0f), EY = (int32_t)lroundf((y1 - y0) * 16.0f);
  const int32_t len2 = EX * EX + EY * EY;                                   // Q8
  const int32_t hwq = (int32_t)lroundf(hw * 256.0f);
  for (int y = r0; y <= r1; y++) {
    uint16_t *row = b.px + (y - b.top) * SCREEN_W;
    const int32_t PY = y * 16 + 8 - AY;
    for (int x = c0; x <= c1; x++) {
      const int32_t PX = x * 16 + 8 - AX;
      const int32_t dot = PX * EX + PY * EY;
      uint32_t d2;                                                          // Q8 px^2
      if (dot <= 0 || !len2) d2 = (uint32_t)(PX * PX + PY * PY);
      else if (dot >= len2)  d2 = (uint32_t)((PX - EX) * (PX - EX) + (PY - EY) * (PY - EY));
      else {
        const int64_t cr = (int64_t)PX * EY - (int64_t)PY * EX;
        d2 = (uint32_t)((cr * cr) / len2);
      }
      const int32_t dq = (int32_t)(sqrtf((float)d2) * 16.0f) - hwq;        // Q8
      int cov = 128 - dq;
      if (cov <= 0) continue;
      if (cov > 256) cov = 256;
      row[x] = blend(row[x], col, (uint8_t)((cov * a) >> 8));
    }
  }
}

void strokeArc(const Band &b, float cx, float cy, float R, float a0, float a1, float w,
               uint16_t col, uint8_t a) {
  const float hw = w * 0.5f;
  const float rad = 3.14159265f / 180.0f;
  // Endpoint directions (y grows downwards, so "up" is (0, -1)).
  const float e0x = sinf(a0 * rad), e0y = -cosf(a0 * rad);
  const float e1x = sinf(a1 * rad), e1y = -cosf(a1 * rad);
  const float E0x = cx + R * e0x, E0y = cy + R * e0y;
  const float E1x = cx + R * e1x, E1y = cy + R * e1y;

  // Bounding box: endpoints, plus the extremes of the circle the arc passes.
  float ymin = fminf(E0y, E1y), ymax = fmaxf(E0y, E1y);
  float xmin = fminf(E0x, E1x), xmax = fmaxf(E0x, E1x);
  auto passes = [&](float t) { return (a0 < t && a1 > t) || (a0 < t + 360.0f && a1 > t + 360.0f); };
  if (passes(0.0f))   ymin = cy - R;
  if (passes(90.0f))  xmax = cx + R;
  if (passes(180.0f)) ymax = cy + R;
  if (passes(270.0f)) xmin = cx - R;
  int r0, r1;
  if (!rowRange(b, ymin - hw - 1, ymax + hw + 1, r0, r1)) return;

  // Integer per pixel, Q4 coordinates: distance from the circle as
  // (r^2 - R^2) / 2R, the wedge test as two cross products. Only pixels past
  // either end (the round caps) take a square root.
  const int32_t CXq = (int32_t)lroundf(cx * 16.0f), CYq = (int32_t)lroundf(cy * 16.0f);
  const int32_t RRq = (int32_t)lroundf(R * R * 256.0f);
  const int32_t invTwoR = (int32_t)lroundf(65536.0f / (2.0f * R));        // Q16
  const int32_t E0X = (int32_t)lroundf(e0x * 4096.0f), E0Y = (int32_t)lroundf(e0y * 4096.0f);
  const int32_t E1X = (int32_t)lroundf(e1x * 4096.0f), E1Y = (int32_t)lroundf(e1y * 4096.0f);
  const int32_t hwq = (int32_t)lroundf(hw * 256.0f);
  const float Ro = R + hw + 1.0f, Ri = R - hw - 1.0f;

  for (int y = r0; y <= r1; y++) {
    uint16_t *row = b.px + (y - b.top) * SCREEN_W;
    const float dy = y + 0.5f - cy;
    if (fabsf(dy) > Ro) continue;
    const float xo = sqrtf(Ro * Ro - dy * dy);
    const float xi = (fabsf(dy) < Ri) ? sqrtf(Ri * Ri - dy * dy) : 0.0f;
    const int32_t dyq = (int32_t)(y * 16 + 8) - CYq;
    for (int side = 0; side < 2; side++) {
      float s0 = side ? cx + xi : cx - xo, s1 = side ? cx + xo : cx - xi;
      if (s0 < xmin - hw - 1) s0 = xmin - hw - 1;
      if (s1 > xmax + hw + 1) s1 = xmax + hw + 1;
      if (s0 > s1) continue;
      int c0, c1;
      colRange(s0, s1, c0, c1);
      for (int x = c0; x <= c1; x++) {
        const int32_t dxq = (int32_t)(x * 16 + 8) - CXq;
        const int32_t r2 = dxq * dxq + dyq * dyq;                          // Q8 px^2
        const bool in0 = (E0X * dyq - E0Y * dxq) >= 0;    // clockwise of the start
        const bool in1 = (dxq * E1Y - dyq * E1X) >= 0;    // anticlockwise of the end
        int32_t dq;                                        // distance from the stroke edge, Q8
        if (in0 && in1) {
          int32_t d = (int32_t)(((int64_t)(r2 - RRq) * invTwoR) >> 16);
          if (d < 0) d = -d;
          dq = d - hwq;
        } else {
          const float px = x + 0.5f, py = y + 0.5f;
          const float d0 = sqrtf((px - E0x) * (px - E0x) + (py - E0y) * (py - E0y));
          const float d1 = sqrtf((px - E1x) * (px - E1x) + (py - E1y) * (py - E1y));
          dq = (int32_t)((fminf(d0, d1) - hw) * 256.0f);
        }
        int cov = 128 - dq;
        if (cov <= 0) continue;
        if (cov > 256) cov = 256;
        row[x] = blend(row[x], col, (uint8_t)((cov * a) >> 8));
      }
    }
  }
}

int32_t textWidth16(const UiFont &f, const char *s) {
  int32_t w = 0;
  for (const char *p = s; *p; p++) w += glyphOf(f, *p).adv16;
  return w;
}

void fitEllipsis(const UiFont &f, char *buf, size_t cap, int32_t maxW16) {
  if (textWidth16(f, buf) <= maxW16) return;
  const int32_t ell = f.glyphs[UI_FONT_ELLIPSIS - UI_FONT_FIRST].adv16;
  size_t len = strlen(buf);
  int32_t w = textWidth16(f, buf);
  while (len > 0 && w + ell > maxW16) {
    len--;
    w -= glyphOf(f, buf[len]).adv16;
  }
  while (len > 0 && buf[len - 1] == ' ') len--;     // no "Word …"
  if (len + 2 > cap) len = cap - 2;
  buf[len]     = (char)UI_FONT_ELLIPSIS;
  buf[len + 1] = '\0';
}

void drawText(const Band &b, const UiFont &f, const char *s, int32_t x16, int baseline,
              uint16_t col, uint8_t a, int clipX0, int clipX1, bool shadow) {
  if (!a || !*s) return;
  if (baseline - f.ascent - 4 > b.bot || baseline + f.descent + 4 < b.top) return;
  if (shadow) {
    // A blur is too slow here; three offset taps of the glyph itself land
    // close to the mock's soft shadow 2 px below.
    struct Tap { int8_t dx, dy; uint8_t a; };
    static const Tap TAPS[] = { { 0, 1, 80 }, { 0, 2, 110 }, { 0, 3, 60 } };
    for (const Tap &t : TAPS)
      glyphPass(b, f, s, x16, baseline, t.dx, t.dy, 0x0000, (uint8_t)((t.a * a) >> 8), clipX0, clipX1);
  }
  glyphPass(b, f, s, x16, baseline, 0, 0, col, a, clipX0, clipX1);
}

}
