// Hot per-pixel loops: built with -O2 whatever the IDE's "Optimize" menu
// says (its default -Os spills registers in them and runs about 2x slower).
#pragma GCC optimize("O2")

#include "backdrop.h"
#include "albums.h"
#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace Backdrop {

namespace {

  constexpr int log2i(int v) { return v <= 1 ? 0 : 1 + log2i(v / 2); }

  constexpr int CELL     = UI_BG_CELL;
  constexpr int N        = SCREEN_W / CELL;
  constexpr int ROW      = N * 3;
  constexpr int PLANE    = N * ROW;
  constexpr int SUB      = 2 * CELL;
  constexpr int SUBSHIFT = log2i(SUB);
  constexpr int SCALE    = 2 * SUBSHIFT;

  static_assert((CELL & (CELL - 1)) == 0 && CELL >= 2, "UI_BG_CELL must be a power of two");
  static_assert(SCREEN_W % CELL == 0, "UI_BG_CELL must divide the screen");

  // The mock draws the cover 160 px wide on its 128 px screen, starting 16 px
  // above and left of it: 1.25x, centred. Art pixel x lands on screen at
  // (x + 0.5) * 1.25 - 30.
  constexpr float ZOOM   = 1.25f;
  // Art is sampled every 2nd pixel each way when a thumbnail is built. A cell
  // still averages about 10 samples, and the build takes a quarter of the
  // time (one pass over the art in flash).
  constexpr int   STEP   = 2;

  // Thumbnails are built on demand into a few cache slots, not one per album
  // at boot (that grew the heap with every album and blanked the chooser at
  // 7). The chooser only ever shows two at once (the blend between
  // neighbouring rows), so 3 slots let one be rebuilt while the other two
  // stay put. A rebuild is one pass over the album art in flash, a few ms,
  // once per album that scrolls into view.
  constexpr uint8_t SLOTS = 3;

  uint8_t *cache     = nullptr;    // slotCount * PLANE thumbnails, then N * N scratch
  uint8_t *scratch   = nullptr;    // `valid` flags for buildThumb()
  uint8_t  slotCount = 0;
  int16_t  slotArt[SLOTS];         // album in each slot, -1 = empty
  uint32_t slotUsed[SLOTS];        // when it was last asked for (0 = never)
  uint32_t useClock  = 0;

  const uint8_t *srcA = nullptr;
  const uint8_t *srcB = nullptr;
  int            wB   = 0;

  Shade shade = Shade::TOP_DOWN;

  uint8_t halfChord[SCREEN_H];     // half-width of the DISC_R circle per row
  int8_t  cellOf[ART_W];           // art row/column -> thumbnail cell, -1 = off screen

  // Shades, as "keep" factors out of 256 (256 - 256 * the shade's alpha).
  //  TOP_DOWN: the mock's linear-gradient(180deg, .15 0%, .35 40%, .72 70%)
  //  VIGNETTE: its radial-gradient(circle, .45 0%, .72 75%), whose 100% is the
  //            distance to a corner, 120 * sqrt(2). Indexed by r^2 / 64.
  // Kept at or below 240, which leaves room for the dither on the brightest art.
  uint8_t keepRow[SCREEN_H];
  constexpr int VIG_SHIFT = 6;
  constexpr int VIG_SIZE  = ((SCREEN_W / 2) * (SCREEN_W / 2) * 2 >> VIG_SHIFT) + 2;
  uint8_t keepVig[VIG_SIZE];

  const uint8_t BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

  uint8_t keepFor(float alpha) {
    int k = (int)lroundf((1.0f - alpha) * 256.0f);
    if (k > 240) k = 240;
    if (k < 0)   k = 0;
    return (uint8_t)k;
  }

