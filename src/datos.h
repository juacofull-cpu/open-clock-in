#pragma once
#include <Arduino.h>

struct Ajustes {
  String pin;
  bool girada;
  bool sonido;
  uint8_t brilloReposo;
  String ssid;
  String clave;
  String panelUrl;    // servidor (docs/API.md), sin "/" al final
  String panelLlave;  // RELOJ_TOKEN del panel (solo en la NVS, nunca en el código)
  String oficina;     // "A" o "B": dónde está este reloj (va en cada marca)
};

bool oficina_valida(const String& o);  // las del calendario del panel: A y B

void datos_iniciar();  // monta la memoria flash (LittleFS) y lee los ajustes

// Hora
bool hora_valida();
time_t hora_ahora();  // = time(nullptr); úsala donde se incluya <mutex>/<ctime> (borran el reloj del simulador)
String fecha_hoy();                          // AAAA-MM-DD, hora de Chile
String hora_local(time_t t, const char* fmt);  // con strftime, hora de Chile

// Ajustes
Ajustes& ajustes();
void ajustes_guardar();
