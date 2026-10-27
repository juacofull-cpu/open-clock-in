// Pantalla ST77922 (QSPI) + táctil + unión con LVGL.
//
// El panel es vertical (320x480) y el ST77922 no puede intercambiar ejes, así que
// la imagen horizontal (480x320) se gira al enviarla. LVGL dibuja por trozos, cada
// trozo se copia a una imagen completa en PSRAM y de ahí se manda girado al panel.
// El panel exige ventanas alineadas a 4 píxeles; como la imagen completa está en
// memoria, se puede agrandar la ventana sin perder nada.
//
// Driver basado en el de NerdMiner para esta misma placa (es3c35pDisplayDriver.cpp);
// la tabla de arranque es la del firmware de fábrica.
#include <Arduino.h>
#include <Wire.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include "config.h"
#include "pantalla.h"

#define NAT_W 320  // panel físico (vertical)
#define NAT_H 480
#define LCD_HZ 40000000
#define TX_BYTES 30720  // 48 filas físicas por envío

// {comando, cantidad de datos, datos...}
static const uint8_t LCD_INIT[] = {
    0xF1, 1, 0x00,
    0x60, 3, 0x00, 0x00, 0x00,
    0x65, 1, 0x80,
    0x79, 1, 0x06,
    0x7B, 3, 0x00, 0x08, 0x08,
    0x80, 11, 0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA,
    0x81, 4, 0x26, 0x52, 0x72, 0x27,
    0x84, 2, 0x92, 0x25,
    0x87, 6, 0x10, 0x10, 0x58, 0x00, 0x02, 0x3A,
    0x88, 15, 0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06,
    0x89, 3, 0x00, 0x00, 0x00,
    0x8A, 11, 0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19,
    0x8B, 9, 0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E,
    0x8C, 13, 0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09,
    0x8D, 1, 0x0C,
    0x8E, 6, 0x33, 0x01, 0x0C, 0x13, 0x01, 0x01,
    0xB3, 2, 0x00, 0x30,
    0xF1, 1, 0x00,
    0x71, 1, 0xD0,
    0x66, 2, 0x02, 0x3F,
    0xBE, 3, 0x26, 0x00, 0x9D,
    0x70, 12, 0x01, 0xA6, 0x11, 0x40, 0xE0, 0x00, 0x11, 0x60, 0x11, 0x00, 0x00, 0x1A,
    0x90, 9, 0x04, 0x04, 0x55, 0x74, 0x00, 0x40, 0x43, 0x2D, 0x2D,
    0x91, 9, 0x04, 0x04, 0x55, 0x75, 0x00, 0x40, 0x42, 0x2D, 0x2D,
    0x92, 10, 0x04, 0x44, 0x55, 0xC0, 0x06, 0x00, 0x07, 0x05, 0x90, 0x2D,
    0x93, 10, 0x04, 0x43, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05, 0x90, 0x2D,
    0x94, 6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x95, 5, 0x96, 0x16, 0x00, 0x00, 0xFF,
    0x96, 12, 0x44, 0x53, 0x03, 0x12, 0x23, 0x24, 0x06, 0x05, 0x9A, 0x2D, 0x00, 0x44,
    0x97, 12, 0x44, 0x53, 0x47, 0x56, 0x20, 0x20, 0x02, 0x01, 0x9A, 0x2D, 0x00, 0x44,
    0xBA, 5, 0x55, 0x9A, 0x2D, 0x9A, 0x2D,
    0x9A, 7, 0x40, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
    0x9B, 7, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
    0x9C, 13, 0x5C, 0x12, 0x00, 0x00, 0x10, 0x12, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00,
    0x9D, 8, 0x8A, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01,
    0x9E, 7, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01,
    0xB4, 12, 0x1D, 0x1C, 0x1E, 0x0B, 0x14, 0x02, 0x13, 0x09, 0x1E, 0x00, 0x1E, 0x10,
    0xB5, 12, 0x1D, 0x1C, 0x1E, 0x0A, 0x15, 0x03, 0x11, 0x08, 0x1E, 0x01, 0x1E, 0x12,
    0xB6, 7, 0x77, 0x77, 0x00, 0x0A, 0xFF, 0x0A, 0xFF,
    0x86, 14, 0xC6, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x0C, 0x13, 0x01, 0xA5, 0x00, 0xA5, 0xA5,
    0xB7, 16, 0x07, 0x0A, 0x0E, 0x06, 0x05, 0x03, 0x2B, 0x03, 0x03, 0x42, 0x07, 0x10, 0x10, 0x2E, 0x3F, 0x0D,
    0xB8, 16, 0x07, 0x0A, 0x0D, 0x05, 0x05, 0x02, 0x2B, 0x02, 0x03, 0x42, 0x06, 0x10, 0x0F, 0x2E, 0x3F, 0x0D,
    0xB9, 2, 0x23, 0x23,
    0xBF, 6, 0x10, 0x14, 0x14, 0x0B, 0x0B, 0x0B,
    0xF2, 1, 0x00,
    0x73, 5, 0x04, 0xDA, 0x12, 0x54, 0x47,
    0x77, 5, 0x6B, 0x5B, 0xFD, 0xC3, 0xC5,
    0x7A, 2, 0x15, 0x27,
    0x7B, 2, 0x04, 0x57,
    0x7E, 2, 0x01, 0x0E,
    0xBF, 1, 0x36,
    0xE3, 2, 0x40, 0x40,
    0xF0, 1, 0x00,
    0xD0, 1, 0x00,
    0x2A, 4, 0x00, 0x00, 0x01, 0x3F,
    0x2B, 4, 0x00, 0x00, 0x01, 0xDF,
    0x21, 0,        // inversión encendida (este panel la necesita para colores reales)
    0x11, 0,        // salir de reposo, esperar 120 ms
    0x29, 0,        // pantalla encendida
    0x2C, 0,
    0x3A, 1, 0x01,  // RGB565
    0x36, 1, 0x00,
    0x35, 1, 0x01,  // señal TE, esperar 20 ms
};

