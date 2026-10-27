// Pitidos por el parlante (códec ES8311 + I2S) y el LED RGB de la placa.
// El arranque del ES8311 sigue al driver oficial de Espressif (esp-bsp/components/es8311),
// con MCLK de 4,096 MHz y 16 kHz. Si el códec no responde, el reloj funciona igual, mudo.
#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include "config.h"
#include "sonido.h"
#include "tema.h"

#define FS 16000

enum Tono : uint8_t { T_OK, T_SALIDA, T_ERROR, T_AVISO, T_TECLA, T_ESPECIAL };  // mismo orden que TonoTema

static bool listo = false;
static bool activo = true;
static QueueHandle_t cola = nullptr;

static bool esW(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static uint8_t esR(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1) != 1) return 0;
  return Wire.read();
}

static bool es8311Iniciar() {
  if (!esW(0x00, 0x1F)) return false;  // reinicio
  delay(20);
  esW(0x00, 0x00);
  esW(0x00, 0x80);  // encendido
  // Reloj desde el pin MCLK, todos los relojes activos, SCLK sin invertir
  esW(0x01, 0x3F);
  esW(0x06, esR(0x06) & ~(1 << 5));
  // Divisores para MCLK 4,096 MHz y 16 kHz: pre_div 1, x1, adc/dac div 1, osr 0x10
  esW(0x02, esR(0x02) & 0x07);
  esW(0x03, 0x10);
  esW(0x04, 0x10);
  esW(0x05, 0x00);
  esW(0x06, (esR(0x06) & 0xE0) | (4 - 1));
  esW(0x07, esR(0x07) & 0xC0);
  esW(0x08, 0xFF);
  // Esclavo, formato I2S, 16 bits
  esW(0x00, esR(0x00) & 0xBF);
  esW(0x09, 3 << 2);
  esW(0x0A, 3 << 2);
  esW(0x0D, 0x01);  // parte analógica encendida
  esW(0x0E, 0x02);
  esW(0x12, 0x00);  // DAC encendido
  esW(0x13, 0x10);  // salida hacia el amplificador
  esW(0x1C, 0x6A);
  esW(0x37, 0x08);
  esW(0x32, 0xBF);  // volumen ~75 %
  esW(0x31, 0x00);  // sin silencio
  return true;
}

static bool i2sIniciar() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = FS;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  cfg.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) return false;
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_MCLK;
  pins.bck_io_num = I2S_BCLK;
  pins.ws_io_num = I2S_WS;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  return i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK;
}

// Onda cuadrada suavizada (suena más fuerte que una senoidal en un parlante chico)
static void tocar(int frec, int ms, int amp) {
  static int16_t buf[256 * 2];
  const int total = FS * ms / 1000;
  const int rampa = FS * 4 / 1000;
  const int periodo = frec > 0 ? FS / frec : 0;
  int i = 0;
  while (i < total) {
    int n = 0;
    for (; n < 256 && i < total; n++, i++) {
      int v = 0;
      if (periodo > 0) {
        int env = amp;
        if (i < rampa) env = amp * i / rampa;
        else if (total - i < rampa) env = amp * (total - i) / rampa;
        v = ((i % periodo) < periodo / 2) ? env : -env;
      }
      buf[2 * n] = buf[2 * n + 1] = (int16_t)v;
    }
    size_t escrito;
    i2s_write(I2S_NUM_0, buf, n * 4, &escrito, portMAX_DELAY);
  }
}

static void tarea(void*) {
  uint8_t t;
  for (;;) {
    if (xQueueReceive(cola, &t, portMAX_DELAY) != pdTRUE) continue;
    digitalWrite(AMP_EN, LOW);
    delay(5);
    std::vector<Nota> melodia;
    if (tema_melodia((TonoTema)t, melodia)) {  // el tema de temporada trae su propia melodía
      const int amp = t == T_TECLA ? 4000 : 8000;
      for (const Nota& n : melodia) tocar(n.frec, n.ms, n.frec ? amp : 0);
    } else switch (t) {
      case T_OK:
      case T_SALIDA:
      case T_ESPECIAL: tocar(1319, 90, 9000); tocar(1760, 160, 9000); break;
      case T_ERROR: tocar(330, 180, 10000); tocar(0, 60, 0); tocar(330, 260, 10000); break;
      case T_AVISO: tocar(988, 120, 8000); tocar(0, 60, 0); tocar(988, 120, 8000); break;
      case T_TECLA: tocar(2093, 18, 4000); break;
    }
    tocar(0, 40, 0);
    i2s_zero_dma_buffer(I2S_NUM_0);
    if (uxQueueMessagesWaiting(cola) == 0) digitalWrite(AMP_EN, HIGH);  // apagado: sin soplido
  }
}

static void pedir(Tono t) {
  if (!listo || !activo) return;
  uint8_t v = t;
  xQueueSend(cola, &v, 0);
}

void sonido_iniciar() {
  pinMode(AMP_EN, OUTPUT);
  digitalWrite(AMP_EN, HIGH);
  // Wire ya quedó iniciado por el táctil (mismo bus)
  if (!i2sIniciar()) {
    Serial.println("[sonido] I2S no inició");
    return;
  }
  if (!es8311Iniciar()) {
    Serial.println("[sonido] el códec ES8311 no responde");
    return;
  }
  cola = xQueueCreate(4, 1);
  xTaskCreatePinnedToCore(tarea, "sonido", 4096, NULL, 3, NULL, 0);
  listo = true;
}

void sonido_activar(bool on) { activo = on; }
void sonido_ok() { pedir(T_OK); }
void sonido_salida() { pedir(T_SALIDA); }
void sonido_especial() { pedir(T_ESPECIAL); }
void sonido_error() { pedir(T_ERROR); }
void sonido_aviso() { pedir(T_AVISO); }
void sonido_tecla() { pedir(T_TECLA); }

void led(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(LED_RGB, r, g, b); }
