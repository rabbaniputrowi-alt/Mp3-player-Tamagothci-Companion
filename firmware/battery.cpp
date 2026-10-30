#include "battery.h"
#include "config.h"
#include <Arduino.h>

namespace Battery {

namespace {
  // The carrier PCB has a 100k / 100k divider from the battery to the sense
  // pin, so the pin sees half the battery voltage.
  constexpr float BATTERY_DIVIDER_RATIO = 2.0f;

  // RP2040 ADC: 12-bit (set explicitly in init()), 3.3 V reference.
  constexpr float    ADC_REF_V = 3.3f;
  constexpr uint16_t ADC_MAX   = 4095;

  // 3.30 V = 0 %, 4.15 V = 100 %, linear. Coarse: see percent() in battery.h.
  constexpr float V_EMPTY = 3.30f;
  constexpr float V_FULL  = 4.15f;

  // Low-battery flag: set below V_LOW, cleared again above V_LOW + V_LOW_HYST.
  constexpr float V_LOW      = 3.40f;
  constexpr float V_LOW_HYST = 0.05f;

  constexpr uint32_t SAMPLE_MS      = 1000;   // one measurement per second
  constexpr uint8_t  SAMPLES        = 8;      // reads averaged per measurement
  constexpr float    FILTER_ALPHA   = 0.2f;   // weight of each new measurement

  float    filtered     = 0.0f;
  bool     low          = false;
  uint32_t lastSampleMs = 0;

  // Average of SAMPLES reads, as a battery voltage. The first read is thrown
  // away: the divider's source impedance is 50k, high for the ADC's sampling
  // capacitor, and the first read after a pause is the least settled.
  float measure() {
    analogRead(BATTERY_SENSE_PIN);
    uint32_t sum = 0;
    for (uint8_t i = 0; i < SAMPLES; i++) sum += analogRead(BATTERY_SENSE_PIN);
    const float raw = (float)sum / SAMPLES;
    return raw * ADC_REF_V / ADC_MAX * BATTERY_DIVIDER_RATIO;
  }

  void updateLow() {
    if (!low && filtered < V_LOW)                    low = true;
    else if (low && filtered >= V_LOW + V_LOW_HYST)  low = false;
  }
}

void init() {
  // arduino-pico's default is 10 bits; the maths above assumes 12. GP26 is
  // not used by anything else (the display is on GP14/15), and analogRead()
  // switches the pin to the ADC by itself.
  analogReadResolution(12);

  filtered     = measure();
  low          = false;
  lastSampleMs = millis();
  updateLow();
}

void service(uint32_t nowMs) {
  if (nowMs - lastSampleMs < SAMPLE_MS) return;
  lastSampleMs = nowMs;

  filtered += (measure() - filtered) * FILTER_ALPHA;
  updateLow();
}

float volts() {
  return filtered;
}

uint8_t percent() {
  float p = (filtered - V_EMPTY) / (V_FULL - V_EMPTY) * 100.0f;
  if (p < 0.0f)   p = 0.0f;
  if (p > 100.0f) p = 100.0f;
  return (uint8_t)(p + 0.5f);
}

bool isLow() {
  return low;
}

} // namespace Battery
