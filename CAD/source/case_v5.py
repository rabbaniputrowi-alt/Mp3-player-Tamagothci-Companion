"""Stardance case v5: case v4.7 adapted to carrier PCB v2.

Starts from the v4 STEP (the v4 CadQuery source is not in the repo) and edits it:
  1. fills the v4 side openings (PREV, NEXT, MENU holes, POWER slot, old TP4056 USB-C)
     by copying a clean cross-section of the same straight wall across each opening,
     so the split-line lip comes out unchanged
  2. cuts PREV / NEXT / MENU at the right-angle switch actuator height (z 11.7) and the
     POWER slot at the slide lever height (z 10.45), all in the front shell
  3. cuts the USB-C opening for J5 on the left leg's bottom edge
  4. adds four M2 heat-set insert bosses (back) and screw columns (front) at H1-H4
  5. notches the display collar where the NEXT switch body reaches it
  6. raises the three wall caps to the new holes, trims their flanges clear of the PCB,
     and lengthens the PLAY nub for a 6x6x5 mm switch
Case coordinates: mm, origin at the screen centre, x right, y up, z out of the front.
"""
import math, sys, os
import cadquery as cq

V = cq.Vector
SIDE_Z_OLD = 7.5
ACT_Z = 8.2 + 3.5           # board top + PTS645 actuator axis height
SLIDE_Z = 8.2 + 2.25        # board top + OS102011 lever centre

def frame_box(p, n, t0, t1, n0, n1, z0, z1):
    """Box in a wall frame: t along the wall, n outward normal, z up."""
    nx, ny = n
    ang = math.degrees(math.atan2(ny, nx)) - 90          # local +y -> n
    b = cq.Solid.makeBox(t1 - t0, n1 - n0, z1 - z0, V(t0, n0, z0))
    return b.rotate(V(0, 0, 0), V(0, 0, 1), ang).translate(V(p[0], p[1], 0))

def tangent(n):
    # same direction as frame_box's local +x, unit length
    l = math.hypot(n[0], n[1])
    return (n[1] / l, -n[0] / l)

def filler(shell, p, n, half_w, z0, z1, src_s, w=0.6):
    """The wall's cross-section at tangent offset src_s, extruded across [-half_w, half_w].

    The edges these features sit on are straight, so the wall (with the split-line lip)
    is the same profile all along them; extruding a clean section reproduces it exactly.
    """
    t = tangent(n)
    tv = V(t[0], t[1], 0)
    a0 = src_s - w / 2
    sl = shell.intersect(frame_box(p, n, a0, a0 + w, -4.0, 1.5, z0, z1))
    plane_pt = V(p[0], p[1], 0) + tv * a0
    out = None
    for f in sl.Faces():
        if f.geomType() != 'PLANE': continue
        nn = f.normalAt()
        if abs(abs(nn.dot(tv)) - 1) > 1e-5: continue
        if abs((f.Center() - plane_pt).dot(tv)) > 1e-3: continue
        f2 = f.translate(tv * (-half_w - a0))
        pr = cq.Solid.extrudeLinear(f2, tv * (2 * half_w))
        out = pr if out is None else out.fuse(pr)
    return out

def wall_hole(p, n, w, h, zc, r=None, depth=(-4.0, 2.0)):
    """Rounded-rectangle (or round, w == h) hole through a wall, w along it, h in z."""
    nx, ny = n
    ang = math.degrees(math.atan2(ny, nx)) - 90
    L = depth[1] - depth[0]
    if abs(w - h) < 1e-6:
        s = cq.Workplane('XZ').circle(w / 2).extrude(-L)
    else:
        s = cq.Workplane('XZ').slot2D(w, h).extrude(-L) if r is None else cq.Workplane('XZ').rect(w, h).extrude(-L).edges('|Y').fillet(r)
    s = s.translate((0, depth[0], zc)).val()
    return s.rotate(V(0, 0, 0), V(0, 0, 1), ang).translate(V(p[0], p[1], 0))

# outward wall normals of the straight edges the features sit on
N_ARM = (0.0, 1.0)
N_TOP_L = (-0.862, 0.507); N_TOP_R = (0.862, 0.507)
N_LEG_L = (0.2551, -0.9669)

OLD = [  # (name, wall point, normal, half width to fill, z range, source offset along the wall)
    ('PREV',  (-25.2, 21.9), N_ARM,   2.8, (4.6, 10.4), -5.5),
    ('MENU',  ( 25.2, 21.9), N_ARM,   4.8, (4.6, 10.4), 5.5),
    ('NEXT',  (-13.4, 29.0), N_TOP_L, 2.8, (4.6, 10.4), 6.0),
    ('POWER', ( 13.4, 29.0), N_TOP_R, 2.8, (5.8, 9.2), -6.0),
    ('USB_OLD', (-24.3, -36.9), N_LEG_L, 5.4, (5.0, 10.0), 7.0),     # old opening was 9.6 x 3.9
]
H = [(32.93, 14.28), (-32.93, 14.28), (35.3, -33.0), (-35.3, -33.0)]

