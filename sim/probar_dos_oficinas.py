"""Prueba de dos relojes a la vez (Oficina A y Oficina B) contra el panel falso.

Contrato: docs/API.md, «Dos relojes: Oficina A y Oficina B».
Uso:  python -I probar_dos_oficinas.py [exe]
      exe por defecto .sim/build/reloj-sim.exe
Levanta servidor_falso.py (puerto 8075, datos en .../sim/dos-oficinas/falso) y dos simuladores con
datos nuevos en .../sim/dos-oficinas/reloj-a y .../reloj-b (control en 8111 y 8112). No toca la
carpeta .../sim/demo ni otros simuladores abiertos. Capturas en .../sim/capturas/dos-oficinas/.
Revisa:
  - cada marca llega al panel con la oficina del reloj; una marca vieja en cola (sin oficina) va sin ella;
  - un dedo registrado en A no se reconoce en B hasta que B baja su plantilla; después «Hola Ana» en B
    y la marca queda en la oficina B; lo mismo al revés (Bruno registrada en B, marca en A);
  - soltar el dedo (Borrar en A) borra la plantilla del panel y B lo saca de su lector.
Tarda unos 2 minutos.
"""
import json
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
EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, "build", "reloj-sim.exe")
BASE = os.path.join(RAIZ, "dos-oficinas")
CAPTURAS = os.path.join(RAIZ, "capturas", "dos-oficinas")
PANEL = "http://127.0.0.1:8075"
LLAVE = "prueba"
ANIMACION = 0.5
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


def falso():
    return json.loads(pide(PANEL + "/falso/marcas")[1])


def revisar(cond, que):
    print("   ", "ok:" if cond else "!! FALLA:", que)
    if not cond:
        fallas.append(que)


class Reloj:
    def __init__(self, letra, puerto):
        self.letra = letra
        self.control = "http://127.0.0.1:%d" % puerto
        self.datos = os.path.join(BASE, "reloj-" + letra.lower())
        shutil.rmtree(self.datos, ignore_errors=True)
        os.makedirs(os.path.join(self.datos, "flash"))
        with open(os.path.join(self.datos, "nvs-reloj.txt"), "w", newline="\n") as f:
            f.write(f"pin=1234\nsonido=0\nssid=Oficina\nclave=oficina123\npurl={PANEL}\npllave={LLAVE}\n"
                    f"oficina={letra}\n")
        self.proc = None

    def arrancar(self):
        env = dict(os.environ, RELOJ_SIM_DATOS=self.datos, RELOJ_SIM_PRUEBAS=self.control.rsplit(":", 1)[1],
                   RELOJ_SIM_OCULTO="1", RELOJ_SIM_SIN_NTP="1")
        self.proc = subprocess.Popen([EXE], env=env)

    def sim(self, ruta, **q):
        return pide(self.control + ruta + ("?" + urllib.parse.urlencode(q) if q else ""))[1]

    def estado(self):
        return dict(l.split("=", 1) for l in self.sim("/estado").splitlines() if "=" in l)

    def memoria(self):
        """{posición: dedo} de lo que guarda su lector"""
        m = self.estado().get("memoria", "")
        return {int(a): int(b) for a, b in (x.split(":") for x in m.split(",") if x)}

    def textos(self):
        r, encima = [], False
        for l in self.sim("/textos").splitlines():
            if l == "--- encima ---":
                encima = True
                continue
            pos, _, t = l.partition(" ")
            if "," in pos:
                x, y = pos.split(",")
                r.append((int(x), int(y), t, encima))
        return r

    def hay(self, t, encima=None):
        return any(t in x[2] and (encima is None or x[3] == encima) for x in self.textos())

    def esperar(self, cond, seg, que):
        fin = time.time() + seg
        while time.time() < fin:
            if cond():
                return True
            time.sleep(0.25)
        fallas.append("reloj %s: no pasó: %s" % (self.letra, que))
        print("   !! reloj %s: no pasó: %s" % (self.letra, que))
        print("      pantalla:", [x[2] for x in self.textos()])
        print("      estado:", self.estado())
        raise SystemExit("se corta la prueba")

    def tocar(self, t, exacto=False):
        time.sleep(ANIMACION)
        for x, y, s, _ in reversed(self.textos()):
            if (s == t) if exacto else (t in s):
                self.sim("/tocar", x=x, y=y)
                time.sleep(0.4)
                return
        fallas.append("reloj %s: no encontré para tocar: %s" % (self.letra, t))
        print("   !! no encontré para tocar:", t, [x[2] for x in self.textos()])
        raise SystemExit("se corta la prueba")

    def captura(self, nombre):
        os.makedirs(CAPTURAS, exist_ok=True)
        bmp = os.path.join(self.datos, nombre + ".bmp")
        time.sleep(ANIMACION)
        self.sim("/captura", v=0, archivo=bmp)
        Image.open(bmp).save(os.path.join(CAPTURAS, nombre + ".png"))
        os.remove(bmp)
        print("    captura", nombre)

    def cerrar_capa(self):
        self.esperar(lambda: not any(x[3] for x in self.textos()), 8, "se cierra el aviso")

    def entrar_admin(self):
        self.sim("/tocar", x=444, y=32)
        self.esperar(lambda: self.hay("Clave de administrador"), 5, "pantalla de clave")
        for k in "1234":
            self.tocar("[tecla] " + k, exacto=True)
        for x, y, s, _ in self.textos():
            if s.startswith("[tecla]") and s.endswith("\uf00c"):
                self.sim("/tocar", x=x, y=y)
        self.esperar(lambda: self.hay("Administración"), 5, "menú de administración")

    def salir_admin(self):
        for _ in range(4):  # desde una ficha o lista: «Volver» hasta el menú
            time.sleep(ANIMACION)
            if self.hay("Salir"):
                break
            self.tocar("Volver")
        self.tocar("Salir")
        self.esperar(lambda: self.hay("Pon tu dedo"), 5, "inicio")

    def registrar(self, nombre, dedo):
        self.entrar_admin()
        self.tocar("Registrar dedo")
        self.esperar(lambda: self.hay(nombre), 15, "lista de perfiles con " + nombre)
        self.tocar(nombre)
        self.esperar(lambda: self.hay("PASO 1 DE 2"), 5, "paso 1")
        self.sim("/dedo", n=dedo)
        self.esperar(lambda: self.hay("PASO 2 DE 2"), 8, "paso 2")
        self.sim("/dedo", n=dedo)
        self.esperar(lambda: self.hay("ya puede marcar"), 20, nombre + " registrado")
        self.tocar("Listo")
        self.salir_admin()

    def marcar(self, dedo, texto, nombre_captura=None, seg=8):
        self.sim("/dedo", n=dedo)
        self.esperar(lambda: self.hay(texto, True), seg, "saludo «%s»" % texto)
        if nombre_captura:
            self.captura(nombre_captura)
        self.cerrar_capa()

    def cerrar(self):
        if not self.proc:
            return
        try:
            self.sim("/salir")
        except Exception:
            pass
        try:
            self.proc.wait(10)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def huella_de(perfil):
    perf = json.loads(pide(PANEL + "/api/reloj/perfiles", llave=LLAVE)[1])
    return next((p["huellas"] for p in perf["perfiles"] if p["perfil"] == perfil), []), perf.get("plantillas", {})


