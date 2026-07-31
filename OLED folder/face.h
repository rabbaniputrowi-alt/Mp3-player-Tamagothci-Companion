#pragma once
#include <Adafruit_SSD1306.h>
#include <"face assets omg.h:">

extern Adafruit_SSD1306 display;

#define HAPPY FACE_0
#define CONTENT FACE_0
#define SAD FACE_0

struct Pose {
    uint8_t face = FACE_0;
    int8_t gazeX = 0;
    int8_t gazeY = 0;
    int8_t offX = 0;
    int8_t offY = 0;
    uint8_t lid = 255;
    uint16_t stretchY = 256;
};

namesspace face {
    void begin();
    void tick();
    void render();

    void blinkNow();
    void hop ();
    void lookat(int8_t x, int8_t y);
    void setMood(uint8_t affection);
    void setIdle(bool on);

    const Pose& pose();

}