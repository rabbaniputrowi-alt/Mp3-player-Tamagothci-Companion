#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

#define BTN_ONE   2
#define BTN_TWO   3
#define BTN_THREE 4
#define BUZZER    5

const uint8_t BUTTON_PINS[3]      = {BTN_ONE, BTN_TWO, BTN_THREE};
const char *const BUTTON_LABELS[3] = {"One", "Two", "Three"};


const unsigned int BUTTON_TONES[3] = {440, 660, 880}; // Hz: A4, E5, A5
const unsigned int BEEP_MS = 120;

const unsigned long DEBOUNCE_MS = 30;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

uint8_t lastReading[3]  = {HIGH, HIGH, HIGH};
uint8_t stableState[3]  = {HIGH, HIGH, HIGH};
unsigned long lastChange[3] = {0, 0, 0};

void showMessage(const char *msg) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 8);
  display.println(msg);
  display.display();
}

void setup() {
  Serial.begin(115200);

  for (uint8_t i = 0; i < 3; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  }

  pinMode(BUZZER, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  
  tone(BUZZER, 660, 100); //omg bru
  delay(150);
  tone(BUZZER, 990, 100);
  delay(150);

  showMessage("Hello");
}

void loop() {
  unsigned long now = millis();

  for (uint8_t i = 0; i < 3; i++) {
    uint8_t reading = digitalRead(BUTTON_PINS[i]);

    if (reading != lastReading[i]) {
      lastReading[i] = reading;
      lastChange[i] = now;
    }

    if (now - lastChange[i] >= DEBOUNCE_MS && reading != stableState[i]) {
      stableState[i] = reading;
      if (reading == LOW) { // press
        showMessage(BUTTON_LABELS[i]);

        tone(BUZZER, BUTTON_TONES[i], BEEP_MS);
      }
    }
  }
}
