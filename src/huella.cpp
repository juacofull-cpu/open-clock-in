// Lector de huella AS608 en su propia tarea: cada orden al lector tarda decenas o
// cientos de milisegundos, y así la pantalla sigue respondiendo. La interfaz le manda
// órdenes por una cola y recibe avisos por otra.
// Las plantillas (sacar/poner para el otro reloj) van por una segunda cola «de fondo»
// que solo se atiende entre órdenes de la interfaz: así no cortan un registro en curso.
#include <Arduino.h>
#include <algorithm>
#include <Adafruit_Fingerprint.h>
#include "config.h"
#include "huella.h"

enum class HCmd : uint8_t { ESCANEAR, PAUSAR, ENROLAR, BORRAR, BORRAR_TODO, SACAR, PONER, QUITAR };
struct Orden {
  HCmd cmd;
  uint16_t id;
  uint8_t* datos;  // PONER: plantilla (la tarea la libera)
  uint16_t largo;
};

static HardwareSerial uart(1);
static Adafruit_Fingerprint fp(&uart);
static QueueHandle_t qOrden, qFondo, qAviso;
static volatile bool conectado = false;
static bool parametros = false;  // ReadSysPara contestó: se conoce el largo de paquete (fp.packet_len)
static volatile uint16_t capacidad = 0;
static bool escaneando = false;

static bool avisar(HEv ev, uint16_t id = 0, uint16_t conf = 0, uint8_t paso = 0, const char* msg = "",
                   uint8_t* datos = nullptr, uint16_t largo = 0) {
  HuellaEv a = {};
  a.ev = ev;
  a.id = id;
  a.confianza = conf;
  a.paso = paso;
  strncpy(a.msg, msg, sizeof(a.msg) - 1);
  a.datos = datos;
  a.largo = largo;
  return xQueueSend(qAviso, &a, pdMS_TO_TICKS(100)) == pdTRUE;
}

static bool hayOrden() { return uxQueueMessagesWaiting(qOrden) > 0; }

static bool conectar() {
  for (int i = 0; i < 3; i++) {
    if (fp.verifyPassword()) {
      parametros = fp.getParameters() == FINGERPRINT_OK;
      if (!parametros || fp.capacity == 0 || fp.capacity > 3000) fp.capacity = 162;
      capacidad = fp.capacity;
      conectado = true;
      Serial.printf("[huella] conectado, capacidad %u, paquetes de %u bytes%s\n", capacidad, fp.packet_len,
                    parametros ? "" : " (sin ReadSysPara)");
      avisar(HEv::CONECTADO, capacidad);
      return true;
    }
    delay(200);
  }
  return false;
}

static void perdida() {
  conectado = false;
  Serial.println("[huella] el lector no responde");
  avisar(HEv::DESCONECTADO);
}

// Espera a que se ponga un dedo. 1 = hay imagen, 0 = se acabó el tiempo o llegó
// otra orden, -1 = el lector no responde.
static int esperarDedo(uint32_t msMax) {
  const uint32_t t0 = millis();
  int fallas = 0;
  while (millis() - t0 < msMax) {
    if (hayOrden()) return 0;
    const uint8_t r = fp.getImage();
    if (r == FINGERPRINT_OK) return 1;
    if (r == FINGERPRINT_PACKETRECIEVEERR) {
      if (++fallas >= 3) return -1;
    } else {
      fallas = 0;
    }
    delay(80);
  }
  return 0;
}

static void esperarSinDedo(uint32_t msMax) {
  const uint32_t t0 = millis();
  while (millis() - t0 < msMax) {
    if (hayOrden()) return;
    if (fp.getImage() == FINGERPRINT_NOFINGER) return;
    delay(80);
  }
}

