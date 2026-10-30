#include "playlist_ui.h"
#include "config.h"
#include "display.h"
#include "audio_player.h"
#include "playlist_art.h"
#include "backdrop.h"
#include "mini_record.h"
#include "track_names.h"
#include "ui_draw.h"
#include "ui_fonts.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#if UI_TIMING_DEBUG || UI_DUAL_CORE
#include <Arduino.h>
#endif

namespace PlaylistUI {

namespace {

  using UiDraw::Band;

  // The mock is drawn on a 128 px canvas; the panel is 240 px.
  constexpr float K = SCREEN_W / 128.0f;

  // ---- Playlist arc (mock: each record is a 128 px box rotated about
  // (64, 140), with the record centred at (64, 48) in it) ---------------------
  constexpr float ARC_PIVOT_Y = 140.0f * K;
  constexpr float ARC_RADIUS  = (140.0f - 48.0f) * K;
  constexpr float ARC_STEP    = 40.0f;          // degrees between playlists
  constexpr float TITLE_TOP   = 88.0f * K;      // name under the record
  constexpr float TITLE_LH    = 15.0f * K;
  constexpr float TITLE_INSET = 14.0f * K;
  constexpr float TICK_STEP   = 6.0f;           // degrees between ticks
  constexpr float TICK_R0     = 61.0f * K;
  constexpr float TICK_R1     = 58.0f * K;
  constexpr float TICK_R1_SEL = 55.0f * K;
  constexpr float TICK_W      = 1.2f * K;

  // ---- Track barrel ----------------------------------------------------------
  constexpr float ROW_PITCH   = 17.0f * K;
  constexpr int   BAR_Y0      = (int)(56.0f * K);        // dark band, rows 105..134
  constexpr int   BAR_Y1      = (int)(72.0f * K) - 1;
  constexpr uint16_t BAR_KEEP = 141;                      // rgba(0,0,0,.45)
  constexpr float BARREL_R    = 58.0f * K;                // the circle rows narrow with
  constexpr float BARREL_PAD  = 18.0f * K;
  constexpr float SCROLL_R    = 59.0f * K;                // left-rim position arc
  constexpr float SCROLL_W    = 2.0f * K;
  constexpr float SCROLL_A0   = 220.0f, SCROLL_A1 = 320.0f, SCROLL_THUMB = 7.0f;

  // ---- Footer ------------------------------------------------------------------
  constexpr float FOOT_TOP_TR = 108.0f * K;
  constexpr float FOOT_INSET  = 26.0f * K;

  constexpr uint16_t COL_WHITE = 0xFFFF;
  constexpr uint16_t COL_ROW   = 0xE71C;     // #e0e0e0
  constexpr uint16_t COL_FOOT  = 0xDEDB;     // #d8d8d8

  // Marquee (mock: 0.6 px per 40 ms tick, 25 ticks of rest before it moves,
  // 35 at the far end).
  constexpr float MQ_SPEED  = 0.6f * K / 40.0f;    // px per ms
  constexpr uint32_t MQ_LEAD = 25 * 40;
  constexpr uint32_t MQ_TAIL = 35 * 40;

  // ---- State ---------------------------------------------------------------------
  bool     trackMode  = false;
  uint8_t  listOwner  = 0;
  uint8_t  count      = 0;
  int32_t  target     = 0;      // selected row; unwrapped on the playlist arc
  float    pos        = 0.0f;   // animated position
  float    slideFrom  = 0.0f;
  float    slideTo    = 0.0f;
  uint32_t slideStart = 0;
  uint16_t slideMs    = UI_ARC_SLIDE_MS;
  bool     sliding    = false;
  bool     dirtyFull  = true;
  bool     dirtyMarq  = false;
  uint32_t nowCache   = 0;
  uint32_t mqStart    = 0;
  int      mqDrawn    = -1;     // marquee offset (px) on screen

  inline int32_t wrap(int32_t i) {
    if (!count) return 0;
    int32_t m = i % count;
    return m < 0 ? m + count : m;
  }

  float easeOutCubic(float t) {          // close to the mock's cubic-bezier(.2,.8,.2,1)
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
  }

  void startSlide(int32_t to, uint32_t nowMs) {
    slideFrom  = pos;
    slideTo    = (float)to;
    slideStart = nowMs;
    slideMs    = trackMode ? UI_ROW_SLIDE_MS : UI_ARC_SLIDE_MS;
    sliding    = true;
    dirtyFull  = true;
  }

