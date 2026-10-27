// WebServer de la ESP32 imitado con sockets de Windows. Solo escucha en el propio PC
// (127.0.0.1), así Windows no pide permiso de firewall.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "WebServer.h"
#include "sim.h"

static const char* textoEstado(int c) {
  switch (c) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    default: return "Error";
  }
}

static std::string decodificar(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) {
      o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else {
      o += s[i];
    }
  }
  return o;
}

static void leerArgs(const std::string& q, std::map<std::string, String>& args) {
  size_t p = 0;
  while (p <= q.size()) {
    size_t f = q.find('&', p);
    if (f == std::string::npos) f = q.size();
    const std::string par = q.substr(p, f - p);
    if (!par.empty()) {
      const size_t e = par.find('=');
      args[decodificar(par.substr(0, e))] = String(e == std::string::npos ? "" : decodificar(par.substr(e + 1)));
    }
    p = f + 1;
  }
}

static std::string base64(const std::string& s) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  unsigned v = 0;
  int b = -6;
  for (unsigned char c : s) {
    v = (v << 8) + c;
    b += 8;
    while (b >= 0) {
      o += t[(v >> b) & 0x3F];
      b -= 6;
    }
  }
  if (b > -6) o += t[((v << 8) >> (b + 8)) & 0x3F];
  while (o.size() % 4) o += '=';
  return o;
}

static std::string cabecera(const std::string& cab, const char* nombre) {
  // Busca "nombre:" sin importar mayúsculas
  std::string bajo = cab, n = nombre;
  for (char& c : bajo) c = (char)tolower((unsigned char)c);
  for (char& c : n) c = (char)tolower((unsigned char)c);
  size_t p = bajo.find("\r\n" + n + ":");
  if (p == std::string::npos) return "";
  p += n.size() + 3;
  size_t f = cab.find("\r\n", p);
  std::string v = cab.substr(p, f - p);
  while (!v.empty() && v[0] == ' ') v.erase(0, 1);
  return v;
}

WebServer::WebServer(int puerto) : puertoPedido_(puerto) {}

void WebServer::begin() {
  static bool wsa = false;
  if (!wsa) {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    wsa = true;
  }
  for (int i = 0; i < 20; i++) {
    const int p = puertoPedido_ == 80 ? (i == 0 ? 80 : 8079 + i) : puertoPedido_ + i;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL excl = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)p);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (sockaddr*)&a, sizeof(a)) == 0 && listen(s, 16) == 0) {
      u_long noBloquear = 1;
      ioctlsocket(s, FIONBIO, &noBloquear);
      escucha_ = (intptr_t)s;
      puerto_ = p;
      if (puertoPedido_ == 80) sim_puerto_panel = p;
      Serial.printf("[sim] servidor web en http://localhost:%d\n", p);
      return;
    }
    closesocket(s);
  }
  Serial.println("[sim] no se pudo abrir el servidor web");
}

void WebServer::handleClient() {
  if (escucha_ < 0) return;
  for (int i = 0; i < 6; i++) {
    SOCKET c = accept((SOCKET)escucha_, nullptr, nullptr);
    if (c == INVALID_SOCKET) return;
    u_long bloquear = 0;
    ioctlsocket(c, FIONBIO, &bloquear);
    DWORD espera = 5000;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&espera, sizeof(espera));
    atender((intptr_t)c);
    shutdown(c, SD_SEND);
    closesocket(c);
  }
}

void WebServer::on(const String& uri, HTTPMethod m, THandlerFunction fn, THandlerFunction subida) {
  rutas_.push_back({uri, m, fn, subida});
}

void WebServer::escribir(const char* d, size_t n) {
  while (n > 0) {
    const int k = ::send((SOCKET)cliente_, d, (int)std::min<size_t>(n, 65536), 0);
    if (k <= 0) return;
    d += k;
    n -= (size_t)k;
  }
}

