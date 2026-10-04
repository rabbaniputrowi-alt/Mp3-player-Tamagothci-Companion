// Idle:        pet rests, then plays BLINK, LOOKAROUND or (rarely) BUBBLE_FACE.
// Tilt:        the face follows how the device is held, as if printed on a ball.
// Shake:       shaken face while it lasts, dizzy once it stops.
// Buttons (fire on debounced release, same wiring as TFT_folder):
//   GP0 PREV  -> WINK
//   GP1 PLAY  -> MUSIC_SPIN
//   GP2 NEXT  -> WINK_TRANSITION (affection)
//   GP3 MENU  -> test button, steps through LOOKAROUND, BUBBLE_FACE and a
//                pretend 1.5 s shake (works without the MPU6050 wired up)
//
// Board: waveshare_rp2040_zero, arduino-pico core. Uses the same manually
// configured TFT_eSPI User_Setup.h as TFT_folder (GC9A01, SCL->GP15,
// SDA->GP14, CS->GP6, DC->GP7, RST->GP8). MPU6050 on I2C0, SDA->GP4, SCL->GP5.
// The pet data is ~1 MB: Tools > Flash Size must be "2MB (no FS)".

#include <TFT_eSPI.h>
#include "pet_anims.h"
#include "pet_renderer.h"
#include "pet.h"
#include "motion.h"
#include "mpu6050.h"

//  pins 
const uint8_t PIN_BTN_PREV = 0;
const uint8_t PIN_BTN_PLAY = 1;
const uint8_t PIN_BTN_NEXT = 2;
const uint8_t PIN_BTN_MENU = 3;

//  switches 
const bool TILT_FOLLOW  = true;    // false = face never moves with tilt
const bool SHAKE_DIZZY  = true;    // false = shaking is ignored
const bool MOTION_DEBUG = true;    // print tilt readings twice a second

//  timing 
const uint32_t DEBOUNCE_MS      = 25;
const uint32_t MOTION_PERIOD_MS = 10;     // sensor read rate (100 Hz)
const uint32_t MPU_RETRY_MS     = 3000;   // look for a missing sensor this often
const uint32_t FAKE_SHAKE_MS    = 1500;
const float    FOLLOW_EASE_S    = 0.15f;  // face glides to centre for other clips
const float    REDRAW_RAD       = 0.006f; // redraw once the face has turned ~0.35 deg

//  display 
// 24 rows x 240 px x 2 B = 11.5 KB of RAM, 10 bands per frame.
const uint16_t BAND_ROWS = 24;
static uint16_t bandBuf[PET_W * BAND_ROWS];
TFT_eSPI tft;

//  buttons 
struct Button {
  uint8_t pin;
  PetAnimId anim;    // PET_ANIM_COUNT = the MENU test button
  const char* label;
  bool raw;          // last raw read
  bool stable;       // debounced level (HIGH = released, pull-up)
  uint32_t changedAt;
};

Button buttons[] = {
  {PIN_BTN_PREV, PET_ANIM_WINK,            "PREV -> wink",       HIGH, HIGH, 0},
  {PIN_BTN_PLAY, PET_ANIM_MUSIC_SPIN,      "PLAY -> music spin", HIGH, HIGH, 0},
  {PIN_BTN_NEXT, PET_ANIM_WINK_TRANSITION, "NEXT -> affection",  HIGH, HIGH, 0},
  {PIN_BTN_MENU, PET_ANIM_COUNT,           "MENU",               HIGH, HIGH, 0},
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

//  motion 
bool     mpuOk = false;
uint32_t mpuNextTry = 0;
uint8_t  mpuFails = 0;
uint32_t lastMotionMs = 0;
uint32_t fakeShakeUntil = 0;
bool     fakeShake = false;
uint8_t  menuStep = 0;
float    follow = 0;                        // 0 = face centred, 1 = full tilt
float    ballX = 0, ballY = 0, ballZ = 0;   // what the face should be turned to
float    drawnX = 0, drawnY = 0, drawnZ = 0;  // what is on screen now

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
  if (mpuOk && TILT_FOLLOW) Motion::look(rx, ry, rz);
  // Clips that move the face themselves get it back in the centre.
  const float want = Pet::tiltAllowed() ? 1.0f : 0.0f;
  float k = dt / FOLLOW_EASE_S;
  if (k > 1) k = 1;
  follow += (want - follow) * k;
  ballX = rx * follow;
  ballY = ry * follow;
  ballZ = rz * follow;

  static uint32_t lastPrint = 0;
  if (MOTION_DEBUG && mpuOk && now - lastPrint >= 500) {
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
void drawFrame(uint16_t frame) {
  PetRenderer::setBall(ballX, ballY, ballZ);
  drawnX = ballX;
  drawnY = ballY;
  drawnZ = ballZ;
  PetRenderer::startFrame(frame);
  tft.startWrite();
  for (uint16_t y = 0; y < PET_H; y += BAND_ROWS) {
    uint16_t rows = (PET_H - y < BAND_ROWS) ? (PET_H - y) : BAND_ROWS;
    PetRenderer::renderBand(bandBuf, y, rows);
    tft.pushImage(0, y, PET_W, rows, bandBuf);
    updateMotion(millis());   // keep sampling at 100 Hz while a frame is drawn
  }
  tft.endWrite();
}

// MENU steps through the things the other buttons can't reach.
void menuPressed(uint32_t now) {
  switch (menuStep) {
    case 0:
      Serial.printf("MENU -> lookaround%s\n",
                    Pet::trigger(PET_ANIM_LOOKAROUND, now) ? "" : " (busy, ignored)");
      break;
    case 1:
      Serial.printf("MENU -> bubble face%s\n",
                    Pet::trigger(PET_ANIM_BUBBLE_FACE, now) ? "" : " (busy, ignored)");
      break;
    default:
      Serial.println("MENU -> pretend shake");
      fakeShake = true;
      fakeShakeUntil = now + FAKE_SHAKE_MS;
      break;
  }
  menuStep = (menuStep + 1) % 3;
}

void setup() {
  Serial.begin(115200);

  for (Button& b : buttons) pinMode(b.pin, INPUT_PULLUP);

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);   // pushImage byte-swaps on RP2040, see CLAUDE.md
  tft.fillScreen(TFT_BLACK);

  PetRenderer::init();
  tryMpu(millis());
  if (!mpuOk) Serial.println("MPU6050 not found: no tilt, no shake (MENU can fake a shake)");

  Pet::begin(millis(), micros() ^ analogRead(26));
  drawFrame(Pet::currentFrame());
}

void loop() {
  const uint32_t now = millis();

  for (Button& b : buttons) {
    if (!pollButton(b, now)) continue;
    if (b.anim == PET_ANIM_COUNT) {
      menuPressed(now);
    } else {
      bool ok = Pet::trigger(b.anim, now);
      Serial.printf("%s%s\n", b.label, ok ? "" : " (busy, ignored)");
    }
  }

  updateMotion(now);
  if (fakeShake && (int32_t)(now - fakeShakeUntil) >= 0) fakeShake = false;
  Pet::setShaking(fakeShake || (SHAKE_DIZZY && mpuOk && Motion::shaking()), now);

  const bool frameChanged = Pet::update(now);
  if (frameChanged || ballMoved()) {
    uint32_t t0 = micros();
    drawFrame(Pet::currentFrame());
    static uint32_t lastReport = 0;
    if (now - lastReport > 2000) {  // occasional draw-time readout
      lastReport = now;
      Serial.printf("frame draw %lu us\n", (unsigned long)(micros() - t0));
    }
  }
}
