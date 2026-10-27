#pragma once
#include <stdint.h>

void sonido_iniciar();
void sonido_activar(bool on);
void sonido_ok();      // marca aceptada / huella registrada
void sonido_salida();  // marca de salida (suena como sonido_ok salvo que el tema traiga otra)
void sonido_especial();  // la melodía de la sorpresa del tema que se acaba de elegir
void sonido_error();   // no reconocida
void sonido_aviso();   // marca repetida, falta la hora
void sonido_tecla();   // toque de botón
void led(uint8_t r, uint8_t g, uint8_t b);
