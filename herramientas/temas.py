"""Mete los temas de temas/*.json dentro del programa: escribe src/temas_incluidos.h.

Uso:  python -I herramientas/temas.py      (después de editar un tema; luego compilar)
Van compactados (sin espacios). Si dos temas se pisan en las fechas, gana el que va primero
(orden alfabético del archivo); un /tema.json bajado del panel gana a todos.
"""
import glob
import json
import os

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
partes, total = [], 0
for ruta in sorted(glob.glob(os.path.join(RAIZ, "temas", "*.json"))):
    with open(ruta, encoding="utf-8") as f:
        t = json.load(f)
    for k in ("nombre", "desde", "hasta"):
        if k not in t:
            raise SystemExit(f"{ruta}: falta «{k}»")
    s = json.dumps(t, ensure_ascii=False, separators=(",", ":"))
    if ')json"' in s:
        raise SystemExit(f"{ruta}: no puede tener )json\"")
    total += len(s.encode("utf-8"))
    partes.append(f'  // {os.path.basename(ruta)}: {t["nombre"]}, {t["desde"]} al {t["hasta"]}\n  R"json({s})json",\n')
with open(os.path.join(RAIZ, "src", "temas_incluidos.h"), "w", encoding="utf-8", newline="\n") as f:
    f.write("// Lo escribe herramientas/temas.py desde temas/*.json: no editar a mano.\n#pragma once\n\n")
    f.write("static const char* const TEMAS_INCLUIDOS[] = {\n" + "".join(partes) + "};\n")
print(f"{len(partes)} temas, {total} bytes")
