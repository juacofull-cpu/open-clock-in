// Conexión con el servidor (contrato en docs/API.md).
// Los trabajadores son los perfiles del panel; aquí quedan copias para seguir
// funcionando sin internet:
//   /perfiles.json         último GET /api/reloj/perfiles (huella -> perfil, nombre, foto)
//   /hoy.json              marcas que este reloj hizo hoy (para saludar y para «Marcas de hoy»)
//   /cola.txt              marcas por mandar, en orden: id;huella;hora UTC (0 = sin hora);oficina
//                          (las líneas viejas no traen oficina y se mandan sin ella)
//   /fotos/<perfil>.jpg    foto de cada perfil, y /fotos/versiones.txt con su versión
//   /plantillas.txt        huellas que tiene ESTE lector: número;versión de su plantilla
//                          ("" = registrada aquí y todavía sin subir al panel)
// Todo corre en la tarea del loop; solo el pedido HTTP va en otra tarea (http.cpp).
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <sys/time.h>
#include "config.h"
#include "datos.h"
#include "fotos.h"
#include "http.h"
#include "panel.h"
#include "red.h"

enum : uint8_t { P_PERFILES = 1, P_ASIGNAR, P_SOLTAR, P_MARCA, P_FOTO, P_SUBIR, P_BAJAR };
enum class Conexion : uint8_t { CONECTANDO, OK, LLAVE_MALA, NO_ACTIVADO, FALLA };

struct EnCola {
  String id;
  uint16_t huella;
  time_t t;     // 0 = sin hora
  uint32_t ms;  // millis() al marcar (0 = de antes de reiniciar)
  String oficina;  // la del reloj al marcar ("" = marca de antes de las dos oficinas)
};

// Pedido de la interfaz que espera turno
struct Op {
  uint8_t tipo;
  uint16_t huella;
  String perfil;
};

static std::vector<Perfil> perfiles;
static std::vector<MarcaHoy> hoy;
static String hoyFecha;
static std::vector<EnCola> cola;
static std::map<String, String> fotoLocal;  // perfil -> versión guardada
static std::deque<PanelEv> eventos;

static bool hayOp = false;
static Op op, opEnCurso;
static bool enCurso = false;
static String idEnviado, fotoEnviada;
static uint32_t proximoIntento = 0, espera = 0;  // reintentos de lo automático
static uint32_t ultimosPerfiles = 0;
static bool perfilesAlDia = false;  // ya se bajaron en este arranque
static bool refrescoPedido = false;  // «No te conozco»: pedir la lista apenas se pueda
static Conexion estado = Conexion::CONECTANDO;
static String estadoTexto;

// Plantillas (contrato «Dos relojes»): el dedo registrado en un reloj se sube al panel y el
// otro reloj lo baja a su lector. Lo del lector va de a una orden por vez.
enum class OpLector : uint8_t { NADA, SACANDO, PONIENDO, QUITANDO };
static std::map<uint16_t, String> enLector;          // /plantillas.txt
static std::map<uint16_t, String> plantillasPanel;   // último GET perfiles: número -> versión
static bool panelConPlantillas = false;              // el panel ya manda "plantillas"
static std::map<uint16_t, uint32_t> reintentoNum;    // número -> millis() desde cuando se reintenta
static OpLector opLector = OpLector::NADA;
static uint16_t numLector = 0;
static uint32_t desdeLector = 0;
static String versionPoniendo;
static uint16_t numSubir = 0;  // plantilla ya sacada del lector, esperando turno para subir
static std::vector<uint8_t> datosSubir;

// ---------------------------------------------------------------- ayudas --
static bool escribir(const char* ruta, const std::function<void(File&)>& fn) {
  const String tmp = String(ruta) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  fn(f);
  f.close();
  return LittleFS.rename(tmp.c_str(), ruta);
}

// Escribir en la flash detiene los dos núcleos (la pantalla se congela unas decenas de ms).
// Lo que se puede rehacer (hoy.json, la cola ya mandada: el panel no duplica por el id) se
// guarda después, cuando terminó el saludo. Lo nuevo de la cola se guarda al tiro.
static bool hoySucio = false, colaSucia = false;
static uint32_t sucioDesde = 0;
#define MS_GUARDAR_DESPUES 5000  // saludo (3,5 s) + su salida

static void guardarDespues(bool hoy_, bool cola_) {
  hoySucio |= hoy_;
  colaSucia |= cola_;
  sucioDesde = millis();
}

static String isoUtc(time_t t) {
  struct tm tm;
  gmtime_r(&t, &tm);
  char b[24];
  strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return String(b);
}

// "2026-10-25T11:02:11Z" -> segundos desde 1970 (UTC)
static time_t deIsoUtc(const char* s) {
  int Y, M, D, h, m, seg;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &seg) != 6) return 0;
  Y -= M <= 2;  // días desde 1970 (algoritmo de H. Hinnant)
  const long era = (Y >= 0 ? Y : Y - 399) / 400;
  const long yoe = Y - era * 400;
  const long doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long dias = era * 146097 + doe - 719468;
  return (time_t)((long long)dias * 86400 + h * 3600 + m * 60 + seg);
}

