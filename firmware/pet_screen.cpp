// pet_screen.cpp -- see pet_screen.h. Ported from Pet_Test.ino.
//
// Draws through Display::panel() (the one TFT_eSPI instance) into
// Display::bandBuffer() (the one band buffer). Display::init() already does
// the setSwapBytes(true) the pet relies on. MPU6050 on I2C0, SDA->GP4,
// SCL->GP5 (pins in config.h).
#include "pet_screen.h"
#include "config.h"
#include "display.h"
#include "pet_anims.h"
#include "pet_renderer.h"
#include "pet.h"
#include "motion.h"
#include "mpu6050.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>

// The pet frames are full screen, and a pet band must fit in the shared
// band buffer (SCREEN_W x BAND_H).
static_assert(PET_W == SCREEN_W && PET_H == SCREEN_H && PET_BAND_ROWS <= BAND_H,
              "pet frame / band size does not fit Display::bandBuffer()");

namespace PetScreen {

namespace {

  //  timing
  const uint32_t MOTION_PERIOD_MS = 10;     // sensor read rate (100 Hz)
  const uint32_t MPU_RETRY_MS     = 3000;   // look for a missing sensor this often
  const float    FOLLOW_EASE_S    = 0.15f;  // face glides to centre for other clips
  const float    REDRAW_RAD       = 0.006f; // redraw once the face has turned ~0.35 deg

  //  motion
  bool     mpuOk = false;
  uint32_t mpuNextTry = 0;
  uint8_t  mpuFails = 0;
  uint32_t lastMotionMs = 0;
  float    follow = 0;                        // 0 = face centred, 1 = full tilt
  float    ballX = 0, ballY = 0, ballZ = 0;   // what the face should be turned to
  float    drawnX = 0, drawnY = 0, drawnZ = 0;  // what is on screen now

  bool     forceRedraw = false;               // set by enter()

  //  leaving (see leaveWith() in pet_screen.h)
  PetAnimId leaveAnim = PET_ANIM_COUNT;       // PET_ANIM_COUNT = not leaving
  bool      leaveFinished = false;            // the leave clip has run through

  void tryMpu(uint32_t now) {
    mpuOk = Mpu6050::begin();
    mpuNextTry = now + MPU_RETRY_MS;
    mpuFails = 0;
    if (mpuOk) {
      Motion::reset();
      lastMotionMs = now;
      Serial.printf("MPU6050 found (WHO_AM_I 0x%02X)\n", Mpu6050::whoAmI());
    }
  }

  // Reads the sensor at 100 Hz and works out where the face should be.
  void updateMotion(uint32_t now) {
    const uint32_t elapsed = now - lastMotionMs;
    if (elapsed < MOTION_PERIOD_MS) return;
    lastMotionMs = now;
    const float dt = elapsed * 0.001f;

    if (!mpuOk && (int32_t)(now - mpuNextTry) >= 0) tryMpu(now);
    if (mpuOk) {
      Motion::Sample s;
      if (Mpu6050::read(s)) {
        mpuFails = 0;
        Motion::feed(s, dt);
      } else if (++mpuFails >= 5) {
        mpuOk = false;
        mpuNextTry = now + MPU_RETRY_MS;
        Motion::reset();
        Serial.println("MPU6050 stopped answering");
      }
    }

    float rx = 0, ry = 0, rz = 0;
    if (mpuOk && PET_TILT_FOLLOW) Motion::look(rx, ry, rz);
    // Clips that move the face themselves get it back in the centre.
    const float want = Pet::tiltAllowed() ? 1.0f : 0.0f;
    float k = dt / FOLLOW_EASE_S;
    if (k > 1) k = 1;
    follow += (want - follow) * k;
    ballX = rx * follow;
    ballY = ry * follow;
    ballZ = rz * follow;

    static uint32_t lastPrint = 0;
    if (PET_MOTION_DEBUG && mpuOk && now - lastPrint >= 500) {
      lastPrint = now;
      float dx, dy, dz;
      Motion::down(dx, dy, dz);
      Serial.printf("down %+.2f %+.2f %+.2f | look %+.0f %+.0f %+.0f deg%s\n",
                    dx, dy, dz, rx * 57.3f, ry * 57.3f, rz * 57.3f,
                    Motion::shaking() ? " | SHAKING" : "");
    }
  }

