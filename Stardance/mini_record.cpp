// Hot per-pixel loops: built with -O2 whatever the IDE's "Optimize" menu
// says (its default -Os spills registers in them and runs about 2x slower).
#pragma GCC optimize("O2")

#include "mini_record.h"
#include "albums.h"
#include "config.h"

#include <math.h>

namespace MiniRecord {

namespace {

  // ---- Geometry, in unscaled screen px (mock px x 1.875) -------------------
  constexpr float SPINDLE_R  = 3.75f;      // the 4 px hole
  constexpr float RING_W     = 0.9375f;    // the mock's 0.5 px box-shadow rings
  constexpr float SHADOW_DY  = 7.5f;       // box-shadow 0 4px 10px rgba(0,0,0,.6)
  constexpr float SHADOW_SIG = 9.375f;     //   blur 10 px = gaussian sigma 5 px
  constexpr float SHADOW_A   = 0.6f;
  constexpr float SHADOW_EXT = 2.2f * SHADOW_SIG;  // past this the shadow is under 1%

  // Grooves: repeating-radial-gradient(transparent 0 1.1px, rgba(255,255,255,.07)
  // 1.4px, transparent 1.8px), i.e. a faint ramp every 1.8 mock px.
  constexpr float GROOVE_PERIOD = 1.8f * 1.875f;
  constexpr float GROOVE_START  = 1.1f * 1.875f;
  constexpr float GROOVE_PEAK   = 1.4f * 1.875f;
  constexpr float GROOVE_A      = 0.07f;

  // Base colour #0b0a0a.
  constexpr int BASE_R = 11, BASE_G = 10, BASE_B = 10;

  // ---- Label geometry --------------------------------------------------
  // An 86 x 86 grid centred on the label (pixel j covers u in [j-43, j-42)),
  // used only to clamp the anti-aliased edge to pixels the circle actually
  // covers; the label itself is sampled straight from the art, not stored.
  constexpr int LAB   = 86;
  constexpr int LAB_C = 43;
  uint8_t  rowX0[LAB], rowLen[LAB];
  uint8_t  artPos[LAB];         // label column / row -> art pixel

  // ---- Lookup tables -------------------------------------------------------
  // Groove alpha by squared distance, in 4 px^2 steps (0.03 px at the
  // record's rim), x 2048 (so .07 = 143). Pre-filtered over a pixel's width
  // so the fine ramps do not alias.
  constexpr int GROOVE_SHIFT = 18;    // Q16 px^2 -> 4 px^2
  constexpr int GROOVE_STEP  = 4;
  constexpr int GROOVE_SIZE  = (int)((RADIUS + 2) * (RADIUS + 2) / GROOVE_STEP) + 2;
  uint8_t grooveLut[GROOVE_SIZE];
  // Glare alpha by angle (256 steps per turn), x 1024 (so .14 = 143).
  uint8_t glareLut[256];
  // atan(t / 256) in 1/256 turns, t = 0..256.
  uint8_t atanLut[257];
  // Shadow alpha by squared distance from the shadow's centre, 16 px^2 steps, 0..255.
  constexpr int SHADOW_SHIFT = 20;
  constexpr int SHADOW_STEP  = 16;
  constexpr int SHADOW_SIZE  = (int)((RADIUS + SHADOW_EXT) * (RADIUS + SHADOW_EXT) / SHADOW_STEP) + 2;
  uint8_t shadowLut[SHADOW_SIZE];

  const uint8_t BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

  // Normal CDF, close enough for a shadow: the logistic 1 / (1 + e^-1.702x),
  // with e^z as (1 + z/1024)^1024. Not erff() / expf(): the core links those
  // into RAM, about 3.6 KB of it, for a table built once at boot.
  float phi(float x) {
    float e = 1.0f - 1.702f * x / 1024.0f;
    if (e < 0.0f) e = 0.0f;
    for (int i = 0; i < 10; i++) e *= e;
    return 1.0f / (1.0f + e);
  }

