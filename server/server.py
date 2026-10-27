"""Open Clock-In reference server: the backend the clock talks to (contract: docs/API.md).

One file, Python 3.9+ standard library + SQLite. Pillow is only needed to add photos.
Storage lives in the `Store` class: to use Postgres, MySQL, Supabase, Firebase... rewrite
only that class (same methods), the HTTP part does not change.

    export CLOCK_TOKEN="$(python -c 'import secrets; print(secrets.token_urlsafe(32))')"
    python server.py add-person ana "Ana" --photo ana.jpg
    python server.py serve --port 8078
    python server.py export-csv punches.csv --from 2026-10-01 --to 2026-10-31

The clock only speaks HTTPS with a valid certificate: put this behind Caddy, nginx, a
Cloudflare Tunnel or a host that gives you HTTPS (see server/README.md).

Environment:
  CLOCK_TOKEN   required, long random text; the clock sends it as "Authorization: Bearer ..."
  CLOCK_DB      SQLite file (default: clock.db next to this file)
  CLOCK_TZ      IANA time zone used to decide the working day (default America/Santiago)
  CLOCK_OFFICES comma-separated office letters accepted from clocks (default A,B)
"""
import argparse
import base64
import csv
import hashlib
import hmac
import io
import json
import os
import re
import sqlite3
import sys
import threading
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.environ.get("CLOCK_DB", os.path.join(HERE, "clock.db"))
OFFICES = tuple(o.strip().upper() for o in os.environ.get("CLOCK_OFFICES", "A,B").split(",") if o.strip())
REPEAT_SECONDS = 120          # same person again within 2 min = "already punched", not stored
MAX_FUTURE = timedelta(minutes=5)
MAX_AGE = timedelta(days=7)   # queued punches older than this are rejected


def local_tz():
    name = os.environ.get("CLOCK_TZ", "America/Santiago")
    try:
        from zoneinfo import ZoneInfo
        return ZoneInfo(name)
    except Exception:  # Windows without the tzdata package: pip install tzdata
        print(f"[server] time zone {name!r} not available, using UTC (pip install tzdata)", file=sys.stderr)
        return timezone.utc


TZ = local_tz()


def short_hash(b):
    return hashlib.sha256(b).hexdigest()[:12]


def utc_now():
    return datetime.now(timezone.utc)


def iso(t):
    return t.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


