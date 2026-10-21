#include "config.h"
#include "audio_player.h"

namespace {
  // Simple debounce: react on release, not on press, to avoid double-fires.
  bool prevWasDown = false;
  bool playWasDown = false;
  bool nextWasDown = false;
}

void setup() {
  pinMode(PIN_BTN_PREV, INPUT_PULLUP);
  pinMode(PIN_BTN_PLAY, INPUT_PULLUP);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);

  AudioPlayer::init();

  // Starts on ALBUMS[0] / SD card folder 01, track 1. Swap this out
  // once there's a menu/selection flow for choosing which album to
  // start on.
  AudioPlayer::playPlaylist(0, 1);
}

void loop() {
  bool prevDown = (digitalRead(PIN_BTN_PREV) == LOW);
  bool playDown = (digitalRead(PIN_BTN_PLAY) == LOW);
  bool nextDown = (digitalRead(PIN_BTN_NEXT) == LOW);

  if (prevWasDown && !prevDown) AudioPlayer::previous();
  if (playWasDown && !playDown) AudioPlayer::togglePlayPause();
  if (nextWasDown && !nextDown) AudioPlayer::next();

  prevWasDown = prevDown;
  playWasDown = playDown;
  nextWasDown = nextDown;
}
