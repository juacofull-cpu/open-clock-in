// Cliente HTTPS de la placa, en su propia tarea (núcleo 0).
// Valida el certificado del servidor con el paquete de raíces de ESP-IDF (el de Mozilla,
// que ya viene dentro de libmbedtls del framework): nunca setInsecure(), porque va la llave.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include "http.h"

extern const uint8_t raices_crt[] asm("_binary_x509_crt_bundle_start");

static QueueHandle_t qIda, qVuelta;

static void hacer(WiFiClientSecure& cli, PedidoHttp* p) {
  if (!p->url.startsWith("https://")) {  // la llave no viaja sin cifrar
    p->codigo = -2;
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    p->codigo = -1;
    return;
  }
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  if (!http.begin(cli, p->url)) {
    p->codigo = -1;
    return;
  }
  http.addHeader("Authorization", "Bearer " + p->llave);
  if (p->cuerpo.length()) http.addHeader("Content-Type", "application/json");
  p->codigo = http.sendRequest(p->metodo.c_str(), p->cuerpo);
  if (p->codigo == 200 && p->archivo.length()) {
    File f = LittleFS.open(p->archivo, "w");
    if (!f || http.writeToStream(&f) <= 0) p->codigo = -3;
    if (f) f.close();
  } else if (p->codigo > 0) {
    p->respuesta = http.getString();
  }
  http.end();
}

static void tarea(void*) {
  WiFiClientSecure cli;
  cli.setCACertBundle(raices_crt);
  cli.setHandshakeTimeout(15);
  for (;;) {
    PedidoHttp* p = nullptr;
    if (xQueueReceive(qIda, &p, portMAX_DELAY) != pdTRUE || !p) continue;
    hacer(cli, p);
    if (p->codigo <= 0) Serial.printf("[http] %s %s: sin respuesta (%d)\n", p->metodo.c_str(), p->url.c_str(), p->codigo);
    xQueueSend(qVuelta, &p, portMAX_DELAY);
  }
}

void http_iniciar() {
  qIda = xQueueCreate(1, sizeof(PedidoHttp*));
  qVuelta = xQueueCreate(2, sizeof(PedidoHttp*));
  // La conexión segura necesita bastante pila
  xTaskCreatePinnedToCore(tarea, "http", 10240, NULL, 1, NULL, 0);
}

bool http_enviar(PedidoHttp* p) { return xQueueSend(qIda, &p, 0) == pdTRUE; }

PedidoHttp* http_recoger() {
  PedidoHttp* p = nullptr;
  return xQueueReceive(qVuelta, &p, 0) == pdTRUE ? p : nullptr;
}
