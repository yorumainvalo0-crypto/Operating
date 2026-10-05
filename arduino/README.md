# Dino Runner für Arduino Uno

Ein kleines Jump'n'Run im Stil des Chrome-Dinos. Der Dino springt über Kakteen und duckt sich unter Vögeln. Je weiter er läuft, desto schneller wird das Spiel. Der Highscore bleibt auch nach dem Ausschalten gespeichert.

| Ordner | Inhalt |
|---|---|
| `DinoRunner/` | das Spiel |
| `I2CScanner/` | Hilfsprogramm, um ein I2C-Display zu erkennen |

## 1. Welches Display habe ich?

| Aussehen | Pins | `DISPLAY_TYPE` |
|---|---|---|
| kleines OLED 0,96", schwarz mit weißer/blauer Schrift | 4 (GND VCC SCL SDA) | `DISPLAY_OLED_SSD1306` |
| OLED 1,3" (etwas größer) | 4 | `DISPLAY_OLED_SH1106` |
| grünes/blaues Textdisplay 16×2 **mit** kleiner Platine hinten | 4 (GND VCC SDA SCL) | `DISPLAY_LCD1602_I2C` |
| grünes/blaues Textdisplay 16×2 **ohne** Zusatzplatine | 16 | `DISPLAY_LCD1602` |
| Nokia 5110 (quadratisch, rote/blaue Platine) | 8 | `DISPLAY_NOKIA_5110` |
| Farb-TFT 1,8" (160×128) | 8+ | `DISPLAY_TFT_ST7735` |

Bei Displays mit 4 Pins hilft der Sketch `I2CScanner`: hochladen, den seriellen Monitor (9600 Baud) öffnen, und er zeigt den passenden Typ und die Adresse an.

## 2. Bibliotheken installieren

Arduino IDE → *Werkzeuge → Bibliotheken verwalten* → nur die Bibliothek für **dein** Display installieren:

| Display | Bibliothek |
|---|---|
| OLED SSD1306 | `Adafruit SSD1306` (installiert *Adafruit GFX* mit) |
| OLED SH1106 | `Adafruit SH110X` |
| Nokia 5110 | `Adafruit PCD8544 Nokia 5110 LCD library` |
| TFT ST7735 | `Adafruit ST7735 and ST7789 Library` |
| LCD 16×2 I2C | `LiquidCrystal I2C` (von Frank de Brabander) |
| LCD 16×2 ohne I2C | `LiquidCrystal` (schon vorinstalliert) |

Board: *Arduino Uno*. Dein Board ist ein Nachbau mit **CH340**-Chip. Falls kein COM-Port erscheint, musst du den CH340-Treiber installieren.

## 3. Einstellungen in `DinoRunner/config.h`

```cpp
#define DISPLAY_TYPE DISPLAY_OLED_SSD1306   // <- dein Display

#define BTN_JUMP_PIN   A0   // Pins der Taster frei wählbar
#define BTN_DUCK_PIN   A1   // -1 = Taster nicht vorhanden
#define BTN_START_PIN  A2
#define BTN_MODE_PIN   A3

#define BUZZER_PIN 6        // -1 = kein Summer
```

Die Pins A0–A3 und D6 sind bei allen Displays frei. Wenn du andere Pins nimmst, dürfen sie nicht mit den Display-Pins unten kollidieren.

## 4. Verkabelung

### Taster (4×)
Jeder Taster wird zwischen seinen Pin und **GND** geschaltet, Widerstände sind nicht nötig. Bei den 4-beinigen Tastern auf dem Steckbrett nimmst du zwei **diagonal** gegenüberliegende Beine.

```
A0 ──[JUMP]── GND      A2 ──[START]── GND
A1 ──[DUCK]── GND      A3 ──[MODE]─── GND
D6 ──(+) Summer (−)── GND   (optional)
```

### OLED / LCD mit I2C (4 Pins)
| Display | Arduino |
|---|---|
| GND | GND |
| VCC | 5V |
| SDA | A4 (oder SDA) |
| SCL | A5 (oder SCL) |

### LCD 16×2 ohne I2C
| LCD | Arduino |
|---|---|
| VSS, RW, K | GND |
| VDD, A (über 220 Ω) | 5V |
| V0 | Mittelpin eines 10k-Potis (Poti außen an 5V/GND) → Kontrast |
| RS / E | 12 / 11 |
| D4 / D5 / D6 / D7 | 5 / 4 / 3 / 2 |

### Nokia 5110
| Nokia | Arduino |
|---|---|
| RST / CE / DC | 8 / 10 / 9 |
| DIN / CLK | 11 / 13 |
| VCC | **3.3V** |
| BL | 3.3V (Hintergrundlicht, je nach Modul auch GND) |
| GND | GND |

### TFT ST7735 (1,8")
| TFT | Arduino |
|---|---|
| VCC / GND | 5V / GND |
| CS / RESET / A0 (DC) | 10 / 8 / 9 |
| SDA / SCK | 11 / 13 |
| LED | 3.3V |

## 5. Steuerung

| Taste | Titelbild | Im Spiel | Pause | Game Over |
|---|---|---|---|---|
| JUMP | Start | springen | weiter | neues Spiel |
| DUCK | – | ducken / schneller fallen | – | – |
| START | Start | Pause | weiter | neues Spiel |
| MODE | Schwierigkeit EASY/NORMAL/HARD | – | Ton an/aus | zurück zum Titel |

Auf dem 16×2-Textdisplay gibt es nur Kakteen (drüberspringen) und hoch fliegende Vögel (nicht hineinspringen).

## 6. Fehlersuche

- **Die LED „L“ auf dem Board blinkt schnell:** Das Display wurde nicht gefunden. Prüfe `DISPLAY_TYPE`, die Verkabelung und die I2C-Adresse (`I2CScanner`).
- **OLED bleibt schwarz:** Stelle `OLED_I2C_ADDR` auf `0x3D` ein oder probiere `DISPLAY_OLED_SH1106`.
- **Das LCD zeigt nur Kästchen oder nichts:** Dreh am blauen Poti hinten (Kontrast). Bei I2C probierst du die Adresse `0x3F`.
- **Am TFT sind die Farben vertauscht oder der Rand verschoben:** Stelle `TFT_TAB` auf `INITR_GREENTAB` oder `INITR_REDTAB`.
- **Das Nokia-Display ist zu hell oder zu dunkel:** Passe `NOKIA_CONTRAST` an.