def build(case_step, sw3_shape=None, usb_mouth=(-10.65, -30.73), usb_dir_deg=-75.0):
    solids = cq.importers.importStep(case_step).solids().vals()
    front = back = None; caps = {}
    for s in solids:
        bb = s.BoundingBox(); c = s.Center()
        if s.Volume() > 5000:
            if bb.zmin > 5: front = s
            else: back = s
        else:
            key = 'PLAY' if bb.zmin > 12 else ('MENU' if c.x > 10 else ('PREV' if c.y < 25 else 'NEXT'))
            caps[key] = s

    # 1. fill the v4 openings
    # (fillers are taken from the untouched v4 shells, then fused in one go)
    ff = [filler(front, p, n, hw, z0, z1, s) for name, p, n, hw, (z0, z1), s in OLD]
    bf = [filler(back, p, n, hw, z0, z1, s) for name, p, n, hw, (z0, z1), s in OLD]
    for f in ff:
        if f is not None: front = front.fuse(f)
    for f in bf:
        if f is not None: back = back.fuse(f)
    front = front.clean(); back = back.clean()

    # 2. new side holes, front shell only (z 9.2-14.2 and 9.15-11.75 are above the split)
    front = front.cut(wall_hole((-25.2, 21.9), N_ARM, 5.0, 5.0, ACT_Z))
    front = front.cut(wall_hole((-13.4, 29.0), N_TOP_L, 5.0, 5.0, ACT_Z))
    front = front.cut(wall_hole((25.2, 21.9), N_ARM, 9.0, 5.0, ACT_Z))           # MENU pill
    front = front.cut(wall_hole((13.4, 29.0), N_TOP_R, 6.0, 2.6, SLIDE_Z))       # POWER slot

    # 3. USB-C opening for J5: 13.0 x 7.4 so a plug overmold up to ~12 x 6.5 fits, from the mouth outward
    a = math.radians(usb_dir_deg); n = (math.cos(a), math.sin(a))
    usb = wall_hole(usb_mouth, n, 13.0, 7.4, 8.2 + 1.63, r=2.2, depth=(0.2, 6.0))
    front = front.cut(usb); back = back.cut(usb)

    # 4. bosses (back) and screw columns (front)
    for x, y in H:
        boss = cq.Workplane().circle(3.0).extrude(5.5).translate((x, y, 1.5)).val()
        back = back.fuse(boss).cut(cq.Workplane().circle(1.6).extrude(5.3).translate((x, y, 1.7)).val())
        col = cq.Workplane().circle(3.2).extrude(17.0 - 8.2).translate((x, y, 8.2)).val()
        front = front.fuse(col)
        front = front.cut(cq.Workplane().circle(1.15).extrude(15).translate((x, y, 8.0)).val())
        front = front.cut(cq.Workplane().circle(2.1).extrude(10).translate((x, y, 15.0)).val())
    back = back.clean(); front = front.clean()

    # 5. collar notch around the NEXT switch body (0.4 mm clearance), collar ring only
    if sw3_shape is not None:
        zone = cq.Workplane().circle(22.0).circle(18.9).extrude(16.6 - 12.4).translate((0, 0, 12.4)).val()
        if sw3_shape.intersect(zone).Volume() > 1e-3:
            front = front.cut(_grown_box(sw3_shape, zone)).clean()

    # 6. caps
    dz = ACT_Z - SIDE_Z_OLD
    for k in ('PREV', 'NEXT', 'MENU'):
        c = caps[k].translate(V(0, 0, dz))
        c = c.cut(cq.Solid.makeBox(120, 120, 10, V(-60, -60, 8.4 - 10)))          # clear of the board
        if k == 'MENU':
            c = c.cut(cq.Solid.makeBox(10, 10, 10, V(10.4, 14.0, 10.1 - 10)))       # clear of the GY-521 corner
        caps[k] = c.clean()
    bb = caps['PLAY'].BoundingBox()
    nub = cq.Workplane().circle(1.5).extrude(0.4).translate((-29.0, -26.5, bb.zmin - 0.4)).val()
    caps['PLAY'] = caps['PLAY'].fuse(nub).clean()
    return front, back, caps

def _grown_box(shape, zone, pad=0.4):
    """Axis-aligned slabs around shape ∩ zone, grown by pad: a simple clearance cutter."""
    inter = shape.intersect(zone)
    out = None
    for so in inter.Solids() if hasattr(inter, 'Solids') else [inter]:
        bb = so.BoundingBox()
        b = cq.Solid.makeBox(bb.xlen + 2 * pad, bb.ylen + 2 * pad, (16.6 - 12.4),
                             V(bb.xmin - pad, bb.ymin - pad, 12.4))
        out = b if out is None else out.fuse(b)
    return out.intersect(zone)
