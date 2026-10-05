// I2C scanner: finds I2C displays and tells you which DISPLAY_TYPE to use.
// Wiring: display SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND.
// Open the Serial Monitor at 9600 baud to see the result.
#include <Wire.h>

void setup() {
  Serial.begin(9600);
  Wire.begin();
  while (!Serial) {}
}

void loop() {
  uint8_t found = 0;
  Serial.println(F("Scanning I2C bus..."));

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    found++;

    Serial.print(F("  Device at 0x"));
    if (addr < 16) Serial.print('0');
    Serial.print(addr, HEX);

    if (addr == 0x3C || addr == 0x3D) {
      Serial.println(F("  -> OLED display: DISPLAY_OLED_SSD1306 (0.96\") or DISPLAY_OLED_SH1106 (1.3\")"));
      Serial.println(F("     set OLED_I2C_ADDR in config.h to this address"));
    } else if ((addr >= 0x20 && addr <= 0x27) || (addr >= 0x38 && addr <= 0x3F)) {
      Serial.println(F("  -> 16x2 LCD with I2C backpack: DISPLAY_LCD1602_I2C"));
      Serial.println(F("     set LCD_I2C_ADDR in config.h to this address"));
    } else {
      Serial.println(F("  -> unknown device"));
    }
  }

  if (found == 0) {
    Serial.println(F("No I2C device found. Check wiring, or your display uses SPI/parallel pins."));
  }
  Serial.println();
  delay(5000);
}