// Versión de una plantilla: 12 primeros caracteres del sha256 de sus bytes (como el panel)
static String versionDe(const uint8_t* d, size_t n) {
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
      0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
      0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
      0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  auto rot = [](uint32_t x, int r) { return (x >> r) | (x << (32 - r)); };
  const uint64_t bits = (uint64_t)n * 8;
  const size_t total = ((n + 9 + 63) / 64) * 64;  // con el 0x80 y el largo al final
  for (size_t bloque = 0; bloque < total; bloque += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      uint32_t x = 0;
      for (int j = 0; j < 4; j++) {
        const size_t k = bloque + i * 4 + j;
        uint8_t b = k < n ? d[k] : k == n ? 0x80 : 0;
        if (k >= total - 8) b = (uint8_t)(bits >> (8 * (total - 1 - k)));
        x = (x << 8) | b;
      }
      w[i] = x;
    }
    for (int i = 16; i < 64; i++) {
      const uint32_t s0 = rot(w[i - 15], 7) ^ rot(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rot(w[i - 2], 17) ^ rot(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], e4 = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      const uint32_t t1 = hh + (rot(e, 6) ^ rot(e, 11) ^ rot(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
      const uint32_t t2 = (rot(a, 2) ^ rot(a, 13) ^ rot(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g;
      g = f;
      f = e;
      e = e4 + t1;
      e4 = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += e4, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
  }
  char hex[13];
  snprintf(hex, sizeof(hex), "%08lx%04lx", (unsigned long)h[0], (unsigned long)(h[1] >> 16));
  return String(hex);
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static String aBase64(const uint8_t* d, size_t n) {
  String r;
  r.reserve((n + 2) / 3 * 4 + 1);
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    r += B64[(v >> 18) & 63];
    r += B64[(v >> 12) & 63];
    r += i + 1 < n ? B64[(v >> 6) & 63] : '=';
    r += i + 2 < n ? B64[v & 63] : '=';
  }
  return r;
}

// Devuelve el largo, o -1 si no es base64 o no cabe en max
static int deBase64(const char* s, uint8_t* out, size_t max) {
  size_t n = 0;
  uint32_t v = 0;
  int bits = 0;
  for (; *s && *s != '='; s++) {
    const char* p = strchr(B64, *s);
    if (!p || !*s) {
      if (*s == '\n' || *s == '\r') continue;
      return -1;
    }
    v = (v << 6) | (uint32_t)(p - B64);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= max) return -1;
      out[n++] = (uint8_t)(v >> bits);
    }
  }
  return (int)n;
}

static String nuevoId() {
  char b[20];
  snprintf(b, sizeof(b), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  return String(b);
}

static String detalle(const String& resp) {
  JsonDocument d;
  if (deserializeJson(d, resp)) return String();
  const char* t = d["detail"] | "";
  return String(t);
}

// FastAPI contesta 404 {"detail":"Not Found"} si la ruta no existe (panel sin actualizar)
static bool sinRutas(int codigo, const String& resp) { return codigo == 404 && detalle(resp) == "Not Found"; }

static String motivo(int codigo, const String& resp) {
  if (codigo == -2) return "La dirección del panel tiene que empezar con https://";
  if (codigo < 0) return "Necesita internet";
  if (codigo == 401) return "La llave del panel está mala";
  if (codigo == 503) return "El reloj no está activado en el panel";
  if (sinRutas(codigo, resp)) return "El panel no tiene la conexión del reloj";
  const String d = detalle(resp);
  if (d.length() && d.length() < 80) return d;
  return "El panel respondió con error " + String(codigo);
}

// Lo que dice cada respuesta de la conexión en general
static void anotarConexion(int codigo, const String& resp) {
  if (codigo == 401) estado = Conexion::LLAVE_MALA;
  else if (codigo == 503) estado = Conexion::NO_ACTIVADO;
  else if (codigo < 0 || codigo >= 500 || sinRutas(codigo, resp)) estado = Conexion::FALLA;
  else estado = Conexion::OK;
  estadoTexto = estado == Conexion::OK ? String() : motivo(codigo, resp);
  if (estado == Conexion::OK) {
    espera = 0;
    proximoIntento = millis();
  } else {
    espera = espera ? std::min<uint32_t>(espera * 2, 60000) : 5000;
    proximoIntento = millis() + espera;
  }
}

static void avisar(PEv ev, uint16_t huella = 0, const String& texto = String()) {
  PanelEv e;
  e.ev = ev;
  e.huella = huella;
  e.texto = texto;
  eventos.push_back(e);
}

// --------------------------------------------------------------- perfiles --
static void leerPerfiles(JsonArrayConst arr) {
  perfiles.clear();
  for (JsonObjectConst o : arr) {
    Perfil p;
    p.perfil = (const char*)(o["perfil"] | "");
    p.nombre = (const char*)(o["nombre"] | "");
    p.foto = (const char*)(o["foto"] | "");
    for (uint16_t h : o["huellas"].as<JsonArrayConst>()) p.huellas.push_back(h);
    if (p.perfil.length()) perfiles.push_back(p);
  }
}

static String perfilesGuardados;  // lo último escrito: la lista llega cada 3 min casi siempre igual

static String textoPerfiles() {
  JsonDocument d;
  JsonArray arr = d["perfiles"].to<JsonArray>();
  for (auto& p : perfiles) {
    JsonObject o = arr.add<JsonObject>();
    o["perfil"] = p.perfil;
    o["nombre"] = p.nombre;
    o["foto"] = p.foto;
    JsonArray h = o["huellas"].to<JsonArray>();
    for (uint16_t x : p.huellas) h.add(x);
  }
  String txt;
  serializeJson(d, txt);
  return txt;
}

static void guardarPerfiles() {
  const String txt = textoPerfiles();
  if (txt == perfilesGuardados) return;
  if (escribir("/perfiles.json", [&](File& f) { f.print(txt); })) perfilesGuardados = txt;
}

static void cargarPerfiles() {
  File f = LittleFS.open("/perfiles.json", "r");
  if (!f) return;
  JsonDocument d;
  if (!deserializeJson(d, f)) {
    leerPerfiles(d["perfiles"].as<JsonArrayConst>());
    perfilesGuardados = textoPerfiles();  // lo que hay en la flash, escrito igual
  }
  f.close();
}

static Perfil* perfilMut(const String& id) {
  for (auto& p : perfiles)
    if (p.perfil == id) return &p;
  return nullptr;
}

static void soltarHuella(uint16_t huella) {
  for (auto& p : perfiles)
    for (size_t i = 0; i < p.huellas.size(); i++)
      if (p.huellas[i] == huella) p.huellas.erase(p.huellas.begin() + i--);
}

// El panel dijo que esa huella es de ese perfil: el caché se corrige
static void ponerHuella(uint16_t huella, const String& perfil, const String& nombre) {
  Perfil* p = perfilMut(perfil);
  if (p && std::find(p->huellas.begin(), p->huellas.end(), huella) != p->huellas.end()) return;
  soltarHuella(huella);
  if (!p) {
    perfiles.push_back(Perfil());
    p = &perfiles.back();
    p->perfil = perfil;
    p->nombre = nombre;
  }
  p->huellas.push_back(huella);
  guardarPerfiles();
}

const std::vector<Perfil>& panel_perfiles() { return perfiles; }

const Perfil* panel_por_huella(uint16_t huella) {
  for (auto& p : perfiles)
    for (uint16_t h : p.huellas)
      if (h == huella) return &p;
  return nullptr;
}

const Perfil* panel_buscar(const String& perfil) { return perfilMut(perfil); }

int panel_huella_libre(uint16_t capacidad) {
  // Tampoco una que el lector todavía tiene y está por borrarse (se cruzarían)
  for (uint16_t id = 1; id < capacidad && id <= 999; id++)
    if (!panel_por_huella(id) && !enLector.count(id)) return id;
  return -1;
}

uint16_t panel_huellas_usadas() {
  uint16_t n = 0;
  for (auto& p : perfiles) n += p.huellas.size();
  return n;
}

// ------------------------------------------------------------------ fotos --
static void guardarVersiones() {
  escribir("/fotos/versiones.txt", [](File& f) {
    for (auto& kv : fotoLocal) f.printf("%s;%s\n", kv.first.c_str(), kv.second.c_str());
  });
}

static void cargarVersiones() {
  File f = LittleFS.open("/fotos/versiones.txt", "r");
  if (!f) return;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    const int a = l.indexOf(';');
    if (a > 0) fotoLocal[l.substring(0, a)] = l.substring(a + 1);
  }
  f.close();
}

// Perfiles cuya foto hay que bajar (o borrar porque ya no tienen)
static const Perfil* fotoPendiente() {
  for (auto& p : perfiles) {
    auto it = fotoLocal.find(p.perfil);
    const String local = it == fotoLocal.end() ? String() : it->second;
    if (p.foto.length() && p.foto != local) return &p;
  }
  return nullptr;
}

static void limpiarFotos() {
  bool cambio = false;
  for (auto it = fotoLocal.begin(); it != fotoLocal.end();) {
    const Perfil* p = perfilMut(it->first);
    if (!p || !p->foto.length()) {
      LittleFS.remove(foto_ruta(it->first).c_str());
      fotos_cambio(it->first);
      it = fotoLocal.erase(it);
      cambio = true;
    } else {
      ++it;
    }
  }
  if (cambio) guardarVersiones();
}

// -------------------------------------------------------------------- hoy --
static void guardarHoy() {
  hoySucio = false;
  JsonDocument d;
  d["fecha"] = hoyFecha;
  JsonArray arr = d["marcas"].to<JsonArray>();
  for (auto& m : hoy) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = m.id;
    o["h"] = m.huella;
    o["p"] = m.perfil;
    o["n"] = m.nombre;
    o["t"] = m.tipo;
    o["hr"] = m.hora;
    o["s"] = (long long)m.t;
    o["r"] = m.repetida;
    o["e"] = m.enviada;
  }
  escribir("/hoy.json", [&](File& f) { serializeJson(d, f); });
}

static void cargarHoy() {
  File f = LittleFS.open("/hoy.json", "r");
  if (!f) return;
  JsonDocument d;
  if (!deserializeJson(d, f)) {
    hoyFecha = (const char*)(d["fecha"] | "");
    for (JsonObjectConst o : d["marcas"].as<JsonArrayConst>()) {
      MarcaHoy m;
      m.id = (const char*)(o["id"] | "");
      m.huella = o["h"] | 0;
      m.perfil = (const char*)(o["p"] | "");
      m.nombre = (const char*)(o["n"] | "");
      m.tipo = (const char*)(o["t"] | "");
      m.hora = (const char*)(o["hr"] | "");
      m.t = (time_t)(o["s"] | (long long)0);
      m.ms = 0;
      m.repetida = o["r"] | false;
      m.enviada = o["e"] | false;
      hoy.push_back(m);
    }
  }
  f.close();
}

// Al cambiar el día (hora de Chile, como el panel) se parte de cero
static void asegurarDia() {
  if (!hora_valida()) return;
  const String f = fecha_hoy();
  if (f == hoyFecha) return;
  hoyFecha = f;
  hoy.clear();
  guardarHoy();
}

static MarcaHoy* buscarHoy(const String& id) {
  for (auto& m : hoy)
    if (m.id == id) return &m;
  return nullptr;
}

static void quitarHoy(const String& id) {
  for (size_t i = 0; i < hoy.size(); i++)
    if (hoy[i].id == id) {
      hoy.erase(hoy.begin() + i);
      guardarDespues(true, false);
      return;
    }
}

// Última marca que contó de esa persona hoy
static const MarcaHoy* ultimaDe(const String& perfil) {
  for (size_t i = hoy.size(); i-- > 0;)
    if (hoy[i].perfil == perfil && !hoy[i].repetida && hoy[i].tipo.length()) return &hoy[i];
  return nullptr;
}

const std::vector<MarcaHoy>& panel_marcas_hoy() { return hoy; }

// ------------------------------------------------------------------- cola --
static void guardarCola() {
  colaSucia = false;
  escribir("/cola.txt", [](File& f) {
    for (auto& c : cola)
      f.printf("%s;%u;%lld;%s\n", c.id.c_str(), (unsigned)c.huella, (long long)c.t, c.oficina.c_str());
  });
}

static void cargarCola() {
  File f = LittleFS.open("/cola.txt", "r");
  if (!f) return;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    const int a = l.indexOf(';'), b = l.indexOf(';', a + 1), c = l.indexOf(';', b + 1);
    if (a < 8 || b < 0) continue;
    String oficina = c < 0 ? String() : l.substring(c + 1);  // las de antes no traen oficina
    if (!oficina_valida(oficina)) oficina = "";
    cola.push_back({l.substring(0, a), (uint16_t)l.substring(a + 1, b).toInt(),
                    (time_t)atoll(l.substring(b + 1, c < 0 ? l.length() : c).c_str()), 0, oficina});
  }
  f.close();
}

size_t panel_en_cola() { return cola.size(); }

// Marcas hechas sin hora: apenas llega la hora se les pone la que correspondía
static void ponerHoras() {
  if (!hora_valida()) return;
  const time_t ahora = time(nullptr);
  bool cambioCola = false, cambioHoy = false;
  for (auto& c : cola)
    if (!c.t && c.ms) {
      c.t = ahora - (time_t)((millis() - c.ms) / 1000);
      c.ms = 0;
      cambioCola = true;
    }
  for (auto& m : hoy)
    if (!m.t && m.ms) {
      m.t = ahora - (time_t)((millis() - m.ms) / 1000);
      m.ms = 0;
      if (!m.hora.length()) m.hora = hora_local(m.t, "%H:%M");
      cambioHoy = true;
    }
  if (cambioCola) guardarCola();
  if (cambioHoy) {
    if (!hoyFecha.length()) hoyFecha = fecha_hoy();
    guardarHoy();
  }
}

// ----------------------------------------------------------------- marcar --
Saludo panel_marcar(uint16_t huella) {
  asegurarDia();
  const time_t ahora = hora_valida() ? time(nullptr) : 0;
  const uint32_t ms = millis() | 1;
  Saludo s;
  s.id = nuevoId();
  s.hora = ahora ? hora_local(ahora, "%H:%M") : String();
  const Perfil* p = panel_por_huella(huella);
  MarcaHoy m = {};
  m.id = s.id;
  m.huella = huella;
  m.t = ahora;
  m.ms = ahora ? 0 : ms;
  m.hora = s.hora;
  if (p) {
    s.nombre = m.nombre = p->nombre;
    s.perfil = m.perfil = p->perfil;
    const MarcaHoy* u = ultimaDe(p->perfil);
    if (u && u->t && ahora && ahora >= u->t && ahora - u->t < SEG_MARCA_REPETIDA) {
      s.repetida = true;
      s.tipo = u->tipo;
    } else {
      s.tipo = (u && u->tipo == "ENTRADA") ? "SALIDA" : "ENTRADA";
    }
  }
  m.tipo = s.tipo;
  m.repetida = s.repetida;
  hoy.push_back(m);
  guardarDespues(true, false);
  // Se manda igual aunque no se conozca la huella: el panel es el que sabe.
  // La cola sí se guarda al tiro (antes de que empiece el saludo): si se corta la luz, la marca no se pierde.
  cola.push_back({s.id, huella, ahora, ahora ? 0 : ms, ajustes().oficina});
  guardarCola();
  return s;
}

// ------------------------------------------------------------- plantillas --
static bool mandar(uint8_t tipo, uint32_t ref, const char* metodo, const String& ruta, const String& cuerpo,
                   const String& archivo = String());

static void guardarEnLector() {
  escribir("/plantillas.txt", [](File& f) {
    for (auto& kv : enLector) f.printf("%u;%s\n", (unsigned)kv.first, kv.second.c_str());
  });
}

static void cargarEnLector() {
  File f = LittleFS.open("/plantillas.txt", "r");
  if (!f) {
    // Primera vez con esta versión: los dedos ya registrados (los del caché) están en este
    // lector y no tienen plantilla en el panel: se suben.
    for (auto& p : perfiles)
      for (uint16_t h : p.huellas) enLector[h] = "";
    guardarEnLector();
    return;
  }
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    const int a = l.indexOf(';');
    if (a > 0) enLector[(uint16_t)l.substring(0, a).toInt()] = l.substring(a + 1);
  }
  f.close();
}

