#pragma once
#include <Arduino.h>

void red_iniciar();
void red_tarea();
void red_conectar(const String& ssid, const String& clave);  // guarda y conecta
bool red_conectada();
String red_ip();
String red_estado();  // texto corto para mostrar
