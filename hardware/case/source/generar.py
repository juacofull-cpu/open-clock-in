"""
Caja para reloj control con huella, modelada en SolidCraft (B-rep: piezas cerradas).

Hardware (medidas oficiales: plano lcdwiki ES3C35P y ficha del AS608):
  - Pantalla ESP32-S3 3.5" QDtech ES3C35P (plano LCM OUTLINE V1.0, 2025-07-24)
  - Lector de huella AS608 45.1 x 20.3 x 18.6 (foto con medidas del vendedor)
  - Parlante ovalado que viene con la pantalla (medida estimada por foto: 40 x 28)

Ejes del modelo (caja colgada en la pared, mirando de frente):
  X a la derecha, Y hacia arriba, Z hacia el usuario. Z = 0 es la pared
  (cara exterior de la tapa); la cara frontal queda en Z = ZF.

Uso:  python generar.py            -> script SolidCraft, .solidcraft, STEP/STL, revision.json
"""
import json
import os
import subprocess
import sys
from pathlib import Path

SOLIDCRAFT = Path(os.environ.get("SOLIDCRAFT_CLI", "solidcraft-cli"))  # CLI de SolidCraft (o pon la ruta en SOLIDCRAFT_CLI)
SALIDA = Path(os.environ.get("CASE_OUT", Path(__file__).resolve().parent / "out"))

# ======================= PARAMETROS =======================
# --- Caja ---
WALL = 2.4            # paredes laterales, arriba y abajo
FRONT_T = 2.0         # pared frontal
LID_T = 3.0           # tapa
INNER_D = 32.0        # profundidad interior (deja ~10 mm detras del sensor para su cable)
CORNER_R = 6.0        # radio de esquinas exteriores
EDGE_CH = 0.6         # chaflan en las caras que van sobre la cama (evita pata de elefante)

# --- Pantalla ES3C35P (en horizontal: MIC a la izquierda, USB-C a la derecha) ---
PCB_W, PCB_H, PCB_T, PCB_R = 101.5, 54.5, 1.6, 3.5
HOLE_DX, HOLE_DY, PCB_HOLE_D = 94.5, 47.9, 3.2
GLASS_W, GLASS_H = 83.0, 54.5          # vidrio tactil, centrado en la placa
STACK = 4.2                            # frente del vidrio -> cara frontal de la placa
SMD = 4.7                              # componentes detras de la placa
VA_W, VA_H = 74.44, 49.96              # zona visible
VA_DX = -2.63                          # la zona visible esta corrida hacia el MIC
MIC_DX, MIC_DY = -46.81, 17.43         # agujero del microfono (desde el centro de la placa)
MIC_HOLE = False                       # el programa no usa el microfono: frente cerrado ahi
USB_W, USB_T, USB_L = 8.94, 3.26, 7.35 # receptaculo USB-C, en el reverso, borde derecho
GAP_GLASS = 0.2                        # aire entre vidrio y frente (no presiona el tactil)
GAP_USB = 1.0                          # placa -> pared del USB (el enchufe entra completo)
GAP_SIDE = 3.0                         # placa -> pared izquierda
CABLE_GAP = 10.0                       # bajo la placa: conectores JST con su cable doblado
TOP_SPACE = 14.0                       # sobre la placa: parlante (hasta 12 mm de grosor)
POST_D, PILOT_D = 5.5, 2.5             # postes de la placa, guia para tornillo M3

# --- Lector de huella AS608 ---
FP_L, FP_W, FP_H = 45.1, 20.3, 18.6
FP_CLR_SIDE, FP_CLR_TOP = 0.3, 0.4
FP_PROTRUDE = 23.0      # sale del frente: toda la rampa con la ventana + 8 mm
FP_LIP = 3.0            # borde vertical bajo la rampa (adelante)
FP_RAMP = 15.0          # largo horizontal de la rampa (estimado de fotos)
SLEEVE_T = 1.6          # funda interior que guia el sensor
FP_PRESS = 0.15         # nervios aplastables de la funda: aprietan 0.15 mm (sensor firme aunque
                        # mida +-0.15 distinto); la punta delantera entra en rampa
