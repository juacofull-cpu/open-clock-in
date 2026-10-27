"""Logo de Open Clock-In: un círculo del color de acento con una huella blanca encima.

Uso:  python -I herramientas/dibujar_logo.py   ->  herramientas/logo.png  (después: logo.py)
Para poner tu propio logo, reemplaza herramientas/logo.png (PNG con transparencia) y corre logo.py.
"""
import os

from PIL import Image, ImageDraw

ACENTO = (0x6C, 0x5C, 0xE7)  # el mismo COLOR_ACENTO de src/config.h
N = 1200  # se dibuja grande y logo.py lo achica
SALIDA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logo.png")

im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.ellipse((0, 0, N - 1, N - 1), fill=ACENTO + (255,))
cx, cy, grosor = N / 2, N * 0.53, N * 0.034
# Crestas: arcos concéntricos, cada uno con un corte distinto (como una huella)
for i, (desde, hasta) in enumerate([(200, 340), (190, 350), (180, 330), (200, 380), (170, 345),
                                    (185, 395), (165, 335)]):
    r = N * (0.06 + 0.055 * i)
    caja = (cx - r, cy - r * 1.18, cx + r, cy + r * 1.18)
    d.arc(caja, desde, hasta, fill="white", width=int(grosor))
d.rounded_rectangle((cx - grosor / 2, cy - N * 0.02, cx + grosor / 2, cy + N * 0.16), radius=grosor / 2, fill="white")
im.save(SALIDA)
print("logo ->", SALIDA)