static spi_device_handle_t lcd = nullptr;
static uint8_t* tx = nullptr;     // buffer DMA de envío
static uint16_t* fb = nullptr;    // imagen horizontal completa, ya en orden de bytes del panel
static uint8_t* lvBuf = nullptr;  // buffer de dibujo de LVGL
static bool girada = false;
static lv_display_t* disp = nullptr;

// Escritura de registro: código 0x02, el comando va en la dirección de 24 bits.
static void lcdCmd(uint8_t cmd, const uint8_t* data, size_t len) {
  spi_transaction_t t = {};
  t.cmd = 0x02;
  t.addr = (uint32_t)cmd << 8;
  if (len) {
    memcpy(tx, data, len);
    t.tx_buffer = tx;
    t.length = len * 8;
  }
  spi_device_polling_transmit(lcd, &t);
}

static bool panelIniciar() {
  spi_bus_config_t bus = {};
  bus.mosi_io_num = LCD_D0;
  bus.miso_io_num = LCD_D1;
  bus.quadwp_io_num = LCD_D2;
  bus.quadhd_io_num = LCD_D3;
  bus.sclk_io_num = LCD_SCK;
  bus.max_transfer_sz = TX_BYTES;
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  spi_device_interface_config_t dev = {};
  dev.command_bits = 8;
  dev.address_bits = 24;
  dev.mode = 0;
  dev.clock_speed_hz = LCD_HZ;
  dev.spics_io_num = LCD_CS;
  dev.flags = SPI_DEVICE_HALFDUPLEX;
  dev.queue_size = 1;
  if (spi_bus_add_device(SPI2_HOST, &dev, &lcd) != ESP_OK) return false;

  tx = (uint8_t*)heap_caps_malloc(TX_BYTES, MALLOC_CAP_DMA);
  if (!tx) return false;

  for (size_t i = 0; i < sizeof(LCD_INIT);) {
    uint8_t cmd = LCD_INIT[i], len = LCD_INIT[i + 1];
    lcdCmd(cmd, &LCD_INIT[i + 2], len);
    if (cmd == 0x11) delay(120);
    else if (cmd == 0x35) delay(20);
    i += 2 + len;
  }
  return true;
}

