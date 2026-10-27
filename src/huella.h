#pragma once
#include <stdint.h>

// Lo que el lector le avisa a la interfaz
enum class HEv : uint8_t {
  CONECTADO,       // id = capacidad del lector
  DESCONECTADO,
  COINCIDE,        // id = número de huella, confianza
  NO_COINCIDE,
  IMAGEN_MALA,     // se puso el dedo pero no se leyó bien
  ENR_PON_DEDO,    // paso = 1 o 2
  ENR_SACA_DEDO,
  ENR_OK,          // id = número de huella guardada
  ENR_DUPLICADA,   // id = número de huella que ya existía
  ENR_ERROR,       // texto en msg
  // Plantillas (para que el dedo sirva en los dos relojes; las pide panel.cpp en segundo plano)
  PLANTILLA_SACADA,  // id, datos/largo: bytes crudos de UpChar. Quien lo recibe hace free(datos)
  PLANTILLA_PUESTA,  // id: DownChar + Store listos
  PLANTILLA_ERROR,   // id, paso = 1 sacar / 2 poner, confianza = código del lector (0x0C = no hay nada)
  BORRADA,           // id, paso = 1 si se borró (respuesta a huella_quitar)
};

struct HuellaEv {
  HEv ev;
  uint16_t id;
  uint16_t confianza;
  uint8_t paso;
  char msg[64];
  uint8_t* datos;  // solo PLANTILLA_SACADA (malloc; lo libera quien lo recibe)
  uint16_t largo;
};

// Largo de las plantillas que se aceptan (el AS608 usa 512 bytes; el panel acepta 256 a 2048)
#define PLANTILLA_MIN 256
#define PLANTILLA_MAX 2048

void huella_iniciar();
void huella_escanear(bool on);       // reconocer dedos para marcar (pantalla de inicio)
void huella_enrolar(uint16_t idHuella);
void huella_borrar(uint16_t idHuella);
void huella_borrar_todo();
bool huella_evento(HuellaEv& ev);    // sacar el siguiente aviso, si hay
// En segundo plano (no cortan un registro ni una lectura en curso). false = cola llena, probar después.
bool huella_sacar_plantilla(uint16_t idHuella);                                // LoadChar + UpChar
bool huella_poner_plantilla(uint16_t idHuella, uint8_t* datos, uint16_t largo);  // DownChar + Store; se queda con datos (free)
bool huella_quitar(uint16_t idHuella);                                         // DeletChar, avisa BORRADA
bool huella_conectado();
uint16_t huella_capacidad();
