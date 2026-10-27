// Ventana "Lector de huella": los dedos que se pueden apoyar, el botón BOOT, el LED
// y lo que en la placa no se ve en la pantalla (brillo, sonido, WiFi, internet).
#include <lvgl.h>
#include <algorithm>
#include "datos.h"
#include "panel.h"
#include "red.h"
#include "sim.h"

#define C_FONDO lv_color_hex(0x0E0E0E)
#define C_PANEL lv_color_hex(0x2A2A2A)
#define C_TIENE lv_color_hex(0x1D3B2A)
#define C_TENUE lv_color_hex(0x8C8C8C)
#define C_AVISO lv_color_hex(0xC98A00)

static lv_obj_t *lblEstado, *lblInfo, *luz, *swEnchufe, *swInternet;
static lv_obj_t *botonDedo[SIM_DEDOS], *lblDueno[SIM_DEDOS];

static void ponerTexto(lv_obj_t* l, const char* t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

static lv_obj_t* boton(lv_obj_t* p, int w, int h, lv_color_t c) {
  lv_obj_t* b = lv_button_create(p);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, c, 0);
  lv_obj_set_style_radius(b, 4, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 4, 0);
  return b;
}

static lv_obj_t* texto(lv_obj_t* p, const char* t, const lv_font_t* f, lv_color_t c) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  return l;
}

