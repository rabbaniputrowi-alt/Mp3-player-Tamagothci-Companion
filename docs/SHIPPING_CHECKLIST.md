# Shipping checklist

What is finished, what needs the hardware in hand, and what only the owner can decide. Update it as things get done.

## Done

- [x] Music player firmware: disc view, chooser, backdrop, autoplay
- [x] Pet firmware: animations, tilt, shake, looping penguin
- [x] Both merged into one sketch that compiles (arduino-pico 6.3.1)
- [x] Priced BOM (BOM.csv) and notes (docs/BOM_NOTES.md): $59.20 for priced lines, four lines unpriced
- [x] README, wiring reference and TFT_eSPI setup example

## Needs hardware

### Merged firmware on the device

- [ ] Flash the merged build and confirm it boots to the pet view
- [ ] Tilt: the face follows the tilt. Up/down is confirmed. Left/right sign (`LOOK_SIGN_LR`) is not; confirm or flip it
- [ ] Shake: the pet gets dizzy and recovers
- [ ] Penguin: PLAY starts it, it loops, a second PLAY or MENU stops it
- [ ] Chooser: short MENU opens it from both views, MENU goes back
- [ ] Held MENU swaps between pet and disc view
- [ ] Autoplay: let a track end and check the next one starts (BUSY on GP27)
- [ ] Battery: the battery line appears on Serial and the value is plausible
- [ ] Volume is sane at the default (15 of 30) on headphones
- [ ] SD card has folders 01 to 07 with files named 001.mp3 and up, and every playlist plays
- [ ] Read the "Global variables use ..." line after the final build and check free heap at startup

### PCB

- [ ] Schematic has DFPlayer BUSY to GP27 through 10k
- [ ] Schematic has the battery divider (100k/100k) to GP26
- [ ] Schematic has the 1k in series on GP28 (DFPlayer RX)
- [ ] ERC clean in KiCad 10
- [ ] DRC clean in KiCad 10
- [ ] Confirm footprints for the right-angle side switches, the slide switch, the USB-C receptacle and the 8205A package (see docs/BOM_NOTES.md)
- [ ] Export Gerbers, BOM and CPL
- [ ] Order the board, then assemble and test power, charging and each peripheral

### Case (update for PCB v2)

- [ ] Holes for PREV, NEXT and MENU, and the POWER slot, reaching up to about z 11.7
- [ ] USB-C opening at about (-10.0, -33.2), z about 9.8
- [ ] Four M2 bosses or heat-set inserts at H1 to H4
- [ ] PLAY switch is about 5.3 mm tall, check the clearance
- [ ] NEXT switch clashes with the display by about 1 mm, resolve it
- [ ] Screen retention
- [ ] Screws: confirm length and insert size against the CAD
- [ ] Print and fit-check with the real board, battery and display

## Needs the owner

- [ ] Rewrite the "Why I built it" draft in the README in your own words
- [ ] Record real photos or renders and add them to the README
- [ ] Commit the KiCad project to `PCB/` and the case CAD to `CAD/`
- [ ] Choose a license (none chosen yet) and add a LICENSE file
- [ ] Replace the third-party album covers and the penguin gif with your own or properly licensed art
- [ ] Check the unpriced BOM lines (LiPo, PCB shipping, filament, order shipping and import) and re-check the $120 cap
- [ ] Update `CLAUDE.md`: its references to `MERGE_PLAN.md` now point to `docs/MERGE_PLAN.md`
