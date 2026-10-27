// Ajustes del reloj en la NVS (Preferences): clave, WiFi, pantalla y conexión al panel.
// Las personas y las marcas viven en el servidor (ver panel.cpp).
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"
#include "datos.h"

static Ajustes aj;
static Preferences prefs;

bool hora_valida() { return time(nullptr) > 1735689600; }  // después del 1-1-2025
time_t hora_ahora() { return time(nullptr); }

String hora_local(time_t t, const char* fmt) {
  struct tm tm;
  localtime_r(&t, &tm);
  char b[40];
  strftime(b, sizeof(b), fmt, &tm);
  return String(b);
}

String fecha_hoy() { return hora_local(time(nullptr), "%Y-%m-%d"); }

Ajustes& ajustes() { return aj; }

void ajustes_guardar() {
  prefs.putString("pin", aj.pin);
  prefs.putBool("girada", aj.girada);
  prefs.putBool("sonido", aj.sonido);
  prefs.putUChar("brillo", aj.brilloReposo);
  prefs.putString("ssid", aj.ssid);
  prefs.putString("clave", aj.clave);
  prefs.putString("purl", aj.panelUrl);
  prefs.putString("pllave", aj.panelLlave);
  prefs.putString("oficina", aj.oficina);
}

bool oficina_valida(const String& o) { return o == "A" || o == "B"; }

void datos_iniciar() {
  if (!LittleFS.begin(true)) Serial.println("[datos] no se pudo montar LittleFS");
  prefs.begin("reloj", false);
  aj.pin = prefs.getString("pin", PIN_INICIAL);
  aj.girada = prefs.getBool("girada", false);
  aj.sonido = prefs.getBool("sonido", true);
  aj.brilloReposo = prefs.getUChar("brillo", 35);
  aj.ssid = prefs.getString("ssid", "");
  aj.clave = prefs.getString("clave", "");
  aj.panelUrl = prefs.getString("purl", PANEL_URL_INICIAL);
  aj.panelLlave = prefs.getString("pllave", "");
  aj.oficina = prefs.getString("oficina", "A");
  if (!oficina_valida(aj.oficina)) aj.oficina = "A";
}