CHIN_WALL = 2.4
CHIN_SIDE_H = 8.0       # costados bajos de la repisa: no estorban el dedo
CHIN_LIP_H = 2.5        # tope delantero (no tapa la ventana)
CHIN_FLANGE_T = 3.0
CHIN_FLANGE_W = 8.0     # ala a cada lado, para los tornillos

# --- Parlante: va en un canal (techo + guias + repisas abajo), entra deslizando por atras ---
SPK_W, SPK_D, SPK_T = 40.0, 28.0, 12.0
SPK_POCKET_W = 43.0
SPK_CH_H = 13.0         # alto del canal: parlantes de hasta 12.5 mm (mas delgado: cinta de espuma)
SPK_LEDGE_W = 5.5       # repisas: sostienen parlantes desde 33 mm de ancho
SPK_LEDGE_T = 1.3
SPK_LEDGE_Z = 21.0      # las repisas terminan detras de la placa y del conector del parlante

# --- Tornillos ---
BOSS_D = 7.0
SCREW_CLEAR_D, SCREW_HEAD_D, SCREW_HEAD_DEPTH = 3.4, 6.6, 1.8
CHIN_BOSS_L = 8.0

# --- Colgado en pared ---
KEYHOLE_SPACING = 60.0
KEYHOLE_BIG_D, KEYHOLE_SLOT_W, KEYHOLE_TRAVEL = 8.5, 4.2, 8.0

# --- Holguras de la tapa ---
LIP_T, LIP_H, LIP_CLR = 1.2, 2.0, 0.3
# ==========================================================

SLEEVE_IN_W = FP_W + 2 * FP_CLR_SIDE
SLEEVE_IN_H = FP_H + FP_CLR_TOP
INNER_W = GAP_SIDE + PCB_W + GAP_USB
INNER_H = SLEEVE_IN_H + SLEEVE_T + CABLE_GAP + PCB_H + TOP_SPACE
OUT_W, OUT_H = INNER_W + 2 * WALL, INNER_H + 2 * WALL
XI, YI = INNER_W / 2, INNER_H / 2
Y0 = -YI                                   # piso interior: ahi apoya el sensor

Z_LID_IN = LID_T                           # cara interior de la tapa
ZFI = LID_T + INNER_D                      # cara interior del frente
ZF = ZFI + FRONT_T                         # cara frontal

DISP_CX = -XI + GAP_SIDE + PCB_W / 2
DISP_CY = Y0 + SLEEVE_IN_H + SLEEVE_T + CABLE_GAP + PCB_H / 2
Z_GLASS = ZFI - GAP_GLASS                  # frente del vidrio
Z_PCB_F = Z_GLASS - STACK                  # cara frontal de la placa
Z_PCB_B = Z_PCB_F - PCB_T                  # reverso de la placa
USB_Z = Z_PCB_B - USB_T / 2                # centro del receptaculo
USB_HOLE_W, USB_HOLE_H, USB_HOLE_R = 14.0, 8.5, 2.0

FP_ZF = ZF + FP_PROTRUDE                   # frente del sensor
FP_ZB = FP_ZF - FP_L                       # reverso del sensor (conector)
SLEEVE_Z0 = FP_ZB + 4.0                    # la funda termina antes del conector

# las torres van 1 mm mas afuera que el centro del redondeo interior: asi cortan la esquina
# de frente (rozarla casi tangente hace fallar la union) y no llegan al redondeo exterior
CORNER_IN_R = CORNER_R - WALL
BOSS_X, BOSS_Y = XI - CORNER_IN_R + 1.0, YI - CORNER_IN_R + 1.0
CHIN_XO = SLEEVE_IN_W / 2 + CHIN_WALL
CHIN_SCREW_X = CHIN_XO + CHIN_FLANGE_W / 2
CHIN_SCREW_Y = Y0 + 9.0
SPK_Z = (Z_LID_IN + ZFI) / 2


