#include "config.h"
#include "audio_player.h"
#include "album_store.h"
#include "playlist_art.h"
#include "disc_renderer.h"
#include "display.h"

namespace {
  // ---- Button debouncing -------------------------------------------------
  //
  // Reacting on the release edge alone is NOT debouncing — loop() polls in
  // microseconds, so the contact bounce of a single press (typically a few
  // ms of chatter) produces several release edges and fires the action
  // several times. On PLAY that meant one press could toggle play/pause an
  // even number of times and appear to do nothing, or land stopped and stay
  // stopped. A press now has to hold its new level for DEBOUNCE_MS before it
  // counts.
  constexpr uint32_t DEBOUNCE_MS = 25;

  struct Button {
    uint8_t  pin;
    bool     stable;        // debounced level, true = pressed
    bool     lastRaw;       // last raw sample, used to time the settle window
    uint32_t lastChangeMs;
  };

  Button btnPrev { PIN_BTN_PREV, false, false, 0 };
  Button btnPlay { PIN_BTN_PLAY, false, false, 0 };
  Button btnNext { PIN_BTN_NEXT, false, false, 0 };
  Button btnMenu { PIN_BTN_MENU, false, false, 0 };

  // True exactly once per physical press, on the debounced release.
  bool released(Button &b, uint32_t now) {
    const bool raw = (digitalRead(b.pin) == LOW);   // INPUT_PULLUP: pressed = LOW

    if (raw != b.lastRaw) {          // still bouncing — restart the settle window
      b.lastRaw      = raw;
      b.lastChangeMs = now;
      return false;
    }
    if (now - b.lastChangeMs < DEBOUNCE_MS) return false;  // not settled yet
    if (raw == b.stable)                    return false;  // no real change

    b.stable = raw;
    return !raw;                     // fire on press-then-release, not on press
  }

  // ~33ms between frames. Pushing faster than this just starves the button
  // poll without looking any smoother at 25 deg/s.
  constexpr uint32_t FRAME_INTERVAL_MS = 33;
  uint32_t lastFrameMs = 0;

  // Pulls the cover art for wherever AudioPlayer currently is and hands it
  // to AlbumStore. AlbumStore::load() early-returns when the index is
  // already resident, so calling this after every playback change is cheap
  // — the 56 KB flash->RAM memcpy only actually runs when the art changes.
  // Returns true if a different cover was loaded.
  bool syncArt() {
    uint8_t want = PlaylistArt::artIndexFor(AudioPlayer::currentPlaylist(),
                                            AudioPlayer::currentTrack());
    if (want == AlbumStore::current()) return false;
    AlbumStore::load(want);
    return true;
  }

  // MENU short-press: advance to the next playlist, wrapping at the end of
  // the PlaylistArt table. Playback restarts at track 1 of that folder.
  void cyclePlaylist() {
    uint8_t count = PlaylistArt::playlistCount();
    if (count == 0) return;

    uint8_t nextPlaylist = (uint8_t)((AudioPlayer::currentPlaylist() + 1) % count);
    AudioPlayer::playPlaylist(nextPlaylist, 1);

    // setSpinning() MUST come before replay(): replay() snapshots the
    // current spin state into resumeSpinAfter and restores it when the
    // snap-back finishes. Called the other way round, a MENU press made
    // while paused would spin briefly and then stop itself again ~350ms
    // later, leaving the disc stopped while AudioPlayer thought it was
    // playing — after which PLAY appeared to do nothing.
    DiscRenderer::setSpinning(AudioPlayer::isPlaying());

    // Swap the art and snap the disc back to 0 so a new album visibly
    // "drops" onto the panel instead of continuing mid-rotation.
    if (syncArt()) DiscRenderer::replay();
  }
}

void setup() {
  pinMode(PIN_BTN_PREV, INPUT_PULLUP);
  pinMode(PIN_BTN_PLAY, INPUT_PULLUP);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  pinMode(PIN_BTN_MENU, INPUT_PULLUP);

  // Art into RAM first, then the renderer's chord table, then the panel —
  // Display::init() expects both to be ready before anything is pushed.
  AlbumStore::init();
  DiscRenderer::init();
  Display::init();

  AudioPlayer::init();

  // Starts on playlist 0 / SD card folder 01, track 1. MENU cycles from
  // here through the rest of the PlaylistArt table.
  AudioPlayer::playPlaylist(0, 1);
  syncArt();
  DiscRenderer::setSpinning(AudioPlayer::isPlaying());
}

void loop() {
  uint32_t now = millis();

  if (released(btnPrev, now)) {
    AudioPlayer::previous();
    syncArt();   // no-op unless this playlist maps several covers across tracks
  }
  if (released(btnPlay, now)) {
    AudioPlayer::togglePlayPause();
    DiscRenderer::setSpinning(AudioPlayer::isPlaying());
  }
  if (released(btnNext, now)) {
    AudioPlayer::next();
    syncArt();
  }
  if (released(btnMenu, now)) {
    cyclePlaylist();
  }

  // Re-read the clock: the handlers above talk to the DFPlayer, which costs
  // ~10ms per command, so `now` is stale by the time we get here.
  now = millis();
  if (now - lastFrameMs >= FRAME_INTERVAL_MS) {
    lastFrameMs = now;
    DiscRenderer::tick(now);
    Display::drawDisc();
  }
}
