#include "playlist_ui.h"
#include "config.h"
#include "display.h"
#include "audio_player.h"
#include "playlist_art.h"
#include "backdrop.h"

#include <TFT_eSPI.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace PlaylistUI {

namespace {

  const char *const PLAYLIST_NAMES[] = {
    "Hamilton",
    "Sweet Revenge",
    "bunii"
  };
  constexpr uint8_t PLAYLIST_NAME_COUNT =
      sizeof(PLAYLIST_NAMES) / sizeof(PLAYLIST_NAMES[0]);

  const GFXfont *const FONT_BIG   = &FreeSerifBoldItalic18pt7b;
  const GFXfont *const FONT_SMALL = &FreeSerifBoldItalic12pt7b;

  bool     trackMode  = false;
  uint8_t  listOwner  = 0;
  uint8_t  count      = 0;
  uint8_t  selected   = 0;

  float    pos        = 0.0f;
  float    slideFrom  = 0.0f;
  float    slideTo    = 0.0f;
  uint32_t slideStart = 0;
  bool     sliding    = false;
  bool     dirty      = true;

  uint8_t  halfChord[SCREEN_H];

  TFT_eSprite *strip = nullptr;
  bool         ready = false;

  struct Row {
    int16_t         cy;
    uint16_t        color;
    const GFXfont  *font;
    char            label[40];
  };
  Row     rows[2 * UI_VISIBLE_SPAN + 2];
  uint8_t rowCount = 0;

  float easeOutCubic(float t) {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
  }

  uint8_t chordAt(int y) {
    if (y < 0 || y >= SCREEN_H) return 0;
    return halfChord[y];
  }

  uint8_t artFor(int i) {
    return trackMode ? PlaylistArt::artIndexFor(listOwner, (uint8_t)(i + 1))
                     : PlaylistArt::artIndexFor((uint8_t)i, 1);
  }

  void updateBackdrop() {
    if (!count) {
      const uint8_t a = trackMode ? PlaylistArt::artIndexFor(listOwner, 1) : 0;
      Backdrop::setBlend(a, a, 0);
      return;
    }
    int i0 = (int)floorf(pos);
    if (i0 < 0) i0 = 0;
    if (i0 > (int)count - 1) i0 = (int)count - 1;
    const int i1 = (i0 + 1 < (int)count) ? i0 + 1 : i0;

    float f = pos - (float)i0;
    if (f < 0.0f) f = 0.0f;
    int t = (int)(f * 256.0f + 0.5f);
    if (t > 255) t = 255;

    Backdrop::setBlend(artFor(i0), artFor(i1), (uint8_t)t);
  }

  void labelFor(uint8_t index, char *out, size_t n) {
    if (trackMode) {
      snprintf(out, n, "Track %u", (unsigned)(index + 1));
    } else if (index < PLAYLIST_NAME_COUNT) {
      snprintf(out, n, "%s", PLAYLIST_NAMES[index]);
    } else {
      snprintf(out, n, "Folder %02u", (unsigned)(index + 1));
    }
  }

  void fitToWidth(char *buf, size_t cap, int availW) {
    if (strip->textWidth(buf) <= availW) return;

    char work[40];
    size_t len = strlen(buf);
    while (len > 1) {
      len--;
      if (len + 4 > sizeof(work) || len + 4 > cap) continue;
      memcpy(work, buf, len);
      memcpy(work + len, "...", 4);
      if (strip->textWidth(work) <= availW) {
        memcpy(buf, work, len + 4);
        return;
      }
    }
    if (cap >= 4) memcpy(buf, "...", 4);
  }

  void buildRows() {
    rowCount = 0;
    strip->setTextDatum(MC_DATUM);

    if (!count) {
      if (trackMode) {
        Row &r  = rows[0];
        r.cy    = DISC_CY;
        r.color = Backdrop::tint(DISC_CY, UI_TEXT_NEAR);
        r.font  = FONT_SMALL;
        snprintf(r.label, sizeof(r.label), "No tracks");
        rowCount = 1;
      }
      return;
    }

    const int first = (int)floorf(pos) - UI_VISIBLE_SPAN;
    const int last  = (int)ceilf(pos)  + UI_VISIBLE_SPAN;
    const uint8_t cap = (uint8_t)(sizeof(rows) / sizeof(rows[0]));

    for (int i = first; i <= last && rowCount < cap; i++) {
      if (i < 0 || i >= (int)count) continue;

      const float d    = fabsf((float)i - pos);
      const int   rank = (int)(d + 0.5f);
      if (rank > 2) continue;

      Row &r  = rows[rowCount];
      r.cy    = (int16_t)(DISC_CY + lroundf(((float)i - pos) * UI_ROW_PITCH));
      r.color = Backdrop::tint(r.cy, rank <= 1 ? UI_TEXT_NEAR : UI_TEXT_FAR);

      const int half  = chordAt(r.cy);
      const int avail = half * 2 - 2 * UI_TEXT_MARGIN;
      if (avail < 48) continue;

      labelFor((uint8_t)i, r.label, sizeof(r.label));

      r.font = (d < 0.5f) ? FONT_BIG : FONT_SMALL;
      strip->setFreeFont(r.font);
      if (r.font == FONT_BIG && strip->textWidth(r.label) > avail) {
        r.font = FONT_SMALL;
        strip->setFreeFont(r.font);
      }

      fitToWidth(r.label, sizeof(r.label), avail);
      rowCount++;
    }
  }