# ------------------------------------------------------------------
# Constructor de scripts SolidCraft
# ------------------------------------------------------------------
class Script:
    def __init__(self):
        self.cmds = []
        self.n = 0

    def add(self, command, **params):
        self.cmds.append({"command": command, "params": params})

    def sketch(self, origin=(0, 0, 0), x_dir=(1, 0, 0), y_dir=(0, 1, 0)):
        self.n += 1
        name = f"sk{self.n}"
        self.add("sketch.create", plane={"origin": list(origin), "x_dir": list(x_dir), "y_dir": list(y_dir)}, name=name)
        return name

    def done(self):
        self.add("sketch.finish")

    def rrect(self, cx, cy, w, h, r):
        self.add("sketch.rectangle.rounded", p0=[cx - w / 2, cy - h / 2], p1=[cx + w / 2, cy + h / 2], radius=r)

    def rect(self, cx, cy, w, h):
        self.add("sketch.rectangle.center", center=[cx, cy], corner=[cx + w / 2, cy + h / 2])

    def circle(self, cx, cy, d):
        self.add("sketch.circle.center", center=[cx, cy], diameter=d)

    def slot(self, x0, y0, x1, y1, w):
        self.add("sketch.slot.center_to_center", p0=[x0, y0], p1=[x1, y1], width=w)

    def poly(self, pts):
        self.add("sketch.line", points=[list(p) for p in pts], closed=True)

    def extrude(self, sketch, distance, op="new", target=None, name=None, direction=None, taper=None):
        p = {"sketch": sketch, "distance": distance, "operation": op}
        if target:
            p["targets"] = [target]
        if name:
            p["body_name"] = name
        if direction:
            p["direction"] = direction
        if taper is not None:
            p["taper"] = taper
        self.add("solid.extrude", **p)

    def box(self, x0, x1, y0, y1, z0, z1, op="new", target=None, name=None):
        p = {"length": x1 - x0, "width": y1 - y0, "height": z1 - z0,
             "center": [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2], "operation": op}
        if target:
            p["targets"] = [target]
        if name:
            p["body_name"] = name
        self.add("solid.box", **p)

    def cyl(self, x, y, z0, z1, d, op="new", target=None, name=None):
        p = {"diameter": d, "height": z1 - z0, "base": [x, y, z0], "axis": "Z", "operation": op}
        if target:
            p["targets"] = [target]
        if name:
            p["body_name"] = name
        self.add("solid.cylinder", **p)


def keyhole_points():
    ky = YI - 26.0
    return [(kx, ky) for kx in (-KEYHOLE_SPACING / 2, KEYHOLE_SPACING / 2)]


