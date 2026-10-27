// LittleFS del simulador: los archivos de la "memoria flash" quedan en
// .sim/flash\ (mismas rutas que en la placa).
#pragma once
#include <memory>
#include "Arduino.h"

struct ArchivoSim;

class File : public Stream {
 public:
  File() {}
  explicit File(std::shared_ptr<ArchivoSim> a) : a_(a) {}
  explicit operator bool() const;
  void close();
  size_t size() const;
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* b, size_t n) override;
  int available() override;
  int read() override;
  int peek() override;

 private:
  std::shared_ptr<ArchivoSim> a_;
};

class LittleFSSim {
 public:
  bool begin(bool formatearSiFalla = false);
  File open(const char* ruta, const char* modo = "r");
  File open(const String& ruta, const char* modo = "r") { return open(ruta.c_str(), modo); }
  bool exists(const char* ruta);
  bool exists(const String& ruta) { return exists(ruta.c_str()); }
  bool remove(const char* ruta);
  bool rename(const char* de, const char* a);
  bool mkdir(const char* ruta);
  size_t totalBytes();
  size_t usedBytes();
};
extern LittleFSSim LittleFS;
