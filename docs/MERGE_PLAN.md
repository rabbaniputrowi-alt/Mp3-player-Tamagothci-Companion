# Stardance merge plan: TFT_folder + Pet_Test into one sketch

Written 2026-10-04 against branch `merge` (same commit as `main`, 50d9c99).
For Claude Code. Read `CLAUDE.md` at the repo root first: it has the hardware,
pins and gotchas this plan assumes.

## Goal

One Arduino sketch, `Stardance/`, that contains both the music
player (today `TFT_folder/`) and the pet (today `Tamagotchi Bee/Pet_Test/`),
with the asset pipelines moved to `tools/`.

Update 2026-10-04, after the merge: `Stardance/` and `tools/` were moved up
from `Tamagotchi Code/` to the repo root and `Tamagotchi Code/` was removed.
Paths in this plan are written for the new location; the step list below
describes the moves as they were originally done.

Each half keeps behaving the way it does today. The only new behaviour is how
the two share one screen and four buttons.

## Decisions already made by Bruhb

1. Holding MENU swaps between the pet view and the disc view.
2. A short MENU press opens the playlist chooser from either view. Backing out
   of the chooser returns to the view it was opened from.
3. PREV / PLAY / NEXT control the music in every view. In the pet view the pet
   also reacts, using the mapping Pet_Test already has:
   PREV -> WINK, PLAY -> MUSIC_SPIN, NEXT -> WINK_TRANSITION.
   Changed 2026-10-07: MUSIC_SPIN is no longer the PLAY reaction. It plays
   as the transition from the pet view into the chooser (short MENU), and
   the chooser opens when the clip ends. See "Glue" in `CLAUDE.md`. The
   state table below still shows the original mapping.
   Changed 2026-10-12: the PLAY slot MUSIC_SPIN left free is PENGUIN now
   (the dancing penguin gif, `Pet::trigger(PET_ANIM_PENGUIN, now)`).
4. Folder layout as scaffolded (see below).

Assumptions made while planning. Each is one line to change, so do not stop to
ask, just keep them easy to find:

- Boots into the pet view.
- Still auto-plays folder 01 track 1 at boot, as TFT_folder does now.
- `LONG_PRESS_MS` = 500.
- Pet_Test's MENU test stepping (lookaround, bubble face, pretend shake) is
  dropped, because MENU now has a real job.

## Target layout

```
Stardance/                    flat sketch, folder name = .ino name
  Stardance.ino               the only setup()/loop(); owns AppState + buttons
  config.h                    from TFT_folder, plus pet switches
  display.*  disc_renderer.*  album_store.*  albums.*  audio_player.*
  playlist_ui.*  playlist_art.*  backdrop.*  track_names.*
  pet.*  pet_renderer.*  pet_anims.*  motion.*  mpu6050.*
  pet_screen.*                new: what is left of Pet_Test.ino
tools/
  Spinning Disc/              build_albums.py, build_tracks.py, json, covers/
  Pet_Pipeline/               build_pet_anims.py, preview_pet_anims.py, json, clips/
  previews/
```

Arduino only compiles files sitting directly in the sketch folder, so
everything in `Stardance/` stays flat. `tools/` is outside the sketch on
purpose.

## What is in the way (found by reading both sketches)

### 1. TFT_folder does not compile as committed

`config.h` defines the chooser constants without a prefix (`ROW_PITCH`,
`MAX_PLAYLISTS`, `BG_CELL`, ...). `playlist_ui.cpp`, `audio_player.cpp` and
`backdrop.cpp` use them with a `UI_` prefix. Nothing uses the bare names.
Fix this before moving anything, otherwise merge errors and old errors are
indistinguishable. Rename in `config.h` only:

```
ROW_PITCH -> UI_ROW_PITCH          COL_BAND   -> UI_COL_BAND
BAND_Y0   -> UI_BAND_Y0            COL_SEL    -> UI_COL_SEL
BAND_Y1   -> UI_BAND_Y1            COL_SHADOW -> UI_COL_SHADOW
VISIBLE_SPAN -> UI_VISIBLE_SPAN    TEXT_NEAR  -> UI_TEXT_NEAR
TEXT_MARGIN  -> UI_TEXT_MARGIN     TEXT_FAR   -> UI_TEXT_FAR
GLYPH_HALF   -> UI_GLYPH_HALF      BG_CELL    -> UI_BG_CELL
SLIDE_MS     -> UI_SLIDE_MS        BG_BLUR_PASSES -> UI_BG_BLUR_PASSES
MAX_PLAYLISTS -> UI_MAX_PLAYLISTS  BG_TARGET_LUMA -> UI_BG_TARGET_LUMA
BG_DIM -> UI_BG_DIM                BG_SATURATION  -> UI_BG_SATURATION
```

