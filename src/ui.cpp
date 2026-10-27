// Interfaz táctil (LVGL 9). Pantalla horizontal de 480x320.
//
// Regla: nunca se cambia de pantalla dentro de un evento de LVGL (borraría el botón
// que se está tocando). Los botones piden el cambio con ir(...) y ui_tarea() lo hace.
//
// Paleta: un color de acento (COLOR_ACENTO en config.h) + negro
// + blanco, sin otros colores. Poppins. Cápsulas, círculos y tarjetas muy redondeadas.
// Los estados se distinguen por fondo (acento / negro / blanco), forma e ícono, no por color.
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <lvgl.h>
#include <algorithm>
#include "config.h"
#include "datos.h"
#include "fotos.h"
#include "huella.h"
#include "panel.h"
#include "pantalla.h"
#include "red.h"
#include "sonido.h"
#include "tema.h"
#include "ui.h"

#define C_FONDO lv_color_hex(0x111111)       // negro de marca
#define C_SUPERFICIE lv_color_hex(0x202020)  // tarjetas sobre el negro (grises que el RGB565 deja neutros)
#define C_LINEA lv_color_hex(0x303030)       // bordes, botones secundarios
#define C_TEXTO lv_color_hex(0xFFFFFF)
#define C_TENUE lv_color_hex(0x9A9A9A)
#define C_ACENTO lv_color_hex(COLOR_ACENTO)
#define C_TINTA lv_color_hex(0x1A1A1A)  // texto sobre blanco
#define C_GRIS lv_color_hex(0x4A4A4A)   // texto secundario sobre blanco

LV_IMAGE_DECLARE(logo_chico);
LV_IMAGE_DECLARE(logo_grande);

typedef void (*Destino)();

enum class Vista { ARRANQUE, INICIO, PIN, MENU, PERSONAS, PERSONA, REGISTRO, MARCAS, WIFI, WIFI_CLAVE, AJUSTES, INFO };
static Vista vista = Vista::ARRANQUE;
static Destino pendiente = nullptr;
static void ir(Destino d) { pendiente = d; }

static const char* DIAS[] = {"Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado"};
static const char* MESES[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio",
                              "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"};

// Referencias a objetos de la pantalla actual (se ponen en nulo al cambiar)
static lv_obj_t *lblHH, *lblMM, *lblFecha, *lblAviso, *lblWifi, *lblCola, *chipCola, *ondaInicio, *puntoInicio, *lblOficina;
static lv_obj_t *puntos, *lblPinMsg;
static lv_obj_t* taTexto;
static lv_obj_t *cejaPaso, *lblPaso, *circuloPaso, *lblCirculo, *ondaPaso, *soportePaso, *lblInstr, *filaFin;
static lv_obj_t *lblWifiEstado, *listaRedes;

static void limpiarRefs() {
  lblHH = lblMM = lblFecha = lblAviso = lblWifi = lblCola = chipCola = ondaInicio = puntoInicio = lblOficina = nullptr;
  puntos = lblPinMsg = nullptr;
  taTexto = nullptr;
  cejaPaso = lblPaso = circuloPaso = lblCirculo = ondaPaso = soportePaso = lblInstr = filaFin = nullptr;
  lblWifiEstado = listaRedes = nullptr;
}

static lv_obj_t* capa = nullptr;          // aviso a pantalla completa
static lv_obj_t* capaSaliendo = nullptr;  // el anterior mientras se desvanece

// ------------------------------------------------------------- animación --
// Todo se mueve con curva suave (arranca rápido y frena) y en poco tiempo: en la placa
// cada cuadro redibuja la zona que cambia, así que se anima poco y bien.
static void animar(void* var, lv_anim_exec_xcb_t cb, int32_t desde, int32_t hasta, uint32_t ms,
                   uint32_t espera = 0, lv_anim_path_cb_t curva = lv_anim_path_ease_out) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, var);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, desde, hasta);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_delay(&a, espera);
  lv_anim_set_path_cb(&a, curva);
  lv_anim_start(&a);
}

static void aOpa(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
static void aFondoOpa(void* o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
static void aTextoOpa(void* o, int32_t v) { lv_obj_set_style_text_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
static void aSubir(void* o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)o, v, 0); }
static void aY(void* o, int32_t v) { lv_obj_set_y((lv_obj_t*)o, v); }
static void aEscala(void* o, int32_t v) { lv_obj_set_style_transform_scale((lv_obj_t*)o, v, 0); }
static void aImagenOpa(void* o, int32_t v) { lv_obj_set_style_image_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
static void aImagenEscala(void* o, int32_t v) { lv_image_set_scale((lv_obj_t*)o, v); }

// Onda que sale del punto del lector: crece y se apaga (el reloj «escucha»)
static void aOnda(void* o, int32_t v) {
  lv_obj_t* ob = (lv_obj_t*)o;
  const int32_t min = (int32_t)(intptr_t)lv_obj_get_user_data(ob) & 0xFFFF;
  const int32_t max = (int32_t)(intptr_t)lv_obj_get_user_data(ob) >> 16;
  lv_obj_set_size(ob, v, v);
  lv_obj_set_style_border_opa(ob, (lv_opa_t)(max > min ? 255 * (max - v) / (max - min) : 0), 0);
}

// Sacudida de «no»: va y viene y se calma
static void aSacudir(void* o, int32_t v) {
  const int32_t x = 12 * lv_trigo_sin((int16_t)(v * 1080 / 1000)) / LV_TRIGO_SIN_MAX;
  lv_obj_set_style_translate_x((lv_obj_t*)o, x * (1000 - v) / 1000, 0);
}

static void sacudir(lv_obj_t* o) { animar(o, aSacudir, 0, 1000, 420, 0, lv_anim_path_linear); }

// Aparece subiendo un poco y encendiéndose
static void aparecer(lv_obj_t* o, uint32_t espera = 0) {
  lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
  animar(o, aOpa, 0, 255, 260, espera);
  animar(o, aSubir, 14, 0, 320, espera);
}

// Rebote al cambiar de estado (necesita el pivote al centro: ver centrarPivote)
static void rebotar(lv_obj_t* o) { animar(o, aEscala, 200, 256, 420, 0, lv_anim_path_overshoot); }

static void centrarPivote(lv_obj_t* o) {
  lv_obj_set_style_transform_pivot_x(o, LV_PCT(50), 0);
  lv_obj_set_style_transform_pivot_y(o, LV_PCT(50), 0);
}

static lv_obj_t* onda(lv_obj_t* p, lv_color_t c, int min, int max, uint32_t ms) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_set_style_border_color(o, c, 0);
  lv_obj_set_size(o, min, min);
  lv_obj_center(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_user_data(o, (void*)(intptr_t)((max << 16) | min));
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, aOnda);
  lv_anim_set_values(&a, min, max);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_repeat_delay(&a, 500);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
  return o;
}

// Al tocar, el botón se achica un poco y vuelve (la sombra del tema queda fuera)
// ----------------------------------------------------- tema de temporada --
// Copos, confeti o «almas» que suben, meciéndose; o fuegos artificiales que revientan.
// Van primero en la pantalla para quedar detrás de todo. Solo se anima posición y bg_opa:
// nada de opacidad del objeto, que en la placa pide capas.
static void aX(void* o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t*)o, v, 0); }
static void aFondoOpaSola(void* o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }

#define SUBE 0x80000000u  // en el user_data de la partícula, junto a su duración

static void caer(lv_obj_t* c, int y0, uint32_t ms, bool sube, bool sinFin) {
  const int ini = sube ? PANT_H + 12 : -12, fin = sube ? -12 : PANT_H + 12;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, c);
  lv_anim_set_exec_cb(&a, aY);
  lv_anim_set_values(&a, sinFin ? ini : y0, fin);
  lv_anim_set_duration(&a, sinFin ? ms : ms * (uint32_t)abs(fin - y0) / (uint32_t)abs(fin - ini));
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  if (sinFin) lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  else
    lv_anim_set_completed_cb(&a, [](lv_anim_t* a) {  // primera pasada desde su altura: ahora de borde a borde
      const uint32_t ud = (uint32_t)(uintptr_t)lv_obj_get_user_data((lv_obj_t*)a->var);
      caer((lv_obj_t*)a->var, 0, ud & ~SUBE, ud & SUBE, true);
    });
  lv_anim_start(&a);
}

static void particulas(lv_obj_t* p, int n, Adorno tipo) {
  const bool confeti = tipo == Adorno::CONFETI, almas = tipo == Adorno::ALMAS;
  for (int i = 0; i < n; i++) {
    lv_obj_t* c = lv_obj_create(p);
    lv_obj_remove_style_all(c);
    if (confeti) {
      lv_obj_set_size(c, lv_rand(4, 6), lv_rand(7, 10));
      lv_obj_set_style_radius(c, 1, 0);
      lv_obj_set_style_bg_color(c, i % 2 ? lv_color_hex(0xFFFFFF) : C_ACENTO, 0);
      lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    } else {
      const int d = almas ? lv_rand(6, 12) : lv_rand(3, 8);
      lv_obj_set_size(c, d, d);
      lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(c, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_bg_opa(c, (lv_opa_t)(almas ? lv_rand(25, 70) : lv_rand(90, 200)), 0);
    }
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_x(c, lv_rand(0, PANT_W));
    // Parte en cualquier altura (que la pantalla ya se vea llena) y después cruza de borde a borde sin fin
    const uint32_t ms = confeti ? lv_rand(4500, 8000) : almas ? lv_rand(9000, 15000) : lv_rand(7000, 13000);
    const int y0 = lv_rand(-10, PANT_H);
    lv_obj_set_user_data(c, (void*)(uintptr_t)(ms | (almas ? SUBE : 0)));
    lv_obj_set_y(c, y0);
    caer(c, y0, ms, almas, false);
    const int vaiven = confeti ? 16 : almas ? 14 : 9;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, aX);
    lv_anim_set_values(&a, -vaiven, vaiven);
    lv_anim_set_duration(&a, lv_rand(1800, 3200));
    lv_anim_set_playback_duration(&a, lv_rand(1800, 3200));
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }
}

