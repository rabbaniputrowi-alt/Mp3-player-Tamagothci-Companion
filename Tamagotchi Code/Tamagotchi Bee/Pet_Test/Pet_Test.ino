// Idle:        pet rests, then plays BLINK or LOOKAROUND on its own.
// Buttons (fire on debounced release, same wiring as TFT_folder):
//   GP0 PREV  -> WINK
//   GP1 PLAY  -> MUSIC_SPIN
//   GP2 NEXT  -> WINK_TRANSITION (affection)
//   GP3 MENU  -> LOOKAROUND (handy for testing the idle clip on demand)
//
// Board: waveshare_rp2040_zero, arduino-pico core. Uses the same manually
// configured TFT_eSPI User_Setup.h as TFT_folder (GC9A01, SCL->GP15,
// SDA->GP14, CS->GP6, DC->GP7, RST->GP8).

#include <TFT_eSPI.h>
#include "pet_anims.h"
#include "pet_renderer.h"
#include "pet.h"

//  pins 
const uint8_t PIN_BTN_PREV = 0;
const uint8_t PIN_BTN_PLAY = 1;
const uint8_t PIN_BTN_NEXT = 2;
const uint8_t PIN_BTN_MENU = 3;

//  timing 
const uint32_t DEBOUNCE_MS = 25;

//  display 
// 24 rows x 240 px x 2 B = 11.5 KB of RAM, 10 bands per frame.
const uint16_t BAND_ROWS = 24;
static uint16_t bandBuf[PET_W * BAND_ROWS];
TFT_eSPI tft;

//  buttons 
struct Button {
  uint8_t pin;
  PetAnimId anim;
  const char* label;
  bool raw;          // last raw read
  bool stable;       // debounced level (HIGH = released, pull-up)
  uint32_t changedAt;
};

Button buttons[] = {
  {PIN_BTN_PREV, PET_ANIM_WINK,            "PREV -> wink",       HIGH, HIGH, 0},
  {PIN_BTN_PLAY, PET_ANIM_MUSIC_SPIN,      "PLAY -> music spin", HIGH, HIGH, 0},
  {PIN_BTN_NEXT, PET_ANIM_WINK_TRANSITION, "NEXT -> affection",  HIGH, HIGH, 0},
  {PIN_BTN_MENU, PET_ANIM_LOOKAROUND,      "MENU -> lookaround", HIGH, HIGH, 0},
};

// Returns true once per physical press, on the debounced release edge.
bool pollButton(Button& b, uint32_t now) {
  bool r = digitalRead(b.pin);
  if (r != b.raw) {
    b.raw = r;
    b.changedAt = now;
  }
  if (r != b.stable && now - b.changedAt >= DEBOUNCE_MS) {
    b.stable = r;
    return r == HIGH;  // released
  }
  return false;
}

// ---- drawing 
void drawFrame(uint16_t frame) {
  PetRenderer::startFrame(frame);
  tft.startWrite();
  for (uint16_t y = 0; y < PET_H; y += BAND_ROWS) {
    uint16_t rows = (PET_H - y < BAND_ROWS) ? (PET_H - y) : BAND_ROWS;
    PetRenderer::fillPixels(bandBuf, (uint32_t)PET_W * rows);
    tft.pushImage(0, y, PET_W, rows, bandBuf);
  }
  tft.endWrite();
}

void setup() {
  Serial.begin(115200);

  for (Button& b : buttons) pinMode(b.pin, INPUT_PULLUP);

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);   // pushImage byte-swaps on RP2040, see CLAUDE.md
  tft.fillScreen(TFT_BLACK);

  Pet::begin(millis(), micros() ^ analogRead(26));
  drawFrame(Pet::currentFrame());
}

void loop() {
  const uint32_t now = millis();

  for (Button& b : buttons) {
    if (pollButton(b, now)) {
      bool ok = Pet::trigger(b.anim, now);
      Serial.printf("%s%s\n", b.label, ok ? "" : " (busy, ignored)");
    }
  }

  if (Pet::update(now)) {
    uint32_t t0 = micros();
    drawFrame(Pet::currentFrame());
    static uint32_t lastReport = 0;
    if (now - lastReport > 2000) {  // occasional draw-time readout
      lastReport = now;
      Serial.printf("frame draw %lu us\n", (unsigned long)(micros() - t0));
    }
  }
}
