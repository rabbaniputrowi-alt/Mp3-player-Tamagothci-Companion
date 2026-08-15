This file basically displays the basic wiring shceme for this project
couldnt find a good software to create wiring diagrams on with all my components so im writting this instead

# NEW PARTS
- Waveshare RP2040 Zero
- TFT Round display
- MPU 6050
- 2 pin tactile buttons (4x)
- DFPlayer
- JP-307 Stereo Jack Barrel
- Jumper Wires U
- Breadboard
- 3.7 Lip Battery 1200mAh
- Charging Module

# Wiring
## DFplayer
VCC -> 3.3v Rail
RX -> GPIO 28
TX -> GPIO 29
DAC_R -> Ring (PJ_307)
DAC_L -> Tip (PJ-307)
GND -> GND Rail

## JP-307 Jack Barrel
P.S verify pins with multimeter on continuity mode
Ring -> DAC_R
Tip -> DAC_L
Sleeve -> GND Rail

## TFT Round Display
RST -> GPIO 8
CS -> GPIO 6
DC -> GPIO 7
SDA -> GPIO 14
SCL -> GPIO 15
GND -> GND Rail
VCC -> 3v3 Rail

## MPU 6050
SDA -> GPIO 4
SCL -> GPIO 5
GND -> GND Rail
VCC -> 3v3 Rail

## Tactile Buttons
btn1 -> GPIO 0
btn2 -> GPIO 1
btn3 -> GPIO 2
btn4 -> GPIO 3

## Waveshare RP2040 Zero
- All of the GPIO pins cited above
- 3v3  -> +Rail = 3v3 Rail
= GND -> -Rail = GND Rail