"""Build the populated carrier board in case coordinates from the KiCad file
(used by assembly.py and the fit check).

KiCad (X, Y) maps to case (X - 150, 100 - Y); board bottom z 7.0, top z 8.2.
"""
import sys, os, math
import cadquery as cq
from shapely.geometry import LineString
from shapely.ops import polygonize, unary_union
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sexp import parse, find, one, prop
import parts

Z_BOT, T = 7.0, 1.2
Z_TOP = Z_BOT + T
HERE = os.path.dirname(os.path.abspath(__file__))
# KiCad's own 3D library (set KICAD10_3DMODEL_DIR, e.g. C:/Program Files/KiCad/10.0/share/kicad/3dmodels),
# or LIB3D: a flat folder of the same .step files
LIB3D = os.environ.get('LIB3D', '')
OWN3D = os.environ.get('OWN3D', os.path.join(HERE, '..', '..', 'PCB', '3d'))

# Footprints whose KiCad library model is missing or wrong for this build.
OVERRIDE = {
    'stardance:RP2040_Zero': 'RP2040_Zero',
    'stardance:DFPlayer_Mini': 'DFPlayer_Mini',
    'stardance:GY521_MPU6050': 'GY521_MPU6050',
    'Connector_USB:USB_C_Receptacle_GCT_USB4125-xx-x_6P_TopMnt_Horizontal': 'USB_C_GCT_USB4125_6P',
    'Button_Switch_THT:SW_Slide_SPDT_Angled_CK_OS102011MA1Q': 'SW_Slide_CK_OS102011MA1Q',
    'Package_SO:SOIC-8-1EP_3.9x4.9mm_P1.27mm_EP2.41x3.3mm': 'SOIC-8-1EP_3.9x4.9mm',
    'Button_Switch_THT:SW_PUSH_6mm': 'SW_Tactile_6x6x5mm_THT',
}
WIRE_PADS = {'J3', 'J4'}          # wires to the jack and the display, no part on the board

_cache = {}
def load(path):
    if path not in _cache:
        _cache[path] = cq.importers.importStep(path).val()
    return _cache[path]

def outline_solid(b):
    segs = []
    for g in find(b, 'gr_line'):
        if one(g, 'layer')[1] != 'Edge.Cuts': continue
        s, e = one(g, 'start'), one(g, 'end')
        segs.append(LineString([(float(s[1]) - 150, 100 - float(s[2])), (float(e[1]) - 150, 100 - float(e[2]))]))
    polys = sorted(polygonize(unary_union(segs)), key=lambda p: -p.area)
    outer = polys[0]
    wp = cq.Workplane().polyline(list(outer.exterior.coords)[:-1]).close().extrude(T).translate((0, 0, Z_BOT))
    return wp, outer

def build(pcb_path):
    b = parse(open(pcb_path, encoding='utf-8').read())
    board, outline = outline_solid(b)
    placed = []
    for fp in find(b, 'footprint'):
        ref = prop(fp, 'Reference'); name = fp[1]
        at = one(fp, 'at'); X, Y = float(at[1]), float(at[2]); rot = float(at[3]) if len(at) > 3 else 0.0
        x, y = X - 150, 100 - Y
        if name.startswith('MountingHole'):
            board = board.cut(cq.Workplane().circle(1.1).extrude(5).translate((x, y, Z_BOT - 1)))
            continue
        if ref in WIRE_PADS: continue
        if name in OVERRIDE:
            path = f'{OWN3D}/{OVERRIDE[name]}.step'
        else:
            m = one(fp, 'model')
            if not m: continue
            rel = m[1].replace('.wrl', '.step')
            cands = [os.path.join(LIB3D, os.path.basename(rel))] if LIB3D else []
            for var in ('KICAD10_3DMODEL_DIR', 'KICAD9_3DMODEL_DIR', 'KICAD8_3DMODEL_DIR'):
                if os.environ.get(var):
                    cands.append(os.environ[var] + '/' + rel.split('}/', 1)[-1])
            path = next((c for c in cands if os.path.exists(c)), cands[0] if cands else rel)
            if not os.path.exists(path):
                print('no model for', ref, path); continue
        shape = load(path).rotate((0, 0, 0), (0, 0, 1), rot).translate(cq.Vector(x, y, Z_TOP))
        placed.append((ref, name, shape))
    return board, outline, placed

if __name__ == '__main__':
    board, outline, placed = build(sys.argv[1])
    print('board area', round(outline.area), 'bounds', [round(v, 1) for v in outline.bounds])
    for ref, name, s in placed:
        bb = s.BoundingBox(); print(f'{ref:5} z {bb.zmin:6.2f}..{bb.zmax:6.2f}  x {bb.xmin:6.1f}..{bb.xmax:6.1f} y {bb.ymin:6.1f}..{bb.ymax:6.1f}')
