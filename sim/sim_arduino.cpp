// Arduino, hora, archivos, NVS, WiFi y actualización imitados en el PC.
#include <windows.h>
#include <random>
#include <filesystem>
#include <fstream>
#include "ESPmDNS.h"
#include "LittleFS.h"
#include "Preferences.h"
#include "Update.h"
#include "WiFi.h"
#include "sim.h"

#undef time
namespace fs = std::filesystem;

SerialSim Serial;
EspSim ESP;
LittleFSSim LittleFS;
MDNSSim MDNS;
UpdateSim Update;
WiFiSim WiFi;
bool sim_boot_apretado = false;
int sim_puerto_panel = 0;

std::string sim_ruta(const std::string& rel) {
  static std::string base;
  if (base.empty()) {
    const char* e = getenv("RELOJ_SIM_DATOS");
    base = e && *e ? e : ".sim";
    fs::create_directories(base);
  }
  return base + "/" + rel;
}

// ------------------------------------------------------------- núcleo --
static const ULONGLONG inicio = GetTickCount64();
uint32_t millis() { return (uint32_t)(GetTickCount64() - inicio); }
void delay(uint32_t ms) { Sleep(ms); }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) { return pin == 0 && sim_boot_apretado ? LOW : HIGH; }

static FILE* registro() {
  static FILE* f = fopen(sim_ruta("registro.txt").c_str(), "ab");
  return f;
}
size_t SerialSim::write(uint8_t c) { return write(&c, 1); }
size_t SerialSim::write(const uint8_t* b, size_t n) {
  if (FILE* f = registro()) {
    fwrite(b, 1, n, f);
    fflush(f);
  }
  return n;
}

