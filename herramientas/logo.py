"""Logo de la pantalla (va sobre negro): convierte herramientas/logo.png al formato RGB565A8 de LVGL 9.

Uso:  python -I herramientas/logo.py [logo.png]  ->  src/imagenes/logo.c
"""
import os
import sys

from PIL import Image

AQUI = os.path.dirname(os.path.abspath(__file__))
ORIGEN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(AQUI, "logo.png")
SALIDA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "imagenes", "logo.c")
TAMANOS = {"logo_chico": 50, "logo_grande": 150}  # alto en px


def a_c(nombre, im):
    w, h = im.size
    rgb, alfa = bytearray(), bytearray()
    px = im.tobytes()
    for i in range(0, len(px), 4):
        r, g, b, a = px[i:i + 4]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        rgb += bytes((v & 0xFF, v >> 8))
        alfa.append(a)
    datos = rgb + alfa
    filas = [", ".join(f"0x{x:02x}" for x in datos[i:i + 24]) for i in range(0, len(datos), 24)]
    return (f"static const uint8_t {nombre}_px[] = {{\n  " + ",\n  ".join(filas) + "\n};\n"
            f"const lv_image_dsc_t {nombre} = {{\n"
            f"  .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .flags = 0,"
            f" .w = {w}, .h = {h}, .stride = {w * 2}}},\n"
            f"  .data_size = {len(datos)},\n  .data = {nombre}_px,\n}};\n\n")


def main():
    base = Image.open(ORIGEN).convert("RGBA")
    base = base.crop(base.getbbox()).convert("RGBa")  # premultiplicado: sin bordes sucios al achicar
    out = ["// Generado por herramientas/logo.py: no editar a mano.\n#include <lvgl.h>\n\n"]
    for nombre, alto in TAMANOS.items():
        ancho = round(base.width * alto / base.height)
        out.append(a_c(nombre, base.resize((ancho, alto), Image.LANCZOS).convert("RGBA")))
        print(nombre, ancho, "x", alto)
    os.makedirs(os.path.dirname(SALIDA), exist_ok=True)
    with open(SALIDA, "w", encoding="utf-8") as f:
        f.write("".join(out))


if __name__ == "__main__":
    sys.exit(main())
