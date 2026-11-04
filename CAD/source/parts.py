"""Simplified 3D models for the parts that have no KiCad library model.

Model coordinates follow KiCad: mm, origin at the footprint origin, x right,
y = -footprint y (KiCad footprints are y-down, models are y-up), z = 0 on the
board's top face. Display and battery are built directly in case coordinates.
"""
import cadquery as cq

def box(x0, x1, y0, y1, z0, z1, fillet=0):
    b = cq.Workplane().box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))
    if fillet: b = b.edges('|Z').fillet(fillet)
    return b

def pins(xy, z0, z1, w=0.64):
    w2 = w / 2
    r = None
    for x, y in xy:
        p = box(x - w2, x + w2, y - w2, y + w2, z0, z1)
        r = p if r is None else r.union(p)
    return r

def rp2040_zero():
    # Waveshare RP2040-Zero, 23.5 x 18 x 1.0, castellated, soldered flat. USB-C on the
    # footprint's -y edge (model +y), overhanging the board by 0.8 mm.
    m = box(-9.0, 9.0, -11.75, 11.75, 0, 1.0)
    m = m.union(box(-4.47, 4.47, 5.44, 12.74, 1.0, 4.26, 0.4))        # USB-C receptacle
    m = m.union(box(-3.5, 3.5, -4.5, 2.5, 1.0, 1.9))                   # RP2040, 7x7 QFN
    m = m.union(box(-7.6, -5.0, 1.5, 4.5, 1.0, 2.2))                   # BOOT button
    m = m.union(box(5.0, 7.6, 1.5, 4.5, 1.0, 2.2))                     # RESET button
    m = m.union(box(-1.5, 1.5, -10.5, -6.5, 1.0, 1.9))                 # flash, USON-8
    m = m.union(box(5.5, 7.5, -10.0, -8.0, 1.0, 1.8))                  # WS2812 LED
    return m

def dfplayer_mini():
    # DFPlayer Mini (DFR0299 and clones), 20.6 x 20.6 x 1.0, pins soldered through the
    # carrier with the module flat on it. microSD socket on top, opening at model -y.
    m = box(-10.3, 10.3, -10.3, 10.3, 0, 1.0)
    m = m.union(box(-7.0, 7.0, -10.3, 4.6, 1.0, 2.85))                # microSD push-push socket
    m = m.union(box(-4.1, 4.1, 5.2, 9.6, 1.0, 2.7))                   # YX5200-24SS (SSOP-24)
    xs = [-8.89, 8.89]; ys = [-8.89 + 2.54 * i for i in range(8)]
    m = m.union(pins([(x, y) for x in xs for y in ys], -1.3, 1.6))    # leads, trimmed flush
    return m

def gy521():
    # GY-521 MPU6050 breakout, 21.2 x 16.4 x 1.6, flat on the carrier, 8 pins through.
    m = box(-10.6, 10.6, -8.2, 8.2, 0, 1.6)
    m = m.union(box(-2.0, 2.0, -2.5, 1.5, 1.6, 2.5))                  # MPU6050, 4x4 QFN
    m = m.union(box(-7.5, -4.5, -4.0, -1.0, 1.6, 2.7))                # 3.3 V LDO
    m = m.union(box(5.0, 6.6, -4.0, -3.2, 1.6, 2.2))                  # power LED
    m = m.union(pins([(-8.89 + 2.54 * i, 6.9) for i in range(8)], -1.3, 2.4))
    return m

def usb4125():
    # GCT USB4125 6P top-mount USB-C, 8.94 wide, 7.35 deep, 3.26 tall. Mouth at the
    # footprint's +y edge (y = 2.825 in the footprint, model -2.825).
    m = box(-4.47, 4.47, -2.825, 4.525, 0, 3.26, 0.3)
    mouth = box(-4.17, 4.17, -3.0, 4.0, 0.3, 2.96, 1.2)
    return m.cut(mouth.translate((0, 0, 0)).intersect(box(-5, 5, -3.0, 2.0, 0, 4)))