static void esperarNum(uint16_t n, uint32_t ms) { reintentoNum[n] = millis() + ms; }

static bool listoNum(uint16_t n) {
  auto it = reintentoNum.find(n);
  return it == reintentoNum.end() || (int32_t)(millis() - it->second) >= 0;
}

static void empezarLector(OpLector op, uint16_t n) {
  opLector = op;
  numLector = n;
  desdeLector = millis();
}

// Lo que se hace con el lector según la última lista del panel. true = se mandó un pedido HTTP.
static bool sincronizarPlantillas() {
  if (opLector != OpLector::NADA) {
    if (millis() - desdeLector < 30000) return false;
    Serial.printf("[panel] el lector no contestó por la plantilla %u\n", numLector);
    esperarNum(numLector, 60000);
    opLector = OpLector::NADA;
  }
  if (!perfilesAlDia || !huella_conectado()) return false;
  // 1. Un número que el panel ya no tiene sale del lector (lista vacía = algo raro: no se borra nada)
  if (!perfiles.empty())
    for (auto& kv : enLector) {
      const uint16_t n = kv.first;
      if (panel_por_huella(n) || !listoNum(n)) continue;
      if (huella_quitar(n)) empezarLector(OpLector::QUITANDO, n);
      return false;
    }
  if (!panelConPlantillas) return false;  // panel sin plantillas todavía
  // 2. Dedos registrados aquí que el panel no tiene: se sacan del lector y se suben
  for (auto& kv : enLector) {
    const uint16_t n = kv.first;
    if (kv.second.length() || !panel_por_huella(n) || plantillasPanel.count(n) || !listoNum(n)) continue;
    if (huella_sacar_plantilla(n)) empezarLector(OpLector::SACANDO, n);
    return false;
  }
  // 3. Dedos que este lector no tiene (o con otra versión): se bajan del panel
  for (auto& kv : plantillasPanel) {
    const uint16_t n = kv.first;
    if (!panel_por_huella(n) || !listoNum(n)) continue;
    auto it = enLector.find(n);
    if (it != enLector.end() && it->second == kv.second) continue;
    return mandar(P_BAJAR, n, "GET", "/api/reloj/huellas/" + String(n) + "/plantilla", String());
  }
  return false;
}

