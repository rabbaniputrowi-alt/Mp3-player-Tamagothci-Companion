/*
 * DFPlayer Mini standalone verification sketch
 *
 * Purpose: confirm the module powers up, the SD card is readable,
 * and audio actually reaches the 3.5mm jack. Nothing else.
 *
 * Library: "DFRobotDFPlayerMini" by DFRobot (Library Manager)
 * Serial Monitor: 115200 baud
 *
 * Wiring (Arduino Nano) — matches SoftwareSerial(6, 5) below:
 *   DFPlayer VCC   -> 3.3v
 *   DFPlayer GND   -> GND
 *   DFPlayer TX    -> D6                  (Nano receives)
 *   DFPlayer RX    -> 1k resistor -> D5   (Nano transmits)
 *   DFPlayer DAC_L -> PJ-307 
 *   DFPlayer DAC_R -> PJ-307 
 *   DFPlayer GND   -> PJ-307 
 *
 * Decoupling: 470-1000uF electrolytic across DFPlayer VCC and GND
 *
 * SD card: FAT32. Folder named mp3 at the card root,
 * files named 0001.mp3, 0002.mp3, and so on
 */

#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// SoftwareSerial(RX, TX)
SoftwareSerial dfSerial(6, 5);
DFRobotDFPlayerMini player;

// Set to false to skip the module reset during init. Some MP3-TF-16P
// clones fail the handshake when reset is requested but work fine after.
const bool RESET_ON_INIT = true;

// Set to true to keep going even if init reports failure, so playback
// is attempted anyway. Useful for diagnosing clone handshake quirks.
const bool CONTINUE_ON_INIT_FAIL = false;

void printDetail(uint8_t type, int value);

void setup() {
  Serial.begin(115200);
  dfSerial.begin(9600);

  Serial.println(F("DFPlayer Mini test v2"));
  Serial.println(F("Initializing (may take a few seconds)..."));

  bool ok = player.begin(dfSerial, true, RESET_ON_INIT);

  if (!ok) {
    Serial.println(F("FAILED to initialize. Check, in this order:"));
    Serial.println(F("  1. Series resistor on the D5 line is ~1k, not 10k"));
    Serial.println(F("  2. DFPlayer TX -> D6, DFPlayer RX -> D5 (not swapped)"));
    Serial.println(F("  3. 5V at VCC, common ground with the Nano"));
    Serial.println(F("  4. SD card inserted, /mp3 folder, 0001.mp3 naming"));

    if (!CONTINUE_ON_INIT_FAIL) {
      Serial.println(F("Halting. Set CONTINUE_ON_INIT_FAIL = true to try anyway."));
      while (true) delay(100);
    }
    Serial.println(F("Continuing anyway..."));
  } else {
    Serial.println(F("DFPlayer online."));
  }

  player.setTimeOut(500);
  player.volume(30);            // 0-30. volume above 22 may cause distortion
  player.EQ(DFPLAYER_EQ_NORMAL);
  player.outputDevice(DFPLAYER_DEVICE_SD);

  int files = player.readFileCounts();
  Serial.print(F("Files detected on SD: "));
  Serial.println(files);

  if (files <= 0) {
    Serial.println(F("No files found — check folder name and 4-digit prefixes."));
  }

  Serial.println(F("Playing track 1..."));
  player.play(2);
}

void loop() {
  if (player.available()) {
    printDetail(player.readType(), player.read());
  }
}

void printDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      Serial.println(F("Timeout — no response from module."));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("SD card inserted."));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("SD card removed."));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("SD card online."));
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Finished track "));
      Serial.println(value);
      break;
    case DFPlayerError:
      Serial.print(F("Error: "));
      switch (value) {
        case Busy:             Serial.println(F("card not found"));           break;
        case Sleeping:         Serial.println(F("module sleeping"));          break;
        case SerialWrongStack: Serial.println(F("bad serial data"));          break;
        case CheckSumNotMatch: Serial.println(F("checksum mismatch"));        break;
        case FileIndexOut:     Serial.println(F("file index out of range"));  break;
        case FileMismatch:     Serial.println(F("file not found"));           break;
        case Advertise:        Serial.println(F("in advertise mode"));        break;
        default:               Serial.println(value);                         break;
      }
      break;
    default:
      break;
  }
}
