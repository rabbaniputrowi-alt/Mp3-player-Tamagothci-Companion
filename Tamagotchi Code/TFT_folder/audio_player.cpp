#include "audio_player.h"
#include "config.h"
#include <DFRobotDFPlayerMini.h>

namespace AudioPlayer {
 
namespace {
  DFRobotDFPlayerMini dfplayer;
 
  constexpr uint8_t DEFAULT_VOLUME = 20;   // 0-30 range for this module
 
  uint8_t curPlaylist = 0;  // 0-based, ALBUMS[] index
  uint8_t curTrack     = 1;  // 1-based, SD card file number within the folder
  bool    playing      = false;
}
 
void init() {
  // GP28/29 support UART1 on the RP2040's GPIO function mux (every 8th
  // pin repeats the TX/RX/CTS/RTS pattern) — this remaps Serial2 there
  // instead of its default pins. Must be called before begin().
  Serial2.setTX(DFPLAYER_TX_PIN);
  Serial2.setRX(DFPLAYER_RX_PIN);
  Serial2.begin(9600);
 
  // Some DFPlayer clones never send the ACK this expects during the
  // handshake — begin() can report failure on those boards even though
  // playback commands work fine afterward. Log it, but don't gate
  // anything on the result.
  bool ok = dfplayer.begin(Serial2, /*isACK=*/true, /*doReset=*/true);
  if (!ok) {
    // No Serial console assumed connected in normal operation, but if
    // you're debugging with one attached, this is where you'd see it:
    // Serial.println("DFPlayer handshake failed — likely a clone chip, continuing anyway.");
  }
 
  dfplayer.volume(DEFAULT_VOLUME);
 
  curPlaylist = 0;
  curTrack    = 1;
  playing     = false;
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
  // NOTE: this doesn't yet know how many tracks are in the current
  // playlist, so it can walk past the end into a track that doesn't
  // exist — DFPlayer clones generally just do nothing audible in that
  // case rather than erroring loudly. Worth adding a per-playlist track
  // count once the SD card is fully populated, to wrap back to track 1
  // properly instead of silently stalling.
  curTrack++;
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
 
} // namespace AudioPlayer