static void mandarSubir() {
  String cuerpo = "{\"plantilla\":\"" + aBase64(datosSubir.data(), datosSubir.size()) + "\"}";
  if (!mandar(P_SUBIR, numSubir, "PUT", "/api/reloj/huellas/" + String(numSubir) + "/plantilla", cuerpo))
    return;
  Serial.printf("[panel] subiendo plantilla %u (%u bytes)\n", numSubir, (unsigned)datosSubir.size());
}

static void respuestaSubir(PedidoHttp* p) {
  const uint16_t n = p->ref;
  if (n != numSubir) return;
  if (p->codigo == 200) {
    JsonDocument d;
    deserializeJson(d, p->respuesta);
    String v = (const char*)(d["version"] | "");
    if (!v.length()) v = versionDe(datosSubir.data(), datosSubir.size());
    if (enLector.count(n)) {
      enLector[n] = v;
      guardarEnLector();
    }
    plantillasPanel[n] = v;
    reintentoNum.erase(n);
  } else if (sinRutas(p->codigo, p->respuesta) || p->codigo == 400 || p->codigo == 413 || p->codigo == 422) {
    esperarNum(n, 30 * 60 * 1000UL);  // el panel no la acepta: no insistir seguido
  } else if (p->codigo == 404) {
    // Ese número ya no tiene perfil: el paso 1 lo saca del lector
  } else {
    esperarNum(n, 60000);  // sin red o panel caído: se vuelve a sacar del lector más tarde
  }
  Serial.printf("[panel] plantilla %u subida: %d\n", n, p->codigo);
  numSubir = 0;
  datosSubir.clear();
  datosSubir.shrink_to_fit();
}

