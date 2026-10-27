// Control remoto para probar el simulador sin manos: solo se abre si existe la
// variable RELOJ_SIM_PRUEBAS con un puerto (por ejemplo 8099). Escucha en 127.0.0.1.
//   /tocar?x=&y=   /arrastrar?x=&y=&x2=&y2=   /dedo?n=   /mal   /enchufar?on=0|1   /boot?on=0|1
//   /internet?on=0|1   /captura?v=0|1&archivo=ruta.bmp   /textos   /estado   /perfiles   /salir
#include <windows.h>
#include <memory>
#include "WebServer.h"
#include "datos.h"
#include "huella.h"
#include "panel.h"
#include "sim.h"
#include "tema.h"

static std::unique_ptr<WebServer> srv;

static void ok() { srv->send(200, "text/plain; charset=utf-8", "ok"); }

void sim_pruebas_iniciar() {
  const char* p = getenv("RELOJ_SIM_PRUEBAS");
  if (!p || !atoi(p)) return;
  srv.reset(new WebServer(atoi(p)));
  srv->on("/tocar", HTTP_ANY, [] {
    sim_tocar(srv->arg("x").toInt(), srv->arg("y").toInt());
    ok();
  });
  srv->on("/arrastrar", HTTP_ANY, [] {
    sim_arrastrar(srv->arg("x").toInt(), srv->arg("y").toInt(), srv->arg("x2").toInt(), srv->arg("y2").toInt());
    ok();
  });
  srv->on("/dedo", HTTP_ANY, [] {
    sim_lector_dedo(srv->arg("n").toInt());
    ok();
  });
  srv->on("/mal", HTTP_ANY, [] {
    sim_lector_mal();
    ok();
  });
  srv->on("/enchufar", HTTP_ANY, [] {
    sim_lector_enchufar(srv->arg("on") != "0");
    ok();
  });
  srv->on("/internet", HTTP_ANY, [] {
    sim_internet = srv->arg("on") != "0";
    ok();
  });
  srv->on("/boot", HTTP_ANY, [] {
    sim_boot_apretado = srv->arg("on") != "0";
    ok();
  });
  srv->on("/captura", HTTP_ANY, [] {
    const bool hecho = sim_captura(srv->arg("v").toInt(), srv->arg("archivo").std());
    srv->send(hecho ? 200 : 500, "text/plain", hecho ? "ok" : "error");
  });
  srv->on("/textos", HTTP_ANY, [] {
    std::string s;
    sim_textos(s);
    srv->send(200, "text/plain; charset=utf-8", String(s));
  });
  srv->on("/estado", HTTP_ANY, [] {
    char b[1024];
    snprintf(b, sizeof(b),
             "lector=%s\nbrillo=%u\nled=%06X\nhora_valida=%d\nhora=%s\nperfiles=%u\ncola=%u\nconectado=%d\n"
             "panel=%s\ninternet=%d\noficina=%s\nplantillas=%s\nmemoria=%s\ntema=%s\n",
             sim_lector_estado().c_str(), sim_brillo(), (unsigned)sim_led(), hora_valida() ? 1 : 0,
             hora_valida() ? hora_local(time(nullptr), "%Y-%m-%d %H:%M:%S").c_str() : "",
             (unsigned)panel_perfiles().size(), (unsigned)panel_en_cola(), panel_conectado() ? 1 : 0,
             panel_estado().c_str(), sim_internet ? 1 : 0, ajustes().oficina.c_str(),
             panel_plantillas_estado().c_str(), sim_lector_memoria().c_str(), tema_nombre().c_str());
    srv->send(200, "text/plain; charset=utf-8", b);
  });
  srv->on("/perfiles", HTTP_ANY, [] {  // pedir la lista al panel ahora (no esperar los 3 min)
    panel_pedir_perfiles();
    ok();
  });
  srv->on("/salir", HTTP_ANY, [] {
    ok();
    ExitProcess(0);
  });
  srv->begin();
}

void sim_pruebas_tick() {
  if (srv) srv->handleClient();
}