  float grooveAt(float r) {
    const float p = fmodf(r, GROOVE_PERIOD);
    if (p < GROOVE_START) return 0.0f;
    if (p < GROOVE_PEAK)  return GROOVE_A * (p - GROOVE_START) / (GROOVE_PEAK - GROOVE_START);
    return GROOVE_A * (GROOVE_PERIOD - p) / (GROOVE_PERIOD - GROOVE_PEAK);
  }

  // conic-gradient(from 30deg, 0 0deg, .14 25deg, 0 55deg, 0 180deg, .1 205deg, 0 235deg)
  float glareAt(float deg) {
    float a = fmodf(deg - 30.0f + 360.0f, 360.0f);
    if (a < 25.0f)  return 0.14f * a / 25.0f;
    if (a < 55.0f)  return 0.14f * (55.0f - a) / 30.0f;
    if (a < 180.0f) return 0.0f;
    if (a < 205.0f) return 0.10f * (a - 180.0f) / 25.0f;
    if (a < 235.0f) return 0.10f * (235.0f - a) / 30.0f;
    return 0.0f;
  }

  // Angle of (x, y) clockwise from straight up (y grows downwards), 0..255.
  inline uint8_t angle256(int32_t x, int32_t y) {
    const int32_t n = -y;                        // "north" component
    const uint32_t ax = (uint32_t)(x < 0 ? -x : x);
    const uint32_t an = (uint32_t)(n < 0 ? -n : n);
    uint32_t a;
    if (ax <= an) a = an ? atanLut[(ax << 8) / an] : 0;
    else          a = 64 - atanLut[(an << 8) / ax];
    if (n >= 0) return (uint8_t)(x >= 0 ? a : 256 - a);
    return (uint8_t)(x >= 0 ? 128 - a : 128 + a);
  }

  struct Rgb { int r, g, b; };

  inline Rgb unpack(uint16_t c) { return { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 }; }
  inline Rgb unpack8(uint16_t c) {
    const int r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return { (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2) };
  }
  inline Rgb mix(Rgb a, Rgb b, int t /*0..256*/) {
    return { a.r + (((b.r - a.r) * t) >> 8), a.g + (((b.g - a.g) * t) >> 8), a.b + (((b.b - a.b) * t) >> 8) };
  }
  // Dithered pack. A colour that came from unpack() packs back unchanged.
  inline uint16_t pack(Rgb c, int bay) {
    int r = (c.r + (bay >> 1)) >> 3, g = (c.g + (bay >> 2)) >> 2, b = (c.b + (bay >> 1)) >> 3;
    if (r > 31) r = 31; if (r < 0) r = 0;
    if (g > 63) g = 63; if (g < 0) g = 0;
    if (b > 31) b = 31; if (b < 0) b = 0;
    return (uint16_t)((r << 11) | (g << 5) | b);
  }

  // RGB565 in "lanes" (G in bits 21..26, R in 11..15, B in 0..4), so one
  // multiply scales all three channels: a 5-bit alpha (0..32) blend or
  // darken costs a handful of instructions.
  constexpr uint32_t LANES = 0x07E0F81Fu;
  inline uint32_t spread(uint16_t c) { return (c | ((uint32_t)c << 16)) & LANES; }
  inline uint16_t fold(uint32_t v)   { v &= LANES; return (uint16_t)(v | (v >> 16)); }
  inline uint16_t blend5(uint16_t bg, uint16_t fg, uint32_t a5) {
    const uint32_t b = spread(bg);
    return fold(b + (((spread(fg) - b) * a5) >> 5));
  }
  inline uint16_t darken5(uint16_t c, uint32_t keep5) { return fold((spread(c) * keep5) >> 5); }