// Un fuego artificial: 12 chispas que salen del mismo punto, frenan y se apagan
static void reventar(lv_obj_t* p) {
  const int x = lv_rand(60, PANT_W - 60), y = lv_rand(40, PANT_H / 2);
  const bool blanco = lv_rand(0, 1);
  for (int i = 0; i < 12; i++) {
    lv_obj_t* c = lv_obj_create(p);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 7, 7);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, (i % 2) == blanco ? lv_color_hex(0xFFFFFF) : C_ACENTO, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(c, x, y);
    const int ang = i * 30 + lv_rand(0, 15), r = lv_rand(50, 75);
    animar(c, aX, 0, r * lv_trigo_cos((int16_t)ang) / LV_TRIGO_SIN_MAX, 900);
    animar(c, aSubir, 0, r * lv_trigo_sin((int16_t)ang) / LV_TRIGO_SIN_MAX + 10, 1000);  // y cae un poco
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, aFondoOpaSola);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, [](lv_anim_t* a) { lv_obj_delete_async((lv_obj_t*)a->var); });
    lv_anim_start(&a);
  }
}

static void fuegos(lv_obj_t* p, uint32_t cada) {
  lv_timer_t* t = lv_timer_create([](lv_timer_t* t) { reventar((lv_obj_t*)lv_timer_get_user_data(t)); }, cada, p);
  reventar(p);
  lv_obj_add_event_cb(p, [](lv_event_t* e) { lv_timer_delete((lv_timer_t*)lv_event_get_user_data(e)); },
                      LV_EVENT_DELETE, t);
}

static void adornos(lv_obj_t* p, int n) {
  const Adorno tipo = tema_adorno();
  if (tipo == Adorno::FUEGOS) fuegos(p, 2700 / std::max(1, n / 8));
  else if (tipo != Adorno::NADA) particulas(p, n, tipo);
}

static void aBrillo(void* o, int32_t v) { lv_obj_set_style_shadow_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }

// Guirnalda arriba: un cable que cuelga entre el logo y la tuerca, con luces acento y
// blancas siempre encendidas (solo respiran un poco, nunca se apagan).
#define LUCES_N 9
static lv_point_precise_t cableLuces[LUCES_N];

static void luces(lv_obj_t* s) {
  const int x0 = 150, x1 = PANT_W - 150;
  for (int i = 0; i < LUCES_N; i++) {
    const int x = x0 + (x1 - x0) * i / (LUCES_N - 1);
    const int y = 8 + 14 * lv_trigo_sin((int16_t)(180 * i / (LUCES_N - 1))) / LV_TRIGO_SIN_MAX;
    cableLuces[i] = {(lv_value_precise_t)x, (lv_value_precise_t)y};
  }
  lv_obj_t* cable = lv_line_create(s);
  lv_line_set_points(cable, cableLuces, LUCES_N);
  lv_obj_set_style_line_width(cable, 2, 0);
  lv_obj_set_style_line_color(cable, C_LINEA, 0);
  lv_obj_set_style_line_rounded(cable, true, 0);
  lv_obj_set_pos(cable, 0, 0);
  for (int i = 0; i < LUCES_N; i++) {
    lv_obj_t* l = lv_obj_create(s);
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, 12, 12);
    lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(l, i % 2 ? lv_color_hex(0xFFFFFF) : C_ACENTO, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(l, i % 2 ? lv_color_hex(0xFFFFFF) : C_ACENTO, 0);
    lv_obj_set_style_shadow_width(l, 14, 0);
    lv_obj_set_style_shadow_opa(l, 140, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(l, (int)cableLuces[i].x - 6, (int)cableLuces[i].y - 1);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_exec_cb(&a, aBrillo);  // el halo, no la opacidad: no pide capas en la placa
    lv_anim_set_values(&a, 160, 40);
    lv_anim_set_duration(&a, 1200);
    lv_anim_set_playback_duration(&a, 1200);
    lv_anim_set_delay(&a, (i % 3) * 400);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }
}

static lv_style_t stTactil, stPresionado, stPresionadoAncho;
static lv_style_transition_dsc_t trTactil;

static void estilosIniciar() {
  static const lv_style_prop_t props[] = {LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y, LV_STYLE_PROP_INV};
  lv_style_transition_dsc_init(&trTactil, props, lv_anim_path_ease_out, 150, 0, nullptr);
  lv_style_init(&stTactil);
  lv_style_set_shadow_width(&stTactil, 0);
  lv_style_set_border_width(&stTactil, 0);
  lv_style_set_radius(&stTactil, LV_RADIUS_CIRCLE);
  lv_style_set_transform_pivot_x(&stTactil, LV_PCT(50));
  lv_style_set_transform_pivot_y(&stTactil, LV_PCT(50));
  lv_style_set_transition(&stTactil, &trTactil);
  lv_style_init(&stPresionado);
  lv_style_set_transform_scale_x(&stPresionado, 238);
  lv_style_set_transform_scale_y(&stPresionado, 238);
  lv_style_init(&stPresionadoAncho);  // filas de lista: casi todo el ancho, se achican menos
  lv_style_set_transform_scale_x(&stPresionadoAncho, 248);
  lv_style_set_transform_scale_y(&stPresionadoAncho, 248);
}

static void tactil(lv_obj_t* o, bool ancho = false) {
  lv_obj_add_style(o, &stTactil, 0);
  lv_obj_add_style(o, ancho ? &stPresionadoAncho : &stPresionado, LV_STATE_PRESSED);
}

// ------------------------------------------------------------------ ayudas --
static lv_obj_t* nuevaPantalla() {
  limpiarRefs();
  lv_obj_t* s = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s, C_FONDO, 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s, C_TEXTO, 0);
  lv_obj_set_style_text_font(s, &f_texto, 0);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

// Profundidad de cada pantalla: más adentro entra desde la derecha, volver sale hacia la derecha
enum class PinModo { ENTRAR, NUEVA1, NUEVA2 };
static PinModo pinModo = PinModo::ENTRAR;

static int nivel(Vista v) {
  switch (v) {
    case Vista::ARRANQUE:
    case Vista::INICIO: return 0;
    case Vista::PIN: return pinModo == PinModo::ENTRAR ? 1 : 4;
    case Vista::MENU: return 2;
    case Vista::PERSONAS:
    case Vista::MARCAS:
    case Vista::WIFI:
    case Vista::AJUSTES: return 3;
    case Vista::REGISTRO: return 5;
    default: return 4;
  }
}

static uint32_t finTransicion = 0;
static bool soltarFotos = false;

static void cambiar(lv_obj_t* s, Vista v) {
  lv_obj_t* vieja = lv_screen_active();
  lv_screen_load_anim_t tipo = LV_SCR_LOAD_ANIM_NONE;
  uint32_t ms = 0;
  if (vista == Vista::ARRANQUE && v != Vista::ARRANQUE) {
    tipo = LV_SCR_LOAD_ANIM_FADE_IN;
    ms = 450;
  } else if (v != vista) {  // la misma pantalla redibujada (llegó la lista, etc.) cambia sin moverse
    tipo = nivel(v) >= nivel(vista) ? LV_SCR_LOAD_ANIM_OVER_LEFT : LV_SCR_LOAD_ANIM_OUT_RIGHT;
    ms = 280;
  }
  lv_screen_load_anim(s, tipo, ms, 0, true);
  // LVGL las mueve en línea recta; con curva suave se sienten más naturales
  for (lv_obj_t* o : {s, vieja}) {
    lv_anim_t* a = o ? lv_anim_get(o, nullptr) : nullptr;
    if (a) lv_anim_set_path_cb(a, lv_anim_path_ease_out);
  }
  vista = v;
  huella_escanear(v == Vista::INICIO);
  lv_display_trigger_activity(NULL);
  // Las fotos olvidadas se sueltan cuando ya no se ven (la pantalla vieja sigue en pantalla mientras sale)
  finTransicion = millis() + ms + 60;
  soltarFotos = true;
}

static lv_obj_t* etiqueta(lv_obj_t* p, const char* txt, const lv_font_t* f, lv_color_t c) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  return l;
}

static void ponerTexto(lv_obj_t* l, const char* t) {
  if (l && strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

// Cambiar un estilo, aunque sea al mismo valor, redibuja el objeto: solo si cambió
static void ponerColorTexto(lv_obj_t* o, lv_color_t c) {
  if (o && !lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) lv_obj_set_style_text_color(o, c, 0);
}

static void ponerColorFondo(lv_obj_t* o, lv_color_t c) {
  if (o && !lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), c)) lv_obj_set_style_bg_color(o, c, 0);
}

static void mostrarObj(lv_obj_t* o, bool si) {
  if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == !si) return;
  if (si) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Cápsula de sección de la marca: mayúsculas, letra espaciada
static lv_obj_t* ceja(lv_obj_t* p, const char* txt, lv_color_t fondo, lv_color_t color) {
  lv_obj_t* c = lv_obj_create(p);
  lv_obj_remove_style_all(c);
  lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(c, fondo, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(c, 14, 0);
  lv_obj_set_style_pad_ver(c, 4, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* l = etiqueta(c, txt, &f_chico, color);
  lv_obj_set_style_text_letter_space(l, 2, 0);
  return c;
}

static void clickSonido(lv_event_t*) { sonido_tecla(); }

static lv_obj_t* boton(lv_obj_t* p, const char* txt, lv_color_t bg, lv_event_cb_t cb, void* ud = nullptr,
                       const lv_font_t* f = &f_grande) {
  lv_obj_t* b = lv_button_create(p);
  tactil(b);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, C_TEXTO, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, clickSonido, LV_EVENT_CLICKED, nullptr);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

static void irDesdeEvento(lv_event_t* e) { ir((Destino)lv_event_get_user_data(e)); }

static lv_obj_t* botonIr(lv_obj_t* p, const char* txt, lv_color_t bg, Destino d) {
  return boton(p, txt, bg, irDesdeEvento, (void*)d);
}

// Barra de arriba: botón para volver + título
static void cabecera(lv_obj_t* s, const char* titulo, Destino volver, const char* txtVolver = LV_SYMBOL_LEFT "  Volver") {
  lv_obj_t* b = botonIr(s, txtVolver, C_SUPERFICIE, volver);
  lv_obj_set_size(b, 136, 44);
  lv_obj_set_pos(b, 8, 6);
  lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &f_texto, 0);
  lv_obj_t* t = etiqueta(s, titulo, &f_grande, C_TEXTO);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, PANT_W - 172);
  lv_obj_set_pos(t, 160, 12);
}

static lv_obj_t* columna(lv_obj_t* s, int y) {
  lv_obj_t* c = lv_obj_create(s);
  lv_obj_set_pos(c, 0, y);
  lv_obj_set_size(c, PANT_W, PANT_H - y);
  lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, 0, 0);
  lv_obj_set_style_pad_all(c, 8, 0);
  lv_obj_set_style_pad_row(c, 8, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  return c;
}

