// Temas de temporada (Navidad, Año Nuevo, Fiestas Patrias...): saludos que rotan, melodías,
// adornos y sorpresas escondidas. Cada tema es un JSON (los de temas/*.json vienen dentro del
// programa; un /tema.json en la memoria flash, que bajará del panel, va primero). Vale el
// primero cuyas fechas «desde»-«hasta» (MM-DD, pueden cruzar el año) incluyan hoy; si ninguno,
// el reloj se ve y suena como siempre.
#pragma once
#include <Arduino.h>
#include <vector>

struct Nota {
  uint16_t frec;  // Hz; 0 = silencio
  uint16_t ms;
};

// T_ESPECIAL = la melodía de la última sorpresa elegida (saludo especial o los 5 toques)
enum class TonoTema : uint8_t { T_OK, T_SALIDA, T_ERROR, T_AVISO, T_TECLA, T_ESPECIAL };
enum class Adorno : uint8_t { NADA, NIEVE, CONFETI, FUEGOS, ALMAS };

void tema_cargar();          // lee los temas (al arrancar y cuando llegue uno nuevo)
bool tema_activo();          // algún tema vale hoy
String tema_nombre();        // "Navidad"; vacío si no hay
Adorno tema_adorno();        // lo que se mueve en el inicio y en el saludo
bool tema_luces();           // guirnalda de luces arriba del inicio
// Saludo del tema para esa marca, con {nombre}, {año} y {año_que_viene} reemplazados; vacío =
// usar el de siempre. Primero las reglas «especiales» (por día, hora o al azar, 1 de cada N),
// después la lista normal. La misma marca (semilla) da siempre la misma frase, así una corrección
// del panel no la cambia. conTono = la regla trae su melodía (tocar sonido_especial()).
String tema_saludo(bool entrada, const String& nombre, const String& semilla, bool& conTono);
String tema_sorpresa(bool& conTono);  // al tocar 5 veces la hora; vacío = no hay
bool tema_melodia(TonoTema t, std::vector<Nota>& notas);  // false = usar el tono de siempre
