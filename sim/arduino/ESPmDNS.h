// En el PC no hay reloj.local: el panel se abre en http://localhost
#pragma once
#include "Arduino.h"

class MDNSSim {
 public:
  bool begin(const char*) { return true; }
  void addService(const char*, const char*, uint16_t) {}
};
extern MDNSSim MDNS;