Leave `BAND_H`, `BAND_PIXELS`, `NUM_BANDS` alone. They are a different thing
(render band height) and are used unprefixed everywhere.

### 2. RAM: both halves together do not fit

The RP2040 gives the sketch 262,144 bytes for globals plus heap. Adding up the
big buffers in the two sketches:

| Buffer | Where | Bytes |
|---|---|---|
| Album art working copy + palette | `album_store.cpp` | 58,112 |
| `bandA` + `bandB` | `display.cpp` | 46,080 |
| Pet frame texture `s_tex` | `pet_renderer.cpp` | 57,600 |
| Ball height table `s_z` | `pet_renderer.cpp` | 14,400 |
| Pet `bandBuf` | `Pet_Test.ino` | 11,520 |
| Small tables | various | ~2,300 |
| Static total | | ~190,000 |
| Chooser sprite (heap) | `playlist_ui.cpp` | 23,040 |
| Backdrop thumbs, 3 albums (heap; since 2026-10-12 a fixed 3-slot cache, 36,000) | `backdrop.cpp` | 32,400 |
| Backdrop `valid` scratch (heap, freed) | `backdrop.cpp` | 3,600 |
| Heap peak | | ~59,000 |

That is about 249 KB before the core's own RAM (USB, Serial, Wire, TFT_eSPI
object: assume 10 to 20 KB). It will either fail to link or, worse, link and
then have `malloc` fail at boot. Both heap users fail silently: a failed thumb
allocation gives a black chooser backdrop, a failed sprite gives a chooser
that never draws at all.

Fix, as part of the merge:

- Drop `bandB`. The two buffers ping-ponged for the DMA attempt, which was
  reverted. With blocking `pushImage()` one buffer is enough. Saves 23,040.
- Drop the pet's own `bandBuf` and draw the pet into the display's band
  buffer. Add `uint16_t *Display::bandBuffer();`. Saves 11,520.

That brings the static total to about 155 KB and the whole app to about
214 KB, leaving roughly 45 KB for the core and margin.

Held in reserve, not for this merge: `AlbumStore::pixels` and the pet's
`s_tex` are both 240x240 one-byte-per-pixel buffers and only one view is on
screen at a time, so they could share one 57,600 byte buffer.

### 3. Two TFT_eSPI instances

`Pet_Test.ino` declares its own `TFT_eSPI tft;`. `display.cpp` already owns
one and exposes it as `Display::panel()`. There must only ever be one. The
pet draws through `Display::panel()`, and `Display::init()` already does the
`setSwapBytes(true)` the pet relies on.

### 4. Two button systems and colliding names

Both `.ino` files define `Button`, `DEBOUNCE_MS` and the button pins.
`Pet_Test.ino` declares `const uint8_t PIN_BTN_PREV = 0;` while `config.h`
has `#define PIN_BTN_PREV 0`, which will not compile once they meet. Only the
TFT_folder versions survive, in `Stardance.ino` and `config.h`.

They also debounce differently. TFT_folder fires on the press edge with a
25 ms lockout (its comment explains why: a full-screen push blocks the loop
for about 40 ms). Pet_Test fires on a settled release. Keep the TFT_folder
approach and extend it for long press (design below).

Everything else is namespaced or file-local and does not collide:
`Pet`, `PetRenderer`, `Motion`, `Mpu6050` versus `AlbumStore`, `DiscRenderer`,
`Display`, `PlaylistUI`, `PlaylistArt`, `Backdrop`, `TrackNames`,
`AudioPlayer`.

### 5. Blocking draws versus 100 Hz motion sampling

The pet reads the MPU6050 every 10 ms and calls `updateMotion()` between its
10 draw bands to keep that rate. A disc frame blocks for about 40 ms. So:
only sample motion while the pet view is showing, and keep the pet at 24-row
bands (10 per frame) even though the shared buffer holds 48 rows. On entering
the pet view call `Motion::reset()` so however the device is held right then
becomes "straight".

### 6. `DiscRenderer::replay()` restores spin state later

`replay()` snapshots the spin state and restores it when the snap-back ends,
about 350 ms of `tick()` time later. If `tick()` stops while the disc is
hidden, that restore is still pending when the disc comes back and can undo a
pause made in the pet view (disc spins while the music is paused). So call
`DiscRenderer::tick(now)` every frame in every view. It is only float maths.
Rendering still happens in the disc view only.

### 7. Generated files are stale. Do not regenerate during the merge

