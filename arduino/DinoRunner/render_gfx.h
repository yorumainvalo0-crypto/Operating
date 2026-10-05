// Renderer for graphic displays (OLED SSD1306/SH1106, Nokia 5110, TFT ST7735).
#pragma once
#include <Adafruit_GFX.h>
#include "game.h"
#include "sprites.h"

// ---------------------------------------------------------------------
// Display driver selection
// ---------------------------------------------------------------------
#if DISPLAY_TYPE == DISPLAY_OLED_SSD1306
  #include <Wire.h>
  #include <Adafruit_SSD1306.h>
  Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
  #define COLOR_BG     SSD1306_BLACK
  #define COLOR_DINO   SSD1306_WHITE
  #define COLOR_CACTUS SSD1306_WHITE
  #define COLOR_BIRD   SSD1306_WHITE
  #define COLOR_GROUND SSD1306_WHITE
  #define COLOR_TEXT   SSD1306_WHITE
  bool displayInit() { return display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR); }

#elif DISPLAY_TYPE == DISPLAY_OLED_SH1106
  #include <Wire.h>
  #include <Adafruit_SH110X.h>
  Adafruit_SH1106G display(SCREEN_W, SCREEN_H, &Wire, -1);
  #define COLOR_BG     SH110X_BLACK
  #define COLOR_DINO   SH110X_WHITE
  #define COLOR_CACTUS SH110X_WHITE
  #define COLOR_BIRD   SH110X_WHITE
  #define COLOR_GROUND SH110X_WHITE
  #define COLOR_TEXT   SH110X_WHITE
  bool displayInit() { return display.begin(OLED_I2C_ADDR, true); }

#elif DISPLAY_TYPE == DISPLAY_NOKIA_5110
  #include <SPI.h>
  #include <Adafruit_PCD8544.h>
  Adafruit_PCD8544 display(SPI_DC_PIN, SPI_CS_PIN, SPI_RST_PIN);
  // Nokia: BLACK pixels on a light background
  #define COLOR_BG     WHITE
  #define COLOR_DINO   BLACK
  #define COLOR_CACTUS BLACK
  #define COLOR_BIRD   BLACK
  #define COLOR_GROUND BLACK
  #define COLOR_TEXT   BLACK
  bool displayInit() {
    if (!display.begin()) return false;
    display.setContrast(NOKIA_CONTRAST);
    return true;
  }

#elif DISPLAY_TYPE == DISPLAY_TFT_ST7735
  #include <SPI.h>
  #include <Adafruit_ST7735.h>
  Adafruit_ST7735 display(SPI_CS_PIN, SPI_DC_PIN, SPI_RST_PIN);
  #define COLOR_BG     ST77XX_BLACK
  #define COLOR_DINO   ST77XX_WHITE
  #define COLOR_CACTUS ST77XX_GREEN
  #define COLOR_BIRD   ST77XX_YELLOW
  #define COLOR_GROUND 0x8410  // gray
  #define COLOR_TEXT   ST77XX_WHITE
  bool displayInit() {
    display.initR(TFT_TAB);
    display.setRotation(1);  // landscape 160x128
    display.fillScreen(COLOR_BG);
    return true;
  }
#endif

// Each moving sprite has a slot: 0 = dino, 1.. = obstacles.
// Unbuffered displays (TFT) remember the last drawn rectangle so only the
// uncovered part has to be erased (no flicker).
#define SLOT_DINO 0
#define SLOT_COUNT (1 + MAX_OBSTACLES)

#if !GFX_BUFFERED
struct Rect { int16_t x, y, w, h; };
Rect prevRect[SLOT_COUNT];
GameState lastState = (GameState)0xFF;
uint8_t lastOverlayKey = 0xFF;
uint16_t lastScoreDrawn = 0xFFFF;

// Erase the part of rectangle o that is not covered by rectangle n
void eraseDiff(const Rect& o, const Rect& n) {
  if (o.w == 0) return;
  int16_t ox2 = o.x + o.w, oy2 = o.y + o.h;
  int16_t nx2 = n.x + n.w, ny2 = n.y + n.h;
  if (n.w == 0 || nx2 <= o.x || n.x >= ox2 || ny2 <= o.y || n.y >= oy2) {
    display.fillRect(o.x, o.y, o.w, o.h, COLOR_BG);
    return;
  }
  if (n.x > o.x) display.fillRect(o.x, o.y, n.x - o.x, o.h, COLOR_BG);
  if (nx2 < ox2) display.fillRect(nx2, o.y, ox2 - nx2, o.h, COLOR_BG);
  int16_t ix = max(o.x, n.x), ix2 = min(ox2, nx2);
  if (n.y > o.y) display.fillRect(ix, o.y, ix2 - ix, n.y - o.y, COLOR_BG);
  if (ny2 < oy2) display.fillRect(ix, ny2, ix2 - ix, oy2 - ny2, COLOR_BG);
}

