# AGENTS.md: working on Open Clock-In

Firmware for a fingerprint time clock (ESP32-S3 + AS608), a reference server, a Windows simulator
and a 3D-printed case. Read this file before changing anything.

## Glossary (the code is in Spanish)

| Spanish | Meaning |
|---|---|
| reloj | the clock (this device) |
| panel | the server/backend (any server that implements `docs/API.md`) |
| perfil / nombre | person id / display name |
| huella | finger number stored in the sensor (0–999), or the sensor itself |
| plantilla | fingerprint template (raw bytes from the AS608, 512 B) |
| marca, ENTRADA / SALIDA | punch, in / out |
| repetida | same person again within 2 min, not counted |
| cola | offline queue of punches (`/cola.txt` in flash) |
| oficina | which clock/site a punch came from (A, B…) |
| ajustes / clave | settings (NVS) / admin PIN |
| pantalla, tema, sonido | display, seasonal theme, sound |

## Layout

| Path | What it is |
|---|---|
| `src/` | firmware (PlatformIO, Arduino 2.0 + LVGL 9.2) |
| `include/lv_conf.h` | LVGL config for the board |
| `temas/*.json` | seasonal themes, compiled in by `herramientas/temas.py` → `src/temas_incluidos.h` |
| `herramientas/` | generators: fonts (`fuentes.sh`), logo (`dibujar_logo.py`, `logo.py`), themes |
| `sim/` | Windows simulator (fake Arduino/WiFi/sensor/flash), fake server, end-to-end tests |
| `server/` | reference server, Python stdlib + SQLite (`Store` class = the only DB code) |
| `docs/API.md` | **the contract between clock and server**; change it together with both sides |
| `hardware/` | parts, wiring, 3D case (STL/STEP/3MF + SolidCraft sources) |

### Firmware files

| File | Does |
|---|---|
| `config.h` | pins, initial PIN, time zone, accent color, empty default server URL |
| `pantalla.cpp` | ST77922 QSPI driver, touch, backlight, LVGL bridge (framebuffer in PSRAM, software rotation) |
| `huella.cpp` | sensor task (core 0): match, enroll, delete; background queue for template up/download (UpChar/DownChar) |
| `datos.cpp` | settings in NVS (PIN, Wi-Fi, server URL and key, office), valid-time flag |
| `panel.cpp` | server sync: people cache (`/perfiles.json`), today's punches (`/hoy.json`), queue (`/cola.txt`), enroll/delete, photos, templates (`/plantillas.txt`) |
| `http.cpp` | HTTPS requests in their own task (core 0), certificate checked with the ESP-IDF bundle |
| `fotos.cpp` | profile JPEG → round image with TJPGD in PSRAM; silhouette when missing |
| `ui.cpp` | every LVGL screen; screen changes deferred through `ir()` |
| `red.cpp`, `web_panel.h` | Wi-Fi, NTP, mDNS `reloj.local`, the device web page and OTA |
| `sonido.cpp` | tones and theme melodies over I2S to the ES8311, RGB LED |
| `tema.cpp` | seasonal themes: dates, rotating greetings, surprises, decorations, melodies |

## Build and test

```sh
pio run                                   # firmware; must end in SUCCESS
cmake -G Ninja -S sim -B .sim/build && ninja -C .sim/build   # simulator (Windows + MinGW)
python -I sim/probar_panel.py             # end-to-end vs sim/servidor_falso.py -> "RESULTADO: todo bien"
python -I sim/probar_dos_oficinas.py      # two clocks sharing templates -> "RESULTADO: todo bien"
python -I sim/probar_panel.py http://127.0.0.1:8078 "$CLOCK_TOKEN"   # vs server/server.py (fresh DB + `demo`)
```

`RELOJ_SIM_RAIZ` moves the simulator's data and screenshots (default `.sim/`). The simulator's
`LIBDEPS` (CMake cache) must point to the LVGL that PlatformIO downloaded (default
`.pio/libdeps/reloj`). CI runs `pio run` and a server smoke test on every push.

**Done means:** `pio run` succeeds, both simulator suites print `RESULTADO: todo bien` when you
touched `src/` or `sim/`, `docs/API.md` matches the code on both sides, and no secret was added.

## Rules that keep it working

- LVGL only from the loop task (core 1). Never delete a screen inside its own event: use `ir()`.
- Network only in the `http.cpp` task. `panel.cpp` leaves one request and collects it in the loop.
- Writing to flash stalls both cores: do not write while an animation runs. New punches are saved to
  the queue right away; `hoy.json` and queue cleanup are deferred 5 s (`guardarDespues`).
- HTTPS only on the board, with the certificate checked. Never `setInsecure()`. `http://` is allowed
  only under `#ifdef SIMULADOR`.
- The server URL and key live only in NVS, set from `http://reloj.local`. Never put real URLs,
  tokens, Wi-Fi passwords or people's names in code, tests or docs. Test people: Ana, Bruno, Diego,
  Eva, Carla.
- The clock never creates people. The server owns the list; the clock links finger numbers.
- Punch ids must stay idempotent. Queued punches are retried with the same id.
- Delete a template from the sensor only when the number belongs to no one, and never when the
  people list came back empty.
- Do not call `fp.begin()`: it reopens the UART on pins 17/18, which belong to I2S.
- Do not use IO45/IO46 for anything: they are strapping pins.
- Display: windows aligned to 4 px. In `tema.cpp` use `hora_ahora()`, not `time()` (the simulator
  breaks).
- Palette: `COLOR_ACENTO` + black, white and greys. States are shown by background, shape and icon.
- Fonts are Poppins (OFL) generated with `herramientas/fuentes.sh` (`lv_font_conv`, 4 bpp, Latin-1).
  `f_reloj` only has `0-9 : -`. Declare a new font in `lv_conf.h`.
- Animations use `lv_anim` (`animar`, `aparecer`, `rebotar`, `sacudir`, `onda` in `ui.cpp`). Opacity
  animations use layers: keep few at a time. `probar_panel.py` waits for `ANIMACION` before reading.

## Good first tasks

- Run it on a real board and report the checklist in README → Status.
- English UI strings (or a small string table to switch languages).
- A server-side `GET /api/reloj/tema` so themes can be changed without reflashing (`tema_cargar()` already reads `/tema.json`).
- Instant feedback when a finger touches the sensor (today the greeting appears after the search, about 0.5–1 s).
- A battery-backed RTC, so the clock keeps the time through a power cut with no Wi-Fi.
- More server backends: Postgres, Supabase, Google Sheets.
