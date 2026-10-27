// Temas de temporada. Formato de cada uno (ejemplos en temas/*.json):
//   {"nombre": "Navidad", "desde": "12-01", "hasta": "12-25", "adorno": "nieve", "luces": true,
//    "entrada": ["¡Jo jo jo, {nombre}!", ...], "salida": ["Felices fiestas, {nombre}", ...],
//    "especiales": [{"dia": "12-24", "desde": "15:00", "hasta": "23:59", "tipo": "salida", "cada": 1,
//                    "textos": ["¡Corre, {nombre}, que llega el Viejito Pascuero!"], "tono": [[392, 200], ...]}],
//    "sorpresa": {"texto": "¡Jo jo jo!", "tono": [...]},
//    "tonos": {"ok": [[659, 150], [0, 40], ...], "salida": [...], "error": [...], "aviso": [...], "tecla": [...]}}
// Adornos: nieve, confeti, fuegos, almas. En «especiales» todo es opcional: sin «dia» vale todo el
// tema, sin horas todo el día, «tipo» entrada/salida (sin él, las dos) y «cada»: N = 1 de cada N
// marcas. Vale la primera que calce. Cada nota es [Hz, ms]; 0 Hz = silencio. Lo que falte se
// queda como siempre.
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <algorithm>
#include <mutex>
#include "datos.h"
#include "tema.h"
#include "temas_incluidos.h"

#define RUTA "/tema.json"
#define NOTAS_MAX 64  // ~3 s de melodía: suena en cada marca, tiene que ser corta

struct Especial {
  int dia = 0;                   // mes * 100 + día; 0 = cualquiera
  int desde = 0, hasta = 2359;   // HHMM
  int tipo = 0;                  // 0 = las dos, 1 = entrada, 2 = salida
  uint16_t cada = 1;
  std::vector<String> textos;
  std::vector<Nota> tono;
};

struct Tema {
  String nombre;
  int desde = 0, hasta = 0;  // mes * 100 + día
  Adorno adorno = Adorno::NADA;
  bool luces = false;
  std::vector<String> entrada, salida;
  std::vector<Especial> especiales;
  String sorpresa;
  std::vector<Nota> tonoSorpresa;
  std::vector<Nota> tonos[5];
};

static std::vector<Tema> temas;
static std::vector<Nota> especial;  // T_ESPECIAL: la melodía de la última sorpresa
static std::mutex mx;  // la tarea del sonido lee las melodías mientras la interfaz puede recargar