# ------------------------------------------------------------------
# PIEZA 1: FRENTE (se imprime con la cara frontal sobre la cama)
# ------------------------------------------------------------------
def build_front(s):
    B = "Frente"
    # cuerpo exterior con chaflan en la cara frontal
    k = s.sketch((0, 0, Z_LID_IN)); s.rrect(0, 0, OUT_W, OUT_H, CORNER_R); s.done()
    s.extrude(k, ZF - EDGE_CH - Z_LID_IN, name=B)
    k = s.sketch((0, 0, ZF - EDGE_CH)); s.rrect(0, 0, OUT_W, OUT_H, CORNER_R); s.done()
    s.extrude(k, EDGE_CH, op="join", target=B, taper=-45)
    # vaciado interior
    k = s.sketch((0, 0, Z_LID_IN - 1)); s.rrect(0, 0, INNER_W, INNER_H, CORNER_R - WALL); s.done()
    s.extrude(k, ZFI - Z_LID_IN + 1, op="cut", target=B)

    # ventana de la pantalla (zona visible + 0.4 por lado) con chaflan de 1 mm afuera
    wx, ww, wh = DISP_CX + VA_DX, VA_W + 0.8, VA_H + 0.8
    k = s.sketch((0, 0, ZFI - 1)); s.rrect(wx, DISP_CY, ww, wh, 1.0); s.done()
    s.extrude(k, FRONT_T + 2, op="cut", target=B)
    # el chaflan sigue 0.3 mm mas alla de la ventana: asi su tapa cae en el vacio y no
    # coincide con las paredes de la ventana (las caras coincidentes dejan astillas en la malla)
    k = s.sketch((0, 0, ZF)); s.rrect(wx, DISP_CY, ww + 2.0, wh + 2.0, 2.0); s.done()
    s.extrude(k, 1.3, op="cut", target=B, direction="negative", taper=-45)

    # microfono
    if MIC_HOLE:
        s.cyl(DISP_CX + MIC_DX, DISP_CY + MIC_DY, ZFI - 1, ZF + 1, 2.0, op="cut", target=B)

    # funda del sensor: un bloque macizo y despues un solo corte que hace el canal y la
    # abertura del frente (llega hasta el piso)
    s.box(-SLEEVE_IN_W / 2 - SLEEVE_T, SLEEVE_IN_W / 2 + SLEEVE_T, Y0 - 0.5, Y0 + SLEEVE_IN_H + SLEEVE_T,
          SLEEVE_Z0, ZFI + 0.5, op="join", target=B)
    s.box(-SLEEVE_IN_W / 2, SLEEVE_IN_W / 2, Y0 - 0.01, Y0 + SLEEVE_IN_H, SLEEVE_Z0 - 1, ZF + 1, op="cut", target=B)
    sensor_ribs(s, B)

    # torres de la tapa (4 esquinas) y postes de la pantalla
    for sx in (-1, 1):
        for sy in (-1, 1):
            s.cyl(sx * BOSS_X, sy * BOSS_Y, Z_LID_IN, ZFI + 0.5, BOSS_D, op="join", target=B)
            s.cyl(DISP_CX + sx * HOLE_DX / 2, DISP_CY + sy * HOLE_DY / 2, Z_PCB_F, ZFI + 0.5, POST_D, op="join", target=B)
    # torres de la repisa del sensor
    for sx in (-1, 1):
        s.cyl(sx * CHIN_SCREW_X, CHIN_SCREW_Y, ZFI - CHIN_BOSS_L, ZFI + 0.5, BOSS_D, op="join", target=B)

    # agujeros guia
    for sx in (-1, 1):
        for sy in (-1, 1):
            s.cyl(sx * BOSS_X, sy * BOSS_Y, Z_LID_IN - 1, ZFI - 1.0, PILOT_D, op="cut", target=B)
            # guia justa para un M3x6 (punta en ZFI): deja 2 mm de frente sin perforar
            s.cyl(DISP_CX + sx * HOLE_DX / 2, DISP_CY + sy * HOLE_DY / 2, Z_PCB_F - 1, ZFI - 0.2, PILOT_D, op="cut", target=B)
        s.cyl(sx * CHIN_SCREW_X, CHIN_SCREW_Y, ZFI - CHIN_BOSS_L - 1, ZF + 1, PILOT_D, op="cut", target=B)

    # hueco USB-C en la pared derecha
    k = s.sketch((XI + WALL / 2, 0, 0), x_dir=(0, 1, 0), y_dir=(0, 0, 1))
    s.rrect(DISP_CY, USB_Z, USB_HOLE_W, USB_HOLE_H, USB_HOLE_R); s.done()
    s.extrude(k, WALL + 2, op="cut", target=B, direction="symmetric")

    # rejilla del parlante en el techo (ranuras a lo largo de Z: se imprimen sin soporte)
    k = s.sketch((0, YI + WALL / 2, 0), x_dir=(1, 0, 0), y_dir=(0, 0, 1))
    for i in range(7):
        x = -12 + 4 * i
        s.slot(x, SPK_Z - 8, x, SPK_Z + 8, 2.0)
    s.done()
    s.extrude(k, WALL + 2, op="cut", target=B, direction="symmetric")
    speaker_channel(s, B)

    # ventilacion abajo (entra aire fresco), a ambos lados del sensor
    k = s.sketch((0, -YI - WALL / 2, 0), x_dir=(1, 0, 0), y_dir=(0, 0, 1))
    for sx in (-1, 1):
        for i in range(5):
            x = sx * (27 + 4 * i)
            s.slot(x, SPK_Z - 6, x, SPK_Z + 6, 2.0)
    s.done()
    s.extrude(k, WALL + 2, op="cut", target=B, direction="symmetric")