// Envía un rectángulo de la imagen horizontal al panel vertical.
// Sin girar: x horizontal = 479 - fila física, y horizontal = columna física.
static void panelEnviar(int x, int y, int w, int h) {
  if (!lcd || !fb) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > PANT_W) w = PANT_W - x;
  if (y + h > PANT_H) h = PANT_H - y;
  if (w <= 0 || h <= 0) return;

  int px0, px1, py0, py1;  // rectángulo físico, fin exclusivo
  if (!girada) {
    px0 = y;
    px1 = y + h;
    py0 = NAT_H - (x + w);
    py1 = NAT_H - x;
  } else {
    px0 = NAT_W - (y + h);
    px1 = NAT_W - y;
    py0 = x;
    py1 = x + w;
  }
  px0 &= ~3;
  py0 &= ~3;
  px1 = (px1 + 3) & ~3;
  py1 = (py1 + 3) & ~3;
  const int nw = px1 - px0, nh = py1 - py0;

  spi_device_acquire_bus(lcd, portMAX_DELAY);
  const uint8_t ca[4] = {(uint8_t)(px0 >> 8), (uint8_t)px0, (uint8_t)((px1 - 1) >> 8), (uint8_t)(px1 - 1)};
  const uint8_t ra[4] = {(uint8_t)(py0 >> 8), (uint8_t)py0, (uint8_t)((py1 - 1) >> 8), (uint8_t)(py1 - 1)};
  lcdCmd(0x2A, ca, 4);
  lcdCmd(0x2B, ra, 4);

  uint16_t* dst = (uint16_t*)tx;
  const int filasPorEnvio = (TX_BYTES / 2) / nw;
  for (int r = 0; r < nh; r += filasPorEnvio) {
    const int n = (nh - r < filasPorEnvio) ? nh - r : filasPorEnvio;
    for (int i = 0; i < nw; i++) {
      const int px = px0 + i;
      if (!girada) {
        const uint16_t* s = fb + px * PANT_W + (PANT_W - 1 - (py0 + r));
        for (int j = 0; j < n; j++) dst[j * nw + i] = s[-j];
      } else {
        const uint16_t* s = fb + (PANT_H - 1 - px) * PANT_W + (py0 + r);
        for (int j = 0; j < n; j++) dst[j * nw + i] = s[j];
      }
    }
    // Píxeles: código 0x32 y RAMWR en la dirección, datos por 4 líneas. CS queda
    // activo entre los envíos de una misma ventana.
    spi_transaction_ext_t t = {};
    t.base.flags = SPI_TRANS_MODE_QIO;
    if (r == 0) {
      t.base.cmd = 0x32;
      t.base.addr = 0x002C00;
    } else {
      t.base.flags |= SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    }
    if (r + n < nh) t.base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    t.base.tx_buffer = tx;
    t.base.length = (size_t)n * nw * 16;
    spi_device_polling_transmit(lcd, &t.base);
  }
  spi_device_release_bus(lcd);
}

// Medición para la placa: cuánto del tiempo se va en mandar a la pantalla (monitor serie cada 30 s)
static uint32_t medEnvios = 0, medUs = 0, medMaxUs = 0, medDesde = 0;

static void flushCb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const uint32_t t0 = micros();
  const int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  const uint16_t* s = (const uint16_t*)px;
  for (int y = a->y1; y <= a->y2; y++) {
    uint16_t* fila = fb + y * PANT_W + a->x1;
    for (int x = 0; x < w; x++) fila[x] = __builtin_bswap16(*s++);
  }
  panelEnviar(a->x1, a->y1, w, h);
  const uint32_t us = micros() - t0;
  medEnvios++;
  medUs += us;
  if (us > medMaxUs) medMaxUs = us;
  lv_display_flush_ready(d);
}

// ------------------------------------------------------------------ táctil --
// Registros de 16 bits, byte alto primero.
#define TP_ESTADO 0x0001
#define TP_MAX_PUNTOS 0x0009
#define TP_REPORTE 0x0010  // 4 bytes de cabecera y 7 bytes por punto
#define TP_BYTES_PUNTO 7
#define TP_PUNTOS_MAX 10

static bool tactilOk = false;
static uint8_t tactilPuntos = TP_PUNTOS_MAX;

