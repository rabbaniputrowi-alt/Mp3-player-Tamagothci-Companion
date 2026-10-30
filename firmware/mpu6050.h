// mpu6050.h -- reads the MPU6050 (GY-521) over I2C0 on GP4 (SDA) / GP5 (SCL)
// and hands back samples in screen axes, ready for Motion::feed().
#pragma once
#include "motion.h"

namespace Mpu6050 {

// Wake the sensor. Returns false if nothing answers; the sketch keeps
// running without tilt or shake in that case.
bool begin();

// Read one sample. Returns false on an I2C error.
bool read(Motion::Sample& out);

// WHO_AM_I value seen by begin() (0x68 on a real MPU6050; clones vary).
uint8_t whoAmI();

}  // namespace Mpu6050
