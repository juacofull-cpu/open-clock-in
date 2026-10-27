"""Panel falso para probar el simulador sin un servidor de verdad (rutas /api/reloj/* del
contrato docs/API.md). Guarda todo en memoria y las huellas en
.sim/falso/huellas.json. Sirve dos fotos de prueba (Ana y Bruno).

Uso:  python -I servidor_falso.py [puerto=8078]     (llave: RELOJ_TOKEN, por defecto "prueba")
Extra para las pruebas: GET /falso/marcas (las marcas guardadas, sin llave).
Dos relojes (contrato, 2026-10-26): oficina en cada marca ("A" si falta) y plantillas del lector
(PUT/GET /api/reloj/huellas/{n}/plantilla, "plantillas" en /api/reloj/perfiles), guardadas en
plantillas.json de la carpeta de datos.
"""
import base64
import hashlib
import io
import json
import os
import re
import sys
import threading
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
RAIZ = os.environ.get("RELOJ_SIM_RAIZ", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".sim"))

PUERTO = int(sys.argv[1]) if len(sys.argv) > 1 else 8078
LLAVE = os.environ.get("RELOJ_TOKEN", "prueba")
DATOS = os.environ.get("RELOJ_FALSO_DATOS", os.path.join(RAIZ, "falso"))
os.makedirs(DATOS, exist_ok=True)

try:
    from zoneinfo import ZoneInfo
    CHILE = ZoneInfo("America/Santiago")
except Exception:  # Windows sin tzdata: octubre es horario de verano (-3)
    CHILE = timezone(timedelta(hours=-3))

# RELOJ_FALSO_FECHA=2026-12-24T16:30 (hora de Chile): el panel vive en esa fecha, para probar los temas
DESFASE = timedelta(0)
if os.environ.get("RELOJ_FALSO_FECHA"):
    _f = datetime.strptime(os.environ["RELOJ_FALSO_FECHA"], "%Y-%m-%dT%H:%M").replace(tzinfo=CHILE)
    DESFASE = _f - datetime.now(timezone.utc)


def ahora_utc():
    return datetime.now(timezone.utc) + DESFASE

PERFILES = [("ana", "Ana"), ("bruno", "Bruno"), ("diego", "Diego"), ("eva", "Eva"),
            ("carla", "Carla")]
COLORES = {"ana": ((236, 112, 99), (120, 40, 31)), "bruno": ((93, 173, 226), (21, 67, 96))}


def foto_prueba(perfil):
    """JPEG baseline 160x160 como el que manda el panel (cara simple de color)."""
    ruta = os.path.join(DATOS, perfil + ".jpg")
    if not os.path.exists(ruta):
        from PIL import Image, ImageDraw
        claro, oscuro = COLORES[perfil]
        im = Image.new("RGB", (160, 160), claro)
        d = ImageDraw.Draw(im)
        d.ellipse((45, 22, 115, 98), fill=(250, 220, 190))
        d.ellipse((20, 100, 140, 210), fill=oscuro)
        d.ellipse((63, 50, 73, 60), fill=(40, 40, 40))
        d.ellipse((87, 50, 97, 60), fill=(40, 40, 40))
        d.arc((65, 60, 95, 85), 20, 160, fill=(150, 50, 50), width=3)
        d.text((6, 4), perfil.upper(), fill=(255, 255, 255))
        im.save(ruta, "JPEG", quality=80, progressive=False, optimize=False)
    with open(ruta, "rb") as f:
        return f.read()


FOTOS = {p: foto_prueba(p) for p in COLORES}
VERSION = {p: hashlib.sha256(j).hexdigest()[:12] for p, j in FOTOS.items()}

candado = threading.Lock()
ARCH_HUELLAS = os.path.join(DATOS, "huellas.json")
huellas = {}
if os.path.exists(ARCH_HUELLAS):
    huellas = {int(k): v for k, v in json.load(open(ARCH_HUELLAS, encoding="utf-8")).items()}
ARCH_PLANTILLAS = os.path.join(DATOS, "plantillas.json")
plantillas = {}   # número -> bytes
if os.path.exists(ARCH_PLANTILLAS):
    plantillas = {int(k): base64.b64decode(v) for k, v in json.load(open(ARCH_PLANTILLAS, encoding="utf-8")).items()}
OFICINAS = ("A", "B")
marcas = {}       # id -> respuesta
guardadas = []    # marcas no repetidas: {perfil, tipo, hora_utc}


def guardar_huellas():
    with open(ARCH_HUELLAS, "w", encoding="utf-8") as f:
        json.dump(huellas, f)
    with open(ARCH_PLANTILLAS, "w", encoding="utf-8") as f:
        json.dump({k: base64.b64encode(v).decode() for k, v in plantillas.items()}, f)


def version_plantilla(b):
    return hashlib.sha256(b).hexdigest()[:12]


def nombre_de(perfil):
    return dict(PERFILES).get(perfil)


