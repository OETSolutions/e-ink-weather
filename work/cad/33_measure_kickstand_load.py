"""Sizing numbers for "will the swing-out foot hold the case up?".

The user's complaint is that nothing holds the leg open. That is a question about TORQUE,
not about whether a detent exists, so this computes both sides:

  * the moment gravity applies to the leg about the printed hinge, in the standing pose;
  * the moment the printed friction hinge and a bore detent can actually produce before
    the PETG clip exceeds its elastic strain limit.

Everything is measured off the saved solids; nothing here is assumed except material
properties, which are stated. Run:  freecadcmd 33_measure_kickstand_load.py
"""
import math
import os

import FreeCAD as App

HERE = os.path.dirname(os.path.abspath(__file__)) or "."

# --- material ---------------------------------------------------------------
RHO_PETG = 1.27e-3          # g/mm3
RHO_PANEL = 2.50e-3
RHO_BATTERY = 2.00e-3
RHO_PCB = 1.85e-3
E_PETG = 2000.0             # MPa (2.0 GPa), typical PETG
STRAIN_ALLOW = 0.025        # PETG allowable elastic strain (published 3-5%; 2.5% used)
MU = 0.35                   # PETG-PETG friction

# --- geometry, all from 06_rear_cover_foot.py -------------------------------
HINGE_Y = 5.00
KNUCKLE_Z = -0.68
PIN_R = 2.20
BORE_R = 2.08
CLIP_W = 3.00
CLIP_WALL = 1.60
ARM_CENTERS = [44.2, 67.2, 90.2]
FOOT_Y, FOOT_H = 8.5, 40.0
FOOT_X, FOOT_W = 32.2, 70.0
SWING_DEG = 65.0
COVER_T = 3.00
CASE_H = 108.5


def solid_cg(shape):
    """Volume-weighted centre of mass, valid for Compound too."""
    tv = 0.0
    ty = tz = 0.0
    solids = shape.Solids or [shape]
    for s in solids:
        v = s.Volume
        c = s.CenterOfMass
        tv += v
        ty += v * c.y
        tz += v * c.z
    return tv, ty / tv, tz / tv


def density(name):
    n = name.upper()
    if "PANEL" in n or "DISPLAY" in n:
        return RHO_PANEL
    if "BATT" in n:
        return RHO_BATTERY
    if "ESP" in n or "BOARD" in n or "PCB" in n:
        return RHO_PCB
    return RHO_PETG


def assembly_cg():
    doc = App.openDocument(os.path.join(HERE, "EInk_Weather_Display_Assembly.FCStd"))
    W = 0.0
    wy = wz = 0.0
    rows = []
    for o in doc.Objects:
        if not hasattr(o, "Shape") or o.Shape.isNull() or o.Shape.Volume <= 0:
            continue
        # Skip construction geometry: datum planes have "shapes" but no mass.
        if o.TypeId in ("App::Plane", "App::Line", "App::Point", "App::Origin",
                        "App::FeaturePython") or "Plane" in o.Name:
            continue
        # The already-assembled parts are conflated with their duplicates in the assembly;
        # only count leaf Part::Feature solids that carry real volume.
        if not o.TypeId.startswith("Part::"):
            continue
        vol, cy, cz = solid_cg(o.Shape)
        if vol <= 0:
            continue
        m = vol * density(o.Name + " " + o.Label)
        W += m
        wy += m * cy
        wz += m * cz
        rows.append((m, App.Vector(0, cy, cz), o.Name))
    rows.sort(key=lambda r: r[0], reverse=True)
    return W, wy / W, wz / W, rows


def leg_mass():
    """Mass of the printed leg alone (the swing-out foot solid)."""
    doc = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
    for o in doc.Objects:
        if "FOOT" in o.Name.upper() and hasattr(o, "Shape") and o.Shape.Volume > 0:
            vol, cy, cz = solid_cg(o.Shape)
            return vol * RHO_PETG, App.Vector(0, cy, cz)
    return 0.0, None


