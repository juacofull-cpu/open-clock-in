// Configuración de LVGL 9.2 para el reloj control. Lo que no está aquí toma el valor
// por defecto de lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 20
#define LV_DPI_DEF 160
#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

// Fuentes de símbolos (flechas, wifi, ajustes...). Las letras con tilde vienen de
// las fuentes propias en src/fuentes, que usan estas como respaldo.
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(f_texto) LV_FONT_DECLARE(f_chico) LV_FONT_DECLARE(f_grande) LV_FONT_DECLARE(f_titulo) LV_FONT_DECLARE(f_reloj) LV_FONT_DECLARE(f_fino)
#define LV_FONT_DEFAULT &f_texto

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0
#define LV_THEME_DEFAULT_TRANSITION_TIME 60

#define LV_USE_KEYBOARD 1
#define LV_USE_TEXTAREA 1
#define LV_USE_ROLLER 1
#define LV_USE_TABLE 1
#define LV_USE_LIST 1
#define LV_USE_SWITCH 1
#define LV_USE_SLIDER 1
#define LV_USE_BUTTONMATRIX 1

// Fotos de los perfiles (JPEG baseline). fotos.cpp usa el decodificador directo.
#define LV_USE_TJPGD 1

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#endif
