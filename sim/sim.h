// Piezas del simulador que se hablan entre sí (no las usa el código del reloj).
#pragma once
#include <stdint.h>
#include <string>
#include "Arduino.h"

// Carpeta de datos: .sim/ (o la que diga RELOJ_SIM_DATOS, para pruebas)
std::string sim_ruta(const std::string& rel);

extern bool sim_boot_apretado;  // botón BOOT de la placa
extern int sim_puerto_panel;
extern bool sim_internet;       // false = se cortó internet (sim_http.cpp)    // puerto en que quedó el panel web (80 u 8080...)

// Lector de huella simulado (sim_huella.cpp)
#define SIM_DEDOS 6
void sim_lector_tick();
void sim_lector_dedo(int dedo);  // 1..SIM_DEDOS: alguien apoya ese dedo y lo saca
void sim_lector_mal();           // dedo mal apoyado
void sim_lector_enchufar(bool on);
bool sim_lector_enchufado();
String sim_lector_estado();
int sim_lector_posicion(int dedo);  // dónde quedó guardado ese dedo en el lector, -1 si no está
String sim_lector_memoria();        // "posición:dedo,..." de lo que guarda el lector

// Pantalla, sonido y LED (sim_pantalla.cpp)
uint8_t sim_brillo();
bool sim_girada();
bool sim_sonido();
uint32_t sim_led();  // 0xRRGGBB
void sim_tocar(int x, int y);
void sim_arrastrar(int x1, int y1, int x2, int y2);
bool sim_captura(int ventana, const std::string& archivo);  // 0 = pantalla, 1 = panel
void sim_textos(std::string& salida);
void sim_panel_crear();
void sim_panel_tick();

// Control para pruebas automáticas por HTTP (sim_pruebas.cpp)
void sim_pruebas_iniciar();
void sim_pruebas_tick();