  void fillEmptyCells(uint8_t *t, uint8_t *valid) {
    for (int pass = 0; pass < N; pass++) {
      bool any = false;
      for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
          const int c = j * N + i;
          if (valid[c]) continue;
          int sum[3] = { 0, 0, 0 }, n = 0;
          const int nb[4][2] = { { i - 1, j }, { i + 1, j }, { i, j - 1 }, { i, j + 1 } };
          for (auto &p : nb) {
            if (p[0] < 0 || p[0] >= N || p[1] < 0 || p[1] >= N) continue;
            const int q = p[1] * N + p[0];
            if (valid[q] != 1) continue;
            for (int ch = 0; ch < 3; ch++) sum[ch] += t[q * 3 + ch];
            n++;
          }
          if (!n) { any = true; continue; }
          for (int ch = 0; ch < 3; ch++) t[c * 3 + ch] = (uint8_t)(sum[ch] / n);
          valid[c] = 2;
        }
      }
      for (int c = 0; c < N * N; c++) if (valid[c] == 2) valid[c] = 1;
      if (!any) break;
    }
  }

  void blurLine(uint8_t *p, int stride) {
    uint8_t src[N * 3];
    for (int i = 0; i < N; i++)
      for (int ch = 0; ch < 3; ch++) src[i * 3 + ch] = p[i * stride + ch];

    for (int i = 0; i < N; i++) {
      for (int ch = 0; ch < 3; ch++) {
        auto at = [&](int k) {
          if (k < 0) k = 0;
          if (k > N - 1) k = N - 1;
          return (int)src[k * 3 + ch];
        };
        const int v = at(i - 2) + 4 * at(i - 1) + 6 * at(i) + 4 * at(i + 1) + at(i + 2);
        p[i * stride + ch] = (uint8_t)((v + 8) >> 4);
      }
    }
  }

  // One cell row of the thumbnail from the per-cell sums in acc / cnt.
  void flushRow(int cj, const uint32_t *acc, const uint16_t *cnt, uint8_t *t, uint8_t *valid) {
    for (int ci = 0; ci < N; ci++) {
      const int cell = cj * N + ci;
      if (!cnt[ci]) { valid[cell] = 0; continue; }
      valid[cell] = 1;
      t[cell * 3 + 0] = (uint8_t)((acc[ci * 3 + 0] * 255 + cnt[ci] * 15) / (cnt[ci] * 31));
      t[cell * 3 + 1] = (uint8_t)((acc[ci * 3 + 1] * 255 + cnt[ci] * 31) / (cnt[ci] * 63));
      t[cell * 3 + 2] = (uint8_t)((acc[ci * 3 + 2] * 255 + cnt[ci] * 15) / (cnt[ci] * 31));
    }
  }

  // Order follows the mock's CSS filter "blur(7px) saturate(1.15)": average
  // the zoomed art into cells, blur, then saturate. The shade is applied
  // later, per pixel, in renderRows().
  void buildThumb(const AlbumArt &art, uint8_t *t, uint8_t *valid) {
    static uint32_t acc[ROW];
    static uint16_t cnt[N];

    // Pixels outside DISC_R are blacked out by build_albums.py; skip them and
    // let fillEmptyCells() extend the edge colour instead.
    const int r2 = DISC_R * DISC_R;
    memset(valid, 0, N * N);

    int curJ = -1;
    for (int y = 0; y < ART_W; y += STEP) {
      const int cj = cellOf[y];
      if (cj != curJ) {
        if (curJ >= 0) flushRow(curJ, acc, cnt, t, valid);
        memset(acc, 0, sizeof(acc));
        memset(cnt, 0, sizeof(cnt));
        curJ = cj;
      }
      if (cj < 0) continue;

      const uint8_t *row = art.pixels + y * ART_W;
      const int dy = y - DISC_CY;
      for (int x = 0; x < ART_W; x += STEP) {
        const int ci = cellOf[x];
        if (ci < 0) continue;
        const int dx = x - DISC_CX;
        if (dx * dx + dy * dy > r2) continue;
        const uint16_t c = art.palette[row[x]];
        acc[ci * 3 + 0] += (c >> 11) & 0x1F;
        acc[ci * 3 + 1] += (c >> 5)  & 0x3F;
        acc[ci * 3 + 2] +=  c        & 0x1F;
        cnt[ci]++;
      }
    }
    if (curJ >= 0) flushRow(curJ, acc, cnt, t, valid);

    fillEmptyCells(t, valid);

    for (int pass = 0; pass < UI_BG_BLUR_PASSES; pass++) {
      for (int j = 0; j < N; j++) blurLine(t + j * ROW, 3);
      for (int i = 0; i < N; i++) blurLine(t + i * 3, ROW);
    }

    // saturate(): push each channel away from the luma (Rec. 709 weights, as
    // the CSS filter uses).
    for (int c = 0; c < N * N; c++) {
      uint8_t *p = t + c * 3;
      const int l = (54 * p[0] + 183 * p[1] + 19 * p[2]) >> 8;
      for (int ch = 0; ch < 3; ch++) {
        int v = l + (((int)p[ch] - l) * UI_BG_SATURATION >> 8);
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        p[ch] = (uint8_t)v;
      }
    }
  }

  // Thumbnail of album `art`, built into the least recently used slot if it
  // is not cached yet. Asking for one makes it the most recent, so the next
  // call can never evict it while two or more slots exist.
  const uint8_t *thumbFor(uint8_t art) {
    if (!cache || art >= ALBUM_COUNT) return nullptr;
    useClock++;
    uint8_t victim = 0;
    for (uint8_t s = 0; s < slotCount; s++) {
      if (slotArt[s] == art) {
        slotUsed[s] = useClock;
        return cache + s * PLANE;
      }
      if (slotUsed[s] < slotUsed[victim]) victim = s;   // empty slots are 0, so they go first
    }
    uint8_t *t = cache + victim * PLANE;
    buildThumb(ALBUMS[art], t, scratch);
    slotArt[victim]  = art;
    slotUsed[victim] = useClock;
    return t;
  }

  // The thumbnail renderRows() reads: srcA itself, or srcA and srcB mixed
  // into `mixed` once per frame (setBlend()), not per pixel.
  uint8_t *mixed   = nullptr;     // one PLANE after the slots
  bool     mixDone = false;

  const uint8_t *plane() {
    if (!srcA) return nullptr;
    if (!wB)   return srcA;
    if (!mixed) return srcA;
    if (!mixDone) {
      for (int i = 0; i < PLANE; i++) mixed[i] = (uint8_t)((srcA[i] * (256 - wB) + srcB[i] * wB) >> 8);
      mixDone = true;
    }
    return mixed;
  }

}

