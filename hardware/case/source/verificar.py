"""
Revisa la caja generada:
  1. avisos de SolidCraft en cada operacion
  2. choques entre piezas y componentes simulados (deben ser 0)
  3. distancias minimas pieza <-> componente
  4. mallas STL: cerradas, un solo cuerpo, orientacion de impresion, voladizos sin soporte
  5. pieza de prueba de calce (franja del frente + repisa) para imprimir antes que todo
Uso:  python verificar.py
"""
import json
import os
import sys
from pathlib import Path

import numpy as np
import trimesh

SALIDA = Path(os.environ.get("CASE_OUT", Path(__file__).resolve().parent / "out"))

# como va cada pieza en la cama de la A1: rotacion + que cara queda abajo
ORIENTACION = {
    "frente": "cara frontal abajo",
    "tapa": "cara de la pared abajo",
    "repisa": "ala abajo",
}

# pares que se tocan a proposito (cara contra cara, volumen 0)
CONTACTO_OK = {
    frozenset({"Frente", "Repisa"}),
    frozenset({"Frente", "Tapa"}),
}
# pares que se aprietan a proposito: volumen maximo aceptado (mm3). Los nervios de la funda
# aprietan 0.15 mm al sensor: unos 12 mm3 en total; mas que esto es un error de medidas.
PRESION_OK = {
    frozenset({"Frente", "x_Sensor"}): 20.0,
}
# franja del frente que se imprime como prueba: desde la cara frontal hasta esta profundidad
PRUEBA_Z = 28.0
ZF = 37.0


def presion_ok(a, b, vol):
    lim = PRESION_OK.get(frozenset({a, b}))
    return lim is not None and vol <= lim


def choque_mallas(a, b):
    """Volumen comun de dos piezas y su grosor menor (0 = solo se tocan)."""
    import manifold3d as m3

    def mf(nombre):
        t = trimesh.load(SALIDA / f"{nombre.lower()}.stl")
        return m3.Manifold(m3.Mesh(np.asarray(t.vertices, np.float32), np.asarray(t.faces, np.uint32)))

    comun = mf(a) ^ mf(b)
    vol = comun.volume()
    if vol < 1e-9:
        return 0.0, 0.0
    v = np.asarray(comun.to_mesh().vert_properties)[:, :3]
    return vol, float((v.max(0) - v.min(0)).min())


def orientar(nombre, m):
    """Devuelve la malla tal como va en la cama (z = 0 abajo)."""
    m = m.copy()
    if nombre == "frente":
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [0, 1, 0]))
    m.apply_translation([0, 0, -m.bounds[0][2]])
    return m


def revisar_malla(nombre):
    m = trimesh.load(SALIDA / f"{nombre}.stl")
    p = orientar(nombre, m)
    bodies = p.split(only_watertight=False)
    fallas = []
    if not p.is_watertight:
        fallas.append("malla abierta")
    if not p.is_winding_consistent:
        fallas.append("normales invertidas")
    if len(bodies) != 1:
        fallas.append(f"{len(bodies)} cuerpos separados")
    # voladizos: caras mirando hacia abajo (> 45 grados) que no estan en la cama
    n = p.face_normals
    zc = p.triangles_center[:, 2]
    abajo = (n[:, 2] < -0.72) & (zc > 0.05)      # 45 grados justos (chaflanes) se imprimen bien
    area_vol = p.area_faces[abajo].sum()
    # agrupar las caras colgantes en islas para contar puentes
    islas = []
    if abajo.any():
        idx = np.nonzero(abajo)[0]
        sub = p.submesh([idx], append=True)
        for isla in sub.split(only_watertight=False):
            ext = isla.extents
            islas.append((round(float(isla.bounds[0][2]), 2), round(float(max(ext[0], ext[1])), 1),
                          round(float(min(ext[0], ext[1])), 1), round(float(isla.area), 1)))
    bed = p.area_faces[(n[:, 2] < -0.99) & (zc < 0.05)].sum()
    return {
        "pieza": nombre,
        "orientacion": ORIENTACION[nombre],
        "medidas_cama_mm": [round(float(v), 1) for v in p.extents],
        "volumen_cm3": round(float(p.volume) / 1000, 1),
        "gramos_PLA_aprox": round(float(p.volume) / 1000 * 1.24, 0),
        "area_en_cama_mm2": round(float(bed)),
        "cerrada": bool(p.is_watertight),
        "fallas": fallas,
        "voladizos_mm2": round(float(area_vol), 1),
        "voladizos": sorted(islas, key=lambda t: -t[3])[:12],
    }, p


