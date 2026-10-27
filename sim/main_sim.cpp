// Reloj control en el PC: el mismo arranque y la misma vuelta que src/main.cpp.
// La interfaz (ui.cpp), los datos (datos.cpp), el panel web (red.cpp) y la conexión con
// el servidor (panel.cpp, fotos.cpp) son los de la placa sin cambios; lo de abajo
// (pantalla, lector, sonido, WiFi, archivos, HTTP) es imitado.
#include <lvgl.h>
#include "config.h"
#include "datos.h"
#include "huella.h"
#include "panel.h"
#include "pantalla.h"
#include "red.h"
#include "sim.h"
#include "sonido.h"
#include "tema.h"
#include "ui.h"

int main() {
  Serial.begin(115200);
  datos_iniciar();
  tema_cargar();
  pantalla_iniciar(ajustes().girada);  // abre las dos ventanas
  lv_lock();
  sonido_iniciar();
  huella_iniciar();
  red_iniciar();
  panel_iniciar();
  ui_iniciar();
  sim_pruebas_iniciar();
  Serial.printf("[reloj] version %s lista, %u perfiles, %u marcas por enviar\n", VERSION, (unsigned)panel_perfiles().size(),
                (unsigned)panel_en_cola());
  lv_unlock();

  for (;;) {
    lv_lock();
    lv_timer_handler();
    ui_tarea();
    red_tarea();
    panel_tarea();
    pantalla_tarea(ajustes().brilloReposo);
    sim_lector_tick();
    sim_panel_tick();
    sim_pruebas_tick();
    lv_unlock();
    delay(5);
  }
}