static void respuestaBajar(PedidoHttp* p) {
  const uint16_t n = p->ref;
  Serial.printf("[panel] plantilla %u bajada: %d\n", n, p->codigo);
  if (p->codigo == 404 && !sinRutas(p->codigo, p->respuesta)) {
    plantillasPanel.erase(n);  // ya no la tiene
    return;
  }
  if (p->codigo != 200) {
    esperarNum(n, 60000);
    return;
  }
  JsonDocument d;
  uint8_t* buf = (uint8_t*)malloc(PLANTILLA_MAX);
  const int largo = (!buf || deserializeJson(d, p->respuesta)) ? -1 : deBase64(d["plantilla"] | "", buf, PLANTILLA_MAX);
  if (largo < PLANTILLA_MIN) {
    Serial.printf("[panel] la plantilla %u no sirve (%d bytes)\n", n, largo);
    free(buf);
    esperarNum(n, 30 * 60 * 1000UL);
    return;
  }
  String v = (const char*)(d["version"] | "");
  if (!v.length()) v = versionDe(buf, largo);
  if (opLector != OpLector::NADA || !huella_poner_plantilla(n, buf, largo)) {  // se queda con buf
    if (opLector != OpLector::NADA) free(buf);
    esperarNum(n, 3000);
    return;
  }
  versionPoniendo = v;
  empezarLector(OpLector::PONIENDO, n);
}