void sim_panel_crear() {
  lv_obj_t* s = lv_screen_active();
  lv_obj_set_style_bg_color(s, C_FONDO, 0);
  lv_obj_set_style_text_color(s, lv_color_white(), 0);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* t = texto(s, "LECTOR DE HUELLA", &f_grande, lv_color_white());
  lv_obj_set_pos(t, 12, 8);
  lblEstado = texto(s, "", &f_chico, C_TENUE);
  lv_obj_set_pos(lblEstado, 12, 40);
  lv_obj_t* a = texto(s, "Toca un dedo para apoyarlo en el lector", &f_chico, C_TENUE);
  lv_obj_set_pos(a, 12, 62);

  for (int i = 0; i < SIM_DEDOS; i++) {
    lv_obj_t* b = boton(s, 134, 56, C_PANEL);
    lv_obj_set_pos(b, 12 + (i % 2) * 142, 90 + (i / 2) * 64);
    lv_obj_t* l = texto(b, (String("Dedo ") + String(i + 1)).c_str(), &f_texto, lv_color_white());
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 2);
    lblDueno[i] = texto(b, "", &f_chico, C_TENUE);
    lv_label_set_long_mode(lblDueno[i], LV_LABEL_LONG_DOT);
    lv_obj_set_width(lblDueno[i], 124);
    lv_obj_set_style_text_align(lblDueno[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lblDueno[i], LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_event_cb(b, [](lv_event_t* e) { sim_lector_dedo((int)(intptr_t)lv_event_get_user_data(e)); },
                        LV_EVENT_CLICKED, (void*)(intptr_t)(i + 1));
    botonDedo[i] = b;
  }

  lv_obj_t* mal = boton(s, 276, 44, C_AVISO);
  lv_obj_set_pos(mal, 12, 286);
  lv_obj_center(texto(mal, "Dedo mal apoyado", &f_texto, lv_color_white()));
  lv_obj_add_event_cb(mal, [](lv_event_t*) { sim_lector_mal(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* le = texto(s, "Lector", &f_texto, lv_color_white());
  lv_obj_set_pos(le, 12, 348);
  swEnchufe = lv_switch_create(s);
  lv_obj_set_pos(swEnchufe, 82, 344);
  lv_obj_add_state(swEnchufe, LV_STATE_CHECKED);
  lv_obj_add_event_cb(swEnchufe, [](lv_event_t* e) {
    sim_lector_enchufar(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
  }, LV_EVENT_VALUE_CHANGED, nullptr);

  // Internet: apagado imita que se cayó la salida a internet (el WiFi sigue)
  lv_obj_t* li = texto(s, "Internet", &f_texto, lv_color_white());
  lv_obj_set_pos(li, 150, 348);
  swInternet = lv_switch_create(s);
  lv_obj_set_pos(swInternet, 232, 344);
  lv_obj_add_state(swInternet, LV_STATE_CHECKED);
  lv_obj_add_event_cb(swInternet, [](lv_event_t* e) {
    sim_internet = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
  }, LV_EVENT_VALUE_CHANGED, nullptr);

  // BOOT: mantenerlo apretado 10 s vuelve la clave a la inicial
  lv_obj_t* boot = boton(s, 90, 44, C_PANEL);
  lv_obj_set_pos(boot, 12, 384);
  lv_obj_center(texto(boot, "BOOT", &f_texto, lv_color_white()));
  lv_obj_add_event_cb(boot, [](lv_event_t*) { sim_boot_apretado = true; }, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(boot, [](lv_event_t*) { sim_boot_apretado = false; }, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(boot, [](lv_event_t*) { sim_boot_apretado = false; }, LV_EVENT_PRESS_LOST, nullptr);
  lv_obj_t* nb = texto(s, "mantener 10 s", &f_chico, C_TENUE);
  lv_obj_set_pos(nb, 110, 396);

  luz = lv_obj_create(s);
  lv_obj_set_size(luz, 28, 28);
  lv_obj_set_pos(luz, 258, 392);
  lv_obj_set_style_radius(luz, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(luz, 1, 0);
  lv_obj_set_style_border_color(luz, lv_color_hex(0x555555), 0);
  lv_obj_remove_flag(luz, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* nl = texto(s, "LED", &f_chico, C_TENUE);
  lv_obj_set_pos(nl, 224, 396);

  lblInfo = texto(s, "", &f_chico, C_TENUE);
  lv_obj_set_pos(lblInfo, 12, 436);
}

void sim_panel_tick() {
  static uint32_t t = 0;
  if (millis() - t < 250) return;
  t = millis();
  ponerTexto(lblEstado, sim_lector_estado().c_str());
  for (int i = 0; i < SIM_DEDOS; i++) {
    const int pos = sim_lector_posicion(i + 1);
    const Perfil* tr = pos >= 0 ? panel_por_huella((uint16_t)pos) : nullptr;
    const String dueno = pos < 0 ? String("sin registrar") : tr ? tr->nombre : String("guardado sin dueño");
    ponerTexto(lblDueno[i], dueno.c_str());
    lv_obj_set_style_bg_color(botonDedo[i], tr ? C_TIENE : C_PANEL, 0);
  }
  const uint32_t c = sim_led();
  // El LED real con valores bajos (90 de 255) igual se ve fuerte: se escala para mostrarlo
  auto subir = [](uint32_t v) { return (uint32_t)(v ? std::min<uint32_t>(255, 60 + v * 2) : 0); };
  lv_obj_set_style_bg_color(luz, lv_color_make(subir(c >> 16 & 0xFF), subir(c >> 8 & 0xFF), subir(c & 0xFF)), 0);
  String info = "Brillo " + String(sim_brillo()) + " %   Sonido " + (sim_sonido() ? "sí" : "no") +
                "   Girada " + (sim_girada() ? "sí" : "no");
  if (red_conectada()) info += "\nReloj web: http://" + red_ip();
  else info += "\nWiFi «Oficina», clave oficina123";
  info += "\nPanel: " + panel_estado();
  if (panel_en_cola()) info += "  ·  " + String((unsigned)panel_en_cola()) + " por enviar";
  // Si lo cambió una prueba por HTTP, que el interruptor lo muestre
  if (sim_internet != lv_obj_has_state(swInternet, LV_STATE_CHECKED)) {
    if (sim_internet) lv_obj_add_state(swInternet, LV_STATE_CHECKED);
    else lv_obj_remove_state(swInternet, LV_STATE_CHECKED);
  }
  ponerTexto(lblInfo, info.c_str());
}