void EspSim::restart() {
  Serial.println("[sim] reinicio");
  char exe[MAX_PATH];
  GetModuleFileNameA(NULL, exe, MAX_PATH);
  STARTUPINFOA si = {sizeof(si)};
  PROCESS_INFORMATION pi;
  if (CreateProcessA(exe, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  ExitProcess(0);
}

// ---------------------------------------------------------------- hora --
// La placa arranca en 1970 (sin hora) hasta que el WiFi o el administrador la ponen.
// RELOJ_SIM_CON_HORA=1 hace partir con la hora del PC.
static long long desfase() {
  static long long d = getenv("RELOJ_SIM_CON_HORA") ? 0 : 86400 - (long long)::time(nullptr);
  return d;
}
static long long ajuste = 0;
static bool ajustada = false;

time_t sim_time(time_t* t) {
  const time_t v = (time_t)(::time(nullptr) + (ajustada ? ajuste : desfase()));
  if (t) *t = v;
  return v;
}

int sim_settimeofday(const struct timeval* tv, const void*) {
  ajuste = (long long)tv->tv_sec - (long long)::time(nullptr);
  ajustada = true;
  Serial.printf("[sim] hora puesta a mano: %ld\n", (long)tv->tv_sec);
  return 0;
}

// RELOJ_SIM_SIN_NTP=1: la hora por internet no responde (el reloj la toma del panel)
void configTzTime(const char*, const char*, const char*, const char*) {
  if (getenv("RELOJ_SIM_SIN_NTP")) {
    Serial.println("[sim] la hora por internet no responde (RELOJ_SIM_SIN_NTP)");
    return;
  }
  ajuste = 0;
  ajustada = true;
  Serial.println("[sim] hora tomada de internet (la del PC)");
}

uint32_t esp_random() {
  static std::mt19937 g{std::random_device{}()};
  return (uint32_t)g();
}

// ------------------------------------------------------------ LittleFS --
struct ArchivoSim {
  FILE* f = nullptr;
  size_t tam = 0;
  bool lectura = false;
};

static std::string enFlash(const char* ruta) { return sim_ruta("flash") + ruta; }

File::operator bool() const { return a_ && a_->f; }
void File::close() {
  if (a_ && a_->f) fclose(a_->f);
  if (a_) a_->f = nullptr;
}
size_t File::size() const { return a_ ? a_->tam : 0; }
size_t File::write(uint8_t c) { return write(&c, 1); }
size_t File::write(const uint8_t* b, size_t n) {
  if (!*this || a_->lectura) return 0;
  const size_t k = fwrite(b, 1, n, a_->f);
  a_->tam += k;
  return k;
}
int File::available() {
  if (!*this || !a_->lectura) return 0;
  const long p = ftell(a_->f);
  return p < 0 ? 0 : (int)(a_->tam - (size_t)p);
}
int File::read() { return *this && a_->lectura ? fgetc(a_->f) : -1; }
int File::peek() {
  if (!*this || !a_->lectura) return -1;
  const int c = fgetc(a_->f);
  if (c >= 0) ungetc(c, a_->f);
  return c;
}

bool LittleFSSim::begin(bool) {
  std::error_code ec;
  fs::create_directories(sim_ruta("flash"), ec);
  return !ec;
}

File LittleFSSim::open(const char* ruta, const char* modo) {
  const std::string r = enFlash(ruta);
  const char* m = modo[0] == 'w' ? "wb" : modo[0] == 'a' ? "ab" : "rb";
  FILE* f = fopen(r.c_str(), m);
  if (!f) return File();
  auto a = std::make_shared<ArchivoSim>();
  a->f = f;
  a->lectura = modo[0] == 'r';
  std::error_code ec;
  a->tam = modo[0] == 'w' ? 0 : (size_t)fs::file_size(r, ec);
  return File(a);
}

bool LittleFSSim::exists(const char* ruta) { return fs::exists(enFlash(ruta)); }
bool LittleFSSim::remove(const char* ruta) { return ::remove(enFlash(ruta).c_str()) == 0; }
bool LittleFSSim::rename(const char* de, const char* a) {
  return MoveFileExA(enFlash(de).c_str(), enFlash(a).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
bool LittleFSSim::mkdir(const char* ruta) {
  std::error_code ec;
  fs::create_directories(enFlash(ruta), ec);
  return !ec;
}
size_t LittleFSSim::totalBytes() { return 0x9E0000; }  // la partición de datos de la placa
size_t LittleFSSim::usedBytes() {
  size_t n = 0;
  std::error_code ec;
  for (auto& e : fs::recursive_directory_iterator(sim_ruta("flash"), ec))
    if (e.is_regular_file()) n += (size_t)e.file_size();
  return n;
}

// ---------------------------------------------------------- Preferences --
static std::string escapar(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '\\') o += "\\\\";
    else if (c == '\n') o += "\\n";
    else o += c;
  }
  return o;
}
static std::string desescapar(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      o += s[i + 1] == 'n' ? '\n' : s[i + 1];
      i++;
    } else {
      o += s[i];
    }
  }
  return o;
}

bool Preferences::begin(const char* nombre, bool) {
  ruta_ = sim_ruta(std::string("nvs-") + nombre + ".txt");
  v_.clear();
  std::ifstream f(ruta_, std::ios::binary);
  std::string l;
  while (std::getline(f, l)) {
    const size_t p = l.find('=');
    if (p != std::string::npos) v_[l.substr(0, p)] = desescapar(l.substr(p + 1));
  }
  return true;
}
void Preferences::guardar() {
  std::ofstream f(ruta_, std::ios::binary | std::ios::trunc);
  for (auto& kv : v_) f << kv.first << "=" << escapar(kv.second) << "\n";
}
String Preferences::getString(const char* k, const String& def) {
  auto it = v_.find(k);
  return it == v_.end() ? def : String(it->second);
}
size_t Preferences::putString(const char* k, const String& v) {
  v_[k] = v.std();
  guardar();
  return v.length();
}
bool Preferences::getBool(const char* k, bool def) {
  auto it = v_.find(k);
  return it == v_.end() ? def : it->second == "1";
}
size_t Preferences::putBool(const char* k, bool v) {
  v_[k] = v ? "1" : "0";
  guardar();
  return 1;
}
uint8_t Preferences::getUChar(const char* k, uint8_t def) {
  auto it = v_.find(k);
  return it == v_.end() ? def : (uint8_t)atoi(it->second.c_str());
}
size_t Preferences::putUChar(const char* k, uint8_t v) {
  v_[k] = std::to_string(v);
  guardar();
  return 1;
}