void resetSlots() {
  for (uint8_t i = 0; i < SLOT_COUNT; i++) prevRect[i].w = 0;
}
#endif

void drawSprite(uint8_t slot, int16_t x, int16_t y, const uint8_t* bmp, uint8_t w, uint8_t h, uint16_t color) {
#if GFX_BUFFERED
  (void)slot;
  display.drawBitmap(x, y, bmp, w, h, color);
#else
  Rect n = { x, y, w, h };
  eraseDiff(prevRect[slot], n);
  display.drawBitmap(x, y, bmp, w, h, color, COLOR_BG);
  prevRect[slot] = n;
#endif
}

void hideSprite(uint8_t slot) {
#if GFX_BUFFERED
  (void)slot;
#else
  if (prevRect[slot].w) {
    display.fillRect(prevRect[slot].x, prevRect[slot].y, prevRect[slot].w, prevRect[slot].h, COLOR_BG);
    prevRect[slot].w = 0;
  }
#endif
}

// ---------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------
void textAt(int16_t x, int16_t y, const char* s, uint8_t size = 1) {
  display.setTextSize(size);
  display.setCursor(x, y);
  display.print(s);
}

void textAt(int16_t x, int16_t y, const __FlashStringHelper* s, uint8_t size = 1) {
  display.setTextSize(size);
  display.setCursor(x, y);
  display.print(s);
}

void textCentered(int16_t y, const __FlashStringHelper* s, uint8_t size = 1) {
  int16_t w = strlen_P((const char*)s) * 6 * size;
  textAt((SCREEN_W - w) / 2, y, s, size);
}

void textCentered(int16_t y, const char* s, uint8_t size = 1) {
  int16_t w = strlen(s) * 6 * size;
  textAt((SCREEN_W - w) / 2, y, s, size);
}

void formatScore(char* buf, uint16_t v) {
  sprintf_P(buf, PSTR("%05u"), v);
}

// ---------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------
void drawTitle() {
  char buf[16];
  display.setTextColor(COLOR_TEXT, COLOR_BG);

#if SCREEN_H >= 64
  const int16_t top = (SCREEN_H - 64) / 2;  // centers the layout on bigger screens
  textCentered(top + 2, F("DINO RUNNER"), SCREEN_W >= 132 ? 2 : 1);
  display.drawBitmap((SCREEN_W - 16) / 2, top + 16, SPR_DINO_JUMP, 16, 16, COLOR_DINO);
  const int16_t yMode = top + 36, yHi = top + 45, yStart = top + 55;
#else
  textCentered(0, F("DINO RUNNER"));
  const int16_t yMode = 14, yHi = 24, yStart = 38;
#endif

  char name[7];
  strcpy_P(name, DIFF_NAMES[game.difficulty]);
  sprintf_P(buf, PSTR("< %s >"), name);
  textCentered(yMode, buf);

  char score[6];
  formatScore(score, game.hiScore[game.difficulty]);
  sprintf_P(buf, PSTR("HI %s"), score);
  textCentered(yHi, buf);

#if GFX_BUFFERED
  if ((millis() / 500) % 2) textCentered(yStart, F("JUMP = Start"));
#else
  textCentered(yStart, F("JUMP = Start"));
#endif
}

void drawGround() {
#if GFX_BUFFERED
  display.drawFastHLine(0, GROUND_Y, SCREEN_W, COLOR_GROUND);
#else
  display.fillRect(0, GROUND_Y + 1, SCREEN_W, 3, COLOR_BG);
#endif
  // Scrolling pebbles below the ground line
  int16_t offset = game.groundOffset >> FP_SHIFT;
  for (uint8_t i = 0; i < SCREEN_W / 16; i++) {
    int16_t x = (int16_t)(i * 37 + 11) % SCREEN_W - offset;
    if (x < 0) x += SCREEN_W;
    display.drawPixel(x, GROUND_Y + 2 + (i % 2), COLOR_GROUND);
    if (i % 3 == 0) display.drawPixel(x + 1, GROUND_Y + 2 + (i % 2), COLOR_GROUND);
  }
}

