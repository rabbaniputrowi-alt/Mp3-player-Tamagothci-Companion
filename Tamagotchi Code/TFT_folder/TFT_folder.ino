#include "config.h"
#include "album_store.h"
#include "disc_renderer.h"
#include "display.h"

namespace {
  // Assumes a standard active-low button: one leg to PIN_BTN_PLAY, the
  // other to GND, using the RP2040's internal pull-up. If your button is
  // wired the opposite way (active-high), flip the == LOW check below.
  constexpr uint16_t LONG_PRESS_MS = 600;

  bool     btnDown        = false;
  bool     longPressFired = false;
  uint32_t btnDownAt      = 0;

  void handlePlayButton(uint32_t now) {
    bool pressed = (digitalRead(PIN_BTN_PLAY) == LOW);

    if (pressed && !btnDown) {
      btnDown        = true;
      longPressFired = false;
      btnDownAt      = now;
    } else if (pressed && btnDown && !longPressFired) {
      if (now - btnDownAt >= LONG_PRESS_MS) {
        longPressFired = true;
        DiscRenderer::replay();
      }
    } else if (!pressed && btnDown) {
      if (!longPressFired) {
        // Short press released before the long-press threshold: toggle
        // play/pause instead.
        DiscRenderer::setSpinning(!DiscRenderer::isSpinning());
      }
      btnDown = false;
    }
  }
}

void setup() {
  pinMode(PIN_BTN_PLAY, INPUT_PULLUP);

  AlbumStore::init();
  Display::init();
  DiscRenderer::init();
  DiscRenderer::setSpinning(true);
}

void loop() {
  uint32_t now = millis();
  handlePlayButton(now);
  DiscRenderer::tick(now);
  Display::drawDisc();
}
