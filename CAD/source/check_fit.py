"""Interference check: every part of the populated board, display, battery and jack against
the v5 case parts and each other. Prints HITS [] when nothing overlaps.

    python check_fit.py ../../PCB/stardance_pcb.kicad_pcb <folder with v5_*.step>
"""
import sys, os, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cadquery as cq, board as B, parts

def vol_common(a, b):
    bb1, bb2 = a.BoundingBox(), b.BoundingBox()
    if (bb1.xmax < bb2.xmin or bb2.xmax < bb1.xmin or bb1.ymax < bb2.ymin or bb2.ymax < bb1.ymin
            or bb1.zmax < bb2.zmin or bb2.zmax < bb1.zmin):
        return 0.0
    return a.intersect(b).Volume()

d = sys.argv[2]
case = {n: cq.importers.importStep(f'{d}/v5_{n}.step').val() for n in
        ['front_shell', 'back_shell', 'cap_PREV', 'cap_NEXT', 'cap_MENU', 'cap_PLAY']}
bd, ol, placed = B.build(sys.argv[1])
elec = {r: s for r, _, s in placed}
elec['PCB'] = bd.val(); elec['DISPLAY'] = parts.gc9a01_display().val()
elec['BATTERY'] = parts.lipo_103040().val(); elec['JACK'] = parts.pj307_jack().val()
hits = []
for (en, e), (cn, c) in itertools.product(elec.items(), case.items()):
    v = vol_common(e, c)
    if abs(v) > 0.001: hits.append((round(v, 3), en, cn))
for (an, a), (bn, b) in itertools.combinations(elec.items(), 2):
    if 'PCB' in (an, bn): continue
    v = vol_common(a, b)
    if abs(v) > 0.001: hits.append((round(v, 3), an, bn))
for (an, a), (bn, b) in itertools.combinations(case.items(), 2):
    v = vol_common(a, b)
    if abs(v) > 0.001: hits.append((round(v, 3), an, bn))
print('HITS', sorted(hits, reverse=True))