// --------------------------------------------------------------- Update --
bool UpdateSim::begin(size_t) {
  f_ = fopen(sim_ruta("firmware-subido.bin").c_str(), "wb");
  total_ = 0;
  ok_ = f_ != nullptr;
  return ok_;
}
size_t UpdateSim::write(uint8_t* d, size_t n) {
  if (!ok_) return 0;
  if (total_ == 0 && n && d[0] != 0xE9) {
    Serial.println("[sim] el archivo no es un programa de ESP32 (Wrong Magic Byte)");
    ok_ = false;
    return 0;
  }
  fwrite(d, 1, n, f_);
  total_ += n;
  return n;
}
bool UpdateSim::end(bool) {
  if (f_) fclose(f_);
  f_ = nullptr;
  if (ok_ && total_ > 0) Serial.printf("[sim] programa nuevo recibido: %u bytes\n", (unsigned)total_);
  return ok_ && total_ > 0;
}
void UpdateSim::abort() {
  if (f_) fclose(f_);
  f_ = nullptr;
  ok_ = false;
}

// ----------------------------------------------------------------- WiFi --
struct RedSim {
  const char* ssid;
  const char* clave;
  int rssi;
};
static const RedSim REDES[] = {
    {"Oficina", "oficina123", -52},
    {"Oficina-Invitados", "", -68},
    {"Vecino 2.4G", "nolasabes", -82},
};
static const int N_REDES = sizeof(REDES) / sizeof(REDES[0]);

void WiFiSim::begin(const char* ssid, const char* clave) {
  ssid_ = ssid;
  clave_ = clave ? clave : "";
  intentando_ = true;
  conectado_ = false;
  desde_ = millis();
}
void WiFiSim::disconnect(bool) {
  intentando_ = conectado_ = false;
}
int WiFiSim::status() {
  if (intentando_ && !conectado_ && millis() - desde_ > 2500) {
    for (const RedSim& r : REDES)
      if (ssid_ == r.ssid && clave_ == r.clave) {
        conectado_ = true;
        Serial.printf("[sim] WiFi conectado a %s\n", r.ssid);
      }
  }
  return conectado_ ? WL_CONNECTED : WL_DISCONNECTED;
}
String WiFiSim::SSID() { return conectado_ ? ssid_ : String(); }
int WiFiSim::RSSI() {
  for (const RedSim& r : REDES)
    if (conectado_ && ssid_ == r.ssid) return r.rssi;
  return 0;
}
String WiFiSim::SSID(int i) { return i >= 0 && i < N_REDES && buscando_ == 2 ? String(REDES[i].ssid) : String(); }
int WiFiSim::RSSI(int i) { return i >= 0 && i < N_REDES && buscando_ == 2 ? REDES[i].rssi : 0; }
IPAddress WiFiSim::localIP() {
  if (!conectado_) return IPAddress(String("0.0.0.0"));
  return IPAddress(sim_puerto_panel == 80 || !sim_puerto_panel ? String("localhost")
                                                               : "localhost:" + String(sim_puerto_panel));
}
int WiFiSim::scanNetworks(bool) {
  buscando_ = 1;
  busquedaDesde_ = millis();
  return WIFI_SCAN_RUNNING;
}
int WiFiSim::scanComplete() {
  if (buscando_ == 1 && millis() - busquedaDesde_ > 1500) buscando_ = 2;
  return buscando_ == 2 ? N_REDES : buscando_ == 1 ? WIFI_SCAN_RUNNING : WIFI_SCAN_FAILED;
}
void WiFiSim::scanDelete() { buscando_ = 0; }
