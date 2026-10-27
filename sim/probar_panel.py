"""Prueba de punta a punta del simulador contra un panel (el falso de sim/servidor_falso.py o tu propio servidor).

Uso:  python -I probar_panel.py [url-del-panel] [llave]
      sin url levanta servidor_falso.py en el puerto 8078.
Parte con datos nuevos en .sim/prueba-panel/ y deja las capturas en
.sim/capturas/panel/ (con el panel falso, en .../panel/panel-falso/). Necesita el simulador compilado y Pillow.
Tarda unos 5 minutos: entre una marca y la siguiente de la misma persona hay que esperar 2 min.
"""
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone

from PIL import Image
RAIZ = os.environ.get("RELOJ_SIM_RAIZ", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".sim"))

AQUI = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(RAIZ, "build", "reloj-sim.exe")
DATOS = os.path.join(RAIZ, "prueba-panel")
CAPTURAS = os.path.join(RAIZ, "capturas", "panel")
CONTROL = "http://127.0.0.1:8099"
PANEL = sys.argv[1] if len(sys.argv) > 1 else ""
LLAVE = sys.argv[2] if len(sys.argv) > 2 else "prueba"
falso = None
if not PANEL:
    PANEL = "http://127.0.0.1:8078"
    datos_falso = os.path.join(RAIZ, "falso")
    os.makedirs(datos_falso, exist_ok=True)
    if os.path.exists(os.path.join(datos_falso, "huellas.json")):
        os.remove(os.path.join(datos_falso, "huellas.json"))
    falso = subprocess.Popen([sys.executable, "-I", os.path.join(AQUI, "servidor_falso.py"), "8078"],
                             stdout=open(os.path.join(datos_falso, "registro.txt"), "w"), stderr=subprocess.STDOUT)
    time.sleep(1.5)
    CAPTURAS += r"\panel-falso"

fallas = []


def pide(url, metodo="GET", cuerpo=None, llave=None):
    h = {"Content-Type": "application/json"}
    if llave:
        h["Authorization"] = "Bearer " + llave
    req = urllib.request.Request(url, method=metodo, headers=h,
                                 data=json.dumps(cuerpo).encode() if cuerpo is not None else None)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8")


def sim(ruta, **q):
    return pide(CONTROL + ruta + ("?" + urllib.parse.urlencode(q) if q else ""))[1]


def estado():
    return dict(l.split("=", 1) for l in sim("/estado").splitlines() if "=" in l)


def textos():
    """[(x, y, texto, encima)]"""
    r, encima = [], False
    for l in sim("/textos").splitlines():
        if l == "--- encima ---":
            encima = True
            continue
        pos, _, t = l.partition(" ")
        if "," in pos:
            x, y = pos.split(",")
            r.append((int(x), int(y), t, encima))
    return r


def hay(t, encima=None):
    return any(t in x[2] and (encima is None or x[3] == encima) for x in textos())


def esperar(cond, seg=15, que=""):
    fin = time.time() + seg
    while time.time() < fin:
        if cond():
            return True
        time.sleep(0.25)
    fallas.append("no pasó: " + que)
    print("   !! no pasó:", que)
    print("      pantalla:", [x[2] for x in textos()])
    raise SystemExit("se corta la prueba")


ANIMACION = 0.5  # lo que tarda en asentarse una pantalla o un aviso que entra (ui.cpp, 280-520 ms)


def tocar(t, exacto=False):
    time.sleep(ANIMACION)  # mientras entra deslizándose, las posiciones todavía se mueven
    for x, y, s, _ in reversed(textos()):  # lo de encima primero
        if (s == t) if exacto else (t in s):
            sim("/tocar", x=x, y=y)
            time.sleep(0.4)
            return
    fallas.append("no encontré para tocar: " + t)
    print("   !! no encontré para tocar:", t, [x[2] for x in textos()])
    raise SystemExit("se corta la prueba")


def captura(nombre):
    os.makedirs(CAPTURAS, exist_ok=True)
    bmp = os.path.join(DATOS, nombre + ".bmp")
    time.sleep(ANIMACION)
    sim("/captura", v=0, archivo=bmp)
    Image.open(bmp).save(os.path.join(CAPTURAS, nombre + ".png"))
    os.remove(bmp)
    print("   captura", nombre)


