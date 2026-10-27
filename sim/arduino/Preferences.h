// NVS del simulador: un archivo de texto clave=valor en .sim/.
#pragma once
#include <map>
#include <string>
#include "Arduino.h"

class Preferences {
 public:
  bool begin(const char* nombre, bool soloLectura = false);
  String getString(const char* k, const String& def = String());
  size_t putString(const char* k, const String& v);
  bool getBool(const char* k, bool def = false);
  size_t putBool(const char* k, bool v);
  uint8_t getUChar(const char* k, uint8_t def = 0);
  size_t putUChar(const char* k, uint8_t v);

 private:
  void guardar();
  std::string ruta_;
  std::map<std::string, std::string> v_;
};
