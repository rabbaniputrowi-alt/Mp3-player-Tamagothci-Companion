#pragma once
#include <stdint.h>

#define TFT_SCLK_PIN   14
#define TFT_MOSI_PIN   15
#define TFT_CS_PIN      6
#define TFT_DC_PIN      7
#define TFT_RST_PIN     8

#define DFPLAYER_TX_PIN  28
#define DFPLAYER_RX_PIN  29

#define MPU6050_SDA_PIN   4
#define MPU6050_SCL_PIN   5

#define PIN_BTN_PREV   0   // pet/playing: previous | select: scroll up
#define PIN_BTN_PLAY   1   // pet/playing: play/pause | select: confirm
#define PIN_BTN_NEXT   2   // pet/playing: next | select: scroll down
#define PIN_BTN_MENU   3   // short: open chooser / back | held: swap pet <-> disc

#define LONG_PRESS_MS 500  // MENU held this long = swap views instead of chooser

#define SCREEN_W      240
#define SCREEN_H      240
#define DISC_CX       120
#define DISC_CY       120
#define DISC_R        118   // slightly inside 240px edge — keeps source index in range

#define ART_W         240              // source art edge, square
#define ART_PIXELS    (ART_W * ART_W)  // 57600 — indexed pixels, 1 byte each
#define PALETTE_SIZE  256

#define BAND_H         48              // 240 / 5 bands per frame
#define BAND_PIXELS   (SCREEN_W * BAND_H)
#define NUM_BANDS     (SCREEN_H / BAND_H)

enum class AppState : uint8_t {
  PET = 0,          // the pet view
  PLAYING,          // the spinning disc view
  PLAYLIST_SELECT,
  TRACK_SELECT
};

// Pet view. Draws in PET_BAND_ROWS-row bands (10 per frame) into the shared
// Display::bandBuffer(), sampling the MPU6050 between bands so motion keeps
// its 100 Hz rate while a frame is drawn.
#define PET_BAND_ROWS     24
#define PET_TILT_FOLLOW   true    // false = face never moves with tilt
#define PET_SHAKE_DIZZY   true    // false = shaking is ignored
#define PET_MOTION_DEBUG  true    // print tilt readings twice a second

#define UI_ROW_PITCH      43
#define UI_BAND_Y0        98
#define UI_BAND_Y1       142
#define UI_VISIBLE_SPAN    2
#define UI_TEXT_MARGIN     8
#define UI_GLYPH_HALF     22
#define UI_SLIDE_MS      200
#define UI_MAX_PLAYLISTS   8

#define UI_COL_BAND   0xEF7D
#define UI_COL_SEL    0x0000
#define UI_COL_SHADOW 0x0000

#define UI_TEXT_NEAR       205
#define UI_TEXT_FAR        140

#define UI_BG_CELL           4
#define UI_BG_BLUR_PASSES    1
#define UI_BG_TARGET_LUMA   84
#define UI_BG_DIM          200
#define UI_BG_SATURATION   384
