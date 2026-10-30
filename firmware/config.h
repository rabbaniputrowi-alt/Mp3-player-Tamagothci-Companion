#pragma once
#include <stdint.h>

#define TFT_SCLK_PIN   14
#define TFT_MOSI_PIN   15
#define TFT_CS_PIN      6
#define TFT_DC_PIN      7
#define TFT_RST_PIN     8

#define DFPLAYER_TX_PIN  28
#define DFPLAYER_RX_PIN  29
#define DFPLAYER_BUSY_PIN 27   // DFPlayer BUSY (LOW while a track plays), for autoplay.
                               // Wire it through a 10k resistor. Optional: if it is
                               // never seen LOW, autoplay ignores it.

#define BATTERY_SENSE_PIN 26   // ADC0. The carrier PCB has a 100k/100k divider from the
                               // battery, so the pin sees half the battery voltage
                               // (battery.cpp). GP26 is not used by the display (SPI1
                               // is on GP14/15), so it is free for this

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

#ifndef DISC_GROOVES
#define DISC_GROOVES  true  // the big disc gets the chooser's groove gradient too
                            // (screen-space, so it doesn't spin with the disc),
                            // added 2026-10-25. Not the fixed glare as well — see
                            // disc_renderer.cpp. false = plain album art, as before
#endif

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
#ifndef PET_MOTION_DEBUG
#define PET_MOTION_DEBUG  false   // true = print tilt readings twice a second and
                                  // the pet frame draw time every 2 s on Serial.
                                  // Off by default so the console stays quiet. Turn
                                  // it on with the build flag -D PET_MOTION_DEBUG=true
                                  // or by editing this default. It is also what
                                  // confirms the left / right tilt sign
                                  // (LOOK_SIGN_LR in motion.cpp) on hardware: tilt
                                  // the device left / right and check the face and
                                  // the second "look" value (left / right) agree
#endif

// Playlist / track chooser (playlist_ui.cpp), after the Claude Design mock
// "Vinyl & barrel". The mock is drawn on a 128 px canvas and the panel is
// 240 px, so its sizes are multiplied by 1.875 in the code.
//   Playlists ride an arc along the top: each one is a small record with its
//   cover as the label. Tracks roll on a barrel that narrows with the circle.
#define UI_MAX_PLAYLISTS     8      // audio_player.cpp caches track counts per playlist

#define UI_ARC_SLIDE_MS    260      // one step along the playlist arc
#define UI_ROW_SLIDE_MS    200      // one step of the track barrel

#define UI_RECORD_GROOVES  true     // fine grooves + two glare wedges on the small
                                    // records. They are the mock's own gradients
                                    // (a faint repeating radial ramp and a fixed
                                    // conic highlight), not drawn rings, and the
                                    // glare does not rotate. false = plain black.
#define UI_RECORD_SHADOW   true     // soft drop shadow under each record

#define UI_PUSH_ROUND      true     // push only the pixels inside the round glass
                                    // (about a fifth less SPI time per frame)
#ifndef UI_DUAL_CORE
#define UI_DUAL_CORE       true     // core 1 draws the next band while core 0 pushes
                                    // this one, so a frame takes about as long as the
                                    // slower of the two instead of both added up.
                                    // false = everything on core 0, same pictures
#endif
#define UI_BAND_ROWS         24     // chooser band height: two bands share
                                    // Display::bandBuffer() (48 rows)
#ifndef UI_TIMING_DEBUG
#define UI_TIMING_DEBUG    false    // true = print each chooser frame's time (and the
                                    // push part of it) on Serial. Off by default now
                                    // that it is tuned; switch it on with the build
                                    // flag -D UI_TIMING_DEBUG=true or by editing this
                                    // default
#endif

// Blurred album-art backdrop (backdrop.cpp). The art is drawn 1.25x larger
// than the screen, as in the mock, then blurred and saturated.
#define UI_BG_CELL           8      // thumbnail cell, px (power of two)
#define UI_BG_BLUR_PASSES    2      // 5-tap passes over the thumbnail
#define UI_BG_SATURATION   294      // 256 = unchanged, 294 = the mock's saturate(1.15)
