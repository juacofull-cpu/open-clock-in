// Prueba del lector de huella (AS608 / ZFM-20) cuando el reloj dice «lector desconectado».
//
// Se graba EN VEZ del reloj (pio run -t upload en esta carpeta) y se lee por el USB a 115200
// (pio device monitor). Después se vuelve a grabar el reloj. Cada ronda (~20 s) muestra:
//   [NIVEL]   si cada línea de datos del conector UART tiene algo que la empuja (un TX en reposo
//             queda ALTO fijo; un RX o un cable suelto quedan «suelta»)
//   [PUENTE]  si RXD y TXD están unidos entre sí (prueba sin lector: junta los dos cables)
//   [ECO]     el UART de la placa recibiéndose a sí mismo por dentro: si esto falla, es la prueba
//   RX=… TX=… la orden VfyPwd del AS608 en los dos sentidos de cable, en IO43/44 y en el conector
//             extendido (IO45/46), a 9600–115200 baudios, con los bytes crudos que vuelven.
//             «RESPONDE» = encontró al lector: el reloj usa RX=IO44 TX=IO43 a 57600.
// Lo aprendido (2026-11-03): los colores de los cables no son los del manual. En ese lector el
// verde era el aviso de toque (queda ALTO fijo y parece un TX) y el TX era el amarillo.
#include <Arduino.h>
#include "driver/uart.h"

HardwareSerial uart(1);
// VfyPwd con la clave de fábrica (0): un AS608 contesta EF 01 … aunque su clave sea otra
const uint8_t VFY[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x07, 0x13, 0, 0, 0, 0, 0x00, 0x1B};
const uint32_t BAUDS[] = {57600, 9600, 19200, 38400, 115200};
// {RX de la placa, TX de la placa}: conector UART en los dos sentidos y el extendido
const int PARES[][2] = {{44, 43}, {43, 44}, {46, 45}, {45, 46}};

void probar(int rx, int tx, uint32_t b, bool eco = false) {
  uart.end();
  delay(20);
  uart.begin(b, SERIAL_8N1, rx, tx);
  uart_set_loop_back(UART_NUM_1, eco);
  delay(60);
  while (uart.available()) uart.read();
  uart.write(VFY, sizeof(VFY));
  uart.flush();
  uint8_t r[32];
  int n = 0;
  const uint32_t t = millis();
  while (millis() - t < 400 && n < 32)
    if (uart.available()) r[n++] = uart.read();
  if (eco) Serial.printf("[ECO] UART por dentro: %d de %u bytes %s", n, (unsigned)sizeof(VFY),
                         n == (int)sizeof(VFY) ? "(bien)" : "(MAL: la prueba no sirve)");
  else Serial.printf("RX=IO%d TX=IO%d %6lu baudios: %2d bytes", rx, tx, (unsigned long)b, n);
  if (!eco) {
    for (int i = 0; i < n; i++) Serial.printf(" %02X", r[i]);
    if (n >= 10 && r[0] == 0xEF && r[1] == 0x01)
      Serial.printf("  <-- RESPONDE (código %02X%s)", r[9],
                    r[9] == 0 ? ", clave OK" : r[9] == 0x13 ? ", otra clave" : "");
  }
  Serial.println();
}

int lee(int pin, int modo) {
  pinMode(pin, modo);
  delay(5);
  return digitalRead(pin);
}

void niveles() {
  uart.end();
  delay(20);
  // Una línea suelta sigue a la resistencia interna (abajo = 0, arriba = 1); una que algo empuja queda fija
  const int r0 = lee(44, INPUT_PULLDOWN), r1 = lee(44, INPUT_PULLUP);
  const int t0 = lee(43, INPUT_PULLDOWN), t1 = lee(43, INPUT_PULLUP);
  Serial.printf("[NIVEL] RXD (IO44): %s | TXD (IO43): %s\n",
                r0 == r1 ? (r0 ? "ALTO fijo (algo la empuja: un TX en reposo, o el aviso de toque)" : "BAJO fijo")
                         : "suelta",
                t0 == t1 ? (t0 ? "ALTO fijo" : "BAJO fijo") : "suelta (un RX también se ve así)");
  // Puente: mueve IO43 y mira si IO44 lo sigue
  pinMode(43, OUTPUT);
  digitalWrite(43, HIGH);
  const int a = lee(44, INPUT_PULLDOWN);
  digitalWrite(43, LOW);
  const int b = lee(44, INPUT_PULLUP);
  pinMode(43, INPUT);
  Serial.printf("[PUENTE] TXD -> RXD: %s\n", (a == 1 && b == 0) ? "unidos" : "no se tocan");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
}

void loop() {
  Serial.println("=== ronda ===");
  niveles();
  probar(44, 43, 57600, true);
  for (auto& p : PARES)
    for (uint32_t b : BAUDS) probar(p[0], p[1], b);
  delay(2000);
}
