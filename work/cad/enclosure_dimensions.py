"""Authoritative enclosure dimensions and FDM design minimums.

Final generators import this module. Do not duplicate these stack planes in individual
scripts: that is how the prior 0.3 mm display lip and inconsistent battery clearance were
introduced.

Z=0 is the rear-most nominal cover plane; +Z points toward the display/front.
"""

# ---- sourced/user envelopes ----------------------------------------------------
PANEL_W=125.4
PANEL_H=99.5
PANEL_T=0.9
BATTERY_W=70.0
BATTERY_H=39.0
BATTERY_T=11.0
CASE_W=134.4
CASE_H=108.5

# ---- explicit FDM design minimums (0.4 mm nozzle, PETG) -------------------------
MIN_PRINTED_WALL=1.2          # three 0.4 mm extrusion lines
MIN_LOAD_WALL=1.6             # retaining lips / structural links
MIN_M2_THREAD=2.0             # distributed case attachment, self-tap pilot
MIN_M2_5_THREAD=4.0           # DEPRECATED: no M2.5 screws are permitted in this build.
                              # Kept only so older scripts still import; use MIN_M3_THREAD.
# The user owns M2 and M3 screws ONLY and has said so repeatedly ("why did you design m2.5
# screws AGAIN!!!! I told you M2 or M3!!!! I don't have M2.5 screws!"). Every threaded
# feature must use an M2 or an M3. The user has since said M2 is BETTER ("M2 screws are
# better"), so M2 is now the default and M3 is kept only where an M2 would be too weak.
MIN_M3_THREAD=5.0             # M3 self-tap pilot engagement in a printed boss

# ---- fastener geometry, one place -------------------------------------------------
M2_CLEAR=2.2                  # M2 clearance hole
M2_HEAD=4.5                   # M2 pan-head diameter
M2_PILOT=1.6                  # M2 self-tapping pilot
M2_ENGAGE=4.0                 # 2x diameter: the usual minimum for a self-tap in plastic
M3_CLEAR=3.4                  # M3 clearance hole
M3_HEAD=6.0                   # M3 pan-head diameter
M3_PILOT=2.5                  # M3 self-tapping pilot
MIN_SKIN=1.2                  # pocket/floor skin

# ---- complete rear-to-front physical stack ------------------------------------
REAR_COVER_T=3.0              # 1.8 foot pocket leaves 1.2 skin
BATTERY_REAR_PAD=0.2          # compressed locating pad / assembly tolerance
BATTERY_Z0=REAR_COVER_T+BATTERY_REAR_PAD              # 3.2
BATTERY_Z1=BATTERY_Z0+BATTERY_T                        # 14.2
GRID_ASSEMBLY_GAP=0.1
GRID_Z0=BATTERY_Z1+GRID_ASSEMBLY_GAP                   # 14.3
GRID_T=1.2
GRID_Z1=GRID_Z0+GRID_T                                 # 15.5
REAR_FOAM_T=0.5
REAR_FOAM_Z0=GRID_Z1                                   # 15.5
PANEL_Z=REAR_FOAM_Z0+REAR_FOAM_T                       # 16.0
PANEL_Z1=PANEL_Z+PANEL_T                               # 16.9
FRONT_GASKET_T=0.2
FRONT_GASKET_Z0=PANEL_Z1                               # 16.9
LIP_UNDERSIDE=FRONT_GASKET_Z0+FRONT_GASKET_T           # 17.1
RETAINING_LIP_T=1.6
CASE_D=LIP_UNDERSIDE+RETAINING_LIP_T                   # 18.7

# One assembly split plane: rear chassis owns <=; front bezel owns >=.
SPLIT_Z=GRID_Z1

# ---- folded FPC ---------------------------------------------------------------
FPC_ROOT_X=67.2
FPC_ROOT_Y=4.5
FPC_ROOT_Z=PANEL_Z+0.06        # 0.12 mm FPC midplane
FPC_LEN=24.0
FPC_T=0.12
FPC_ROOT_W=24.5
FPC_TIP_W=12.5
FPC_TAPER=3.69
# Bend radius is bounded from BOTH sides:
#   * upper bound: the fold must not drive the tail into the chassis. At R3.9 the tail
#     descended to z=8.5 and swung to y=0.54, crossing the side wall and grid (7.12 mm3).
#   * lower bound: a tighter fold pulls P5 lower and can starve the board standoffs.
# R2.9 keeps the tail clear of the wall while holding the board roughly where the verified
# assembly had it. Inner radius 2.84 stays above the 2.0 mm flex minimum.
FPC_BEND_R=2.9


def validate_stack():
    tol=1e-9
    assert abs(REAR_COVER_T-3.0)<tol
    assert REAR_COVER_T-1.8>=MIN_SKIN-tol
    assert abs(GRID_Z1-GRID_Z0-MIN_PRINTED_WALL)<tol
    assert abs(PANEL_Z1-PANEL_Z-PANEL_T)<tol
    assert abs(LIP_UNDERSIDE-PANEL_Z1-FRONT_GASKET_T)<tol
    assert abs(CASE_D-LIP_UNDERSIDE-RETAINING_LIP_T)<tol
    assert RETAINING_LIP_T>=MIN_LOAD_WALL-tol
    # SPLIT_Z must equal the grid top so the two printed halves meet at one plane.
    assert abs(SPLIT_Z-GRID_Z1)<tol


validate_stack()