  // The disc surface is grey: #0b0a0a plus a white overlay (grooves + glare).
  // Returned as 8.2 fixed point. `glare` says whether (uq, vq) may be inside a
  // glare wedge; the angle (a division) is only worked out when it is.
  inline int surfaceGrey(int32_t uq, int32_t vq, uint32_t d2, bool glare) {
#if UI_RECORD_GROOVES
    uint32_t gi = d2 >> GROOVE_SHIFT;
    if (gi >= (uint32_t)GROOVE_SIZE) gi = GROOVE_SIZE - 1;
    int w = grooveLut[gi] * 2;                                    // Q12 white
    if (glare) w += glareLut[angle256(uq, vq)] * 4;
    return BASE_G * 4 + (((1020 - BASE_G * 4) * w) >> 12);
#else
    (void)uq; (void)vq; (void)d2; (void)glare;
    return BASE_G * 4;
#endif
  }
  // The glare wedges sit in the upper-right and lower-left quarters only.
  inline bool inGlareQuarter(int32_t uq, int32_t vq) { return uq && vq && ((uq > 0) == (vq < 0)); }
  inline Rgb greyRgb(int g4) { return { g4 >> 2, g4 >> 2, g4 >> 2 }; }

  // Grey 8.2 to RGB565 with this pixel's ordered dither (as in backdrop.cpp).
  inline uint16_t packGrey(int g4, uint32_t dith) {
    const uint32_t v = (uint32_t)g4 * 0x00400801u + dith;
    return (uint16_t)(((v >> 16) & 0xF800) | ((v >> 10) & 0x07E0) | ((v >> 5) & 0x001F));
  }

  // Coverage (0..256) of a pixel whose centre is dq (screen px, Q8) outside
  // an edge: of the inside, and of a band [0, wq] just outside it.
  inline int insideCov(int dq) { int c = 128 - dq; return c < 0 ? 0 : (c > 256 ? 256 : c); }
  inline int bandCov(int dq, int wq) {
    int lo = dq - 128, hi = dq + 128;
    if (lo < 0)  lo = 0;
    if (hi > wq) hi = wq;
    return hi > lo ? hi - lo : 0;
  }

  uint32_t isqrt32(uint32_t v) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
      if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
      else              r >>= 1;
      bit >>= 2;
    }
    return r;
  }

  inline uint32_t sq16(float r) { return r <= 0 ? 0 : (uint32_t)(r * r * 65536.0f); }

  // Square root of a fixed-point square (a few per row), via the float root.
  inline int32_t isqrtf(uint32_t v) { return (int32_t)sqrtf((float)v); }

  // Half-width of the round glass per row (px, rounded up), for clipping.
  uint8_t glassHalf[SCREEN_H];

}

uint32_t init() {
  // Label rows: keep the pixels whose centre is inside a radius a little over
  // the label's, so the anti-aliased edge clamps to pixels the circle covers.
  const float rr = (LABEL_RADIUS + 1.0f) * (LABEL_RADIUS + 1.0f);
  for (int k = 0; k < LAB; k++) {
    const float y = k + 0.5f - LAB_C;
    int x0 = LAB, x1 = -1;
    for (int j = 0; j < LAB; j++) {
      const float x = j + 0.5f - LAB_C;
      if (x * x + y * y <= rr) { if (j < x0) x0 = j; x1 = j; }
    }
    if (x1 < x0) { x0 = LAB_C; x1 = LAB_C; }
    rowX0[k] = (uint8_t)x0; rowLen[k] = (uint8_t)(x1 - x0 + 1);
  }

  // The label shows the whole cover. The stored art is valid out to DISC_R
  // (build_albums.py blacks out the rest), so the label's rim maps to DISC_R.
  const float f = DISC_R / LABEL_RADIUS;
  for (int j = 0; j < LAB; j++) {
    int a = (int)floorf(DISC_CX + (j + 0.5f - LAB_C) * f) - 1;
    if (a < 0) a = 0;
    if (a > ART_W - 3) a = ART_W - 3;
    artPos[j] = (uint8_t)a;
  }

  for (int i = 0; i < GROOVE_SIZE; i++) {
    const float r = sqrtf((float)(GROOVE_STEP * i + GROOVE_STEP / 2));
    float s = 0.0f;
    for (int k = 0; k < 16; k++) s += grooveAt(r - 0.5f + (k + 0.5f) / 16.0f);
    grooveLut[i] = (uint8_t)lroundf(s / 16.0f * 2048.0f);
  }
  for (int i = 0; i < 256; i++) {
    float s = 0.0f;
    for (int k = 0; k < 4; k++) s += glareAt((i + k / 4.0f) * 360.0f / 256.0f);
    glareLut[i] = (uint8_t)lroundf(s / 4.0f * 1024.0f);
  }
  for (int y = 0; y < SCREEN_H; y++) {
    const float g = SCREEN_W / 2.0f, dy = y + 0.5f - g;
    glassHalf[y] = (uint8_t)ceilf(sqrtf(fmaxf(g * g - dy * dy, 0.0f)) + 1.0f);
  }
  for (int t = 0; t <= 256; t++)
    atanLut[t] = (uint8_t)lroundf(atanf(t / 256.0f) * 128.0f / 3.14159265f);
  for (int i = 0; i < SHADOW_SIZE; i++) {
    const float r = sqrtf((float)(SHADOW_STEP * i + SHADOW_STEP / 2));
    const float a = SHADOW_A * phi((RADIUS - r) / SHADOW_SIG);
    shadowLut[i] = (uint8_t)lroundf(a * 255.0f);
  }

  return 0;
}