// Tarjeta de esquinas muy redondeadas (filas de ajustes y de dedos)
static lv_obj_t* tarjeta(lv_obj_t* p, int w, int h) {
  lv_obj_t* f = lv_obj_create(p);
  lv_obj_set_size(f, w, h);
  lv_obj_set_style_bg_color(f, C_SUPERFICIE, 0);
  lv_obj_set_style_border_width(f, 0, 0);
  lv_obj_set_style_radius(f, 22, 0);
  lv_obj_set_style_pad_hor(f, 18, 0);
  lv_obj_remove_flag(f, LV_OBJ_FLAG_SCROLLABLE);
  return f;
}

static lv_obj_t* listaVacia(lv_obj_t* s, int y) {
  lv_obj_t* lista = lv_list_create(s);
  lv_obj_set_pos(lista, 0, y);
  lv_obj_set_size(lista, PANT_W, PANT_H - y);
  lv_obj_set_style_bg_color(lista, C_FONDO, 0);
  lv_obj_set_style_border_width(lista, 0, 0);
  lv_obj_set_style_radius(lista, 0, 0);
  lv_obj_set_style_pad_hor(lista, 8, 0);
  lv_obj_set_style_pad_ver(lista, 6, 0);
  lv_obj_set_style_pad_row(lista, 8, 0);
  return lista;
}

// Fila de lista como tarjeta: ícono o foto, nombre y un dato a la derecha en gris
static lv_obj_t* filaLista(lv_obj_t* lista, const void* icono, const String& txt, const String& dato = "") {
  lv_obj_t* b = lv_list_add_button(lista, icono, txt.c_str());
  tactil(b, true);
  lv_obj_set_style_radius(b, 22, 0);
  lv_obj_set_style_text_font(b, &f_grande, 0);
  lv_obj_set_style_bg_color(b, C_SUPERFICIE, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, C_LINEA, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b, C_TEXTO, 0);
  lv_obj_set_style_pad_hor(b, 12, 0);
  lv_obj_set_style_pad_column(b, 14, 0);
  lv_obj_set_height(b, 62);
  if (dato.length()) {
    lv_obj_t* nombre = lv_obj_get_child(b, -1);
    lv_obj_set_flex_grow(nombre, 1);
    lv_obj_t* d = etiqueta(b, dato.c_str(), &f_texto, C_TENUE);
    lv_obj_set_style_pad_right(d, 6, 0);
  }
  lv_obj_add_event_cb(b, clickSonido, LV_EVENT_CLICKED, nullptr);
  return b;
}

static String dedos(size_t n) {
  if (!n) return "sin dedo";
  return String((unsigned)n) + (n == 1 ? " dedo" : " dedos");
}

// --------------------------------------------- aviso a pantalla completa --
// Tres tonos de la marca: acento (entrada, listo), negro (salida, esperando) y
// blanco (avisos y errores, con el ícono en el acento).
enum class Tono { ACENTO, NEGRO, BLANCO };

static lv_timer_t* capaTimer = nullptr;
static bool cerrarCapaPedido = false;
static bool capaConSaludo = false;  // la capa muestra el saludo de una marca
static Saludo capaSaludo;

static void aCapaSale(void* o, int32_t v) {
  lv_obj_set_style_bg_opa((lv_obj_t*)o, (lv_opa_t)v, 0);
  for (uint32_t i = 0; i < lv_obj_get_child_count((lv_obj_t*)o); i++)  // el contenido (y la nieve, si hay)
    lv_obj_set_style_opa(lv_obj_get_child((lv_obj_t*)o, (int32_t)i), (lv_opa_t)v, 0);
}

static void cerrarCapa(bool suave = true) {
  if (capaTimer) {
    lv_timer_delete(capaTimer);
    capaTimer = nullptr;
  }
  if (capa) {
    if (suave && !capaSaliendo) {
      capaSaliendo = capa;
      lv_obj_remove_flag(capa, LV_OBJ_FLAG_CLICKABLE);
      lv_anim_delete(capa, nullptr);
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, capa);
      lv_anim_set_exec_cb(&a, aCapaSale);
      lv_anim_set_values(&a, lv_obj_get_style_bg_opa(capa, 0), 0);
      lv_anim_set_duration(&a, 260);
      lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
      lv_anim_set_completed_cb(&a, [](lv_anim_t* a) {
        lv_obj_delete((lv_obj_t*)a->var);
        if (capaSaliendo == a->var) capaSaliendo = nullptr;
      });
      lv_anim_start(&a);
    } else {
      lv_obj_delete(capa);
    }
    capa = nullptr;
  }
  capaConSaludo = false;
  led(0, 0, 0);
  soltarFotos = true;
}

