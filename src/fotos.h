// Fotos de los perfiles para la pantalla. El JPEG (baseline, 160x160, lo baja panel.cpp)
// queda en /fotos/<perfil>.jpg; aquí se decodifica una vez con TJPGD, se recorta en
// círculo y queda en memoria (PSRAM). Sin foto: una silueta.
#pragma once
#include <Arduino.h>
#include <lvgl.h>

#define FOTO_GRANDE 132  // al saludar
#define FOTO_CHICA 44    // en las listas

const lv_image_dsc_t* foto_imagen(const String& perfil, int tam);  // nunca nulo
String foto_ruta(const String& perfil);
// Decodifica de antemano (una por llamada) para que el saludo no espere. true = trabajó
bool foto_precargar(const String& perfil, int tam);
void fotos_cambio(const String& perfil);  // se bajó otra o se borró: olvidar la decodificada
void fotos_soltar_viejas();               // liberar las olvidadas (cuando ya no se ven)