def sensor_ribs(s, B):
    """Nervios aplastables dentro de la funda: 2 por costado y 2 en el techo. Van a lo largo de Z
    (se imprimen de pie) y los 2 mm de cada punta se angostan: el sensor entra sin trabarse por
    cualquier lado. La punta queda 0.1 mm afuera de la pared (nunca al ras: cara coincidente)."""
    z0, z1 = SLEEVE_Z0 + 1.0, ZFI - 0.3
    lead, taper = 2.0, -9.93                        # se achica 0.35 mm por lado en 2 mm
    emb = 0.8                                       # parte metida en la pared de la funda
    ribs = []                                       # (centro x, centro y, ancho x, alto y)
    for sx in (-1, 1):
        sal = FP_CLR_SIDE + FP_PRESS                # lo que entra al canal
        xw = sx * SLEEVE_IN_W / 2
        for y in (Y0 + 5.0, Y0 + 13.5):
            ribs.append((xw + sx * (emb - sal) / 2, y, emb + sal, 1.2))
    sal = FP_CLR_TOP + FP_PRESS
    yw = Y0 + SLEEVE_IN_H
    for x in (-6.0, 6.0):
        ribs.append((x, yw + (emb - sal) / 2, 1.2, emb + sal))
    for cx, cy, w, h in ribs:
        k = s.sketch((0, 0, z0 + lead)); s.rect(cx, cy, w, h); s.done()
        s.extrude(k, z1 - z0 - 2 * lead, op="join", target=B)
        k = s.sketch((0, 0, z0 + lead)); s.rect(cx, cy, w, h); s.done()
        s.extrude(k, lead, op="join", target=B, direction="negative", taper=taper)
        k = s.sketch((0, 0, z1 - lead)); s.rect(cx, cy, w, h); s.done()
        s.extrude(k, lead, op="join", target=B, taper=taper)


def speaker_channel(s, B):
    """Canal del parlante: guias a los costados que bajan hasta unas repisas bajo el parlante.
    Todo lo que cuelga va en 45 grados (se imprime sin soporte con el frente sobre la cama)."""
    zb = SPK_Z - 12
    y_ledge = YI - SPK_CH_H                     # cara de arriba de las repisas
    yl = y_ledge - SPK_LEDGE_T - 0.2            # fondo de las guias (0.2 bajo las repisas)
    gx = SPK_POCKET_W / 2
    for sx in (-1, 1):
        # guia: perfil en el plano YZ (u = z, v = y), extruido 1.2 mm hacia -X
        k = s.sketch((sx * gx + (1.2 if sx > 0 else 0.0), 0, 0), x_dir=(0, 0, 1), y_dir=(0, 1, 0))
        zs = SPK_LEDGE_Z + (YI - 3.0 - yl)
        s.poly([(zb, YI + 0.5), (ZFI + 0.5, YI + 0.5), (ZFI + 0.5, YI - 3.0), (zs, YI - 3.0),
                (SPK_LEDGE_Z, yl), (zb, yl)])
        s.done()
        s.extrude(k, 1.2, op="join", target=B)
        # repisa: perfil en el plano XZ (u = x, v = z), extruido hacia -Y desde su cara de arriba
        k = s.sketch((0, y_ledge, 0), x_dir=(1, 0, 0), y_dir=(0, 0, 1))
        xo, xi = sx * (gx + 0.6), sx * (gx - SPK_LEDGE_W)
        dz = SPK_LEDGE_W + 0.6
        pts = [(xo, zb + 0.5), (xi, zb + 0.5), (xi, SPK_LEDGE_Z - dz), (xo, SPK_LEDGE_Z)]
        s.poly(pts if sx > 0 else pts[::-1])
        s.done()
        s.extrude(k, SPK_LEDGE_T, op="join", target=B)