def main():
    W, cg_y, cg_z, rows = assembly_cg()
    print("=== mass budget (uniform-density estimate) ===")
    print("assembly total %.1f g   CG y %.2f  z %.2f" % (W, cg_y, cg_z))
    for m, c, name in rows[:8]:
        print("   %-40s %7.1f g  CG y %6.2f z %7.2f" % (name[:40], m, c.y, c.z))
    Wn = W * 9.81e-3                       # newtons
    print("assembly weight %.2f N" % Wn)
    m_leg, leg_cg = leg_mass()
    print("printed leg %.1f g" % m_leg)

    L = (FOOT_Y + FOOT_H) - HINGE_Y        # hinge to leg tip along the leg
    phi = math.radians(SWING_DEG)
    print("\nleg length hinge->tip %.1f mm, open angle %.0f deg" % (L, SWING_DEG))

    # Standing pose: the leg tip and the case's bottom edge both rest on the table.
    # Leg world direction is at angle (theta+phi) from vertical (derived in the notes),
    # and the 5 mm from the hinge to the bottom edge must land on the same table line:
    #     cos(theta+phi) = -(HINGE_Y/L) * cos(theta)
    def resid(th):
        return math.cos(th + phi) + (HINGE_Y / L) * math.cos(th)

    lo, hi = math.radians(1.0), math.radians(89.0)
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        if resid(lo) * resid(mid) <= 0:
            hi = mid
        else:
            lo = mid
    th = 0.5 * (lo + hi)
    print("standing lean: case %.1f deg back from vertical" % math.degrees(th))
    leg_world = math.degrees(th + phi)
    print("leg points %.1f deg from vertical (%.1f deg below horizontal)"
          % (leg_world, leg_world - 90.0))

    # Statics about the bottom-edge contact (case coords). Gravity in case coords is
    # (-cos th)*yhat + (sin th)*zhat per unit weight.
    gy, gz = -math.cos(th), math.sin(th)
    # Contact at the case's bottom-rear corner, hinge is 5 mm up the case from it.
    cy, cz = 0.0, 0.0
    hy, hz = HINGE_Y - cy, KNUCKLE_Z - cz
    # Leg tip in case coords.
    ty = HINGE_Y + L * math.cos(phi)
    tz = KNUCKLE_Z - L * math.sin(phi)
    # Moment of the weight about the bottom-edge contact, per newton of weight.
    m_cg = (cg_y - cy) * gz - (cg_z - cz) * gy
    m_tip = (ty - cy) * gz - (tz - cz) * gy
    # Balance: W*m_cg + N*m_tip = 0  where N is the upward tip reaction (world up maps to
    # (cos th)*yhat - (sin th)*zhat in case coords).
    n_y, n_z = math.cos(th), -math.sin(th)
    m_tipN = (ty - cy) * n_z - (tz - cz) * n_y
    N = -Wn * m_cg / m_tipN
    print("tip reaction N = %.2f N  (%.0f%% of weight)" % (N, 100.0 * N / Wn))

    # Holding moment required at the hinge. The tip reaction is vertical; in case coords
    # its line of action has direction (n_y,n_z). Moment about the hinge, sign positive =
    # folding the leg (reducing phi).
    dy, dz = ty - HINGE_Y, tz - KNUCKLE_Z
    tau_tip = N * (dy * math.sin(th) - dz * math.cos(th)) * -1.0
    # Leg's own weight at its CG, distance from the hinge.
    leg_cg_y, leg_cg_z = leg_cg.y, leg_cg.z
    dyw, dzw = leg_cg_y - HINGE_Y, leg_cg_z - KNUCKLE_Z
    tau_leg = (m_leg * 9.81e-3) * (dyw * math.sin(th) - dzw * math.cos(th)) * -1.0
    print("holding moment needed: tip %.1f N.mm  leg own weight %.1f N.mm  TOTAL %.1f N.mm"
          % (tau_tip, tau_leg, tau_tip + tau_leg))

    # What the printed joint can supply.
    print("\n=== what the printed clip can supply (per clip, %d clips) ===" % len(ARM_CENTERS))
    t, w, R = CLIP_WALL, CLIP_W, BORE_R
    I = w * t ** 3 / 12.0
    # Full-ring radial stiffness, softened ~4x for the 210-degree partial wrap.
    k_ring = E_PETG * I / (0.149 * R ** 3)
    k = k_ring / 4.0
    d_allow = STRAIN_ALLOW * R
    print("clip radial stiffness ~%.0f N/mm (%.0f full-ring, softened x4 for 210 deg)" % (k, k_ring))
    print("radial travel allowed at %.1f%% strain: %.3f mm" % (STRAIN_ALLOW * 100, d_allow))
    print("preload in the model: 0.12 mm -> %.1f%% strain -> %s"
          % (100 * 0.12 / R, "YIELDS" if 0.12 / R > STRAIN_ALLOW else "ok"))
    F_pre = k * 0.12
    tau_fric1 = MU * F_pre * R
    print("friction: %.0f N radial -> %.1f N.mm per clip -> %.1f N.mm total"
          % (F_pre, tau_fric1, tau_fric1 * len(ARM_CENTERS)))
    # Detent: a rib of radial height h must be climbed, needing ~h of radial expansion.
    for h in (0.10, 0.20, 0.35, 0.60):
        st = h / R
        Fd = k * h
        tau_d = Fd * R
        print("  detent %.2f mm deep: strain %5.1f%% %s  force %5.1f N  "
              "%.1f N.mm/clip  %.1f N.mm total"
              % (h, 100 * st, "BREAKS" if st > STRAIN_ALLOW else "ok  ",
                 Fd, tau_d, tau_d * len(ARM_CENTERS)))

    # Cantilever latch option: a beam long enough to deflect a useful amount safely.
    print("\n=== a dedicated cantilever latch could supply ===")
    for Lb, tb, wb in ((8.0, 1.20, 4.0), (10.0, 1.20, 4.0), (8.0, 1.00, 5.0)):
        # Deflection that reaches the strain limit:  e = 1.5*t*y/L^2
        y_allow = STRAIN_ALLOW * Lb ** 2 / (1.5 * tb)
        F = E_PETG * wb * tb ** 3 * y_allow / (4.0 * Lb ** 3)
        print("  beam L %.0f t %.2f w %.1f: travel %.2f mm, force %.2f N" %
              (Lb, tb, wb, y_allow, F))
        print("     at the leg's %.0f mm radius -> holding moment %.0f N.mm"
              % (L - 5.0, F * (L - 5.0)))


main()
