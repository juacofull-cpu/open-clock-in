// WiFi simulado: hay tres redes de mentira. «Oficina» tiene clave (oficina123),
// «Oficina-Invitados» no tiene. Con la clave mala se queda intentando, como la placa.
#pragma once
#include <vector>
#include "Arduino.h"

#define WL_IDLE_STATUS 0
#define WL_NO_SSID_AVAIL 1
#define WL_CONNECTED 3
#define WL_CONNECT_FAILED 4
#define WL_DISCONNECTED 6
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)
#define WIFI_STA 1

class IPAddress {
 public:
  IPAddress() {}
  explicit IPAddress(const String& s) : s_(s) {}
  String toString() const { return s_; }

 private:
  String s_ = "0.0.0.0";
};

class WiFiSim {
 public:
  void mode(int) {}
  void setHostname(const char*) {}
  void setAutoReconnect(bool) {}
  void persistent(bool) {}
  void begin(const char* ssid, const char* clave);
  void disconnect(bool apagar = false);
  int status();
  String SSID();
  String SSID(int i);
  int RSSI();
  int RSSI(int i);
  IPAddress localIP();
  int scanNetworks(bool async = false);
  int scanComplete();
  void scanDelete();

 private:
  String ssid_, clave_;
  bool intentando_ = false, conectado_ = false;
  uint32_t desde_ = 0;
  int buscando_ = 0;  // 0 nada, 1 buscando, 2 listo
  uint32_t busquedaDesde_ = 0;
};
extern WiFiSim WiFi;

void configTzTime(const char* zona, const char* s1, const char* s2 = nullptr, const char* s3 = nullptr);