  uint8_t artFor(int i) {
    return trackMode ? PlaylistArt::artIndexFor(listOwner, (uint8_t)(i + 1))
                     : PlaylistArt::artIndexFor((uint8_t)wrap(i), 1);
  }

  void playlistLabel(uint8_t p, char *out, size_t n) {
    const char *name = TrackNames::playlistName(p);
    if (name) snprintf(out, n, "%s", name);
    else      snprintf(out, n, "Folder %02u", (unsigned)(p + 1));
  }

  void trackLabel(uint8_t i, char *out, size_t n) {
    const char *t = TrackNames::title(listOwner, (uint8_t)(i + 1));
    if (t) snprintf(out, n, "%s", t);
    else   snprintf(out, n, "Track %02u", (unsigned)(i + 1));
  }

  void updateBackdrop() {
    Backdrop::setShade(trackMode ? Backdrop::Shade::VIGNETTE : Backdrop::Shade::TOP_DOWN);
    if (!count) {
      const uint8_t a = trackMode ? PlaylistArt::artIndexFor(listOwner, 1) : 0;
      Backdrop::setBlend(a, a, 0);
      return;
    }
    float p = pos;
    if (trackMode) {
      if (p < 0) p = 0;
      if (p > count - 1) p = (float)(count - 1);
    }
    const int i0 = (int)floorf(p);
    int i1 = i0 + 1;
    if (trackMode && i1 > count - 1) i1 = i0;
    int t = (int)((p - (float)i0) * 256.0f + 0.5f);
    if (t > 255) t = 255;
    Backdrop::setBlend(artFor(i0), artFor(i1), (uint8_t)t);
  }

  // Width (px) a barrel row has at vertical offset dy from the centre.
  float rowWidth(float dy) {
    const float v = BARREL_R * BARREL_R - dy * dy;
    if (v <= 0.0f) return 0.0f;
    const float w = 2.0f * sqrtf(v) - BARREL_PAD;
    return w > 0.0f ? w : 0.0f;
  }

  // ---- Marquee -------------------------------------------------------------------
  // How far the selected title is scrolled left now (px), 0 if it fits.
  int marqueeOffset(uint32_t nowMs) {
    if (!trackMode || !count || sliding) return 0;
    char label[64];
    trackLabel((uint8_t)target, label, sizeof(label));
    const float w = UiDraw::textWidth16(UI_FONT_ROW_SEL, label) / 16.0f;
    const float over = w - rowWidth(0.0f);
    if (over <= 0.0f) return 0;
    const uint32_t run   = (uint32_t)(over / MQ_SPEED);
    const uint32_t cycle = MQ_LEAD + run + MQ_TAIL;
    const uint32_t ph    = (nowMs - mqStart) % cycle;
    if (ph < MQ_LEAD) return 0;
    float off = (ph - MQ_LEAD) * MQ_SPEED;
    if (off > over) off = over;
    return (int)(off + 0.5f);
  }

  // ---- Playlists -----------------------------------------------------------------
  struct Records {
    MiniRecord::Spec spec[4];
    uint8_t n;
  };

  Records layoutRecords() {
    Records r;
    r.n = 0;
    float d[UI_MAX_PLAYLISTS];
    uint8_t idx[UI_MAX_PLAYLISTS];
    uint8_t m = 0;
    for (int i = 0; i < count && i < UI_MAX_PLAYLISTS; i++) {
      float di = fmodf((float)i - pos, (float)count);
      if (di < 0) di += count;
      if (di > count / 2.0f) di -= count;
      if (fabsf(di) >= 2.0f) continue;
      d[m] = di; idx[m] = (uint8_t)i; m++;
    }
    // Far ones first, the selected record on top.
    for (uint8_t a = 0; a < m; a++)
      for (uint8_t b = a + 1; b < m; b++)
        if (fabsf(d[b]) > fabsf(d[a])) {
          float t = d[a]; d[a] = d[b]; d[b] = t;
          uint8_t u = idx[a]; idx[a] = idx[b]; idx[b] = u;
        }
    const float rad = 3.14159265f / 180.0f;
    for (uint8_t k = 0; k < m && r.n < 4; k++) {
      const float ad = fabsf(d[k]);
      const float op = ad <= 1.0f ? 1.0f - 0.25f * ad : 0.75f * (2.0f - ad);
      const float a  = d[k] * ARC_STEP * rad;
      MiniRecord::Spec &s = r.spec[r.n++];
      s.cx    = SCREEN_W / 2.0f + ARC_RADIUS * sinf(a);
      s.cy    = ARC_PIVOT_Y - ARC_RADIUS * cosf(a);
      s.scale = ad >= 1.0f ? 0.7f : 1.0f - 0.3f * ad;
      s.alpha = (uint8_t)(op * 255.0f + 0.5f);
      s.art   = PlaylistArt::artIndexFor(idx[k], 1);
    }
    return r;
  }

