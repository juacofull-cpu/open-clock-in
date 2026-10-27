# Reference server

`server.py` is a complete backend for the clock: Python standard library + SQLite, one file.
It implements [docs/API.md](../docs/API.md). Pillow is only needed to add photos.

## Run it

```sh
cd server
pip install pillow tzdata        # tzdata only on Windows
export CLOCK_TOKEN="$(python -c 'import secrets; print(secrets.token_urlsafe(32))')"
python server.py add-person ana "Ana" --photo ana.jpg
python server.py add-person bruno "Bruno"
python server.py serve --host 0.0.0.0 --port 8078
```

| Command | What it does |
|---|---|
| `serve [--host] [--port]` | HTTP server (default `127.0.0.1:8078`, or `$PORT`) |
| `add-person <id> <name> [--photo file]` | add or rename someone; any image becomes a 160×160 baseline JPEG |
| `remove-person <id>` | hide someone and release their fingers (their punches stay) |
| `list` | people, their finger numbers and whether they have a photo |
| `demo` | five demo people (Ana, Bruno, Diego, Eva, Carla) |
| `export-csv <file> [--from YYYY-MM-DD] [--to YYYY-MM-DD]` | punches in local time |

| Variable | Default | |
|---|---|---|
| `CLOCK_TOKEN` | — | **required**: long random text; the same one goes into the clock |
| `CLOCK_DB` | `server/clock.db` | SQLite file |
| `CLOCK_TZ` | `America/Santiago` | IANA zone that defines the working day |
| `CLOCK_OFFICES` | `A,B` | office letters the clocks may send |

## HTTPS (required by the clock)

The ESP32 only talks HTTPS and checks the certificate. Choose one of these:

- **Caddy** on a machine with a domain: `caddy reverse-proxy --from clock.example.com --to 127.0.0.1:8078`
  (it gets a Let's Encrypt certificate by itself).
- **Cloudflare Tunnel**, with no open ports: `cloudflared tunnel --url http://127.0.0.1:8078`.
- **A platform that gives you HTTPS** (Railway, Fly.io, Render…): deploy the `Dockerfile` in this
  folder, set `CLOCK_TOKEN`, and put `CLOCK_DB` on a persistent volume.

Then on the clock open `http://reloj.local` and set the address to `https://clock.example.com`, plus
the token and the office.

## Using another database

The HTTP part only calls the methods of the `Store` class. To keep the data in Postgres, MySQL,
Supabase, Firebase, Airtable or your HR system, rewrite `Store` with the same methods:

`people`, `person`, `put_person`, `remove_person`, `fingers`, `finger_owner`, `assign_finger`,
`release_finger`, `set_template`, `punch_by_id`, `last_punches`, `save_punch`, `punches_between`.

Tables (SQLite version):

```sql
people  (perfil TEXT PK, nombre TEXT, orden INT, foto BLOB, activo INT)
fingers (huella INT PK, perfil TEXT, plantilla BLOB)
punches (id TEXT PK, perfil TEXT, tipo TEXT, t_utc TEXT, oficina TEXT, repetida INT, respuesta TEXT)
```

You can also write your own server in any language from [docs/API.md](../docs/API.md) and test it
with the simulator: `python -I sim/probar_panel.py https://your-server your-token`.

## Security notes

- Fingerprint templates are biometric data. Many countries regulate them: get consent, keep the
  database private, and delete a person's fingers when they leave (`remove-person` does).
- Anyone with the token can read the people list and add punches: keep it out of git, and generate a
  new one if it leaks (then type it into each clock again).
- The server does not log the `Authorization` header.
