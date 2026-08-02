#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>
#include "anim_assets.h"

#define SWIDTH 128
#define SHEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// button pins
#define PET 6
#define WINK 8
#define MUSIC 11
#define DEBOUNCE_MS 40

// tuning
#define REST_FRAME 1
#define IDLE_GAP_MIN_MS 1500
#define IDLE_GAP_MAX_MS 4000
#define WINK_IDLE_CHANCE 12
#define AFF_WINDOW 4000
#define AFF2_THRESHOLD 3

Adafruit_SSD1306 display(SWIDTH, SHEIGHT, &Wire, OLED_RESET);

struct Button {
    uint8_t pin;
    bool stableState = HIGH;
    bool lastRaw = HIGH;
    unsigned long lastChangeMs = 0;

    Button(uint8_t p) : pin(p) {}

    bool poll() {
        bool raw = digitalRead(pin);
        if (raw != lastRaw) {
            lastChangeMs = millis();
            lastRaw = raw;
        }
        if ((millis() - lastChangeMs) > DEBOUNCE_MS && raw != stableState) {
            stableState = raw;
            if (stableState == LOW) {
                return true;
            }
        }
        return false;
    }
};

Button petButton { PET };
Button winkButton { WINK };
Button musicButton { MUSIC };

void decodeFrameToDisplayBuffer(uint8_t uniqueIndex) {
    const uint8_t *src = (const uint8_t*)pgm_read_word(&anim_unique[uniqueIndex]);
    uint8_t *buf = display.getBuffer();
    uint16_t si = 0, di = 0;
    while (di < ANIM_FRAME_BYTES) {
        uint8_t skip = pgm_read_byte(src + si++);
        if (skip) { memset(buf + di, 0, skip); di += skip; }
        uint8_t run = pgm_read_byte(src + si++);
        for (uint8_t k = 0; k < run; k++) buf[di++] = pgm_read_byte(src + si++);
    }
}

struct AnimPlayer {
    const uint8_t *seq = nullptr;
    uint8_t len = 0;
    uint8_t pos = 0;
    unsigned long lastFrameMs = 0;
    bool active = false;

    void start(const uint8_t *seqPtr, uint8_t seqLen) {
        seq = seqPtr;
        len = seqLen;
        pos = 0;
        active = (len > 0);
        if (active) {
            decodeFrameToDisplayBuffer(pgm_read_byte(&seq[0]));
            display.display();
            lastFrameMs = millis();
        }
    }

    bool isDone() const {
        return !active;
    }

    void update() {
        if (!active) return;
        if (millis() - lastFrameMs >= ANIM_FRAME_DELAY_MS) {
            pos++;
            if (pos >= len) {
                active = false;
                return;
            }
            decodeFrameToDisplayBuffer(pgm_read_byte(&seq[pos]));
            display.display();
            lastFrameMs = millis();
        }
    }
};

AnimPlayer player;

enum Mode : uint8_t {
    MODE_INTERACTIVE,
    MODE_IDLE,
    MODE_TRANSITION,
    MODE_MUSIC
};

Mode mode = MODE_IDLE;

bool chainToEmblem = false;
unsigned long nextIdleAt = 0;
uint8_t affectionCount = 0;
unsigned long lastAffectionAt = 0;

void goIdle() {
    mode = MODE_IDLE;
    decodeFrameToDisplayBuffer(REST_FRAME);
    display.display();
    nextIdleAt = millis() + random(IDLE_GAP_MIN_MS, IDLE_GAP_MAX_MS);
}

void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(A0));
    pinMode(PET, INPUT_PULLUP);
    pinMode(WINK, INPUT_PULLUP);
    pinMode(MUSIC, INPUT_PULLUP);

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;);
    }
    goIdle();
}

void loop() {
    bool petPressed = petButton.poll();
    bool winkPressed = winkButton.poll();
    bool musicPressed = musicButton.poll();

    player.update();

    if (mode == MODE_TRANSITION && player.isDone()) {
        if (chainToEmblem) {
            chainToEmblem = false;
            player.start(music_seq, MUSIC_SEQ_LEN);
        } else {
            mode = MODE_MUSIC;
        }
    }

    if (mode == MODE_INTERACTIVE && player.isDone()) {
        goIdle();
    }

    if (musicPressed) {
        if (mode == MODE_IDLE) {
            mode = MODE_TRANSITION;
            chainToEmblem = true;
            player.start(melody_seq, MELODY_SEQ_LEN);
        } else if (mode == MODE_MUSIC) {
            goIdle();
        }
    }

    if (mode == MODE_IDLE) {
        if (petPressed) {
            unsigned long now = millis();
            if (now - lastAffectionAt > AFF_WINDOW) affectionCount = 0;
            affectionCount++;
            lastAffectionAt = now;
            mode = MODE_INTERACTIVE;
            if (affectionCount >= AFF2_THRESHOLD) {
                player.start(aff1_seq, AFF1_SEQ_LEN);
                affectionCount = 0;
            } else {
                player.start(aff_seq, AFF_SEQ_LEN);
            }
        } else if (winkPressed) {
            mode = MODE_INTERACTIVE;
            player.start(wink_seq, WINK_SEQ_LEN);
        } else if (millis() >= nextIdleAt) {
            mode = MODE_INTERACTIVE;
            if (random(100) < WINK_IDLE_CHANCE) {
                player.start(wink_seq, WINK_SEQ_LEN);
            } else if (random(2) == 0) {
                player.start(blink_seq, BLINK_SEQ_LEN);
            } else {
                player.start(look_seq, LOOK_SEQ_LEN);
            }
        }
    }
}