void drawScene() {
  drawGround();

  // Obstacles
  for (uint8_t i = 0; i < MAX_OBSTACLES; i++) {
    const Obstacle& o = game.obs[i];
    if (!o.active) { hideSprite(1 + i); continue; }
    const ObstacleDef& d = OB_DEFS[o.type];
    int16_t x = o.x >> FP_SHIFT;
    int16_t y = GROUND_Y - d.alt - d.h;
    switch (o.type) {
      case OB_CACTUS_S: drawSprite(1 + i, x, y, SPR_CACTUS_S, d.w, d.h, COLOR_CACTUS); break;
      case OB_CACTUS_L: drawSprite(1 + i, x, y, SPR_CACTUS_L, d.w, d.h, COLOR_CACTUS); break;
      default: {
        const uint8_t* bmp = ((game.frame / 8) % 2) ? SPR_BIRD_UP : SPR_BIRD_DOWN;
        drawSprite(1 + i, x, y, bmp, d.w, d.h, COLOR_BIRD);
      }
    }
  }

  // Dino (drawn last so it stays visible)
  const uint8_t* bmp;
  bool runFrameA = (game.frame / 4) % 2;
  if (game.state == STATE_GAME_OVER) bmp = SPR_DINO_DEAD;
  else if (game.dinoY > 0)           bmp = SPR_DINO_JUMP;
  else if (game.ducking)             bmp = runFrameA ? SPR_DINO_DUCK_A : SPR_DINO_DUCK_B;
  else                               bmp = runFrameA ? SPR_DINO_RUN_A : SPR_DINO_RUN_B;
  int16_t y = GROUND_Y - 16 - (game.dinoY >> FP_SHIFT);
  drawSprite(SLOT_DINO, DINO_X, y, bmp, 16, 16, COLOR_DINO);
}

void drawHud(bool withHiScore) {
  char score[6];
  display.setTextColor(COLOR_TEXT, COLOR_BG);
  if (withHiScore) {
    formatScore(score, game.hiScore[game.difficulty]);
    textAt(0, 0, F("HI"));
    textAt(14, 0, score);
  }
  formatScore(score, game.score);
  textAt(SCREEN_W - 30, 0, score);
}

// Framed box in the middle of the screen with up to two lines of text
void drawBox(const __FlashStringHelper* line1, const char* line2) {
  const int16_t w = SCREEN_W < 100 ? SCREEN_W - 4 : 100, h = line2 ? 24 : 14;
  const int16_t x = (SCREEN_W - w) / 2, y = (GROUND_Y - h) / 2 + 2;
  display.fillRect(x, y, w, h, COLOR_BG);
  display.drawRect(x, y, w, h, COLOR_TEXT);
  display.setTextColor(COLOR_TEXT, COLOR_BG);
  textCentered(y + 3, line1);
  if (line2) textCentered(y + 13, line2);
}

void drawPauseBox() {
  drawBox(F("PAUSE"), game.soundOn ? "Sound ON " : "Sound OFF");
}

void drawGameOverBox() {
  char buf[16], score[6];
  formatScore(score, game.score);
  if (game.newHiScore) sprintf_P(buf, PSTR("NEW HI %s"), score);
  else                 sprintf_P(buf, PSTR("Score %s"), score);
  drawBox(F("GAME OVER"), buf);
}

// ---------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------
bool displayBegin() {
  if (!displayInit()) return false;
  display.setTextWrap(false);
#if !GFX_BUFFERED
  resetSlots();
#endif
  return true;
}

#if GFX_BUFFERED
// Displays with frame buffer: redraw everything, then send the buffer
void render() {
  display.clearDisplay();
  if (game.state == STATE_TITLE) {
    drawTitle();
  } else {
    drawScene();
    drawHud(true);
    if (game.state == STATE_PAUSED) drawPauseBox();
    if (game.state == STATE_GAME_OVER) drawGameOverBox();
  }
  display.display();
}

#else
// Displays without frame buffer: redraw static parts only when something changes
void render() {
  GameState prev = lastState;
  bool stateChanged = game.state != prev;
  lastState = game.state;

  switch (game.state) {
    case STATE_TITLE: {
      uint8_t key = game.difficulty;
      if (stateChanged || key != lastOverlayKey) {
        display.fillScreen(COLOR_BG);
        drawTitle();
        lastOverlayKey = key;
      }
      break;
    }

    case STATE_RUNNING:
      if (stateChanged) {
        if (prev == STATE_PAUSED) {
          // Remove the pause box and force all sprites to be redrawn
          display.fillRect(0, 0, SCREEN_W, GROUND_Y, COLOR_BG);
        } else {
          display.fillScreen(COLOR_BG);
          display.drawFastHLine(0, GROUND_Y, SCREEN_W, COLOR_GROUND);
        }
        resetSlots();
        drawHud(true);
        lastScoreDrawn = game.score;
      }
      drawScene();
      if (game.score != lastScoreDrawn) {
        drawHud(false);
        lastScoreDrawn = game.score;
      }
      break;

    case STATE_PAUSED:
      if (stateChanged || game.soundOn != lastOverlayKey) {
        drawPauseBox();
        lastOverlayKey = game.soundOn;
      }
      break;

    case STATE_GAME_OVER:
      if (stateChanged) {
        drawScene();  // shows the crashed dino
        drawHud(false);
        drawGameOverBox();
      }
      break;
  }
}
#endif