void panel_lector(const HuellaEv& ev) {
  const bool esperada = opLector != OpLector::NADA && ev.id == numLector;
  const uint16_t n = ev.id;
  switch (ev.ev) {
    case HEv::PLANTILLA_SACADA:
      if (esperada && opLector == OpLector::SACANDO && enLector.count(n) && !numSubir) {
        numSubir = n;
        datosSubir.assign(ev.datos, ev.datos + ev.largo);
      }
      free(ev.datos);
      break;
    case HEv::PLANTILLA_PUESTA:
      enLector[n] = esperada ? versionPoniendo : String();
      reintentoNum.erase(n);
      guardarEnLector();
      Serial.printf("[panel] plantilla %u en el lector\n", n);
      break;
    case HEv::PLANTILLA_ERROR:
      Serial.printf("[panel] plantilla %u: el lector dijo %u al %s\n", n, ev.confianza, ev.paso == 1 ? "sacarla" : "ponerla");
      if (ev.paso == 1 && ev.confianza == 0x0C) {  // no hay nada en ese número: no hay qué subir
        enLector.erase(n);
        guardarEnLector();
      } else {
        esperarNum(n, 60000);
      }
      break;
    case HEv::BORRADA:
      if (ev.paso == 1) {
        enLector.erase(n);
        guardarEnLector();
        Serial.printf("[panel] huella %u borrada del lector (el panel ya no la tiene)\n", n);
      } else {
        esperarNum(n, 60000);
      }
      break;
    default:
      return;
  }
  if (esperada) opLector = OpLector::NADA;
}

String panel_plantillas_estado() {
  unsigned porSubir = 0, porBajar = 0;
  for (auto& kv : enLector)
    if (!kv.second.length() && panel_por_huella(kv.first) && !plantillasPanel.count(kv.first)) porSubir++;
  for (auto& kv : plantillasPanel) {
    auto it = enLector.find(kv.first);
    if (panel_por_huella(kv.first) && (it == enLector.end() || it->second != kv.second)) porBajar++;
  }
  return String((unsigned)enLector.size()) + " en el lector · " + String(porSubir) + " por subir · " +
         String(porBajar) + " por bajar";
}

// ------------------------------------------------------------- respuestas --
static void respuestaPerfiles(PedidoHttp* p) {
  const bool pedidoPorUi = p->ref == 1;
  if (p->codigo != 200) {
    if (pedidoPorUi) avisar(PEv::PERFILES_ERROR, 0, motivo(p->codigo, p->respuesta));
    return;
  }
  JsonDocument d;
  if (deserializeJson(d, p->respuesta) || !d["perfiles"].is<JsonArrayConst>()) {
    if (pedidoPorUi) avisar(PEv::PERFILES_ERROR, 0, "El panel mandó una lista que no se entiende");
    return;
  }
  leerPerfiles(d["perfiles"].as<JsonArrayConst>());
  guardarPerfiles();
  limpiarFotos();
  plantillasPanel.clear();
  panelConPlantillas = d["plantillas"].is<JsonObjectConst>();
  for (JsonPairConst kv : d["plantillas"].as<JsonObjectConst>()) {
    const int n = atoi(kv.key().c_str());
    const char* v = kv.value() | "";
    if (n >= 0 && n <= 999 && *v) plantillasPanel[(uint16_t)n] = v;
  }
  ultimosPerfiles = millis();
  perfilesAlDia = true;
  // Sin hora por internet (NTP), se usa la del panel
  const time_t ahora = deIsoUtc(d["ahora"] | "");
  if (!hora_valida() && ahora > 1735689600) {
    struct timeval tv = {ahora, 0};
    settimeofday(&tv, nullptr);
    Serial.println("[panel] hora tomada del panel");
  }
  Serial.printf("[panel] %u perfiles\n", (unsigned)perfiles.size());
  avisar(PEv::PERFILES);
}

static void respuestaMarca(PedidoHttp* p) {
  if (cola.empty() || cola.front().id != idEnviado) return;
  const EnCola c = cola.front();
  PanelEv e;
  e.ev = PEv::MARCA;
  e.huella = c.huella;
  e.saludo.id = c.id;
  if (p->codigo == 200) {
    JsonDocument d;
    deserializeJson(d, p->respuesta);
    e.saludo.nombre = (const char*)(d["nombre"] | "");
    e.saludo.tipo = (const char*)(d["tipo"] | "");
    e.saludo.repetida = d["repetida"] | false;
    e.saludo.hora = (const char*)(d["hora"] | "");
    const String perfil = (const char*)(d["perfil"] | "");
    e.saludo.perfil = perfil;
    if (MarcaHoy* m = buscarHoy(c.id)) {
      m->perfil = perfil;
      m->nombre = e.saludo.nombre;
      m->tipo = e.saludo.tipo;
      m->repetida = e.saludo.repetida;
      if (e.saludo.hora.length()) m->hora = e.saludo.hora;
      m->enviada = true;
      guardarDespues(true, false);  // llega con el saludo en pantalla: no congelarlo
    }
    if (perfil.length()) ponerHuella(c.huella, perfil, e.saludo.nombre);
    Serial.printf("[panel] marca %s: %s %s%s\n", c.id.c_str(), perfil.c_str(), e.saludo.tipo.c_str(),
                  e.saludo.repetida ? " (repetida)" : "");
  } else if (p->codigo == 404 && !sinRutas(p->codigo, p->respuesta)) {
    // Huella sin perfil: no se guardó nada
    quitarHoy(c.id);
    soltarHuella(c.huella);
    guardarPerfiles();
    Serial.printf("[panel] marca %s: huella %u sin perfil\n", c.id.c_str(), c.huella);
  } else if (p->codigo == 400 || p->codigo == 422) {
    // Nunca va a pasar (por ejemplo, hora de hace más de 7 días): se descarta
    quitarHoy(c.id);
    Serial.printf("[panel] marca %s descartada (%d): %s\n", c.id.c_str(), p->codigo, p->respuesta.c_str());
    cola.erase(cola.begin());
    guardarDespues(false, true);
    return;
  } else {
    return;  // sin red, llave mala, panel caído: queda en la cola y se reintenta
  }
  // Si se apaga antes de guardar, se vuelve a mandar con el mismo id y el panel no la duplica
  cola.erase(cola.begin());
  guardarDespues(false, true);
  eventos.push_back(e);
}