// Una vuelta del modo marcar: si hay un dedo, buscarlo en la memoria del lector.
static void escanearUnaVez() {
  static int fallas = 0;
  const uint8_t r = fp.getImage();
  if (r == FINGERPRINT_NOFINGER) {
    fallas = 0;
    delay(90);
    return;
  }
  if (r == FINGERPRINT_PACKETRECIEVEERR) {
    if (++fallas >= 3) {
      fallas = 0;
      perdida();
    }
    delay(100);
    return;
  }
  fallas = 0;
  if (r != FINGERPRINT_OK) {
    delay(100);
    return;
  }
  if (fp.image2Tz(1) != FINGERPRINT_OK) {
    avisar(HEv::IMAGEN_MALA);
  } else {
    const uint8_t s = fp.fingerSearch(1);
    if (s == FINGERPRINT_OK) avisar(HEv::COINCIDE, fp.fingerID, fp.confidence);
    else avisar(HEv::NO_COINCIDE);
  }
  esperarSinDedo(6000);  // no volver a leer el mismo dedo mientras siga apoyado
}

// Registro: dos lecturas del mismo dedo, se combinan y se guardan en idHuella.
static void enrolar(uint16_t idHuella) {
  for (uint8_t paso = 1; paso <= 2; paso++) {
    for (;;) {
      avisar(HEv::ENR_PON_DEDO, 0, 0, paso);
      const int d = esperarDedo(30000);
      if (d < 0) {
        perdida();
        avisar(HEv::ENR_ERROR, 0, 0, paso, "El lector de huella no responde");
        return;
      }
      if (d == 0) {
        if (!hayOrden()) avisar(HEv::ENR_ERROR, 0, 0, paso, "Se acabó el tiempo");
        return;
      }
      if (fp.image2Tz(paso) == FINGERPRINT_OK) break;
      avisar(HEv::IMAGEN_MALA, 0, 0, paso);
      esperarSinDedo(5000);
    }
    if (paso == 1) {
      // ¿Ya está registrado este dedo?
      if (fp.fingerSearch(1) == FINGERPRINT_OK) {
        avisar(HEv::ENR_DUPLICADA, fp.fingerID);
        esperarSinDedo(5000);
        return;
      }
      avisar(HEv::ENR_SACA_DEDO);
      esperarSinDedo(10000);
      if (hayOrden()) return;
      delay(300);
    }
  }
  const uint8_t m = fp.createModel();
  if (m == FINGERPRINT_ENROLLMISMATCH) {
    avisar(HEv::ENR_ERROR, 0, 0, 2, "Las dos lecturas no coinciden. Usa el mismo dedo.");
    return;
  }
  if (m != FINGERPRINT_OK) {
    avisar(HEv::ENR_ERROR, 0, 0, 2, "No se pudo combinar la huella");
    return;
  }
  fp.deleteModel(idHuella);  // por si quedó algo viejo en esa posición
  if (fp.storeModel(idHuella) != FINGERPRINT_OK) {
    avisar(HEv::ENR_ERROR, 0, 0, 2, "No se pudo guardar en el lector");
    return;
  }
  avisar(HEv::ENR_OK, idHuella);
  esperarSinDedo(5000);
}

// ------------------------------------------------------------ plantillas --
// Protocolo de la hoja del lector (manual ZFM-20, misma familia y órdenes que el AS608:
// «5.1 Data package format» y 6.2.6 a 6.2.9):
// EF01 + dirección (4) + tipo (01 orden, 02 datos, 07 respuesta, 08 último de datos) +
// largo (2: contenido + 2 de la suma) + contenido + suma (2) de tipo, largo y contenido.
// UpChar/DownChar mueven la plantilla en paquetes de datos del largo configurado
// (ReadSysPara, parámetro 6: 32/64/128/256 bytes). La plantilla son 512 bytes (hoja:
// «Template size 512 bytes»); igual no se fija el número: se lee hasta el paquete 08.
// La librería de Adafruit trae LoadChar/UpChar/Store/DeletChar pero lee paquetes de hasta
// 64 bytes y no trae DownChar: los paquetes de datos se leen y escriben aquí.