void rowSpan(const Spec &s, int &y0, int &y1) {
  const float top = s.cy - (RADIUS + RING_W + 1.0f) * s.scale;
  float bot = s.cy + (RADIUS + RING_W + 1.0f) * s.scale;
#if UI_RECORD_SHADOW
  const float sb = s.cy + (SHADOW_DY + RADIUS + SHADOW_EXT) * s.scale;
  if (sb > bot) bot = sb;
#endif
  y0 = (int)floorf(top);
  y1 = (int)ceilf(bot);
}

void draw(const UiDraw::Band &b, const Spec &s) {
  if (!s.alpha || s.scale <= 0.05f) return;
  int y0, y1;
  rowSpan(s, y0, y1);
  if (y0 < b.top) y0 = b.top;
  if (y1 > b.bot) y1 = b.bot;
  if (y0 > y1) return;

  // ---- Per record (floats are fine here, once) -------------------------------
  const AlbumArt &art = ALBUMS[s.art];
  const float sc = s.scale, inv = 1.0f / sc;
  const float hp = 0.5f * inv;                  // half a screen pixel, unscaled
  const int   alpha = s.alpha + (s.alpha >> 7); // 0..256
  const uint32_t a5 = (uint32_t)(alpha + 4) >> 3;

  // Zone limits on d2 = u^2 + v^2 (unscaled px, Q16), and the same radii in Q8.
  const uint32_t Z_SPIN   = sq16(SPINDLE_R + RING_W + hp);
  const uint32_t Z_LABIN  = sq16(LABEL_RADIUS - hp);
  const uint32_t Z_LABOUT = sq16(LABEL_RADIUS + RING_W + hp);
  const uint32_t Z_RECIN  = sq16(RADIUS - hp);
  const uint32_t Z_RECOUT = sq16(RADIUS + RING_W + hp);
  const int32_t  R_SPIN   = (int32_t)((SPINDLE_R + RING_W + hp) * 256.0f) + 1;
  const int32_t  R_LABIN  = (int32_t)((LABEL_RADIUS - hp) * 256.0f);
  const int32_t  R_LABOUT = (int32_t)((LABEL_RADIUS + RING_W + hp) * 256.0f) + 1;
  const int32_t  R_RECIN  = (int32_t)((RADIUS - hp) * 256.0f);
  const int32_t  R_RECOUT = (int32_t)((RADIUS + RING_W + hp) * 256.0f) + 1;

  // Distance outside an edge of radius R in screen px (Q8), from d2:
  // (rho^2 - R^2) / 2R, times the scale. Exact enough within a few px of it.
  const int32_t RR_REC = (int32_t)(sq16(RADIUS) >> 8), RR_LAB = (int32_t)(sq16(LABEL_RADIUS) >> 8);
  const int32_t K_REC  = (int32_t)(sc * 65536.0f / (2.0f * RADIUS));
  const int32_t K_LAB  = (int32_t)(sc * 65536.0f / (2.0f * LABEL_RADIUS));
  const int32_t SC_Q8  = (int32_t)(sc * 256.0f);
  const int ringQ      = (int)(RING_W * sc * 256.0f);
  const int spinQ      = (int)(SPINDLE_R * 256.0f);

  // Screen geometry in Q8.
  const int32_t cxQ  = (int32_t)lroundf(s.cx * 256.0f);
  const int32_t cyQ  = (int32_t)lroundf(s.cy * 256.0f);
  const int32_t du   = (int32_t)lroundf(inv * 256.0f);       // unscaled Q8 per screen px
  const int32_t rRec = (int32_t)lroundf((RADIUS + RING_W + 1.0f) * sc * 256.0f);
#if UI_RECORD_SHADOW
  const int32_t rSh  = (int32_t)lroundf((RADIUS + SHADOW_EXT) * sc * 256.0f);
  const int32_t cySQ = cyQ + (int32_t)lroundf(SHADOW_DY * sc * 256.0f);
#endif
  constexpr int32_t GQ = (SCREEN_W / 2) * 256;

  for (int y = y0; y <= y1; y++) {
    uint16_t *row = b.px + (y - b.top) * SCREEN_W;
    const int32_t pyQ = y * 256 + 128;

    // Columns: the record's circle, widened to the shadow's, cut to the glass
    // (nothing outside it is ever pushed).
    int32_t half = 0;
    const int32_t dyr = pyQ - cyQ;
    if (dyr > -rRec && dyr < rRec) half = isqrtf((uint32_t)(rRec * rRec - dyr * dyr));
#if UI_RECORD_SHADOW
    const int32_t dys = pyQ - cySQ;
    if (dys > -rSh && dys < rSh) { const int32_t h = isqrtf((uint32_t)(rSh * rSh - dys * dys)); if (h > half) half = h; }
#endif
    if (!half) continue;
    const int32_t gh = glassHalf[y] * 256;
    int x0 = ((cxQ - half > GQ - gh) ? cxQ - half : GQ - gh) >> 8;
    int x1 = (((cxQ + half < GQ + gh) ? cxQ + half : GQ + gh) + 255) >> 8;
    if (x0 < 0) x0 = 0;
    if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
    if (x0 > x1) continue;

    const int32_t vq  = (dyr * du) >> 8;           // this row, unscaled Q8
    const uint32_t v2 = (uint32_t)(vq * vq);
    const int32_t avq = vq < 0 ? -vq : vq;
#if UI_RECORD_SHADOW
    const int32_t vsq  = (dys * du) >> 8;
    const uint32_t vs2 = (uint32_t)(vsq * vsq);
#endif
    // This row of the label.
    int k = (vq + LAB_C * 256) >> 8;
    if (k < 0) k = 0;
    if (k > LAB - 1) k = LAB - 1;
    const int jx0 = rowX0[k], jx1 = rowX0[k] + rowLen[k] - 1;
    const uint8_t  *artRow = art.pixels + (artPos[k] + 1) * ART_W + 1;

    const uint8_t *bay = BAYER + (y & 3) * 4;
    uint32_t dith[4];
    for (int q = 0; q < 4; q++) {
      const uint32_t bv = bay[q];
      dith[q] = ((2 * bv) << 22) | (bv << 11) | (2 * bv);
    }

    const int32_t uq0 = (((x0 * 256 + 128) - cxQ) * du) >> 8;
    auto uAt = [&](int x) -> int32_t { return uq0 + (x - x0) * du; };

    // Columns strictly inside unscaled radius Rz on this row (1 px margin),
    // and columns that may be inside it (1 px margin the other way).
    auto inner = [&](int32_t Rz, int &a, int &c) {
      a = 1; c = 0;
      if (avq >= Rz) return;
      const int32_t hs = (isqrtf((uint32_t)(Rz * Rz) - v2) * SC_Q8) >> 8;
      a = ((cxQ - hs) >> 8) + 1;
      c = ((cxQ + hs) >> 8) - 1;
    };
    auto outer = [&](int32_t Rz, int &a, int &c) {
      a = 1; c = 0;
      if (avq > Rz) return;
      const int32_t hs = (isqrtf((uint32_t)(Rz * Rz) - v2) * SC_Q8) >> 8;
      a = ((cxQ - hs) >> 8) - 1;
      c = ((cxQ + hs) >> 8) + 1;
    };
    auto clampRun = [&](int &a, int &c) { if (a < x0) a = x0; if (c > x1) c = x1; return a <= c; };

    // ---- Runs: one kind of pixel each, so the loops stay short ----------------
    auto shadowRun = [&](int a, int c) {
#if UI_RECORD_SHADOW
      if (!clampRun(a, c)) return;
      int32_t uq = uAt(a);
      for (int x = a; x <= c; x++, uq += du) {
        const uint32_t si = ((uint32_t)(uq * uq) + vs2) >> SHADOW_SHIFT;
        if (si >= (uint32_t)SHADOW_SIZE) continue;
        const uint32_t sh = (shadowLut[si] * (uint32_t)alpha) >> 8;
        if (sh < 4) continue;
        row[x] = darken5(row[x], (256 - sh + 4) >> 3);
      }
#else
      (void)a; (void)c;
#endif
    };
    // This row's stretch of u where a glare wedge can be (it spans 30..85
    // degrees clockwise from up, and the same turned half way round; a little
    // wider here for the table's smoothing).
    int32_t gLo = 1, gHi = 0;
    if (vq < 0)      { gLo = (-vq * 136) >> 8;  gHi = -vq * 20; }    // tan 28 deg, tan 87 deg
    else if (vq > 0) { gLo = -vq * 20;          gHi = (-vq * 136) >> 8; }
    auto grooveRun = [&](int a, int c) {
      if (!clampRun(a, c)) return;
      int32_t uq = uAt(a);
      for (int x = a; x <= c; x++, uq += du) {
        const uint32_t d2 = (uint32_t)(uq * uq) + v2;
        const uint16_t px = packGrey(surfaceGrey(uq, vq, d2, uq > gLo && uq < gHi), dith[x & 3]);
        row[x] = (alpha >= 256) ? px : blend5(row[x], px, a5);
      }
    };
    auto labelRun = [&](int a, int c) {
      if (!clampRun(a, c)) return;
      int32_t jq = uAt(a) + LAB_C * 256;
      for (int x = a; x <= c; x++, jq += du) {
        const int j = jq >> 8;
        const uint16_t lc = art.palette[artRow[artPos[j]]];
        row[x] = (alpha >= 256) ? lc : blend5(row[x], lc, a5);
      }
    };
    // Any pixel, from its distance: edges, rims, the spindle.
    auto generic = [&](int a, int c) {
      if (!clampRun(a, c)) return;
      int32_t uq = uAt(a);
      for (int x = a; x <= c; x++, uq += du) {
        const uint32_t u2 = (uint32_t)(uq * uq);
        const uint32_t d2 = u2 + v2;
        uint16_t &px = row[x];

        if (d2 > Z_RECOUT) {
#if UI_RECORD_SHADOW
          const uint32_t si = (u2 + vs2) >> SHADOW_SHIFT;
          if (si >= (uint32_t)SHADOW_SIZE) continue;
          const uint32_t sh = (shadowLut[si] * (uint32_t)alpha) >> 8;
          if (sh >= 4) px = darken5(px, (256 - sh + 4) >> 3);
#endif
          continue;
        }

        if (d2 < Z_LABIN) {
          int j = (uq + LAB_C * 256) >> 8;
          if (j < jx0) j = jx0;
          if (j > jx1) j = jx1;
          const uint16_t lc = art.palette[artRow[artPos[j]]];
          if (d2 > Z_SPIN) {
            px = (alpha >= 256) ? lc : blend5(px, lc, a5);          // label
            continue;
          }
          // Spindle hole and its light rim, over the label.
          const int dq = (int)((((int32_t)isqrt32(d2) - spinQ) * SC_Q8) >> 8);
          Rgb c = mix(unpack8(lc), Rgb{ 255, 255, 255 }, (bandCov(dq, ringQ) * 90) >> 8);   // .35
          c = mix(c, Rgb{ BASE_R, BASE_G, BASE_B }, insideCov(dq));
          if (alpha < 256) c = mix(unpack(px), c, alpha);
          px = pack(c, bay[x & 3]);
          continue;
        }

        if (d2 > Z_LABOUT && d2 < Z_RECIN) {
          const uint16_t c = packGrey(surfaceGrey(uq, vq, d2, inGlareQuarter(uq, vq)), dith[x & 3]);
          px = (alpha >= 256) ? c : blend5(px, c, a5);
          continue;
        }

        if (d2 >= Z_RECIN) {
          // Record rim: shadowed backdrop, the light ring, then the disc.
          const Rgb bg = unpack(px);
          Rgb under = bg;
#if UI_RECORD_SHADOW
          const uint32_t si = (u2 + vs2) >> SHADOW_SHIFT;
          if (si < (uint32_t)SHADOW_SIZE) under = mix(bg, Rgb{ 0, 0, 0 }, shadowLut[si]);
#endif
          const int dq = (int)(((((int32_t)d2 >> 8) - RR_REC) * K_REC) >> 16);
          Rgb c = mix(under, Rgb{ 255, 255, 255 }, (bandCov(dq, ringQ) * 56) >> 8);    // .22
          c = mix(c, greyRgb(surfaceGrey(uq, vq, d2, inGlareQuarter(uq, vq))), insideCov(dq));
          px = pack(mix(bg, c, alpha), bay[x & 3]);
          continue;
        }

        // Label rim: grooves, the label's dark ring, then the label.
        int j = (uq + LAB_C * 256) >> 8;
        if (j < jx0) j = jx0;
        if (j > jx1) j = jx1;
        const uint16_t lc = art.palette[artRow[artPos[j]]];
        const int dq = (int)(((((int32_t)d2 >> 8) - RR_LAB) * K_LAB) >> 16);
        Rgb c = mix(greyRgb(surfaceGrey(uq, vq, d2, inGlareQuarter(uq, vq))), Rgb{ 0, 0, 0 }, (bandCov(dq, ringQ) * 154) >> 8);  // .6
        c = mix(c, unpack8(lc), insideCov(dq));
        if (alpha < 256) c = mix(unpack(px), c, alpha);
        px = pack(c, bay[x & 3]);
      }
    };

    // ---- Split the row, outside in ---------------------------------------------
    //  shadow | rim | grooves | label rim | label | spindle | label | ... mirrored
    int ro0, ro1, gi0, gi1, lo0, lo1, li0, li1, sp0, sp1;
    outer(R_RECOUT, ro0, ro1);
    if (ro0 > ro1) { shadowRun(x0, x1); continue; }
    shadowRun(x0, ro0 - 1);
    shadowRun(ro1 + 1, x1);
    inner(R_RECIN, gi0, gi1);
    if (gi0 > gi1 || gi0 < ro0 || gi1 > ro1) { generic(ro0, ro1); continue; }
    generic(ro0, gi0 - 1);
    generic(gi1 + 1, ro1);
    outer(R_LABOUT, lo0, lo1);
    if (lo0 > lo1) { grooveRun(gi0, gi1); continue; }
    if (lo0 < gi0) lo0 = gi0;
    if (lo1 > gi1) lo1 = gi1;
    grooveRun(gi0, lo0 - 1);
    grooveRun(lo1 + 1, gi1);
    inner(R_LABIN, li0, li1);
    if (li0 > li1) { generic(lo0, lo1); continue; }
    if (li0 < lo0) li0 = lo0;
    if (li1 > lo1) li1 = lo1;
    generic(lo0, li0 - 1);
    generic(li1 + 1, lo1);
    outer(R_SPIN, sp0, sp1);
    if (sp0 > sp1) { labelRun(li0, li1); continue; }
    if (sp0 < li0) sp0 = li0;
    if (sp1 > li1) sp1 = li1;
    labelRun(li0, sp0 - 1);
    generic(sp0, sp1);
    labelRun(sp1 + 1, li1);
  }
}

}
