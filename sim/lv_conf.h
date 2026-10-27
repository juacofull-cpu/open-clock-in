// LVGL para el simulador: la misma configuración de la placa (include/lv_conf.h)
// más el driver de ventanas de Windows y su candado entre hilos.
#ifndef SIM_LV_CONF_H
#define SIM_LV_CONF_H
#include "../include/lv_conf.h"

#undef LV_USE_OS
#define LV_USE_OS LV_OS_WINDOWS
#define LV_USE_WINDOWS 1

#endif
