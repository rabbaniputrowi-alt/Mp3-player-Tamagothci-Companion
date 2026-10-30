#include "audio_player.h"
#include "config.h"
#include "track_names.h"
#include <DFRobotDFPlayerMini.h>

namespace AudioPlayer {

namespace {
  DFRobotDFPlayerMini dfplayer;

  // 0-30 range for this module (30 = max). Half scale, not max: firmware.ino
  // auto-plays folder 01 track 1 at boot straight into the earbuds, and full
  // volume is painfully loud there. There is no volume control on the
  // buttons yet (all four are allocated), so this is the level it stays at.
  constexpr uint8_t DEFAULT_VOLUME = 15;

  // module fallback
  constexpr uint8_t FALLBACK_TRACK_COUNT = 8;

  uint8_t curPlaylist = 0;  // 0-based, ALBUMS[] index
  uint8_t curTrack     = 1;  // 1-based, SD card file number within the folder
  bool    playing      = false;

  // Autoplay. The module sends a "play finished" message (0x3D) when a
  // track ends; genuine chips send it twice and some clones also send one
  // right after a new play command. Anything that arrives within
  // FINISH_GUARD_MS of our last play command is treated as that echo and
  // ignored, so one finished track only ever advances once.
  constexpr uint32_t FINISH_GUARD_MS = 1500;
  uint32_t lastPlayCmdMs = 0;

  // Autoplay, second source: the BUSY pin, LOW while a track plays. Some
  // clones never send the "finished" message, so a track counts as done
  // when BUSY has been HIGH for BUSY_IDLE_MS while we think we are playing.
  // The pin has a pull-up, so if nothing is wired to it it reads HIGH for
  // ever; busyWorks stays false until BUSY has been seen LOW once, and until
  // then the pin is ignored (otherwise an unwired pin would skip a track
  // every couple of seconds).
  constexpr uint32_t BUSY_IDLE_MS = 600;
  bool     busyWorks       = false;
  uint32_t busyLastLowMs   = 0;

  void playCurrent() {
    // ALBUMS[] is 0-based, SD card folders are 1-based (01, 02, ...).
    dfplayer.playFolder(curPlaylist + 1, curTrack);
    playing       = true;
    lastPlayCmdMs = millis();
  }

