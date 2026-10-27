// Conexión con el servidor (docs/API.md): los trabajadores son los perfiles del panel y las
// marcas se guardan allá. Contrato en docs/API.md.
#pragma once
#include <Arduino.h>
#include <vector>
#include "huella.h"

struct Perfil {
  String perfil;  // id del perfil en el panel ("ana")
  String nombre;  // "Ana"
  std::vector<uint16_t> huellas;
  String foto;    // versión de su foto en el panel ("" = sin foto propia: silueta)
};

// Una marca que este reloj hizo hoy (copia local; la de verdad está en el panel)
struct MarcaHoy {
  String id;
  uint16_t huella;
  String perfil, nombre;  // vacíos = huella sin perfil conocido
  String tipo;            // ENTRADA / SALIDA
  String hora;            // HH:MM
  time_t t;               // 0 = se marcó sin hora
  uint32_t ms;            // millis() al marcar, para ponerle la hora si llega después
  bool repetida, enviada;
};

// Lo que se le muestra a la persona al poner el dedo
struct Saludo {
  String id;      // de la marca
  String perfil;
  String nombre;  // vacío = no se conoce la huella
  String tipo;    // ENTRADA / SALIDA
  bool repetida = false;
  String hora;    // HH:MM
};

// Avisos del panel a la interfaz
enum class PEv : uint8_t { PERFILES, PERFILES_ERROR, ASIGNADA, ASIGNAR_ERROR, SOLTADA, SOLTAR_ERROR, MARCA };
struct PanelEv {
  PEv ev;
  uint16_t huella = 0;
  String texto;   // motivo del error, para mostrarlo tal cual
  Saludo saludo;  // MARCA: lo que respondió el panel
};

void panel_iniciar();
void panel_tarea();  // en el loop: manda lo pendiente y procesa las respuestas
void panel_guardar_ya();  // escribe en la flash lo que quedó para después (antes de reiniciar)
bool panel_evento(PanelEv& ev);

// Perfiles (copia del último GET /api/reloj/perfiles)
const std::vector<Perfil>& panel_perfiles();
const Perfil* panel_por_huella(uint16_t huella);
const Perfil* panel_buscar(const String& perfil);
int panel_huella_libre(uint16_t capacidad);
uint16_t panel_huellas_usadas();

// Marcar: saluda al tiro con lo que se sabe y deja la marca en la cola
Saludo panel_marcar(uint16_t huella);
const std::vector<MarcaHoy>& panel_marcas_hoy();
size_t panel_en_cola();

// Necesitan internet (contestan con un aviso)
bool panel_listo();  // hay WiFi y llave
void panel_pedir_perfiles();
void panel_refrescar_pronto();  // «No te conozco»: quizás se registró en el otro reloj (máx. cada 15 s)
void panel_asignar(uint16_t huella, const String& perfil);
void panel_soltar(uint16_t huella);

// Estado y configuración
bool panel_conectado();  // la última vez el panel contestó bien
String panel_estado();   // texto corto para mostrar
// "" = guardado, si no el motivo. Llave u oficina vacías = se dejan las que había.
String panel_configurar(const String& url, const String& llave, const String& oficina = String());

// Plantillas del lector (para que el dedo sirva en los dos relojes)
void panel_lector(const HuellaEv& ev);  // avisos PLANTILLA_* y BORRADA del lector (libera ev.datos)
String panel_plantillas_estado();       // «3 en el lector · 1 por subir · 0 por bajar»
