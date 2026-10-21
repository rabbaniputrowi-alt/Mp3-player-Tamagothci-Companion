// Stardance: MP3 player + Tamagotchi-style pet on one round display.
//
// Views and buttons:
//   PET / PLAYING (disc)  PREV / NEXT  previous / next track
//                         PLAY         play / pause
//                         MENU short   open the playlist chooser
//                         MENU held    swap pet <-> disc
//   In the pet view the pet also reacts: PREV -> WINK, NEXT ->
//   WINK_TRANSITION (affection), PLAY -> PENGUIN (the dancing penguin gif,
//   full screen in its own colours). The penguin loops until PLAY is pressed
//   again or MENU is pressed (short or held); PREV, NEXT and shaking leave it
//   dancing.
//   From the pet view the chooser does not cut in at once: the pet plays
//   MUSIC_SPIN first as the transition (wink, spin down to a dot, music-note
//   face, spin out to a dot) and the chooser opens when the clip ends. A
//   second short MENU while it plays skips the rest. From the disc view the
//   chooser opens straight away.
//   PLAYLIST_SELECT       PREV / NEXT scroll, PLAY open tracks, MENU back
//   TRACK_SELECT          PREV / NEXT scroll, PLAY play it, MENU back
//   Backing out of the chooser (or picking a track) returns to the view it
//   was opened from (homeView).
//
// Board: waveshare_rp2040_zero, arduino-pico core.
// The pet data is ~1.2 MB: Tools > Flash Size must be "2MB (no FS)".

#include "config.h"
#include "audio_player.h"
#include "album_store.h"
#include "playlist_art.h"
#include "disc_renderer.h"
#include "display.h"
#include "playlist_ui.h"
#include "pet_anims.h"
#include "pet.h"
#include "pet_screen.h"

namespace {
  constexpr uint32_t DEBOUNCE_MS = 25;

  // DOWN fires on the press edge. A full-screen push blocks the loop for
  // ~40ms, so a quick tap may be seen by only one poll; waiting for a settled
  // release needs two and silently dropped the press. DEBOUNCE_MS is the
  // lockout that swallows contact bounce instead.
  // SHORT_UP / LONG_HOLD are for MENU only, which has two jobs: SHORT_UP on
  // release if it was not held, LONG_HOLD once while still held past
  // LONG_PRESS_MS (so the swap happens without waiting for the release).
  enum class Press : uint8_t { NONE, DOWN, SHORT_UP, LONG_HOLD };

  struct Button {
    uint8_t  pin;
    bool     down;
    bool     longFired;
    uint32_t lastEventMs;   // last accepted edge
  };

  Button btnPrev { PIN_BTN_PREV, false, false, 0 };
  Button btnPlay { PIN_BTN_PLAY, false, false, 0 };
  Button btnNext { PIN_BTN_NEXT, false, false, 0 };
  Button btnMenu { PIN_BTN_MENU, false, false, 0 };

  Press poll(Button &b, uint32_t now) {
    const bool raw = (digitalRead(b.pin) == LOW);
    if (raw != b.down) {
      if (now - b.lastEventMs < DEBOUNCE_MS) return Press::NONE;  // bounce lockout
      b.down        = raw;
      b.lastEventMs = now;
      if (raw) { b.longFired = false; return Press::DOWN; }
      return b.longFired ? Press::NONE : Press::SHORT_UP;
    }
    if (b.down && !b.longFired && now - b.lastEventMs >= LONG_PRESS_MS) {
      b.longFired = true;
      return Press::LONG_HOLD;
    }
    return Press::NONE;
  }

  constexpr uint32_t FRAME_INTERVAL_MS = 33;
  uint32_t lastFrameMs = 0;

  AppState appState = AppState::PET;
  AppState homeView = AppState::PET;   // PET or PLAYING: where the chooser returns to

  // The clip the pet plays on its way from the pet view into the chooser.
  // It runs about 3.8 s (the 0.8 s neutral-face hold it opens on is skipped).
  constexpr PetAnimId CHOOSER_TRANSITION_ANIM = PET_ANIM_MUSIC_SPIN;

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

  // Every switch into a view goes through here, so the pet always gets its
  // enter() (motion re-centred, forced repaint).
  void showView(AppState view, uint32_t now) {
    appState = view;
    if (view == AppState::PET) PetScreen::enter(now);
  }

  void openChooser() {
    homeView = appState;
    PlaylistUI::openPlaylists(AudioPlayer::currentPlaylist());
    appState = AppState::PLAYLIST_SELECT;
  }

  void handlePrevNext(int8_t delta, uint32_t now) {
    switch (appState) {
      case AppState::PET:
      case AppState::PLAYING:
        if (delta < 0) AudioPlayer::previous();
        else           AudioPlayer::next();
        syncArt();
        // The music action always happens; trigger() returning false (an
        // interactive clip is already playing) is fine to ignore.
        if (appState == AppState::PET)
          Pet::trigger(delta < 0 ? PET_ANIM_WINK : PET_ANIM_WINK_TRANSITION, now);
        break;

      case AppState::PLAYLIST_SELECT:
      case AppState::TRACK_SELECT:
        PlaylistUI::scroll(delta, now);
        break;
    }
  }

