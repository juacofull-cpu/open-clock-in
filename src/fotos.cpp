// Fotos de los perfiles: JPEG -> imagen redonda RGB565 + transparencia (RGB565A8).
// Se usa el TJPGD que trae LVGL (LV_USE_TJPGD), directo: así se decodifica una sola vez
// y no en cada redibujo, y no hace falta un sistema de archivos de LVGL.
#include <LittleFS.h>
#include <math.h>
#include <map>
#include <set>
#include <vector>
#include "src/libs/tjpgd/tjpgd.h"
#include "fotos.h"
#ifndef SIMULADOR
#include <esp_heap_caps.h>
#endif

static std::map<String, lv_image_dsc_t*> listas;  // "perfil#tam" -> imagen
static std::vector<lv_image_dsc_t*> viejas;
static std::set<String> fallidas;  // JPEG que no se pudo leer: no se reintenta hasta que cambie

// Lo grande va a la PSRAM; la memoria interna queda para WiFi y la conexión segura
static void* reservar(size_t n) {
#ifndef SIMULADOR
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) return p;
#endif
  return malloc(n);
}

String foto_ruta(const String& perfil) {
  String n;
  for (char c : perfil) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    n += ok ? c : '_';
    if (n.length() >= 24) break;
  }
  return "/fotos/" + n + ".jpg";
}

static lv_image_dsc_t* nuevaImagen(int t) {
  lv_image_dsc_t* d = (lv_image_dsc_t*)calloc(1, sizeof(lv_image_dsc_t));
  uint8_t* datos = (uint8_t*)reservar((size_t)t * t * 3);
  if (!d || !datos) {
    free(d);
    free(datos);
    return nullptr;
  }
  d->header.magic = LV_IMAGE_HEADER_MAGIC;
  d->header.cf = LV_COLOR_FORMAT_RGB565A8;
  d->header.w = t;
  d->header.h = t;
  d->header.stride = t * 2;
  d->data_size = (uint32_t)t * t * 3;
  d->data = datos;
  return d;
}

static void soltarImagen(lv_image_dsc_t* d) {
  if (!d) return;
  lv_image_cache_drop(d);
  free((void*)d->data);
  free(d);
}

// Pinta un pixel con la transparencia del círculo (borde suavizado de 1 px)
static void pixel(lv_image_dsc_t* d, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  const int t = d->header.w;
  const float c = t / 2.0f, dx = x + 0.5f - c, dy = y + 0.5f - c;
  const float borde = c - sqrtf(dx * dx + dy * dy) + 0.5f;
  const uint8_t a = borde <= 0 ? 0 : borde >= 1 ? 255 : (uint8_t)(borde * 255);
  uint8_t* datos = (uint8_t*)d->data;
  const uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  datos[(y * t + x) * 2] = v & 0xFF;
  datos[(y * t + x) * 2 + 1] = v >> 8;
  datos[t * t * 2 + y * t + x] = a;
}

// Silueta gris para quien no tiene foto propia
static lv_image_dsc_t* silueta(int t) {
  lv_image_dsc_t* d = nuevaImagen(t);
  if (!d) return nullptr;
  const float c = t / 2.0f;
  for (int y = 0; y < t; y++)
    for (int x = 0; x < t; x++) {
      const float px = x + 0.5f, py = y + 0.5f;
      const float cab = hypotf(px - c, py - t * 0.38f), cuerpo = hypotf(px - c, py - t * 1.02f);
      const bool figura = cab < t * 0.19f || cuerpo < t * 0.42f;
      const uint8_t v = figura ? 0x9A : 0x3A;
      pixel(d, x, y, v, v, v);
    }
  return d;
}

// ------------------------------------------------------------- JPEG --
struct Lectura {
  File* f;
  uint8_t* rgb;  // B, G, R por pixel (así lo entrega TJPGD)
  int w, h;
};

static size_t entrada(JDEC* jd, uint8_t* buf, size_t n) {
  Lectura* l = (Lectura*)jd->device;
  if (buf) return l->f->readBytes((char*)buf, n);
  size_t k = 0;
  while (k < n && l->f->read() >= 0) k++;
  return k;
}

