"""Prueba de los temas incluidos (temas/*.json) en el simulador, cada uno en su fecha.

Uso:  python -I grabar_festividades.py [exe]
Para cada caso levanta el panel falso viviendo en esa fecha (RELOJ_FALSO_FECHA), abre el
simulador con los datos de probar_panel.py (dedo 1 = Ana), graba el inicio, marca una entrada
y toca la hora 5 veces (la sorpresa). Revisa que el saludo sea el esperado. Deja GIF, PNG y los
WAV de todas las melodías en .sim/capturas/festividades/.
"""
import glob
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import urllib.parse
import urllib.request

from PIL import Image
RAIZ = os.environ.get("RELOJ_SIM_RAIZ", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".sim"))

AQUI = os.path.dirname(os.path.abspath(__file__))
TEMAS = os.path.join(AQUI, "..", "temas")
EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, "build-navidad", "reloj-sim.exe")
ORIGEN = os.path.join(RAIZ, "prueba-panel")
DATOS = os.path.join(RAIZ, "prueba-festividades")
FALSO = os.path.join(RAIZ, "falso-festividades")
SALIDA = os.path.join(RAIZ, "capturas", "festividades")
CONTROL = "http://127.0.0.1:8099"

# nombre, fecha en Chile, texto que debe salir al marcar Ana (o None), sorpresa esperada (o None), extra
CASOS = [
    ("navidad", "2026-12-10T09:00", None, "¡Jo jo jo! ¡Me pillaste!", {}),
    ("navidad-24", "2026-12-24T10:00", "¡Hoy llega el Viejito Pascuero, Ana!", None, {}),
    ("navidad-secreto", "2026-12-10T09:00", "Psst, Ana... el Viejito Pascuero ya sabe si te portaste bien", None,
     {"RELOJ_SIM_SORPRESAS": "1"}),
    ("anio-nuevo", "2026-12-28T09:00", None, "Diez, nueve, ocho... ¡Feliz Año Nuevo!", {}),
    ("anio-nuevo-31", "2026-12-31T09:00", "¡Último día del 2026, Ana!", None, {}),
    ("anio-nuevo-01", "2027-01-01T10:00", "¿Ana? ¡Hoy es feriado, anda a dormir!", None, {}),
    ("anio-nuevo-uvas", "2026-12-28T09:00", None, None, {"RELOJ_SIM_SORPRESAS": "1"}),
    ("san-valentin", "2027-02-14T09:00", None, "Este reloj te quiere harto", {}),
    ("trabajador", "2027-05-01T09:00", None, "Hoy el reloj también está de feriado", {}),
    ("fiestas-patrias", "2026-09-16T09:00", None, "¡Ceacheí! ¡Chi chi chi, le le le!", {}),
    ("fiestas-patrias-18", "2026-09-18T11:00", "¿Ana? ¡Hoy es 18, anda a la fonda!", None, {}),
    ("halloween", "2026-10-31T09:00", None, "¡BUUU!", {}),
    ("halloween-secreto", "2026-10-31T09:00", "Esta huella... ya la había visto antes, Ana", None,
     {"RELOJ_SIM_SORPRESAS": "1"}),
    ("dia-normal", "2026-11-15T09:00", "Hola Ana", None, {}),
]


def sim(ruta, **q):
    url = CONTROL + ruta + ("?" + urllib.parse.urlencode(q) if q else "")
    with urllib.request.urlopen(url, timeout=10) as r:
        return r.read().decode("utf-8")


def estado():
    return dict(l.split("=", 1) for l in sim("/estado").splitlines() if "=" in l)


def textos():
    return [l.partition(" ")[2] for l in sim("/textos").splitlines()]


def esperar(cond, seg, que):
    fin = time.time() + seg
    while time.time() < fin:
        try:
            if cond():
                return
        except OSError:
            pass
        time.sleep(0.2)
    raise RuntimeError("no llegó: " + que)


def captura(nombre):
    bmp = os.path.join(DATOS, "cuadro.bmp")
    sim("/captura", v=0, archivo=bmp)
    Image.open(bmp).convert("RGB").save(os.path.join(SALIDA, nombre + ".png"))
    os.remove(bmp)