# ------------------------------------------------------------------ storage --
class Store:
    """Everything the server keeps. Swap this class to use another database."""

    def __init__(self, path):
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.lock = threading.Lock()
        self.db.executescript("""
            PRAGMA journal_mode=WAL;
            CREATE TABLE IF NOT EXISTS people (
                perfil TEXT PRIMARY KEY, nombre TEXT NOT NULL, orden INTEGER NOT NULL DEFAULT 0,
                foto BLOB, activo INTEGER NOT NULL DEFAULT 1);
            CREATE TABLE IF NOT EXISTS fingers (
                huella INTEGER PRIMARY KEY, perfil TEXT NOT NULL REFERENCES people(perfil),
                plantilla BLOB);
            CREATE TABLE IF NOT EXISTS punches (
                id TEXT PRIMARY KEY, perfil TEXT NOT NULL, tipo TEXT NOT NULL, t_utc TEXT NOT NULL,
                oficina TEXT, repetida INTEGER NOT NULL, respuesta TEXT NOT NULL);
            CREATE INDEX IF NOT EXISTS punches_perfil_t ON punches(perfil, t_utc);
        """)

    # people
    def people(self):
        with self.lock:
            return self.db.execute("SELECT * FROM people WHERE activo = 1 ORDER BY orden, nombre").fetchall()

    def person(self, perfil):
        with self.lock:
            return self.db.execute("SELECT * FROM people WHERE perfil = ? AND activo = 1", (perfil,)).fetchone()

    def put_person(self, perfil, nombre, foto=None):
        with self.lock, self.db:
            n = self.db.execute("SELECT COALESCE(MAX(orden), 0) + 1 FROM people").fetchone()[0]
            self.db.execute("""INSERT INTO people (perfil, nombre, orden, foto) VALUES (?, ?, ?, ?)
                               ON CONFLICT(perfil) DO UPDATE SET nombre = excluded.nombre, activo = 1,
                               foto = COALESCE(excluded.foto, people.foto)""", (perfil, nombre, n, foto))

    def remove_person(self, perfil):
        with self.lock, self.db:  # punches stay for the reports; fingers are released
            self.db.execute("UPDATE people SET activo = 0 WHERE perfil = ?", (perfil,))
            self.db.execute("DELETE FROM fingers WHERE perfil = ?", (perfil,))

    # fingers (finger number in the sensor -> person; same number on every clock)
    def fingers(self):
        with self.lock:
            return self.db.execute("""SELECT f.huella, f.perfil, f.plantilla FROM fingers f
                                      JOIN people p ON p.perfil = f.perfil AND p.activo = 1""").fetchall()

    def finger_owner(self, huella):
        with self.lock:
            r = self.db.execute("""SELECT f.perfil FROM fingers f JOIN people p
                                   ON p.perfil = f.perfil AND p.activo = 1 WHERE huella = ?""", (huella,)).fetchone()
        return r[0] if r else None

    def assign_finger(self, huella, perfil):
        """True if assigned, False if the number belongs to another active person."""
        with self.lock, self.db:
            r = self.db.execute("""SELECT f.perfil, p.activo FROM fingers f LEFT JOIN people p
                                   ON p.perfil = f.perfil WHERE huella = ?""", (huella,)).fetchone()
            if r and r["perfil"] != perfil and r["activo"] == 1:
                return False
            if r and r["perfil"] != perfil:  # the old owner was removed: its template goes too
                self.db.execute("DELETE FROM fingers WHERE huella = ?", (huella,))
            self.db.execute("INSERT OR IGNORE INTO fingers (huella, perfil) VALUES (?, ?)", (huella, perfil))
            return True

    def release_finger(self, huella):
        with self.lock, self.db:
            self.db.execute("DELETE FROM fingers WHERE huella = ?", (huella,))

    def set_template(self, huella, data):
        with self.lock, self.db:
            return self.db.execute("UPDATE fingers SET plantilla = ? WHERE huella = ?", (data, huella)).rowcount > 0

    # punches
    def punch_by_id(self, id_):
        with self.lock:
            r = self.db.execute("SELECT respuesta FROM punches WHERE id = ?", (id_,)).fetchone()
        return json.loads(r[0]) if r else None

    def last_punches(self, perfil, until_iso):
        """Stored (non-repeated) punches of a person up to a moment, newest first."""
        with self.lock:
            return self.db.execute("""SELECT tipo, t_utc FROM punches WHERE perfil = ? AND repetida = 0
                                      AND t_utc <= ? ORDER BY t_utc DESC LIMIT 20""", (perfil, until_iso)).fetchall()

    def save_punch(self, id_, perfil, tipo, t, oficina, repetida, respuesta):
        with self.lock, self.db:
            self.db.execute("INSERT OR IGNORE INTO punches VALUES (?, ?, ?, ?, ?, ?, ?)",
                            (id_, perfil, tipo, iso(t), oficina, int(repetida), json.dumps(respuesta)))

    def punches_between(self, desde, hasta):
        with self.lock:
            return self.db.execute("""SELECT m.t_utc, m.perfil, COALESCE(p.nombre, m.perfil) AS nombre, m.tipo, m.oficina
                                      FROM punches m LEFT JOIN people p ON p.perfil = m.perfil
                                      WHERE m.repetida = 0 AND m.t_utc >= ? AND m.t_utc < ?
                                      ORDER BY m.t_utc""", (desde, hasta)).fetchall()


