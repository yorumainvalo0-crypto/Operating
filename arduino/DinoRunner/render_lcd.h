// Renderer for 16x2 character LCDs (with or without I2C backpack).
#pragma once
#include "game.h"

#if DISPLAY_TYPE == DISPLAY_LCD1602_I2C
  #include <Wire.h>
  #include <LiquidCrystal_I2C.h>
  LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
  bool displayInit() {
    lcd.init();
    lcd.backlight();
    return true;
  }
#else
  #include <LiquidCrystal.h>
  LiquidCrystal lcd(LCD_RS_PIN, LCD_EN_PIN, LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN);
  bool displayInit() {
    lcd.begin(LCD_COLS, LCD_ROWS);
    return true;
  }
#endif

// Custom characters (5x8 pixels each, max. 8)
enum : uint8_t { CH_RUN_A, CH_RUN_B, CH_JUMP, CH_DUCK, CH_DEAD, CH_CACTUS, CH_BIRD_UP, CH_BIRD_DOWN };

const uint8_t LCD_GLYPHS[8][8] PROGMEM = {
  { 0b00111, 0b00101, 0b00111, 0b10110, 0b11111, 0b01110, 0b01010, 0b01000 },  // run A
  { 0b00111, 0b00101, 0b00111, 0b10110, 0b11111, 0b01110, 0b01010, 0b00010 },  // run B
  { 0b00111, 0b00101, 0b00111, 0b10110, 0b11111, 0b01110, 0b01010, 0b01010 },  // jump
  { 0b00000, 0b00000, 0b00000, 0b00000, 0b00011, 0b11111, 0b01110, 0b01010 },  // duck
  { 0b00111, 0b00111, 0b00111, 0b10110, 0b11111, 0b01110, 0b01010, 0b10001 },  // crashed
  { 0b00100, 0b00101, 0b10101, 0b10101, 0b11111, 0b00100, 0b00100, 0b00100 },  // cactus
  { 0b00100, 0b00110, 0b10111, 0b11111, 0b00110, 0b00000, 0b00000, 0b00000 },  // bird, wings up
  { 0b00000, 0b00000, 0b10000, 0b11111, 0b00111, 0b00110, 0b00100, 0b00000 },  // bird, wings down
};

// Screen content is composed in a buffer; only changed cells are sent to the LCD
char lcdBuf[LCD_ROWS][LCD_COLS];
char lcdShown[LCD_ROWS][LCD_COLS];

void putStr(uint8_t row, uint8_t col, const __FlashStringHelper* s) {
  const char* p = (const char*)s;
  char c;
  while (col < LCD_COLS && (c = pgm_read_byte(p++))) lcdBuf[row][col++] = c;
}

void putStrRam(uint8_t row, uint8_t col, const char* s) {
  while (col < LCD_COLS && *s) lcdBuf[row][col++] = *s++;
}

void putNum(uint8_t row, uint8_t col, uint16_t v, uint8_t digits) {
  for (int8_t i = digits - 1; i >= 0; i--) {
    if (col + i < LCD_COLS) lcdBuf[row][col + i] = '0' + v % 10;
    v /= 10;
  }
}

void flushLcd() {
  for (uint8_t r = 0; r < LCD_ROWS; r++) {
    int8_t cursorCol = -1;
    for (uint8_t c = 0; c < LCD_COLS; c++) {
      if (lcdBuf[r][c] == lcdShown[r][c]) { cursorCol = -1; continue; }
      if (cursorCol != (int8_t)c) lcd.setCursor(c, r);
      lcd.write((uint8_t)lcdBuf[r][c]);
      lcdShown[r][c] = lcdBuf[r][c];
      cursorCol = c + 1;  // LCD advances the cursor automatically
    }
  }
}

void drawLcdScene() {
  // Score top right (birds are drawn on top of it)
  putNum(0, LCD_COLS - 5, game.score, 5);

  for (uint8_t i = 0; i < MAX_OBSTACLES; i++) {
    const Obstacle& o = game.obs[i];
    if (!o.active) continue;
    int8_t col = obstacleCol(o);
    if (col < 0 || col >= LCD_COLS) continue;
    uint8_t ch = CH_CACTUS;
    if (obstacleRow(o) == 0) ch = ((game.frame / 8) % 2) ? CH_BIRD_UP : CH_BIRD_DOWN;
    lcdBuf[obstacleRow(o)][col] = ch;
  }

  uint8_t ch;
  if (game.state == STATE_GAME_OVER) ch = CH_DEAD;
  else if (game.dinoY > 0)           ch = CH_JUMP;
  else if (game.ducking)             ch = CH_DUCK;
  else                               ch = ((game.frame / 4) % 2) ? CH_RUN_A : CH_RUN_B;
  lcdBuf[dinoRow()][LCD_DINO_COL] = ch;
}

// ---------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------
bool displayBegin() {
  if (!displayInit()) return false;
  uint8_t glyph[8];
  for (uint8_t i = 0; i < 8; i++) {
    memcpy_P(glyph, LCD_GLYPHS[i], 8);
    lcd.createChar(i, glyph);
  }
  lcd.clear();
  memset(lcdShown, ' ', sizeof(lcdShown));
  return true;
}

void render() {
  memset(lcdBuf, ' ', sizeof(lcdBuf));
  char name[7];

  switch (game.state) {
    case STATE_TITLE:
      putStr(0, 2, F("DINO RUNNER"));
      lcdBuf[0][0] = CH_RUN_A;
      if ((millis() / 1500) % 2) {
        putStr(1, 0, F("JUMP = Start"));
      } else {
        putStr(1, 0, F("HI"));
        putNum(1, 3, game.hiScore[game.difficulty], 5);
        strcpy_P(name, DIFF_NAMES[game.difficulty]);
        putStrRam(1, LCD_COLS - strlen(name), name);
      }
      break;

    case STATE_RUNNING:
      drawLcdScene();
      break;

    case STATE_PAUSED:
      drawLcdScene();
      putStr(0, 3, F("PAUSE"));
      putStr(1, 6, game.soundOn ? F("Sound ON") : F("Sound OFF"));
      break;

    case STATE_GAME_OVER:
      if (millis() - game.gameOverAt < 1500) {
        drawLcdScene();  // show the crash for a moment
        putStr(0, 2, F("GAME OVER"));
      } else {
        putStr(0, 0, F("GAME OVER"));
        putNum(0, LCD_COLS - 5, game.score, 5);
        putStr(1, 0, F("HI"));
        putNum(1, 3, game.hiScore[game.difficulty], 5);
        if (game.newHiScore) putStr(1, 10, F("NEW!"));
      }
      break;
  }
  flushLcd();
}
