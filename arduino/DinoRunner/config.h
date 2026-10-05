// =====================================================================
//  Dino Runner - user configuration
//  Change the settings below, then upload the sketch to your Arduino.
// =====================================================================
#pragma once

// ---------------------------------------------------------------------
// 1) Select your display (set DISPLAY_TYPE to exactly one of these)
// ---------------------------------------------------------------------
#define DISPLAY_OLED_SSD1306  1  // 0.96" OLED 128x64, I2C (4 pins: GND VCC SCL SDA)
#define DISPLAY_OLED_SH1106   2  // 1.3"  OLED 128x64, I2C (looks like SSD1306, but bigger)
#define DISPLAY_NOKIA_5110    3  // Nokia 5110 LCD 84x48, SPI (8 pins: RST CE DC DIN CLK VCC BL GND)
#define DISPLAY_TFT_ST7735    4  // 1.8" color TFT 160x128, SPI
#define DISPLAY_LCD1602_I2C   5  // 16x2 text LCD with I2C backpack (4 pins)
#define DISPLAY_LCD1602       6  // 16x2 text LCD without backpack (16 pins, parallel)

#define DISPLAY_TYPE DISPLAY_OLED_SSD1306

// ---------------------------------------------------------------------
// 2) Buttons
//    Wire each button between the pin and GND (internal pull-ups are used).
//    Use -1 for a button you don't have.
// ---------------------------------------------------------------------
#define BTN_JUMP_PIN   A0  // jump / start game
#define BTN_DUCK_PIN   A1  // duck (fast fall while in the air)
#define BTN_START_PIN  A2  // start / pause
#define BTN_MODE_PIN   A3  // title: change difficulty, pause: sound on/off, game over: back to title

// ---------------------------------------------------------------------
// 3) Optional buzzer (passive or active piezo between pin and GND)
// ---------------------------------------------------------------------
#define BUZZER_PIN 6  // -1 = no buzzer

// ---------------------------------------------------------------------
// 4) Display specific settings
// ---------------------------------------------------------------------
// I2C addresses (run the I2CScanner sketch if the display stays dark)
#define OLED_I2C_ADDR 0x3C  // usually 0x3C, sometimes 0x3D
#define LCD_I2C_ADDR  0x27  // usually 0x27 or 0x3F

// SPI displays (Nokia 5110, TFT): hardware SPI uses D11 (DIN/MOSI) and D13 (CLK/SCK)
#define SPI_CS_PIN   10
#define SPI_DC_PIN   9
#define SPI_RST_PIN  8

#define NOKIA_CONTRAST 50  // 30..70, adjust if the Nokia screen is too light/dark

// TFT variant: INITR_BLACKTAB, INITR_GREENTAB or INITR_REDTAB (try another one if colors/offsets look wrong)
#define TFT_TAB INITR_BLACKTAB

// Parallel 16x2 LCD pins (classic Arduino example wiring)
#define LCD_RS_PIN 12
#define LCD_EN_PIN 11
#define LCD_D4_PIN 5
#define LCD_D5_PIN 4
#define LCD_D6_PIN 3
#define LCD_D7_PIN 2
