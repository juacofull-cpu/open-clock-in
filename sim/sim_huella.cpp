// Lector AS608 simulado. Reemplaza a src/huella.cpp con la misma interfaz y los mismos
// avisos, en el mismo orden. Cada "dedo" del panel es un dedo distinto de una persona.
// Lo que el lector guarda queda en .sim/lector.txt (como su memoria propia).
// Plantillas: la de un dedo simulado son 512 bytes que salen del número de dedo (siempre los
// mismos), así un dedo registrado en un simulador se reconoce en otro solo si este recibió la
// plantilla (DownChar + Store). Bytes que no son de ningún dedo quedan como dedo 0 (no
// coincide con nadie).
#include <deque>
#include <fstream>
#include <map>
#include "huella.h"
#include "sim.h"

static const uint16_t CAPACIDAD = 162;  // la que asume huella.cpp si el lector no la informa

enum class Modo { REPOSO, PASO1, SACAR, PASO2 };
static Modo modo = Modo::REPOSO;
static bool escaneando = false, enchufado = true, conectado = false;
static uint32_t desde = 0, conectarEn = 600;
static uint16_t posEnrolar = 0, capacidad = 0;  // como en la placa: 0 hasta la primera conexión
static int primerDedo = 0;
static std::map<uint16_t, int> memoria;  // posición en el lector -> dedo
static std::deque<HuellaEv> avisos;

static bool avisar(HEv ev, uint16_t id = 0, uint16_t conf = 0, uint8_t paso = 0, const char* msg = "",
                   uint8_t* datos = nullptr, uint16_t largo = 0) {
  HuellaEv a = {};
  a.ev = ev;
  a.id = id;
  a.confianza = conf;
  a.paso = paso;
  strncpy(a.msg, msg, sizeof(a.msg) - 1);
  a.datos = datos;
  a.largo = largo;
  if (avisos.size() >= 12) return false;  // la cola de la placa tiene 12 lugares
  avisos.push_back(a);
  return true;
}

// ------------------------------------------------------------ plantillas --
static const uint16_t LARGO_PLANTILLA = 512;  // el del AS608

static void plantillaDe(int dedo, uint8_t* b) {
  uint32_t x = 0x9E3779B9u * (uint32_t)(dedo + 1);
  for (uint16_t i = 0; i < LARGO_PLANTILLA; i++) {
    x = x * 1664525u + 1013904223u;
    b[i] = (uint8_t)(x >> 24);
  }
  b[0] = 'S', b[1] = 'I', b[2] = 'M', b[3] = (uint8_t)dedo;
}

static int dedoDe(const uint8_t* d, uint16_t n) {
  if (n != LARGO_PLANTILLA || d[0] != 'S' || d[1] != 'I' || d[2] != 'M') return 0;
  uint8_t b[LARGO_PLANTILLA];
  plantillaDe(d[3], b);
  return memcmp(b, d, n) ? 0 : d[3];
}

static void guardar() {
  std::ofstream f(sim_ruta("lector.txt"), std::ios::trunc);
  for (auto& kv : memoria) f << kv.first << "=" << kv.second << "\n";
}

static void cargar() {
  std::ifstream f(sim_ruta("lector.txt"));
  uint16_t pos;
  char igual;
  int dedo;
  while (f >> pos >> igual >> dedo) memoria[pos] = dedo;
}

static int buscar(int dedo) {
  for (auto& kv : memoria)
    if (kv.second == dedo) return kv.first;
  return -1;
}

// Cualquier orden nueva corta el registro en curso (en la placa, hayOrden())
static void cortarRegistro() { modo = Modo::REPOSO; }

void huella_iniciar() {
  cargar();
  conectarEn = millis() + 600;  // el lector tarda en arrancar
}

void huella_escanear(bool on) {
  cortarRegistro();
  escaneando = on;
}

void huella_enrolar(uint16_t id) {
  cortarRegistro();
  escaneando = false;
  if (!conectado) {
    avisar(HEv::ENR_ERROR, 0, 0, 1, "El lector de huella no está conectado");
    return;
  }
  posEnrolar = id;
  modo = Modo::PASO1;
  desde = millis();
  avisar(HEv::ENR_PON_DEDO, 0, 0, 1);
}

void huella_borrar(uint16_t id) {
  if (!conectado) return;
  memoria.erase(id);
  guardar();
}

// En la placa estas tres van por la cola de fondo y contestan con un aviso; aquí al tiro
bool huella_sacar_plantilla(uint16_t id) {
  if (!conectado) return avisar(HEv::PLANTILLA_ERROR, id, 0x01, 1);
  auto it = memoria.find(id);
  if (it == memoria.end()) return avisar(HEv::PLANTILLA_ERROR, id, 0x0C, 1);  // LoadChar: no hay nada
  uint8_t* b = (uint8_t*)malloc(LARGO_PLANTILLA);
  plantillaDe(it->second, b);
  if (!avisar(HEv::PLANTILLA_SACADA, id, 0, 1, "", b, LARGO_PLANTILLA)) {
    free(b);
    return false;
  }
  Serial.printf("[huella] sacar plantilla %u: dedo %d\n", id, it->second);
  return true;
}