def grabar(nombre, segundos):
    cuadros, bmp, fin = [], os.path.join(DATOS, "cuadro.bmp"), time.time() + segundos
    t0 = time.time()
    while time.time() < fin:
        sim("/captura", v=0, archivo=bmp)
        cuadros.append(Image.open(bmp).convert("RGB"))
    dur = max(20, int(1000 * (time.time() - t0) / len(cuadros)))
    cuadros[0].save(os.path.join(SALIDA, nombre + ".gif"), save_all=True, append_images=cuadros[1:],
                    duration=dur, loop=0)
    os.remove(bmp)


def wav(nombre, notas, amp=8000):
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
    datos = struct.pack("<%dh" % len(m), *m)
    with open(os.path.join(SALIDA, "sonidos", nombre + ".wav"), "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(datos)) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 1, fs, fs * 2, 2, 16) + b"data" + struct.pack("<I", len(datos)) + datos)


shutil.rmtree(SALIDA, ignore_errors=True)
os.makedirs(os.path.join(SALIDA, "sonidos"))
for ruta in sorted(glob.glob(os.path.join(TEMAS, "*.json"))):
    base = os.path.splitext(os.path.basename(ruta))[0]
    with open(ruta, encoding="utf-8") as f:
        t = json.load(f)
    for clave in ("ok", "salida"):
        if t["tonos"].get(clave):
            wav(f"{base}-{'entrada' if clave == 'ok' else 'salida'}", t["tonos"][clave])
    if t.get("sorpresa", {}).get("tono"):
        wav(f"{base}-sorpresa", t["sorpresa"]["tono"])
    for i, e in enumerate(t.get("especiales", [])):
        if e.get("tono"):
            wav(f"{base}-especial-{e.get('dia', i)}", e["tono"])

fallas = []
for nombre, fecha, saludo, sorpresa, extra in CASOS:
    print(f"{nombre} ({fecha})")
    shutil.rmtree(DATOS, ignore_errors=True)
    shutil.copytree(ORIGEN, DATOS)
    for f in ("hoy.json", "cola.txt", "tema.json"):
        if os.path.exists(os.path.join(DATOS, "flash", f)):
            os.remove(os.path.join(DATOS, "flash", f))
    shutil.rmtree(FALSO, ignore_errors=True)
    shutil.copytree(os.path.join(RAIZ, "falso"), FALSO)
    falso = subprocess.Popen([sys.executable, "-I", os.path.join(AQUI, "servidor_falso.py"), "8078"],
                             env=dict(os.environ, RELOJ_FALSO_DATOS=FALSO, RELOJ_FALSO_FECHA=fecha),
                             stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    time.sleep(1.2)
    env = dict(os.environ, RELOJ_SIM_DATOS=DATOS, RELOJ_SIM_PRUEBAS="8099", RELOJ_SIM_OCULTO="1",
               RELOJ_SIM_SIN_NTP="1", **extra)
    reloj = subprocess.Popen([EXE], env=env)
    try:
        esperar(lambda: estado().get("hora_valida") == "1", 30, "hora desde el panel")
        time.sleep(2.5)  # el inicio se rearma con el tema apenas hay hora
        if not extra:
            grabar(f"{nombre}-1-inicio", 3)
        sim("/dedo", n=1)
        time.sleep(1.6)
        captura(f"{nombre}-2-entrada")
        vistos = textos()
        if saludo and saludo not in vistos:
            fallas.append(f"{nombre}: esperaba «{saludo}» y salió {[t for t in vistos if 'Ana' in t]}")
        elif not saludo:
            print("   saludo:", [t for t in vistos if "Ana" in t])
        if sorpresa:
            time.sleep(3.5)  # que se cierre el saludo
            for _ in range(5):
                sim("/tocar", x=240, y=154)
                time.sleep(0.15)
            time.sleep(1.4)
            captura(f"{nombre}-3-sorpresa")
            if sorpresa not in textos():
                fallas.append(f"{nombre}: la sorpresa «{sorpresa}» no salió")
    except (RuntimeError, OSError) as e:
        fallas.append(f"{nombre}: {e}")
    finally:
        try:
            sim("/salir")
        except OSError:
            pass
        time.sleep(0.5)
        reloj.kill()
        falso.kill()
        time.sleep(0.3)

print()
print("RESULTADO:", "todo bien" if not fallas else "FALLAS")
for f in fallas:
    print("  -", f)
sys.exit(1 if fallas else 0)
