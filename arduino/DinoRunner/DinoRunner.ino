// =====================================================================
//  Dino Runner - a small jump'n'run game for Arduino Uno
//
//  All settings (display type, button pins, buzzer) are in config.h.
//
//  Controls:
//    JUMP   jump, start the game
//    DUCK   duck, fall faster while in the air
//    START  start / pause
//    MODE   title screen: change difficulty
//           pause:        sound on/off
//           game over:    back to title screen
// =====================================================================
#include "config.h"
#include "platform.h"
#include "game.h"
#if IS_LCD
  #include "render_lcd.h"
#else
  #include "render_gfx.h"
#endif

// ---------------------------------------------------------------------
// Buttons (active LOW, internal pull-ups)
// ---------------------------------------------------------------------
enum { BTN_JUMP, BTN_DUCK, BTN_START, BTN_MODE, BTN_COUNT };

const int8_t BUTTON_PINS[BTN_COUNT] = { BTN_JUMP_PIN, BTN_DUCK_PIN, BTN_START_PIN, BTN_MODE_PIN };
bool btnDown[BTN_COUNT];     // currently held
bool btnPressed[BTN_COUNT];  // pressed since the last frame

void setupButtons() {
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    if (BUTTON_PINS[i] >= 0) pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  }
}

// Called once per frame; the frame time (33 ms) also debounces the buttons
void readButtons() {
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    bool down = BUTTON_PINS[i] >= 0 && digitalRead(BUTTON_PINS[i]) == LOW;
    btnPressed[i] = down && !btnDown[i];
    btnDown[i] = down;
  }
}

// ---------------------------------------------------------------------
// Setup and main loop
// ---------------------------------------------------------------------
void setup() {
  setupButtons();
  loadSave();
  game.state = STATE_TITLE;

  // Serial is not used on purpose: OLED displays need 1 KB RAM for their frame buffer
  if (!displayBegin()) {
    // Display not found: check DISPLAY_TYPE, wiring and I2C address in config.h
    pinMode(LED_BUILTIN, OUTPUT);
    while (true) {  // blink the onboard LED as error signal
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(200);
    }
  }
}

void loop() {
  static uint32_t lastFrame = 0;
  uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  readButtons();

  switch (game.state) {
    case STATE_TITLE:
      if (btnPressed[BTN_JUMP] || btnPressed[BTN_START]) {
        startGame();
      } else if (btnPressed[BTN_MODE]) {
        game.difficulty = (game.difficulty + 1) % 3;
        writeSave();
        beep(660, 30);
      }
      break;

    case STATE_RUNNING:
      if (btnPressed[BTN_START]) game.state = STATE_PAUSED;
      else gameUpdate(btnPressed[BTN_JUMP], btnDown[BTN_DUCK]);
      break;

    case STATE_PAUSED:
      if (btnPressed[BTN_START] || btnPressed[BTN_JUMP]) {
        game.state = STATE_RUNNING;
      } else if (btnPressed[BTN_MODE]) {
        game.soundOn = !game.soundOn;
        writeSave();
        beep(660, 30);
      }
      break;

    case STATE_GAME_OVER:
      if (now - game.gameOverAt < 700) break;  // avoid restarting by accident
      if (btnPressed[BTN_JUMP] || btnPressed[BTN_START]) startGame();
      else if (btnPressed[BTN_MODE]) game.state = STATE_TITLE;
      break;
  }

  render();
}