# ------------------------------------------------------------------ arranque --
datos_falso = os.path.join(BASE, "falso")
shutil.rmtree(datos_falso, ignore_errors=True)
os.makedirs(datos_falso)
# Carla ya tenía un dedo (9) de antes, y el reloj B tiene una marca vieja en cola, sin oficina
with open(os.path.join(datos_falso, "huellas.json"), "w") as f:
    json.dump({"9": "carla"}, f)
srv = subprocess.Popen([sys.executable, "-I", os.path.join(AQUI, "servidor_falso.py"), "8075"],
                       env=dict(os.environ, RELOJ_FALSO_DATOS=datos_falso),
                       stdout=open(os.path.join(BASE, "falso-registro.txt"), "w"), stderr=subprocess.STDOUT)
a, b = Reloj("A", 8111), Reloj("B", 8112)
with open(os.path.join(b.datos, "flash", "cola.txt"), "w", newline="\n") as f:
    f.write("viejaSinOficina01;9;0\n")
time.sleep(1.5)
a.arrancar()
b.arrancar()
try:
    time.sleep(2)
    print("1. los dos arrancan y se conectan; cada uno muestra su oficina")
    for r in (a, b):
        r.esperar(lambda: r.estado().get("conectado") == "1", 30, "panel conectado")
        r.esperar(lambda: r.estado().get("hora_valida") == "1", 5, "hora desde el panel")
        revisar(r.estado().get("oficina") == r.letra, "reloj %s dice oficina %s" % (r.letra, r.letra))
        r.esperar(lambda: r.hay("OFICINA " + r.letra), 5, "OFICINA %s en el inicio" % r.letra)
    a.captura("01-inicio-oficina-a")
    b.captura("02-inicio-oficina-b")

    print("2. la marca vieja en cola de B (sin oficina) llega sin el campo")
    b.esperar(lambda: b.estado().get("cola") == "0", 20, "cola de B vacía")
    m = [x for x in falso()["marcas"] if x["perfil"] == "carla"]
    revisar(len(m) == 1 and not m[0]["con_oficina"] and m[0]["oficina"] == "A",
            "marca vieja de Carla sin campo oficina (el panel supone A): %s" % m)

    print("3. Ana registra su dedo (dedo 1) en el reloj A: A sube la plantilla")
    a.registrar("Ana", 1)
    huellas_ana = []
    a.esperar(lambda: huella_de("ana")[0], 10, "Ana con huella en el panel")
    huellas_ana, vers = huella_de("ana")
    n_ana = huellas_ana[0]
    a.esperar(lambda: str(n_ana) in huella_de("ana")[1], 20, "plantilla de Ana en el panel")
    print("    Ana = huella", n_ana, "| A:", a.estado().get("plantillas"))
    revisar(a.memoria().get(n_ana) == 1, "el lector de A tiene el dedo 1 en %d" % n_ana)

    print("4. Ana marca en B antes de que B tenga su plantilla: «No te conozco»")
    revisar(n_ana not in b.memoria(), "B todavía no tiene la huella %d" % n_ana)
    b.marcar(1, "No te conozco", "03-b-no-la-conoce-todavia")

    print("5. el «No te conozco» hace que B pida la lista y baje la plantilla (DownChar + Store)")
    b.esperar(lambda: b.memoria().get(n_ana) == 1, 40, "B tiene el dedo 1 en %d" % n_ana)
    print("    B:", b.estado().get("plantillas"))
    b.marcar(1, "Hola Ana", "04-b-hola-ana")
    b.esperar(lambda: b.estado().get("cola") == "0", 15, "marca de B enviada")
    m = [x for x in falso()["marcas"] if x["perfil"] == "ana"]
    revisar(len(m) == 1 and m[0]["oficina"] == "B" and m[0]["con_oficina"] and m[0]["tipo"] == "ENTRADA",
            "la entrada de Ana quedó en la oficina B: %s" % m)

    print("6. Ana pone el dedo en A al rato: el panel dice que ya marcó (la jornada es una sola)")
    a.sim("/dedo", n=1)
    a.esperar(lambda: a.hay("Ya marcaste, Ana", True), 15, "A corrige a «Ya marcaste, Ana»")
    a.captura("05-a-ya-marcaste")
    a.cerrar_capa()

    print("7. al revés: Bruno registra en B (dedo 2) y marca en A")
    b.registrar("Bruno", 2)
    b.esperar(lambda: huella_de("bruno")[0] and str(huella_de("bruno")[0][0]) in huella_de("bruno")[1], 20,
              "plantilla de Bruno en el panel")
    n_bruno = huella_de("bruno")[0][0]
    revisar(n_bruno != n_ana, "Bruno tiene otro número (%d)" % n_bruno)
    a.sim("/perfiles")  # en la placa lo hace solo cada 3 min o con un «No te conozco»
    a.esperar(lambda: a.memoria().get(n_bruno) == 2, 20, "A tiene el dedo 2 en %d" % n_bruno)
    a.marcar(2, "Hola Bruno", "06-a-hola-bruno")
    a.esperar(lambda: a.estado().get("cola") == "0", 15, "marca de A enviada")
    m = [x for x in falso()["marcas"] if x["perfil"] == "bruno"]
    revisar(len(m) == 1 and m[0]["oficina"] == "A", "la entrada de Bruno quedó en la oficina A: %s" % m)

    print("8. se suelta el dedo de Ana en A (Borrar): sale de A, del panel y después de B")
    a.entrar_admin()
    a.tocar("Personas")
    a.esperar(lambda: a.hay("Ana"), 10, "lista")
    a.tocar("Ana")
    a.esperar(lambda: a.hay("Dedo N"), 5, "ficha de Ana")
    a.tocar("Borrar")
    a.esperar(lambda: a.hay("¿Borrar el dedo"), 3, "confirmación")
    for x, y, s, enc in reversed(a.textos()):
        if s == "Borrar" and enc:
            a.sim("/tocar", x=x, y=y)
            break
    a.esperar(lambda: a.hay("Dedo borrado"), 15, "dedo borrado")
    a.cerrar_capa()
    a.salir_admin()
    revisar(n_ana not in a.memoria(), "A sacó la huella %d de su lector" % n_ana)
    revisar(str(n_ana) not in huella_de("ana")[1], "el panel ya no tiene la plantilla de Ana")
    b.sim("/perfiles")
    b.esperar(lambda: n_ana not in b.memoria(), 20, "B saca la huella %d de su lector (DeletChar)" % n_ana)
    revisar(b.memoria().get(n_bruno) == 2, "B sigue con Bruno (%d)" % n_bruno)
    b.marcar(1, "No te conozco", "07-b-dedo-soltado")
    b.entrar_admin()
    b.tocar("Ajustes")
    b.sim("/arrastrar", x=240, y=280, x2=240, y2=80)  # «Información del equipo» está al fondo
    time.sleep(1)
    if not b.hay("Panel:"):
        b.tocar("Información del equipo")
    b.esperar(lambda: b.hay("Panel:"), 5, "información")
    b.captura("08-b-informacion")
    print("    A:", a.estado().get("plantillas"), "| B:", b.estado().get("plantillas"))
    print("    marcas en el panel:", [(x["perfil"], x["tipo"], x["oficina"]) for x in falso()["marcas"]])
finally:
    a.cerrar()
    b.cerrar()
    srv.terminate()

print("\nRESULTADO:", "todo bien" if not fallas else "%d fallas" % len(fallas))
for f in fallas:
    print(" -", f)
sys.exit(1 if fallas else 0)