static void escribirPaquete(uint8_t tipo, const uint8_t* datos, uint16_t n) {
  const uint16_t largo = n + 2;
  const uint8_t cab[9] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, tipo, (uint8_t)(largo >> 8), (uint8_t)largo};
  uart.write(cab, sizeof(cab));
  uint16_t suma = tipo + (largo >> 8) + (largo & 0xFF);
  for (uint16_t i = 0; i < n; i++) suma += datos[i];
  uart.write(datos, n);
  const uint8_t s[2] = {(uint8_t)(suma >> 8), (uint8_t)suma};
  uart.write(s, 2);
}

// Lee un paquete entero: su contenido queda en datos (hasta max bytes) y el largo en n
static bool leerPaquete(uint8_t& tipo, uint8_t* datos, uint16_t max, uint16_t& n, uint32_t msMax) {
  const uint32_t t0 = millis();
  auto leer = [&](uint8_t& b) {
    while (!uart.available()) {
      if (millis() - t0 > msMax) return false;
      delay(1);
    }
    b = uart.read();
    return true;
  };
  uint8_t b = 0, previo = 0;
  for (;;) {  // sincronizar con EF 01
    if (!leer(b)) return false;
    if (previo == 0xEF && b == 0x01) break;
    previo = b;
  }
  uint8_t cab[7];  // dirección (4), tipo, largo (2)
  for (auto& c : cab)
    if (!leer(c)) return false;
  tipo = cab[4];
  const uint16_t largo = ((uint16_t)cab[5] << 8) | cab[6];
  if (largo < 2 || largo - 2 > max) return false;
  n = largo - 2;
  uint16_t suma = tipo + cab[5] + cab[6];
  for (uint16_t i = 0; i < n; i++) {
    if (!leer(datos[i])) return false;
    suma += datos[i];
  }
  uint8_t s1, s2;
  if (!leer(s1) || !leer(s2)) return false;
  return (((uint16_t)s1 << 8) | s2) == suma;
}

// LoadChar (número -> CharBuffer1) + UpChar (CharBuffer1 -> aquí). Devuelve el código del lector.
static uint8_t sacarPlantilla(uint16_t id, uint8_t*& buf, uint16_t& total) {
  buf = nullptr;
  total = 0;
  uint8_t r = fp.loadModel(id);
  if (r != FINGERPRINT_OK) return r;
  while (uart.available()) uart.read();
  r = fp.getModel();  // contesta 00 y sigue con los paquetes de datos
  if (r != FINGERPRINT_OK) return r;
  buf = (uint8_t*)malloc(PLANTILLA_MAX);
  if (!buf) return FINGERPRINT_BADPACKET;
  for (;;) {
    uint8_t tipo = 0;
    uint16_t n = 0;
    if (!leerPaquete(tipo, buf + total, PLANTILLA_MAX - total, n, 1000) ||
        (tipo != FINGERPRINT_DATAPACKET && tipo != FINGERPRINT_ENDDATAPACKET)) {
      free(buf);
      buf = nullptr;
      total = 0;
      return FINGERPRINT_PACKETRECIEVEERR;
    }
    total += n;
    if (tipo == FINGERPRINT_ENDDATAPACKET) break;
  }
  if (total < PLANTILLA_MIN) {
    free(buf);
    buf = nullptr;
    return FINGERPRINT_UPLOADFEATUREFAIL;
  }
  return FINGERPRINT_OK;
}

