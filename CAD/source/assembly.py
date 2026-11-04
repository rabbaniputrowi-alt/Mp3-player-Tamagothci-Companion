"""Full Stardance assembly: case v5, populated carrier PCB v2, display, battery, jack, screws.

    python assembly.py <stardance_pcb.kicad_pcb> <v5 part folder> <out folder>
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cadquery as cq, board as B, parts

pcb, v5, out = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
L = lambda n: cq.importers.importStep(f'{v5}/v5_{n}.step').val()
C = cq.Color
case_col = C(0.86, 0.80, 0.93); cap_col = C(0.98, 0.84, 0.30)

asm = cq.Assembly(name='stardance_assembly')
case = cq.Assembly(name='case_v5')
case.add(L('front_shell'), name='front_shell', color=case_col)
case.add(L('back_shell'), name='back_shell', color=case_col)
for k in ('PREV', 'NEXT', 'MENU', 'PLAY'):
    case.add(L(f'cap_{k}'), name=f'cap_{k}', color=cap_col)
asm.add(case)

bd, outline, placed = B.build(pcb)
pcba = cq.Assembly(name='carrier_pcb_v2')
pcba.add(bd, name='PCB', color=C(0.10, 0.45, 0.22))
cols = {'U1': C(0.15, 0.15, 0.15), 'U2': C(0.12, 0.25, 0.55), 'U3': C(0.12, 0.25, 0.55)}
for ref, name, s in placed:
    pcba.add(s, name=ref, color=cols.get(ref, C(0.25, 0.25, 0.28)))
asm.add(pcba)
asm.add(parts.gc9a01_display(), name='display_GC9A01', color=C(0.05, 0.05, 0.08))
asm.add(parts.lipo_103040(), name='battery_LiPo_103040', color=C(0.75, 0.75, 0.78))
asm.add(parts.pj307_jack(), name='jack_PJ307', color=C(0.1, 0.1, 0.1))
H = [(32.93, 14.28), (-32.93, 14.28), (35.3, -33.0), (-35.3, -33.0)]
hw = cq.Assembly(name='hardware')
for i, (x, y) in enumerate(H, 1):
    screw = cq.Workplane().circle(1.0).extrude(12.0).translate((x, y, 3.0)).union(
        cq.Workplane().circle(1.9).extrude(2.0).translate((x, y, 15.0)))
    insert = cq.Workplane().circle(1.75).circle(1.0).extrude(5.0).translate((x, y, 2.0))
    hw.add(screw, name=f'screw_M2x12_{i}', color=C(0.6, 0.6, 0.62))
    hw.add(insert, name=f'insert_M2x5_{i}', color=C(0.8, 0.65, 0.3))
asm.add(hw)
asm.save(f'{out}/stardance_assembly.step')

# case files for printing
allcase = cq.Assembly(name='stardance_case_v5')
for n in ('front_shell', 'back_shell', 'cap_PREV', 'cap_NEXT', 'cap_MENU', 'cap_PLAY'):
    allcase.add(L(n), name=n)
allcase.save(f'{out}/stardance_case_v5.step')
caps = cq.Workplane().add([L(f'cap_{k}') for k in ('PREV', 'NEXT', 'MENU', 'PLAY')])
for n in ('front_shell', 'back_shell'):
    cq.exporters.export(cq.Workplane().add(L(n)), f'{out}/stardance_v5_{n}.step')
    cq.exporters.export(cq.Workplane().add(L(n)), f'{out}/stardance_v5_{n}.stl', tolerance=0.02, angularTolerance=0.1)
cq.exporters.export(caps, f'{out}/stardance_v5_button_caps.step')
cq.exporters.export(caps, f'{out}/stardance_v5_button_caps.stl', tolerance=0.01, angularTolerance=0.1)
print('done')
