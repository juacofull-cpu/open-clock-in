"""Graba en GIF cómo se mueve la pantalla del simulador (arranque, saludo, menú, clave, registro).

Uso:  python -I grabar_animaciones.py
Necesita el simulador compilado, Pillow y haber corrido antes probar_panel.py (usa una copia de
sus datos: Ana con el dedo 1). Deja los GIF en .sim/capturas/marca/.
Las capturas se piden una tras otra por HTTP, así que el GIF muestra el movimiento, no la
velocidad exacta de la placa.
"""
import os
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request

from PIL import Image
RAIZ = os.environ.get("RELOJ_SIM_RAIZ", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".sim"))

AQUI = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(RAIZ, "build", "reloj-sim.exe")
ORIGEN = os.path.join(RAIZ, "prueba-panel")
DATOS = os.path.join(RAIZ, "prueba-marca")
FALSO = os.path.join(RAIZ, "falso-marca")
SALIDA = os.path.join(RAIZ, "capturas", "marca")
CONTROL = "http://127.0.0.1:8099"


def sim(ruta, **q):
    url = CONTROL + ruta + ("?" + urllib.parse.urlencode(q) if q else "")
    with urllib.request.urlopen(url, timeout=10) as r:
        return r.read().decode("utf-8")


def textos():
    r = []
    for l in sim("/textos").splitlines():
        pos, _, t = l.partition(" ")
        if "," in pos:
            x, y = pos.split(",")
            r.append((int(x), int(y), t))
    return r


def tocar(t, exacto=False):
    for x, y, s in reversed(textos()):
        if (s == t) if exacto else (t in s):
            sim("/tocar", x=x, y=y)
            return
    raise SystemExit("no encontré para tocar: " + t)


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
    os.makedirs(SALIDA, exist_ok=True)
    cuadros[0].save(os.path.join(SALIDA, nombre + ".gif"), save_all=True, append_images=cuadros[1:],
                    duration=dur, loop=0)
    os.remove(bmp)
    print(f"   {nombre}.gif: {len(cuadros)} cuadros, {dur} ms c/u")


shutil.rmtree(DATOS, ignore_errors=True)
shutil.copytree(ORIGEN, DATOS)
for f in ("hoy.json", "cola.txt"):  # que la marca de Ana sea la primera del día
    if os.path.exists(os.path.join(DATOS, f)):
        os.remove(os.path.join(DATOS, f))
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
    for _ in range(40):  # que el servidor de pruebas del simulador esté arriba
        try:
            sim("/estado")
            break
        except OSError:
            time.sleep(0.05)
    print("grabando")
    grabar("1-arranque", 2.8)
    time.sleep(3)
    grabar("2-hola", 4.6, lambda: sim("/dedo", n=1))
    time.sleep(1)
    grabar("3-no-te-conozco", 1.2, lambda: sim("/dedo", n=40))
    time.sleep(4)
    grabar("4-a-la-clave", 0.9, lambda: sim("/tocar", x=444, y=38))
    time.sleep(0.4)

    def clave():
        for k in "12":
            tocar("[tecla] " + k, exacto=True)
            time.sleep(0.15)
    grabar("5-clave-mala", 1.4, lambda: (clave(), tocar("[tecla] \uf00c", exacto=True)))
    for k in "1234":  # PIN_INICIAL (sin ajustes guardados)
        tocar("[tecla] " + k, exacto=True)
        time.sleep(0.1)
    grabar("6-al-menu", 1.0, lambda: tocar("[tecla] \uf00c", exacto=True))
    time.sleep(0.5)
    grabar("7-registro", 2.4, lambda: tocar("Registrar dedo"))
    time.sleep(1)
    grabar("8-elige-y-pon-el-dedo", 3.0, lambda: tocar("Eva"))
    grabar("9-volver", 0.9, lambda: tocar("Cancelar"))
finally:
    try:
        sim("/salir")
    except OSError:
        pass
    time.sleep(0.5)
    reloj.kill()
    falso.kill()
