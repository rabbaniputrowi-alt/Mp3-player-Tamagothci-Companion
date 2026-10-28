# Wiring

Wiring reference for the breadboard prototype and the carrier PCB (v2). I could not find a good tool for a wiring diagram with all these parts, so this is a table.

The earlier version of this file had the display SDA and SCL swapped. The working wiring is SCL (clock) on GP14 and SDA (data) on GP15.

Where the breadboard and the PCB differ, the note says so. Verify jack pins with a multimeter in continuity mode.

| Signal | Pin | Note |
|---|---|---|
| Display SCL (SPI clock) | GP14 | Module is labelled with I2C names, but it is SPI. Clock goes here. |
| Display SDA (SPI data) | GP15 | Data line. SPI1 can only take SCK on GP10/14/26 and TX on GP11/15/27, so these two cannot be swapped. |
| Display CS | GP6 | |
| Display DC | GP7 | |
| Display RST | GP8 | |
| Display VCC | 3V3 | |
| Display GND | GND | No backlight pin on this module. |
| DFPlayer RX | GP28 (MCU TX) | Through a 1k series resistor. |
| DFPlayer TX | GP29 (MCU RX) | UART0 = Serial1. |
| DFPlayer BUSY | GP27 | Through a 10k resistor. LOW while playing. Used for autoplay. |
| DFPlayer VCC | 3.3 V rail (breadboard), VSYS (PCB) | The PCB design powers it from VSYS. |
| DFPlayer GND | GND | |
| DFPlayer DAC_L | Jack tip | |
| DFPlayer DAC_R | Jack ring | |
| Jack sleeve (PJ-307) | GND | Three-conductor TRS, no mic line. |
| MPU6050 SDA | GP4 | I2C0 |
| MPU6050 SCL | GP5 | I2C0 |
| MPU6050 VCC | 3V3 | |
| MPU6050 GND | GND | |
| Button PREV | GP0 | Other leg to GND. INPUT_PULLUP, active low. |
| Button PLAY | GP1 | Same. |
| Button NEXT | GP2 | Same. |
| Button MENU | GP3 | Same. |
| Battery sense | GP26 (ADC) | PCB design: 100k/100k divider from the battery supply to GND, midpoint to GP26, so the pin sees half the battery voltage. Check the tap point against the schematic. The old breadboard notes do not record a divider. |
| Battery | VSYS | Not 3V3. PCB: LiPo through the TP4056 charger (USB-C), DW01A + FS8205A protection and an SS14 diode into VSYS. Breadboard: a separate charging module. |

GP9 to GP13 are not reachable on the RP2040-Zero and are not used.

## Parts on the breadboard

- Waveshare RP2040-Zero
- GC9A01 round display
- MPU6050 (GY-521)
- 4 tactile buttons
- DFPlayer Mini
- PJ-307 stereo jack
- 3.7 V 1200 mAh LiPo and a charging module
- Breadboard and jumper wires
