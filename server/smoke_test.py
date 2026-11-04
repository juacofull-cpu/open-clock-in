"""Quick check of server.py without hardware: people, fingers, templates, punches, idempotency.

Run:  CLOCK_TOKEN=x CLOCK_DB=/tmp/clock.db python server/smoke_test.py
"""
import base64
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
TOKEN = os.environ["CLOCK_TOKEN"]
DB = os.environ["CLOCK_DB"]
URL = "http://127.0.0.1:8090"
for f in (DB, DB + "-wal", DB + "-shm"):
    if os.path.exists(f):
        os.remove(f)


def call(method, path, body=None, token=TOKEN):
    req = urllib.request.Request(URL + path, method=method, data=json.dumps(body).encode() if body is not None else None,
                                 headers={"Content-Type": "application/json", **({"Authorization": "Bearer " + token} if token else {})})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            data = r.read()
            return r.status, json.loads(data) if r.headers.get_content_type() == "application/json" else data
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        sys.exit(1)


subprocess.run([sys.executable, os.path.join(HERE, "server.py"), "demo"], check=True)
srv = subprocess.Popen([sys.executable, os.path.join(HERE, "server.py"), "serve", "--port", "8090"])
try:
    for _ in range(50):
        try:
            urllib.request.urlopen(URL + "/health", timeout=1)
            break
        except OSError:
            time.sleep(0.1)
    check(call("GET", "/api/reloj/perfiles", token="wrong")[0] == 401, "wrong key -> 401")
    code, p = call("GET", "/api/reloj/perfiles")
    check(code == 200 and [x["perfil"] for x in p["perfiles"]][:2] == ["ana", "bruno"], "people list")
    check(p["perfiles"][0]["foto"] != "", "demo photo version")
    code, jpg = call("GET", "/api/reloj/fotos/ana")
    check(code == 200 and jpg[:2] == b"\xff\xd8", "photo is a JPEG")
    check(call("POST", "/api/reloj/huellas", {"huella": 1, "perfil": "ana"})[0] == 200, "assign finger 1 to ana")
    check(call("POST", "/api/reloj/huellas", {"huella": 1, "perfil": "bruno"})[0] == 409, "finger 1 taken -> 409")
    check(call("POST", "/api/reloj/huellas", {"huella": 2, "perfil": "nobody"})[0] == 404, "unknown person -> 404")
    tpl = base64.b64encode(bytes(range(256)) * 2).decode()
    code, r = call("PUT", "/api/reloj/huellas/1/plantilla", {"plantilla": tpl})
    check(code == 200 and len(r["version"]) == 12, "upload template")
    check(call("GET", "/api/reloj/huellas/1/plantilla")[1]["plantilla"] == tpl, "download template")
    check(call("GET", "/api/reloj/perfiles")[1]["plantillas"] == {"1": r["version"]}, "template listed")
    now = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    code, a = call("POST", "/api/reloj/marcas", {"id": "punch-0001", "huella": 1, "hora": now, "oficina": "B"})
    check(code == 200 and a["tipo"] == "ENTRADA" and not a["repetida"] and a["nombre"] == "Ana", "first punch = ENTRADA")
    check(call("POST", "/api/reloj/marcas", {"id": "punch-0001", "huella": 1, "hora": now})[1] == a, "same id = same answer")
    check(call("POST", "/api/reloj/marcas", {"id": "punch-0002", "huella": 1})[1]["repetida"], "again within 2 min = repetida")
    check(call("POST", "/api/reloj/marcas", {"id": "punch-0003", "huella": 9})[0] == 404, "unknown finger -> 404")
    check(call("POST", "/api/reloj/marcas", {"id": "punch-0004", "huella": 1, "oficina": "Z"})[0] == 422, "unknown office -> 422")
    ultima = {x["perfil"]: x["ultima"] for x in call("GET", "/api/reloj/perfiles")[1]["perfiles"]}
    check(ultima["ana"] == {"tipo": "ENTRADA", "hora": now} and ultima["diego"] is None, "perfiles: last punch")

    # 24/7: a shift crosses midnight; an IN with no OUT for more than 16 h is a forgotten OUT
    from datetime import datetime, timedelta, timezone
    from zoneinfo import ZoneInfo
    tz = ZoneInfo(os.environ.get("CLOCK_TZ", "America/Santiago"))
    day0 = datetime.now(tz).replace(hour=0, minute=0, second=0, microsecond=0)

    def at(days, h, m):  # local time `days` days from today -> UTC string
        return (day0 + timedelta(days=days, hours=h, minutes=m)).astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    def kind(id_, finger, t):
        return call("POST", "/api/reloj/marcas", {"id": id_, "huella": finger, "hora": t})[1]["tipo"]

    call("POST", "/api/reloj/huellas", {"huella": 3, "perfil": "diego"})
    check(kind("night-0001", 3, at(-3, 23, 50)) == "ENTRADA", "in at 23:50")
    check(kind("night-0002", 3, at(-2, 0, 22)) == "SALIDA", "out at 00:22 the next day (crosses midnight)")
    check(kind("night-0003", 3, at(-2, 1, 0)) == "ENTRADA", "in at 01:00")
    check(kind("night-0004", 3, at(-2, 17, 30)) == "ENTRADA", "16.5 h later = forgot the OUT, this is a new IN")
    call("POST", "/api/reloj/huellas", {"huella": 4, "perfil": "eva"})
    check(kind("late-0002", 4, at(-4, 0, 30)) == "ENTRADA", "queued punch arrives first")
    check(kind("late-0001", 4, at(-5, 23, 50)) == "ENTRADA", "the older one arrives late")
    import sqlite3
    with sqlite3.connect(DB) as db:
        later = db.execute("SELECT tipo FROM punches WHERE id = 'late-0002'").fetchone()[0]
    check(later == "SALIDA", "the late punch fixed the type of the one after it")
    check(call("GET", "/api/reloj/perfiles")[1]["jornada_horas"] == 16, "perfiles: jornada_horas")

    check(call("DELETE", "/api/reloj/huellas/1")[0] == 200, "release finger")
    check(call("GET", "/api/reloj/huellas/1/plantilla")[0] == 404, "template gone with the finger")
    print("smoke test: all good")
finally:
    srv.terminate()
