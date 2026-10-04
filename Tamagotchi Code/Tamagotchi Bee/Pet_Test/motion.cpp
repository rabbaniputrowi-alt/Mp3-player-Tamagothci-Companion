// motion.cpp -- tilt tracking and shake detection, see motion.h.
#include "motion.h"
#include <math.h>

namespace {

//  tuning 
// Which way the face slides, set separately for the two tilt directions.
// +1: toward the raised side (the face stays put while the case turns
//     around it).
// -1: toward the lowered side, like a weight rolling downhill.
const float LOOK_SIGN_LR  = 1.0f;    // tilting left / right
const float LOOK_SIGN_UD  = -1.0f;   // tilting toward / away from you
const float LOOK_GAIN     = 1.0f;    // 1 = face turns exactly as far as the case tilts
const float LOOK_MAX_DEG  = 30.0f;   // furthest the face slides toward the rim
const float ROLL_GAIN     = 1.0f;    // 0 = never lean; 1 = stay level with the ground
const float ROLL_MAX_DEG  = 40.0f;
const float LOOK_SMOOTH_S = 0.08f;   // how lazily the face follows
// However you hold the device slowly becomes "looking straight". Seconds;
// 0 = never re-centre (the pose at power-up stays the reference).
const float RECENTER_S    = 6.0f;
const float ACC_TRUST_S   = 0.5f;    // how fast the accelerometer corrects gyro drift

const float SHAKE_G       = 0.55f;   // push/pull beyond gravity that counts as a shake
const float SHAKE_RISE_S  = 0.10f;
const float SHAKE_FALL_S  = 0.25f;
const float SHAKE_ON      = 0.60f;
const float SHAKE_OFF     = 0.20f;
// 

const float DEG = 0.01745329252f;

struct V3 { float x, y, z; };

V3 cross(const V3& a, const V3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float len(const V3& a) { return sqrtf(dot(a, a)); }
V3 unit(const V3& a) {
  float l = len(a);
  if (l < 1e-6f) return {0, 0, -1};
  return {a.x / l, a.y / l, a.z / l};
}
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool  s_ready = false;
V3    s_down = {0, 0, -1};   // gravity direction, gyro + accel fused
V3    s_base = {0, 0, -1};   // slow average of s_down = "looking straight"
float s_level = 0;           // 0..1, how shaky the last moments were
bool  s_shaking = false;
V3    s_look = {0, 0, 0};    // smoothed output, radians

}  // namespace

namespace Motion {

void reset() {
  s_ready = false;
  s_level = 0;
  s_shaking = false;
  s_look = {0, 0, 0};
}

void feed(const Sample& s, float dt) {
  dt = clampf(dt, 0.0005f, 0.1f);
  const V3 acc = {s.ax, s.ay, s.az};
  const float g = len(acc);
  // The accelerometer reads the push holding the device up, so "down" is
  // the opposite way.
  const V3 accDown = (g > 0.05f) ? V3{-s.ax / g, -s.ay / g, -s.az / g} : s_down;

  if (!s_ready) {
    s_down = accDown;
    s_base = accDown;
    s_ready = true;
  }

  // 1. Gyro carries the gravity direction through fast moves...
  const V3 w = {s.gx * DEG, s.gy * DEG, s.gz * DEG};
  const V3 turn = cross(s_down, w);
  s_down = {s_down.x + turn.x * dt, s_down.y + turn.y * dt, s_down.z + turn.z * dt};
  // 2. ...and the accelerometer pulls it back whenever it is reading about
  //    1 g (i.e. not being flung around).
  if (fabsf(g - 1.0f) < 0.25f) {
    const float k = clampf(dt / ACC_TRUST_S, 0, 1);
    s_down = {s_down.x + (accDown.x - s_down.x) * k,
              s_down.y + (accDown.y - s_down.y) * k,
              s_down.z + (accDown.z - s_down.z) * k};
  }
  s_down = unit(s_down);

  // Shake = acceleration that gravity does not explain, kept up for a while.
  // A single knock or putting it down is too short to count.
  const V3 extra = {s.ax + s_down.x, s.ay + s_down.y, s.az + s_down.z};
  const bool rough = len(extra) > SHAKE_G;
  const float tau = rough ? SHAKE_RISE_S : SHAKE_FALL_S;
  s_level += ((rough ? 1.0f : 0.0f) - s_level) * clampf(dt / tau, 0, 1);
  if (!s_shaking && s_level > SHAKE_ON) s_shaking = true;
  if (s_shaking && s_level < SHAKE_OFF) s_shaking = false;

  // Slowly accept the current pose as the new "straight ahead".
  if (RECENTER_S > 0 && !s_shaking) {
    const float k = clampf(dt / RECENTER_S, 0, 1);
    s_base = unit({s_base.x + (s_down.x - s_base.x) * k,
                   s_base.y + (s_down.y - s_base.y) * k,
                   s_base.z + (s_down.z - s_base.z) * k});
  }

  // The turn that takes the reference gravity to the current one is exactly
  // how a ball that stays still in the world moves relative to the case.
  const V3 c = cross(s_base, s_down);
  const float sn = len(c);
  const float angle = atan2f(sn, dot(s_base, s_down));
  V3 target = {0, 0, 0};
  if (sn > 1e-5f) {
    const float k = angle / sn;
    target = {c.x * k * LOOK_GAIN * LOOK_SIGN_UD, c.y * k * LOOK_GAIN * LOOK_SIGN_LR,
              c.z * k * ROLL_GAIN};
  }
  const float lookMag = sqrtf(target.x * target.x + target.y * target.y);
  const float lookMax = LOOK_MAX_DEG * DEG;
  if (lookMag > lookMax) {
    target.x *= lookMax / lookMag;
    target.y *= lookMax / lookMag;
  }
  target.z = clampf(target.z, -ROLL_MAX_DEG * DEG, ROLL_MAX_DEG * DEG);
  if (s_shaking) target = {0, 0, 0};

  const float k = clampf(dt / LOOK_SMOOTH_S, 0, 1);
  s_look = {s_look.x + (target.x - s_look.x) * k,
            s_look.y + (target.y - s_look.y) * k,
            s_look.z + (target.z - s_look.z) * k};
}

bool shaking() { return s_shaking; }

void look(float& rx, float& ry, float& rz) {
  rx = s_look.x;
  ry = s_look.y;
  rz = s_look.z;
}

void down(float& x, float& y, float& z) {
  x = s_down.x;
  y = s_down.y;
  z = s_down.z;
}

}  // namespace Motion