- `albums.json` lists 7 albums. The committed `albums.cpp` holds 3.
- `tracks.json` was edited after `track_names.cpp` was generated.

Move `albums.*`, `track_names.*` and `pet_anims.*` byte for byte. Regenerating
changes behaviour and RAM (see follow-ups) and would make the merge impossible
to verify.

## Design

### config.h

- The `UI_` rename above.
- `enum class AppState : uint8_t { PET = 0, PLAYING, PLAYLIST_SELECT, TRACK_SELECT };`
- Add: `LONG_PRESS_MS 500`, `PET_BAND_ROWS 24`, and the three Pet_Test
  switches as `PET_TILT_FOLLOW`, `PET_SHAKE_DIZZY`, `PET_MOTION_DEBUG`.
- `mpu6050.cpp` has its own `PIN_SDA` / `PIN_SCL`. Switch it to the
  `MPU6050_SDA_PIN` / `MPU6050_SCL_PIN` that `config.h` already defines, so
  pins live in one file.
- Tuning constants stay in the `.cpp` that uses them, as the rest of the code
  does (`SPIN_DEG_S` in `disc_renderer.cpp`, `LOOK_*` in `motion.cpp`).
  `MOTION_PERIOD_MS`, `MPU_RETRY_MS`, `FOLLOW_EASE_S`, `REDRAW_RAD` go to
  `pet_screen.cpp`.

### display.h / display.cpp

- Remove `bandB` and the buffer swap in `drawDisc()`.
- Add `uint16_t *bandBuffer();` returning `bandA`, documented as shared by
  whichever view is drawing.

### pet_screen.h / pet_screen.cpp (new)

Everything from `Pet_Test.ino` except `setup()`, `loop()`, the buttons and the
MENU test. Suggested surface:

```cpp
namespace PetScreen {
  void init(uint32_t nowMs);    // PetRenderer::init(), first MPU probe, Pet::begin()
  void enter(uint32_t nowMs);   // pet view just became visible
  void tick(uint32_t nowMs);    // call every loop() while the pet view is showing
}
```

- `init()` seeds `Pet::begin()` the same way as today
  (`micros() ^ analogRead(26)`).
- `enter()` calls `Motion::reset()`, zeroes `follow` and the ball angles, and
  sets a force-redraw flag. Without the flag nothing repaints until the next
  animation frame, and the previous view stays on screen.
- `tick()` is the body of Pet_Test's `loop()` after the buttons:
  `updateMotion()`, `Pet::setShaking()`, `Pet::update()`, then `drawFrame()`
  when the frame changed, the ball moved, or a redraw is forced.
- `drawFrame()` uses `Display::panel()` and `Display::bandBuffer()`. Add
  `static_assert(PET_W == SCREEN_W && PET_BAND_ROWS <= BAND_H, ...)`.
- Keep `tryMpu()`, the 3 s retry, the 5-failures drop-out and the serial
  debug prints (behind `PET_MOTION_DEBUG`). Two open items still need those
  prints: confirming left/right tilt and measuring real frame draw time.
- The pet does not tick while hidden. `Pet::update()` already catches up
  after a gap, so nothing extra is needed.

`pet.*`, `pet_renderer.*`, `motion.*`, `pet_anims.*` move unchanged.
`pet_renderer` and `motion` are deliberately Arduino-free so they can be
tested on a PC. Do not add Arduino includes to them.

### Stardance.ino

Start from `TFT_folder.ino` (keep `syncArt()`, `startPlayback()` and the
`setSpinning()` before `replay()` ordering exactly) and add:

Buttons. One poll function, three events:

```cpp
enum class Press : uint8_t { NONE, DOWN, SHORT_UP, LONG_HOLD };

struct Button {
  uint8_t  pin;
  bool     down;
  bool     longFired;
  uint32_t lastEventMs;   // last accepted edge
};

Press poll(Button &b, uint32_t now) {
  const bool raw = (digitalRead(b.pin) == LOW);
  if (raw != b.down) {
    if (now - b.lastEventMs < DEBOUNCE_MS) return Press::NONE;  // bounce lockout, as today
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
```

PREV / PLAY / NEXT act on `DOWN`, so they feel exactly as they do now. MENU
ignores `DOWN` and acts on `SHORT_UP` and `LONG_HOLD`.

State. `appState` plus `homeView` (PET or PLAYING), the view the chooser
returns to.

