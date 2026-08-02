#include "animation.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  Wire.begin();
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
}

void loop() {
  for (uint16_t i = 0; i < ANIM_FRAME_COUNT; i++) {
    display.clearDisplay();
    display.drawBitmap(0, 0, anim_frames[i], SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
    display.display();
    delay(pgm_read_word(&anim_delays_ms[i]));
  }
}
