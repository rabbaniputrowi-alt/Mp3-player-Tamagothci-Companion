#include "config.h"
#include "audio_player.h"
#include "album_store.h"
#include "playlist_art.h"
#include "disc_renderer.h"
#include "display.h"
#include "playlist_ui.h"

namespace {
  constexpr uint32_t DEBOUNCE_MS = 25;

  struct Button {
    uint8_t  pin;
    bool     down;
    uint32_t lastEventMs;
  };

  Button btnPrev { PIN_BTN_PREV, false, 0 };
  Button btnPlay { PIN_BTN_PLAY, false, 0 };
  Button btnNext { PIN_BTN_NEXT, false, 0 };
  Button btnMenu { PIN_BTN_MENU, false, 0 };

  // Fires on the press edge. A full-screen push blocks the loop for ~40ms, so
  // a quick tap may be seen by only one poll; waiting for a settled release
  // needs two and silently dropped the press. DEBOUNCE_MS is the lockout that
  // swallows contact bounce instead.
  bool pressed(Button &b, uint32_t now) {
    const bool raw = (digitalRead(b.pin) == LOW);
    if (raw == b.down) return false;
    if (now - b.lastEventMs < DEBOUNCE_MS) return false;
    b.down        = raw;
    b.lastEventMs = now;
    return raw;
  }

  constexpr uint32_t FRAME_INTERVAL_MS = 33;
  uint32_t lastFrameMs = 0;

  AppState appState = AppState::PLAYING;

  bool syncArt() {
    uint8_t want = PlaylistArt::artIndexFor(AudioPlayer::currentPlaylist(),
                                            AudioPlayer::currentTrack());
    if (want == AlbumStore::current()) return false;
    AlbumStore::load(want);
    return true;
  }

  void startPlayback(uint8_t playlist, uint8_t track) {
    AudioPlayer::playPlaylist(playlist, track);
    // setSpinning() must come before replay(), which snapshots the spin state
    // and restores it when the snap-back ends.
    DiscRenderer::setSpinning(AudioPlayer::isPlaying());
    if (syncArt()) DiscRenderer::replay();
  }

  void handlePrevNext(int8_t delta, uint32_t now) {
    if (appState == AppState::PLAYING) {
      if (delta < 0) AudioPlayer::previous();
      else           AudioPlayer::next();
      syncArt();
    } else {
      PlaylistUI::scroll(delta, now);
    }
  }

  void handlePlay() {
    switch (appState) {
      case AppState::PLAYING:
        AudioPlayer::togglePlayPause();
        DiscRenderer::setSpinning(AudioPlayer::isPlaying());
        break;

      case AppState::PLAYLIST_SELECT:
        PlaylistUI::openTracks(PlaylistUI::selectedIndex(), 1);
        appState = AppState::TRACK_SELECT;
        break;

      case AppState::TRACK_SELECT:
        if (PlaylistUI::itemCount()) {
          startPlayback(PlaylistUI::listPlaylist(),
                        (uint8_t)(PlaylistUI::selectedIndex() + 1));
          appState = AppState::PLAYING;
        }
        break;
    }
  }

  void handleMenu() {
    switch (appState) {
      case AppState::PLAYING:
        PlaylistUI::openPlaylists(AudioPlayer::currentPlaylist());
        appState = AppState::PLAYLIST_SELECT;
        break;

      case AppState::PLAYLIST_SELECT:
        appState = AppState::PLAYING;
        break;

      case AppState::TRACK_SELECT:
        PlaylistUI::openPlaylists(PlaylistUI::listPlaylist());
        appState = AppState::PLAYLIST_SELECT;
        break;
    }
  }
}

void setup() {
  pinMode(PIN_BTN_PREV, INPUT_PULLUP);
  pinMode(PIN_BTN_PLAY, INPUT_PULLUP);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  pinMode(PIN_BTN_MENU, INPUT_PULLUP);

  AlbumStore::init();
  DiscRenderer::init();
  Display::init();
  PlaylistUI::init();
  AudioPlayer::init();

  AudioPlayer::playPlaylist(0, 1);
  syncArt();
  DiscRenderer::setSpinning(AudioPlayer::isPlaying());
}

void loop() {
  uint32_t now = millis();

  if (pressed(btnPrev, now)) handlePrevNext(-1, now);
  if (pressed(btnPlay, now)) handlePlay();
  if (pressed(btnNext, now)) handlePrevNext(+1, now);
  if (pressed(btnMenu, now)) handleMenu();

  now = millis();
  if (now - lastFrameMs >= FRAME_INTERVAL_MS) {
    lastFrameMs = now;

    if (appState == AppState::PLAYING) {
      DiscRenderer::tick(now);
      Display::drawDisc();
    } else {
      PlaylistUI::tick(now);
      if (PlaylistUI::needsRedraw()) PlaylistUI::draw();
    }
  }
}