def prueba_calce():
    """Franja frontal del frente (ventana, torres, USB, funda con nervios) para probar el calce
    de la pantalla y del sensor con 1/3 del plastico, antes de imprimir la caja entera."""
    import manifold3d as m3
    t = trimesh.load(SALIDA / "frente.stl")
    lo, hi = t.bounds
    franja = m3.Manifold(m3.Mesh(np.asarray(t.vertices, np.float32), np.asarray(t.faces, np.uint32)))
    caja = m3.Manifold.cube([hi[0] - lo[0] + 2, hi[1] - lo[1] + 2, ZF - PRUEBA_Z + 1]).translate(
        [lo[0] - 1, lo[1] - 1, PRUEBA_Z])
    m = (franja ^ caja).to_mesh()
    p = trimesh.Trimesh(np.asarray(m.vert_properties)[:, :3], np.asarray(m.tri_verts), process=True)
    p = orientar("frente", p)
    p.export(SALIDA / "imprimir_prueba_calce.stl")
    return {"volumen_cm3": round(float(p.volume) / 1000, 1),
            "gramos_PLA_aprox": round(float(p.volume) / 1000 * 1.24, 0),
            "cerrada": bool(p.is_watertight), "cuerpos": len(p.split(only_watertight=False)),
            "medidas_cama_mm": [round(float(v), 1) for v in p.extents]}


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    res = json.loads((SALIDA / "revision.json").read_text(encoding="utf-8"))
    script = json.loads((SALIDA / "caja_reloj.solidcraft-script.json").read_text(encoding="utf-8"))
    problemas = []

    print("== 1. Avisos de SolidCraft")
    n_warn = 0
    for cmd, r in zip(script, res):
        if isinstance(r, dict) and r.get("warning"):
            n_warn += 1
            print(f"  {cmd['command']} {r.get('name')}: {r['warning']}")
    print(f"  {n_warn} avisos")
    if n_warn:
        problemas.append(f"{n_warn} avisos de SolidCraft")

    inter = next(r for c, r in zip(script, res) if c["command"] == "inspect.interference")
    clear = [r for c, r in zip(script, res) if c["command"] == "inspect.clearance"]

    print("\n== 2. Choques (volumen compartido)")
    malos = []
    for it in inter["interferences"]:
        par = frozenset({it["a"], it["b"]})
        sim_sim = it["a"].startswith("x_") and it["b"].startswith("x_")
        vol = it.get("volume_mm3", it.get("volume"))
        apriete = presion_ok(it["a"], it["b"], vol)
        tag = "ok (simulados entre si)" if sim_sim else "ok (apriete a proposito)" if apriete else "CHOQUE"
        print(f"  {it['a']} <-> {it['b']}: {vol:.1f} mm3  {tag}")
        if not sim_sim and not apriete and par not in CONTACTO_OK:
            malos.append(it)
    for u in inter.get("unchecked", []):
        # SolidCraft no pudo intersectar este par: se revisa con las mallas (manifold3d)
        vol, grosor = choque_mallas(u["a"], u["b"])
        ok = grosor < 0.01
        print(f"  {u['a']} <-> {u['b']}: SolidCraft no lo reviso; con mallas: {vol:.3f} mm3, "
              f"grosor {grosor:.4f} mm  {'ok (solo contacto de caras)' if ok else 'CHOQUE'}")
        if not ok:
            problemas.append(f"choque {u['a']}-{u['b']}")
    print(f"  revisados {inter['checked']} cuerpos; choques reales: {len(malos)}")
    if malos:
        problemas.append(f"{len(malos)} choques")

    print("\n== 3. Distancias pieza <-> componente (< 1 mm)")
    for o in clear[0].get("overlaps", []):
        if presion_ok(o["a"], o["b"], o["volume_mm3"]):
            print(f"  {o['a']} <-> {o['b']}: aprieta {o['volume_mm3']:.1f} mm3 (nervios, a proposito)")
            continue
        print(f"  CHOQUE {o['a']} <-> {o['b']}: {o['volume_mm3']:.3f} mm3")
        problemas.append(f"choque {o['a']}-{o['b']}")
    for nr in sorted(clear[0].get("near", []), key=lambda x: x["distance_mm"]):
        print(f"  {nr['a']} <-> {nr['b']}: {nr['distance_mm']:.2f} mm")
    print("  entre piezas (< 0.25 mm, contacto esperado frente-tapa y frente-repisa):")
    for o in clear[1].get("overlaps", []):
        print(f"  CHOQUE {o['a']} <-> {o['b']}: {o['volume_mm3']:.3f} mm3")
        problemas.append(f"choque {o['a']}-{o['b']}")
    for nr in clear[1].get("near", []):
        print(f"  {nr['a']} <-> {nr['b']}: {nr['distance_mm']:.2f} mm")

    print("\n== 4. Mallas para imprimir (orientadas para la cama)")
    reporte = []
    for nombre in ("frente", "tapa", "repisa"):
        info, malla = revisar_malla(nombre)
        malla.export(SALIDA / f"imprimir_{nombre}.stl")
        reporte.append(info)
        print(f"  {nombre}: {info['medidas_cama_mm']} mm, {info['volumen_cm3']} cm3 (~{info['gramos_PLA_aprox']:.0f} g PLA), "
              f"cerrada={info['cerrada']}, fallas={info['fallas'] or 'ninguna'}")
        print(f"    voladizos > 45 grados: {info['voladizos_mm2']} mm2 en {len(info['voladizos'])} zonas "
              f"(altura, largo, ancho, area): {info['voladizos'][:6]}")
        if info["fallas"]:
            problemas.append(f"{nombre}: {info['fallas']}")
        if max(info["medidas_cama_mm"][:2]) > 250:
            problemas.append(f"{nombre} no cabe en la A1")

    print("\n== 5. Prueba de calce")
    prueba = prueba_calce()
    print(f"  franja del frente desde la cara hasta {ZF - PRUEBA_Z:.0f} mm de fondo (se imprime con la repisa): "
          f"{prueba['volumen_cm3']} cm3 (~{prueba['gramos_PLA_aprox']:.0f} g), cerrada={prueba['cerrada']}, "
          f"cuerpos={prueba['cuerpos']}")
    if not prueba["cerrada"] or prueba["cuerpos"] != 1:
        problemas.append("prueba de calce: malla con fallas")

    (SALIDA / "verificacion.json").write_text(json.dumps({"mallas": reporte, "prueba_calce": prueba,
                                                          "problemas": problemas},
                                                         indent=1, ensure_ascii=False), encoding="utf-8")
    print("\n== RESULTADO:", "SIN PROBLEMAS" if not problemas else problemas)
    return 1 if problemas else 0


if __name__ == "__main__":
    sys.exit(main())