# ------------------------------------------------------------------
# PIEZA 2: TAPA (se imprime con la cara de la pared sobre la cama)
# ------------------------------------------------------------------
def build_lid(s):
    B = "Tapa"
    k = s.sketch((0, 0, 0)); s.rrect(0, 0, OUT_W - 2 * EDGE_CH, OUT_H - 2 * EDGE_CH, CORNER_R - EDGE_CH); s.done()
    s.extrude(k, EDGE_CH, name=B, taper=45)
    k = s.sketch((0, 0, EDGE_CH)); s.rrect(0, 0, OUT_W, OUT_H, CORNER_R); s.done()
    s.extrude(k, LID_T - EDGE_CH, op="join", target=B)

    # tornillos: pasante + rebaje para la cabeza (lado pared)
    for sx in (-1, 1):
        for sy in (-1, 1):
            s.cyl(sx * BOSS_X, sy * BOSS_Y, -1, LID_T + 1, SCREW_CLEAR_D, op="cut", target=B)
            s.cyl(sx * BOSS_X, sy * BOSS_Y, -1, SCREW_HEAD_DEPTH, SCREW_HEAD_D, op="cut", target=B)
            # puente escalonado sobre el rebaje (2 capas de 0.2): cada capa se apoya en la
            # anterior, asi el techo del rebaje se imprime sin soporte y sin colgar
            # (0.05 mas que el agujero y la ranura pasada del rebaje: nada queda tangente)
            cx, cy, r = sx * BOSS_X, sy * BOSS_Y, SCREW_CLEAR_D / 2 + 0.05
            h = SCREW_HEAD_D / 2 + 0.3
            s.box(cx - h, cx + h, cy - r - 0.1, cy + r + 0.1,
                  SCREW_HEAD_DEPTH - 0.01, SCREW_HEAD_DEPTH + 0.2, op="cut", target=B)
            s.box(cx - r, cx + r, cy - r, cy + r, SCREW_HEAD_DEPTH + 0.1, SCREW_HEAD_DEPTH + 0.4, op="cut", target=B)
    # ojales para colgar (circulo grande abajo + ranura hacia arriba)
    for kx, ky in keyhole_points():
        s.cyl(kx, ky, -1, LID_T + 1, KEYHOLE_BIG_D, op="cut", target=B)
        k = s.sketch((0, 0, -1)); s.slot(kx, ky, kx, ky + KEYHOLE_TRAVEL, KEYHOLE_SLOT_W); s.done()
        s.extrude(k, LID_T + 2, op="cut", target=B)

    # pestañas de centrado (izquierda, derecha y abajo con paso al centro)
    xi, yi = XI - LIP_CLR, YI - LIP_CLR
    skip = BOSS_D + 1.0
    gap = SLEEVE_IN_W / 2 + 4
    z0, z1 = LID_T - 0.01, LID_T + LIP_H
    s.box(-xi, -xi + LIP_T, -yi + skip, yi - skip, z0, z1, op="join", target=B)
    s.box(xi - LIP_T, xi, -yi + skip, yi - skip, z0, z1, op="join", target=B)
    s.box(-xi + skip, -gap, -yi, -yi + LIP_T, z0, z1, op="join", target=B)
    s.box(gap, xi - skip, -yi, -yi + LIP_T, z0, z1, op="join", target=B)

    # topes traseros del sensor (en las esquinas de arriba de su cara trasera, lejos del conector)
    for sx in (-1, 1):
        x = sx * (FP_W / 2 - 1.8)
        s.box(x - 1.5, x + 1.5, Y0 + FP_H - 3.5, Y0 + FP_H - 0.5, LID_T - 0.01, FP_ZB - 0.4, op="join", target=B)


