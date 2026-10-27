// Se incluye antes que todo en el código del reloj cuando se compila para el PC.
// El reloj del simulador parte sin hora (como la placa sin WiFi): time() lee ese
// reloj propio, y settimeofday() o la hora por internet lo ajustan.
#pragma once
#include <time.h>
#include <sys/time.h>

time_t sim_time(time_t* t);
int sim_settimeofday(const struct timeval* tv, const void* zona);

#define time(x) sim_time(x)
#define settimeofday(a, b) sim_settimeofday(a, b)