| State | PREV / NEXT | PLAY | MENU short | MENU held |
|---|---|---|---|---|
| PET | previous / next track, `syncArt()`, pet WINK / WINK_TRANSITION | play/pause, `setSpinning()`, pet MUSIC_SPIN | `homeView = PET`, open playlists | go to PLAYING |
| PLAYING | previous / next track, `syncArt()` | play/pause, `setSpinning()` | `homeView = PLAYING`, open playlists | go to PET |
| PLAYLIST_SELECT | scroll | open tracks | back to `homeView` | ignored |
| TRACK_SELECT | scroll | `startPlayback()`, back to `homeView` | back to PLAYLIST_SELECT | ignored |

- Every transition into PET calls `PetScreen::enter(now)`.
- Pet reactions are `Pet::trigger(PET_ANIM_WINK, now)` and friends, called
  straight from `Stardance.ino`, and only while `appState == AppState::PET`.
- The music action always happens. `Pet::trigger()` may return false when an
  interactive clip is already playing. That is fine, ignore it.
- `DiscRenderer::setSpinning(AudioPlayer::isPlaying())` is called on PLAY in
  the pet view too, so the disc is right when it is swapped in.
- The existing `switch (appState)` blocks have no `default`. Add the PET case
  to each.

Loop:

```cpp
void loop() {
  uint32_t now = millis();
  // poll four buttons, dispatch

  now = millis();
  if (appState == AppState::PET) PetScreen::tick(now);   // not frame-gated, draws only on change

  if (now - lastFrameMs >= FRAME_INTERVAL_MS) {
    lastFrameMs = now;
    DiscRenderer::tick(now);                              // every view, see problem 6
    if (appState == AppState::PLAYING) {
      Display::drawDisc();
    } else if (appState != AppState::PET) {
      PlaylistUI::tick(now);
      if (PlaylistUI::needsRedraw()) PlaylistUI::draw();
    }
  }
}
```

Setup order:

```
Serial.begin(115200)
pinMode x4
AlbumStore::init()
DiscRenderer::init()
Display::init()
PlaylistUI::init()        // the two big heap allocations, keep them early
AudioPlayer::init()
PetScreen::init(millis())
AudioPlayer::playPlaylist(0, 1); syncArt(); DiscRenderer::setSpinning(...)
PetScreen::enter(millis())
```

Print `rp2040.getFreeHeap()` before and after `PlaylistUI::init()`. The drop
should be about 59 KB (36,000 backdrop cache + 23,040 sprite; 55 KB before 2026-10-12). A smaller drop means an
allocation failed.

## Steps

Each step ends compiling. Commit after each so a bad step is easy to back out.
Do not flash-test on Bruhb's behalf; tell him what to look for.

Step 0. Baseline. In `TFT_folder/config.h`, apply the `UI_` rename. Compile
`TFT_folder` on its own. This is the "known good" to compare against.

Step 1. Moves only, no content edits. Use `git mv` so history follows.
- `TFT_folder/` -> `Stardance/`, `TFT_folder.ino` -> `Stardance.ino`
- `Stardance/Spinning Disc/` -> `tools/Spinning Disc/`
- `Tamagotchi Bee/Pet_Pipeline/` -> `tools/Pet_Pipeline/`
- `Tamagotchi Bee/previews/` -> `tools/previews/`
- `PlaylistUI.ccp`, `PlaylistUI.h`, `TFT_folder.ino.bak` -> `Archive/`
  (stale duplicates, never compiled)
- Delete the empty `Stardance/` folder at the repo root. It is a leftover and
  not the sketch folder.
- Add a `.gitignore` with `__pycache__/` and untrack the two committed `.pyc`
  files.
Compile `Stardance`. It must behave exactly like Step 0.

Step 2. Bring the pet sources in, untouched: `git mv` `pet.*`,
`pet_renderer.*`, `pet_anims.*`, `motion.*`, `mpu6050.*` into `Stardance/`.
Leave `Pet_Test.ino` where it is for now as the reference for Step 4. Compile.
Nothing calls the pet code yet, so the binary should barely change.

Step 3. RAM diet in `display.*`: one band buffer, `bandBuffer()` accessor.
Compile. Disc view must look the same as before.

Step 4. Write `pet_screen.*` from `Pet_Test.ino`. Point `mpu6050.cpp` at the
`config.h` pin names. Add the pet constants and `AppState::PET` to `config.h`.
Compile.

Step 5. Rework `Stardance.ino`: `Press` events, `homeView`, the state table,
the new loop and setup order. Compile.

Step 6. Clean up: move `Pet_Test.ino` to `Archive/`, remove the now empty
`Tamagotchi Bee/` folder, update the "Layout" section of `CLAUDE.md` to the
merged tree and delete its "before the merge" part.

New build commands after the move (paths are relative to each tools folder):