# ------------------------------------------------------------------
# PIEZA 3: REPISA DEL SENSOR (atornillada al frente; se imprime con el ala sobre la cama)
# ------------------------------------------------------------------
def build_chin(s):
    B = "Repisa"
    zf_in = FP_ZF + FP_CLR_SIDE
    z_end = zf_in + CHIN_WALL
    xin = SLEEVE_IN_W / 2
    yb = Y0 - WALL
    fx = CHIN_XO + CHIN_FLANGE_W
    ytop = Y0 + SLEEVE_IN_H + SLEEVE_T + 1.4
    # ala
    s.box(-fx, fx, yb, ytop, ZF, ZF + CHIN_FLANGE_T, name=B)
    s.box(-xin, xin, Y0, Y0 + SLEEVE_IN_H, ZF - 1, ZF + CHIN_FLANGE_T + 1, op="cut", target=B)
    for sx in (-1, 1):
        s.cyl(sx * CHIN_SCREW_X, CHIN_SCREW_Y, ZF - 1, ZF + CHIN_FLANGE_T + 1, SCREW_CLEAR_D, op="cut", target=B)
    # piso, costados y tope delantero
    s.box(-CHIN_XO, CHIN_XO, yb, Y0, ZF + CHIN_FLANGE_T - 0.01, z_end, op="join", target=B)
    for sx in (-1, 1):
        x0, x1 = sx * xin, sx * CHIN_XO
        s.box(min(x0, x1), max(x0, x1), Y0 - 0.01, Y0 + CHIN_SIDE_H, ZF + CHIN_FLANGE_T - 0.01, z_end, op="join", target=B)
    s.box(-CHIN_XO, CHIN_XO, Y0 - 0.01, Y0 + CHIN_LIP_H, zf_in, z_end, op="join", target=B)


# ------------------------------------------------------------------
# Componentes simulados, para revisar choques
# ------------------------------------------------------------------
def build_dummies(s):
    k = s.sketch((0, 0, Z_PCB_B)); s.rrect(DISP_CX, DISP_CY, PCB_W, PCB_H, PCB_R)
    for sx in (-1, 1):
        for sy in (-1, 1):
            s.circle(DISP_CX + sx * HOLE_DX / 2, DISP_CY + sy * HOLE_DY / 2, PCB_HOLE_D)
    s.done()
    s.extrude(k, PCB_T, name="x_Placa")
    s.box(DISP_CX - GLASS_W / 2, DISP_CX + GLASS_W / 2, DISP_CY - GLASS_H / 2, DISP_CY + GLASS_H / 2,
          Z_PCB_F, Z_GLASS, name="x_Vidrio")
    # componentes del reverso (deja libres los anillos de los agujeros de montaje)
    k = s.sketch((0, 0, Z_PCB_B - SMD)); s.rrect(DISP_CX, DISP_CY, PCB_W, PCB_H, PCB_R)
    for sx in (-1, 1):
        for sy in (-1, 1):
            s.circle(DISP_CX + sx * HOLE_DX / 2, DISP_CY + sy * HOLE_DY / 2, 5.6)
    s.done()
    s.extrude(k, SMD, name="x_Componentes")
    # enchufe USB-C metido a fondo: carcasa metalica + cuerpo de goma (12.35 x 6.5, el maximo de la norma)
    xe = DISP_CX + PCB_W / 2
    s.box(xe - 6.2, xe + 0.5, DISP_CY - 4.2, DISP_CY + 4.2, USB_Z - 1.3, USB_Z + 1.3, name="x_USB_metal")
    s.box(xe + 0.5, xe + 30, DISP_CY - 12.35 / 2, DISP_CY + 12.35 / 2, USB_Z - 3.25, USB_Z + 3.25, name="x_USB_goma")
    # conectores JST de abajo con el enchufe y el cable doblado
    for i, dx in enumerate((-32.99, -13.29, 9.14, 33.02)):
        x = DISP_CX + dx
        yb = DISP_CY - PCB_H / 2
        s.box(x - 4, x + 4, yb - 8, yb + 1, Z_PCB_B - SMD, Z_PCB_B, name=f"x_JST{i + 1}")
    # sensor AS608 (perfil con rampa)
    k = s.sketch((0, 0, 0), x_dir=(0, 0, 1), y_dir=(0, 1, 0))
    s.poly([(FP_ZB, Y0), (FP_ZF, Y0), (FP_ZF, Y0 + FP_LIP), (FP_ZF - FP_RAMP, Y0 + FP_H), (FP_ZB, Y0 + FP_H)])
    s.done()
    s.extrude(k, FP_W, name="x_Sensor", direction="symmetric")
    # conector del sensor con el cable saliendo hacia atras
    s.box(-6, 6, Y0 + 4, Y0 + 13, FP_ZB - 10, FP_ZB, name="x_Sensor_cable")
    # parlante en su canal, contra el techo
    s.box(-SPK_W / 2, SPK_W / 2, YI - SPK_T, YI, SPK_Z - SPK_D / 2, SPK_Z + SPK_D / 2, name="x_Parlante")
    # conector del parlante (MX1.25, borde de arriba de la placa, a 26.8 mm del lado USB) con su enchufe
    xs = DISP_CX + PCB_W / 2 - 26.8
    ys = DISP_CY + PCB_H / 2
    s.box(xs - 3.5, xs + 3.5, ys - 4.5, ys + 0.5, Z_PCB_B - 5.5, Z_PCB_B, name="x_JST_parlante")


