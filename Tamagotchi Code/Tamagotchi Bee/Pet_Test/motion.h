// motion.h -- turns raw MPU6050 samples into "is it being shaken" and
// "which way should the face look".
//
// No Arduino dependencies, so it can be host-compiled and tested. The sensor
// read itself lives in mpu6050.cpp.
//
// Everything here is in SCREEN axes, as seen by someone looking at the display:
//   +x = right, +y = up, +z = out of the screen toward the viewer.
#pragma once
#include <stdint.h>

namespace Motion {

struct Sample {
  float ax, ay, az;   // acceleration in g (lying face-up on a table reads az = +1)
  float gx, gy, gz;   // rotation rate in deg/s about x, y, z
};

// Forget everything; the next sample becomes the new "looking straight" pose.
void reset();

// Feed one sample. dt = seconds since the previous one.
void feed(const Sample& s, float dt);

// True while the device is being shaken (with hysteresis, so it does not
// flicker between the swings of a shake).
bool shaking();

// How far the face ball should be turned, in radians about the screen's
// x, y and z axes. All zero = face centred and upright.
//   rx > 0  face moves down      ry > 0  face moves right
//   rz > 0  face turns anticlockwise
void look(float& rx, float& ry, float& rz);

// Direction of gravity ("down") in screen axes, for the serial debug print.
void down(float& x, float& y, float& z);

}  // namespace Motion
