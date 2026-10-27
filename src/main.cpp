// Reloj control con huella para la placa ES3C35P (ESP32-S3, pantalla táctil 3,5").
//
// Núcleo 1 (loop): pantalla, interfaz, WiFi, panel web y lo que se manda al servidor.
// Núcleo 0: pedidos HTTPS al panel (http.cpp), lector de huella (huella.cpp) y sonido.
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "datos.h"
#include "huella.h"
#include "panel.h"
#include "pantalla.h"
#include "red.h"
#include "sonido.h"
#include "tema.h"
#include "ui.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
  Serial.begin(115200);
  pinMode(AMP_EN, OUTPUT);
  digitalWrite(AMP_EN, HIGH);  // parlante apagado hasta que haya algo que sonar
  setenv("TZ", ZONA_HORARIA, 1);
  tzset();

  datos_iniciar();
  tema_cargar();
  pantalla_iniciar(ajustes().girada);
  sonido_iniciar();
  huella_iniciar();
  red_iniciar();
  panel_iniciar();
  ui_iniciar();
  Serial.printf("[reloj] version %s lista, %u perfiles, %u marcas por enviar\n", VERSION, (unsigned)panel_perfiles().size(),
                (unsigned)panel_en_cola());
}

void loop() {
  lv_timer_handler();
  ui_tarea();
  red_tarea();
  panel_tarea();
  pantalla_tarea(ajustes().brilloReposo);
  delay(5);
}