static void respuestaFoto(PedidoHttp* p) {
  const String tmp = foto_ruta(fotoEnviada) + ".tmp";
  const Perfil* pf = perfilMut(fotoEnviada);
  if (p->codigo == 200 && pf) {
    LittleFS.rename(tmp.c_str(), foto_ruta(fotoEnviada).c_str());
    fotoLocal[fotoEnviada] = pf->foto;
  } else {
    LittleFS.remove(tmp.c_str());
    if (p->codigo == 404 && pf && !sinRutas(p->codigo, p->respuesta)) {
      fotoLocal[fotoEnviada] = pf->foto;  // no tiene: no insistir hasta que cambie
      LittleFS.remove(foto_ruta(fotoEnviada).c_str());
    } else {
      return;
    }
  }
  guardarVersiones();
  fotos_cambio(fotoEnviada);
  Serial.printf("[panel] foto de %s: %d\n", fotoEnviada.c_str(), p->codigo);
}

static void procesar(PedidoHttp* p) {
  anotarConexion(p->codigo, p->respuesta);
  switch (p->tipo) {
    case P_PERFILES: respuestaPerfiles(p); break;
    case P_MARCA: respuestaMarca(p); break;
    case P_FOTO: respuestaFoto(p); break;
    case P_SUBIR: respuestaSubir(p); break;
    case P_BAJAR: respuestaBajar(p); break;
    case P_ASIGNAR:
      if (p->codigo == 200) {
        const Perfil* pf = perfilMut(opEnCurso.perfil);
        ponerHuella(opEnCurso.huella, opEnCurso.perfil, pf ? pf->nombre : opEnCurso.perfil);
        enLector[opEnCurso.huella] = "";  // está en este lector y falta subirla
        reintentoNum.erase(opEnCurso.huella);
        guardarEnLector();
        avisar(PEv::ASIGNADA, opEnCurso.huella);
      } else {
        String m = motivo(p->codigo, p->respuesta);
        if (p->codigo == 404 && !sinRutas(p->codigo, p->respuesta)) m = "Esa persona ya no está en el panel";
        if (p->codigo == 409) m = "Ese número de huella ya es de otra persona en el panel";
        avisar(PEv::ASIGNAR_ERROR, opEnCurso.huella, m);
      }
      break;
    case P_SOLTAR:
      if (p->codigo == 200) {
        soltarHuella(opEnCurso.huella);
        guardarPerfiles();
        enLector.erase(opEnCurso.huella);  // la interfaz la borra del lector al recibir SOLTADA
        plantillasPanel.erase(opEnCurso.huella);
        guardarEnLector();
        avisar(PEv::SOLTADA, opEnCurso.huella);
      } else {
        avisar(PEv::SOLTAR_ERROR, opEnCurso.huella, motivo(p->codigo, p->respuesta));
      }
      break;
  }
}

// ---------------------------------------------------------------- pedidos --
static bool mandar(uint8_t tipo, uint32_t ref, const char* metodo, const String& ruta, const String& cuerpo,
                   const String& archivo) {
  PedidoHttp* p = new PedidoHttp();
  p->tipo = tipo;
  p->ref = ref;
  p->metodo = metodo;
  p->url = ajustes().panelUrl + ruta;
  p->llave = ajustes().panelLlave;
  p->cuerpo = cuerpo;
  p->archivo = archivo;
  if (!http_enviar(p)) {
    delete p;
    return false;
  }
  enCurso = true;
  return true;
}

static void mandarOp() {
  opEnCurso = op;
  hayOp = false;
  if (op.tipo == P_PERFILES) {
    mandar(P_PERFILES, 1, "GET", "/api/reloj/perfiles", String());
  } else if (op.tipo == P_ASIGNAR) {
    JsonDocument d;
    d["huella"] = op.huella;
    d["perfil"] = op.perfil;
    String cuerpo;
    serializeJson(d, cuerpo);
    mandar(P_ASIGNAR, op.huella, "POST", "/api/reloj/huellas", cuerpo);
  } else if (op.tipo == P_SOLTAR) {
    mandar(P_SOLTAR, op.huella, "DELETE", "/api/reloj/huellas/" + String(op.huella), String());
  }
}