  void drawTicks(const Band &b) {
    const int sel = wrap(target);
    const float rad = 3.14159265f / 180.0f;
    for (int i = 0; i < count; i++) {
      const float a = (180.0f + (i - (count - 1) / 2.0f) * -TICK_STEP) * rad;
      const bool on = (i == sel);
      const float r1 = on ? TICK_R1_SEL : TICK_R1;
      UiDraw::strokeLine(b, 120.0f + TICK_R0 * sinf(a), 120.0f - TICK_R0 * cosf(a),
                            120.0f + r1 * sinf(a),      120.0f - r1 * cosf(a),
                         TICK_W, COL_WHITE, on ? 255 : 89);
    }
  }

  void centredText(const Band &b, const UiFont &f, const char *s, float top, float lineH,
                   float inset, uint16_t col, uint8_t a, bool shadow) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", s);
    const int32_t avail16 = (int32_t)((SCREEN_W - 2 * inset) * 16.0f);
    UiDraw::fitEllipsis(f, buf, sizeof(buf), avail16);
    const int32_t w16 = UiDraw::textWidth16(f, buf);
    const int baseline = (int)lroundf(top + (lineH - (f.ascent + f.descent)) / 2.0f + f.ascent);
    UiDraw::drawText(b, f, buf, (SCREEN_W / 2) * 16 - w16 / 2, baseline, col, a,
                     (int)inset, SCREEN_W - 1 - (int)inset, shadow);
  }