```
cd "tools/Spinning Disc"
python build_albums.py --manifest albums.json --out ../../Stardance
python build_tracks.py --manifest tracks.json --out ../../Stardance

cd "tools/Pet_Pipeline"
python build_pet_anims.py --manifest pet_anims.json --out ../../Stardance
python preview_pet_anims.py --sketch ../../Stardance --out ../previews
```

The scripts resolve `covers/` and `clips/` relative to their manifest, so they
keep working as long as those folders move with the json. Do not run the two
album/track builders as part of this merge (problem 7).

## Checks

Compile (Arduino IDE, or arduino-cli if it is installed):
- Board `Waveshare RP2040 Zero`, arduino-pico core. FQBN
  `rp2040:rp2040:waveshare_rp2040_zero`.
- Flash Size must be "2MB (no FS)". Confirm the option id with
  `arduino-cli board details -b rp2040:rp2040:waveshare_rp2040_zero`.
- Expected flash: about 1.72 MB (pet 1,183 KB with the penguin + 7 albums
  407 KB + code and fonts).
- Read the "Global variables use N bytes ... leaving M bytes" line after
  Step 5, when the pet code is actually linked in (until then the linker
  drops it). Expect N around 165 to 175 KB and M around 90 KB. The chooser
  needs 59 KB of that at boot. If M is under about 70 KB, stop and report
  instead of continuing. For reference, without Step 3 it would be about
  N = 200 KB, M = 55 to 60 KB, which is the silent-failure case.
- If `TFT_eSPI` behaves as if `User_Setup.h` were wrong: that file lives in
  the Arduino libraries folder, not in this repo, and the IDE caches its
  compile. See the gotchas in `CLAUDE.md`.

On the device, for Bruhb, in this order:
1. Boots to the pet. Idle blinks happen, tilt moves the face, shaking gives
   the dizzy sequence.
2. PREV / PLAY / NEXT change the music. PREV makes the pet wink, NEXT plays
   the affection clip, PLAY shows the dancing penguin (since 2026-10-12):
   it appears on the press in its own blue / orange colours and keeps
   looping (since 2026-10-18). A second PLAY stops it and the normal face
   comes back in its normal colours; MENU (short or held) also stops it.
   PREV / NEXT and shaking leave it dancing. Every PLAY still pauses /
   resumes the music.
3. Short MENU in the pet view: the pet plays Music Spin straight away (wink,
   spin down to a dot, music-note face, spin out to a dot), then the chooser
   appears, with no flash of the normal face in between. A second short
   MENU during the clip jumps to the chooser. Pick a track: music changes,
   back on the pet.
4. Hold MENU: disc view, art matches the playing track, spinning only if
   playing. Hold again: pet, drawn immediately.
5. Pause in the pet view, hold MENU: the disc is stopped.
6. Open the chooser from the disc view and back out: lands on the disc.
7. Serial at 115200: free heap before/after `PlaylistUI::init()`,
   "MPU6050 found", "frame draw N us".

## Not part of this merge

Found while reading. Leave them, but do not trip over them:

- Fixed 2026-10-12: regenerating albums broke on
  `covers/DivineDestiny.jpg` (the file is `.png`); `playlist_art.cpp` and
  `tracks.json` only knew 3 playlists; backdrop thumbs cost 10,800 bytes of
  heap per album. The cover path is corrected, all 7 albums are generated,
  `PLAYLIST_ART[]` and `tracks.json` have folders 01-07, and the backdrop
  builds thumbnails on demand into a fixed 3-slot cache (36 KB) instead.
- Fixed 2026-10-12: `tracks.json` folder 02 had `"tracks": [""]`, which
  would have counted as one track; it is `[]` now.
- `config.h` has `TFT_SCLK_PIN 14` / `TFT_MOSI_PIN 15`. That matches the real
  wiring and `User_Setup.h` (SCL on GP14, SDA on GP15; SPI1 can only put SCK
  on GP14 here). Nothing reads those five `TFT_*_PIN` defines; the real pins
  are in TFT_eSPI's `User_Setup.h`. (An earlier draft of this note had them
  reversed; corrected 2026-10-04.)
- `LOOK_SIGN_LR` was flipped in `motion.cpp` and has not been confirmed on
  hardware yet.

## House rules for this repo

- Give Bruhb whole files, not partial diffs.
- Keep the existing explanatory comments. A trimmed-comment version was tried
  and reverted.
- Working first, polish later. Do not add features this plan does not list.
- Never reintroduce: vinyl grooves or rings, the centre white circle, glare
  wedges, the waveform player GUI, DMA pushes.
- Never hand-edit `albums.*`, `track_names.*` or `pet_anims.*`.
