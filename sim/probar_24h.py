"""Prueba de turnos 24/7: una jornada que cruza la medianoche y el otro reloj.

La SALIDA es la marca que sigue a una ENTRADA de hace 16 h o menos (jornada_horas), sea del día que
sea, y el servidor le dice a cada reloj la última marca de cada persona en /api/reloj/perfiles
(«ultima»). Antes el reloj contaba por día: quien entraba 23:50 y salía 00:22 recibía «Hola» otra vez.

Uso:  python -I probar_24h.py [exe]
Levanta server/server.py (puerto 8031, base nueva en <RELOJ_SIM_RAIZ>/prueba-24h, `demo`) y dos
simuladores (A en 8111, B en 8112). La hora de cada simulador se mueve con /hora. Revisa:
  1. A: Ana entra ayer 23:50 -> «Hola»
  2. A sin internet: hoy 00:05 -> «Nos vemos» (lo sabe aunque «Marcas de hoy» partió de cero)
  3. A: hoy 00:10 -> «Hola»
  4. B (nunca vio esas marcas): hoy 00:20 -> «Nos vemos», por la «ultima» del servidor
  5. el servidor guardó ENTRADA, SALIDA, ENTRADA, SALIDA con sus oficinas
Hay que correrla después de las 00:25 hora local (las marcas no pueden ir al futuro). Tarda ~2 min.
Las utilidades (Reloj, pide, revisar, huella_de) son las de probar_dos_oficinas.py.
"""
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

from PIL import Image  # noqa: F401 (lo usa Reloj.captura)

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.environ.get("RELOJ_SIM_RAIZ", os.path.join(AQUI, "..", ".sim"))
EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, "build", "reloj-sim.exe")
BASE = os.path.join(RAIZ, "prueba-24h")
CAPTURAS = os.path.join(RAIZ, "capturas", "24h")
PANEL = "http://127.0.0.1:8031"
LLAVE = "prueba"
ANIMACION = 0.5
fallas = []
TZ = ZoneInfo(os.environ.get("CLOCK_TZ", "America/Santiago"))

_src = open(os.path.join(AQUI, "probar_dos_oficinas.py"), encoding="utf-8").read()
exec(_src[_src.index("def pide("):_src.index("# ------------------------------------------------------------------ arranque")])

hoy0 = datetime.now(TZ).replace(hour=0, minute=0, second=0, microsecond=0)
if datetime.now(TZ) < hoy0 + timedelta(minutes=25):
    raise SystemExit("correr después de las 00:25 hora local")


def t(h, m, dias=0):
    return int((hoy0 + timedelta(days=dias, hours=h, minutes=m)).timestamp())


def cola_vacia(r):
    r.esperar(lambda: r.estado().get("cola") == "0", 20, "la cola se vacía")


shutil.rmtree(BASE, ignore_errors=True)
os.makedirs(BASE)
DB = os.path.join(BASE, "clock.db")
SERVER = os.path.join(AQUI, "..", "server", "server.py")
env_srv = dict(os.environ, CLOCK_DB=DB, CLOCK_TOKEN=LLAVE)
subprocess.run([sys.executable, SERVER, "demo"], env=env_srv, check=True)
servidor = subprocess.Popen([sys.executable, SERVER, "serve", "--port", "8031"], env=env_srv,
                            stdout=open(os.path.join(BASE, "server.log"), "w"), stderr=subprocess.STDOUT)
a, b = Reloj("A", 8111), Reloj("B", 8112)
try:
    for _ in range(60):
        try:
            if pide(PANEL + "/health")[0] == 200:
                break
        except Exception:
            pass
        time.sleep(0.5)
    a.arrancar()
    b.arrancar()
    time.sleep(2)
    for r in (a, b):
        r.esperar(lambda: r.estado().get("conectado") == "1", 30, "servidor conectado")

    print("0. Ana registra el dedo 1 en A; B baja la plantilla")
    a.registrar("Ana", 1)
    n_ana = huella_de("ana")[0][0]
    b.sim("/perfiles")
    b.esperar(lambda: b.memoria().get(n_ana) == 1, 40, "B tiene el dedo de Ana")

    print("1. A, ayer 23:50: entra")
    a.sim("/hora", t=t(23, 50, -1))
    a.marcar(1, "Hola", "1-a-entra-2350")
    cola_vacia(a)

    print("2. A sin internet, hoy 00:05: sale (cruza la medianoche)")
    a.sim("/internet", on=0)
    a.sim("/hora", t=t(0, 5))
    a.marcar(1, "Nos vemos", "2-a-sale-0005-sin-internet")
    a.sim("/internet", on=1)
    cola_vacia(a)

    print("3. A, hoy 00:10: entra de nuevo")
    a.sim("/hora", t=t(0, 10))
    a.marcar(1, "Hola", "3-a-entra-0010")
    cola_vacia(a)

    print("4. B, hoy 00:20: sale, aunque la entrada la vio A")
    b.sim("/perfiles")
    time.sleep(3)
    b.sim("/hora", t=t(0, 20))
    b.marcar(1, "Nos vemos", "4-b-sale-0020")
    cola_vacia(b)

    print("5. lo que guardó el servidor")
    with sqlite3.connect(DB) as c:
        filas = c.execute("select t_utc, tipo, oficina from punches where perfil='ana' and repetida=0 "
                          "order by t_utc").fetchall()
    for f in filas:
        print("   ", f)
    revisar([(x[1], x[2]) for x in filas] == [("ENTRADA", "A"), ("SALIDA", "A"), ("ENTRADA", "A"),
                                               ("SALIDA", "B")], "ENTRADA, SALIDA, ENTRADA, SALIDA (A, A, A, B)")
    perf = json.loads(pide(PANEL + "/api/reloj/perfiles", llave=LLAVE)[1])
    ult = next(p["ultima"] for p in perf["perfiles"] if p["perfil"] == "ana")
    revisar(ult and ult["tipo"] == "SALIDA", "el servidor dice que lo último de Ana es SALIDA: %s" % ult)
    revisar(perf.get("jornada_horas") == 16, "jornada_horas = 16")
finally:
    a.cerrar()
    b.cerrar()
    servidor.terminate()
print()
print("RESULTADO: todo bien" if not fallas else "FALLAS:\n  " + "\n  ".join(fallas))
sys.exit(1 if fallas else 0)