PIEZAS = ["Frente", "Tapa", "Repisa"]
SIMULADOS = ["x_Placa", "x_Vidrio", "x_Componentes", "x_USB_metal", "x_USB_goma", "x_JST1", "x_JST2",
             "x_JST3", "x_JST4", "x_Sensor", "x_Sensor_cable", "x_Parlante", "x_JST_parlante"]


def build_script(con_simulados=True, revisar=True, exportar=True):
    s = Script()
    s.add("document.units", units="mm")
    build_front(s)
    build_lid(s)
    build_chin(s)
    if con_simulados:
        build_dummies(s)
    if revisar:
        s.add("inspect.interference")
        s.add("inspect.clearance", bodies=PIEZAS, against=SIMULADOS, clearance=1.0)
        s.add("inspect.clearance", bodies=PIEZAS, against=PIEZAS, clearance=0.25)
        s.add("inspect.measure", bodies=PIEZAS)
    if exportar:
        for b in PIEZAS:
            s.add("file.export", path=str(SALIDA / f"{b.lower()}.step").replace("\\", "/"), bodies=[b])
            s.add("file.export", path=str(SALIDA / f"{b.lower()}.stl").replace("\\", "/"), bodies=[b])
    return s


def main():
    SALIDA.mkdir(parents=True, exist_ok=True)
    s = build_script()
    script = SALIDA / "caja_reloj.solidcraft-script.json"
    script.write_text(json.dumps(s.cmds, indent=1), encoding="utf-8")
    res = SALIDA / "revision.json"
    r = subprocess.run([str(SOLIDCRAFT), "run", str(script), "--save", str(SALIDA / "caja_reloj.solidcraft"),
                        "--results", "--quiet", "--json-out", str(res)], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-4000:], r.stderr[-4000:])
        sys.exit(r.returncode)
    print(f"Medidas exteriores: {OUT_W:.1f} x {OUT_H:.1f} x {ZF:.1f} mm "
          f"(+{FP_PROTRUDE + FP_CLR_SIDE + CHIN_WALL:.1f} mm de repisa)")
    print(f"Script: {script}\nRevision: {res}")


if __name__ == "__main__":
    main()