static void mostrarCapa(Tono tono, const char* textoCeja, const String& l1, const String& l2, uint32_t ms,
                        const lv_image_dsc_t* foto = nullptr, const char* icono = nullptr) {
  const bool reemplaza = capa != nullptr;  // ya había un aviso: se cambia sin volver a encender el fondo
  cerrarCapa(false);
  const lv_color_t fondo = tono == Tono::ACENTO ? C_ACENTO : tono == Tono::NEGRO ? C_FONDO : C_TEXTO;
  const lv_color_t texto = tono == Tono::BLANCO ? C_TINTA : C_TEXTO;
  const lv_color_t texto2 = tono == Tono::BLANCO ? C_GRIS : tono == Tono::NEGRO ? C_TENUE : C_TEXTO;
  const lv_color_t acento = tono == Tono::ACENTO ? C_TEXTO : C_ACENTO;  // anillo de la foto, círculo del ícono, ceja
  const lv_color_t sobreAcento = tono == Tono::ACENTO ? C_ACENTO : C_TEXTO;

  capa = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(capa, 0, 0);
  lv_obj_set_size(capa, PANT_W, PANT_H);
  lv_obj_set_style_bg_color(capa, fondo, 0);
  lv_obj_set_style_bg_opa(capa, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(capa, 0, 0);
  lv_obj_set_style_border_width(capa, 0, 0);
  lv_obj_set_style_pad_all(capa, 0, 0);
  lv_obj_remove_flag(capa, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(capa, LV_OBJ_FLAG_CLICKABLE);
  if (tema_adorno() != Adorno::NADA && tono != Tono::BLANCO) {  // en el fondo blanco no se verían
    lv_obj_t* copos = lv_obj_create(capa);  // todos juntos: al cerrar se apaga una sola capa
    lv_obj_remove_style_all(copos);
    lv_obj_set_size(copos, PANT_W, PANT_H);
    lv_obj_remove_flag(copos, LV_OBJ_FLAG_CLICKABLE);
    adornos(copos, 16);
  }

  lv_obj_t* cont = lv_obj_create(capa);
  lv_obj_remove_style_all(cont);
  lv_obj_set_size(cont, PANT_W - 32, LV_SIZE_CONTENT);
  lv_obj_center(cont);
  lv_obj_set_style_pad_row(cont, 8, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  // Foto dentro de un anillo, o un ícono dentro de un círculo
  lv_obj_t* redondo = nullptr;
  if (foto || icono) {
    redondo = lv_obj_create(cont);
    lv_obj_remove_style_all(redondo);
    const int d = foto ? FOTO_GRANDE + 12 : 92;
    lv_obj_set_size(redondo, d, d);
    lv_obj_set_style_radius(redondo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(redondo, acento, 0);
    lv_obj_set_style_bg_opa(redondo, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_bottom(redondo, 4, 0);
    centrarPivote(redondo);
    lv_obj_remove_flag(redondo, LV_OBJ_FLAG_CLICKABLE);
    if (foto) {
      lv_obj_t* img = lv_image_create(redondo);
      lv_image_set_src(img, foto);
      lv_obj_center(img);
    } else {
      lv_obj_t* l = etiqueta(redondo, icono, &f_titulo, sobreAcento);
      lv_obj_center(l);
    }
  }
  if (textoCeja && *textoCeja) ceja(cont, textoCeja, acento, sobreAcento);

  // Un nombre largo con letra gigante no cabe: va un poco más chico
  const lv_font_t* f1 = lv_text_get_width(l1.c_str(), l1.length(), &f_titulo, 0) > PANT_W - 40 ? &f_grande : &f_titulo;
  lv_obj_t* t = etiqueta(cont, l1.c_str(), f1, texto);
  lv_obj_set_width(t, PANT_W - 40);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  if (l2.length()) {
    lv_obj_t* l = etiqueta(cont, l2.c_str(), &f_fino, texto2);
    lv_obj_set_width(l, PANT_W - 40);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  }

  // Entrada: el color se enciende, el contenido sube y la foto rebota
  if (!reemplaza) animar(capa, aFondoOpa, 0, 255, 200);
  lv_obj_set_style_opa(cont, LV_OPA_TRANSP, 0);
  animar(cont, aOpa, 0, 255, 260, reemplaza ? 0 : 70);
  animar(cont, aSubir, 22, 0, 380, reemplaza ? 0 : 70);
  if (redondo) animar(redondo, aEscala, 170, 256, 520, reemplaza ? 0 : 60, lv_anim_path_overshoot);

  lv_obj_add_event_cb(capa, [](lv_event_t*) { cerrarCapaPedido = true; }, LV_EVENT_CLICKED, nullptr);
  capaTimer = lv_timer_create([](lv_timer_t*) { cerrarCapaPedido = true; }, ms, nullptr);
  lv_display_trigger_activity(NULL);
}

static bool sinInternet() {
  if (panel_listo()) return false;
  mostrarCapa(Tono::BLANCO, "", "Necesita internet",
              ajustes().panelLlave.length() ? red_estado() : String("Falta la llave del panel (reloj.local)"), 3000,
              nullptr, LV_SYMBOL_WIFI);
  sonido_aviso();
  return true;
}

// Ventana de confirmación (Sí / No)
static Destino alConfirmar = nullptr;
static lv_obj_t* modal = nullptr;

static void confirmar(const String& texto, const char* txtSi, Destino si) {
  alConfirmar = si;
  modal = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(modal, 0, 0);
  lv_obj_set_size(modal, PANT_W, PANT_H);
  lv_obj_set_style_bg_color(modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(modal, LV_OPA_70, 0);
  lv_obj_set_style_border_width(modal, 0, 0);
  lv_obj_set_style_radius(modal, 0, 0);
  lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
  animar(modal, aFondoOpa, 0, LV_OPA_70, 200);
  lv_obj_t* caja = lv_obj_create(modal);
  lv_obj_set_size(caja, 432, 220);
  lv_obj_center(caja);
  lv_obj_set_style_bg_color(caja, C_SUPERFICIE, 0);
  lv_obj_set_style_border_width(caja, 0, 0);
  lv_obj_set_style_radius(caja, 28, 0);
  lv_obj_set_style_pad_all(caja, 20, 0);
  lv_obj_remove_flag(caja, LV_OBJ_FLAG_SCROLLABLE);
  aparecer(caja, 40);
  lv_obj_t* l = etiqueta(caja, texto.c_str(), &f_grande, C_TEXTO);
  lv_obj_set_width(l, 392);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t* no = boton(caja, "Cancelar", C_LINEA, [](lv_event_t*) {
    lv_obj_delete_async(modal);
    modal = nullptr;
  });
  lv_obj_set_size(no, 186, 56);
  lv_obj_align(no, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t* si2 = boton(caja, txtSi, C_ACENTO, [](lv_event_t*) {
    lv_obj_delete_async(modal);
    modal = nullptr;
    ir(alConfirmar);
  });
  lv_obj_set_size(si2, 186, 56);
  lv_obj_align(si2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

// Teclado con ñ (para la clave del WiFi)
static const char* const kbMin[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
                                    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "ñ", "\n",
                                    "1#", "z", "x", "c", "v", "b", "n", "m", ",", ".", "-", "\n",
                                    LV_SYMBOL_CLOSE, " ", LV_SYMBOL_OK, ""};
static const char* const kbMay[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
                                    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ñ", "\n",
                                    "1#", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "-", "\n",
                                    LV_SYMBOL_CLOSE, " ", LV_SYMBOL_OK, ""};
#define K(x) ((lv_buttonmatrix_ctrl_t)(x))
#define K_ESP K(LV_BUTTONMATRIX_CTRL_CHECKED | 6)
#define K_TEC K(4)
static const lv_buttonmatrix_ctrl_t kbCtrl[] = {
    K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_ESP,
    K_ESP, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC,
    K_ESP, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC, K_TEC,
    K(LV_BUTTONMATRIX_CTRL_CHECKED | 4), K(12), K(LV_BUTTONMATRIX_CTRL_CHECKED | 4)};

static lv_obj_t* teclado(lv_obj_t* s, lv_obj_t* ta) {
  lv_obj_t* kb = lv_keyboard_create(s);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, kbMin, kbCtrl);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, kbMay, kbCtrl);
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_set_size(kb, PANT_W, 212);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(kb, C_FONDO, 0);
  lv_obj_set_style_text_font(kb, &f_texto, LV_PART_ITEMS);
  lv_obj_set_style_radius(kb, 12, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, C_SUPERFICIE, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, C_LINEA, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(kb, C_ACENTO, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(kb, C_TEXTO, LV_PART_ITEMS);
  lv_obj_add_event_cb(kb, clickSonido, LV_EVENT_VALUE_CHANGED, nullptr);
  return kb;
}

static lv_obj_t* campoTexto(lv_obj_t* s, const char* placeholder, const String& valor) {
  lv_obj_t* ta = lv_textarea_create(s);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_textarea_set_max_length(ta, 40);
  lv_textarea_set_text(ta, valor.c_str());
  lv_obj_set_size(ta, PANT_W - 16, 52);
  lv_obj_set_pos(ta, 8, 56);
  lv_obj_set_style_text_font(ta, &f_grande, 0);
  lv_obj_set_style_bg_color(ta, C_SUPERFICIE, 0);
  lv_obj_set_style_radius(ta, 18, 0);
  lv_obj_set_style_border_color(ta, C_ACENTO, LV_STATE_FOCUSED);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
  return ta;
}

// Declaraciones de las pantallas
static void irInicio();
static void mostrarSorpresa();
static void irPin();
static void irMenu();
static void irPersonas();
static void irPersona();
static void irRegistro();
static void irMarcas();
static void irWifi();
static void irWifiClave();
static void irAjustes();
static void irInfo();

// ============================================================== ARRANQUE ==
// El logo aparece y crece un poco al encender; después entra la pantalla de inicio
static void irArranque() {
  lv_obj_t* s = nuevaPantalla();
  lv_obj_t* logo = lv_image_create(s);
  lv_image_set_src(logo, &logo_grande);
  lv_image_set_pivot(logo, logo_grande.header.w / 2, logo_grande.header.h / 2);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -14);
  lv_obj_set_style_image_opa(logo, LV_OPA_TRANSP, 0);
  animar(logo, aImagenOpa, 0, 255, 500, 100);
  animar(logo, aImagenEscala, 190, 256, 800, 100, lv_anim_path_overshoot);
  lv_obj_t* l = etiqueta(s, "Reloj control", &f_fino, C_TENUE);
  lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -30);
  aparecer(l, 500);
  cambiar(s, Vista::ARRANQUE);
  lv_timer_t* t = lv_timer_create([](lv_timer_t*) { ir(irInicio); }, 1700, nullptr);
  lv_timer_set_repeat_count(t, 1);
}

// ================================================================ INICIO ==
// Al cambiar un número, el nuevo baja desde arriba y se enciende
static void cambiarNumero(lv_obj_t* l, const char* t, bool animado) {
  if (!l || !strcmp(lv_label_get_text(l), t)) return;
  lv_label_set_text(l, t);
  if (!animado) return;
  animar(l, aSubir, -22, 0, 420);
  animar(l, aTextoOpa, 0, 255, 360);
}

static String temaEnInicio;  // con qué tema se armó la pantalla de inicio (vacío = ninguno)

static void refrescarInicio(bool forzar) {
  if (vista != Vista::INICIO || !lblHH) return;
  if (tema_nombre() != temaEnInicio) {  // llegó la hora o cambió el día: el tema entra, sale o cambia
    ir(irInicio);
    return;
  }
  static int ultimoMinuto = -1;
  static bool ultimaValida = false;
  const time_t ahora = time(nullptr);
  struct tm tm;
  localtime_r(&ahora, &tm);
  if (forzar || tm.tm_min != ultimoMinuto || hora_valida() != ultimaValida) {
    ultimoMinuto = tm.tm_min;
    ultimaValida = hora_valida();
    if (hora_valida()) {
      cambiarNumero(lblHH, hora_local(ahora, "%H").c_str(), !forzar);
      cambiarNumero(lblMM, hora_local(ahora, "%M").c_str(), !forzar);
      char f[48];
      snprintf(f, sizeof(f), "%s %d de %s", DIAS[tm.tm_wday], tm.tm_mday, MESES[tm.tm_mon]);
      ponerTexto(lblFecha, f);
    } else {
      cambiarNumero(lblHH, "--", false);
      cambiarNumero(lblMM, "--", false);
      ponerTexto(lblFecha, "");
    }
  }
  // Aviso de abajo: lo más importante primero. Lo que pide acción va en el acento.
  const char* aviso = "Pon tu dedo en el lector";
  lv_color_t color = C_TEXTO;
  const bool lector = huella_conectado();
  if (!lector) {
    aviso = "Lector de huella desconectado";
    color = C_ACENTO;
  } else if (!ajustes().panelLlave.length()) {
    aviso = "Falta conectar el panel: entra a " LV_SYMBOL_SETTINGS;
    color = C_ACENTO;
  }
  ponerTexto(lblAviso, aviso);
  ponerColorTexto(lblAviso, color);
  // Sin lector la onda se detiene y el punto queda gris: no hay nada escuchando
  if (ondaInicio) {
    mostrarObj(ondaInicio, lector);
    ponerColorFondo(puntoInicio, lector ? C_ACENTO : C_LINEA);
  }

  // Arriba a la derecha: conexión con el panel y marcas guardadas por mandar
  if (!ajustes().ssid.length()) {
    ponerTexto(lblWifi, "");
  } else {
    ponerTexto(lblWifi, LV_SYMBOL_WIFI);
    ponerColorTexto(lblWifi, panel_conectado() ? C_TENUE : C_ACENTO);
  }
  ponerTexto(lblOficina, ("OFICINA " + ajustes().oficina).c_str());  // se cambia en reloj.local
  const size_t n = panel_en_cola();
  ponerTexto(lblCola, n ? (String((unsigned)n) + " por enviar").c_str() : "");
  mostrarObj(chipCola, n > 0);
}

static void irInicio() {
  lv_obj_t* s = nuevaPantalla();
  temaEnInicio = tema_nombre();
  adornos(s, 30);
  if (tema_luces()) luces(s);
  lv_obj_t* logo = lv_image_create(s);
  lv_image_set_src(logo, &logo_chico);
  lv_obj_set_pos(logo, 18, 14);
  // Dónde está este reloj (Oficina A o B), chico bajo el logo
  lblOficina = etiqueta(s, "", &f_chico, C_TENUE);
  lv_obj_set_style_text_letter_space(lblOficina, 2, 0);
  lv_obj_set_pos(lblOficina, 22, 66);

  lblWifi = etiqueta(s, "", &lv_font_montserrat_20, C_TENUE);
  lv_obj_align(lblWifi, LV_ALIGN_TOP_RIGHT, -76, 26);
  lv_obj_t* eng = lv_button_create(s);
  tactil(eng);
  lv_obj_set_size(eng, 56, 56);
  lv_obj_align(eng, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_set_style_bg_color(eng, C_SUPERFICIE, 0);
  lv_obj_t* el = etiqueta(eng, LV_SYMBOL_SETTINGS, &lv_font_montserrat_20, C_TENUE);
  lv_obj_center(el);
  lv_obj_add_event_cb(eng, clickSonido, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(eng, irDesdeEvento, LV_EVENT_CLICKED, (void*)irPin);
  chipCola = ceja(s, "", C_ACENTO, C_TEXTO);
  lblCola = lv_obj_get_child(chipCola, 0);
  lv_obj_set_style_text_letter_space(lblCola, 0, 0);
  lv_obj_align(chipCola, LV_ALIGN_TOP_RIGHT, -12, 74);
  lv_obj_add_flag(chipCola, LV_OBJ_FLAG_HIDDEN);

  // Hora: horas y minutos aparte para que solo se mueva lo que cambia; los dos puntos respiran
  lv_obj_t* hora = lv_obj_create(s);
  lv_obj_remove_style_all(hora);
  lv_obj_set_size(hora, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(hora, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hora, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(hora, 4, 0);
  lv_obj_align(hora, LV_ALIGN_CENTER, 0, -6);
  // Sorpresa escondida del tema: tocar la hora 5 veces seguidas
  lv_obj_add_event_cb(hora, [](lv_event_t*) {
    static uint32_t ultimo = 0;
    static uint8_t toques = 0;
    toques = lv_tick_elaps(ultimo) < 700 ? toques + 1 : 1;
    ultimo = lv_tick_get();
    if (toques >= 5) {
      toques = 0;
      mostrarSorpresa();
    }
  }, LV_EVENT_CLICKED, nullptr);
  lblHH = etiqueta(hora, "--", &f_reloj, C_TEXTO);
  lv_obj_t* dp = etiqueta(hora, ":", &f_reloj, C_ACENTO);
  lv_obj_set_style_translate_y(dp, -6, 0);
  lblMM = etiqueta(hora, "--", &f_reloj, C_TEXTO);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, dp);
  lv_anim_set_exec_cb(&a, aTextoOpa);
  lv_anim_set_values(&a, 255, 70);
  lv_anim_set_duration(&a, 1000);
  lv_anim_set_playback_duration(&a, 1000);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);

  lblFecha = etiqueta(s, "", &f_fino, C_TENUE);
  lv_obj_align(lblFecha, LV_ALIGN_CENTER, 0, 62);

  // Cápsula de abajo: punto acento con su onda + la instrucción
  lv_obj_t* pill = lv_obj_create(s);
  lv_obj_remove_style_all(pill);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, 52);
  lv_obj_set_style_bg_color(pill, C_SUPERFICIE, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_left(pill, 20, 0);
  lv_obj_set_style_pad_right(pill, 24, 0);
  lv_obj_set_style_pad_column(pill, 16, 0);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* sop = lv_obj_create(pill);
  lv_obj_remove_style_all(sop);
  lv_obj_set_size(sop, 14, 14);
  lv_obj_add_flag(sop, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  ondaInicio = onda(sop, C_ACENTO, 14, 40, 1600);
  puntoInicio = lv_obj_create(sop);
  lv_obj_remove_style_all(puntoInicio);
  lv_obj_set_size(puntoInicio, 14, 14);
  lv_obj_set_style_radius(puntoInicio, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(puntoInicio, C_ACENTO, 0);
  lv_obj_set_style_bg_opa(puntoInicio, LV_OPA_COVER, 0);
  lv_obj_center(puntoInicio);
  lblAviso = etiqueta(pill, "", &f_texto, C_TEXTO);

  cambiar(s, Vista::INICIO);
  refrescarInicio(true);
}

static void mostrarSorpresa() {
  bool conTono;
  const String texto = tema_sorpresa(conTono);
  if (!texto.length()) return;
  mostrarCapa(Tono::ACENTO, "", texto, "", 4000);
  if (conTono) sonido_especial();
  else sonido_ok();
}

// «Hola Ana» / «Nos vemos Ana» / «Ya marcaste, Ana» / «No te conozco»
static void mostrarSaludo(const Saludo& s) {
  if (!s.nombre.length()) {
    mostrarCapa(Tono::BLANCO, "", "No te conozco", "Pide que registren tu dedo", 3500, nullptr, "?");
    sonido_error();
    led(90, 0, 0);
  } else if (s.repetida) {
    mostrarCapa(Tono::BLANCO, "", "Ya marcaste, " + s.nombre, "", 3500, foto_imagen(s.perfil, FOTO_GRANDE));
    sonido_aviso();
    led(90, 50, 0);
  } else {
    const bool ent = s.tipo != "SALIDA";
    bool conTono;
    String texto = tema_saludo(ent, s.nombre, s.id, conTono);  // Navidad y otros: frases que rotan y sorpresas
    if (!texto.length()) texto = (ent ? "Hola " : "Nos vemos ") + s.nombre;
    mostrarCapa(ent ? Tono::ACENTO : Tono::NEGRO, ent ? "ENTRADA" : "SALIDA", texto, s.hora, 3500,
                foto_imagen(s.perfil, FOTO_GRANDE));
    if (conTono) sonido_especial();
    else if (ent) sonido_ok();
    else sonido_salida();
    led(0, ent ? 90 : 0, ent ? 0 : 90);
  }
  capaConSaludo = true;
  capaSaludo = s;
}

// El panel contestó algo distinto a lo que se supuso: se corrige si sigue a la vista
static void corregirSaludo(const Saludo& r) {
  if (!capa || !capaConSaludo || capaSaludo.id != r.id) return;
  if (capaSaludo.nombre == r.nombre && capaSaludo.tipo == r.tipo && capaSaludo.repetida == r.repetida) return;
  Saludo s = r;
  if (!s.hora.length()) s.hora = capaSaludo.hora;
  mostrarSaludo(s);
}

// =================================================================== CLAVE ==
#define PIN_MAX 6
static String pinTxt, pinPrimera;

// Seis puntos: los escritos se llenan del acento con un rebote
static void mostrarPuntos(bool nuevo = false) {
  if (!puntos) return;
  for (int i = 0; i < PIN_MAX; i++) {
    lv_obj_t* p = lv_obj_get_child(puntos, i);
    const bool lleno = i < (int)pinTxt.length();
    lv_obj_set_style_bg_opa(p, lleno ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, lleno ? 0 : 2, 0);
    if (nuevo && i == (int)pinTxt.length() - 1) rebotar(p);
  }
}

static void pinMal(const char* msg) {
  ponerTexto(lblPinMsg, msg);
  lv_obj_set_style_text_color(lblPinMsg, C_ACENTO, 0);
  sacudir(puntos);
}

static void confirmarPin() {
  switch (pinModo) {
    case PinModo::ENTRAR:
      if (pinTxt == ajustes().pin) {
        ir(irMenu);
        return;
      }
      pinMal("Clave incorrecta");
      sonido_error();
      break;
    case PinModo::NUEVA1:
      if (pinTxt.length() < 4) {
        pinMal("Usa al menos 4 números");
        break;
      }
      pinPrimera = pinTxt;
      pinModo = PinModo::NUEVA2;
      ir(irPin);
      return;
    case PinModo::NUEVA2:
      if (pinTxt != pinPrimera) {
        pinModo = PinModo::NUEVA1;
        mostrarCapa(Tono::BLANCO, "", "No coinciden", "Escribe la clave nueva otra vez", 2500, nullptr, "!");
        ir(irPin);
        return;
      }
      ajustes().pin = pinTxt;
      ajustes_guardar();
      pinModo = PinModo::ENTRAR;
      mostrarCapa(Tono::ACENTO, "", "Listo", "Clave cambiada", 2000, nullptr, LV_SYMBOL_OK);
      ir(irAjustes);
      return;
  }
  pinTxt = "";
  mostrarPuntos();
}

static void irPin() {
  lv_obj_t* s = nuevaPantalla();
  const char* titulo = pinModo == PinModo::ENTRAR ? "Clave de administrador"
                       : pinModo == PinModo::NUEVA1 ? "Clave nueva"
                                                    : "Repite la clave nueva";
  cabecera(s, titulo, pinModo == PinModo::ENTRAR ? irInicio : irAjustes);
  pinTxt = "";
  puntos = lv_obj_create(s);
  lv_obj_remove_style_all(puntos);
  lv_obj_set_size(puntos, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(puntos, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(puntos, 12, 0);
  lv_obj_set_style_pad_all(puntos, 6, 0);
  lv_obj_set_pos(puntos, 18, 110);
  for (int i = 0; i < PIN_MAX; i++) {
    lv_obj_t* p = lv_obj_create(puntos);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, 18, 18);
    lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(p, C_ACENTO, 0);
    lv_obj_set_style_border_color(p, C_LINEA, 0);
    centrarPivote(p);
  }
  mostrarPuntos();
  lblPinMsg = etiqueta(s, "", &f_texto, C_TENUE);
  lv_obj_set_width(lblPinMsg, 200);
  lv_label_set_long_mode(lblPinMsg, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(lblPinMsg, 24, 160);

  static const char* const mapa[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                                     LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};
  lv_obj_t* bm = lv_buttonmatrix_create(s);
  lv_buttonmatrix_set_map(bm, mapa);
  lv_obj_set_size(bm, 252, 258);
  lv_obj_align(bm, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
  lv_obj_set_style_text_font(bm, &f_grande, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bm, 0, 0);
  lv_obj_set_style_pad_all(bm, 4, 0);
  lv_obj_set_style_pad_gap(bm, 10, 0);
  lv_obj_set_style_radius(bm, LV_RADIUS_CIRCLE, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(bm, C_SUPERFICIE, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(bm, C_ACENTO, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(bm, C_TEXTO, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(bm, 0, LV_PART_ITEMS);
  lv_obj_add_event_cb(bm, [](lv_event_t* e) {
    lv_obj_t* bm = (lv_obj_t*)lv_event_get_target(e);
    const uint32_t id = lv_buttonmatrix_get_selected_button(bm);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char* t = lv_buttonmatrix_get_button_text(bm, id);
    sonido_tecla();
    if (!strcmp(t, LV_SYMBOL_OK)) {
      confirmarPin();
      return;
    }
    bool nuevo = false;
    if (!strcmp(t, LV_SYMBOL_BACKSPACE)) {
      if (pinTxt.length()) pinTxt.remove(pinTxt.length() - 1);
    } else if (pinTxt.length() < PIN_MAX) {
      pinTxt += t;
      nuevo = true;
    }
    mostrarPuntos(nuevo);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  cambiar(s, Vista::PIN);
}

// ==================================================================== MENÚ ==
static bool registrando = false;  // la lista de personas es para registrarle un dedo
static bool listaFresca = false;  // ya llegó la lista del panel en esta visita
static String listaError;
static String selPerfil;          // perfil elegido
static std::vector<String> idsLista;
static String firmaEnPantalla;    // lo que mostraba la lista o la ficha al armarla

// Nombres, fotos y dedos de todos: si no cambió, no hay que rearmar la pantalla
static String firmaPersonas() {
  String f;
  for (const Perfil& p : panel_perfiles()) {
    f += p.perfil + '|' + p.nombre + '|' + p.foto + '|';
    for (uint16_t h : p.huellas) f += String(h) + ',';
    f += ';';
  }
  return f;
}

static void abrirPersonas(bool paraRegistrar) {
  if (paraRegistrar && sinInternet()) return;
  registrando = paraRegistrar;
  listaFresca = false;
  listaError = "";
  if (panel_listo()) panel_pedir_perfiles();
  irPersonas();
}

static void irMenu() {
  lv_obj_t* s = nuevaPantalla();
  cabecera(s, "Administración", irInicio, LV_SYMBOL_LEFT "  Salir");
  String estado = String((unsigned)panel_perfiles().size()) + " personas  ·  " + panel_estado();
  if (panel_en_cola()) estado += "  ·  " + String((unsigned)panel_en_cola()) + " por enviar";
  lv_obj_t* e = etiqueta(s, estado.c_str(), &f_chico, C_TENUE);
  lv_label_set_long_mode(e, LV_LABEL_LONG_DOT);
  lv_obj_set_width(e, PANT_W - 32);
  lv_obj_set_pos(e, 16, 60);

  lv_obj_t* g = lv_obj_create(s);
  lv_obj_set_pos(g, 0, 82);
  lv_obj_set_size(g, PANT_W, PANT_H - 82);
  lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g, 0, 0);
  lv_obj_set_style_pad_all(g, 8, 0);
  lv_obj_set_style_pad_gap(g, 8, 0);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);

  struct Item {
    const char* txt;
    lv_color_t color;
    Destino d;
  };
  const Item items[] = {
      {LV_SYMBOL_PLUS "  Registrar dedo", C_ACENTO, [] { abrirPersonas(true); }},
      {LV_SYMBOL_LIST "  Personas", C_SUPERFICIE, [] { abrirPersonas(false); }},
      {LV_SYMBOL_EYE_OPEN "  Marcas de hoy", C_SUPERFICIE, irMarcas},
      {LV_SYMBOL_WIFI "  WiFi", C_SUPERFICIE, irWifi},
      {LV_SYMBOL_SETTINGS "  Ajustes", C_SUPERFICIE, irAjustes},
      {LV_SYMBOL_HOME "  Salir", C_SUPERFICIE, irInicio},
  };
  uint32_t espera = 120;
  for (const Item& it : items) {
    lv_obj_t* b = botonIr(g, it.txt, it.color, it.d);
    lv_obj_set_size(b, 228, 70);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &f_texto, 0);
    aparecer(b, espera);  // las tarjetas llegan una tras otra
    espera += 35;
  }
  cambiar(s, Vista::MENU);
}

// ============================================================== PERSONAS ==
// Los perfiles del panel. Para registrar se espera la lista recién bajada (así el
// número de huella libre sale de lo que el panel sabe hoy).
static void irPersonas() {
  lv_obj_t* s = nuevaPantalla();
  const auto& v = panel_perfiles();
  firmaEnPantalla = firmaPersonas();
  const String titulo = registrando ? String("Elige a la persona") : "Personas (" + String((unsigned)v.size()) + ")";
  cabecera(s, titulo.c_str(), irMenu);
  idsLista.clear();
  const char* aviso = nullptr;
  if (registrando && !listaFresca) aviso = listaError.length() ? listaError.c_str() : "Bajando la lista del panel...";
  else if (v.empty()) aviso = listaError.length() ? listaError.c_str() : "No hay perfiles en el panel.";
  if (aviso) {
    if (!listaError.length() && registrando && !listaFresca) {
      lv_obj_t* sop = lv_obj_create(s);  // mientras baja, la misma onda del inicio
      lv_obj_remove_style_all(sop);
      lv_obj_set_size(sop, 16, 16);
      lv_obj_add_flag(sop, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
      lv_obj_align(sop, LV_ALIGN_CENTER, 0, -56);
      onda(sop, C_ACENTO, 16, 56, 1400);
      lv_obj_t* p = lv_obj_create(sop);
      lv_obj_remove_style_all(p);
      lv_obj_set_size(p, 16, 16);
      lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(p, C_ACENTO, 0);
      lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
      lv_obj_center(p);
    }
    lv_obj_t* l = etiqueta(s, aviso, &f_grande, listaError.length() ? C_ACENTO : C_TENUE);
    lv_obj_set_width(l, PANT_W - 40);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 4);
    if (listaError.length()) {
      lv_obj_t* b = boton(s, LV_SYMBOL_REFRESH "  Reintentar", C_SUPERFICIE, [](lv_event_t*) { ir([] { abrirPersonas(registrando); }); });
      lv_obj_set_size(b, 220, 56);
      lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -16);
    }
  } else {
    int y = 58;
    if (!registrando && listaError.length()) {  // sin internet: se ve la lista guardada
      lv_obj_t* n = etiqueta(s, ("Lista guardada · " + listaError).c_str(), &f_chico, C_ACENTO);
      lv_obj_set_pos(n, 16, 58);
      y = 80;
    }
    lv_obj_t* lista = listaVacia(s, y);
    for (const Perfil& p : v) {
      idsLista.push_back(p.perfil);
      lv_obj_t* b = filaLista(lista, foto_imagen(p.perfil, FOTO_CHICA), p.nombre, dedos(p.huellas.size()));
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        const size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e);
        if (i >= idsLista.size()) return;
        selPerfil = idsLista[i];
        ir(registrando ? irRegistro : irPersona);
      }, LV_EVENT_CLICKED, (void*)(uintptr_t)(idsLista.size() - 1));
    }
  }
  cambiar(s, Vista::PERSONAS);
}

// Ficha de una persona: sus dedos, agregar y borrar
static uint16_t selHuella = 0, borrandoHuella = 0;

static void borrarDedo() {
  if (sinInternet()) return;
  borrandoHuella = selHuella;
  panel_soltar(selHuella);
  mostrarCapa(Tono::NEGRO, "", "Borrando...", "Dedo N° " + String(selHuella), 20000);
}

static void irPersona() {
  const Perfil* p = panel_buscar(selPerfil);
  if (!p) {
    irPersonas();
    return;
  }
  lv_obj_t* s = nuevaPantalla();
  firmaEnPantalla = firmaPersonas();
  cabecera(s, p->nombre.c_str(), irPersonas);
  lv_image_set_src(lv_image_create(s), foto_imagen(p->perfil, FOTO_CHICA));
  lv_obj_set_pos(lv_obj_get_child(s, -1), 18, 60);
  const size_t n = p->huellas.size();
  const String cuantos = !n ? String("Sin dedos registrados") : dedos(n) + (n == 1 ? " registrado" : " registrados");
  lv_obj_t* l = etiqueta(s, cuantos.c_str(), &f_texto, C_TENUE);
  lv_obj_set_pos(l, 76, 70);

  lv_obj_t* c = columna(s, 110);
  lv_obj_set_size(botonIr(c, LV_SYMBOL_PLUS "  Agregar dedo", C_ACENTO, [] {
                    if (!sinInternet()) irRegistro();
                  }), PANT_W - 16, 56);
  for (uint16_t h : p->huellas) {
    lv_obj_t* f = tarjeta(c, PANT_W - 16, 60);
    lv_obj_t* t = etiqueta(f, ("Dedo N° " + String(h)).c_str(), &f_grande, C_TEXTO);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* b = boton(f, LV_SYMBOL_TRASH "  Borrar", C_LINEA, [](lv_event_t* e) {
      selHuella = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
      const Perfil* p = panel_buscar(selPerfil);
      confirmar("¿Borrar el dedo N° " + String(selHuella) + " de " + (p ? p->nombre : selPerfil) + "?", "Borrar", borrarDedo);
    }, (void*)(uintptr_t)h, &f_texto);
    lv_obj_set_size(b, 132, 44);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, 10, 0);
  }
  cambiar(s, Vista::PERSONA);
}

// ====================================================== REGISTRO DE HUELLA ==
// Un círculo grande con el número del paso; la onda late mientras espera el dedo.
// Al terminar, el círculo sube, muestra ✓ o ! y aparecen los botones.
static uint16_t regHuella = 0;
static bool regGuardando = false;  // esperando que el panel guarde la huella

enum class Circulo { ESPERA, LLENO, OK, MAL };

static String nombreSel() {
  const Perfil* p = panel_buscar(selPerfil);
  return p ? p->nombre : selPerfil;
}

static Circulo modoPaso = Circulo::ESPERA;

static void ponerPaso(const char* paso, const char* centro, const char* instr, Circulo modo) {
  if (!lblPaso) return;
  const bool cambio = strcmp(lv_label_get_text(lblCirculo), centro) != 0 || modo != modoPaso;
  modoPaso = modo;
  ponerTexto(lblPaso, paso);
  ponerTexto(lblCirculo, centro);
  ponerTexto(lblInstr, instr);
  lv_obj_set_style_bg_color(circuloPaso, modo == Circulo::ESPERA ? C_SUPERFICIE : modo == Circulo::MAL ? C_TEXTO : C_ACENTO, 0);
  lv_obj_set_style_border_width(circuloPaso, modo == Circulo::ESPERA ? 3 : 0, 0);
  lv_obj_set_style_text_color(lblCirculo, modo == Circulo::MAL ? C_TINTA : C_TEXTO, 0);
  if (modo == Circulo::ESPERA) lv_obj_remove_flag(ondaPaso, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(ondaPaso, LV_OBJ_FLAG_HIDDEN);
  if (cambio) rebotar(circuloPaso);
}

static void volverDeRegistro() {
  if (registrando) irPersonas();
  else irPersona();
}

static void finRegistro(bool ok, const String& texto) {
  regGuardando = false;
  ponerPaso("", ok ? LV_SYMBOL_OK : "!", texto.c_str(), ok ? Circulo::OK : Circulo::MAL);
  lv_obj_add_flag(cejaPaso, LV_OBJ_FLAG_HIDDEN);
  // El círculo sube para dejar lugar a los botones; un texto largo va más chico
  animar(soportePaso, aY, 92, 56, 380);
  const bool largo = lv_text_get_width(texto.c_str(), texto.length(), &f_grande, 0) > 2 * (PANT_W - 48);
  lv_obj_set_style_text_font(lblInstr, largo ? &f_texto : &f_grande, 0);
  lv_obj_align(lblInstr, LV_ALIGN_TOP_MID, 0, 166);
  aparecer(lblInstr, 80);
  lv_obj_remove_flag(filaFin, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clean(filaFin);
  lv_obj_t* b1;
  lv_obj_t* b2;
  if (ok) {
    sonido_ok();
    led(0, 90, 0);
    b1 = botonIr(filaFin, "Registrar otro", C_ACENTO, [] { abrirPersonas(true); });
    b2 = botonIr(filaFin, "Listo", C_SUPERFICIE, registrando ? irMenu : irPersona);
  } else {
    sonido_error();
    led(90, 0, 0);
    b1 = botonIr(filaFin, "Intentar de nuevo", C_ACENTO, irRegistro);
    b2 = botonIr(filaFin, "Volver", C_SUPERFICIE, volverDeRegistro);
  }
  lv_obj_set_size(b1, 220, 56);
  lv_obj_set_size(b2, 220, 56);
  aparecer(filaFin, 160);
}

static void irRegistro() {
  lv_obj_t* s = nuevaPantalla();
  cabecera(s, nombreSel().c_str(), volverDeRegistro, LV_SYMBOL_CLOSE "  Cancelar");
  cejaPaso = ceja(s, "", C_ACENTO, C_TEXTO);
  lblPaso = lv_obj_get_child(cejaPaso, 0);
  lv_obj_align(cejaPaso, LV_ALIGN_TOP_MID, 0, 64);

  soportePaso = lv_obj_create(s);  // tamaño fijo: el círculo y la onda crecen desde el centro
  lv_obj_remove_style_all(soportePaso);
  lv_obj_set_size(soportePaso, 120, 120);
  lv_obj_add_flag(soportePaso, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_remove_flag(soportePaso, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(soportePaso, LV_ALIGN_TOP_MID, 0, 92);
  ondaPaso = onda(soportePaso, C_ACENTO, 84, 124, 1500);
  circuloPaso = lv_obj_create(soportePaso);
  lv_obj_remove_style_all(circuloPaso);
  lv_obj_set_size(circuloPaso, 84, 84);
  lv_obj_set_style_radius(circuloPaso, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(circuloPaso, C_SUPERFICIE, 0);
  lv_obj_set_style_bg_opa(circuloPaso, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(circuloPaso, C_ACENTO, 0);
  lv_obj_set_style_border_width(circuloPaso, 3, 0);
  centrarPivote(circuloPaso);
  lv_obj_center(circuloPaso);
  lblCirculo = etiqueta(circuloPaso, "", &f_titulo, C_TEXTO);
  lv_obj_center(lblCirculo);

  lblInstr = etiqueta(s, "", &f_grande, C_TEXTO);
  lv_obj_set_width(lblInstr, PANT_W - 48);
  lv_obj_set_style_text_align(lblInstr, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lblInstr, LV_LABEL_LONG_WRAP);
  lv_obj_align(lblInstr, LV_ALIGN_TOP_MID, 0, 226);
  filaFin = lv_obj_create(s);
  lv_obj_set_size(filaFin, PANT_W, 76);
  lv_obj_align(filaFin, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(filaFin, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(filaFin, 0, 0);
  lv_obj_set_flex_flow(filaFin, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(filaFin, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(filaFin, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(filaFin, LV_OBJ_FLAG_HIDDEN);
  cambiar(s, Vista::REGISTRO);
  regGuardando = false;

  const int libre = panel_huella_libre(huella_capacidad());
  if (!panel_listo()) {
    finRegistro(false, "Necesita internet");
  } else if (!huella_conectado()) {
    finRegistro(false, "El lector de huella no está conectado");
  } else if (libre < 0) {
    finRegistro(false, "La memoria del lector está llena");
  } else {
    huella_enrolar((uint16_t)libre);
    ponerPaso("PASO 1 DE 2", "1", "Pon el dedo en el lector", Circulo::ESPERA);
    led(0, 0, 40);
  }
}

static void eventoRegistro(const HuellaEv& ev) {
  if (!lblPaso || regGuardando) return;
  switch (ev.ev) {
    case HEv::ENR_PON_DEDO:
      if (ev.paso == 1) ponerPaso("PASO 1 DE 2", "1", "Pon el dedo en el lector", Circulo::ESPERA);
      else ponerPaso("PASO 2 DE 2", "2", "Pon el mismo dedo otra vez", Circulo::ESPERA);
      led(0, 0, 40);
      break;
    case HEv::ENR_SACA_DEDO:
      ponerPaso("PASO 1 DE 2", "1", "Bien. Retira el dedo", Circulo::LLENO);
      sonido_tecla();
      break;
    case HEv::IMAGEN_MALA:
      ponerTexto(lblInstr, "No se leyó bien. Apoya el dedo plano y quieto");
      sacudir(circuloPaso);
      sonido_aviso();
      break;
    case HEv::ENR_OK:
      // Quedó en el lector; ahora el panel tiene que anotar de quién es
      regHuella = ev.id;
      regGuardando = true;
      ponerPaso("GUARDANDO", "···", "Anotando el dedo en el panel...", Circulo::LLENO);
      panel_asignar(ev.id, selPerfil);
      break;
    case HEv::ENR_DUPLICADA: {
      const Perfil* dueno = panel_por_huella(ev.id);
      if (dueno) {
        finRegistro(false, "Ese dedo ya está registrado a nombre de " + dueno->nombre);
      } else {
        huella_borrar(ev.id);  // restos de una huella sin perfil
        finRegistro(false, "Había una huella antigua sin dueño en el lector y ya se borró. Intenta de nuevo.");
      }
      break;
    }
    case HEv::ENR_ERROR:
      finRegistro(false, ev.msg);
      break;
    default:
      break;
  }
}

// ========================================================== MARCAS DE HOY ==
// Lo que este reloj marcó hoy (el registro completo está en el panel, pestaña Asistencia)
static void irMarcas() {
  lv_obj_t* s = nuevaPantalla();
  std::vector<const MarcaHoy*> v;
  for (const MarcaHoy& m : panel_marcas_hoy())
    if (!m.repetida) v.push_back(&m);
  String titulo = "Marcas de hoy (" + String((unsigned)v.size()) + ")";
  cabecera(s, titulo.c_str(), irMenu);
  if (v.empty()) {
    lv_obj_t* l = etiqueta(s, "Todavía no hay marcas hoy.", &f_grande, C_TENUE);
    lv_obj_center(l);
  } else {
    lv_obj_t* tb = lv_table_create(s);
    lv_obj_set_pos(tb, 0, 58);
    lv_obj_set_size(tb, PANT_W, PANT_H - 58);
    lv_table_set_column_count(tb, 3);
    lv_table_set_row_count(tb, v.size());
    lv_table_set_column_width(tb, 0, 92);
    lv_table_set_column_width(tb, 1, 120);
    lv_table_set_column_width(tb, 2, 260);
    lv_obj_set_style_text_font(tb, &f_texto, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(tb, C_FONDO, 0);
    lv_obj_set_style_bg_color(tb, C_FONDO, LV_PART_ITEMS);
    lv_obj_set_style_text_color(tb, C_TEXTO, LV_PART_ITEMS);
    lv_obj_set_style_border_width(tb, 0, 0);
    lv_obj_set_style_border_color(tb, C_LINEA, LV_PART_ITEMS);
    lv_obj_set_style_border_width(tb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(tb, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(tb, 12, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(tb, 16, LV_PART_ITEMS);
    for (size_t i = 0; i < v.size(); i++) {
      const MarcaHoy& m = *v[v.size() - 1 - i];  // la más nueva arriba
      const String nombre = (m.nombre.length() ? m.nombre : String("(huella ") + String(m.huella) + ")") +
                            (m.enviada ? "" : " · por enviar");
      String tipo;  // ENTRADA -> Entrada
      for (size_t k = 0; k < m.tipo.length(); k++) tipo += (char)(k ? tolower(m.tipo[k]) : m.tipo[k]);
      if (!tipo.length()) tipo = "?";
      lv_table_set_cell_value(tb, i, 0, m.hora.length() ? m.hora.c_str() : "--:--");
      lv_table_set_cell_value(tb, i, 1, tipo.c_str());
      lv_table_set_cell_value(tb, i, 2, nombre.c_str());
    }
  }
  cambiar(s, Vista::MARCAS);
}

// =================================================================== WIFI ==
static std::vector<String> redes;
static String redElegida;
static bool buscandoRedes = false;

static void textoLista(lv_obj_t* lista, const char* txt) {
  lv_obj_t* t = lv_list_add_text(lista, txt);
  lv_obj_set_style_text_font(t, &f_texto, 0);
  lv_obj_set_style_text_color(t, C_TENUE, 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(t, 12, 0);
}

static void buscarRedes() {
  if (!listaRedes) return;
  lv_obj_clean(listaRedes);
  textoLista(listaRedes, "Buscando redes...");
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  buscandoRedes = true;
}

static void mostrarRedes(int n) {
  if (!listaRedes) return;
  lv_obj_clean(listaRedes);
  redes.clear();
  uint32_t espera = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length() || std::find(redes.begin(), redes.end(), ssid) != redes.end()) continue;
    redes.push_back(ssid);
    const int rssi = WiFi.RSSI(i);
    lv_obj_t* b = filaLista(listaRedes, LV_SYMBOL_WIFI, ssid, rssi > -60 ? "buena" : rssi > -75 ? "media" : "débil");
    lv_obj_set_height(b, 56);
    if (espera < 300) aparecer(b, espera);  // las primeras llegan en cascada
    espera += 40;
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      redElegida = redes[(size_t)(uintptr_t)lv_event_get_user_data(e)];
      ir(irWifiClave);
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)(redes.size() - 1));
  }
  if (redes.empty()) textoLista(listaRedes, "No se encontraron redes. Toca Buscar.");
  WiFi.scanDelete();
}

static void irWifi() {
  lv_obj_t* s = nuevaPantalla();
  cabecera(s, "WiFi", irMenu);
  lblWifiEstado = etiqueta(s, red_estado().c_str(), &f_texto, C_TENUE);
  lv_label_set_long_mode(lblWifiEstado, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lblWifiEstado, 316);
  lv_obj_set_pos(lblWifiEstado, 16, 70);
  lv_obj_t* b = boton(s, LV_SYMBOL_REFRESH "  Buscar", C_SUPERFICIE, [](lv_event_t*) { buscarRedes(); }, nullptr, &f_texto);
  lv_obj_set_size(b, 132, 44);
  lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -8, 58);
  listaRedes = listaVacia(s, 108);
  cambiar(s, Vista::WIFI);
  buscarRedes();
}

static void irWifiClave() {
  lv_obj_t* s = nuevaPantalla();
  String titulo = "Clave de " + redElegida;
  cabecera(s, titulo.c_str(), irWifi);
  taTexto = campoTexto(s, "Clave del WiFi (vacía si no tiene)", redElegida == ajustes().ssid ? ajustes().clave : String());
  lv_textarea_set_max_length(taTexto, 63);
  lv_obj_t* kb = teclado(s, taTexto);
  lv_obj_add_event_cb(kb, [](lv_event_t*) {
    red_conectar(redElegida, lv_textarea_get_text(taTexto));
    mostrarCapa(Tono::NEGRO, "", "Conectando", redElegida + ". Puede tardar unos segundos", 2500, nullptr, LV_SYMBOL_WIFI);
    ir(irWifi);
  }, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kb, [](lv_event_t*) { ir(irWifi); }, LV_EVENT_CANCEL, nullptr);
  cambiar(s, Vista::WIFI_CLAVE);
}

// ================================================================ AJUSTES ==
static lv_obj_t* filaAjuste(lv_obj_t* c, const char* txt) {
  lv_obj_t* f = tarjeta(c, PANT_W - 16, 58);
  lv_obj_t* l = etiqueta(f, txt, &f_grande, C_TEXTO);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  return f;
}

static void irAjustes() {
  lv_obj_t* s = nuevaPantalla();
  cabecera(s, "Ajustes", irMenu);
  lv_obj_t* c = columna(s, 56);

  lv_obj_t* f = filaAjuste(c, "Girar pantalla 180°");
  lv_obj_t* sw = lv_switch_create(f);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  if (ajustes().girada) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, [](lv_event_t* e) {
    ajustes().girada = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    ajustes_guardar();
    pantalla_girar(ajustes().girada);
  }, LV_EVENT_VALUE_CHANGED, nullptr);

  f = filaAjuste(c, "Sonido");
  sw = lv_switch_create(f);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  if (ajustes().sonido) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, [](lv_event_t* e) {
    ajustes().sonido = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    ajustes_guardar();
    sonido_activar(ajustes().sonido);
    sonido_ok();
  }, LV_EVENT_VALUE_CHANGED, nullptr);

  f = filaAjuste(c, "Brillo en reposo");
  lv_obj_t* sl = lv_slider_create(f);
  lv_obj_set_width(sl, 170);
  lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_slider_set_range(sl, 5, 100);
  lv_slider_set_value(sl, ajustes().brilloReposo, LV_ANIM_OFF);
  lv_obj_add_event_cb(sl, [](lv_event_t* e) {
    ajustes().brilloReposo = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    pantalla_brillo(ajustes().brilloReposo);  // vista previa mientras se mueve
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sl, [](lv_event_t*) { ajustes_guardar(); }, LV_EVENT_RELEASED, nullptr);

  const struct {
    const char* txt;
    Destino d;
  } botones[] = {
      {LV_SYMBOL_EDIT "  Cambiar clave", [] {
         pinModo = PinModo::NUEVA1;
         irPin();
       }},
      {LV_SYMBOL_SETTINGS "  Información del equipo", irInfo},
  };
  for (auto& b : botones) {
    lv_obj_t* o = botonIr(c, b.txt, C_SUPERFICIE, b.d);
    lv_obj_set_size(o, PANT_W - 16, 58);
    lv_obj_set_style_radius(o, 22, 0);
  }
  cambiar(s, Vista::AJUSTES);
}

// ============================================================ INFORMACIÓN ==
static void irInfo() {
  lv_obj_t* s = nuevaPantalla();
  cabecera(s, "Información", irAjustes);
  String host = ajustes().panelUrl;
  const int p = host.indexOf("://");
  if (p >= 0) host = host.substring(p + 3);
  String t;
  t += "Panel:  " + panel_estado() + "\n";
  t += "            " + host + "\n";
  t += "Oficina:  " + ajustes().oficina + "  ·  Por enviar:  " + String((unsigned)panel_en_cola()) + " marcas\n";
  t += "Reloj web:  http://" + (red_conectada() ? red_ip() : String("(sin WiFi)")) + "  ·  reloj.local\n";
  t += "WiFi:  " + red_estado() + "\n";
  t += "Lector:  " + String(huella_conectado() ? "conectado" : "NO RESPONDE") + "  ·  " +
       String(panel_huellas_usadas()) + " de " + String(huella_capacidad()) + " huellas\n";
  t += "Plantillas:  " + panel_plantillas_estado() + "\n";
  t += "Versión:  " VERSION;
  lv_obj_t* l = etiqueta(s, t.c_str(), &f_texto, C_TEXTO);
  lv_obj_set_width(l, PANT_W - 32);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(l, 16, 66);
  lv_obj_set_style_text_line_space(l, 6, 0);
  cambiar(s, Vista::INFO);
}

// ===================================================================== tarea ==
static void manejar(const HuellaEv& ev) {
  // Plantillas y borrados de fondo: son de panel.cpp y no despiertan la pantalla
  if (ev.ev == HEv::PLANTILLA_SACADA || ev.ev == HEv::PLANTILLA_PUESTA || ev.ev == HEv::PLANTILLA_ERROR ||
      ev.ev == HEv::BORRADA) {
    panel_lector(ev);
    return;
  }
  lv_display_trigger_activity(NULL);
  switch (ev.ev) {
    case HEv::CONECTADO:
    case HEv::DESCONECTADO:
      refrescarInicio(false);
      break;
    case HEv::COINCIDE:
      if (vista == Vista::INICIO) mostrarSaludo(panel_marcar(ev.id));
      break;
    case HEv::NO_COINCIDE:
      if (vista == Vista::INICIO) {
        Saludo s;  // sin nombre: «No te conozco»
        mostrarSaludo(s);
        capaConSaludo = false;
        panel_refrescar_pronto();  // si se registró en el otro reloj, su plantilla llega ahora
      }
      break;
    case HEv::IMAGEN_MALA:
      if (vista == Vista::INICIO) {
        mostrarCapa(Tono::BLANCO, "", "No se leyó", "Apoya el dedo plano y quieto", 2000, nullptr, "!");
        sonido_aviso();
      } else if (vista == Vista::REGISTRO) {
        eventoRegistro(ev);
      }
      break;
    case HEv::ENR_OK:
      // Si ya se salió del registro (Cancelar, 90 s sin tocar) el panel nunca anota ese dedo:
      // se borra del lector para que no quede una huella sin dueño que marque por otro.
      if (vista == Vista::REGISTRO && lblPaso && !regGuardando) eventoRegistro(ev);
      else huella_borrar(ev.id);
      break;
    default:
      if (vista == Vista::REGISTRO) eventoRegistro(ev);
      break;
  }
}

// Respuestas del panel. Lo del lector (borrar huellas) se hace aunque ya no se esté
// en esa pantalla, para no dejar huellas sin dueño.
static void manejarPanel(const PanelEv& ev) {
  switch (ev.ev) {
    case PEv::PERFILES:
      // La lista se refresca sola cada 3 min: solo se rearma la pantalla si se estaba
      // esperando o si cambió lo que muestra (rearmarla vuelve la lista arriba y decodifica fotos)
      if (vista == Vista::PERSONAS) {
        if (!listaFresca || listaError.length() || firmaPersonas() != firmaEnPantalla) {
          listaFresca = true;
          listaError = "";
          irPersonas();
        }
      } else if (vista == Vista::PERSONA) {
        if (firmaPersonas() != firmaEnPantalla) irPersona();
      }
      break;
    case PEv::PERFILES_ERROR:
      if (vista == Vista::PERSONAS) {
        listaError = ev.texto;
        irPersonas();
      }
      break;
    case PEv::ASIGNADA:
      if (vista == Vista::REGISTRO && regGuardando && ev.huella == regHuella) finRegistro(true, nombreSel() + " ya puede marcar");
      break;
    case PEv::ASIGNAR_ERROR:
      huella_borrar(ev.huella);  // el panel no la anotó: fuera del lector
      if (vista == Vista::REGISTRO && regGuardando && ev.huella == regHuella) finRegistro(false, ev.texto);
      break;
    case PEv::SOLTADA:
      huella_borrar(ev.huella);
      if (ev.huella == borrandoHuella) {
        borrandoHuella = 0;
        mostrarCapa(Tono::ACENTO, "", "Listo", "Dedo borrado", 1800, nullptr, LV_SYMBOL_OK);
        if (vista == Vista::PERSONA) irPersona();
      }
      break;
    case PEv::SOLTAR_ERROR:
      if (ev.huella == borrandoHuella) {
        borrandoHuella = 0;
        mostrarCapa(Tono::BLANCO, "", "No se pudo", ev.texto, 3500, nullptr, "!");
        sonido_error();
      }
      break;
    case PEv::MARCA:
      corregirSaludo(ev.saludo);
      break;
  }
}

// Mantener apretado BOOT 10 s vuelve la clave a PIN_INICIAL (por si se olvida)
static void botonBoot() {
  static uint32_t desde = 0;
  static bool hecho = false;
  if (digitalRead(BOTON_BOOT) == LOW) {
    if (!desde) desde = millis();
    if (!hecho && millis() - desde > 10000) {
      hecho = true;
      ajustes().pin = PIN_INICIAL;
      ajustes_guardar();
      mostrarCapa(Tono::BLANCO, "", "Clave reiniciada", "Volvió a la clave inicial", 4000, nullptr, "!");
    }
  } else {
    desde = 0;
    hecho = false;
  }
}

void ui_iniciar() {
  pinMode(BOTON_BOOT, INPUT_PULLUP);
  lv_theme_t* th = lv_theme_default_init(lv_display_get_default(), C_ACENTO, C_ACENTO, true, &f_texto);
  lv_display_set_theme(lv_display_get_default(), th);
  estilosIniciar();
  sonido_activar(ajustes().sonido);
  irArranque();
}

void ui_tarea() {
  if (pendiente) {
    Destino d = pendiente;
    pendiente = nullptr;
    d();
  }
  if (cerrarCapaPedido) {
    cerrarCapaPedido = false;
    cerrarCapa();
  }
  if (soltarFotos && !capa && !capaSaliendo && (int32_t)(millis() - finTransicion) > 0) {
    soltarFotos = false;
    fotos_soltar_viejas();
  }
  HuellaEv ev;
  while (huella_evento(ev)) manejar(ev);
  PanelEv pe;
  while (panel_evento(pe)) manejarPanel(pe);

  static uint32_t t = 0;
  if (millis() - t >= 500) {
    t = millis();
    refrescarInicio(false);
    if (lblWifiEstado) ponerTexto(lblWifiEstado, red_estado().c_str());
    // Con la pantalla de inicio quieta, deja lista la foto grande de alguien (una por vez):
    // así al poner el dedo el saludo sale al tiro, sin esperar a leer el JPEG
    if (vista == Vista::INICIO && !capa && !capaSaliendo && !pendiente && (int32_t)(millis() - finTransicion) > 0)
      for (const Perfil& p : panel_perfiles())
        if (p.foto.length() && foto_precargar(p.perfil, FOTO_GRANDE)) break;
  }
  if (buscandoRedes) {
    const int n = WiFi.scanComplete();
    if (n >= 0) {
      buscandoRedes = false;
      mostrarRedes(n);
    } else if (n == WIFI_SCAN_FAILED) {
      buscandoRedes = false;
      mostrarRedes(0);
    }
  }
  botonBoot();
  // Sin tocar nada por 90 s fuera de la pantalla de inicio: volver al inicio
  if (vista != Vista::INICIO && vista != Vista::ARRANQUE && !pendiente && !modal &&
      lv_display_get_inactive_time(NULL) > 90000)
    ir(irInicio);
}
