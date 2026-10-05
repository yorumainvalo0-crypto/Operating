// Derived settings per display type (screen geometry and game tuning).
// Normally you don't need to change anything here.
#pragma once
#include "config.h"

#define IS_LCD (DISPLAY_TYPE == DISPLAY_LCD1602_I2C || DISPLAY_TYPE == DISPLAY_LCD1602)

#if DISPLAY_TYPE == DISPLAY_OLED_SSD1306 || DISPLAY_TYPE == DISPLAY_OLED_SH1106
  #define SCREEN_W 128
  #define SCREEN_H 64
  #define GROUND_Y 58          // screen row of the ground line
  #define GFX_BUFFERED 1       // display has a RAM frame buffer -> full redraw each frame
#elif DISPLAY_TYPE == DISPLAY_NOKIA_5110
  #define SCREEN_W 84
  #define SCREEN_H 48
  #define GROUND_Y 45
  #define GFX_BUFFERED 1
#elif DISPLAY_TYPE == DISPLAY_TFT_ST7735
  #define SCREEN_W 160
  #define SCREEN_H 128
  #define GROUND_Y 100
  #define GFX_BUFFERED 0       // no frame buffer -> erase/redraw only what moved
#elif IS_LCD
  #define SCREEN_W 128         // virtual world: 16 columns x 8 "pixels"
  #define LCD_COLS 16
  #define LCD_ROWS 2
  #define LCD_DINO_COL 1
  #define CELL_COLLISION 1     // collisions are checked per character cell
#else
  #error "Unknown DISPLAY_TYPE - check config.h"
#endif

// --- Game tuning (speeds in 1/16 pixel per frame) ---
#define FRAME_MS         33    // ~30 frames per second
#define JUMP_VELOCITY    92    // jump apex ~29 px
#define GRAVITY          9
#define FAST_FALL_GRAVITY 27   // gravity while DUCK is held in the air
#define MIN_GAP_EXTRA    24    // extra pixels between obstacles beyond one jump distance
#define SCORE_PIXELS     4     // run distance (px) per score point

#if IS_LCD
  // A text LCD reacts slowly, so the game runs slower and skips some obstacle types
  #define SPEED_START      20
  #define SPEED_MAX        48
  #define SPEED_STEP_SCORE 40  // +1 speed every N points
  #define DIFF_SPEED_BONUS 5
  #define GAP_RANDOM       40
  #define USE_LARGE_CACTUS 0
  #define USE_LOW_BIRDS    0
  #define LCD_JUMP_THRESHOLD (12 << 4)  // altitude above which the dino is shown in the top row
#else
  #define SPEED_START      32
  #define SPEED_MAX        80
  #define SPEED_STEP_SCORE 25
  #define DIFF_SPEED_BONUS 8
  #define GAP_RANDOM       60
  #define USE_LARGE_CACTUS 1
  #define USE_LOW_BIRDS    1
#endif