bool huella_poner_plantilla(uint16_t id, uint8_t* datos, uint16_t largo) {
  if (!conectado) {
    free(datos);
    return avisar(HEv::PLANTILLA_ERROR, id, 0x01, 2);
  }
  const int dedo = dedoDe(datos, largo);
  free(datos);
  memoria[id] = dedo;
  guardar();
  Serial.printf("[huella] poner plantilla %u: dedo %d (%u bytes)\n", id, dedo, largo);
  return avisar(HEv::PLANTILLA_PUESTA, id);
}

bool huella_quitar(uint16_t id) {
  if (!conectado) return avisar(HEv::BORRADA, id, 0x01, 0);
  memoria.erase(id);
  guardar();
  Serial.printf("[huella] quitar %u\n", id);
  return avisar(HEv::BORRADA, id, 0, 1);
}

void huella_borrar_todo() {
  if (!conectado) return;
  memoria.clear();
  guardar();
}

bool huella_evento(HuellaEv& ev) {
  if (avisos.empty()) return false;
  ev = avisos.front();
  avisos.pop_front();
  return true;
}

bool huella_conectado() { return conectado; }
uint16_t huella_capacidad() { return capacidad; }

// ------------------------------------------------------- lo que hace el panel --
void sim_lector_tick() {
  const uint32_t ahora = millis();
  if (enchufado && !conectado && conectarEn && ahora >= conectarEn) {
    conectarEn = 0;
    conectado = true;
    capacidad = CAPACIDAD;
    Serial.printf("[huella] conectado, capacidad %u\n", CAPACIDAD);
    avisar(HEv::CONECTADO, CAPACIDAD);
  }
  if (modo == Modo::SACAR && ahora - desde > 700) {
    modo = Modo::PASO2;
    desde = ahora;
    avisar(HEv::ENR_PON_DEDO, 0, 0, 2);
  }
  if ((modo == Modo::PASO1 || modo == Modo::PASO2) && ahora - desde > 30000) {
    avisar(HEv::ENR_ERROR, 0, 0, modo == Modo::PASO1 ? 1 : 2, "Se acabó el tiempo");
    modo = Modo::REPOSO;
  }
}

void sim_lector_dedo(int dedo) {
  if (!conectado) return;
  switch (modo) {
    case Modo::REPOSO:
      if (!escaneando) return;
      {
        const int pos = buscar(dedo);
        if (pos >= 0) avisar(HEv::COINCIDE, (uint16_t)pos, 120);
        else avisar(HEv::NO_COINCIDE);
      }
      break;
    case Modo::PASO1: {
      const int pos = buscar(dedo);
      if (pos >= 0) {
        avisar(HEv::ENR_DUPLICADA, (uint16_t)pos);
        modo = Modo::REPOSO;
        return;
      }
      primerDedo = dedo;
      avisar(HEv::ENR_SACA_DEDO);
      modo = Modo::SACAR;
      desde = millis();
      break;
    }
    case Modo::SACAR:
      break;
    case Modo::PASO2:
      modo = Modo::REPOSO;
      if (dedo != primerDedo) {
        avisar(HEv::ENR_ERROR, 0, 0, 2, "Las dos lecturas no coinciden. Usa el mismo dedo.");
        return;
      }
      memoria[posEnrolar] = dedo;
      guardar();
      avisar(HEv::ENR_OK, posEnrolar);
      break;
  }
}

void sim_lector_mal() {
  if (!conectado) return;
  if (modo == Modo::PASO1 || modo == Modo::PASO2) avisar(HEv::IMAGEN_MALA, 0, 0, modo == Modo::PASO1 ? 1 : 2);
  else if (modo == Modo::REPOSO && escaneando) avisar(HEv::IMAGEN_MALA);
}

void sim_lector_enchufar(bool on) {
  if (on == enchufado) return;
  enchufado = on;
  if (on) {
    conectarEn = millis() + 2000;  // la placa reintenta cada 2 s
    return;
  }
  if (conectado) {
    conectado = false;
    Serial.println("[huella] el lector no responde");
    avisar(HEv::DESCONECTADO);
    if (modo != Modo::REPOSO) avisar(HEv::ENR_ERROR, 0, 0, 1, "El lector de huella no responde");
    modo = Modo::REPOSO;
  }
}

bool sim_lector_enchufado() { return enchufado; }
int sim_lector_posicion(int dedo) { return buscar(dedo); }

String sim_lector_memoria() {
  String r;
  for (auto& kv : memoria) r += (r.length() ? "," : "") + String(kv.first) + ":" + String(kv.second);
  return r;
}

String sim_lector_estado() {
  if (!enchufado) return "Desenchufado";
  if (!conectado) return "Arrancando...";
  switch (modo) {
    case Modo::PASO1: return "Registro: esperando dedo (1 de 2)";
    case Modo::SACAR: return "Registro: saca el dedo";
    case Modo::PASO2: return "Registro: esperando dedo (2 de 2)";
    default: return escaneando ? "Listo para marcar" : "En pausa (menú abierto)";
  }
}