def cerrar_capa():
    esperar(lambda: not any(x[3] for x in textos()), 8, "se cierra el aviso")


def entrar_admin():
    sim("/tocar", x=444, y=32)  # engranaje
    esperar(lambda: hay("Clave de administrador"), 5, "pantalla de clave")
    for k in "1234":
        tocar("[tecla] " + k, exacto=True)
    for x, y, s, _ in textos():
        if s.startswith("[tecla]") and s.endswith("\uf00c"):  # LV_SYMBOL_OK
            sim("/tocar", x=x, y=y)
    esperar(lambda: hay("Administración"), 5, "menú de administración")


def registrar(nombre, dedo):
    tocar("Registrar dedo")
    esperar(lambda: hay(nombre), 15, "lista de perfiles con " + nombre)
    tocar(nombre)
    esperar(lambda: hay("PASO 1 DE 2"), 5, "paso 1")
    sim("/dedo", n=dedo)
    esperar(lambda: hay("PASO 2 DE 2"), 8, "paso 2")
    sim("/dedo", n=dedo)
    esperar(lambda: hay("ya puede marcar"), 20, nombre + " registrado")


def marcar(dedo, texto, nombre_captura=None, seg=6):
    sim("/dedo", n=dedo)
    ok = esperar(lambda: hay(texto, True), seg, "saludo «%s»" % texto)
    if ok and nombre_captura:
        captura(nombre_captura)
    cerrar_capa()


def reloj_utc(minutos):
    return (datetime.now(timezone.utc) + timedelta(minutes=minutos)).strftime("%Y-%m-%dT%H:%M:%SZ")


# ------------------------------------------------------------------ arranque --
shutil.rmtree(DATOS, ignore_errors=True)
os.makedirs(DATOS)
with open(os.path.join(DATOS, "nvs-reloj.txt"), "w", newline="\n") as f:
    f.write(f"pin=1234\nsonido=0\nssid=Oficina\nclave=oficina123\npurl={PANEL}\npllave={LLAVE}\n")
env = dict(os.environ, RELOJ_SIM_DATOS=DATOS, RELOJ_SIM_PRUEBAS="8099", RELOJ_SIM_OCULTO="1",
           RELOJ_SIM_SIN_NTP="1")