void WebServer::atender(intptr_t cli) {
  cliente_ = cli;
  args_.clear();
  autorizacion_ = cabecerasExtra_ = String();
  largo_ = 0;
  enviado_ = porPartes_ = false;
  subida_.reset(new HTTPUpload());

  // Cabeceras
  std::string pedido;
  char b[8192];
  size_t finCab;
  for (;;) {
    finCab = pedido.find("\r\n\r\n");
    if (finCab != std::string::npos || pedido.size() > 65536) break;
    const int k = recv((SOCKET)cli, b, sizeof(b), 0);
    if (k <= 0) return;
    pedido.append(b, k);
  }
  if (finCab == std::string::npos) return;
  const std::string cab = pedido.substr(0, finCab + 2);
  std::string cuerpo = pedido.substr(finCab + 4);
  const size_t largo = (size_t)atoll(cabecera(cab, "Content-Length").c_str());
  while (cuerpo.size() < largo && cuerpo.size() < 16u * 1024 * 1024) {
    const int k = recv((SOCKET)cli, b, sizeof(b), 0);
    if (k <= 0) break;
    cuerpo.append(b, k);
  }

  const size_t e1 = cab.find(' '), e2 = cab.find(' ', e1 + 1);
  const std::string metodo = cab.substr(0, e1), url = cab.substr(e1 + 1, e2 - e1 - 1);
  const size_t q = url.find('?');
  const std::string ruta = decodificar(url.substr(0, q));
  if (q != std::string::npos) leerArgs(url.substr(q + 1), args_);
  const std::string tipo = cabecera(cab, "Content-Type");
  if (tipo.find("application/x-www-form-urlencoded") != std::string::npos) leerArgs(cuerpo, args_);
  autorizacion_ = String(cabecera(cab, "Authorization"));
  const HTTPMethod m = metodo == "GET" ? HTTP_GET : metodo == "POST" ? HTTP_POST : HTTP_ANY;

  Ruta* r = nullptr;
  for (Ruta& x : rutas_)
    if (x.uri == ruta.c_str() && (x.metodo == HTTP_ANY || x.metodo == m)) {
      r = &x;
      break;
    }
  if (!r) {
    if (noEncontrado_) noEncontrado_();
    else send(404, "text/plain", "No existe");
    return;
  }

  // Archivo subido (multipart/form-data): se entrega de a pedazos, como en la placa
  const size_t pb = tipo.find("boundary=");
  if (r->subida && tipo.find("multipart/form-data") != std::string::npos && pb != std::string::npos) {
    std::string limite = "--" + tipo.substr(pb + 9);
    if (limite.size() > 2 && limite[2] == '"') limite = "--" + limite.substr(3, limite.size() - 4);
    size_t p = cuerpo.find(limite);
    while (p != std::string::npos) {
      p += limite.size();
      if (cuerpo.compare(p, 2, "--") == 0) break;
      const size_t finParte = cuerpo.find("\r\n\r\n", p);
      if (finParte == std::string::npos) break;
      const std::string cabParte = cuerpo.substr(p, finParte - p + 2);
      const size_t inicio = finParte + 4;
      const size_t sig = cuerpo.find("\r\n" + limite, inicio);
      if (sig == std::string::npos) break;
      const size_t pf = cabParte.find("filename=\"");
      if (pf != std::string::npos) {
        HTTPUpload& u = *subida_;
        u.filename = String(cabParte.substr(pf + 10, cabParte.find('"', pf + 10) - pf - 10));
        u.totalSize = 0;
        u.status = UPLOAD_FILE_START;
        r->subida();
        for (size_t k = inicio; k < sig; k += HTTP_UPLOAD_BUFLEN) {
          u.currentSize = std::min<size_t>(HTTP_UPLOAD_BUFLEN, sig - k);
          memcpy(u.buf, cuerpo.data() + k, u.currentSize);
          u.status = UPLOAD_FILE_WRITE;
          r->subida();
          u.totalSize += u.currentSize;
        }
        u.currentSize = 0;
        u.status = UPLOAD_FILE_END;
        r->subida();
      }
      p = sig + 2;
    }
  }
  r->fn();
}

bool WebServer::authenticate(const char* usuario, const char* clave) {
  return autorizacion_ == ("Basic " + base64(std::string(usuario) + ":" + clave)).c_str();
}

void WebServer::requestAuthentication(HTTPAuthMethod, const char* reino, const String& msgFalla) {
  sendHeader("WWW-Authenticate", String("Basic realm=\"") + (reino ? reino : "Login Required") + "\"");
  send(401, "text/html", msgFalla);
}

String WebServer::arg(const String& nombre) {
  auto it = args_.find(nombre.std());
  return it == args_.end() ? String() : it->second;
}

void WebServer::sendHeader(const String& nombre, const String& valor, bool) {
  cabecerasExtra_ += nombre + ": " + valor + "\r\n";
}

void WebServer::send(int codigo, const char* tipo, const String& contenido) {
  if (enviado_) return;
  enviado_ = true;
  String r = "HTTP/1.1 " + String(codigo) + " " + textoEstado(codigo) + "\r\n";
  if (tipo) r += String("Content-Type: ") + tipo + "\r\n";
  r += cabecerasExtra_;
  porPartes_ = largo_ == CONTENT_LENGTH_UNKNOWN;
  if (!porPartes_) r += "Content-Length: " + String(contenido.length()) + "\r\n";
  r += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
  r += contenido;
  escribir(r.c_str(), r.length());
  largo_ = 0;
  cabecerasExtra_ = String();
}

void WebServer::sendContent(const String& s) {
  if (porPartes_ && s.length()) escribir(s.c_str(), s.length());
}
