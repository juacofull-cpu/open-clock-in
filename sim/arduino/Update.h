// Actualización sin cable, simulada: revisa que el archivo sea un programa de ESP32
// (primer byte 0xE9, igual que la placa) y lo guarda en .sim/.
#pragma once
#include "Arduino.h"

#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF

class UpdateSim {
 public:
  bool begin(size_t tam);
  size_t write(uint8_t* datos, size_t n);
  bool end(bool sinTam = false);
  void abort();

 private:
  FILE* f_ = nullptr;
  size_t total_ = 0;
  bool ok_ = false;
};
extern UpdateSim Update;