reloj = subprocess.Popen([EXE], env=env)
try:
    time.sleep(2)
    print("1. arranca, conecta al panel y toma la hora de «ahora» (sin NTP)")
    esperar(lambda: estado().get("conectado") == "1", 30, "panel conectado")
    esperar(lambda: estado().get("hora_valida") == "1", 5, "hora desde el panel")
    print("  ", estado())
    time.sleep(1)
    captura("01-inicio")

    print("2. dedo desconocido")
    marcar(1, "No te conozco", "02-no-te-conozco")

    print("3. registrar dedos: Ana (dedo 1), Diego (dedo 3, sin foto), Bruno (dedo 2)")
    entrar_admin()
    tocar("Registrar dedo")
    esperar(lambda: hay("Ana"), 15, "lista para registrar")
    time.sleep(2)  # la primera vez se bajan las fotos: se ven al volver a entrar
    tocar("Volver")
    tocar("Registrar dedo")
    esperar(lambda: hay("Ana"), 15, "lista para registrar")
    captura("03-lista-perfiles")
    tocar("Volver")
    registrar("Ana", 1)
    captura("04-registro-listo")
    tocar("Registrar otro")
    esperar(lambda: hay("Diego"), 15, "lista")
    tocar("Diego")
    esperar(lambda: hay("PASO 1 DE 2"), 5, "paso 1")
    sim("/dedo", n=3)
    esperar(lambda: hay("PASO 2 DE 2"), 8, "paso 2")
    sim("/dedo", n=3)
    esperar(lambda: hay("ya puede marcar"), 20, "Diego registrado")
    tocar("Registrar otro")
    esperar(lambda: hay("Bruno"), 15, "lista")
    tocar("Bruno")
    esperar(lambda: hay("PASO 1 DE 2"), 5, "paso 1")
    sim("/dedo", n=2)
    esperar(lambda: hay("PASO 2 DE 2"), 8, "paso 2")
    sim("/dedo", n=2)
    esperar(lambda: hay("ya puede marcar"), 20, "Bruno registrada")
    tocar("Listo")
    tocar("Personas")
    esperar(lambda: hay("Personas ("), 5, "lista de personas")
    time.sleep(1)
    captura("05-personas")
    tocar("Volver")
    tocar("Salir")
    esperar(lambda: hay("Pon tu dedo"), 5, "inicio")

    print("4. marcas: entrada con foto, entrada sin foto, repetida")
    marcar(1, "Hola Ana", "06-hola-con-foto")
    marcar(3, "Hola Diego", "07-hola-sin-foto")
    marcar(1, "Ya marcaste, Ana", "08-ya-marcaste")

    print("5. el panel manda: Bruno marcó en otro lado hace 5 min, el reloj supone entrada")
    perf = json.loads(pide(PANEL + "/api/reloj/perfiles", llave=LLAVE)[1])["perfiles"]
    h_bruno = next(p["huellas"][0] for p in perf if p["perfil"] == "bruno")
    print("   huellas en el panel:", {p["perfil"]: p["huellas"] for p in perf if p["huellas"]})
    print("  ", pide(PANEL + "/api/reloj/marcas", "POST", {"id": "otro-reloj-0001", "huella": h_bruno,
                                                           "hora": reloj_utc(-5)}, LLAVE))
    sim("/dedo", n=2)
    esperar(lambda: hay("Hola Bruno", True) or hay("Nos vemos Bruno", True), 6, "saludo a Bruno")
    esperar(lambda: hay("Nos vemos Bruno", True), 6, "corrige a «Nos vemos Bruno»")
    captura("09-corregido-por-el-panel")
    cerrar_capa()

    print("6. espera 2 minutos para la salida de Ana")
    time.sleep(125)
    marcar(1, "Nos vemos Ana", "10-nos-vemos")

    print("7. sin internet: Diego marca la salida igual y queda en cola")
    sim("/internet", on=0)
    time.sleep(1)
    marcar(3, "Nos vemos Diego", "11-nos-vemos-sin-internet")
    esperar(lambda: estado().get("cola") == "1", 3, "1 marca en cola")
    time.sleep(6)  # que el panel figure desconectado
    captura("12-inicio-por-enviar")
    entrar_admin()
    tocar("Registrar dedo")
    esperar(lambda: hay("Necesita internet"), 20, "registrar sin internet")
    captura("13-registrar-sin-internet")
    tocar("Volver")
    tocar("Salir")

    print("8. vuelve internet y la marca llega")
    sim("/internet", on=1)
    esperar(lambda: estado().get("cola") == "0", 70, "cola vacía")
    print("  ", estado())

    print("9. borrar el dedo de Diego")
    entrar_admin()
    tocar("Personas")
    esperar(lambda: hay("Diego"), 10, "lista")
    tocar("Diego")
    esperar(lambda: hay("Dedo N"), 5, "ficha de Diego")
    captura("14-ficha-persona")
    tocar("Borrar")
    esperar(lambda: hay("¿Borrar el dedo"), 3, "confirmación")
    for x, y, s, enc in reversed(textos()):
        if s == "Borrar" and enc:
            sim("/tocar", x=x, y=y)
            break
    esperar(lambda: hay("Dedo borrado"), 15, "dedo borrado")
    perf = json.loads(pide(PANEL + "/api/reloj/perfiles", llave=LLAVE)[1])["perfiles"]
    print("   en el panel:", {p["perfil"]: p["huellas"] for p in perf if p["huellas"] or p["perfil"] == "diego"})
    if any(p["perfil"] == "diego" and p["huellas"] for p in perf):
        fallas.append("Diego sigue con huella en el panel")
    if falso:
        m = json.loads(pide(PANEL + "/falso/marcas")[1])["marcas"]
        print("   marcas en el panel falso:", [(x["perfil"], x["tipo"]) for x in m])
    print("   registro del simulador:", os.path.join(DATOS, "registro.txt"))
finally:
    try:
        sim("/salir")
    except Exception:
        pass
    reloj.wait(10)
    if falso:
        falso.terminate()

print("\nRESULTADO:", "todo bien" if not fallas else "%d fallas" % len(fallas))
for f in fallas:
    print(" -", f)
sys.exit(1 if fallas else 0)