  // Track list only: "n / N". The playlist screen has no footer.
  void drawFooter(const Band &b) {
    if (!count) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "%u / %u", (unsigned)(target + 1), (unsigned)count);
    centredText(b, UI_FONT_FOOT, buf, FOOT_TOP_TR, (float)(UI_FONT_FOOT.ascent + UI_FONT_FOOT.descent),
                FOOT_INSET, COL_FOOT, 255, false);
  }

  void drawPlaylists(const Band &b, const Records &rec) {
    for (uint8_t k = 0; k < rec.n; k++) MiniRecord::draw(b, rec.spec[k]);
    char name[64];
    playlistLabel((uint8_t)wrap(target), name, sizeof(name));
    centredText(b, UI_FONT_TITLE, name, TITLE_TOP, TITLE_LH, TITLE_INSET, COL_WHITE, 255, true);
    drawTicks(b);
  }

  // ---- Tracks ----------------------------------------------------------------------
  void drawRow(const Band &b, int i, int mqOff) {
    const float d  = (float)i - pos;
    const float ad = fabsf(d);
    if (ad >= 3.0f) return;
    const float cy = SCREEN_H / 2.0f + d * ROW_PITCH;
    if (cy + 20 < b.top || cy - 20 > b.bot) return;
    float op;
    if (ad <= 1.0f)      op = 1.0f - 0.25f * ad;
    else if (ad <= 2.0f) op = 0.75f - 0.35f * (ad - 1.0f);
    else                 op = 0.4f * (3.0f - ad);
    const uint8_t a = (uint8_t)(op * 255.0f + 0.5f);
    if (!a) return;

    const bool sel = (i == target);
    const UiFont &f = sel ? UI_FONT_ROW_SEL : UI_FONT_ROW;
    const uint16_t col = sel ? COL_WHITE : COL_ROW;
    const float w = rowWidth(d * ROW_PITCH);
    if (w < 8.0f) return;
    const float x0 = SCREEN_W / 2.0f - w / 2.0f, x1 = SCREEN_W / 2.0f + w / 2.0f;

    char label[64];
    trackLabel((uint8_t)i, label, sizeof(label));
    const int32_t avail16 = (int32_t)(w * 16.0f);

    int32_t textW16 = UiDraw::textWidth16(f, label);
    float start;
    if (sel && textW16 > avail16) {
      start = x0 - (float)mqOff;               // overflowing: start-aligned and scrolled
    } else {
      UiDraw::fitEllipsis(f, label, sizeof(label), avail16);
      textW16 = UiDraw::textWidth16(f, label);
      start = SCREEN_W / 2.0f - (textW16 / 16.0f) / 2.0f;
    }

    const int baseline = (int)lroundf(cy + (f.ascent - f.descent) / 2.0f);
    const int cx0 = (int)ceilf(x0), cx1 = (int)floorf(x1) - 1;
    UiDraw::drawText(b, f, label, (int32_t)(start * 16.0f), baseline, col, a, cx0, cx1, true);
  }

  void drawTracks(const Band &b, int mqOff) {
    UiDraw::darken(b, BAR_Y0, BAR_Y1, BAR_KEEP);

    if (!count) {
      UiDraw::drawText(b, UI_FONT_ROW_SEL, "No tracks",
                       (SCREEN_W / 2) * 16 - UiDraw::textWidth16(UI_FONT_ROW_SEL, "No tracks") / 2,
                       (int)(SCREEN_H / 2 + (UI_FONT_ROW_SEL.ascent - UI_FONT_ROW_SEL.descent) / 2),
                       COL_WHITE, 255, 0, SCREEN_W - 1, true);
    } else {
      const int lo = (int)floorf(pos) - 3, hi = (int)ceilf(pos) + 3;
      for (int i = lo; i <= hi; i++) {
        if (i < 0 || i >= count) continue;
        drawRow(b, i, i == target ? mqOff : 0);
      }
      // Position on the left rim: a faint track and a short bright thumb.
      float frac = count > 1 ? pos / (float)(count - 1) : 0.0f;
      if (frac < 0) frac = 0;
      if (frac > 1) frac = 1;
      const float th = SCROLL_A1 - (SCROLL_A1 - SCROLL_A0) * frac;
      UiDraw::strokeArc(b, 120.0f, 120.0f, SCROLL_R, SCROLL_A0, SCROLL_A1, SCROLL_W, COL_WHITE, 51);
      UiDraw::strokeArc(b, 120.0f, 120.0f, SCROLL_R, fmaxf(SCROLL_A0, th - SCROLL_THUMB),
                        fminf(SCROLL_A1, th + SCROLL_THUMB), SCROLL_W, COL_WHITE, 255);
    }
    drawFooter(b);
  }

  // ---- Frame -------------------------------------------------------------------------
  // Everything a band needs, fixed for the whole frame (core 1 reads it).
  constexpr int ROWS  = UI_BAND_ROWS;
  constexpr int BANDS = SCREEN_H / ROWS;
  static_assert(SCREEN_H % ROWS == 0, "UI_BAND_ROWS must divide the screen");
  static_assert(2 * ROWS * SCREEN_W <= BAND_PIXELS, "two chooser bands must fit in the band buffer");
  Records frameRec;
  int     frameMq = 0;

  // The two halves of Display::bandBuffer().
  uint16_t *half(int i) { return Display::bandBuffer() + (i & 1) * ROWS * SCREEN_W; }

  void renderBand(int band, uint16_t *buf) {
    const Band b { buf, band * ROWS, band * ROWS + ROWS - 1 };
    Backdrop::renderRows(b.px, b.top, b.top, b.bot);
    if (trackMode) drawTracks(b, frameMq);
    else           drawPlaylists(b, frameRec);
  }

}

uint32_t init() {
  Backdrop::init();
  return MiniRecord::init();
}

void openPlaylists(uint8_t playlist) {
  trackMode = false;
  count     = PlaylistArt::playlistCount();
  if (count > UI_MAX_PLAYLISTS) count = UI_MAX_PLAYLISTS;
  target    = (count && playlist < count) ? playlist : 0;
  listOwner = (uint8_t)target;
  pos       = (float)target;
  sliding   = false;
  dirtyFull = true;
}

void openTracks(uint8_t playlist, uint8_t track) {
  trackMode = true;
  listOwner = playlist;
  count     = AudioPlayer::trackCount(playlist);

  int idx = (track > 0) ? track - 1 : 0;
  if (!count)            idx = 0;
  else if (idx >= count) idx = count - 1;

  target    = idx;
  pos       = (float)idx;
  sliding   = false;
  mqStart   = nowCache;
  mqDrawn   = -1;
  dirtyFull = true;
}

bool inTracks() { return trackMode; }