static bool tpLeer(uint16_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static void tactilIniciar() {
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, HIGH);
  delay(5);
  digitalWrite(TOUCH_RST, LOW);
  delay(5);
  digitalWrite(TOUCH_RST, HIGH);
  delay(40);
  Wire.begin(I2C_SDA, I2C_SCL, 400000);

  uint8_t estado = 0xFF;
  for (int i = 0; i < 20; i++) {  // nibble bajo 1 = todavía arrancando
    if (tpLeer(TP_ESTADO, &estado, 1) && (estado & 0x0F) != 1) {
      tactilOk = true;
      break;
    }
    delay(10);
  }
  uint8_t n = 0;
  if (tactilOk && tpLeer(TP_MAX_PUNTOS, &n, 1) && n >= 1 && n <= TP_PUNTOS_MAX) tactilPuntos = n;
  Serial.printf("[tactil] %s, %u puntos\n", tactilOk ? "listo" : "no responde", tactilPuntos);
}

static void tactilLeerCb(lv_indev_t*, lv_indev_data_t* data) {
  static int ux = 0, uy = 0;
  data->point.x = ux;
  data->point.y = uy;
  data->state = LV_INDEV_STATE_RELEASED;
  if (!tactilOk) return;
  // Se lee el reporte completo: el controlador retiene el cuadro hasta que se lee entero.
  uint8_t d[4 + TP_PUNTOS_MAX * TP_BYTES_PUNTO];
  if (!tpLeer(TP_REPORTE, d, 4 + tactilPuntos * TP_BYTES_PUNTO)) return;
  const uint8_t* p = d + 4;
  if (!(p[0] & 0x80)) return;
  const int rx = ((p[0] & 0x3F) << 8) | p[1];
  const int ry = ((p[2] & 0x3F) << 8) | p[3];
  if (!girada) {
    ux = NAT_H - 1 - ry;
    uy = rx;
  } else {
    ux = ry;
    uy = NAT_W - 1 - rx;
  }
  ux = constrain(ux, 0, PANT_W - 1);
  uy = constrain(uy, 0, PANT_H - 1);
  data->point.x = ux;
  data->point.y = uy;
  data->state = LV_INDEV_STATE_PRESSED;
}

// ------------------------------------------------------------- brillo --
static uint8_t brilloActual = 255;

void pantalla_brillo(uint8_t pct) {
  if (pct > 100) pct = 100;
  const uint8_t v = (uint8_t)(pct * 255 / 100);
  if (v == brilloActual) return;
  brilloActual = v;
  ledcWrite(0, v);
}

void pantalla_tarea(uint8_t brilloReposo) {
  pantalla_brillo(lv_display_get_inactive_time(NULL) > 60000 ? brilloReposo : 100);
  const uint32_t ahora = millis();
  if (ahora - medDesde >= 30000) {
    // «ocupada» alta (> 50 %) o envíos de más de 40 ms = animaciones que se arrastran
    if (medEnvios)
      Serial.printf("[pantalla] %lu envios en 30 s, promedio %lu ms, el mas largo %lu ms, ocupada %lu %%\n",
                    (unsigned long)medEnvios, (unsigned long)(medUs / medEnvios / 1000),
                    (unsigned long)(medMaxUs / 1000), (unsigned long)(medUs / ((ahora - medDesde) * 10)));
    medEnvios = medUs = medMaxUs = 0;
    medDesde = ahora;
  }
}

bool pantalla_tactil_ok() { return tactilOk; }

static uint32_t ticks() { return millis(); }

void pantalla_iniciar(bool g) {
  girada = g;
  ledcSetup(0, 5000, 8);
  ledcAttachPin(LCD_BL, 0);
  ledcWrite(0, 0);

  fb = (uint16_t*)heap_caps_calloc(PANT_W * PANT_H, 2, MALLOC_CAP_SPIRAM);
  if (!panelIniciar() || !fb) Serial.println("[pantalla] error al iniciar");
  if (fb) panelEnviar(0, 0, PANT_W, PANT_H);  // todo negro antes de encender la luz
  tactilIniciar();

  lv_init();
  lv_tick_set_cb(ticks);
  disp = lv_display_create(PANT_W, PANT_H);
  const size_t bytes = PANT_W * 40 * 2;
  lvBuf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_display_set_buffers(disp, lvBuf, NULL, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, tactilLeerCb);

  brilloActual = 0;
  pantalla_brillo(100);
}

void pantalla_girar(bool g) {
  if (g == girada) return;
  girada = g;
  panelEnviar(0, 0, PANT_W, PANT_H);
}