void init() {
  for (int y = 0; y < SCREEN_H; y++) {
    const int dy = y - DISC_CY;
    const int v  = DISC_R * DISC_R - dy * dy;
    halfChord[y] = (v <= 0) ? 0 : (uint8_t)sqrtf((float)v);
  }

  for (int x = 0; x < ART_W; x++) {
    // Screen position of art pixel x. The zoom is about the centre, so
    // x = 120 stays at 120 and x = 0 lands at -30.
    const float sx = ((float)x + 0.5f) * ZOOM - (ZOOM - 1.0f) * (SCREEN_W / 2.0f);
    const int   c  = (int)floorf(sx / CELL);
    cellOf[x] = (sx < 0.0f || c >= N) ? -1 : (int8_t)c;
  }

  // TOP_DOWN, per row.
  for (int y = 0; y < SCREEN_H; y++) {
    const float t = ((float)y + 0.5f) / SCREEN_H;
    float a;
    if (t < 0.4f)      a = 0.15f + (0.35f - 0.15f) * (t / 0.4f);
    else if (t < 0.7f) a = 0.35f + (0.72f - 0.35f) * ((t - 0.4f) / 0.3f);
    else               a = 0.72f;
    keepRow[y] = keepFor(a);
  }
  // VIGNETTE, by squared distance from the centre.
  const float corner = (SCREEN_W / 2) * 1.41421356f;
  for (int i = 0; i < VIG_SIZE; i++) {
    const float r = sqrtf((float)(i << VIG_SHIFT));
    float f = r / (0.75f * corner);
    if (f > 1.0f) f = 1.0f;
    keepVig[i] = keepFor(0.45f + (0.72f - 0.45f) * f);
  }

  // One block, the same size however many albums there are: SLOTS
  // thumbnails, the blend of two of them, and buildThumb()'s scratch,
  // 11,700 bytes at UI_BG_CELL 8.
  // If that does not fit, take fewer slots (more rebuilds; with one slot the
  // blend shows a single album, see setBlend()). With none the backdrop is
  // black.
  free(cache);
  cache = nullptr;
  for (slotCount = SLOTS; slotCount > 0; slotCount--) {
    cache = (uint8_t *)malloc((size_t)(slotCount + 1) * PLANE + N * N);
    if (cache) break;
  }
  mixed   = cache ? cache + slotCount * PLANE : nullptr;
  scratch = cache ? mixed + PLANE : nullptr;
  for (uint8_t s = 0; s < SLOTS; s++) {
    slotArt[s]  = -1;
    slotUsed[s] = 0;
  }
  useClock = 0;

  srcA = srcB = nullptr;
  wB = 0;
}