# --------------------------------------------------------------------- HTTP --
class Handler(BaseHTTPRequestHandler):
    store: Store = None
    token = ""
    server_version = "OpenClockIn/1.0"

    def log_message(self, fmt, *args):  # never log the Authorization header
        sys.stdout.write("[server] %s %s\n" % (self.address_string(), fmt % args))
        sys.stdout.flush()

    def reply(self, code, body, ctype="application/json", headers=None):
        data = body if isinstance(body, bytes) else json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype if ctype != "application/json" else "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def allowed(self):
        if not self.token:
            self.reply(503, {"detail": "CLOCK_TOKEN is not set on the server."})
            return False
        got = self.headers.get("Authorization", "")
        if hmac.compare_digest(got.encode(), ("Bearer " + self.token).encode()):
            return True
        self.reply(401, {"detail": "Wrong clock key."})
        return False

    def body(self):
        n = min(int(self.headers.get("Content-Length") or 0), 64 * 1024)
        try:
            d = json.loads(self.rfile.read(n) or b"{}")
        except ValueError:
            return None
        return d if isinstance(d, dict) else None

    def do_GET(self):
        if self.path == "/health":
            return self.reply(200, {"ok": True})
        if not self.allowed():
            return
        if self.path == "/api/reloj/perfiles":
            fingers = self.store.fingers()
            people = [{"perfil": p["perfil"], "nombre": p["nombre"],
                       "huellas": sorted(f["huella"] for f in fingers if f["perfil"] == p["perfil"]),
                       "foto": short_hash(p["foto"]) if p["foto"] else ""} for p in self.store.people()]
            templates = {str(f["huella"]): short_hash(f["plantilla"]) for f in fingers if f["plantilla"]}
            return self.reply(200, {"perfiles": people, "ahora": iso(utc_now()), "plantillas": templates})
        m = re.fullmatch(r"/api/reloj/fotos/([A-Za-z0-9_.-]{1,64})", self.path)
        if m:
            p = self.store.person(m.group(1))
            if not p or not p["foto"]:
                return self.reply(404, {"detail": "No photo."})
            return self.reply(200, p["foto"], "image/jpeg", {"ETag": short_hash(p["foto"])})
        m = re.fullmatch(r"/api/reloj/huellas/(\d{1,3})/plantilla", self.path)
        if m:
            f = next((f for f in self.store.fingers() if f["huella"] == int(m.group(1))), None)
            if not f or not f["plantilla"]:
                return self.reply(404, {"detail": "No template for that finger."})
            return self.reply(200, {"plantilla": base64.b64encode(f["plantilla"]).decode(),
                                    "version": short_hash(f["plantilla"])})
        self.reply(404, {"detail": "Not Found"})

    def do_DELETE(self):
        if not self.allowed():
            return
        m = re.fullmatch(r"/api/reloj/huellas/(\d{1,3})", self.path)
        if not m:
            return self.reply(404, {"detail": "Not Found"})
        self.store.release_finger(int(m.group(1)))
        self.reply(200, {"ok": True})

    def do_PUT(self):
        if not self.allowed():
            return
        m = re.fullmatch(r"/api/reloj/huellas/(\d{1,3})/plantilla", self.path)
        if not m:
            return self.reply(404, {"detail": "Not Found"})
        d = self.body()
        try:
            data = base64.b64decode((d or {}).get("plantilla") or "", validate=True)
        except (ValueError, TypeError):
            return self.reply(422, {"detail": "Invalid template."})
        if not 256 <= len(data) <= 2048:
            return self.reply(422, {"detail": "Template size must be 256-2048 bytes."})
        if not self.store.finger_owner(int(m.group(1))) or not self.store.set_template(int(m.group(1)), data):
            return self.reply(404, {"detail": "Huella sin perfil."})
        self.reply(200, {"ok": True, "version": short_hash(data)})

    def do_POST(self):
        if not self.allowed():
            return
        d = self.body()
        if d is None:
            return self.reply(422, {"detail": "Invalid JSON."})
        if self.path == "/api/reloj/huellas":
            h, p = d.get("huella"), d.get("perfil")
            if not isinstance(h, int) or isinstance(h, bool) or not 0 <= h <= 999:
                return self.reply(422, {"detail": "Invalid finger number."})
            if not isinstance(p, str) or not self.store.person(p):
                return self.reply(404, {"detail": "That person does not exist."})
            if not self.store.assign_finger(h, p):
                return self.reply(409, {"detail": "That finger number belongs to someone else."})
            return self.reply(200, {"ok": True})
        if self.path == "/api/reloj/marcas":
            return self.punch(d)
        self.reply(404, {"detail": "Not Found"})

    def punch(self, d):
        id_, h = d.get("id"), d.get("huella")
        if not isinstance(id_, str) or not re.fullmatch(r"[A-Za-z0-9-]{8,40}", id_) \
                or not isinstance(h, int) or isinstance(h, bool):
            return self.reply(422, {"detail": "Invalid punch."})
        office = d.get("oficina")
        if office is None:
            office = OFFICES[0] if OFFICES else "A"
        if not isinstance(office, str) or office.upper() not in OFFICES:
            return self.reply(422, {"detail": "Unknown office."})
        office = office.upper()
        now = utc_now()
        t = now
        if d.get("hora"):
            try:
                t = datetime.strptime(d["hora"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
            except (ValueError, TypeError):
                return self.reply(422, {"detail": "Invalid time."})
            if t > now + MAX_FUTURE or t < now - MAX_AGE:
                return self.reply(422, {"detail": "Time out of range."})
        with PUNCH_LOCK:  # one punch at a time: the type depends on the previous one
            old = self.store.punch_by_id(id_)
            if old is not None:  # the clock retries with the same id: same answer, no duplicate
                return self.reply(200, old)
            perfil = self.store.finger_owner(h)
            if not perfil:
                return self.reply(404, {"detail": "Huella sin perfil."})
            before = self.store.last_punches(perfil, iso(t))
            day = t.astimezone(TZ).date()
            last = before[0] if before else None
            last_t = datetime.strptime(last["t_utc"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc) if last else None
            repeated = last is not None and (t - last_t).total_seconds() < REPEAT_SECONDS
            if repeated:
                kind = last["tipo"]
            else:
                today = last is not None and last_t.astimezone(TZ).date() == day
                kind = "SALIDA" if today and last["tipo"] == "ENTRADA" else "ENTRADA"
            answer = {"perfil": perfil, "nombre": self.store.person(perfil)["nombre"], "tipo": kind,
                      "repetida": repeated, "hora": t.astimezone(TZ).strftime("%H:%M")}
            self.store.save_punch(id_, perfil, kind, t, office, repeated, answer)
        self.reply(200, answer)


PUNCH_LOCK = threading.Lock()


# ---------------------------------------------------------------------- CLI --
def photo_jpeg(path):
    """Any image -> what the clock can draw: baseline JPEG 160x160, no metadata."""
    from PIL import Image, ImageOps
    im = ImageOps.exif_transpose(Image.open(path)).convert("RGB")
    im = ImageOps.fit(im, (160, 160), Image.LANCZOS)
    out = io.BytesIO()
    im.save(out, "JPEG", quality=80, progressive=False, optimize=False)
    return out.getvalue()


def demo_photo(color):
    from PIL import Image, ImageDraw
    im = Image.new("RGB", (160, 160), color)
    d = ImageDraw.Draw(im)
    d.ellipse((45, 22, 115, 98), fill=(250, 220, 190))
    d.ellipse((20, 100, 140, 210), fill=tuple(c // 2 for c in color))
    out = io.BytesIO()
    im.save(out, "JPEG", quality=80, progressive=False)
    return out.getvalue()


def main():
    ap = argparse.ArgumentParser(description="Open Clock-In reference server")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("serve", help="run the HTTP server")
    s.add_argument("--host", default="127.0.0.1")
    s.add_argument("--port", type=int, default=int(os.environ.get("PORT", 8078)))
    a = sub.add_parser("add-person", help="add or rename a person (and optionally set a photo)")
    a.add_argument("perfil", help="short id, e.g. ana")
    a.add_argument("nombre", help="name shown on the clock, e.g. Ana")
    a.add_argument("--photo", help="image file (needs Pillow)")
    r = sub.add_parser("remove-person", help="hide a person and release their fingers (punches are kept)")
    r.add_argument("perfil")
    sub.add_parser("list", help="people and their fingers")
    sub.add_parser("demo", help="add 5 demo people (two with photos)")
    e = sub.add_parser("export-csv", help="punches to CSV (local time)")
    e.add_argument("file")
    e.add_argument("--from", dest="desde", help="YYYY-MM-DD (local), default: 30 days ago")
    e.add_argument("--to", dest="hasta", help="YYYY-MM-DD (local, inclusive), default: today")
    args = ap.parse_args()
    store = Store(DB_PATH)

    if args.cmd == "serve":
        Handler.store, Handler.token = store, os.environ.get("CLOCK_TOKEN", "")
        if not Handler.token:
            print("[server] CLOCK_TOKEN is empty: every /api/reloj call will answer 503", file=sys.stderr)
        print(f"[server] http://{args.host}:{args.port}  db={DB_PATH}  tz={TZ}  offices={','.join(OFFICES)}", flush=True)
        ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()
    elif args.cmd == "add-person":
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", args.perfil):
            sys.exit("perfil: letters, numbers, _ . - (max 64)")
        store.put_person(args.perfil, args.nombre, photo_jpeg(args.photo) if args.photo else None)
        print("ok", args.perfil)
    elif args.cmd == "remove-person":
        store.remove_person(args.perfil)
        print("ok")
    elif args.cmd == "list":
        fingers = store.fingers()
        for p in store.people():
            mine = [f["huella"] for f in fingers if f["perfil"] == p["perfil"]]
            print(f"{p['perfil']:<16} {p['nombre']:<24} fingers={mine} photo={'yes' if p['foto'] else 'no'}")
    elif args.cmd == "demo":
        for perfil, nombre, color in [("ana", "Ana", (236, 112, 99)), ("bruno", "Bruno", (93, 173, 226)),
                                      ("diego", "Diego", None), ("eva", "Eva", None), ("carla", "Carla", None)]:
            store.put_person(perfil, nombre, demo_photo(color) if color else None)
        print("ok: ana, bruno, diego, eva, carla")
    elif args.cmd == "export-csv":
        today = datetime.now(TZ).date()
        d0 = datetime.strptime(args.desde, "%Y-%m-%d").date() if args.desde else today - timedelta(days=30)
        d1 = datetime.strptime(args.hasta, "%Y-%m-%d").date() if args.hasta else today
        start = datetime(d0.year, d0.month, d0.day, tzinfo=TZ)
        end = datetime(d1.year, d1.month, d1.day, tzinfo=TZ) + timedelta(days=1)
        rows = store.punches_between(iso(start), iso(end))
        with open(args.file, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["date", "time", "person", "name", "type", "office"])
            for m in rows:
                t = datetime.strptime(m["t_utc"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc).astimezone(TZ)
                w.writerow([t.date().isoformat(), t.strftime("%H:%M:%S"), m["perfil"], m["nombre"],
                            "IN" if m["tipo"] == "ENTRADA" else "OUT", m["oficina"] or ""])
        print(f"{len(rows)} punches -> {args.file}")


if __name__ == "__main__":
    main()
