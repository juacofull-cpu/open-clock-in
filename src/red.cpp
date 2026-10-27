// WiFi, hora por internet y panel web chico (http://reloj.local o la IP de la pantalla):
// estado, conexión con el servidor (dirección + llave) y actualizar sin cable.
// El panel pide usuario "admin" y la misma clave de la pantalla.
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "config.h"
#include "datos.h"
#include "huella.h"
#include "panel.h"
#include "red.h"
#include "web_panel.h"

static WebServer server(80);
static bool servidorListo = false;
static bool ntpPedido = false;
static uint32_t ultimoIntento = 0;
static uint32_t desdeSinRed = 0;  // desde cuándo no hay conexión (0 = conectada o recién pedida)
static bool otaOk = false, otaAutorizado = false;

static bool autorizado() {
  if (server.authenticate("admin", ajustes().pin.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "Reloj control", "Clave incorrecta");
  return false;
}

static String jsonEsc(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if ((uint8_t)c < 0x20) continue;
    o += c;
  }
  return o;
}

static void rutas() {
  server.on("/", HTTP_GET, [] {
    if (!autorizado()) return;
    server.send_P(200, "text/html; charset=utf-8", PANEL_HTML);
  });

  // La llave nunca sale del reloj: solo se dice si hay una guardada
  server.on("/api/estado", HTTP_GET, [] {
    if (!autorizado()) return;
    String j = "{";
    j += "\"version\":\"" VERSION "\",";
    j += "\"wifi\":\"" + jsonEsc(red_estado()) + "\",";
    j += "\"senal\":" + String(WiFi.RSSI()) + ",";
    j += "\"panelUrl\":\"" + jsonEsc(ajustes().panelUrl) + "\",";
    j += "\"llave\":" + String(ajustes().panelLlave.length() ? "true" : "false") + ",";
    j += "\"oficina\":\"" + jsonEsc(ajustes().oficina) + "\",";
    j += "\"conectado\":" + String(panel_conectado() ? "true" : "false") + ",";
    j += "\"panel\":\"" + jsonEsc(panel_estado()) + "\",";
    j += "\"cola\":" + String((unsigned)panel_en_cola()) + ",";
    j += "\"lector\":" + String(huella_conectado() ? "true" : "false") + ",";
    j += "\"hora\":\"" + (hora_valida() ? hora_local(time(nullptr), "%Y-%m-%d %H:%M") : String()) + "\"";
    j += "}";
    server.send(200, "application/json", j);
  });

  // Dirección del panel, llave y oficina (si la llave viene vacía se deja la que había)
  server.on("/api/panel", HTTP_POST, [] {
    if (!autorizado()) return;
    const String error = panel_configurar(server.arg("url"), server.arg("llave"), server.arg("oficina"));
    if (error.length()) return server.send(400, "text/plain; charset=utf-8", error);
    server.send(200, "text/plain", "ok");
  });

  // Actualizar el programa sin cable: se sube el archivo firmware.bin
  server.on(
      "/actualizar", HTTP_POST,
      [] {
        if (!otaAutorizado) {
          server.requestAuthentication(BASIC_AUTH, "Reloj control", "Clave incorrecta");
          return;
        }
        server.send(200, "text/plain", otaOk ? "ok" : "error");
        if (otaOk) {
          panel_guardar_ya();
          delay(500);
          ESP.restart();
        }
      },
      [] {
        HTTPUpload& up = server.upload();
        if (up.status == UPLOAD_FILE_START) {
          otaAutorizado = server.authenticate("admin", ajustes().pin.c_str());
          otaOk = otaAutorizado && Update.begin(UPDATE_SIZE_UNKNOWN);
        } else if (up.status == UPLOAD_FILE_WRITE) {
          if (otaOk && Update.write(up.buf, up.currentSize) != up.currentSize) otaOk = false;
        } else if (up.status == UPLOAD_FILE_END) {
          if (otaOk) otaOk = Update.end(true);
        } else if (up.status == UPLOAD_FILE_ABORTED) {
          otaOk = false;
          Update.abort();
        }
      });

  server.onNotFound([] { server.send(404, "text/plain", "No existe"); });
}

void red_iniciar() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("reloj-control");
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  rutas();
  if (ajustes().ssid.length()) WiFi.begin(ajustes().ssid.c_str(), ajustes().clave.c_str());
  ultimoIntento = millis();
}

void red_conectar(const String& ssid, const String& clave) {
  ajustes().ssid = ssid;
  ajustes().clave = clave;
  ajustes_guardar();
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid.c_str(), clave.c_str());
  ultimoIntento = millis();
  desdeSinRed = 0;
}

void red_tarea() {
  const bool conectada = WiFi.status() == WL_CONNECTED;
  if (conectada) desdeSinRed = 0;
  else if (!desdeSinRed) desdeSinRed = millis() | 1;
  if (conectada) {
    if (!servidorListo) {
      server.begin();
      MDNS.begin("reloj");
      MDNS.addService("http", "tcp", 80);
      servidorListo = true;
      Serial.printf("[red] conectado, panel en http://%s\n", WiFi.localIP().toString().c_str());
    }
    if (!ntpPedido) {
      configTzTime(ZONA_HORARIA, "ntp.shoa.cl", "time.google.com", "pool.ntp.org");
      ntpPedido = true;
    }
    server.handleClient();
  } else if (ajustes().ssid.length() && millis() - ultimoIntento > 30000) {
    // Reintento por si el router estuvo apagado mucho rato
    ultimoIntento = millis();
    WiFi.disconnect();
    WiFi.begin(ajustes().ssid.c_str(), ajustes().clave.c_str());
  }
}

bool red_conectada() { return WiFi.status() == WL_CONNECTED; }
String red_ip() { return WiFi.localIP().toString(); }

String red_estado() {
  if (!ajustes().ssid.length()) return "Sin WiFi configurado";
  if (red_conectada()) return ajustes().ssid + " · " + red_ip();
  // Clave mala o router apagado: después de 20 s se avisa en vez de seguir "conectando"
  if (desdeSinRed && millis() - desdeSinRed > 20000) return "No conecta a " + ajustes().ssid + ": revisa la clave";
  return "Conectando a " + ajustes().ssid + "...";
}
