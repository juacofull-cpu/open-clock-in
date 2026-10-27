// Servidor web del simulador: la misma interfaz que el WebServer de la ESP32, pero
// atiende de verdad en el PC (http://localhost). Una conexión a la vez, sin keep-alive.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "Arduino.h"

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE };
enum HTTPAuthMethod { BASIC_AUTH, DIGEST_AUTH };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };

#define HTTP_UPLOAD_BUFLEN 1436
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)

struct HTTPUpload {
  HTTPUploadStatus status;
  String filename;
  String name;
  String type;
  size_t totalSize;
  size_t currentSize;
  uint8_t buf[HTTP_UPLOAD_BUFLEN];
};

class WebServer {
 public:
  typedef std::function<void(void)> THandlerFunction;

  explicit WebServer(int puerto = 80);
  void begin();
  void handleClient();
  int puerto() const { return puerto_; }  // el que se pudo abrir (80 o el siguiente libre)

  void on(const String& uri, HTTPMethod m, THandlerFunction fn) { on(uri, m, fn, nullptr); }
  void on(const String& uri, HTTPMethod m, THandlerFunction fn, THandlerFunction subida);
  void onNotFound(THandlerFunction fn) { noEncontrado_ = fn; }

  bool authenticate(const char* usuario, const char* clave);
  void requestAuthentication(HTTPAuthMethod modo = BASIC_AUTH, const char* reino = nullptr,
                             const String& msgFalla = String(""));
  String arg(const String& nombre);
  bool hasArg(const String& nombre) { return args_.count(nombre.std()) > 0; }

  void setContentLength(size_t n) { largo_ = n; }
  void sendHeader(const String& nombre, const String& valor, bool primero = false);
  void send(int codigo, const char* tipo = nullptr, const String& contenido = String(""));
  void send(int codigo, const String& tipo, const String& contenido) { send(codigo, tipo.c_str(), contenido); }
  void send_P(int codigo, PGM_P tipo, PGM_P contenido) { send(codigo, tipo, String(contenido)); }
  void sendContent(const String& s);
  void sendContent(const char* s) { sendContent(String(s)); }
  HTTPUpload& upload() { return *subida_; }

 private:
  struct Ruta {
    String uri;
    HTTPMethod metodo;
    THandlerFunction fn, subida;
  };
  void atender(intptr_t cliente);
  void escribir(const char* d, size_t n);
  int puertoPedido_, puerto_ = 0;
  intptr_t escucha_ = -1, cliente_ = -1;
  std::vector<Ruta> rutas_;
  THandlerFunction noEncontrado_;
  std::map<std::string, String> args_;
  String autorizacion_, cabecerasExtra_;
  size_t largo_ = 0;
  bool enviado_ = false, porPartes_ = false;
  std::unique_ptr<HTTPUpload> subida_;
};
