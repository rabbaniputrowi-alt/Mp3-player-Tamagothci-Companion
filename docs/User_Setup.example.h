// Stardance: example TFT_eSPI User_Setup.h
//
// This is a reconstruction from the project notes, not a copy of the working
// file. It contains only the settings the notes verify.
//
// Compare it with your own User_Setup.h in
//   Documents/Arduino/libraries/TFT_eSPI
// before replacing anything. In particular SPI_FREQUENCY: the working value
// is not recorded in this repo, so it is left as a placeholder below. Pick a
// value that works with your display and wiring.
//
// Stale build cache: the Arduino IDE caches the compiled TFT_eSPI library.
// If a change to User_Setup.h does not take effect, close the IDE, delete the
// folders in AppData\Local\arduino\sketches, and compile again. ctags
// temp-file lock errors during a compile come from the same cache.
//
// Display module note: its silkscreen uses I2C names. SCL is the SPI clock
// (GP14) and SDA is the SPI data line (GP15).

#define GC9A01_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// RP2040 SPI1 (SCK on GP14, TX on GP15)
#define TFT_SPI_PORT 1
#define TFT_SCLK 14
#define TFT_MOSI 15
#define TFT_MISO -1

#define TFT_CS   6
#define TFT_DC   7
#define TFT_RST  8

// Fonts TFT_eSPI loads by default
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// Not recorded in the repo. Set your own, then test.
// #define SPI_FREQUENCY  ???
