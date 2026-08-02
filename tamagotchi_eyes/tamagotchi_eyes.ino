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

//tuning
#define REST_FRAME 1
#define IDLE_GAP_MIN_MS 1500
#define IDLE_GAP_MAX_MS 4000
#define WINK_IDLE_CHANCE 12
#define AFF_WINDOW 4000
#define AFF2_THRESHOLD 3

Adafruit_SSD1306 display(SWIDTH, SHEIGHT, &Wire, OLED_RESET);

struct Button {
    
};

Button petButton = {PET};
Button winkButton = {WINK};
Button musicButton = {MUSIC};

void decodeFrameToDisplay(uint8_t uniqueIndex) {


    struct AnimPlayer {

    }

    void update() {

    }
}

AnimPlayer player;

enum Mode :

void goIdle() {

}

void setup() {

}

void loop () {

}

