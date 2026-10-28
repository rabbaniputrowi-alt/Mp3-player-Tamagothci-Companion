# Stardance

A star-shaped, Y2K-style handheld that is two things at once: an MP3 player with a spinning album-art disc, and a Tamagotchi-style pet that lives on a round screen and reacts when you tilt or shake it.

No photo or render is committed yet. See "Hardware files" and "Known limitations" below for what exists and what does not.

## Why I built it

<!-- DRAFT: rewrite in your own words before publishing -->
I wanted a small Y2K star, something like a keychain, that plays my music and also has a pet living on its screen. I could not buy that, so I designed the hardware and wrote the firmware myself. I work in small steps and test each one on the device before moving on. This build is funded and capped at $120 in parts.

## Features

- Plays MP3s from a microSD card, organised as playlists (one folder per album)
- Album cover rendered as a spinning disc, full screen, with spin-up and spin-down easing
- Playlist and track chooser over a blurred backdrop of the album art
- Animated pet with idle clips, interactive clips and a "face on a ball" view that follows the tilt of the device
- Shake it and the pet gets dizzy
- A dancing penguin mode (press PLAY in the pet view)
- Autoplay: the next track starts by itself when one ends
- 3.5 mm headphone output
- USB-C charging for a 1200 mAh LiPo (on the PCB design)

## Hardware

| Part | Notes |
|---|---|
| MCU | Waveshare RP2040-Zero |
| Display | GC9A01 1.28" round TFT, 240x240, SPI |
| Audio | DFPlayer Mini (clone) with microSD slot |
| Motion | MPU6050 (GY-521) accelerometer and gyro |
| Output | PJ-307 3.5 mm TRS jack |
| Input | 4 tactile buttons |
| Power | 3.7 V 1200 mAh LiPo (40x30x10 mm), on-board TP4056 charger, DW01A + FS8205A protection, SS14 into VSYS, USB-C |

Parts list: [BOM.csv](BOM.csv). Pricing notes and open questions: [docs/BOM_NOTES.md](docs/BOM_NOTES.md).

The priced lines add up to $59.20 for one device, under the $120 cap. Four lines are unpriced (the LiPo, PCB shipping, case filament, and order shipping and import duties), so the cap check is approximate. Several priced lines are substitutes or upper bounds, which BOM_NOTES.md lists.

### Pinout

| Signal | RP2040-Zero pin |
|---|---|
| Display SCK (labelled SCL on the module) | GP14 |
| Display MOSI (labelled SDA on the module) | GP15 |
| Display CS | GP6 |
| Display DC | GP7 |
| Display RST | GP8 |
| DFPlayer RX (via 1k resistor) | GP28 (MCU TX) |
| DFPlayer TX | GP29 (MCU RX) |
| DFPlayer BUSY (via 10k resistor, used for autoplay) | GP27 |
| MPU6050 SDA | GP4 |
| MPU6050 SCL | GP5 |
| Button PREV | GP0 |
| Button PLAY | GP1 |
| Button NEXT | GP2 |
| Button MENU | GP3 |
| Battery sense (ADC, through a 100k/100k divider) | GP26 |
| Battery in | VSYS (not 3V3) |

The display module borrows I2C names for its SPI pins, so SCL is the clock and SDA is the data line. Buttons are wired to ground and use `INPUT_PULLUP`, so they read low when pressed. GP9 to GP13 are not reachable on this board and are not used.

On the PCB design the DFPlayer VCC is on VSYS. The old breadboard used the 3.3 V rail. The full wiring reference for both is in [wiring.md](wiring.md).

## Hardware files

The KiCad project and the case CAD are not committed yet. When they are added, they go in these folders:

- `PCB/` for the KiCad carrier board, footprints and fabrication exports
- `CAD/` for the case source files, exports and renders

Until then there is no schematic image, PCB image or case render in this repo.

## Repository layout

```
Stardance/          the firmware sketch (Stardance.ino and all .cpp/.h files, flat)
tools/
  Spinning Disc/    album art and track name generators
  Pet_Pipeline/     pet animation generator and previewer
  UI_Fonts/         chooser font generator and the font files
  previews/         pet preview gifs and contact sheets
docs/               BOM notes, merge plan, TFT_eSPI setup example, shipping checklist
Archive/            old code, kept for reference only
BOM.csv             bill of materials
wiring.md           wiring reference (breadboard and PCB)
```

Not in the repo yet: `PCB/` and `CAD/` (see "Hardware files").

## Building the firmware

Tested with arduino-pico 6.3.1.

