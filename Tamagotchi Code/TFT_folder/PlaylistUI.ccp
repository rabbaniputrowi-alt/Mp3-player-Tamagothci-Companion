#include "playlist_ui.h"
#include "config.h"
#include "display.h"
#include "audio_player.h"
#include "playlist_art.h"
 
#include <TFT_eSPI.h>
#include <Fonts/GFXFF/FreeSansBold18pt7b.h>
#include <Fonts/GFXFF/FreeSansBold12pt7b.h>
 
#include <math.h>
#include <stdio.h>
#include <string.h>
 
namespace PlaylistUI {
 
namespace {
  const char *const PLAYLIST_NAMES[] = {
    "Hamilton",        // folder 01
    "Sweet Revenge",   // folder 02
    "bunii"            // folder 03
  };
  constexpr uint8_t PLAYLIST_NAME_COUNT =
      sizeof(PLAYLIST_NAMES) / sizeof(PLAYLIST_NAMES[0]);
 
  // ---- List state --------------------------------------------------------
  bool     trackMode  = false;   // false = playlist chooser, true = tracks
  uint8_t  listOwner  = 0;       // playlist the open list belongs to
  uint8_t  count      = 0;
  uint8_t  selected   = 0;
 
  float    pos        = 0.0f;
  float    slideFrom  = 0.0f;
  float    slideTo    = 0.0f;
  uint32_t slideStart = 0;
  bool     sliding    = false;
  bool     dirty      = true;
 
  uint8_t  halfChord[SCREEN_H];   // same chord table idea as disc_renderer,
                                  // kept local so disc_renderer stays untouched
 
  TFT_eSprite *strip = nullptr;   // one BAND_H-tall strip, reused 5x per frame
  bool         ready = false;
 
  //  Per-frame cache 

  const GFXfont *const FONT_BIG   = &FreeSansBold18pt7b;
  const GFXfont *const FONT_SMALL = &FreeSansBold12pt7b;
 
  struct Row {
    int16_t         cy;       // row center, panel coordinates
    uint16_t        color;    // color outside the selection band
    const GFXfont  *font;
    char            label[40];// already truncated to fit this row's chord
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
 
  // Returns 0 for "don't draw this row at all". Black text never appears
  // outside the selection band, so 0x0000 is free to act as the sentinel.
  uint16_t colorForDistance(float d) {
    const int r = (int)(d + 0.5f);
    if (r <= 1) return UI_COL_NEAR;
    if (r == 2) return UI_COL_FAR;
    return 0;
  }
 
  void labelFor(uint8_t index, char *out, size_t n) {
    if (trackMode) {
      // No track titles exist yet
      snprintf(out, n, "Track %u", (unsigned)(index + 1));
    } else if (index < PLAYLIST_NAME_COUNT) {
      snprintf(out, n, "%s", PLAYLIST_NAMES[index]);
    } else {
      snprintf(out, n, "Folder %02u", (unsigned)(index + 1));
    }
  }
 

  void fitToWidth(char *buf, size_t cap, int availW) {
    if (strip->textWidth(buf) <= availW) return;
 
    char work[48];
    size_t len = strlen(buf);
    while (len > 1) {
      len--;
      if (len >= sizeof(work) - 4) continue;
      memcpy(work, buf, len);
      work[len] = '\0';
      strcat(work, "...");
      if (strip->textWidth(work) <= availW) {
        snprintf(buf, cap, "%s", work);
        return;
      }
    }
    snprintf(buf, cap, "%s", "...");
  }
 
