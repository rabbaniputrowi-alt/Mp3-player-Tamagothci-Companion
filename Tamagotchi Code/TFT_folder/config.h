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

#define PIN_BTN_PREV   0   // playing: previous | select: scroll up
#define PIN_BTN_PLAY   1   // playing: play/pause | select: confirm
#define PIN_BTN_NEXT   2   // playing: next | select: scroll down
#define PIN_BTN_MENU   3   // playing: open chooser | select: back

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
  PLAYING = 0,
  PLAYLIST_SELECT,
  TRACK_SELECT
};

#define ROW_PITCH      43
#define BAND_Y0        98
#define BAND_Y1       142
#define VISIBLE_SPAN    2
#define TEXT_MARGIN     8
#define GLYPH_HALF     22
#define SLIDE_MS      200
#define MAX_PLAYLISTS   8

#define COL_BAND   0xEF7D
#define COL_SEL    0x0000
#define COL_SHADOW 0x0000

#define TEXT_NEAR       205
#define TEXT_FAR        140

#define BG_CELL           4
#define BG_BLUR_PASSES    1
#define BG_TARGET_LUMA   84
#define BG_DIM          200
#define BG_SATURATION   384
