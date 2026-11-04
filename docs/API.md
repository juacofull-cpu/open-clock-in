# Clock ↔ server API

This is everything the clock expects from a backend. `server/server.py` implements it with
SQLite; any language or database works as long as it answers the same way.

Field names are Spanish because the firmware is: `perfil` = person id, `nombre` = name,
`huella` = finger number in the sensor, `marca` = punch, `plantilla` = fingerprint template,
`oficina` = office, `ENTRADA`/`SALIDA` = in/out, `repetida` = repeated.

## Basics

- HTTPS only, with a certificate the ESP32 can validate (Let's Encrypt and the usual public CAs
  work; self-signed does not). The PC simulator also accepts `http://`.
- JSON, UTF-8. Errors: an HTTP code plus `{"detail": "..."}`.
- Every `/api/reloj/*` request carries `Authorization: Bearer <CLOCK_TOKEN>`. Wrong key → `401`.
  No key configured on the server → `503`. Compare in constant time.
- The token and the server address are typed in on the clock's own web page (`http://reloj.local`)
  and stored in the ESP32's NVS. They never go into the firmware source.

## People

The server owns the list of people. The clock never creates people; it only links a finger to
someone who already exists.

### `GET /api/reloj/perfiles`

```json
{
  "perfiles": [
    {"perfil": "ana", "nombre": "Ana", "huellas": [1, 7], "foto": "3f9a0c12be41",
     "ultima": {"tipo": "ENTRADA", "hora": "2026-10-25T02:50:00Z"}},
    {"perfil": "diego", "nombre": "Diego", "huellas": [], "foto": "", "ultima": null}
  ],
  "plantillas": {"1": "a1b2c3d4e5f6", "7": "0718293a4b5c"},
  "ahora": "2026-10-25T11:02:11Z",
  "jornada_horas": 16
}
```

- Order = the order shown on the clock.
- `foto`: a short version string of the photo (e.g. the first 12 hex chars of its sha256), or `""` for
  no photo (the clock draws a silhouette).
- `plantillas`: finger number → template version, only for fingers that have an uploaded template.
- `ahora`: server time in UTC. The clock uses it when it has no NTP.
- `ultima`: that person's newest stored punch of the last `jornada_horas` hours, from **any** clock
  (`tipo` + UTC `hora`), or `null`. The clock uses it to greet "Nos vemos" when the IN was punched on
  another clock or before midnight. Optional: an older server without it still works (the clock then
  only knows its own punches).
- `jornada_horas`: longest shift in hours (default 16, see the punch rule below). Optional.
- The clock asks for this every 3 minutes, and again soon after a "No te conozco" (unknown finger).

### `GET /api/reloj/fotos/{perfil}`

`image/jpeg`, **baseline (not progressive)**, 160×160, ~quality 80, no metadata; `ETag` = the same
version as `foto`. `404` if the person has no photo. The clock caches it and only downloads it again
when the version changes.

## Fingers

Finger numbers (0–999) are unique across the whole server, so the same number means the same
person on every clock.

### `POST /api/reloj/huellas` `{"huella": 7, "perfil": "ana"}`

Links a finger number to a person. `200 {"ok": true}`. `404` if the person does not exist.
`409` if the number already belongs to **another** person (same person = ok). `422` if invalid.

### `DELETE /api/reloj/huellas/{huella}`

Releases a number and deletes its template. `200 {"ok": true}`, even if it did not exist.

### `PUT /api/reloj/huellas/{huella}/plantilla` `{"plantilla": "<base64>"}`

After enrolling, the clock uploads the raw template from the sensor (AS608: 512 bytes) so other
clocks can download it. Accept 256–2048 bytes. `200 {"ok": true, "version": "<12 chars>"}`.
`422` for bad base64 or size; `404 {"detail": "Huella sin perfil."}` if the number has no person.

### `GET /api/reloj/huellas/{huella}/plantilla`

`{"plantilla": "<base64>", "version": "..."}`, or `404`. Clocks download templates they do not have
(or whose version changed) and store them in their sensor. A number that is no longer on the
server is deleted from the sensor.

## Punches

### `POST /api/reloj/marcas`

```json
{"id": "a1b2c3d4e5f60718", "huella": 7, "hora": "2026-10-25T11:02:11Z", "oficina": "A"}
```

- `id`: 8–40 chars `[A-Za-z0-9-]`, made by the clock. **Idempotent**: the same `id` again must get
  the same answer and must not create a second punch (the clock retries after network errors).
- `hora`: UTC, when the finger was placed. It can be old if the punch was queued offline. Optional
  (missing = server time). More than 5 minutes in the future or older than 7 days → `422`.
- `oficina`: optional, which clock it came from (missing = the first office). Unknown → `422`.

The server finds the person of that finger (`404 {"detail": "Huella sin perfil."}` if none) and
decides the type with that person's previous punch, **with no day boundary** (shifts can cross
midnight, e.g. 22:00 → 06:30):

- the previous one is an `ENTRADA` at most `jornada_horas` (16 h) ago → `SALIDA`
- anything else (no previous punch, a `SALIDA`, or an `ENTRADA` older than 16 h = forgotten OUT) → `ENTRADA`
- less than 120 s after their previous punch → `repetida: true`, same type as before, not stored

If an older punch arrives late (offline queue), the punches after it are re-typed with the same rule.
In and out can happen at different offices (in at A, out at B = one working day).

```json
{"perfil": "ana", "nombre": "Ana", "tipo": "ENTRADA", "repetida": false, "hora": "08:02"}
```

`hora` is local time, `HH:MM`. The screen shows "Hola Ana" / "Nos vemos Ana" / "Ya marcaste, Ana".

## What the clock does offline

- It keeps the last `/perfiles` answer (with each person's `ultima`) and today's punches, so it can
  still greet people. At midnight it moves each person's last punch into their cached profile before
  clearing "today", so someone who came in at 23:50 still gets "Nos vemos" at 00:22 offline.
- It greets with the newest of its own punch and `ultima`; on a tie (same time) the server wins.
- New punches go to a queue in flash (`/cola.txt`) and are sent in order when the network is back.
  If the server's answer differs from what the clock guessed, the server wins.
- Enrolling and deleting fingers need the server: offline they show "Necesita internet".

## Optional

- `GET /health` → `200 {"ok": true}`, no key. The clock does not use it.
