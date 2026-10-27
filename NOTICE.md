# Third-party components

| Component | Where | License |
|---|---|---|
| [LVGL](https://github.com/lvgl/lvgl) 9.2.2 | downloaded by PlatformIO | MIT |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) 7.2.1 | downloaded by PlatformIO | MIT |
| [Adafruit Fingerprint Sensor Library](https://github.com/adafruit/Adafruit-Fingerprint-Sensor-Library) 2.1.3 | downloaded by PlatformIO | BSD |
| [arduino-esp32](https://github.com/espressif/arduino-esp32) / ESP-IDF | PlatformIO `espressif32@6.6.0` | LGPL-2.1 / Apache-2.0 |
| [Poppins](https://fonts.google.com/specimen/Poppins) (Indian Type Foundry) | converted into `src/fuentes/*.c` | SIL Open Font License 1.1 |
| Montserrat (symbols, built into LVGL) | LVGL fallback fonts | SIL Open Font License 1.1 |

Display pinout and init sequence for the ES3C35P were checked against the LCDWiki specification and
against [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) and [NerdMiner](https://github.com/BitMaker-hub/NerdMiner_v2). The AS608
packet format follows the ZFM-20 user manual.

The short melodies in `temas/` are based on public-domain tunes (Jingle Bells, Bach's Toccata and Fugue in D minor, Auld Lang Syne...).