// DownChar (aquí -> CharBuffer1) + Store (CharBuffer1 -> número)
static uint8_t ponerPlantilla(uint16_t id, const uint8_t* datos, uint16_t largo) {
  if (!parametros) return FINGERPRINT_PACKETRESPONSEFAIL;  // sin el largo de paquete no se puede
  const uint16_t tam = fp.packet_len;
  while (uart.available()) uart.read();
  uint8_t orden[] = {0x09, 0x01};  // DownChar a CharBuffer1
  Adafruit_Fingerprint_Packet p(FINGERPRINT_COMMANDPACKET, sizeof(orden), orden);
  fp.writeStructuredPacket(p);
  if (fp.getStructuredPacket(&p) != FINGERPRINT_OK || p.type != FINGERPRINT_ACKPACKET)
    return FINGERPRINT_PACKETRECIEVEERR;
  if (p.data[0] != FINGERPRINT_OK) return p.data[0];
  for (uint16_t ya = 0; ya < largo; ya += tam) {
    const uint16_t n = std::min<uint16_t>(tam, largo - ya);
    escribirPaquete(ya + n >= largo ? FINGERPRINT_ENDDATAPACKET : FINGERPRINT_DATAPACKET, datos + ya, n);
  }
  uart.flush();  // los paquetes de datos no tienen respuesta: se sigue con Store
  delay(20);
  const uint8_t r = fp.storeModel(id);
  if (r != FINGERPRINT_OK) return r;
  // Control para el monitor serie: lo guardado se vuelve a leer y se compara
  uint8_t* vuelta = nullptr;
  uint16_t n = 0;
  if (sacarPlantilla(id, vuelta, n) == FINGERPRINT_OK)
    Serial.printf("[huella] plantilla %u puesta (%u bytes); releida %s\n", id, largo,
                  n == largo && !memcmp(vuelta, datos, n) ? "igual" : "distinta");
  free(vuelta);
  return FINGERPRINT_OK;
}

static void ordenDeFondo(const Orden& o) {
  if (o.cmd == HCmd::SACAR) {
    uint8_t* buf = nullptr;
    uint16_t n = 0;
    const uint8_t r = sacarPlantilla(o.id, buf, n);
    Serial.printf("[huella] sacar plantilla %u: codigo %u, %u bytes\n", o.id, r, n);
    if (r == FINGERPRINT_OK) {
      if (!avisar(HEv::PLANTILLA_SACADA, o.id, 0, 1, "", buf, n)) free(buf);
    } else {
      avisar(HEv::PLANTILLA_ERROR, o.id, r, 1);
    }
  } else if (o.cmd == HCmd::PONER) {
    const uint8_t r = ponerPlantilla(o.id, o.datos, o.largo);
    free(o.datos);
    Serial.printf("[huella] poner plantilla %u: codigo %u\n", o.id, r);
    if (r == FINGERPRINT_OK) avisar(HEv::PLANTILLA_PUESTA, o.id);
    else avisar(HEv::PLANTILLA_ERROR, o.id, r, 2);
  } else if (o.cmd == HCmd::QUITAR) {
    uint8_t r = FINGERPRINT_PACKETRECIEVEERR;
    for (int i = 0; i < 3 && (r = fp.deleteModel(o.id)) != FINGERPRINT_OK; i++) delay(100);
    avisar(HEv::BORRADA, o.id, r, r == FINGERPRINT_OK ? 1 : 0);
  }
}

// Sin lector, las órdenes de fondo se contestan con error (y se libera lo que traigan)
static void descartarFondo() {
  Orden o;
  while (xQueueReceive(qFondo, &o, 0) == pdTRUE) {
    free(o.datos);
    if (o.cmd == HCmd::QUITAR) avisar(HEv::BORRADA, o.id, FINGERPRINT_PACKETRECIEVEERR, 0);
    else avisar(HEv::PLANTILLA_ERROR, o.id, FINGERPRINT_PACKETRECIEVEERR, o.cmd == HCmd::SACAR ? 1 : 2);
  }
}