1. Install the Arduino IDE and the [arduino-pico](https://github.com/earlephilhower/arduino-pico) core by Earle Philhower (6.3.1).
2. Board: `Waveshare RP2040 Zero`. Flash Size: `2MB (no FS)`.
3. Install the libraries:
   - `TFT_eSPI` 2.5.43
   - `DFRobotDFPlayerMini` 1.0.6
4. Edit `User_Setup.h` in `Documents/Arduino/libraries/TFT_eSPI`. [docs/User_Setup.example.h](docs/User_Setup.example.h) is a starting point. It is a reconstruction from the project notes, not a copy of the working file, and it leaves SPI_FREQUENCY open. The settings that matter:
   ```c
   #define GC9A01_DRIVER
   #define TFT_SPI_PORT 1
   #define TFT_SCLK 14
   #define TFT_MOSI 15
   #define TFT_CS   6
   #define TFT_DC   7
   #define TFT_RST  8
   ```
5. Open `Stardance/Stardance.ino` and upload.

If a change to `User_Setup.h` does not seem to take effect, delete the cached build in `AppData\Local\arduino\sketches\` and compile again.

Python is only needed to regenerate the album, track, pet and font data: `pip install pillow numpy imageio-ffmpeg` (and `fonttools brotli` for the fonts).

## Preparing the SD card

The DFPlayer only understands exact names:

```
01/001.mp3
01/002.mp3
02/001.mp3
...
```

Folders `01` to `99`, files `001.mp3` to `255.mp3`, no extra text in either name. Each folder is one playlist. The firmware currently knows seven albums, folders `01` to `07`. The music itself is not in this repo.

## Adding an album

Run the generators from the folder that holds their json, with `--out` pointing at the `Stardance` sketch folder.

1. Add the cover image and an entry in `albums.json`, then run
   `python build_albums.py --manifest albums.json --out <sketch>`
2. Add a row for the new folder to `PLAYLIST_ART[]` in `playlist_art.cpp`
3. Add the folder to `tracks.json`, then run
   `python build_tracks.py --manifest tracks.json --out <sketch>`
   (or add `--scan <SD card drive>` to read titles off the card)

If you skip step 2 the album never shows up in the chooser.

## Controls

Each view owns PREV, PLAY and NEXT. In the pet view the music is not touched.

| Button | Disc view | Pet view |
|---|---|---|
| PREV | previous track | pet winks |
| PLAY | play/pause | starts or stops the penguin dance |
| NEXT | next track | affection wink |
| MENU (short) | open the playlist chooser | open the playlist chooser |
| MENU (hold) | switch to pet view | switch to disc view |

In the chooser, PREV and NEXT turn the list, PLAY opens a playlist or plays a track, and MENU goes back. When a track ends, the next one in the playlist starts by itself (autoplay).

The device boots into the pet view.

## PCB and case

The carrier PCB (version 2) is a 2-layer, 1.2 mm board that holds the RP2040-Zero, DFPlayer, MPU6050, buttons, jack and the charging circuit, with the battery underneath. The case is 3D printed and closes with four M2 screws into heat-set inserts. Neither the board nor the case source is in the repo yet (see "Hardware files").

## Status

- [x] Music player firmware (disc view, chooser, backdrop)
- [x] Pet firmware (animations, tilt, shake, penguin)
- [x] Both merged into one sketch
- [x] Parts list with prices (BOM.csv)
- [ ] Merged build flash-tested on the final PCB
- [ ] Carrier PCB v2 fabricated and assembled
- [ ] Case updated for PCB v2, printed and fitted
- [ ] KiCad project committed in `PCB/`
- [ ] Case CAD committed in `CAD/`
- [ ] Real photo or render added to this README
- [ ] Battery life measured
- [ ] License chosen

The remaining steps are tracked in [docs/SHIPPING_CHECKLIST.md](docs/SHIPPING_CHECKLIST.md).

### Known limitations / not done yet

- The merged firmware has not been flash-tested as one build on the final PCB.
- PCB v2 has not been fabricated yet.
- The case has not been updated for PCB v2 (button and USB-C positions, M2 inserts).
- There are no volume buttons. The default volume is set to 15 of 30.
- There is no on-screen battery indicator. The battery is read on GP26 and logged to Serial.
- Debug prints are off by default.
- The left/right tilt sign of the pet's face is unconfirmed on hardware (up/down is confirmed).
- `User_Setup.h` for TFT_eSPI has to be edited by hand (see docs/User_Setup.example.h).
- The album covers in `tools/Spinning Disc/covers/` and the penguin gif are third-party artwork. They are placeholders. Replace them with your own or properly licensed art before redistributing this repo. The music is not in the repo.
- The BOM is approximate: four lines are unpriced and some prices are substitutes (see docs/BOM_NOTES.md).

## Credits

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) by Bodmer
- [DFRobotDFPlayerMini](https://github.com/DFRobot/DFRobotDFPlayerMini) by DFRobot
- [arduino-pico](https://github.com/earlephilhower/arduino-pico) by Earle Philhower
- Lora and DM Sans fonts, SIL Open Font License (license texts in `tools/UI_Fonts/fonts/`: [OFL-Lora.txt](tools/UI_Fonts/fonts/OFL-Lora.txt), [OFL-DMSans.txt](tools/UI_Fonts/fonts/OFL-DMSans.txt))
- The pet animations are my own clips. The dancing penguin is a Block Tales fan gif, credited here as that and to be replaced before redistribution.
- Album covers: third-party artwork, placeholders (see Known limitations).

## License

TODO: License not chosen yet.

<!-- e.g. MIT for code, CERN-OHL-P for hardware -->