  void handlePlay(uint32_t now) {
    switch (appState) {
      case AppState::PET:
      case AppState::PLAYING:
        AudioPlayer::togglePlayPause();
        // Also in the pet view, so the disc is right when it is swapped in.
        DiscRenderer::setSpinning(AudioPlayer::isPlaying());
        // PLAY's pet reaction is the penguin gif. (MUSIC_SPIN used to sit in
        // this slot; it is the transition into the chooser now, see
        // handleMenuShort.) The penguin loops until it is stopped, so PLAY
        // works as an on / off switch for it: the first press starts it, the
        // next one sends the pet back to its neutral face. As with PREV /
        // NEXT the music action always happens, and trigger() returning false
        // (another interactive clip, e.g. a wink, is playing) is fine to
        // ignore.
        if (appState == AppState::PET) {
          if (Pet::currentAnim() == PET_ANIM_PENGUIN) Pet::stop(now);
          else                                        Pet::trigger(PET_ANIM_PENGUIN, now);
        }
        break;

      case AppState::PLAYLIST_SELECT:
        PlaylistUI::openTracks(PlaylistUI::selectedIndex(), 1);
        appState = AppState::TRACK_SELECT;
        break;

      case AppState::TRACK_SELECT:
        if (PlaylistUI::itemCount()) {
          startPlayback(PlaylistUI::listPlaylist(),
                        (uint8_t)(PlaylistUI::selectedIndex() + 1));
          showView(homeView, now);
        }
        break;
    }
  }

  void handleMenuShort(uint32_t now) {
    switch (appState) {
      case AppState::PET:
        // The pet plays the transition clip on its way out, and loop() opens
        // the chooser once it has run through. appState stays PET meanwhile,
        // so the music buttons keep working. A second short press while the
        // clip is playing skips the rest of it.
        if (PetScreen::leaving()) openChooser();
        else                      PetScreen::leaveWith(CHOOSER_TRANSITION_ANIM, now);
        break;

      case AppState::PLAYING:
        openChooser();   // no pet on screen here, so straight in
        break;

      case AppState::PLAYLIST_SELECT:
        showView(homeView, now);
        break;

      case AppState::TRACK_SELECT:
        PlaylistUI::openPlaylists(PlaylistUI::listPlaylist());
        appState = AppState::PLAYLIST_SELECT;
        break;
    }
  }

  void handleMenuHold(uint32_t now) {
    switch (appState) {
      case AppState::PET:
        // MENU ends the looping penguin. A short MENU does that by playing
        // the chooser transition over it; here the pet is left as it is
        // while hidden, so stop it explicitly or it would still be dancing
        // when the pet view comes back.
        if (Pet::currentAnim() == PET_ANIM_PENGUIN) Pet::stop(now);
        showView(AppState::PLAYING, now);
        break;

      case AppState::PLAYING:
        showView(AppState::PET, now);
        break;

      case AppState::PLAYLIST_SELECT:
      case AppState::TRACK_SELECT:
        break;   // ignored in the chooser
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_PREV, INPUT_PULLUP);
  pinMode(PIN_BTN_PLAY, INPUT_PULLUP);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  pinMode(PIN_BTN_MENU, INPUT_PULLUP);

  AlbumStore::init();
  DiscRenderer::init();
  Display::init();

  // The chooser's two heap allocations (backdrop thumbnail cache + sprite),
  // kept early. Both fail silently, so print the drop: it should be about
  // 59 KB (36,000 cache + 23,040 sprite), however many albums there are. A
  // smaller drop means one failed. (Before 2026-10-12 the backdrop built one
  // 10,800-byte thumbnail per album here, which stopped fitting at 7 albums.)
  const uint32_t heapBefore = rp2040.getFreeHeap();
  PlaylistUI::init();
  const uint32_t heapAfter = rp2040.getFreeHeap();
  Serial.printf("free heap %lu -> %lu (chooser took %lu)\n",
                (unsigned long)heapBefore, (unsigned long)heapAfter,
                (unsigned long)(heapBefore - heapAfter));

  AudioPlayer::init();
  PetScreen::init(millis());

  AudioPlayer::playPlaylist(0, 1);
  syncArt();
  DiscRenderer::setSpinning(AudioPlayer::isPlaying());

  showView(AppState::PET, millis());
}

void loop() {
  uint32_t now = millis();

  if (poll(btnPrev, now) == Press::DOWN) handlePrevNext(-1, now);
  if (poll(btnPlay, now) == Press::DOWN) handlePlay(now);
  if (poll(btnNext, now) == Press::DOWN) handlePrevNext(+1, now);
  switch (poll(btnMenu, now)) {
    case Press::SHORT_UP:  handleMenuShort(now); break;
    case Press::LONG_HOLD: handleMenuHold(now);  break;
    default: break;
  }

  now = millis();
  // Not frame-gated: samples motion at 100 Hz, draws only on change.
  if (appState == AppState::PET) {
    PetScreen::tick(now);
    // The transition clip started by a short MENU has run through; its last
    // frame (the dot) is still on screen and the chooser draws over it.
    if (PetScreen::leaveDone()) openChooser();
  }

  if (now - lastFrameMs >= FRAME_INTERVAL_MS) {
    lastFrameMs = now;

    // Every view: replay() restores its snapshotted spin state ~350 ms of
    // tick() time later. If tick() stopped while the disc is hidden, that
    // restore would still be pending when the disc comes back and could
    // undo a pause made in the pet view. It is only float maths.
    DiscRenderer::tick(now);

    if (appState == AppState::PLAYING) {
      Display::drawDisc();
    } else if (appState != AppState::PET) {
      PlaylistUI::tick(now);
      if (PlaylistUI::needsRedraw()) PlaylistUI::draw();
    }
  }
}
