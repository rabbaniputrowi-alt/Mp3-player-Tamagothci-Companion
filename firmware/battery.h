#pragma once
#include <stdint.h>

// Battery monitor: reads the LiPo voltage through the carrier PCB's divider
// on GP26 (ADC0) and keeps a smoothed value for the rest of the firmware.
//
// Data only. There is deliberately NO drawing / UI here and nothing in this
// module pauses the music or changes any view: the on-screen indicator (and
// what to do when the battery is low) is left for the user to decide later.
// firmware.ino only prints one Serial line every 10 s,
//   battery 3.87 V 63 %
// from loop() (never from a draw path). isLow() is there for that later UI.
//
// The ADC is read at 12 bits against the RP2040's 3.3 V reference. The pin
// sees half the battery voltage (100k / 100k divider), so volts() is the ADC
// voltage times BATTERY_DIVIDER_RATIO. All the numbers are named constants at
// the top of battery.cpp.
//
// Notes on accuracy: while the TP4056 is charging, the battery terminal sits
// above its resting voltage, so volts() reads high (up to ~4.2 V) and
// percent() is optimistic until charging stops. The ADC reference is the
// board's 3.3 V rail, so a few percent of error is normal; there is no
// calibration constant, add one if a multimeter shows a consistent offset.
namespace Battery {

  // Sets the ADC to 12 bits and takes a first reading, so volts() is valid
  // straight away. Call once in setup().
  void init();

  // Cheap; call every loop(). Samples about once per second (the average of 8
  // reads, then a low-pass filter), so most calls only compare millis().
  void service(uint32_t nowMs);

  // Filtered battery voltage in volts.
  float volts();

  // Rough charge, 0-100. A coarse estimate: it is a straight line between
  // 3.30 V (0 %) and 4.15 V (100 %), and LiPo discharge is not linear (flat
  // around 3.7 V, steep at both ends), so treat it as a gauge, not a measurement.
  uint8_t percent();

  // True once the filtered voltage drops below 3.40 V; clears again only
  // above 3.45 V (0.05 V hysteresis) so it does not flicker at the threshold.
  bool isLow();

}
