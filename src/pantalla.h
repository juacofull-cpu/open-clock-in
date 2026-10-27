#pragma once
#include <stdint.h>

#define PANT_W 480
#define PANT_H 320

void pantalla_iniciar(bool girada);
void pantalla_girar(bool girada);     // 180°, para cuando la placa queda al revés en la caja
void pantalla_brillo(uint8_t pct);    // 0..100
void pantalla_tarea(uint8_t brilloReposo);  // baja el brillo tras 60 s sin uso
bool pantalla_tactil_ok();
