"""Which way does the swing-out leg actually want to go, and how hard does it have to be held?

Patching the detent without answering this is how the last three attempts went wrong. The leg
is a PROP: the case rests on one edge and on the leg's tip, so the assembly has a single degree
of freedom and "folded or open?" is just "where is the potential energy lowest?".

For each open angle phi this solves for the lean angle alpha that puts the case's lowest point
and the leg tip on the same table line, using the real solids (not a box model), then reports

    tau = dU/dphi

which is exactly the moment the hinge must supply to hold the leg there. Positive tau means
gravity is folding the leg and the hinge has to resist it.

Run:  freecadcmd 36_kickstand_energy.py
"""
import math
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) or "."

RHO = {"PETG": 1.27e-3, "PANEL": 2.50e-3, "BATT": 2.00e-3, "PCB": 1.85e-3}
HINGE = (5.00, -0.68)
G = 9.81e-3


def rho_of(name):
    n = name.upper()
    if "PANEL" in n or "DISPLAY" in n:
        return RHO["PANEL"]
    if "BATT" in n:
        return RHO["BATT"]
    if "ESP" in n or "BOARD" in n or "PCB" in n:
        return RHO["PCB"]
    return RHO["PETG"]


def solid_cg(shape):
    tv = 0.0
    ty = tz = 0.0
    for s in (shape.Solids or [shape]):
        v = s.Volume
        if v <= 0:
            continue
        c = s.CenterOfMass
        tv += v
        ty += v * c.y
        tz += v * c.z
    return (tv, ty / tv, tz / tv) if tv > 0 else (0.0, 0.0, 0.0)


def hull2d(pts):
    pts = sorted(set((round(y, 4), round(z, 4)) for y, z in pts))
    if len(pts) <= 2:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower = []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper = []
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def load():
    """(case mass, case CG, case hull, leg mass, leg CG, leg hull) from the saved assembly."""
    doc = App.openDocument(os.path.join(HERE, "EInk_Weather_Display_Assembly.FCStd"))
    case_parts, leg_shape = [], None
    for o in doc.Objects:
        if not o.TypeId.startswith("Part::") or not hasattr(o, "Shape") or o.Shape.isNull():
            continue
        nm = (o.Name + " " + o.Label).upper()
        if "PLANE" in nm or "ORIGIN" in nm or "REFERENCE" in nm:
            continue
        if o.Shape.Volume <= 0:
            continue
        if "SWING_FOOT" in nm:
            leg_shape = o.Shape
            continue
        case_parts.append(o)

    m_c = 0.0
    cyc = czc = 0.0
    for o in case_parts:
        vol, y, z = solid_cg(o.Shape)
        m = vol * rho_of(o.Name + " " + o.Label)
        m_c += m
        cyc += m * y
        czc += m * z
    cyc /= m_c
    czc /= m_c
    case_hull = hull2d([(v.Point.y, v.Point.z) for o in case_parts for v in o.Shape.Vertexes])

    m_l, cyl, czl = solid_cg(leg_shape)
    m_l *= RHO["PETG"]
    leg_hull = hull2d([(v.Point.y, v.Point.z) for v in leg_shape.Vertexes])
    return m_c, (cyc, czc), case_hull, m_l, (cyl, czl), leg_hull


def main():
    m_c, cg_c, case_hull, m_l, cg_l, leg_hull = load()
    print("case without leg %.1f g  CG y %.2f z %.2f  (hull %d pts)"
          % (m_c, cg_c[0], cg_c[1], len(case_hull)))
    print("leg %.1f g  folded CG y %.2f z %.2f  (hull %d pts)"
          % (m_l, cg_l[0], cg_l[1], len(leg_hull)))
    M = m_c + m_l
    hy, hz = HINGE

    def rot(p, a):
        """Rotate case coords by -a about X (lean back); returns (y', z')."""
        y, z = p
        c, s = math.cos(a), math.sin(a)
        return (y * c + z * s, -y * s + z * c)

    def leg_pt(p, phi, a):
        y, z = p
        # swing open by phi about the hinge
        dy, dz = y - hy, z - hz
        c, s = math.cos(phi), math.sin(phi)
        y2, z2 = hy + dy * c + dz * s, hz - dy * s + dz * c
        return rot((y2, z2), a)

    def case_low(a):
        return min(rot(p, a)[1] for p in case_hull)

    def leg_low(phi, a):
        return min(leg_pt(p, phi, a)[1] for p in leg_hull)

    def solve_lean(phi):
        lo, hi = math.radians(5.0), math.radians(80.0)
        f = lambda a: case_low(a) - leg_low(phi, a)
        flo = f(lo)
        for _ in range(60):
            mid = 0.5 * (lo + hi)
            if flo * f(mid) <= 0:
                hi = mid
            else:
                lo, flo = mid, f(mid)
        return 0.5 * (lo + hi)

    print()
    print(" phi |  lean |  tipY  tipZ |  CGh  |    U mJ | tau N.mm | gravity")
    prev = None
    for k in range(20, 101, 2):
        phi = math.radians(k)
        a = solve_lean(phi)
        cz = case_low(a)
        # CGs in world, relative to the table line z=cz
        gy, gz = rot(cg_c, a)
        ly, lz = leg_pt(cg_l, phi, a)
        gy_w = (m_c * gy + m_l * ly) / M
        gz_w = (m_c * (gz - cz) + m_l * (lz - cz)) / M
        U = M * G * gz_w
        tip = leg_pt((max(p[0] for p in leg_hull),), phi, a) if False else None
        ty = max(leg_pt(p, phi, a)[0] for p in leg_hull)
        tz = leg_low(phi, a) - cz
        if prev is not None:
            tau = (U - prev[0]) / math.radians(2.0)
            print(" %3d | %5.1f | %6.1f %5.1f | %5.2f | %7.2f | %+8.1f | %s"
                  % (k, math.degrees(a), ty, tz, gz_w, U, tau,
                     "FOLDS the leg" if tau > 0 else "OPENS the leg"))
        else:
            print(" %3d | %5.1f | %6.1f %5.1f | %5.2f | %7.2f |"
                  % (k, math.degrees(a), ty, tz, gz_w, U))
        prev = (U, k)

    print()
    print("tau > 0 means gravity is trying to FOLD the leg; the hinge must supply that moment")
    print("to hold it. tau < 0 means gravity props it open against the printed stop.")


main()