def os102011ma1q():
    # C&K OS102011MA1Q right-angle slide switch: body 8.6 x 4.4 x 4.5, lever 2 x 4 out of
    # the footprint's +y face (model -y), centred in height.
    m = box(-2.3, 6.3, -2.2, 2.2, 0, 4.5)
    m = m.union(box(0.0, 2.0, -6.2, -2.2, 1.25, 3.25))
    return m

def soic8_ep():
    return box(-1.95, 1.95, -2.45, 2.45, 0.1, 1.65, 0.0).union(
        pins([(sx * 2.7, y) for sx in (-1, 1) for y in (-1.905, -0.635, 0.635, 1.905)], 0, 0.6, 0.4))

def tact_6x6x5_tht():
    # 6x6 mm through-hole tactile switch, 5.0 mm tall (body 3.5 + round actuator).
    # Same pad pattern as KiCad's SW_PUSH_6mm (pads at 0/6.5 x 0/4.5 in the footprint).
    cx, cy = 3.25, -2.25
    m = box(cx - 3, cx + 3, cy - 3, cy + 3, 0, 3.5)
    m = m.union(cq.Workplane().circle(1.75).extrude(1.5).translate((cx, cy, 3.5)))
    m = m.union(pins([(0, 0), (6.5, 0), (0, -4.5), (6.5, -4.5)], -3.0, 0.5, 0.7))
    return m

# ---------------- case-coordinate parts
def gc9a01_display():
    # GC9A01 1.28" round module (nominal, not measured): PCB 38 dia x 1.6 with a header
    # tab 22 wide pointing down, glass 35.6 dia rising 2.1 above the PCB. Back of PCB at
    # z 13.7, glass top at the 17.4 seat.
    pcb = cq.Workplane().circle(19.0).extrude(1.6).union(box(-11, 11, -24.0, 0, 0, 1.6))
    pcb = pcb.translate((0, 0, 13.7))
    glass = cq.Workplane().circle(17.8).extrude(2.1).translate((0, 0, 15.3))
    # Header plastic removed and pins clipped to 1.5 mm; wires are soldered to the stubs
    # (a full-length header would hit the jack and the USB-C receptacle).
    leads = pins([(-7.62 + 2.54 * i, -22.05) for i in range(7)], 12.2, 13.7)
    return pcb.union(glass).union(leads)

def lipo_103040():
    # 3.7 V 1200 mAh LiPo, 40 x 30 x 10, in the back pod pocket.
    return box(-20, 20, -15, 15, -3.4, 6.6).edges('|Z').fillet(1.5)

MODULES = {'RP2040_Zero': rp2040_zero, 'DFPlayer_Mini': dfplayer_mini, 'GY521_MPU6050': gy521,
           'USB_C_GCT_USB4125_6P': usb4125, 'SW_Slide_CK_OS102011MA1Q': os102011ma1q,
           'SOIC-8-1EP_3.9x4.9mm': soic8_ep, 'SW_Tactile_6x6x5mm_THT': tact_6x6x5_tht}

if __name__ == '__main__':
    import sys, os
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    for name, fn in MODULES.items():
        cq.exporters.export(fn(), f'{out}/{name}.step')
        bb = fn().val().BoundingBox(); print(name, [round(v, 2) for v in (bb.xmin, bb.xmax, bb.ymin, bb.ymax, bb.zmin, bb.zmax)])

def pj307_jack():
    # PJ-307 3.5 mm TRS jack (nominal): body 6 x 12.5 x 5 in the board's jack slot, barrel
    # 5 mm dia pointing out of the bottom notch, axis at the old side height z 7.5.
    body = box(-3.0, 3.0, -28.7, -16.2, 5.0, 10.0)
    barrel = cq.Workplane('XZ').circle(2.5).extrude(2.0).translate((0, -28.7, 7.5))
    return body.union(barrel)