static int salida(JDEC* jd, void* bmp, JRECT* r) {
  Lectura* l = (Lectura*)jd->device;
  const uint8_t* s = (const uint8_t*)bmp;
  for (int y = r->top; y <= r->bottom; y++)
    for (int x = r->left; x <= r->right; x++, s += 3)
      if (x < l->w && y < l->h) memcpy(&l->rgb[(y * l->w + x) * 3], s, 3);
  return 1;
}

static lv_image_dsc_t* decodificar(const String& ruta, int t) {
  File f = LittleFS.open(ruta, "r");
  if (!f) return nullptr;
  const size_t tamPozo = 4096;  // lo que pide TJPGD
  void* pozo = malloc(tamPozo);
  JDEC jd;
  Lectura l = {&f, nullptr, 0, 0};
  lv_image_dsc_t* d = nullptr;
  if (pozo && jd_prepare(&jd, entrada, pozo, tamPozo, &l) == JDR_OK && jd.width && jd.height && jd.width <= 640 &&
      jd.height <= 640) {
    l.w = jd.width;
    l.h = jd.height;
    l.rgb = (uint8_t*)reservar((size_t)l.w * l.h * 3);
    if (l.rgb && jd_decomp(&jd, salida, 0) == JDR_OK && (d = nuevaImagen(t))) {
      // Cuadrado del centro, achicado promediando
      const int lado = l.w < l.h ? l.w : l.h, x0 = (l.w - lado) / 2, y0 = (l.h - lado) / 2;
      for (int y = 0; y < t; y++)
        for (int x = 0; x < t; x++) {
          const int ax = x0 + x * lado / t, bx = x0 + (x + 1) * lado / t, ay = y0 + y * lado / t, by = y0 + (y + 1) * lado / t;
          uint32_t sb = 0, sg = 0, sr = 0, n = 0;
          for (int yy = ay; yy < (by > ay ? by : ay + 1); yy++)
            for (int xx = ax; xx < (bx > ax ? bx : ax + 1); xx++) {
              const uint8_t* p = &l.rgb[(yy * l.w + xx) * 3];
              sb += p[0];
              sg += p[1];
              sr += p[2];
              n++;
            }
          pixel(d, x, y, sr / n, sg / n, sb / n);
        }
    }
  }
  if (!d) Serial.printf("[fotos] no se pudo leer %s\n", ruta.c_str());
  free(l.rgb);
  free(pozo);
  f.close();
  return d;
}

// --------------------------------------------------------------- uso --
const lv_image_dsc_t* foto_imagen(const String& perfil, int tam) {
  const String clave = perfil + "#" + String(tam);
  auto it = listas.find(clave);
  if (it != listas.end() && it->second) return it->second;
  lv_image_dsc_t* d = nullptr;
  if (perfil.length() && LittleFS.exists(foto_ruta(perfil))) d = decodificar(foto_ruta(perfil), tam);
  if (d) {
    listas[clave] = d;
    return d;
  }
  // Silueta, una por tamaño
  const String cs = "#" + String(tam);
  auto is = listas.find(cs);
  if (is != listas.end() && is->second) return is->second;
  return listas[cs] = silueta(tam);
}

bool foto_precargar(const String& perfil, int tam) {
  const String clave = perfil + "#" + String(tam);
  if (!perfil.length() || listas.count(clave) || fallidas.count(clave)) return false;
  if (!LittleFS.exists(foto_ruta(perfil))) return false;
  if (lv_image_dsc_t* d = decodificar(foto_ruta(perfil), tam)) listas[clave] = d;
  else fallidas.insert(clave);
  return true;
}

void fotos_cambio(const String& perfil) {
  const String pre = perfil + "#";
  for (auto it = fallidas.begin(); it != fallidas.end();) it = it->startsWith(pre) ? fallidas.erase(it) : std::next(it);
  for (auto it = listas.begin(); it != listas.end();) {
    if (it->first.startsWith(pre)) {
      viejas.push_back(it->second);  // puede estar en pantalla todavía
      it = listas.erase(it);
    } else {
      ++it;
    }
  }
}

void fotos_soltar_viejas() {
  for (lv_image_dsc_t* d : viejas) soltarImagen(d);
  viejas.clear();
}
