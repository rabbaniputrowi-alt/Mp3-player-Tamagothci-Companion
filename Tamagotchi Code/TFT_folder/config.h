#pragma once

#define TFT_SCLK_PIN   14
#define TFT_MOSI_PIN   15
#define TFT_CS_PIN      6
#define TFT_DC_PIN      7
#define TFT_RST_PIN     8

#define DFPLAYER_TX_PIN  28
#define DFPLAYER_RX_PIN  29

#define MPU6050_SDA_PIN   4
#define MPU6050_SCL_PIN   5

#define PIN_BTN_PREV   0   // short: previous track   | long: volume down
#define PIN_BTN_PLAY   1   // short: play/pause        | long: cycle spin speed
#define PIN_BTN_NEXT   2   // short: next track        | long: volume up
#define PIN_BTN_MENU   3   // short: enter playlist    | long: sleep

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