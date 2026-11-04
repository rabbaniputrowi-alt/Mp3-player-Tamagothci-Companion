"""Build case v5 from the v4 STEP.

    python make_v5.py ../../PCB/stardance_pcb.kicad_pcb stardance_case_v4.step <out folder>
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cadquery as cq
import board as B, case_v5
t=time.time()
bd, ol, placed = B.build(sys.argv[1])
sw3 = [s for r, _, s in placed if r == 'SW3'][0]
front, back, caps = case_v5.build(sys.argv[2], sw3_shape=sw3)
out = sys.argv[3]; os.makedirs(out, exist_ok=True)
for name, s in [('front_shell', front), ('back_shell', back)] + [(f'cap_{k}', v) for k, v in caps.items()]:
    print(name, 'valid', s.isValid(), 'solids', len(s.Solids()), 'vol', round(s.Volume(), 1))
    cq.exporters.export(cq.Workplane().add(s), f'{out}/v5_{name}.step')
print('time', round(time.time() - t))