  uint8_t trackCountCache[UI_MAX_PLAYLISTS] = { 0 };
  bool    trackCountKnown[UI_MAX_PLAYLISTS] = { false };
}

void init() {
  // GP28/29 are UART0 pins on the RP2040's GPIO mux (UART0 TX: 0/12/16/28,
  // RX: 1/13/17/29), so the DFPlayer goes on Serial1. Serial2 is UART1 and
  // cannot reach these pins - the core panic()s on an illegal pin, halting
  // the chip before USB enumerates. Must be called before begin().
  pinMode(DFPLAYER_BUSY_PIN, INPUT_PULLUP);

  Serial1.setTX(DFPLAYER_TX_PIN);
  Serial1.setRX(DFPLAYER_RX_PIN);
  Serial1.begin(9600);

  // isACK MUST stay false on this clone. With ACK enabled the library's
  // sendStack() spins in `while (_isSending) waitAvailable();` before every
  // command, and _isSending is only cleared by a 0x41 ACK byte the clone
  // never sends — so it clears via the 500ms timeout instead. That means
  // every play/pause/next call blocked the render loop for half a second
  // and the album art visibly froze on each button press. With ACK off the
  // library just waits 10ms after each command.
  //
  // begin() also reports failure on these boards for the same missing-ACK
  // reason. Log it, but don't gate anything on the result.
  bool ok = dfplayer.begin(Serial1, /*isACK=*/false, /*doReset=*/true);
  if (!ok) {
    // No Serial console assumed connected in normal operation, but if
    // you're debugging with one attached, this is where you'd see it:
    // Serial.println("DFPlayer handshake failed — likely a clone chip, continuing anyway.");
  }

  dfplayer.volume(DEFAULT_VOLUME);

  curPlaylist = 0;
  curTrack    = 1;
  playing     = false;

  for (uint8_t i = 0; i < UI_MAX_PLAYLISTS; i++) {
    trackCountCache[i] = 0;
    trackCountKnown[i] = false;
  }
}

void playPlaylist(uint8_t playlist, uint8_t track) {
  curPlaylist = playlist;
  curTrack    = track;

  playCurrent();
}

void next() {
  // Wrap to track 1 after the last song, but only when tracks.json says how
  // many songs the folder has. The DFPlayer count can be the fallback guess
  // on this clone, and wrapping on a wrong guess would cut the folder short.
  const uint8_t listed = TrackNames::trackCount(curPlaylist);
  if (listed && curTrack >= listed) curTrack = 1;
  else if (curTrack < 255)          curTrack++;
  playCurrent();
}

void previous() {
  if (curTrack > 1) curTrack--;
  playCurrent();
}

void togglePlayPause() {
  if (playing) {
    dfplayer.pause();
    playing = false;
  } else {
    dfplayer.start();
    playing       = true;
    lastPlayCmdMs = millis();
  }
}

bool isPlaying() {
  return playing;
}

uint8_t currentPlaylist() {
  return curPlaylist;
}

uint8_t currentTrack() {
  return curTrack;
}

uint8_t trackCount(uint8_t playlist) {
  if (playlist >= UI_MAX_PLAYLISTS) return FALLBACK_TRACK_COUNT;
  if (trackCountKnown[playlist])    return trackCountCache[playlist];

  // tracks.json is authoritative when it lists this folder - no need to ask
  // the module (which this clone may not answer anyway).
  const uint8_t listed = TrackNames::trackCount(playlist);
  if (listed) {
    trackCountCache[playlist] = listed;
    trackCountKnown[playlist] = true;
    return listed;
  }

  // readFileCountsInFolder() returns -1 when the module doesn't answer.
  const int n = dfplayer.readFileCountsInFolder(playlist + 1);

  uint8_t result;
  if (n == 0)       result = 0;                     // module says empty
  else if (n < 0)   result = FALLBACK_TRACK_COUNT;  // no answer
  else if (n > 255) result = 255;
  else              result = (uint8_t)n;

  trackCountCache[playlist] = result;
  trackCountKnown[playlist] = true;
  return result;
}

bool serviceAutoplay(uint32_t nowMs) {
  bool finished = false;

  // Source 1, the serial "finished" message. available() only reads what is
  // already in the UART buffer, it never waits, so this is cheap to call
  // every loop. Messages other than "finished" (card inserted/removed/online,
  // errors) are only logged on Serial for debugging, then dropped; they change
  // nothing. readType() consumes the message, so it is called exactly once per
  // available() and the type is handled with a switch, not compared once.
  // For DFPlayerError, read() is the module's error code (the library's
  // Busy / FileMismatch / ... numbers), printed as a plain number.
  if (dfplayer.available()) {
    switch (dfplayer.readType()) {
      case DFPlayerPlayFinished:
        finished = true;
        break;
      case DFPlayerError:
        Serial.printf("DFPlayer error %u\n", (unsigned)dfplayer.read());
        break;
      case DFPlayerCardRemoved:
        Serial.println("DFPlayer: SD card removed");
        break;
      case DFPlayerCardInserted:
        Serial.println("DFPlayer: SD card inserted");
        break;
      case DFPlayerCardOnline:
        Serial.println("DFPlayer: SD card online");
        break;
      default:
        break;
    }
  }

  // Source 2, the BUSY pin (see busyWorks above).
  if (digitalRead(DFPLAYER_BUSY_PIN) == LOW) {
    busyWorks     = true;
    busyLastLowMs = nowMs;
  } else if (busyWorks && nowMs - busyLastLowMs >= BUSY_IDLE_MS) {
    finished = true;
  }

  if (!finished) return false;
  // Paused: BUSY is HIGH and a stray finish must not start the next song.
  if (!playing) return false;
  // Right after a play command: BUSY has not gone LOW yet, or this is the
  // duplicate / echo "finished" (see FINISH_GUARD_MS above).
  if (nowMs - lastPlayCmdMs < FINISH_GUARD_MS) return false;

  next();
  return true;
}

} // namespace AudioPlayer