  void paintRows(int top, bool inverted) {
    const int bot = top + BAND_H - 1;

    for (uint8_t k = 0; k < rowCount; k++) {
      const Row &r = rows[k];
      if (r.cy + UI_GLYPH_HALF < top) continue;
      if (r.cy - UI_GLYPH_HALF > bot) continue;

      strip->setFreeFont(r.font);
      if (!inverted) {
        strip->setTextColor(UI_COL_SHADOW);
        strip->drawString(r.label, DISC_CX + 1, r.cy - top + 1);
      }
      strip->setTextColor(inverted ? UI_COL_SEL : r.color);
      strip->drawString(r.label, DISC_CX, r.cy - top);
    }
  }

}

void init() {
  for (int y = 0; y < SCREEN_H; y++) {
    const int dy = y - DISC_CY;
    const int v  = DISC_R * DISC_R - dy * dy;
    halfChord[y] = (v <= 0) ? 0 : (uint8_t)sqrtf((float)v);
  }

  Backdrop::init();

  strip = new TFT_eSprite(&Display::panel());
  strip->setColorDepth(16);
  ready = (strip->createSprite(SCREEN_W, BAND_H) != nullptr);

  if (ready) {
    strip->setFreeFont(FONT_BIG);
    strip->setTextDatum(MC_DATUM);
  }
}

void openPlaylists(uint8_t playlist) {
  trackMode = false;

  uint8_t n = PlaylistArt::playlistCount();
  if (n > PLAYLIST_NAME_COUNT) n = PLAYLIST_NAME_COUNT;
  count = n;

  selected  = (count && playlist < count) ? playlist : 0;
  listOwner = selected;
  pos       = (float)selected;
  sliding   = false;
  dirty     = true;
}

void openTracks(uint8_t playlist, uint8_t track) {
  trackMode = true;
  listOwner = playlist;
  count     = AudioPlayer::trackCount(playlist);

  uint8_t idx = (track > 0) ? (uint8_t)(track - 1) : 0;
  if (!count)            idx = 0;
  else if (idx >= count) idx = (uint8_t)(count - 1);

  selected = idx;
  pos      = (float)idx;
  sliding  = false;
  dirty    = true;
}

void scroll(int8_t delta, uint32_t nowMs) {
  if (!count) return;

  int next = (int)selected + delta;
  if (next < 0)              next = 0;
  if (next > (int)count - 1) next = (int)count - 1;
  if ((uint8_t)next == selected && !sliding) return;

  selected   = (uint8_t)next;
  slideFrom  = pos;
  slideTo    = (float)selected;
  slideStart = nowMs;
  sliding    = true;
  dirty      = true;
}

uint8_t selectedIndex() { return selected; }
uint8_t itemCount()     { return count; }
uint8_t listPlaylist()  { return trackMode ? listOwner : selected; }

void tick(uint32_t nowMs) {
  if (!sliding) return;

  const float t = (float)(nowMs - slideStart) / (float)UI_SLIDE_MS;
  if (t >= 1.0f) {
    pos     = slideTo;
    sliding = false;
  } else {
    pos = slideFrom + (slideTo - slideFrom) * easeOutCubic(t);
  }
  dirty = true;
}

bool needsRedraw() { return sliding || dirty; }

void draw() {
  if (!ready) return;

  updateBackdrop();
  buildRows();

  uint16_t *buf = (uint16_t *)strip->getPointer();

  for (uint8_t b = 0; b < NUM_BANDS; b++) {
    const int top = b * BAND_H;
    const int bot = top + BAND_H - 1;

    const int by0 = (UI_BAND_Y0 > top) ? UI_BAND_Y0 : top;
    const int by1 = (UI_BAND_Y1 < bot) ? UI_BAND_Y1 : bot;

    if (by0 <= by1) {
      if (top <= by0 - 1) Backdrop::renderRows(buf, top, top, by0 - 1);
      if (by1 + 1 <= bot) Backdrop::renderRows(buf, top, by1 + 1, bot);
      memset(buf + (by0 - top) * SCREEN_W, 0, (size_t)(by1 - by0 + 1) * SCREEN_W * sizeof(uint16_t));
    } else {
      Backdrop::renderRows(buf, top, top, bot);
    }

    paintRows(top, false);

    if (by0 <= by1) {
      for (int y = by0; y <= by1; y++) {
        const int half = chordAt(y);
        if (!half) continue;
        strip->drawFastHLine(DISC_CX - half, y - top, half * 2 + 1, UI_COL_BAND);
      }
      strip->setViewport(0, by0 - top, SCREEN_W, by1 - by0 + 1, false);
      paintRows(top, true);
      strip->resetViewport();
    }

    strip->pushSprite(0, top);
  }

  dirty = false;
}

}
