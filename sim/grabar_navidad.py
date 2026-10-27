"""Prueba del tema de Navidad en el simulador: graba el inicio con nieve y luces, una entrada
y una salida con sus saludos navideños, y deja en WAV cómo suenan las melodías del tema.

Uso:  python -I grabar_navidad.py [exe]
Usa temas/navidad.json con las fechas corridas para que valga hoy (el de verdad parte el 1 de
diciembre) y los datos de probar_panel.py (en el lector de mentira: dedo 1 = Ana, dedo 2 = Bruno). Deja todo en
.sim/capturas/navidad/.
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from datetime import datetime

from PIL import Image
RAIZ = os.environ.get("RELOJ_SIM_RAIZ", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".sim"))

AQUI = os.path.dirname(os.path.abspath(__file__))
TEMA = os.path.join(AQUI, "..", "temas", "navidad.json")
EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, "build-navidad", "reloj-sim.exe")
ORIGEN = os.path.join(RAIZ, "prueba-panel")
DATOS = os.path.join(RAIZ, "prueba-navidad")
FALSO = os.path.join(RAIZ, "falso-navidad")
SALIDA = os.path.join(RAIZ, "capturas", "navidad")
CONTROL = "http://127.0.0.1:8099"


def sim(ruta, **q):
    url = CONTROL + ruta + ("?" + urllib.parse.urlencode(q) if q else "")
    with urllib.request.urlopen(url, timeout=10) as r:
        return r.read().decode("utf-8")


def estado():
    return dict(l.split("=", 1) for l in sim("/estado").splitlines() if "=" in l)


def esperar(cond, seg, que):
    fin = time.time() + seg
    while time.time() < fin:
        try:
            if cond():
                return
        except OSError:
            pass
        time.sleep(0.2)
    raise SystemExit("no llegó: " + que)


def grabar(nombre, segundos, accion=None):
    """Captura cuadros seguidos durante `segundos`; `accion` se hace después del primer cuadro."""
    cuadros, tiempos = [], []
    bmp = os.path.join(DATOS, "cuadro.bmp")
    fin = None
    while fin is None or time.time() < fin:
        t0 = time.time()
        sim("/captura", v=0, archivo=bmp)
        cuadros.append(Image.open(bmp).convert("RGB"))
        if fin is None:
            if accion:
                accion()
            fin = time.time() + segundos
        tiempos.append(time.time() - t0)
    dur = max(20, int(1000 * sum(tiempos) / len(tiempos)))
    cuadros[0].save(os.path.join(SALIDA, nombre + ".gif"), save_all=True, append_images=cuadros[1:],
                    duration=dur, loop=0)
    cuadros[len(cuadros) // 2].save(os.path.join(SALIDA, nombre + ".png"))
    os.remove(bmp)
    print(f"   {nombre}.gif: {len(cuadros)} cuadros, {dur} ms c/u")


def wav(nombre, notas, amp):
    """La misma onda cuadrada con rampas de 4 ms que toca sonido.cpp, a 16 kHz."""
    fs, m = 16000, []
    for frec, ms in notas + [[0, 40]]:
        total, rampa = fs * ms // 1000, fs * 4 // 1000
        periodo = fs // frec if frec else 0
        for i in range(total):
            v = 0
            if periodo:
                env = amp * i // rampa if i < rampa else amp * (total - i) // rampa if total - i < rampa else amp
                v = env if i % periodo < periodo // 2 else -env
            m.append(v)
    with open(os.path.join(SALIDA, nombre + ".wav"), "wb") as f:
        datos = struct.pack("<%dh" % len(m), *m)
        f.write(b"RIFF" + struct.pack("<I", 36 + len(datos)) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 1, fs, fs * 2, 2, 16) + b"data" + struct.pack("<I", len(datos)) + datos)


shutil.rmtree(SALIDA, ignore_errors=True)
os.makedirs(SALIDA)
with open(TEMA, encoding="utf-8") as f:
    tema = json.load(f)
for clave, nombre in (("ok", "sonido-entrada"), ("salida", "sonido-salida"), ("error", "sonido-error"),
                      ("aviso", "sonido-ya-marcaste"), ("tecla", "sonido-tecla")):
    wav(nombre, tema["tonos"][clave], 4000 if clave == "tecla" else 8000)

shutil.rmtree(DATOS, ignore_errors=True)
shutil.copytree(ORIGEN, DATOS)
hoy = datetime.now()
tema["desde"] = hoy.strftime("%m-%d")  # solo para la prueba: que el tema valga hoy
with open(os.path.join(DATOS, "flash", "tema.json"), "w", encoding="utf-8") as f:
    json.dump(tema, f, ensure_ascii=False)
for f in ("hoy.json", "cola.txt"):  # que las marcas de hoy partan de cero
    if os.path.exists(os.path.join(DATOS, "flash", f)):
        os.remove(os.path.join(DATOS, "flash", f))
shutil.rmtree(FALSO, ignore_errors=True)
shutil.copytree(os.path.join(RAIZ, "falso"), FALSO)

falso = subprocess.Popen([sys.executable, "-I", os.path.join(AQUI, "servidor_falso.py"), "8078"],
                         env=dict(os.environ, RELOJ_FALSO_DATOS=FALSO),
                         stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
time.sleep(1.5)
env = dict(os.environ, RELOJ_SIM_DATOS=DATOS, RELOJ_SIM_PRUEBAS="8099", RELOJ_SIM_OCULTO="1",
           RELOJ_SIM_SIN_NTP="1")
reloj = subprocess.Popen([EXE], env=env)
try:
    esperar(lambda: estado().get("hora_valida") == "1", 30, "hora desde el panel")
    time.sleep(2.5)  # el inicio se rearma con el tema apenas hay hora
    print("grabando")
    grabar("1-inicio-nieve", 5)
    grabar("2-entrada-ana", 4.2, lambda: sim("/dedo", n=1))
    time.sleep(1)
    grabar("3-entrada-bruno", 4.2, lambda: sim("/dedo", n=2))  # el dedo 2 del lector es el 3 de Bruno
    t_bruno = time.time()
    time.sleep(1)
    grabar("4-ya-marcaste", 2.5, lambda: sim("/dedo", n=1))
    print("   esperando 2 min para que la siguiente sea salida")
    time.sleep(max(0, t_bruno + 125 - time.time()))
    grabar("5-salida-bruno", 4.2, lambda: sim("/dedo", n=2))
    time.sleep(1)
    grabar("6-salida-ana", 4.2, lambda: sim("/dedo", n=1))
finally:
    try:
        sim("/salir")
    except OSError:
        pass
    time.sleep(0.5)
    reloj.kill()
    falso.kill()
