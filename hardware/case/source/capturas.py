"""
Imagenes de la caja para el informe (armada, transparente, explotada, cortes y cama de la A1).
Corre despues de generar.py.  Uso:  python capturas.py
"""
import json
import subprocess
import sys
from pathlib import Path

from generar import SOLIDCRAFT, SALIDA, DISP_CY, Y0, FP_W, FP_H, SLEEVE_IN_W, SLEEVE_IN_H, SLEEVE_T

CAPTURAS = SALIDA.parent / "capturas"
DISENO = SALIDA / "caja_reloj.solidcraft"

PIEZAS = ["Frente", "Tapa", "Repisa"]
SIMULADOS = ["x_Placa", "x_Vidrio", "x_Componentes", "x_USB_metal", "x_USB_goma",
             "x_JST1", "x_JST2", "x_JST3", "x_JST4", "x_Sensor", "x_Sensor_cable", "x_Parlante",
             "x_JST_parlante"]
TODOS = PIEZAS + SIMULADOS

COLOR = {
    "Frente": "#e8e6e1", "Tapa": "#b9b4aa", "Repisa": "#d6d1c7",
    "x_Placa": "#2f7d4a", "x_Vidrio": "#1b1f24", "x_Componentes": "#4b5a52",
    "x_USB_metal": "#a7adb3", "x_USB_goma": "#3a3a3a",
    "x_JST1": "#f2f2ea", "x_JST2": "#f2f2ea", "x_JST3": "#f2f2ea", "x_JST4": "#f2f2ea",
    "x_Sensor": "#2b2d33", "x_Sensor_cable": "#c0392b", "x_Parlante": "#7b4f2c",
    "x_JST_parlante": "#f2f2ea",
}


def cmd(command, **params):
    return {"command": command, "params": params}


def de_pie(cuerpos):
    """El modelo usa Y hacia arriba; SolidCraft muestra Z hacia arriba: se gira 90 grados en X."""
    return [cmd("solid.move", bodies=cuerpos, axis=[1, 0, 0], angle=90)]


def colores(cuerpos, opacidad=None):
    out = []
    for b in cuerpos:
        p = {"bodies": [b], "color": COLOR[b]}
        if opacidad and b in opacidad:
            p["opacity"] = opacidad[b]
        out.append(cmd("appearance.assign", **p))
    return out


def quitar(cuerpos):
    return [cmd("solid.remove", bodies=cuerpos)]


def foto(nombre, script, *extra, ancho=1400, alto=1000):
    CAPTURAS.mkdir(parents=True, exist_ok=True)
    js = CAPTURAS / f"_{nombre}.json"
    js.write_text(json.dumps(script, indent=1), encoding="utf-8")
    out = CAPTURAS / f"{nombre}.png"
    r = subprocess.run([str(SOLIDCRAFT), "snapshot", str(js), "--in", str(DISENO), "--out", str(out),
                        "--width", str(ancho), "--height", str(alto), "--no-axes", *extra],
                       capture_output=True, text=True)
    js.unlink()
    if r.returncode != 0 or not out.exists():
        print(r.stdout[-1500:], r.stderr[-1500:])
        raise SystemExit(f"fallo la captura {nombre}")
    print("  ", out)


def detalle_nervios(z=26.0, escala=22):
    """Corte ampliado de la funda del sensor (los nervios miden 0.45 mm: en la foto 3D no se ven).
    SVG dibujado desde la malla real del frente, visto de frente, con el sensor encima."""
    import trimesh
    corte = trimesh.load(SALIDA / "frente.stl").section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1])
    x0, x1 = -SLEEVE_IN_W / 2 - SLEEVE_T - 2.5, SLEEVE_IN_W / 2 + SLEEVE_T + 2.5
    y0, y1 = Y0 - 3.0, Y0 + SLEEVE_IN_H + SLEEVE_T + 2.5
    def px(x, y):
        return f"{(x - x0) * escala:.1f},{(y1 - y) * escala:.1f}"
    caminos = []
    for poli in corte.discrete:
        caminos.append("M" + " L".join(px(x, y) for x, y, *_ in poli) + " Z")
    w, h = (x1 - x0) * escala, (y1 - y0) * escala
    sensor = [px(-FP_W / 2, Y0), px(FP_W / 2, Y0), px(FP_W / 2, Y0 + FP_H), px(-FP_W / 2, Y0 + FP_H)]
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} {h:.0f}" width="{w:.0f}" height="{h:.0f}">
<defs><clipPath id="c"><rect width="{w:.0f}" height="{h:.0f}"/></clipPath></defs>
<g clip-path="url(#c)">
<path d="{' '.join(caminos)}" fill="#e8e6e1" stroke="#555" stroke-width="1.5" fill-rule="evenodd"/>
<polygon points="{' '.join(sensor)}" fill="#2b2d33" fill-opacity="0.55" stroke="#2b2d33" stroke-width="1.5"/>
</g></svg>'''
    out = CAPTURAS / "10_detalle_nervios.svg"
    out.write_text(svg, encoding="utf-8")
    print("  ", out)


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    base = de_pie(TODOS)

    # 1. armada, como se ve en la pared
    foto("1_armada", base + colores(TODOS), "--view", "iso", "--no-grid")
    foto("2_armada_de_frente", base + colores(TODOS), "--view", "front", "--no-grid")

    # 2. frente transparente: se ven la placa, el parlante y el sensor adentro
    foto("3_por_dentro", base + colores(TODOS, {"Frente": 0.22, "Repisa": 0.35}), "--view", "iso", "--no-grid")

    # 3. explotada: tapa hacia atras, repisa hacia abajo y adelante
    expl = base + [
        cmd("solid.move", bodies=["Tapa"], translate=[0, 70, 0]),
        cmd("solid.move", bodies=["Repisa"], translate=[0, -45, -45]),
        cmd("solid.move", bodies=["x_Sensor", "x_Sensor_cable"], translate=[0, -45, -20]),
    ]
    foto("4_explotada", expl + colores(TODOS), "--view", "iso", "--no-grid")

    # 4. cortes: por el centro (sensor y rampa) y a la altura del USB (vidrio, placa y USB)
    foto("5_corte_sensor", base + colores(TODOS), "--view", "right", "--no-grid", "--section", "YZ")
    # (el corte se aplica en el sistema original, por eso esta va sin girar: vista desde arriba)
    corte = json.dumps({"plane": "XZ", "at": round(DISP_CY, 2)})
    foto("6_corte_pantalla", colores(TODOS), "--view", "back", "--no-grid", "--section", corte)

    # corte de pie (plano XY, visto desde atras): canal del parlante con sus repisas
    foto("9_corte_parlante", colores(TODOS), "--view", "top", "--no-grid",
         "--section", json.dumps({"plane": "XY", "at": 15.0}))
    detalle_nervios()

    # 5. piezas solas como van en la cama (frente boca abajo, tapa con la cara de la pared abajo)
    cama = quitar(SIMULADOS) + [
        cmd("solid.move", bodies=["Frente"], axis=[0, 1, 0], angle=180),
        cmd("solid.move", bodies=["Frente"], translate=[-58, 0, 37]),
        cmd("solid.move", bodies=["Tapa"], translate=[58, 0, 0]),
        cmd("solid.move", bodies=["Repisa"], translate=[0, -28.5, -37]),
    ] + colores(PIEZAS)
    foto("7_cama_a1", cama, "--view", "iso")


if __name__ == "__main__":
    main()