void scroll(int8_t delta, uint32_t nowMs) {
  if (!count) return;
  int32_t next = target + delta;
  if (trackMode) {
    if (next < 0)          next = 0;
    if (next > count - 1)  next = count - 1;
    if (next == target) return;
    mqStart = nowMs;
  }
  target = next;
  startSlide(target, nowMs);
}

uint8_t selectedIndex() { return (uint8_t)(trackMode ? target : wrap(target)); }
uint8_t itemCount()     { return count; }
uint8_t listPlaylist()  { return trackMode ? listOwner : (uint8_t)wrap(target); }

void refresh() { dirtyFull = true; }

void tick(uint32_t nowMs) {
  nowCache = nowMs;

  if (sliding) {
    const float t = (float)(nowMs - slideStart) / (float)slideMs;
    if (t >= 1.0f) {
      pos     = slideTo;
      sliding = false;
      // Keep the unwrapped arc index small.
      if (!trackMode && count && (target > 1000 || target < -1000)) {
        const int32_t w = wrap(target);
        pos += (float)(w - target);
        target = w;
      }
      if (trackMode) mqStart = nowMs;
    } else {
      pos = slideFrom + (slideTo - slideFrom) * easeOutCubic(t);
    }
    dirtyFull = true;
  }

  if (trackMode && !dirtyFull) {
    const int off = marqueeOffset(nowMs);
    if (off != mqDrawn) dirtyMarq = true;
  }
}

bool needsRedraw() { return dirtyFull || dirtyMarq; }

void draw() {
#if UI_TIMING_DEBUG
  const uint32_t t0 = micros();
  uint32_t tPush = 0;
#endif

  // The backdrop thumbnail cache is built here on core 0, before any band is
  // drawn. Record labels have no cache to build: MiniRecord::draw() samples
  // the art in flash directly.
  updateBackdrop();
  frameRec.n = 0;
  if (!trackMode) frameRec = layoutRecords();
  frameMq = marqueeOffset(nowCache);

  // A marquee step only changes the dark band, so only those rows are
  // rendered and pushed.
  const bool full = dirtyFull;
  const int y0 = full ? 0 : BAR_Y0;
  const int y1 = full ? SCREEN_H - 1 : BAR_Y1;
  const int first = y0 / ROWS, last = y1 / ROWS;

#if UI_DUAL_CORE
  // Core 1 draws band i + 1 into one half of the buffer while core 0 pushes
  // band i from the other. Messages are (band << 1) | half.
  rp2040.fifo.push((uint32_t)(first << 1));
  for (int bi = first; bi <= last; bi++) {
    const int h = (bi - first) & 1;
    rp2040.fifo.pop();                                       // band bi is drawn
    if (bi < last) rp2040.fifo.push((uint32_t)(((bi + 1) << 1) | (h ^ 1)));
#if UI_TIMING_DEBUG
    const uint32_t p0 = micros();
#endif
    const int top = bi * ROWS, bot = top + ROWS - 1;
    Display::pushRows(half(h), top, top > y0 ? top : y0, bot < y1 ? bot : y1);
#if UI_TIMING_DEBUG
    tPush += micros() - p0;
#endif
  }
#else
  for (int bi = first; bi <= last; bi++) {
    renderBand(bi, half(0));
#if UI_TIMING_DEBUG
    const uint32_t p0 = micros();
#endif
    const int top = bi * ROWS, bot = top + ROWS - 1;
    Display::pushRows(half(0), top, top > y0 ? top : y0, bot < y1 ? bot : y1);
#if UI_TIMING_DEBUG
    tPush += micros() - p0;
#endif
  }
#endif

  mqDrawn   = frameMq;
  dirtyFull = false;
  dirtyMarq = false;
#if UI_TIMING_DEBUG
  const uint32_t total = micros() - t0;
  Serial.printf("chooser %s frame: %lu us (push %lu)\n", full ? "full" : "band",
                (unsigned long)total, (unsigned long)tPush);
#endif
}

void core1Service() {
#if UI_DUAL_CORE
  // Sleep until core 0 queues a band (adding to the queue sends an event),
  // so the idle core does not spin and drain the battery.
  while (!rp2040.fifo.available()) __wfe();
  const uint32_t m = rp2040.fifo.pop();
  renderBand((int)(m >> 1), half((int)(m & 1)));
  rp2040.fifo.push(m);
#endif
}

}