static int mesDia(const char* s) {
  int m = 0, d = 0;
  if (!s || sscanf(s, "%d-%d", &m, &d) != 2 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
  return m * 100 + d;
}

static int horaMin(const char* s, int siNo) {
  int h = 0, m = 0;
  if (!s || sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return siNo;
  return h * 100 + m;
}

static void leerFrases(JsonArrayConst a, std::vector<String>& v) {
  for (JsonVariantConst f : a) {
    const char* t = f.as<const char*>();
    if (t && *t) v.push_back(t);
  }
}

static void leerNotas(JsonArrayConst a, std::vector<Nota>& v) {
  for (JsonArrayConst n : a) {
    if (v.size() >= NOTAS_MAX) break;
    const int frec = n[0] | 0, ms = n[1] | 0;
    if (ms <= 0 || ms > 2000 || frec < 0 || frec > 8000) continue;
    v.push_back({(uint16_t)frec, (uint16_t)ms});
  }
}

static bool leerTema(JsonDocument& d, const char* origen, Tema& t) {
  t.desde = mesDia(d["desde"]);
  t.hasta = mesDia(d["hasta"]);
  if (!t.desde || !t.hasta) {
    Serial.printf("[tema] %s: faltan las fechas «desde» y «hasta» (MM-DD)\n", origen);
    return false;
  }
  t.nombre = d["nombre"] | "";
  const String adorno = d["adorno"] | "";
  t.adorno = adorno == "nieve" ? Adorno::NIEVE : adorno == "confeti" ? Adorno::CONFETI
           : adorno == "fuegos" ? Adorno::FUEGOS : adorno == "almas" ? Adorno::ALMAS : Adorno::NADA;
  t.luces = d["luces"] | false;
  leerFrases(d["entrada"], t.entrada);
  leerFrases(d["salida"], t.salida);
  for (JsonObjectConst o : d["especiales"].as<JsonArrayConst>()) {
    Especial e;
    e.dia = mesDia(o["dia"]);
    e.desde = horaMin(o["desde"], 0);
    e.hasta = horaMin(o["hasta"], 2359);
    const String tipo = o["tipo"] | "";
    e.tipo = tipo == "entrada" ? 1 : tipo == "salida" ? 2 : 0;
    e.cada = std::max(1, std::min(1000, (int)(o["cada"] | 1)));
    leerFrases(o["textos"], e.textos);
    leerNotas(o["tono"], e.tono);
    if (!e.textos.empty()) t.especiales.push_back(e);
  }
  t.sorpresa = d["sorpresa"]["texto"] | "";
  leerNotas(d["sorpresa"]["tono"], t.tonoSorpresa);
  static const char* const NOMBRES[] = {"ok", "salida", "error", "aviso", "tecla"};
  for (int i = 0; i < 5; i++) leerNotas(d["tonos"][NOMBRES[i]], t.tonos[i]);
  Serial.printf("[tema] %s (%s), del %02d-%02d al %02d-%02d: %u saludos, %u especiales\n", t.nombre.c_str(), origen,
                t.desde / 100, t.desde % 100, t.hasta / 100, t.hasta % 100,
                (unsigned)(t.entrada.size() + t.salida.size()), (unsigned)t.especiales.size());
  return true;
}

void tema_cargar() {
  std::vector<Tema> v;
  JsonDocument d;
  File f = LittleFS.open(RUTA, "r");  // el del panel va primero: puede tapar a uno incluido
  if (f) {
    const DeserializationError e = deserializeJson(d, f);
    f.close();
    Tema t;
    if (e) Serial.printf("[tema] %s no se pudo leer: %s\n", RUTA, e.c_str());
    else if (leerTema(d, RUTA, t)) v.push_back(t);
  }
  for (const char* json : TEMAS_INCLUIDOS) {
    Tema t;
    if (!deserializeJson(d, json) && leerTema(d, "incluido", t)) v.push_back(t);
  }
  std::lock_guard<std::mutex> l(mx);
  temas = v;
}

static bool entre(int hoy, int desde, int hasta) {
  if (desde <= hasta) return hoy >= desde && hoy <= hasta;
  return hoy >= desde || hoy <= hasta;  // cruza el año (ej. 12-26 al 01-02)
}

static const Tema* actual() {
  if (!hora_valida()) return nullptr;
  const time_t ahora = hora_ahora();  // no time(): <mutex> trae <ctime>, que borra el reloj del simulador
  struct tm tm;
  localtime_r(&ahora, &tm);
  const int hoy = (tm.tm_mon + 1) * 100 + tm.tm_mday;
  for (const Tema& t : temas)
    if (entre(hoy, t.desde, t.hasta)) return &t;
  return nullptr;
}

bool tema_activo() { return actual() != nullptr; }

String tema_nombre() {
  const Tema* t = actual();
  return t ? t->nombre : String();
}

Adorno tema_adorno() {
  const Tema* t = actual();
  return t ? t->adorno : Adorno::NADA;
}

bool tema_luces() {
  const Tema* t = actual();
  return t && t->luces;
}

static uint32_t picar(const String& s, uint32_t sal) {  // FNV-1a
  uint32_t h = 2166136261u ^ sal;
  for (size_t i = 0; i < s.length(); i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
  return h;
}

static String rellenar(String s, const String& nombre) {
  const time_t ahora = hora_ahora();
  struct tm tm;
  localtime_r(&ahora, &tm);
  s.replace("{nombre}", nombre);
  s.replace("{año_que_viene}", String(tm.tm_year + 1901));
  s.replace("{año}", String(tm.tm_year + 1900));
  return s;
}

String tema_saludo(bool entrada, const String& nombre, const String& semilla, bool& conTono) {
  conTono = false;
  std::lock_guard<std::mutex> l(mx);
  const Tema* t = actual();
  if (!t) return String();
  const time_t ahora = hora_ahora();
  struct tm tm;
  localtime_r(&ahora, &tm);
  const int hoy = (tm.tm_mon + 1) * 100 + tm.tm_mday, hora = tm.tm_hour * 100 + tm.tm_min;
  bool siempre = false;  // en el simulador: que las de «1 de cada N» salgan para poder verlas
#ifdef SIMULADOR
  siempre = getenv("RELOJ_SIM_SORPRESAS") != nullptr;
#endif
  for (size_t i = 0; i < t->especiales.size(); i++) {
    const Especial& e = t->especiales[i];
    if (e.dia && e.dia != hoy) continue;
    if (hora < e.desde || hora > e.hasta) continue;
    if (e.tipo && (e.tipo == 1) != entrada) continue;
    if (e.cada > 1 && !siempre && picar(semilla, 7919 * (i + 1)) % e.cada) continue;
    if (!e.tono.empty()) {
      especial = e.tono;
      conTono = true;
    }
    return rellenar(e.textos[picar(semilla, 0) % e.textos.size()], nombre);
  }
  const std::vector<String>& v = entrada ? t->entrada : t->salida;
  if (v.empty()) return String();
  return rellenar(v[picar(semilla, 0) % v.size()], nombre);
}

String tema_sorpresa(bool& conTono) {
  conTono = false;
  std::lock_guard<std::mutex> l(mx);
  const Tema* t = actual();
  if (!t || !t->sorpresa.length()) return String();
  if (!t->tonoSorpresa.empty()) {
    especial = t->tonoSorpresa;
    conTono = true;
  }
  return rellenar(t->sorpresa, "");
}

bool tema_melodia(TonoTema tono, std::vector<Nota>& notas) {
  std::lock_guard<std::mutex> l(mx);
  if (tono == TonoTema::T_ESPECIAL) {
    notas = especial;
    return !notas.empty();
  }
  const Tema* t = actual();
  if (!t) return false;
  notas = t->tonos[(int)tono];
  return !notas.empty();
}