static void mandarMarca() {
  const EnCola& c = cola.front();
  JsonDocument d;
  d["id"] = c.id;
  d["huella"] = c.huella;
  if (c.t) d["hora"] = isoUtc(c.t);  // sin hora: el panel pone la suya
  if (c.oficina.length()) d["oficina"] = c.oficina;  // sin oficina: el panel supone "A"
  String cuerpo;
  serializeJson(d, cuerpo);
  idEnviado = c.id;
  mandar(P_MARCA, 0, "POST", "/api/reloj/marcas", cuerpo);
}

static bool tieneLlave() { return ajustes().panelUrl.length() > 0 && ajustes().panelLlave.length() > 0; }
bool panel_listo() { return red_conectada() && tieneLlave(); }

static void pedirOp(uint8_t tipo, uint16_t huella, const String& perfil) {
  if (!panel_listo()) {
    const PEv ev = tipo == P_PERFILES ? PEv::PERFILES_ERROR : tipo == P_ASIGNAR ? PEv::ASIGNAR_ERROR : PEv::SOLTAR_ERROR;
    avisar(ev, huella, "Necesita internet");
    return;
  }
  op = {tipo, huella, perfil};
  hayOp = true;
}

void panel_pedir_perfiles() { pedirOp(P_PERFILES, 0, String()); }

void panel_refrescar_pronto() { refrescoPedido = true; }
void panel_asignar(uint16_t huella, const String& perfil) { pedirOp(P_ASIGNAR, huella, perfil); }
void panel_soltar(uint16_t huella) { pedirOp(P_SOLTAR, huella, String()); }

void panel_tarea() {
  if (PedidoHttp* p = http_recoger()) {
    procesar(p);
    delete p;
    enCurso = false;
  }
  static uint32_t tDia = 0;
  if (millis() - tDia > 1000) {
    tDia = millis();
    ponerHoras();
    asegurarDia();
    if ((hoySucio || colaSucia) && millis() - sucioDesde > MS_GUARDAR_DESPUES) panel_guardar_ya();
  }
  if (enCurso) return;
  if (!panel_listo()) {
    if (hayOp) {
      hayOp = false;
      pedirOp(op.tipo, op.huella, op.perfil);  // contesta «Necesita internet»
    }
    return;
  }
  if (hayOp) return mandarOp();
  if ((int32_t)(millis() - proximoIntento) < 0) return;
  // Primero la lista (trae la hora si no hay NTP), después las marcas, después las fotos
  // Cada 3 min: así un dedo registrado en el otro reloj llega pronto a este
  // (o 15 s después de la anterior si alguien no fue reconocido)
  if (!perfilesAlDia || millis() - ultimosPerfiles > 3 * 60 * 1000UL ||
      (refrescoPedido && millis() - ultimosPerfiles > 15000)) {
    refrescoPedido = false;
    mandar(P_PERFILES, 0, "GET", "/api/reloj/perfiles", String());
    ultimosPerfiles = millis();
    return;
  }
  if (!cola.empty()) return mandarMarca();
  if (numSubir) return mandarSubir();
  if (sincronizarPlantillas()) return;
  if (const Perfil* f = fotoPendiente()) {
    fotoEnviada = f->perfil;
    LittleFS.mkdir("/fotos");
    mandar(P_FOTO, 0, "GET", "/api/reloj/fotos/" + f->perfil, String(), foto_ruta(f->perfil) + ".tmp");
  }
}

void panel_guardar_ya() {
  if (hoySucio) guardarHoy();
  if (colaSucia) guardarCola();
}

bool panel_evento(PanelEv& ev) {
  if (eventos.empty()) return false;
  ev = eventos.front();
  eventos.pop_front();
  return true;
}

// ----------------------------------------------------------------- estado --
bool panel_conectado() { return panel_listo() && estado == Conexion::OK; }

String panel_estado() {
  if (!ajustes().panelUrl.length()) return "Falta la dirección del panel";
  if (!tieneLlave()) return "Falta la llave del panel";
  if (!red_conectada()) return "Sin WiFi";
  switch (estado) {
    case Conexion::OK: return "Panel conectado";
    case Conexion::CONECTANDO: return "Conectando al panel...";
    default: return estadoTexto;
  }
}

String panel_configurar(const String& url, const String& llave, const String& oficina) {
  String u = url, l = llave, o = oficina;
  u.trim();
  l.trim();
  o.trim();
  o.toUpperCase();
  if (o.length() && !oficina_valida(o)) return "La oficina tiene que ser A o B";
  while (u.endsWith("/")) u.remove(u.length() - 1);
#ifdef SIMULADOR
  const bool bien = u.startsWith("https://") || u.startsWith("http://");  // el panel local de prueba va por http
#else
  const bool bien = u.startsWith("https://");
#endif
  if (!bien) return "La dirección tiene que empezar con https://";
  ajustes().panelUrl = u;
  if (l.length()) ajustes().panelLlave = l;
  if (o.length()) ajustes().oficina = o;
  ajustes_guardar();
  estado = Conexion::CONECTANDO;
  espera = 0;
  proximoIntento = millis();
  perfilesAlDia = false;  // se prueba al tiro
  return String();
}

void panel_iniciar() {
  LittleFS.mkdir("/fotos");
  cargarPerfiles();
  cargarHoy();
  cargarCola();
  cargarVersiones();
  cargarEnLector();
  http_iniciar();
  Serial.printf("[panel] %u perfiles en caché, %u marcas en cola, oficina %s, %u huellas en el lector\n",
                (unsigned)perfiles.size(), (unsigned)cola.size(), ajustes().oficina.c_str(), (unsigned)enLector.size());
}
