// Cliente HTTP del simulador (en la placa es src/http.cpp), con WinHTTP en un hilo
// aparte, igual que la tarea de la placa. Acepta http:// para probar contra un panel
// local (en la placa solo https). Con https Windows valida el certificado solo.
// sim_internet = false imita que se cortó internet (WiFi conectado pero sin salida).
#include <windows.h>
#include <winhttp.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include "LittleFS.h"
#include "http.h"
#include "sim.h"

bool sim_internet = true;

static std::mutex mtx;
static std::condition_variable cv;
static PedidoHttp* ida = nullptr;
static std::deque<PedidoHttp*> vuelta;

static std::wstring ancho(const String& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  return w;
}

static void hacer(HINTERNET ses, PedidoHttp* p) {
  p->codigo = -1;
  const std::wstring url = ancho(p->url);
  URL_COMPONENTS u = {};
  u.dwStructSize = sizeof(u);
  wchar_t host[256], ruta[2048];
  u.lpszHostName = host;
  u.dwHostNameLength = 256;
  u.lpszUrlPath = ruta;
  u.dwUrlPathLength = 2048;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u)) return;
  HINTERNET con = WinHttpConnect(ses, host, u.nPort, 0);
  if (!con) return;
  const DWORD seguro = u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req = WinHttpOpenRequest(con, ancho(p->metodo).c_str(), ruta, nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, seguro);
  if (req) {
    std::wstring cab = ancho("Authorization: Bearer " + p->llave + "\r\n");
    if (p->cuerpo.length()) cab += L"Content-Type: application/json\r\n";
    WinHttpAddRequestHeaders(req, cab.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    const bool enviado = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (void*)p->cuerpo.c_str(),
                                            p->cuerpo.length(), p->cuerpo.length(), 0) &&
                         WinHttpReceiveResponse(req, nullptr);
    if (enviado) {
      DWORD cod = 0, tam = sizeof(cod);
      WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &cod, &tam, nullptr);
      std::string cuerpo;
      for (;;) {
        DWORD hay = 0;
        if (!WinHttpQueryDataAvailable(req, &hay) || !hay) break;
        std::vector<char> b(hay);
        DWORD leido = 0;
        if (!WinHttpReadData(req, b.data(), hay, &leido) || !leido) break;
        cuerpo.append(b.data(), leido);
      }
      p->codigo = (int)cod;
      if (cod == 200 && p->archivo.length()) {
        File f = LittleFS.open(p->archivo, "w");
        if (!f || f.write((const uint8_t*)cuerpo.data(), cuerpo.size()) != cuerpo.size()) p->codigo = -3;
        if (f) f.close();
      } else {
        p->respuesta = String(cuerpo);
      }
    }
    WinHttpCloseHandle(req);
  }
  WinHttpCloseHandle(con);
}

static void hilo() {
  HINTERNET ses = WinHttpOpen(L"reloj-control-sim", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
  WinHttpSetTimeouts(ses, 10000, 10000, 10000, 10000);
  for (;;) {
    PedidoHttp* p;
    {
      std::unique_lock<std::mutex> l(mtx);
      cv.wait(l, [] { return ida != nullptr; });
      p = ida;
    }
    // Sin internet no hay respuesta (sin WiFi panel.cpp ni pide)
    if (!sim_internet) {
      Sleep(300);
      p->codigo = -1;
    } else {
      hacer(ses, p);
    }
    if (p->codigo <= 0) Serial.printf("[http] %s %s: sin respuesta (%d)\n", p->metodo.c_str(), p->url.c_str(), p->codigo);
    std::lock_guard<std::mutex> l(mtx);
    ida = nullptr;
    vuelta.push_back(p);
  }
}

void http_iniciar() { std::thread(hilo).detach(); }

bool http_enviar(PedidoHttp* p) {
  std::lock_guard<std::mutex> l(mtx);
  if (ida) return false;
  ida = p;
  cv.notify_one();
  return true;
}

PedidoHttp* http_recoger() {
  std::lock_guard<std::mutex> l(mtx);
  if (vuelta.empty()) return nullptr;
  PedidoHttp* p = vuelta.front();
  vuelta.pop_front();
  return p;
}
