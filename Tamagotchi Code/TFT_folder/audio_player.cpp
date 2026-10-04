#include "audio_player.h"
#include "config.h"
#include "track_names.h"
#include <DFRobotDFPlayerMini.h>

namespace AudioPlayer {

namespace {
  DFRobotDFPlayerMini dfplayer;

  constexpr uint8_t DEFAULT_VOLUME = 20;   // 0-30 range for this module

  // module fallback
  constexpr uint8_t FALLBACK_TRACK_COUNT = 8;

  uint8_t curPlaylist = 0;  // 0-based, ALBUMS[] index
  uint8_t curTrack     = 1;  // 1-based, SD card file number within the folder
  bool    playing      = false;

  uint8_t trackCountCache[UI_MAX_PLAYLISTS] = { 0 };
  bool    trackCountKnown[UI_MAX_PLAYLISTS] = { false };
}

void init() {
  // GP28/29 are UART0 pins on the RP2040's GPIO mux (UART0 TX: 0/12/16/28,
  // RX: 1/13/17/29), so the DFPlayer goes on Serial1. Serial2 is UART1 and
  // cannot reach these pins - the core panic()s on an illegal pin, halting
  // the chip before USB enumerates. Must be called before begin().
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

  // ALBUMS[] is 0-based, SD card folders are 1-based (01, 02, ...).
  uint8_t folder = playlist + 1;
  dfplayer.playFolder(folder, curTrack);
  playing = true;
}

void next() {
  // Wrap to track 1 after the last song, but only when tracks.json says how
  // many songs the folder has. The DFPlayer count can be the fallback guess
  // on this clone, and wrapping on a wrong guess would cut the folder short.
  const uint8_t listed = TrackNames::trackCount(curPlaylist);
  if (listed && curTrack >= listed) curTrack = 1;
  else if (curTrack < 255)          curTrack++;
  dfplayer.playFolder(curPlaylist + 1, curTrack);
  playing = true;
}

void previous() {
  if (curTrack > 1) curTrack--;
  dfplayer.playFolder(curPlaylist + 1, curTrack);
  playing = true;
}

void togglePlayPause() {
  if (playing) {
    dfplayer.pause();
    playing = false;
  } else {
    dfplayer.start();
    playing = true;
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

} // namespace AudioPlayer