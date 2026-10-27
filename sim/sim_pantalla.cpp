// Pantalla, táctil, brillo, sonido y LED en el PC, más la ventana del lector.
// Reemplaza a src/pantalla.cpp y src/sonido.cpp con la misma interfaz.
//
// Ojo con los hilos: cada ventana de LVGL tiene su propio hilo, que toma lv_lock() para
// atender el mouse. Nada de llamadas a ventanas (SetWindowPos y similares) con el candado
// tomado, o se trancan.
#include <windows.h>
#include <mmsystem.h>
#include <lvgl.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include "src/drivers/windows/lv_windows_context.h"
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
#include "config.h"
#include "datos.h"
#include "pantalla.h"
#include "red.h"
#include "sim.h"
#include "sonido.h"
#include "tema.h"

static const int ZOOM = 150;  // la pantalla real mide 3,5"; al 150 % se ve parecida en un monitor
static lv_display_t *dispReloj, *dispPanel;
static uint8_t brillo = 100;
static bool girada = false, sonidoOn = true;
static uint32_t colorLed = 0;
static lv_obj_t* sombra;  // capa negra encima de todo que imita la luz de fondo baja

// ------------------------------------------------------------ pantalla --
void pantalla_brillo(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (pct == brillo && sombra) return;
  brillo = pct;
  if (!sombra) return;
  if (pct >= 100) {
    lv_obj_add_flag(sombra, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(sombra, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(sombra, (lv_opa_t)((100 - pct) * 230 / 100), 0);
  }
}

void pantalla_tarea(uint8_t brilloReposo) {
  pantalla_brillo(lv_display_get_inactive_time(NULL) > 60000 ? brilloReposo : 100);
}

void pantalla_girar(bool g) { girada = g; }  // en la caja se vería derecha igual
bool pantalla_tactil_ok() { return true; }

// Toques para las pruebas automáticas: una fila de lecturas (x, y, apretado)
struct Lectura {
  int16_t x, y;
  bool apretado;
};
static std::deque<Lectura> lecturas;
static Lectura ultima = {0, 0, false};

static void leerPrueba(lv_indev_t*, lv_indev_data_t* d) {
  if (!lecturas.empty()) {
    ultima = lecturas.front();
    lecturas.pop_front();
  }
  d->point.x = ultima.x;
  d->point.y = ultima.y;
  d->state = ultima.apretado ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void sim_tocar(int x, int y) {
  for (int i = 0; i < 3; i++) lecturas.push_back({(int16_t)x, (int16_t)y, true});
  for (int i = 0; i < 3; i++) lecturas.push_back({(int16_t)x, (int16_t)y, false});
}

// Deslizar el dedo de (x1, y1) a (x2, y2), para mover listas
void sim_arrastrar(int x1, int y1, int x2, int y2) {
  for (int i = 0; i <= 20; i++)
    lecturas.push_back({(int16_t)(x1 + (x2 - x1) * i / 20), (int16_t)(y1 + (y2 - y1) * i / 20), true});
  for (int i = 0; i < 3; i++) lecturas.push_back({(int16_t)x2, (int16_t)y2, false});
}

static void salir(lv_event_t*) { ExitProcess(0); }

static HWND ventana(lv_display_t* d) { return lv_windows_get_display_window_handle(d); }

void pantalla_iniciar(bool g) {
  girada = g;
  lv_init();
  dispReloj = lv_windows_create_display(L"Reloj control - simulador", PANT_W, PANT_H, ZOOM, false, true);
  dispPanel = lv_windows_create_display(L"Lector de huella", 300, 490, ZOOM, false, true);
  // Una ventana al lado de la otra, antes de tomar el candado
  RECT r1, r2;
  GetWindowRect(ventana(dispReloj), &r1);
  GetWindowRect(ventana(dispPanel), &r2);
  SetWindowPos(ventana(dispReloj), NULL, 60, 60, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  SetWindowPos(ventana(dispPanel), NULL, 60 + (r1.right - r1.left) + 12, 60, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  if (getenv("RELOJ_SIM_OCULTO")) {  // pruebas automáticas: sin ventanas a la vista
    ShowWindow(ventana(dispReloj), SW_HIDE);
    ShowWindow(ventana(dispPanel), SW_HIDE);
  }

  lv_lock();
  lv_windows_acquire_pointer_indev(dispReloj);
  lv_windows_acquire_pointer_indev(dispPanel);
  lv_indev_t* prueba = lv_indev_create();
  lv_indev_set_type(prueba, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(prueba, leerPrueba);
  lv_indev_set_display(prueba, dispReloj);
  lv_display_add_event_cb(dispReloj, salir, LV_EVENT_DELETE, nullptr);
  lv_display_add_event_cb(dispPanel, salir, LV_EVENT_DELETE, nullptr);

  sombra = lv_obj_create(lv_display_get_layer_sys(dispReloj));
  lv_obj_set_size(sombra, PANT_W, PANT_H);
  lv_obj_set_style_bg_color(sombra, lv_color_black(), 0);
  lv_obj_set_style_border_width(sombra, 0, 0);
  lv_obj_set_style_radius(sombra, 0, 0);
  lv_obj_remove_flag(sombra, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(sombra, LV_OBJ_FLAG_HIDDEN);

  lv_display_set_default(dispPanel);
  sim_panel_crear();
  lv_display_set_default(dispReloj);
  lv_unlock();
}

// Foto exacta de lo que muestra una ventana, en BMP
bool sim_captura(int cual, const std::string& archivo) {
  lv_display_t* d = cual ? dispPanel : dispReloj;
  lv_refr_now(d);
  lv_windows_window_context_t* c = lv_windows_get_window_context(ventana(d));
  if (!c || !c->display_framebuffer_context_handle) return false;
  const int w = lv_display_get_horizontal_resolution(d), h = lv_display_get_vertical_resolution(d);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = nullptr;
  HDC dc = CreateCompatibleDC(NULL);
  HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  HGDIOBJ antes = SelectObject(dc, bmp);
  BitBlt(dc, 0, 0, w, h, c->display_framebuffer_context_handle, 0, 0, SRCCOPY);
  GdiFlush();
  FILE* f = fopen(archivo.c_str(), "wb");
  if (f) {
    BITMAPFILEHEADER fh = {};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + w * h * 4;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
    fwrite(bits, 4, (size_t)w * h, f);
    fclose(f);
  }
  SelectObject(dc, antes);
  DeleteObject(bmp);
  DeleteDC(dc);
  return f != nullptr;
}

// Todos los textos visibles de la pantalla del reloj, con el centro de cada uno
static void juntarTextos(lv_obj_t* o, std::string& s) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  char pos[32];
  snprintf(pos, sizeof(pos), "%d,%d ", (int)(a.x1 + a.x2) / 2, (int)(a.y1 + a.y2) / 2);
  if (lv_obj_check_type(o, &lv_label_class)) {
    s += pos;
    s += lv_label_get_text(o);
    s += "\n";
  } else if (lv_obj_check_type(o, &lv_textarea_class)) {
    s += pos;
    s += "[campo] ";
    s += lv_textarea_get_text(o);
    s += "\n";
    return;
  } else if (lv_obj_has_class(o, &lv_buttonmatrix_class)) {
    // Teclas (teclado, clave): posición exacta de cada una
    const lv_buttonmatrix_t* bm = (const lv_buttonmatrix_t*)o;
    for (uint32_t i = 0; i < bm->btn_cnt; i++) {
      const lv_area_t& b = bm->button_areas[i];
      char t[48];
      snprintf(t, sizeof(t), "%d,%d [tecla] ", (int)(a.x1 + (b.x1 + b.x2) / 2), (int)(a.y1 + (b.y1 + b.y2) / 2));
      s += t;
      s += lv_buttonmatrix_get_button_text(o, i);
      s += "\n";
    }
  } else if (lv_obj_check_type(o, &lv_table_class)) {
    for (uint32_t f = 0; f < lv_table_get_row_count(o); f++) {
      s += pos;
      s += "[fila]";
      for (uint32_t c = 0; c < lv_table_get_column_count(o); c++) {
        const char* v = lv_table_get_cell_value(o, f, c);
        s += " | ";
        s += v ? v : "";
      }
      s += "\n";
    }
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) juntarTextos(lv_obj_get_child(o, i), s);
}

void sim_textos(std::string& s) {
  s.clear();
  lv_obj_update_layout(lv_display_get_screen_active(dispReloj));
  lv_obj_update_layout(lv_display_get_layer_top(dispReloj));
  juntarTextos(lv_display_get_screen_active(dispReloj), s);
  s += "--- encima ---\n";
  juntarTextos(lv_display_get_layer_top(dispReloj), s);
}

uint8_t sim_brillo() { return brillo; }
bool sim_girada() { return girada; }
bool sim_sonido() { return sonidoOn; }
uint32_t sim_led() { return colorLed; }

// -------------------------------------------------------------- sonido --
// Las mismas ondas cuadradas que manda sonido.cpp al parlante (16 kHz).
enum Tono { T_OK, T_SALIDA, T_ERROR, T_AVISO, T_TECLA, T_ESPECIAL };  // mismo orden que TonoTema
static std::mutex mx;
static std::condition_variable cv;
static std::deque<Tono> cola;

static void tocar(std::vector<int16_t>& o, int frec, int ms, int amp) {
  const int FS = 16000, total = FS * ms / 1000, rampa = FS * 4 / 1000, periodo = frec > 0 ? FS / frec : 0;
  amp = amp * 6 / 10;  // un poco más bajo que en el parlante chico de la placa
  for (int i = 0; i < total; i++) {
    int v = 0;
    if (periodo > 0) {
      int env = amp;
      if (i < rampa) env = amp * i / rampa;
      else if (total - i < rampa) env = amp * (total - i) / rampa;
      v = (i % periodo) < periodo / 2 ? env : -env;
    }
    o.push_back((int16_t)v);
  }
}

static void hiloSonido() {
  for (;;) {
    Tono t;
    {
      std::unique_lock<std::mutex> l(mx);
      cv.wait(l, [] { return !cola.empty(); });
      t = cola.front();
      cola.pop_front();
    }
    std::vector<int16_t> m;
    std::vector<Nota> melodia;
    if (tema_melodia((TonoTema)t, melodia)) {
      const int amp = t == T_TECLA ? 4000 : 8000;
      for (const Nota& n : melodia) tocar(m, n.frec, n.ms, n.frec ? amp : 0);
    } else switch (t) {
      case T_OK:
      case T_SALIDA:
      case T_ESPECIAL: tocar(m, 1319, 90, 9000); tocar(m, 1760, 160, 9000); break;
      case T_ERROR: tocar(m, 330, 180, 10000); tocar(m, 0, 60, 0); tocar(m, 330, 260, 10000); break;
      case T_AVISO: tocar(m, 988, 120, 8000); tocar(m, 0, 60, 0); tocar(m, 988, 120, 8000); break;
      case T_TECLA: tocar(m, 2093, 18, 4000); break;
    }
    tocar(m, 0, 40, 0);
    // WAV en memoria: cabecera de 44 bytes + muestras
    const uint32_t datos = (uint32_t)(m.size() * 2);
    std::vector<uint8_t> wav(44 + datos);
    auto u32 = [&](size_t p, uint32_t v) { memcpy(&wav[p], &v, 4); };
    auto u16 = [&](size_t p, uint16_t v) { memcpy(&wav[p], &v, 2); };
    memcpy(&wav[0], "RIFF", 4);
    u32(4, 36 + datos);
    memcpy(&wav[8], "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);
    u16(22, 1);
    u32(24, 16000);
    u32(28, 32000);
    u16(32, 2);
    u16(34, 16);
    memcpy(&wav[36], "data", 4);
    u32(40, datos);
    memcpy(&wav[44], m.data(), datos);
    PlaySoundA((LPCSTR)wav.data(), NULL, SND_MEMORY | SND_SYNC | SND_NODEFAULT);
  }
}

static void pedir(Tono t) {
  if (!sonidoOn) return;
  {
    std::lock_guard<std::mutex> l(mx);
    if (cola.size() >= 8) return;
    cola.push_back(t);
  }
  cv.notify_one();
}

void sonido_iniciar() { std::thread(hiloSonido).detach(); }
void sonido_activar(bool on) { sonidoOn = on; }
void sonido_ok() { pedir(T_OK); }
void sonido_salida() { pedir(T_SALIDA); }
void sonido_especial() { pedir(T_ESPECIAL); }
void sonido_error() { pedir(T_ERROR); }
void sonido_aviso() { pedir(T_AVISO); }
void sonido_tecla() { pedir(T_TECLA); }
void led(uint8_t r, uint8_t g, uint8_t b) { colorLed = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
