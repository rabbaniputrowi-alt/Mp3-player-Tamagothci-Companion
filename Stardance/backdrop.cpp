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
  static_assert(UI_BG_DIM <= 240, "dim must leave headroom for dithering");

  // Thumbnails are built on demand into a few cache slots, not one per album
  // at boot. One thumbnail is PLANE bytes (10,800 at UI_BG_CELL 4), so
  // building them all made the heap grow with every album: 7 albums need
  // 75 KB, and the chooser sprite allocated right after then fails, which
  // leaves the chooser blank. The chooser only ever shows two at once (the
  // blend between neighbouring rows), so 3 slots let one be rebuilt while the
  // other two stay put. A rebuild is one pass over the album art in flash,
  // roughly 10-20 ms, once per album that scrolls into view.
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

  uint8_t halfChord[SCREEN_H];

  const uint8_t BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

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

  void buildThumb(const AlbumArt &art, uint8_t *t, uint8_t *valid) {
    static uint32_t acc[ROW];
    static uint16_t cnt[N];

    const int r2 = DISC_R * DISC_R;

    for (int cj = 0; cj < N; cj++) {
      memset(acc, 0, sizeof(acc));
      memset(cnt, 0, sizeof(cnt));

      for (int y = cj * CELL; y < (cj + 1) * CELL; y++) {
        const uint8_t *row = art.pixels + y * ART_W;
        const int dy = y - DISC_CY;
        for (int x = 0; x < ART_W; x++) {
          const int dx = x - DISC_CX;
          if (dx * dx + dy * dy > r2) continue;
          const uint16_t c = art.palette[row[x]];
          const int ci = x / CELL;
          acc[ci * 3 + 0] += (c >> 11) & 0x1F;
          acc[ci * 3 + 1] += (c >> 5)  & 0x3F;
          acc[ci * 3 + 2] +=  c        & 0x1F;
          cnt[ci]++;
        }
      }

      for (int ci = 0; ci < N; ci++) {
        const int cell = cj * N + ci;
        if (!cnt[ci]) { valid[cell] = 0; continue; }
        valid[cell] = 1;
        t[cell * 3 + 0] = (uint8_t)((acc[ci * 3 + 0] * 255 + cnt[ci] * 15) / (cnt[ci] * 31));
        t[cell * 3 + 1] = (uint8_t)((acc[ci * 3 + 1] * 255 + cnt[ci] * 31) / (cnt[ci] * 63));
        t[cell * 3 + 2] = (uint8_t)((acc[ci * 3 + 2] * 255 + cnt[ci] * 15) / (cnt[ci] * 31));
      }
    }

    fillEmptyCells(t, valid);

    for (int pass = 0; pass < UI_BG_BLUR_PASSES; pass++) {
      for (int j = 0; j < N; j++) blurLine(t + j * ROW, 3);
      for (int i = 0; i < N; i++) blurLine(t + i * 3, ROW);
    }

    uint32_t lumaSum = 0;
    for (int c = 0; c < N * N; c++) {
      uint8_t *p = t + c * 3;
      const int l = (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
      for (int ch = 0; ch < 3; ch++) {
        int v = l + (((int)p[ch] - l) * UI_BG_SATURATION >> 8);
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        p[ch] = (uint8_t)v;
      }
      lumaSum += (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
    }

    const uint32_t mean = lumaSum / (N * N);
    uint32_t scale = UI_BG_DIM;
    if (mean && (uint32_t)UI_BG_TARGET_LUMA * 256 / mean < scale)
      scale = (uint32_t)UI_BG_TARGET_LUMA * 256 / mean;

    for (int i = 0; i < PLANE; i++) t[i] = (uint8_t)((t[i] * scale) >> 8);
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

  inline int blended(int idx) {
    if (!srcA) return 0;
    if (!wB)   return srcA[idx];
    return (srcA[idx] * (256 - wB) + srcB[idx] * wB) >> 8;
  }

}

void init() {
  for (int y = 0; y < SCREEN_H; y++) {
    const int dy = y - DISC_CY;
    const int v  = DISC_R * DISC_R - dy * dy;
    halfChord[y] = (v <= 0) ? 0 : (uint8_t)sqrtf((float)v);
  }

  // One block, the same size however many albums there are: SLOTS
  // thumbnails plus buildThumb()'s scratch, 36,000 bytes at UI_BG_CELL 4.
  // If that does not fit, take fewer slots (more rebuilds; with one slot the
  // blend shows a single album, see setBlend()). With none the backdrop is
  // black, as before.
  free(cache);
  cache = nullptr;
  for (slotCount = SLOTS; slotCount > 0; slotCount--) {
    cache = (uint8_t *)malloc((size_t)slotCount * PLANE + N * N);
    if (cache) break;
  }
  scratch = cache ? cache + slotCount * PLANE : nullptr;
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
}

uint16_t tint(int cy, uint8_t towardWhite) {
  int j0 = (cy - 12) / CELL, j1 = (cy + 12) / CELL;
  if (j0 < 0) j0 = 0;
  if (j1 > N - 1) j1 = N - 1;
  const int i0 = 48 / CELL, i1 = 192 / CELL;

  uint32_t sum[3] = { 0, 0, 0 }, n = 0;
  for (int j = j0; j <= j1; j++) {
    for (int i = i0; i < i1; i++) {
      const int idx = (j * N + i) * 3;
      sum[0] += blended(idx); sum[1] += blended(idx + 1); sum[2] += blended(idx + 2);
      n++;
    }
  }

  uint8_t c[3];
  for (int ch = 0; ch < 3; ch++) {
    const int base = n ? (int)(sum[ch] / n) : 0;
    c[ch] = (uint8_t)(base + (((255 - base) * towardWhite) >> 8));
  }
  return (uint16_t)(((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3));
}

void renderRows(uint16_t *strip, int stripTop, int y0, int y1) {
  int16_t line[ROW];

  for (int y = y0; y <= y1; y++) {
    uint16_t *out = strip + (y - stripTop) * SCREEN_W;
    const int half = halfChord[y];
    if (!half || !srcA) { memset(out, 0, SCREEN_W * sizeof(uint16_t)); continue; }

    const int xs = DISC_CX - half;
    const int xe = DISC_CX + half;
    if (xs > 0)            memset(out, 0, xs * sizeof(uint16_t));
    if (xe < SCREEN_W - 1) memset(out + xe + 1, 0, (SCREEN_W - 1 - xe) * sizeof(uint16_t));

    const int V = 2 * y + 1 - CELL;
    int v0 = V >> SUBSHIFT, fy = V & (SUB - 1);
    if (V < 0)       { v0 = 0;     fy = 0; }
    if (v0 >= N - 1) { v0 = N - 1; fy = 0; }

    const int o0 = v0 * ROW;
    const int o1 = fy ? o0 + ROW : o0;
    if (!wB) {
      for (int i = 0; i < ROW; i++)
        line[i] = (int16_t)(srcA[o0 + i] * (SUB - fy) + srcA[o1 + i] * fy);
    } else {
      for (int i = 0; i < ROW; i++)
        line[i] = (int16_t)(blended(o0 + i) * (SUB - fy) + blended(o1 + i) * fy);
    }

    const uint8_t *bay = BAYER + (y & 3) * 4;

    const int U = 2 * xs + 1 - CELL;
    int u0 = U >> SUBSHIFT, fx = U & (SUB - 1);
    int cr, cg, cb, dr, dg, db;

    auto setup = [&]() {
      int i0 = u0, i1 = u0 + 1;
      if (i0 < 0) i0 = 0;
      if (i0 > N - 1) i0 = N - 1;
      if (i1 < 0) i1 = 0;
      if (i1 > N - 1) i1 = N - 1;
      const int16_t *A = line + i0 * 3;
      const int16_t *B = line + i1 * 3;
      dr = B[0] - A[0]; dg = B[1] - A[1]; db = B[2] - A[2];
      cr = A[0] * SUB + dr * fx;
      cg = A[1] * SUB + dg * fx;
      cb = A[2] * SUB + db * fx;
      dr *= 2; dg *= 2; db *= 2;
    };
    setup();

    for (int x = xs; x <= xe; x++) {
      const int b = bay[x & 3];
      const uint32_t v = ((uint32_t)((cr + (b << (SCALE - 1))) >> (SCALE + 3)) << 11)
                       | ((uint32_t)((cg + (b << (SCALE - 2))) >> (SCALE + 2)) << 5)
                       |  (uint32_t)((cb + (b << (SCALE - 1))) >> (SCALE + 3));
      out[x] = (uint16_t)((v >> 8) | (v << 8));

      cr += dr; cg += dg; cb += db;
      fx += 2;
      if (fx >= SUB) { fx -= SUB; u0++; setup(); }
    }
  }
}

}
