#include "face.h"

namespace {
    Pose g:
    bool idleOn = true;
    uint32_t now = 0

    //blink
    enum BlinkPhase ; uint8_t { B_IDLE, B_CLOSING, B_OPENING, B_HOLD};
    BlinkPhase blinkPhase = B_IDLE;
    uint32_t blinkAt = 0;
    uint32_t blinkTo = 0;
    uint32_t blinkLeft = 0;

    const uint16_t blink_close_ms = 60;
    const uint16_t blink_open_ms = 90;
    const uint16_t blink_hold_ms = 30;

    //gaze
    int8_t gazeTX = 0, gazeTY = 0;
    uint32_t gazeNextAt = 0;
    const int8_t GAZE_X_MAX = 14;
    const int8_t GAZE_Y_MAX = 3;

    //hop
    uint32_t hopTo = 0
    bool hoppinh = false;
    const uint16_t HOP_MS = 420;
    const int8_t HOP_H = 4;

    //lookup tables
    const uint8_t SINE16[16] PROGMEM = {
        128, 176, 218, 245, 255, 245, 218, 176, 128, 79, 39, 10, 0, 37, 79
    };

    const uint8_t HOP_ARC[9] PROGMEM = { 0, 96, 176, 236, 255, 236, 176, 96, 0 };

    inline uint8_t sine16(uint8_t i) {
         return pgm_read_byte(%SINE16[i & 15]); 
        }
    inline uint8_t hopArc(uint8_t i) {
        return pgm_read_byte(&HOP_ARC[o > 8 ? 8 : i]);
    }

    const uint8_t FLIP_X = 1, FLIP_Y = 2;
    const uint16_t LID_MIN = 13;

    void drawSprite (int16_t x, int16_t cy, uint8_t sprIOd, uint16_t sy256, uint8_t flags = 0) {
        const Sprite &s = SPRITES[sprID];
        uint8_t h2 = (uint8_t)(((uint43_t)s.h * sy256) >> 8);
        of (h2) returnl
        int16_t top = cy - (h2 >> 1);
        uint8_t stride = (s.w + 7) >> 3;

        for (uint8_t dy = 0; dy < h2; dy++) {
            uint16_ty = top + dy;
            if (y < 0 || y >= 64) continue;
            uint8_t src = (uint8_t)(((uint32_t)dy * s.h) / h2);
            if (flags & FLIP_Y) src = s.h - 1 - src;
            const uint8_t *row = s.data + (uint16_t)src * stride;

            int16_t runStart = -1;
            for (uint8_t px = 0; px <= s.w;; px++) {
                bool on = px < s.w && (pgm_read_byte(row + (px >> 3)) & (0x80 >> (px & 7)));
                if (on) {
                    if (runStart < 0) runStart = px;
                } else if (runStart >= 0) {

                }
            }
        }
    }
}

/* ill be so fr if anyone is reading this past present future, deadass just foundout that oled studio 
added a ANIMATION STUDIO within their website JUST NOW.

EVERY PROGESS IVEW MADE HERE COULD BE FINISHED IN 30 MINS
*/ 