  void buildRows() {
    rowCount = 0;
    strip->setTextDatum(MC_DATUM);
 
    if (!count) {
      // Folder 03 has art but no mp3s copied over yet, and the module
      // reports it as empty. Say so rather than showing a bare band that
      // looks like the firmware hung.
      if (trackMode) {
        Row &r = rows[0];
        r.cy    = DISC_CY;
        r.color = UI_COL_NEAR;
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
      if (i < 0 || i >= (int)count) continue;    // past either end: not drawn
 
      const float    d   = fabsf((float)i - pos);
      const uint16_t col = colorForDistance(d);
      if (!col) continue;
 
      Row &r  = rows[rowCount];
      r.cy    = (int16_t)(DISC_CY + lroundf(((float)i - pos) * UI_ROW_PITCH));
      r.color = col;
 
      const int half  = chordAt(r.cy);
      const int avail = half * 2 - 2 * UI_TEXT_MARGIN;
      if (avail < 48) continue;                  // too little chord left to
                                                 // say anything — drop the row
                                                 // rather than render a stub
 
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
      strip->setTextColor(inverted ? UI_COL_SEL : r.color);
      strip->drawString(r.label, DISC_CX, r.cy - top);
    }
  }
 
} // anonymous namespace
 
void init() {
  for (int y = 0; y < SCREEN_H; y++) {
    const int dy = y - DISC_CY;
    const int v  = DISC_R * DISC_R - dy * dy;
    halfChord[y] = (v <= 0) ? 0 : (uint8_t)sqrtf((float)v);
  }

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
 
  // First call for this folder talks to the DFPlayer. Deliberately done
  count = AudioPlayer::trackCount(playlist);
 
  uint8_t idx = (track > 0) ? (uint8_t)(track - 1) : 0;
  if (!count)        idx = 0;
  else if (idx >= count) idx = (uint8_t)(count - 1);
 
  selected = idx;
  pos      = (float)idx;
  sliding  = false;
  dirty    = true;
}
 
void scroll(int8_t delta, uint32_t nowMs) {
  if (!count) return;
 
  int next = (int)selected + delta;
  if (next < 0)                next = 0;                 
  if (next > (int)count - 1)   next = (int)count - 1;   
  if ((uint8_t)next == selected && !sliding) return;
 
  selected   = (uint8_t)next;
  slideFrom  = pos;          // retarget from wherever the slide is now
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
  dirty = true;   // covers the final settling frame too
}
 
bool needsRedraw() { return sliding || dirty; }
 
void draw() {
  if (!ready) return;
 
  buildRows();
 
  for (uint8_t b = 0; b < NUM_BANDS; b++) {
    const int top = b * BAND_H;
    const int bot = top + BAND_H - 1;
 
    strip->fillSprite(UI_COL_BG);
 
    // Rails, clipped to the disc. Nothing special is needed to stop them
    // inside the selection band — the band is painted over them in the
    // same white a few lines below, exactly as in the mockup.
    {
      const int y0 = (UI_RAIL_Y0 > top) ? UI_RAIL_Y0 : top;
      const int y1 = (UI_RAIL_Y1 < bot) ? UI_RAIL_Y1 : bot;
      for (int y = y0; y <= y1; y++) {
        const int half = chordAt(y);
        if (!half) continue;
        if (DISC_CX - half <= UI_RAIL_LX) strip->drawPixel(UI_RAIL_LX, y - top, UI_COL_RAIL);
        if (DISC_CX + half >= UI_RAIL_RX) strip->drawPixel(UI_RAIL_RX, y - top, UI_COL_RAIL);
      }
    }
 
    paintRows(top, false);
 
    const int by0 = (UI_BAND_Y0 > top) ? UI_BAND_Y0 : top;
    const int by1 = (UI_BAND_Y1 < bot) ? UI_BAND_Y1 : bot;
    if (by0 <= by1) {
      for (int y = by0; y <= by1; y++) {
        const int half = chordAt(y);
        if (!half) continue;
        strip->drawFastHLine(DISC_CX - half, y - top, half * 2 + 1, UI_COL_BAND);
      }
 
      // vpDatum = false keeps draw coordinates in sprite space rather than
      // rebasing them on the viewport corner.
      strip->setViewport(0, by0 - top, SCREEN_W, by1 - by0 + 1, false);
      paintRows(top, true);
      strip->resetViewport();
    }
 
    strip->pushSprite(0, top);
  }
 
  dirty = false;
}
 
} // namespace PlaylistUI