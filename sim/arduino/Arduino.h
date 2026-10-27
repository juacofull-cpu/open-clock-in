// Imitación mínima del núcleo de Arduino para compilar el reloj en el PC.
// Solo lo que usan ui.cpp, datos.cpp y red.cpp; se comporta como el de la ESP32.
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include "forzado.h"

// LVGL trae los encabezados de Windows: nada de INPUT/OUTPUT ni boolean/byte, que chocan.
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 0x05
#define PROGMEM
#define PGM_P const char*

template <typename T, typename A, typename B>
static inline T constrain(T x, A a, B b) {
  return x < (T)a ? (T)a : x > (T)b ? (T)b : x;
}

uint32_t millis();
void delay(uint32_t ms);
void pinMode(uint8_t pin, uint8_t modo);
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t valor);

class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned int v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(long long v) : s_(std::to_string(v)) {}
  String(unsigned long long v) : s_(std::to_string(v)) {}
  String(double v, unsigned int decimales = 2) {
    char b[64];
    snprintf(b, sizeof(b), "%.*f", (int)decimales, v);
    s_ = b;
  }
  String(float v, unsigned int decimales = 2) : String((double)v, decimales) {}

  const char* c_str() const { return s_.c_str(); }
  unsigned int length() const { return (unsigned int)s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  void reserve(unsigned int n) { s_.reserve(n); }
  const char* begin() const { return s_.data(); }
  const char* end() const { return s_.data() + s_.size(); }
  char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }

  String substring(unsigned int desde) const { return substring(desde, length()); }
  String substring(unsigned int desde, unsigned int hasta) const {
    if (desde > hasta) std::swap(desde, hasta);
    if (desde >= length()) return String();
    if (hasta > length()) hasta = length();
    return String(s_.substr(desde, hasta - desde));
  }
  int indexOf(char c, unsigned int desde = 0) const {
    const size_t p = s_.find(c, desde);
    return p == std::string::npos ? -1 : (int)p;
  }
  int indexOf(const String& t, unsigned int desde = 0) const {
    const size_t p = s_.find(t.s_, desde);
    return p == std::string::npos ? -1 : (int)p;
  }
  int lastIndexOf(char c) const {
    const size_t p = s_.rfind(c);
    return p == std::string::npos ? -1 : (int)p;
  }
  long toInt() const { return atol(s_.c_str()); }
  float toFloat() const { return (float)atof(s_.c_str()); }
  bool startsWith(const String& t) const { return s_.compare(0, t.s_.size(), t.s_) == 0; }
  bool endsWith(const String& t) const {
    return s_.size() >= t.s_.size() && s_.compare(s_.size() - t.s_.size(), t.s_.size(), t.s_) == 0;
  }
  bool equals(const String& t) const { return s_ == t.s_; }

  void trim() {
    const char* blancos = " \t\r\n\f\v";
    const size_t a = s_.find_first_not_of(blancos);
    if (a == std::string::npos) {
      s_.clear();
      return;
    }
    s_ = s_.substr(a, s_.find_last_not_of(blancos) - a + 1);
  }
  void toLowerCase() {
    for (char& c : s_)
      if (c >= 'A' && c <= 'Z') c += 32;
  }
  void toUpperCase() {
    for (char& c : s_)
      if (c >= 'a' && c <= 'z') c -= 32;
  }
  void replace(const String& buscar, const String& poner) {
    if (buscar.s_.empty()) return;
    size_t p = 0;
    while ((p = s_.find(buscar.s_, p)) != std::string::npos) {
      s_.replace(p, buscar.s_.size(), poner.s_);
      p += poner.s_.size();
    }
  }
  void remove(unsigned int i) {
    if (i < s_.size()) s_.erase(i);
  }
  void remove(unsigned int i, unsigned int n) {
    if (i < s_.size()) s_.erase(i, n);
  }
  bool concat(const char* t) {
    s_ += t ? t : "";
    return true;
  }
  bool concat(const String& t) {
    s_ += t.s_;
    return true;
  }
  bool concat(char c) {
    s_ += c;
    return true;
  }

  String& operator+=(const String& t) {
    s_ += t.s_;
    return *this;
  }
  String& operator+=(const char* t) {
    s_ += t ? t : "";
    return *this;
  }
  String& operator+=(char c) {
    s_ += c;
    return *this;
  }

  const std::string& std() const { return s_; }

 private:
  std::string s_;
};

inline String operator+(const String& a, const String& b) {
  String r(a);
  r += b;
  return r;
}
inline String operator+(const String& a, const char* b) {
  String r(a);
  r += b;
  return r;
}
inline String operator+(const char* a, const String& b) {
  String r(a);
  r += b;
  return r;
}
inline String operator+(const String& a, char b) {
  String r(a);
  r += b;
  return r;
}
inline bool operator==(const String& a, const String& b) { return a.std() == b.std(); }
inline bool operator==(const String& a, const char* b) { return a.std() == (b ? b : ""); }
inline bool operator!=(const String& a, const String& b) { return !(a == b); }
inline bool operator!=(const String& a, const char* b) { return !(a == b); }
inline bool operator<(const String& a, const String& b) { return a.std() < b.std(); }
inline bool operator>(const String& a, const String& b) { return a.std() > b.std(); }
inline bool operator<=(const String& a, const String& b) { return a.std() <= b.std(); }
inline bool operator>=(const String& a, const String& b) { return a.std() >= b.std(); }

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* b, size_t n) {
    size_t k = 0;
    while (n--) k += write(*b++);
    return k;
  }
  size_t write(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t print(const char* s) { return write(s); }
  size_t print(const String& s) { return write(s.c_str()); }
  size_t println(const char* s = "") { return print(s) + print("\r\n"); }
  size_t println(const String& s) { return println(s.c_str()); }
  size_t printf(const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((size_t)n < sizeof(b)) return write((const uint8_t*)b, n);
    std::string grande(n + 1, '\0');
    va_start(ap, fmt);
    vsnprintf(&grande[0], grande.size(), fmt, ap);
    va_end(ap);
    return write((const uint8_t*)grande.data(), n);
  }
};

class Printable {
 public:
  virtual ~Printable() {}
  virtual size_t printTo(Print& p) const = 0;
};

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  size_t readBytes(char* buf, size_t n) {
    size_t k = 0;
    while (k < n) {
      const int c = read();
      if (c < 0) break;
      buf[k++] = (char)c;
    }
    return k;
  }
  String readStringUntil(char fin) {
    std::string r;
    for (;;) {
      const int c = read();
      if (c < 0 || c == fin) break;
      r += (char)c;
    }
    return String(r);
  }
};

// Serial escribe al registro del simulador (.sim/registro.txt)
class SerialSim : public Stream {
 public:
  void begin(unsigned long) {}
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* b, size_t n) override;
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};
extern SerialSim Serial;

class EspSim {
 public:
  void restart();  // vuelve a abrir el simulador, como si la placa se reiniciara
};
extern EspSim ESP;