class Pedido(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stdout.write("[falso] " + (fmt % args) + "\n")
        sys.stdout.flush()

    def responder(self, codigo, cuerpo, tipo="application/json", extra=None):
        datos = cuerpo if isinstance(cuerpo, bytes) else json.dumps(cuerpo).encode("utf-8")
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(datos)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(datos)

    def autorizado(self):
        if self.headers.get("Authorization", "") == "Bearer " + LLAVE:
            return True
        self.responder(401, {"detail": "Llave del reloj incorrecta."})
        return False

    def cuerpo(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            return json.loads(self.rfile.read(n) or b"{}")
        except ValueError:
            return None

    def do_GET(self):
        if self.path == "/falso/marcas":
            with candado:
                return self.responder(200, {"marcas": guardadas, "huellas": huellas,
                                            "plantillas": {k: version_plantilla(v) for k, v in plantillas.items()}})
        if not self.autorizado():
            return
        if self.path == "/api/reloj/perfiles":
            with candado:
                lista = [{"perfil": p, "nombre": n, "huellas": sorted(h for h, q in huellas.items() if q == p),
                          "foto": VERSION.get(p, "")} for p, n in PERFILES]
                vers = {str(k): version_plantilla(v) for k, v in plantillas.items()}
            ahora = ahora_utc().strftime("%Y-%m-%dT%H:%M:%SZ")
            return self.responder(200, {"perfiles": lista, "ahora": ahora, "plantillas": vers})
        m = re.fullmatch(r"/api/reloj/huellas/(\d+)/plantilla", self.path)
        if m:
            with candado:
                b = plantillas.get(int(m.group(1)))
            if b is None:
                return self.responder(404, {"detail": "Esa huella no tiene plantilla."})
            return self.responder(200, {"plantilla": base64.b64encode(b).decode(), "version": version_plantilla(b)})
        m = re.fullmatch(r"/api/reloj/fotos/([a-z0-9_-]+)", self.path)
        if m and m.group(1) in FOTOS:
            p = m.group(1)
            return self.responder(200, FOTOS[p], "image/jpeg", {"ETag": VERSION[p]})
        self.responder(404, {"detail": "Not Found"})

    def do_DELETE(self):
        if not self.autorizado():
            return
        m = re.fullmatch(r"/api/reloj/huellas/(\d+)", self.path)
        if not m:
            return self.responder(404, {"detail": "Not Found"})
        with candado:
            huellas.pop(int(m.group(1)), None)
            plantillas.pop(int(m.group(1)), None)  # soltar el dedo borra también su plantilla
            guardar_huellas()
        self.responder(200, {"ok": True})

    def do_PUT(self):
        if not self.autorizado():
            return
        m = re.fullmatch(r"/api/reloj/huellas/(\d+)/plantilla", self.path)
        if not m:
            return self.responder(404, {"detail": "Not Found"})
        d = self.cuerpo()
        try:
            b = base64.b64decode((d or {}).get("plantilla") or "", validate=True)
        except (ValueError, TypeError):
            return self.responder(422, {"detail": "Plantilla inválida."})
        if not 256 <= len(b) <= 2048:
            return self.responder(422, {"detail": "Plantilla de largo inválido."})
        h = int(m.group(1))
        with candado:
            if h not in huellas:
                return self.responder(404, {"detail": "Huella sin perfil."})
            plantillas[h] = b
            guardar_huellas()
        self.responder(200, {"ok": True})

    def do_POST(self):
        if not self.autorizado():
            return
        d = self.cuerpo()
        if not isinstance(d, dict):
            return self.responder(422, {"detail": "JSON inválido."})
        if self.path == "/api/reloj/huellas":
            h, p = d.get("huella"), d.get("perfil")
            if not isinstance(h, int) or not 0 <= h <= 999:
                return self.responder(422, {"detail": "Número de huella inválido."})
            if not nombre_de(p):
                return self.responder(404, {"detail": "Ese perfil no existe."})
            with candado:
                if huellas.get(h, p) != p:
                    return self.responder(409, {"detail": "Ese número de huella ya es de otro perfil."})
                huellas[h] = p
                guardar_huellas()
            return self.responder(200, {"ok": True})
        if self.path == "/api/reloj/marcas":
            return self.marcar(d)
        self.responder(404, {"detail": "Not Found"})

    def marcar(self, d):
        id_, h = d.get("id"), d.get("huella")
        if not isinstance(id_, str) or not re.fullmatch(r"[A-Za-z0-9-]{8,40}", id_) or not isinstance(h, int):
            return self.responder(422, {"detail": "Marca inválida."})
        oficina = d.get("oficina", "A")
        if oficina not in OFICINAS:
            return self.responder(422, {"detail": "Oficina inválida."})
        ahora = ahora_utc()
        hora = ahora
        if d.get("hora"):
            try:
                hora = datetime.strptime(d["hora"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
            except ValueError:
                return self.responder(422, {"detail": "Hora inválida."})
            if hora > ahora + timedelta(minutes=5) or hora < ahora - timedelta(days=7):
                return self.responder(422, {"detail": "Hora fuera de rango."})
        with candado:
            if id_ in marcas:
                return self.responder(200, marcas[id_])
            p = huellas.get(h)
            if not p:
                return self.responder(404, {"detail": "Huella sin perfil."})
            dia = hora.astimezone(CHILE).date()
            antes = [m for m in guardadas if m["perfil"] == p and m["t"] <= hora]
            hoy = [m for m in antes if m["t"].astimezone(CHILE).date() == dia]
            repetida = bool(antes) and (hora - antes[-1]["t"]).total_seconds() < 120
            if repetida:
                tipo = antes[-1]["tipo"]
            else:
                tipo = "SALIDA" if hoy and hoy[-1]["tipo"] == "ENTRADA" else "ENTRADA"
                guardadas.append({"perfil": p, "tipo": tipo, "t": hora, "oficina": oficina,
                                  "con_oficina": "oficina" in d})
                guardadas.sort(key=lambda m: m["t"])
            r = {"perfil": p, "nombre": nombre_de(p), "tipo": tipo, "repetida": repetida,
                 "hora": hora.astimezone(CHILE).strftime("%H:%M")}
            marcas[id_] = r
        self.responder(200, r)


json_default = json.JSONEncoder.default
json.JSONEncoder.default = lambda s, o: o.isoformat() if isinstance(o, datetime) else json_default(s, o)

if __name__ == "__main__":
    print(f"[falso] panel falso en http://127.0.0.1:{PUERTO}  (llave {LLAVE!r}, datos {DATOS})", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PUERTO), Pedido).serve_forever()