void setBlend(uint8_t artA, uint8_t artB, uint8_t t) {
  // With a single slot, fetching B would be built over A: show A alone.
  if (slotCount < 2) { artB = artA; t = 0; }
  const uint8_t *a = thumbFor(artA);
  const uint8_t *b = (artB == artA) ? a : thumbFor(artB);
  if (!a) { a = b; t = 0; }
  if (!b || b == a) t = 0;
  srcA = a;
  srcB = t ? b : a;
  wB   = t;
  mixDone = false;
  plane();          // mix now, on the caller's core, not during drawing
}

void setShade(Shade s) { shade = s; }

void renderRows(uint16_t *band, int bandTop, int y0, int y1) {
  // Thumbnail cells are upscaled with bilinear interpolation, shaded, and
  // dithered to RGB565. Per pixel this is the hottest loop of the chooser
  // (57,600 pixels a frame), so:
  //  - the three channels travel together in one 32-bit word, 10 bits each
  //    as 8.2 fixed point (R in bits 22..31, G in 11..20, B in 0..9; bits 10
  //    and 21 stay 0). One add steps all three along a cell, one add applies
  //    the ordered dither, three masks pull out the 5/6/5 bits;
  //  - the interpolated row is shared by a pair of rows (an even row and the
  //    one below it). The image is heavily blurred, so the two differ by well
  //    under one RGB565 step; each still gets its own dither pattern.
  const uint8_t *src = plane();
  static uint32_t lanes[SCREEN_W];
  int lanesRow = -2;
  int16_t line[ROW];

  for (int y = y0; y <= y1; y++) {
    uint16_t *out = band + (y - bandTop) * SCREEN_W;
    const int half = halfChord[y];
    if (!half || !src) { memset(out, 0, SCREEN_W * sizeof(uint16_t)); continue; }
    const int xs = DISC_CX - half;
    const int xe = DISC_CX + half;

    if (lanesRow != y - 1 || (y & 1) == 0) {
      // Interpolate this row (and the next, if it is the odd one of a pair),
      // over the wider of the two chords.
      int hh = half;
      if (y + 1 < SCREEN_H && halfChord[y + 1] > hh) hh = halfChord[y + 1];
      const int ls = DISC_CX - hh, le = DISC_CX + hh;

      const int V = 2 * y + 2 - CELL;               // between the pair's two rows
      int v0 = V >> SUBSHIFT, fy = V & (SUB - 1);
      if (V < 0)       { v0 = 0;     fy = 0; }
      if (v0 >= N - 1) { v0 = N - 1; fy = 0; }

      // Vertical interpolation into `line` (8.4 fixed point). The TOP_DOWN
      // shade is one factor per row, so it is folded in here.
      const uint8_t *r0 = src + v0 * ROW;
      const uint8_t *r1 = fy ? r0 + ROW : r0;
      const int rowKeep = (shade == Shade::TOP_DOWN) ? keepRow[y] : 256;
      for (int i = 0; i < ROW; i++)
        line[i] = (int16_t)(((r0[i] * (SUB - fy) + r1[i] * fy) * rowKeep) >> 8);

      const int Y = 2 * y + 1 - SCREEN_H;           // doubled, for the vignette
      const int U = 2 * ls + 1 - CELL;
      int u0 = U >> SUBSHIFT, fx = U & (SUB - 1);
      int x  = ls;
      while (x <= le) {
        int i0 = u0, i1 = u0 + 1;
        if (i0 < 0) i0 = 0;
        if (i0 > N - 1) i0 = N - 1;
        if (i1 < 0) i1 = 0;
        if (i1 > N - 1) i1 = N - 1;
        const int16_t *A = line + i0 * 3;
        const int16_t *B = line + i1 * 3;

        // Pixels left in this cell (fx steps by 2 of SUB per pixel).
        int n = (SUB - fx + 1) >> 1;
        if (n > le - x + 1) n = le - x + 1;

        // Start colour and per-pixel step, 8.2. The step is truncated towards
        // zero, so a run never overshoots its end colour (a lane must never
        // go negative, it would borrow from the next one).
        int sr = (A[0] * SUB + (B[0] - A[0]) * fx) >> 6;
        int sg = (A[1] * SUB + (B[1] - A[1]) * fx) >> 6;
        int sb = (A[2] * SUB + (B[2] - A[2]) * fx) >> 6;
        int dr = ((B[0] - A[0]) * 2) / 64;
        int dg = ((B[1] - A[1]) * 2) / 64;
        int db = ((B[2] - A[2]) * 2) / 64;

        if (shade == Shade::VIGNETTE) {
          // The vignette changes slowly: one factor per run, at its middle.
          const int X = 2 * (x + n / 2) + 1 - SCREEN_W;
          const int k = keepVig[(X * X + Y * Y) >> (VIG_SHIFT + 2)];
          sr = (sr * k) >> 8; sg = (sg * k) >> 8; sb = (sb * k) >> 8;
          dr = (dr * k) / 256; dg = (dg * k) / 256; db = (db * k) / 256;
        }

        uint32_t acc = ((uint32_t)sr << 22) + ((uint32_t)sg << 11) + (uint32_t)sb;
        const uint32_t step = (uint32_t)(dr * (1 << 22) + dg * (1 << 11) + db);
        uint32_t *l = lanes + x;
        for (int i = 0; i < n; i++) { l[i] = acc; acc += step; }
        x += n;
        u0++;
        fx += 2 * n - SUB;
      }
      lanesRow = y;
    }

    // This row's 4 dither offsets, in lane form: up to 7/8 of a 5-bit step
    // for R and B, of a 6-bit step for G.
    const uint8_t *bay = BAYER + (y & 3) * 4;
    uint32_t d[4];
    for (int k = 0; k < 4; k++) {
      const uint32_t bv = bay[k];
      d[k] = ((2 * bv) << 22) | (bv << 11) | (2 * bv);
    }

    if (xs > 0)            memset(out, 0, xs * sizeof(uint16_t));
    if (xe < SCREEN_W - 1) memset(out + xe + 1, 0, (SCREEN_W - 1 - xe) * sizeof(uint16_t));
    for (int x = xs; x <= xe; x++) {
      const uint32_t v = lanes[x] + d[x & 3];
      out[x] = (uint16_t)(((v >> 16) & 0xF800) | ((v >> 10) & 0x07E0) | ((v >> 5) & 0x001F));
    }
  }
}

}