  bool ballMoved() {
    return fabsf(ballX - drawnX) > REDRAW_RAD || fabsf(ballY - drawnY) > REDRAW_RAD ||
           fabsf(ballZ - drawnZ) > REDRAW_RAD;
  }

  //  drawing
  // 24 rows per band, 10 bands per frame, into the shared band buffer (which
  // holds 48 rows). The smaller bands keep motion sampled at 100 Hz while a
  // frame is drawn.
  void drawFrame(uint16_t frame) {
    TFT_eSPI &tft = Display::panel();
    uint16_t *bandBuf = Display::bandBuffer();

    PetRenderer::setBall(ballX, ballY, ballZ);
    drawnX = ballX;
    drawnY = ballY;
    drawnZ = ballZ;
    PetRenderer::startFrame(frame);
    tft.startWrite();
    for (uint16_t y = 0; y < PET_H; y += PET_BAND_ROWS) {
      uint16_t rows = (PET_H - y < PET_BAND_ROWS) ? (PET_H - y) : PET_BAND_ROWS;
      PetRenderer::renderBand(bandBuf, y, rows);
      tft.pushImage(0, y, PET_W, rows, bandBuf);
      updateMotion(millis());   // keep sampling at 100 Hz while a frame is drawn
    }
    tft.endWrite();
  }

}  // namespace

void init(uint32_t nowMs) {
  PetRenderer::init();
  tryMpu(nowMs);
  if (!mpuOk) Serial.println("MPU6050 not found: no tilt, no shake");

  Pet::begin(nowMs, micros() ^ analogRead(26));
}

void enter(uint32_t nowMs) {
  // Whatever way the device is held right now becomes "straight".
  Motion::reset();
  follow = 0;
  ballX = ballY = ballZ = 0;
  // Don't let the time spent in another view count as one huge motion step.
  lastMotionMs = nowMs;
  forceRedraw = true;
  leaveAnim = PET_ANIM_COUNT;
  leaveFinished = false;
}

void leaveWith(PetAnimId id, uint32_t nowMs) {
  if (id >= PET_ANIM_COUNT) return;
  Pet::play(id, nowMs, true);
  leaveAnim = id;
  leaveFinished = false;
}

bool leaving()   { return leaveAnim != PET_ANIM_COUNT && !leaveFinished; }
bool leaveDone() { return leaveFinished; }

void tick(uint32_t nowMs) {
  // The leave clip is over: its last frame stays up until the caller has put
  // the next view on screen. Nothing to sample or draw until enter().
  if (leaveFinished) return;

  updateMotion(nowMs);
  // A leave clip runs to its end, so shaking does not get to cut it off. If
  // the device is still being shaken when the pet view comes back, the shake
  // starts then.
  const bool isLeaving = (leaveAnim != PET_ANIM_COUNT);
  Pet::setShaking(PET_SHAKE_DIZZY && mpuOk && Motion::shaking() && !isLeaving, nowMs);

  const bool frameChanged = Pet::update(nowMs);

  // While leaving nothing else can replace the clip (button triggers are
  // ignored during an interactive clip, shaking is held off above), so a
  // different animation here means it has run through and the pet is back on
  // its neutral face. Do not draw that face: it would flash up for a frame
  // between the end of the clip and the next view.
  if (isLeaving && Pet::currentAnim() != leaveAnim) {
    leaveFinished = true;
    return;
  }

  if (frameChanged || ballMoved() || forceRedraw) {
    forceRedraw = false;
    uint32_t t0 = micros();
    drawFrame(Pet::currentFrame());
    static uint32_t lastReport = 0;
    if (PET_MOTION_DEBUG && nowMs - lastReport > 2000) {  // occasional draw-time readout
      lastReport = nowMs;
      Serial.printf("frame draw %lu us\n", (unsigned long)(micros() - t0));
    }
  }
}

}  // namespace PetScreen
