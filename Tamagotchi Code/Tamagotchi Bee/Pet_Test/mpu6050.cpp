// mpu6050.cpp -- raw register access, no library needed.
#include "mpu6050.h"
#include <Arduino.h>
#include <Wire.h>

namespace {

//  wiring 
const uint8_t PIN_SDA = 4;    // I2C0
const uint8_t PIN_SCL = 5;

//  mounting 
// Which sensor axis points along each screen axis, as printed on the GY-521
// silkscreen: 1 = X, 2 = Y, 3 = Z, negative = pointing the opposite way.
// Screen axes, looking at the display: RIGHT, UP, and OUT toward your face.
// Set for this build: the GY-521's X arrow points down the screen, Y points
// left, and the chip faces the back. Check with the serial print:
//   lying face-up on a table  -> down = ( 0,  0, -1)
//   standing upright          -> down = ( 0, -1,  0)
//   right edge pointing down  -> down = (+1,  0,  0)
const int8_t AXIS_RIGHT = -2;
const int8_t AXIS_UP    = -1;
const int8_t AXIS_OUT   = -3;

//  registers 
const uint8_t REG_SMPLRT_DIV   = 0x19;
const uint8_t REG_CONFIG       = 0x1A;
const uint8_t REG_GYRO_CONFIG  = 0x1B;
const uint8_t REG_ACCEL_CONFIG = 0x1C;
const uint8_t REG_ACCEL_XOUT_H = 0x3B;
const uint8_t REG_PWR_MGMT_1   = 0x6B;
const uint8_t REG_WHO_AM_I     = 0x75;

const float ACCEL_LSB_PER_G   = 8192.0f;   // +-4 g range
const float GYRO_LSB_PER_DPS  = 65.5f;     // +-500 deg/s range

uint8_t s_addr = 0x68;
uint8_t s_who = 0;
bool    s_busUp = false;

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(s_addr, (size_t)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

float pick(const float v[3], int8_t axis) {
  const float x = v[(axis < 0 ? -axis : axis) - 1];
  return axis < 0 ? -x : x;
}

}  // namespace

namespace Mpu6050 {

bool begin() {
  if (!s_busUp) {               // begin() is called again on every retry
    Wire.setSDA(PIN_SDA);
    Wire.setSCL(PIN_SCL);
    Wire.begin();
    Wire.setClock(400000);
    Wire.setTimeout(20);        // a loose wire must not freeze the animation
    s_busUp = true;
  }

  // AD0 low = 0x68, AD0 high = 0x69. Clones answer WHO_AM_I with all sorts
  // of values, so any reply at all counts as found.
  bool found = false;
  const uint8_t addresses[] = {0x68, 0x69};
  for (uint8_t addr : addresses) {
    s_addr = addr;
    if (readRegs(REG_WHO_AM_I, &s_who, 1)) {
      found = true;
      break;
    }
  }
  if (!found) return false;

  bool ok = writeReg(REG_PWR_MGMT_1, 0x01);     // wake, clock from gyro X
  delay(50);
  ok &= writeReg(REG_CONFIG, 0x03);             // ~44 Hz low-pass
  ok &= writeReg(REG_SMPLRT_DIV, 9);            // 100 Hz
  ok &= writeReg(REG_GYRO_CONFIG, 0x08);        // +-500 deg/s
  ok &= writeReg(REG_ACCEL_CONFIG, 0x08);       // +-4 g
  return ok;
}

bool read(Motion::Sample& out) {
  uint8_t b[14];
  if (!readRegs(REG_ACCEL_XOUT_H, b, 14)) return false;
  // Big-endian pairs: accel x y z, temperature, gyro x y z.
  auto be16 = [&](uint8_t i) { return (int16_t)((b[i] << 8) | b[i + 1]); };
  const float acc[3] = {be16(0) / ACCEL_LSB_PER_G, be16(2) / ACCEL_LSB_PER_G,
                        be16(4) / ACCEL_LSB_PER_G};
  const float gyr[3] = {be16(8) / GYRO_LSB_PER_DPS, be16(10) / GYRO_LSB_PER_DPS,
                        be16(12) / GYRO_LSB_PER_DPS};
  out.ax = pick(acc, AXIS_RIGHT);
  out.ay = pick(acc, AXIS_UP);
  out.az = pick(acc, AXIS_OUT);
  out.gx = pick(gyr, AXIS_RIGHT);
  out.gy = pick(gyr, AXIS_UP);
  out.gz = pick(gyr, AXIS_OUT);
  return true;
}

uint8_t whoAmI() { return s_who; }

}  // namespace Mpu6050
