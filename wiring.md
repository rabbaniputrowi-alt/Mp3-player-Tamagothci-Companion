This file basically displays the basic wiring shceme for this project
couldnt find a good software to create wiring diagrams on with all my components so im writting this instead

# PROTOTYPE PARTS
- Arduino Nano
- DFPlayer mini
- OLED 0.96" screen
- JP-307 stereo 3.5mm jack barrel
- 2 pin tactile buttons (3x)
- jumper wires
- Breadboard
- 10k Resistor
- 470uF Capacitor

# WIRING

## DFPlayer
VCC -> 5V Rail
RX -> 10k Ohm Resistor -> D5
TX -> D6
DAC_R -> Ring (PJ_307)
DAC_L -> Tip (PJ-307)
GND -> GND Rail

## JP-307 Jack Barrel
P.S verify pins with multimeter on continuity mode
Ring -> DAC_R
Tip -> DAC_L
Sleeve -> GND Rail

## OLED 0.96" Screen
SDA -> A4 (Arduino Nano)
SCL -> A5 (Arduino Nano)
VCC -> 3.3V Rail
GND -> GND Rail

## Tactile Buttons
GND -> GND Rail
+ side -> D2, D3, D4

## Arduino Nano
All of the connected list above plus
GND ->  GND Rail
5V -> 5V Rail
3.3V -> 3.3v Rail