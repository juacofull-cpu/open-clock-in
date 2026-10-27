"""
Lamina con la CLI de Bambu Studio (Bambu A1, 0.20 mm, PLA):
  - la caja completa (3 piezas en una placa)
  - la prueba de calce (franja del frente + repisa), para imprimir primero
Confirma que laminan sin errores y saca tiempo y gramos. Corre despues de verificar.py.
Uso:  python laminar.py
"""
import json
import os
import re
import subprocess
import sys
import zipfile
from pathlib import Path

SALIDA = Path(os.environ.get("CASE_OUT", Path(__file__).resolve().parent / "out"))
BAMBU = Path(os.environ.get("BAMBU_STUDIO", r"C:/Program Files/Bambu Studio/bambu-studio.exe"))
PERFILES = BAMBU.parent / "resources/profiles/BBL"
MAQUINA = PERFILES / "machine/Bambu Lab A1 0.4 nozzle.json"
PROCESO = PERFILES / "process/0.20mm Standard @BBL A1.json"
FILAMENTO = PERFILES / "filament/Bambu PLA Basic @BBL A1.json"
# la A1 viene con la placa PEI texturizada (PLA a 65 grados); sin esto queda Cool Plate a 35
PLACA = "Textured PEI Plate"

TRABAJOS = {
    "caja": (["imprimir_frente.stl", "imprimir_tapa.stl", "imprimir_repisa.stl"], "caja_reloj_A1.gcode.3mf"),
    "prueba_calce": (["imprimir_prueba_calce.stl", "imprimir_repisa.stl"], "prueba_calce_A1.gcode.3mf"),
}
PLANOS = SALIDA / "perfiles"


def aplanar(perfil, extra=None):
    """La CLI no sigue 'inherits' (sin esto usa cama de 200x200 y densidad 0): se juntan los
    padres a mano, del mas lejano al propio, y se escribe un perfil sin herencia."""
    carpeta = perfil.parent
    por_nombre = {}
    for f in carpeta.glob("*.json"):
        try:
            por_nombre[json.loads(f.read_text(encoding="utf-8"))["name"]] = f
        except (KeyError, ValueError):
            pass
    cadena, actual = [], json.loads(perfil.read_text(encoding="utf-8"))
    while True:
        cadena.append(actual)
        padre = actual.get("inherits")
        if not padre:
            break
        actual = json.loads(por_nombre[padre].read_text(encoding="utf-8"))
    plano = {}
    for p in reversed(cadena):
        plano.update(p)
    plano.pop("inherits", None)
    plano.update(extra or {})
    PLANOS.mkdir(exist_ok=True)
    out = PLANOS / perfil.name
    out.write_text(json.dumps(plano, indent=1, ensure_ascii=False), encoding="utf-8")
    return out


def laminar(piezas, salida_3mf, maquina, proceso, filamento):
    if salida_3mf.exists():
        salida_3mf.unlink()
    args = [str(BAMBU), "--slice", "0", "--arrange", "1", "--orient", "0",
            "--load-settings", f"{maquina};{proceso}", "--load-filaments", str(filamento),
            "--outputdir", str(SALIDA), "--export-3mf", salida_3mf.name,
            *[str(SALIDA / p) for p in piezas]]
    r = subprocess.run(args, capture_output=True, text=True, errors="replace", cwd=SALIDA)
    if r.returncode != 0 or not salida_3mf.exists():
        print(r.stdout[-3000:], r.stderr[-3000:])
        raise SystemExit(f"Bambu Studio fallo (rc {r.returncode})")

    with zipfile.ZipFile(salida_3mf) as z:
        info = z.read("Metadata/slice_info.config").decode("utf-8")
        ajustes = json.loads(z.read("Metadata/project_settings.config"))
    # el timelapse "tradicional" no aplica a la A1 sin camara de camara lenta: no es una falla
    ignorar = {"not_support_traditional_timelapse"}
    placas = re.findall(r"<plate>(.*?)</plate>", info, re.S)
    print(f"Laminado A1: {salida_3mf}")
    print(f"  placa {ajustes['curr_bed_type']} ({ajustes['textured_plate_temp']} C), cama {ajustes['printable_area']}, densidad {ajustes['filament_density']}, "
          f"{ajustes['filament_settings_id']}, {ajustes['print_settings_id']}")
    total_s, total_g, problemas = 0, 0.0, []
    if ajustes["curr_bed_type"] != PLACA:
        problemas.append(f"placa {ajustes['curr_bed_type']} en vez de {PLACA}")
    for i, p in enumerate(placas, 1):
        seg = int(re.search(r'key="prediction" value="(\d+)"', p).group(1))
        g = float(re.search(r'used_g="([\d.]+)"', p).group(1))
        piezas = re.findall(r'<object [^>]*name="([^"]+)"', p)
        soporte = re.search(r'key="support_used" value="(\w+)"', p).group(1)
        fuera = re.search(r'key="outside" value="(\w+)"', p).group(1)
        avisos = [a for a in re.findall(r'<warning[^>]*msg="([^"]*)"', p) if a not in ignorar]
        total_s, total_g = total_s + seg, total_g + g
        print(f"  placa {i}: {piezas}  {seg // 3600} h {seg % 3600 // 60} min, {g:.0f} g, "
              f"soporte={soporte}, fuera_de_cama={fuera}, avisos={avisos or 'ninguno'}")
        if avisos or soporte == "true" or fuera == "true":
            problemas.append(f"placa {i}")
    print(f"  TOTAL: {len(placas)} placa(s), {total_s // 3600} h {total_s % 3600 // 60} min, {total_g:.0f} g")
    return {"archivo": salida_3mf.name, "placas": len(placas), "segundos": total_s,
            "gramos": round(total_g, 1), "problemas": problemas}


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    maquina, proceso, filamento = aplanar(MAQUINA), aplanar(PROCESO, {"curr_bed_type": PLACA}), aplanar(FILAMENTO)
    res = {nombre: laminar(piezas, SALIDA / archivo, maquina, proceso, filamento)
           for nombre, (piezas, archivo) in TRABAJOS.items()}
    (SALIDA / "laminado.json").write_text(json.dumps(res, indent=1), encoding="utf-8")
    return 1 if any(r["problemas"] for r in res.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