static void tarea(void*) {
  uart.setRxBufferSize(1024);  // UpChar manda ~560 bytes seguidos (4 paquetes de 128 + cabeceras)
  uart.begin(HUELLA_BAUD, SERIAL_8N1, HUELLA_RX, HUELLA_TX);
  delay(400);  // el lector tarda en arrancar
  bool avisadoSinLector = false;
  for (;;) {
    if (!conectado) {
      if (conectar()) {
        avisadoSinLector = false;
      } else {
        if (!avisadoSinLector) perdida();
        avisadoSinLector = true;
        // Las órdenes que lleguen sin lector se descartan, salvo cambiar de modo
        Orden o;
        while (xQueueReceive(qOrden, &o, 0) == pdTRUE) {
          if (o.cmd == HCmd::ESCANEAR) escaneando = true;
          else if (o.cmd == HCmd::PAUSAR) escaneando = false;
          else if (o.cmd == HCmd::ENROLAR) avisar(HEv::ENR_ERROR, 0, 0, 1, "El lector de huella no está conectado");
        }
        descartarFondo();
        delay(2000);
        continue;
      }
    }

    Orden o;
    if (xQueueReceive(qOrden, &o, escaneando ? 0 : pdMS_TO_TICKS(200)) == pdTRUE) {
      switch (o.cmd) {
        case HCmd::ESCANEAR: escaneando = true; break;
        case HCmd::PAUSAR: escaneando = false; break;
        case HCmd::ENROLAR:
          escaneando = false;
          enrolar(o.id);
          break;
        case HCmd::BORRAR:
          for (int i = 0; i < 3 && fp.deleteModel(o.id) != FINGERPRINT_OK; i++) delay(100);
          break;
        case HCmd::BORRAR_TODO:
          for (int i = 0; i < 3 && fp.emptyDatabase() != FINGERPRINT_OK; i++) delay(100);
          break;
      }
      continue;
    }
    if (xQueueReceive(qFondo, &o, 0) == pdTRUE) {
      ordenDeFondo(o);
      continue;
    }
    if (escaneando) escanearUnaVez();
  }
}

static void enviar(HCmd c, uint16_t id = 0) {
  Orden o = {c, id, nullptr, 0};
  xQueueSend(qOrden, &o, pdMS_TO_TICKS(50));
}

static bool enviarFondo(HCmd c, uint16_t id, uint8_t* datos = nullptr, uint16_t largo = 0) {
  Orden o = {c, id, datos, largo};
  return xQueueSend(qFondo, &o, 0) == pdTRUE;
}

void huella_iniciar() {
  // Ojo: no llamar fp.begin(); reiniciaría el UART en sus pines por defecto (17/18),
  // que en esta placa son del audio.
  qOrden = xQueueCreate(8, sizeof(Orden));
  qFondo = xQueueCreate(2, sizeof(Orden));
  qAviso = xQueueCreate(12, sizeof(HuellaEv));
  xTaskCreatePinnedToCore(tarea, "huella", 6144, NULL, 2, NULL, 0);
}

void huella_escanear(bool on) { enviar(on ? HCmd::ESCANEAR : HCmd::PAUSAR); }
void huella_enrolar(uint16_t id) { enviar(HCmd::ENROLAR, id); }
void huella_borrar(uint16_t id) { enviar(HCmd::BORRAR, id); }
void huella_borrar_todo() { enviar(HCmd::BORRAR_TODO); }
bool huella_sacar_plantilla(uint16_t id) { return enviarFondo(HCmd::SACAR, id); }
bool huella_quitar(uint16_t id) { return enviarFondo(HCmd::QUITAR, id); }
bool huella_poner_plantilla(uint16_t id, uint8_t* datos, uint16_t largo) {
  if (enviarFondo(HCmd::PONER, id, datos, largo)) return true;
  free(datos);
  return false;
}
bool huella_evento(HuellaEv& ev) { return xQueueReceive(qAviso, &ev, 0) == pdTRUE; }
bool huella_conectado() { return conectado; }
uint16_t huella_capacidad() { return capacidad; }
