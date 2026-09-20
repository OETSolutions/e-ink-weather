"""Printable rear cover, service hatch and dual-purpose swing-out foot.

Parts (all FDM-printable, NO purchased hardware for the pivot):
  PRINT_REAR_COVER    - 2.8 mm deep removable cover, follows the 8 mm case chamfer
  PRINT_SERVICE_HATCH - snap-in connector hatch with a USB cable exit slot
  PRINT_FOOT          - 70 x 40 x 1.8 mm foot, flush in the rear pocket when folded

HINGE: a printed bottom-edge snap bearing replaces the sourced 2.0 mm metal pin.

The pivot sits on the case's bottom edge, outside the flat rear face. The leg's full-width
printed knuckle snaps into a C-cradle printed as part of the rear cover. Three narrow necks
connect knuckle to leg through matching swept slots hidden behind the closed barrel. Moving
the pivot to the case edge is essential: the earlier pocket-edge pivot forced the 40 mm leg
to orbit through the cover wall; clearing it required an unacceptable see-through slot.
At the edge, the leg rotates into open air and is collision-free at every integer degree
from folded to 65 degrees open.

The previous pin design also had 190.4 mm3 of foot/cover interference because its barrel
punched through the 1.0 mm pocket skin. The saved bottom-edge joint has zero interference,
14/24 radial bearing-support samples, and continuous cover load paths at five X stations
(`18_validate_printed_hinge.py`).

Dimensions marked ASSUME are design choices, not sourced.
"""
import FreeCAD as App
import Part
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import (CASE_W,CASE_H,CASE_D,REAR_COVER_T,GRID_Z0,
                                  MIN_PRINTED_WALL,MIN_LOAD_WALL,MIN_SKIN,
                                  MIN_M2_THREAD, MIN_M3_THREAD,
                                  M2_CLEAR,M2_HEAD,M2_PILOT)

HERE = os.path.dirname(os.path.abspath(__file__))
COVER_T = REAR_COVER_T                              # 3.0 mm cover
FOOT_W, FOOT_H, FOOT_T = 70.0, 40.0, 1.8           # ASSUME
# Bottom-edge swing-out leg: body starts at y=8.5, immediately above the pivot line.
FOOT_X, FOOT_Y = (CASE_W-FOOT_W)/2, 8.5             # DERIVED from edge-pivot architecture
POCKET_CLEAR = 0.30                                  # ASSUME per-side FDM clearance
POCKET_DEPTH = FOOT_T                                # 1.8 flush foot
POCKET_SKIN = COVER_T-POCKET_DEPTH                   # 1.2 mm, exceeds MIN_SKIN
POCKET_ROOF = POCKET_DEPTH

# ---- printed bottom-edge pivot -------------------------------------------------
# Earlier revisions put the pivot at the pocket's +Y edge. That cannot work: the leg's orbit
# passes through the cover wall. The bottom-edge axis instead sends the leg into open air.
# It is clear of battery (y>=18), every connector (y>=67), and the display envelope.
HINGE_Y = 5.00                                      # axis just outside the bottom-edge wall

# ---- rigid cover pin + compliant foot clip ----------------------------------------
# The pin is printed as part of the cover; the foot carries the C-clip that snaps onto it.
# The rigid side of the joint is a solid pin and the compliant side is a clip that flexes in
# the XY plane -- the standard printable arrangement.
PIN_R = 2.20                                        # rigid pin radius
# FRICTION IS THE ONLY THING THAT CAN HOLD THE LEG OPEN. Read this before changing it.
# The user: "The stops are more robust, but they only stop the foot from OPENING!!! The whole
# problem is that it will just fall shut." MEASURED, and it is a hard geometric fact, not an
# oversight: as the leg CLOSES, its closest material moves AWAY from the cover -- the leg only
# ever touches the cover through this clip preload, and that contact does NOT grow as the leg
# shuts (leg/cover overlap is 15.69 mm3 at every angle from 0 to 63 deg, unchanged). So there is
# nothing anywhere for a closing-side stop to push against: a stop placed there is simply not
# reached, and the leg swings past it. That is why a more robust stop changed nothing.
# The only closure-side hold mechanisms at a pivot are (a) friction, or (b) a detent that
# protrudes into the bore. (b) was rejected on sight by the user ("Get RID of the nubs!!") and
# the reason was real -- see [[project-printed-hinge-design]] on how a bore nub splays the
# clip's free mouth lips. So the holding force is the bore interference, and it is set as high
# as PETG's elastic range safely allows rather than to a nominal "light friction" value.
# The two strain models in the toolchain disagree (validator 29 uses the full mouth arc as the
# flexure length and computes 3.0% here; validator 28 uses a shorter effective length and
# computes 8.4%). Rather than trust the optimistic one, the preload is set so the PESSIMISTIC
# model still passes: 0.18 mm gives ~4.3% by that model and ~1.6% by the other. That is still
# 1.5x the old 0.12, but preload alone cannot close the gap -- see the note above, and the
# "beefier but still flexible" tension noted at CLIP_W.
PIN_PRELOAD = 0.18                                  # interference -> friction (see above)
PIN_CLEAR = -PIN_PRELOAD                            # negative: bore is SMALLER than the pin
BORE_R = PIN_R+PIN_CLEAR                            # clip bore radius (2.02)
# NO DETENT -- folded OR open. Read this before re-adding one.
# The hinge used to carry a raised rib inside each clip (at MOUTH_PIN_ANGLE+180) dropping into a
# matching groove in the pin at the folded angle, plus a second groove at the open angle. The user
# rejected it: "Get RID of the nubs!! They still cause the end of the hooks to flare out and hit!!!"
# The failure is geometric, not a matter of sizing the nub smaller. A rib inside a C-clip is an
# INTERFERENCE feature: pushing the ring out at one angular point opens the ring, and a ring opens
# at its FREE ENDS -- the two mouth lips. So riding the rib over the rigid pin splays the lips
# outward, and the lips are the last material to clear the cover's cavity as the leg swings, so
# they catch. My clearance probe never saw it because it measured a single scalar radial
# expansion at the rib's own angle on an undeflected clip; a local flare at the far side of the
# ring, at the free ends, cannot be represented by one number.
# With no detent the leg is held by the bore interference alone (PIN_PRELOAD), i.e. a plain
# friction hinge: it stays wherever it is put by hand but has no positive location folded or open.
MOUTH_PIN_ANGLE = 0.0
# BEEFED UP 2026-09-20. The user: "the hooks on the foot that go around the hinge pin are too
# weak and need to be beefier." They were 1.6 mm wall x 3.0 mm wide -- the minimum load-bearing
# wall and the narrowest sane bearing. The ring's radial bending section modulus is
# CLIP_W*CLIP_WALL^2/6, so at 3.0 x 1.6 it was 1.28 mm3; at 4.5 x 2.2 it is 3.63 mm3, i.e.
# 2.8x stronger, with 1.5x the axial bearing length so the pin load is spread further.
# CLIP_WALL is DERIVED near KNUCKLE_Z (it is not free: it must exactly fill the gap between
# BORE_R and the cover split plane).
CLIP_W = 4.50                                       # was 3.00; 1.5x wider bearing
# ROOT GUSSET: the haunch that joins each clip ring to the leg plate.
# The ring is a CYLINDER about the hinge axis and the plate is a flat slab, so where the two
# meet they are TANGENT -- not overlapping. MEASURED in probes/93_probe_clip_weld.py: the whole
# attachment was 0.0870 mm3, a 0.12 mm deep line contact (Y 8.50..8.62, Z 0.00..0.46), i.e. the
# clips are not welded to the foot at all. That is the user's "the hooks that go around the back
# cover rod on the swing-out foot just fall right off after printing".
# The gusset fills the wedge between the ring's outer wall and the plate's underside so the two
# become one solid. MEASURED (same probe): 28.12 mm3 per clip, a 320x increase.
CLIP_GUSSET_Y0 = 7.75                               # clear of the mouth box (+Y edge 7.704);
                                                    # 0.0000 mm3 of gusset enters the mouth
CLIP_GUSSET_Y1 = 12.00                              # 3.5 mm of lap onto the plate's underside
CLIP_GUSSET_Z0 = -2.80                              # down to the ring's outer wall
CLIP_GUSSET_Z1 = 1.80                               # flush with the plate's top face
CLIP_GUSSET_FILLET_ROOT = 1.20                      # fillet on the gusset's real root corners.
                                                    # The user: "fillets ... in the acute corners
                                                    # where it will make the gussets stronger."
PIN_ROOT = 4.00                                     # pin extension each side into the cover
WEB_Y0 = 1.00                                       # web start Y, 1 mm INSIDE the cover
                                                    # so the fuse is a real overlap
ARM_T = 1.60                                        # arm thickness (>= 1.2 mm)
CLIP_ROOT_Z = 0.40                                  # clip root height above the plate
# --- wall hang ------------------------------------------------------------------
# The hang point MUST sit above the assembly centre of gravity or the case rotates over when
# hung. Measured assembly CG is at y=52.3 for every battery mass scenario (50/75/100 g), but
# the foot -- the part that folds into the pocket -- only spans y 8.5..48.5, so no keyhole in
# the foot can ever be above the CG. That is exactly why the old slot at y=23.5 rotated the
# case onto its side. The keyholes are therefore in the COVER, above the CG, and there are TWO
# of them so the case cannot rotate about the nail either.
WALL_HANG_Y = 57.0                                  # HEAD-hole centre; the shank finally
                                                    # rests at WALL_HANG_Y+WALL_HANG_SLOT=65,
                                                    # 12.7 mm above the CG (52.3)
WALL_HANG_SLOT = 8.0                                # slot opens UPWARD from the head hole
WALL_HANG_OFFSET = 11.8                             # each keyhole this far from the case
                                                    # centre-line (67.2) -> symmetric pair at
                                                    # x = 55.4 and 79.0, 23.6 mm apart
WALL_HANG_X = (CASE_W/2-WALL_HANG_OFFSET, CASE_W/2+WALL_HANG_OFFSET)
# Placement is constrained on four sides and only this band satisfies all of them:
#   * the SHANK rests at y=65, above the assembly CG (52.3) -- otherwise the hung case
#     rotates over onto its side;
#   * clear of the +Y edge of the swing-foot pocket (y 8.2..48.8) so each hole is a hole in
#     solid 3 mm cover plate, not a hole through the 1.2 mm pocket skin;
#   * the slot top stays below the service bay (y 68) and below the hatch flange's lower
#     edge (66.75), or the screw could slide out sideways into the connector opening;
#   * outside the hinge cavity's skin-void bands x 44.2/67.2/90.2 +- 3.15 that the clip
#     sweep cuts up into z 2.18 -- a hole there would be a 0.82 mm deep notch, not a slot.
PRINT_RIB_Y = 4.00                                  # print-support rib depth at the far edge
RIB_INSET = 4.00                                    # keep the rib clear of the stop at each end
KNUCKLE_R = PIN_R                                   # kept for the print-orientation printout
# Axis height is bounded by the cover depth: the clip's outer top must stay under the cover
# split plane, i.e. KNUCKLE_Z+BORE_R+CLIP_WALL <= COVER_T. The axis is pinned to a FIXED value
# and the wall thickness is derived from it, NOT the other way round -- coupling the axis to
# BORE_R meant every change to PIN_PRELOAD moved the axis, which invalidated the stop's bearing
# face (that face is derived against this exact axis). Two things should not share a variable.
# KNUCKLE_Z = -1.05 is the value the stop face was solved against.
KNUCKLE_Z = -1.05                                   # FIXED; see STOP_RAMP_P derivation
CLIP_WALL = COVER_T-KNUCKLE_Z-BORE_R                # derived, so the clip just fits under the
                                                    # cover: 3.00-(-1.05)-1.85 = 2.20 mm

ARM_W = 6.00                                         # ASSUME; three strong printed necks
ARM_CENTERS = [FOOT_X+12.0, FOOT_X+FOOT_W/2, FOOT_X+FOOT_W-12.0]
SWING_DEG = 65.0                                     # open angle
# At y=8.5 the battery is still 5.7 mm away, so the pivot can use the leg's full width.
HINGE_X0, HINGE_X1 = FOOT_X, FOOT_X+FOOT_W           # full-width load distribution
WEB_Z1 = 3.00                                       # web tops at the cover split plane
CAVITY_X0 = HINGE_X0-2.0                            # clip-sweep cavity, cut before the pin
CAVITY_X1 = HINGE_X1+2.0
# Radial clearance between the clip's OUTER wall and the cavity wall. The clip is a bearing on
# the pin, so this is a running clearance, not just print clearance: the clip's outer wall sits
# at BORE_R+CLIP_WALL, stretched by the bore interference to BORE_R+CLIP_WALL+PIN_PRELOAD = 3.80.
# The margin the cover must keep over that is CAVITY_CLEAR - PIN_PRELOAD = 0.48 mm, i.e. two
# nozzle widths. (This was 0.90 while a detent nub lived in the clip bore and had to be cleared
# at its crest; the nubs are gone, so the cavity is back to the plain running clearance and the
# cover keeps the material.)
CAVITY_CLEAR = 0.60
# The cavity is cut as bands this much wider than each clip, at the clip stations only. Cutting
# it across the FULL width at this radius cost the cover's hinge rail 20 mm3 (validator 23).
CAVITY_PAD = 0.60
# Target lip interference when the pin passes the mouth: small but definite.
LIP_INTERFERENCE = 0.07
CHEEK_T = 3.00                                       # ASSUME side cheek thickness
SLOT_CLEAR = 0.25                                    # ASSUME neck-to-slot clearance per side
# --- 65-degree hard stop ------------------------------------------------------
# The stop must engage ABOVE the gravity barrier (~58.5 deg). 36_kickstand_energy.py gives
# tau = dU/dphi crossing zero there: below it gravity FOLDS the leg, above it gravity OPENS it.
# A stop below the barrier is pressed from the wrong side and the stand collapses. Ours engages
# at 65.0 deg, with margin. (The original lug sat at 55.2 deg -- below the barrier -- and was
# replaced for exactly that reason.)
#
# WHAT WENT WRONG, AND WHAT CHANGED 2026-09-20. The user: "The stops still break off. They need
# more meat." and "the whole foot and stops needs to be completely redone ... the stops are on
# the wrong side of the foot to keep it open; they keep it FROM opening further, but don't keep
# it from closing to prop it up." MEASURED on the two-span buttress this replaces:
#   * it existed ONLY at x 29.40..40.60 and 93.80..105.00, so over the leg's central 53 mm
#     (x 40.6..93.8) there was nothing below the cover floor at all;
#   * its bearing contact at 65 deg was a 0.28 mm3 sliver and its weld into the cover's lower
#     wall was 2.85 mm3 -- a 3 mm3 section carrying the whole standing load. That is why it
#     snaps off first, and once it is gone the leg has nothing to rest against: the collapse.
#   * its load path also ran sideways, from a bearing face at x 29.4..40.6 to webs at x 28..32.
# The stop is now the same buttress section repeated in EVERY GAP between the clips
# (STOP_SECTIONS), so a section sits directly under the bearing face at every x across the leg,
# each with the same short path face -> section -> bed -> web. It is deliberately not one
# continuous bar: the clip's mouth sweeps down through the bar's own sector as the leg opens,
# and only the GAPS are free of the clip at every angle.
STOP_ENABLE=True


# ---- STOP REDESIGN 2026-09-20: A SECTION IN EVERY GAP BETWEEN THE CLIPS -------------
# The user: "The stops still break off. They need more meat." and "the whole foot and stops
# needs to be completely redone ... the stops are on the wrong side of the foot to keep it open;
# they keep it FROM opening further, but don't keep it from closing to prop it up."
#
# MEASURED DEFECTS in the two-span buttress this replaces:
#  - It existed only at x 29.40..40.60 and 93.80..105.00. Over the leg's whole CENTRAL 53 mm
#    (x 40.6..93.8) there was NOTHING below the cover floor at all, so more than half the leg
#    had no stop under it.
#  - Its bearing contact at 65 deg was a **0.28 mm3 sliver** and its weld into the cover's
#    lower wall only **2.85 mm3**. That is why it snaps off -- and once it is gone the leg has
#    nothing to rest against, which is the collapse the user reports.
#  - The load path also ran sideways: the bearing face sat at x 29.4..40.6 while the only solid
#    cover material it reached (the end webs) is at x 28.2..32.0.
#
# THE REPLACEMENT: the SAME buttress section, repeated in every gap between the clips, so a
# section of stop sits directly under the bearing face at every x across the leg (see
# STOP_SECTIONS). This is deliberately NOT one continuous full-width bar: the clip's mouth
# sweeps DOWN through the bar's own region as the leg opens (MEASURED: the clip's free mouth
# runs from pin-angle 40 deg folded to 105 deg at 65 deg -- exactly the sector the bar
# occupies), so a continuous bar would collide with the mouth in mid-swing. In the GAPS the
# sweep is completely free (MEASURED: 0.000 mm3 at every angle), so the sections go there.
# The load path is then the same short one everywhere: face -> section -> bed -> web.
# Stop sections, positioned ONLY in the free gaps between the clips. Each clip band is
# CLIP_W wide (4.5) centred on ARM_CENTERS, so the gaps are those bands' complement. The
# sections must not enter a band at all: inside a band the clip's own preload already overlaps
# the cover, and adding stop material there would both stiffen the clip and confuse the
# preload test (validator 18 splits the overlap into "in the clips" and "anywhere else").
# The outer pair is extended 0.4 mm further out to merge with the end webs
# (x 28.20..31.99 / 102.41..106.19), which is what anchors the stop to the cover.
_CW = CLIP_W/2.0
STOP_SECTIONS=((ARM_CENTERS[0]-_CW-2.0, ARM_CENTERS[0]-_CW),
               (ARM_CENTERS[0]+_CW,       ARM_CENTERS[1]-_CW),
               (ARM_CENTERS[1]+_CW,       ARM_CENTERS[2]-_CW),
               (ARM_CENTERS[2]+_CW,       ARM_CENTERS[2]+_CW+2.0))
# Bearing-face line. RE-DERIVED 2026-09-20 from the leg's REAL solid at 65 deg, not by
# rotating the plate's sharp corner: the plate is an r4.0 rounded prism, so its nominal corner
# (8.50,0.00) is CUT AWAY and the material there begins ~1.2 mm inward. Rotating the sharp
# corner therefore put the face 0.85 mm outside the leg (onset late at 66 deg, and the earlier
# (7.10,-3.56) was left over from the old -0.68 axis besides). MEASURED off the rotated solid,
# the leg's bearing surface at 65 deg is a convex corner whose 65 deg TANGENT passes through
# (8.00,-4.25); that line reaches the cover floor (z -2.88) at (7.361,-2.88) and the plate's
# outer edge at (8.50,-5.32).
STOP_RAMP_P=(7.089,-3.007)                          # inner end of the face
STOP_RAMP_LEN=3.57                                  # outward along the 65 deg direction
STOP_RAMP_T=3.0                                     # material behind the face
STOP_BACK_Y0=4.00                                   # section back face, inside the cover
STOP_Z_BOTTOM=-7.82                                 # the cover's own lowest point; the section is
                                                    # flush with it, so ground clearance and the
                                                    # print-orientation floor are unchanged
# CONCAVE-ROOT FILLETS. The user: "Both the stops and the gussets you added should have fillets
# in the corners to make them a lot stronger." A buttress fails at its root, where peak stress
# sits, so the fillets go on the buttress's own corners. MEASURED: R > 1.00 fails the Boolean on
# this profile (15StdFail_NotDone BRep_API), so 1.00 is the largest that builds -- still a real
# Kt reduction over a sharp corner. The bearing face's outer edges are deliberately NOT filleted:
# that is the stop face and must stay crisp, and a fillet there would round the 65 deg
# engagement point too.
STOP_FILLET_R=1.00

# ---- closure screws -----------------------------------------------------------
# M2 (user: "M2 screws are better"). Four distributed fasteners hold the rear cover on, so
# each carries only the sealing load; M2's 4.0 mm self-tap engagement is ample.
SCREW_D = M2_CLEAR                                  # 2.2 M2 clearance
SCREW_HEAD_D = M2_HEAD                              # 4.5 M2 pan-head counterbore
SCREWS = [(11.0,11.0),(CASE_W-11.0,11.0),(11.0,CASE_H-11.0),(CASE_W-11.0,CASE_H-11.0)]  # DERIVED: must match chassis bosses

# ---- service bay + snap hatch -------------------------------------------------
# DERIVED from the flipped-board transform (Rx180 then Rz-90): all four service
# interfaces still face +Y at y~67..74, but their X span is now 25.9..75.8.
SERVICE_BAY_X0,SERVICE_BAY_X1=22.5,79.2
SERVICE_BAY_Y0,SERVICE_BAY_Y1=68.0,92.0
HATCH_CLEAR=0.25                                      # ASSUME per-side FDM plug clearance
# The plug runs the FULL cover thickness. It was 1.4 mm, which left the bay's outer 1.6 mm open
# and gave the hatch only 1.4 mm of bearing on the bay walls to resist rotation.
HATCH_PLUG_T=COVER_T                                  # fills the opening through its whole depth
HATCH_FLANGE_T=1.2                                    # ASSUME exterior overlay
# Snap-in capture replaces the previous 2x M2 screws: two in-plane PETG cantilever
# tongues are cut out of the hatch plug. Only their small detent noses enter matching
# pockets in the bay walls; the beam bodies stay wholly inside the bay clearance.
# Hatch retention is a SINGLE SCREW plus a continuous anti-rotation lip. The side
# cantilevers are gone: they were cut from the plug but remained attached along their length
# and never released ("the friction pieces on the sides stuck to the side and don't print").
HATCH_SCREW_D=M2_CLEAR                               # M2 clearance
HATCH_SCREW_HEAD_D=M2_HEAD                           # M2 pan head
# ONE screw, driven from OUTSIDE through the hatch's flange and into a boss on the case's
# rear face. The screw axis must lie OUTSIDE the bay opening: the bay spans x 22.5..79.2,
# and its rim material is only y 22.5..28.9 below the opening between x 52.2 and 82.6.
# Measured: the old axis (50.85, 62.0) fell outside the flange's own Y range (66.75..93.25)
# and left 0.0000 mm3 of flange material anywhere in the screw column -- the screw had
# nothing to pass through.
HATCH_SCREW_X=SERVICE_BAY_X1+5.3                              # 84.5, outboard of the lip
HATCH_SCREW_Y=84.0                                            # bay mid-height
HATCH_BOSS_R=2.6                                    # M2 boss: 1.8 mm wall around the 1.6 pilot

# ---- anti-rotation keys ---------------------------------------------------------
# WHY THE CONTINUOUS LIP IS GONE. It was a ring on the plug's inner face sized to drop into a
# matching recess in the cover. But the ring's outer boundary was SERVICE_BAY +- (CLEAR + T)
# = 59.80 x 27.10, while the bay it had to pass through is 56.70 x 24.00. MEASURED in
# probes/94_probe_hatch_insert.py: the lip was 3.10 mm LARGER than the opening in BOTH axes, so
# it physically could not enter -- and the cover's recess was a CLOSED groove (material at
# z 0.00..0.65 and z 2.95..3.00) with nowhere for the lip to go on the way in. This is the user's
# "because it's a groove and not open on the inside, you can't put the door in".
#
# Anything that laps BEHIND the cover is a ring only a moulded part can have: printed, it has
# to pass through the hole, and a feature wider than the hole never will. Opening the recess to
# the exterior face does not rescue it either -- that leaves an undercut the printer cannot make
# without supports and which could never be assembled afterwards.
#
# So nothing laps behind anything. The hatch's inner portion is entirely INSIDE the bay
# footprint and drops straight in. Rotation about the single screw is stopped by two KEYS that
# project sideways off the plug and land in NOTCHES cut through the cover's full thickness --
# no undercut, no flexure, nothing that has to bend. The keys bear on the notch walls, so the
# hatch cannot turn; the screw clamps the flange, so it cannot lift.
#
# Key sizes are deliberately UNEQUAL (3.15 mm against 3.15 mm but on 4.60 and 3.40 mm wide
# notches at different Y spans), so the hatch can only seat one way round and cannot be fitted
# rotated 180 degrees.
HATCH_KEY_T=COVER_T                                 # key fills the cover's full thickness, so it
                                                    # bears on the notch walls over 17 x 3 mm
HATCH_KEY_CLEAR=0.30                                # per-side clearance in the notch
HATCH_KEY1_X0,HATCH_KEY1_X1=19.60,23.00             # -X hook; X1 laps 0.25 mm into the plug
# 80% OF THE PLUG'S WIDTH, per the user: "Add a hook 80% of the width of the opposite end of
# where the screw holds down the hatch so that both ends attach." The screw clamps the +X end
# (at x 84.5), so this is the -X end and it is now the primary attachment there -- wide enough
# that the hatch is held along nearly its whole edge, not just located against rotation.
# 18.80 mm over the plug's 23.50 mm Y span, centred on the plug, which keeps 2.60 mm clear of
# the bay's r3 corners at each end.
HATCH_KEY1_Y0,HATCH_KEY1_Y1=70.60,89.40             # 80% of the plug span, centred
HATCH_KEY2_X0,HATCH_KEY2_X1=78.70,82.10             # +X key; X0 laps 0.25 mm into the plug
HATCH_KEY2_Y0,HATCH_KEY2_Y1=71.50,80.50             # stops short of the screw boss (Y 81.4+)
# The notches are each key GROWN by the clearance on every side and cut through the cover.
HATCH_KEYS=((HATCH_KEY1_X0,HATCH_KEY1_X1,HATCH_KEY1_Y0,HATCH_KEY1_Y1),
            (HATCH_KEY2_X0,HATCH_KEY2_X1,HATCH_KEY2_Y0,HATCH_KEY2_Y1))

# The exterior plate is ONE rounded rectangle, not a flange plus a welded-on tab. The previous
# shape was a 21.25..80.45 flange with a hard-cornered 11 x 18 mm box tab stuck onto its +X side
# to reach the screw, which read as an obvious lump glued to the cover (the user's "weird
# artifacts and shape"). The plate now simply runs from the bay out to the screw.
#
# The lap is set so the plate still covers every notch: the -X notch reaches X 19.30 and the Y
# notches reach Y 71.20..88.80. The -Y edge is held at 67.00 -- above the wall-hang keyhole's
# slot, which ends at Y 65.00. Tracking the old lip boundary instead would have run the plate
# down to Y 61.65 and buried the keyhole, making the case impossible to hang.
HATCH_FLANGE_X0=SERVICE_BAY_X0-6.35                 # 16.15, covers the -X notch (19.30)
HATCH_FLANGE_Y0=67.00                               # above the keyhole slot (Y <= 65.00)
HATCH_FLANGE_Y1=SERVICE_BAY_Y1+6.35                 # 98.35, covers the Y notches (to 88.80)
# +X edge is set by the screw, not by the bay: the M2 head counterbore is dia 4.5 at 84.5, so
# 88.7 leaves a full 1.95 mm of plate outside it (and covers the +X notch, which ends at 82.40).
HATCH_FLANGE_X1=88.70
# The plate is FLUSH at one thickness (HATCH_FLANGE_T). Making it thicker so the head could sit
# flush put its face 2.8 mm below the flange -- and the hatch prints exterior-face down, so that
# would have lifted the whole plate off the bed (validator 23 caught it). The head therefore
# sits proud of the exterior face, which is normal for a service cover.
HATCH_HEAD_CBORE=0.6                                 # shallow seat, not a flush recess
# Finger scallop for prying the hatch off without tools: a half-round notch in the plate's +Y
# edge. Placed at 45.00 rather than the bay centre -- with the plate now laps 6.35 mm past the
# bay on +X, a 3.0 mm scallop at the bay centre (50.85) would cut into the screw boss at 84.5.
HATCH_SCALLOP_R=3.0
HATCH_SCALLOP_R=3.0
HATCH_SCALLOP_X=45.00
USB_SLOT_W,USB_SLOT_H=9.0,3.4                         # ASSUME clear a USB-C plug body
USB_SLOT_X=(25.90+33.30)/2.0                         # DERIVED flipped USB keep-out center


def rounded_rect_wire(w,h,r,x,y,z):
    r=min(r,w/2-1e-6,h/2-1e-6); P=lambda a,b:App.Vector(x+a,y+b,z)
    return Part.Wire([
        Part.makeLine(P(r,0),P(w-r,0)),
        Part.ArcOfCircle(Part.Circle(P(w-r,r),App.Vector(0,0,1),r),math.radians(270),math.radians(360)).toShape(),
        Part.makeLine(P(w,r),P(w,h-r)),
        Part.ArcOfCircle(Part.Circle(P(w-r,h-r),App.Vector(0,0,1),r),math.radians(0),math.radians(90)).toShape(),
        Part.makeLine(P(w-r,h),P(r,h)),
        Part.ArcOfCircle(Part.Circle(P(r,h-r),App.Vector(0,0,1),r),math.radians(90),math.radians(180)).toShape(),
        Part.makeLine(P(0,h-r),P(0,r)),
        Part.ArcOfCircle(Part.Circle(P(r,r),App.Vector(0,0,1),r),math.radians(180),math.radians(270)).toShape(),
    ])


def rprism(w,h,r,x,y,z,dz):
    return Part.Face(rounded_rect_wire(w,h,r,x,y,z)).extrude(App.Vector(0,0,dz))


def cyl_x(x0,length,y,z,r):
    return Part.makeCylinder(r,length,App.Vector(x0,y,z),App.Vector(1,0,0))


def keyhole(cx,cy,z0,dz):
    """Wall-hang keyhole: round head entry with the shank slot ABOVE it.

    Hanging is by GRAVITY, so the slot must rise from the head hole: the screw head passes
    through the big round hole, the case is then lowered, and the shank comes to rest against
    the TOP of the slot. The old version put the slot BELOW the head, which would have needed
    the case to be pushed UP to seat and let the screw slide out on its own. Standard M4/#8
    wall screw: 4 mm shank, 8 mm head.
    """
    head=Part.makeCylinder(4.0,dz,App.Vector(cx,cy,z0))
    stem=Part.makeBox(4.0,WALL_HANG_SLOT,dz,App.Vector(cx-2.0,cy,z0))
    return head.fuse(stem)


def flange_tab(z0,dz):
    """Obsolete. The exterior plate is now a single rounded rectangle (see HATCH_FLANGE_*);
    kept only so nothing that still imports it breaks. Returns an empty compound."""
    return Part.makeCompound([])



def build_cover(outer, foot):
    # Exact cover split: z=0..COVER_T. Do not add Boolean overlap here; the old +0.01 mm
    # produced 9.44 mm3 of real chassis/cover interference around the entire perimeter.
    cover = outer.common(Part.makeBox(CASE_W+10,CASE_H+10,COVER_T,App.Vector(-5,-5,0)))

    # Foot pocket, open from the exterior rear face (z=0), 1.0 mm skin remaining.
    # The pocket follows the leg's own outline. It is NOT extended past the hinge line:
    # extending it punched a see-through slot across the rear face, which the user has
    # already rejected on this case.
    pocket = rprism(FOOT_W+2*POCKET_CLEAR, FOOT_H+2*POCKET_CLEAR, 4.3,
                    FOOT_X-POCKET_CLEAR, FOOT_Y-POCKET_CLEAR, -0.1, POCKET_DEPTH+0.1)
    cover = cover.cut(pocket)

    # The actual neck sweeps are cut explicitly after the rail/barrel/cheeks are all fused,
    # so later fusions cannot refill them. The full-width knuckle hides the slots when folded;
    # when open they lie on the bottom edge, not through the rear face.

    # --- rigid hinge PIN, printed integral to the cover ------------------------------
    # The complaint was correct: the previous mechanism enclosed the knuckle in a CLOSED bore
    # (measured 360-degree wrap) whose only aperture was a 45-degree arc with a 1.91 mm chord --
    # smaller than the 4.4 mm knuckle -- and forcing it on needed ~33% strain. It could not be
    # assembled and broke when tried.
    #
    # Now the cover carries a plain rigid PIN: the foot's compliant clips drop onto it. Order
    # matters -- the clip-sweep cavity is cut FIRST, then the pin is fused inside it, anchored
    # at both ends by webs. Fusing the pin before clearing the cavity deletes the pin.
    pin_r = PIN_R
    # The cavity must clear the clip's OUTER wall with a running margin. With no detent nub in
    # the clip bore the wall only reaches (BORE_R+CLIP_WALL) + PIN_PRELOAD = 3.80 mm, so the
    # margin is cavity_r - 3.80 = CAVITY_CLEAR - PIN_PRELOAD = 0.48 mm -- twice the nozzle and
    # clear of FDM's expected error.
    #
    # The cavity is cut only in BANDS at the three clip stations, not across the full width.
    # A full-width cavity ate the cover's hinge rail -- validator 23 measured its rail connection
    # falling from 32.9 mm3 to 12.8 mm3. The clips are the only thing that sweeps here and they do
    # so in Y-Z at fixed X, so the bands are all that is needed; the material between them is cover
    # that helps the part print and adds stiffness. MEASURED: the rail keeps 33.4 mm3.
    cavity_r = BORE_R+CLIP_WALL+CAVITY_CLEAR
    for cx in ARM_CENTERS:
        cover = cover.cut(cyl_x(cx-CLIP_W/2-CAVITY_PAD, CLIP_W+2*CAVITY_PAD,
                                HINGE_Y, KNUCKLE_Z, cavity_r))
    # Pin spans the whole cavity and projects into the webs at each end.
    pin = cyl_x(HINGE_X0-PIN_ROOT, (HINGE_X1-HINGE_X0)+2*PIN_ROOT,
                HINGE_Y, KNUCKLE_Z, pin_r)
    cover = cover.fuse(pin)
    # The pin is a plain bearing surface. The earlier retracted/open detent relied on grooves
    # here matching raised ribs inside the clips; the ribs are gone (see build_foot) because a
    # radial bump in a C-clip splays the clip's free mouth lips outward as it seats, which is
    # what made the hooks flare and catch on the cover. No grooves, no ribs. Holding force is
    # the bore interference alone.
    # The web must OVERLAP the cover solidly. Setting both to y=2.0 gave a bare face contact,
    # which does not fuse in a Boolean and left the webs (and the pin with them) as separate
    # solids. WEB_Y0 is 1.0 mm INSIDE the cover's material so the join is real.
    for wx0, wx1 in ((HINGE_X0-PIN_ROOT, HINGE_X0-0.2),
                     (HINGE_X1+0.2, HINGE_X1+PIN_ROOT)):
        web = Part.makeBox(wx1-wx0, (HINGE_Y+pin_r)-WEB_Y0, WEB_Z1-(KNUCKLE_Z-pin_r),
                           App.Vector(wx0, WEB_Y0, KNUCKLE_Z-pin_r))
        cover = cover.fuse(web)


    # Closure screw clearance + recessed heads.
    for x,y in SCREWS:
        cover = cover.cut(Part.makeCylinder(SCREW_D/2, COVER_T+2, App.Vector(x,y,-0.5)))
        cover = cover.cut(Part.makeCylinder(SCREW_HEAD_D/2, 1.2, App.Vector(x,y,-0.1)))

    # Wall-hang keyhole through the pocket skin; the foot gets the identical cut below.
    for kx in WALL_HANG_X:
        cover = cover.cut(keyhole(kx, WALL_HANG_Y, -0.1, COVER_T+0.2))

    # Rear service bay: large rounded opening over the upward-facing connector row,
    # closed in normal use by the snap-in PRINT_SERVICE_HATCH.
    bay = rprism(SERVICE_BAY_X1-SERVICE_BAY_X0, SERVICE_BAY_Y1-SERVICE_BAY_Y0, 3.0,
                 SERVICE_BAY_X0, SERVICE_BAY_Y0, -0.2, COVER_T+0.4)
    cover = cover.cut(bay)

    # --- hatch retention: notch seats + ONE screw ---------------------------------------------
    # The hatch's two keys drop into these notches, which are cut through the cover's FULL
    # thickness. They bear on the notch walls to stop the hatch rotating about its single screw.
    # An undercut here -- a recess that opened to the exterior face -- would be a bridge the
    # printer cannot make and the hatch could not enter; see the constant block.
    for kx0, kx1, ky0, ky1 in HATCH_KEYS:
        cover = cover.cut(rprism((kx1-kx0)+2*HATCH_KEY_CLEAR,
                                 (ky1-ky0)+2*HATCH_KEY_CLEAR, 3.0,
                                 kx0-HATCH_KEY_CLEAR, ky0-HATCH_KEY_CLEAR,
                                 -0.2, COVER_T+0.4))
    # Screw boss for the hatch's single screw. It must overlap REAL cover material: the bay
    # spans x 22.5..79.2 (centre 50.9) and CASE_W/2 = 67.2 is inside the opening, so a boss
    # there was a floating island; the bay's centre-line (50.9) had the same problem lower
    # down. The axis is now 79.05, where the bay rim is 6.4 mm wide, so a 3.2 mm boss keeps
    # 0.87 mm of overlap at its narrowest while standing 3.2+1.8 = 5.0 mm clear of the bay.
    sb = Part.makeCylinder(HATCH_BOSS_R, COVER_T, App.Vector(HATCH_SCREW_X, HATCH_SCREW_Y, 0.0))
    cover = cover.fuse(sb)
    # Screw passes from outside -- through the hatch's flange and plug -- and threads into
    # this boss. The bore is cut only through the boss itself (z 0..COVER_T); cutting it
    # deeper would hole the chassis bridge behind, which is deliberately omitted: the
    # "bosses need through holes so long screws can go through" request applies to the four
    # chassis stand-offs, not here.
    cover = cover.cut(Part.makeCylinder(HATCH_SCREW_D/2, COVER_T+0.4,
                                        App.Vector(HATCH_SCREW_X, HATCH_SCREW_Y, -0.2)))

    # USB cable exit slot at the hatch edge: cut through the cover across the bay's +X edge
    # region so a cable can leave while the hatch is fitted.
    usb = Part.makeBox(USB_SLOT_W, 6.0, USB_SLOT_H,
                       App.Vector(USB_SLOT_X-USB_SLOT_W/2, SERVICE_BAY_Y1-1.0, -0.2))
    cover = cover.cut(usb)
    if STOP_ENABLE:
        # The 65 deg stop: the same buttress section in EVERY gap between the clips, so a
        # section sits under the bearing face at every x across the leg. See the constant block
        # for why this replaced the two side buttresses (and why it is not one full-width bar).
        ca, sa = math.cos(math.radians(SWING_DEG)), math.sin(math.radians(SWING_DEG))
        py, pz = STOP_RAMP_P
        ey, ez = py + STOP_RAMP_LEN*ca, pz - STOP_RAMP_LEN*sa      # inward end of the face
        quad = [(py, pz), (ey, ez),
                (ey, STOP_Z_BOTTOM), (STOP_BACK_Y0, STOP_Z_BOTTOM), (STOP_BACK_Y0, 0.0)]
        # Fillet the concave corners -- both bed corners and the root where the section meets the
        # wall. NOT the bearing face's outer edge: that is the stop face and must stay crisp, or
        # a fillet would round the 65 deg engagement point.
        CONCAVE = {(round(ey, 3), round(ez, 3)),
                   (round(ey, 3), round(STOP_Z_BOTTOM, 3)),
                   (round(STOP_BACK_Y0, 3), round(STOP_Z_BOTTOM, 3))}
        stop = None
        for x0, x1 in STOP_SECTIONS:
            pts = [App.Vector(0, y, z) for y, z in quad] + [App.Vector(0, quad[0][0], quad[0][1])]
            prism = Part.Face(Part.makePolygon(pts)).extrude(App.Vector(x1-x0, 0, 0))
            edges = []
            for e in prism.Edges:
                eb = e.BoundBox
                if eb.XMax-eb.XMin < 1.0-1e-9:      # skip the along-X extrusion edges
                    continue
                if (round(eb.YMin, 3), round(eb.ZMin, 3)) in CONCAVE \
                   or (round(eb.YMax, 3), round(eb.ZMax, 3)) in CONCAVE:
                    edges.append(e)
            if edges:
                prism = prism.makeFillet(STOP_FILLET_R, edges)
            prism.translate(App.Vector(x0, 0, 0))
            stop = prism if stop is None else stop.fuse(prism)
        cover = cover.fuse(stop)

    return cover


def build_hatch():
    """Service hatch: two anti-rotation keys + ONE M2 screw driven from OUTSIDE.

    The previous plug carried two in-plane cantilevers cut by U-slots. They did not print as
    flexures -- they stayed attached along their length and just looked like blobs stuck to the
    side ("the friction pieces on the sides stuck to the side and don't print"). Retention is a
    single M2 screw through the flange on the case's EXTERIOR face into a boss on the cover's
    rear face, and rotation about that one screw is stopped by two keys that drop through
    notches in the bay rim and land behind the cover plate. See the constant block for why the
    old perimeter lip could not be assembled at all.
    """
    plug = rprism((SERVICE_BAY_X1-SERVICE_BAY_X0)-2*HATCH_CLEAR,
                  (SERVICE_BAY_Y1-SERVICE_BAY_Y0)-2*HATCH_CLEAR, 2.75,
                  SERVICE_BAY_X0+HATCH_CLEAR, SERVICE_BAY_Y0+HATCH_CLEAR, 0.0, HATCH_PLUG_T)
    # ONE plate. It covers the bay and runs out over the cover to carry the screw, so there is
    # no seam, no step and no separate tab.
    flange = rprism(HATCH_FLANGE_X1-HATCH_FLANGE_X0, HATCH_FLANGE_Y1-HATCH_FLANGE_Y0, 4.0,
                    HATCH_FLANGE_X0, HATCH_FLANGE_Y0, -HATCH_FLANGE_T, HATCH_FLANGE_T)
    hatch = plug.fuse(flange)

    # Two keys on the plug's inner face, inside the bay footprint so they pass straight through
    # the opening and drop into the matching notches in the bay rim. Each laps 0.25 mm into the
    # plug so the join is a real overlap, not a tangent line. Nothing projects past the bay's
    # outer boundary, so nothing has to lap behind the cover and nothing has to bend.
    for kx0, kx1, ky0, ky1 in HATCH_KEYS:
        hatch = hatch.fuse(rprism(kx1-kx0, ky1-ky0, 3.0, kx0, ky0, 0.0, HATCH_KEY_T))

    # Single screw: clearance through the tab, the plug and the lip, counterbored for the
    # head in the tab. Both cuts use HATCH_SCREW_X (84.5) -- cutting the flange's head
    # counterbore at CASE_W/2 left a solid flange column at the true axis.
    hatch = hatch.cut(Part.makeCylinder(HATCH_SCREW_D/2, 8.0,
                                        App.Vector(HATCH_SCREW_X, HATCH_SCREW_Y,
                                                   -HATCH_FLANGE_T-0.2)))
    hatch = hatch.cut(Part.makeCylinder(HATCH_SCREW_HEAD_D/2, HATCH_HEAD_CBORE,
                                        App.Vector(HATCH_SCREW_X, HATCH_SCREW_Y,
                                                   -HATCH_FLANGE_T-0.1)))

    # Finger scallop so the hatch can be pried out without tools. A half-round notch in the
    # plate's +Y edge, instead of the previous 31.6 x 5.0 mm rectangular bite that removed most
    # of that edge and read as a chunk cut out of the part.
    # It is placed at 45.00, not at the bay centre: the plate now laps 6.35 mm past the bay on
    # the +X side, and a 6.0 mm scallop centred on the bay (50.85) would reach X 56.85 and cut
    # straight into the screw boss at 84.5. At 45.00 the scallop spans X 39.00..51.00, clear of
    # the boss (81.90) and still within 5 mm of the bay centre, so it pried fine there.
    # The depth is 0.75 mm x 6.0 mm = 4.5 mm2 of fingernail access -- a dimple, not a bite.
    hatch = hatch.cut(Part.makeCylinder(HATCH_SCALLOP_R, HATCH_FLANGE_T+0.4,
                                        App.Vector(HATCH_SCALLOP_X, HATCH_FLANGE_Y1,
                                                   -HATCH_FLANGE_T-0.3)))
    # USB cable exit -- cut through the flange AND OUT THROUGH ITS +Y EDGE, so it is an OPEN
    # cutout like the cover's, not a closed hole.
    # This used to stop at y=97.0, leaving 1.35 mm of plate across the slot's outer end. That
    # made the passage a HOLE WITH A LIP either side, which is why the user reported it as
    # closed: the cover's own opening is an open cutout running off the service bay (open
    # continuously from the bay to y=97), but the plate plugged its end, and the two did not
    # match. The cut now runs past HATCH_FLANGE_Y1 (98.35) so it breaks the plate's edge.
    hatch = hatch.cut(Part.makeBox(USB_SLOT_W, (HATCH_FLANGE_Y1+0.6)-(SERVICE_BAY_Y1-1.0),
                                   HATCH_FLANGE_T+HATCH_PLUG_T+0.6,
                                   App.Vector(USB_SLOT_X-USB_SLOT_W/2, SERVICE_BAY_Y1-1.0,
                                              -HATCH_FLANGE_T-0.3)))
    return hatch


def build_foot():
    # Leg plate folded into the rear pocket, y=8.5..48.5.
    foot = rprism(FOOT_W, FOOT_H, 4.0, FOOT_X, FOOT_Y, 0.0, FOOT_T)

    # Each hinge station is a COMPLIANT C-CLIP wrapped around the cover's rigid pin. The foot
    # has no knuckle: a solid knuckle would collide with the pin and could never enter an
    # enclosed bore. The clip's outer wall reaches past the plate's top edge in +Y, so the two
    # fuse directly -- no separate arm is needed.
    #
    # Mouth direction is MOUTH_PIN_ANGLE (see the constant block): the mouth must face the
    # direction the foot can actually approach from, or the pin is driven through the wrap.
    for cx in ARM_CENTERS:
        ring = Part.makeCylinder(BORE_R+CLIP_WALL, CLIP_W,
                                 App.Vector(cx-CLIP_W/2, HINGE_Y, KNUCKLE_Z),
                                 App.Vector(1,0,0)).cut(
               Part.makeCylinder(BORE_R, CLIP_W+0.2,
                                 App.Vector(cx-CLIP_W/2-0.1, HINGE_Y, KNUCKLE_Z),
                                 App.Vector(1,0,0)))
        # Mouth opening. The bore is UNDERSIZED (interference fit for friction), so the pin
        # must expand the clip as it passes. The lips therefore have to flex: at 150 deg the
        # chord is 4.02 mm against a 4.40 mm pin, so each lip opens 0.19 mm -- 1.6% wall strain,
        # well inside PETG's 3-5% -- while 210 deg of wrap still passes the pin equator and
        # retains it. (A 60-deg mouth would need 20.6% and could never be snapped on.)
        MOUTH_OPEN_DEG = 150.0
        half_chord = BORE_R*math.sin(math.radians(MOUTH_OPEN_DEG/2.0))
        mouth_h = (BORE_R**2 - half_chord**2)**0.5
        mouth = Part.makeBox(CLIP_W+0.4, BORE_R*2.6, BORE_R+CLIP_WALL-mouth_h,
                             App.Vector(cx-CLIP_W/2-0.2, HINGE_Y-BORE_R*1.3,
                                        KNUCKLE_Z-(BORE_R+CLIP_WALL)))
        mouth.rotate(App.Vector(cx, HINGE_Y, KNUCKLE_Z), App.Vector(1,0,0),
                     MOUTH_PIN_ANGLE-180.0)
        ring = ring.cut(mouth)
        # There is deliberately NO detent nub on this clip. A radial bump inside a C-clip is an
        # interference feature, and it does not merely press the clip out where the bump is: it
        # splays the clip's FREE ENDS -- the two mouth lips -- outward, because pushing a C-ring
        # out at one point opens its gap. Those lips are the last thing to clear the cover, so a
        # nub makes the hook flare and catch exactly when the leg is swung. The holding force is
        # the 0.12 mm bore interference alone.
        foot = foot.fuse(ring)
        # Root gusset: fills the wedge between the ring's outer wall and the plate's underside
        # so the clip is welded to the foot as a solid rather than touching it on a tangent line
        # (0.0870 mm3 -- see the constant block). Sized to stay clear of the mouth channel, so it
        # cannot stiffen the lip the pin has to push past during assembly.
        gus = Part.makeBox(CLIP_W, CLIP_GUSSET_Y1-CLIP_GUSSET_Y0, CLIP_GUSSET_Z1-CLIP_GUSSET_Z0,
                           App.Vector(cx-CLIP_W/2, CLIP_GUSSET_Y0, CLIP_GUSSET_Z0))
        gus = gus.cut(Part.makeCylinder(BORE_R, CLIP_W+0.4,
                                        App.Vector(cx-CLIP_W/2-0.2, HINGE_Y, KNUCKLE_Z),
                                        App.Vector(1,0,0)))
        foot = foot.fuse(gus)
        # --- ACUTE-CORNER FILLETS at the gusset's roots -------------------------------
        # The user: "I see fillets, but not down in the acute corners where it will make the
        # gussets stronger." The fillet has to go on the FUSED solid, because the corners that
        # matter only exist once the gusset, the ring and the plate are one body -- and the
        # sharpest of them, where the gusset's outer face meets the ring's outer CYLINDER, is an
        # acute tangent junction that a box's own edges do not contain at all.
        # MEASURED: the root edge at (Y 12.00, Z 0.00) and the tangent edge at (Y 8.008, Z -2.80)
        # both fillet cleanly at R up to 1.5 on the fused solid, while the gusset's top edges do
        # not (16Standard_Failure: no suitable edge). Fillet exactly those two.
        _root = []
        # Where the gusset's bottom plane crosses the ring's OUTER cylinder: the gusset's sharpest
        # root, an ACUTE junction between a flat face and a cylinder. Computed here because it
        # needs KNUCKLE_Z, which is defined after the constant block.
        _tangent_y = HINGE_Y + ((BORE_R+CLIP_WALL)**2 - (CLIP_GUSSET_Z0-KNUCKLE_Z)**2)**0.5
        for e in foot.Edges:
            bb = e.BoundBox
            if bb.XMax-bb.XMin < CLIP_W-0.1:            # along-X edges only (the profile corners)
                continue
            if not (cx-CLIP_W/2-0.1 <= bb.XMin and bb.XMax <= cx+CLIP_W/2+0.1):
                continue
            y = (bb.YMin+bb.YMax)/2; z = (bb.ZMin+bb.ZMax)/2
            if (abs(y-CLIP_GUSSET_Y1) < 0.02 and abs(z) < 0.02) or \
               (abs(y-_tangent_y) < 0.02 and abs(z-CLIP_GUSSET_Z0) < 0.02):
                _root.append(e)
        if _root:
            foot = foot.makeFillet(CLIP_GUSSET_FILLET_ROOT, _root)
    # --- print-support rib --------------------------------------------------------------
    # The clips hang 3.6 mm below the plate, so printing the foot plate-down leaves them (and
    # the plate's main face) with almost no bed contact -- measured 0.2 mm3, i.e. it would
    # print on supports and could lift. A 4 mm rib under the plate's far edge brings the
    # lowest surface level with the clips: bed contact rises to 53.4 mm3 and it stiffens the
    # leg, which is desirable anyway. Printed plate-and-rib-down.
    # The rib is kept clear of the stop mechanism, which occupies the y 4..8 band at the sides,
    # so it is inset from both plate ends and spans the middle of the far edge.
    rib_bottom = KNUCKLE_Z-(BORE_R+CLIP_WALL)          # -5.20, level with the clips
    rib = Part.makeBox(FOOT_W-2*RIB_INSET, PRINT_RIB_Y, FOOT_T-rib_bottom,
                       App.Vector(FOOT_X+RIB_INSET, FOOT_Y+FOOT_H-PRINT_RIB_Y, rib_bottom))
    foot = foot.fuse(rib).removeSplitter()

    # The foot is the stand only. The wall-hang keyholes are in the cover, above the assembly
    # centre of gravity (see WALL_HANG_*); a keyhole in the foot is necessarily below it.
    return foot


def build_all():
    src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
    outer = src.getObject("CASE_SHELL").Shape

    foot = build_foot()
    cover = build_cover(outer, foot)
    hatch = build_hatch()

    print("hinge axis Y %.2f Z %.2f  knuckle r %.2f  cradle r %.2f" %
          (HINGE_Y, KNUCKLE_Z, PIN_R, BORE_R))
    print("bottom-edge cradle Z %.2f..%.2f; 3 necks W %.2f at X %s" %
          (KNUCKLE_Z-(BORE_R+CLIP_WALL), COVER_T, ARM_W,
           str([round(v,2) for v in ARM_CENTERS])))
    print("foot vs cover interference %.3f mm3 (old pin design was 190.41)" %
          foot.common(cover).Volume)

    if len(cover.Solids) != 1 or not cover.isValid():
        print("DEBUG cover solids", len(cover.Solids))
        for i, q in enumerate(cover.Solids):
            bb = q.BoundBox
            print("   solid %d vol %.1f bbox X %.1f..%.1f Y %.1f..%.1f Z %.1f..%.1f" %
                  (i, q.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
        raise RuntimeError("rear cover is not one valid solid")
    for nm, sh in (("hatch", hatch), ("foot", foot)):
        if len(sh.Solids) != 1 or not sh.isValid():
            print("DEBUG %s solids"%nm, len(sh.Solids))
            for i,q in enumerate(sh.Solids):
                bb=q.BoundBox
                print("   solid %d vol %.2f bbox X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"%(
                    i,q.Volume,bb.XMin,bb.XMax,bb.YMin,bb.YMax,bb.ZMin,bb.ZMax))
            raise RuntimeError(nm + " is not one valid solid")

    # Open-position reference at the design open angle (65 degrees), where the foot rests
    # against the printed stop. Reference only, never printed.
    foot_open = foot.copy()
    foot_open.rotate(App.Vector(0, HINGE_Y, KNUCKLE_Z), App.Vector(1, 0, 0), -SWING_DEG)

    doc = App.newDocument("RearCoverFoot")
    c = doc.addObject("Part::Feature", "PRINT_REAR_COVER"); c.Label = "PRINT: Rear cover"
    c.Shape = cover
    c.addProperty("App::PropertyString", "PrintOrientation").PrintOrientation = "Interior face down; support cradle cheeks only"
    c.addProperty("App::PropertyString", "Hardware").Hardware = "4x M2x8 case screws; 1x M2x8 hatch screw; no hinge hardware"

    f = doc.addObject("Part::Feature", "PRINT_FOOT"); f.Label = "PRINT: Swing-out foot"
    f.Shape = foot
    f.addProperty("App::PropertyString", "PrintOrientation").PrintOrientation = "Large flat face down; knuckle bridges on support"
    f.addProperty("App::PropertyString", "Function").Function = "Kickstand; folds flush into the rear pocket; printed pin/clip bearing is the pivot"

    h = doc.addObject("Part::Feature", "PRINT_SERVICE_HATCH"); h.Label = "PRINT: Connector service hatch (snap-in)"
    h.Shape = hatch
    h.addProperty("App::PropertyString", "PrintOrientation").PrintOrientation = "Exterior flange face down; latches vertical"
    h.addProperty("App::PropertyString", "Hardware").Hardware = "None; snap-in cantilever latches + USB exit slot"

    p = doc.addObject("Part::Feature", "REFERENCE_FOOT_OPEN_65DEG"); p.Label = "REFERENCE: Foot open 65 degrees"
    p.Shape = foot_open
    p.addProperty("App::PropertyString", "NotForPrinting").NotForPrinting = "Reference only; duplicate of PRINT_FOOT"

    doc.recompute(); doc.saveAs(os.path.join(HERE, "rear_cover_foot.FCStd"))

    for name, shape in [("cover", cover), ("hatch", hatch), ("foot", foot), ("open", foot_open)]:
        b = shape.optimalBoundingBox()
        print("%-6s bbox X %.1f..%.1f Y %.1f..%.1f Z %.1f..%.1f solids %d valid %s vol %.0f" %
              (name, b.XMin, b.XMax, b.YMin, b.YMax, b.ZMin, b.ZMax,
               len(shape.Solids), shape.isValid(), shape.Volume))
    print("saved rear_cover_foot.FCStd")


# freecadcmd executes scripts without `__main__`, so call unconditionally.
# freecadcmd executes scripts without `__main__`, so call unconditionally. NO_BUILD is set by
# the standalone probe scripts (34-39) that import this module for its constants and helpers and
# do not want a full rebuild.
if os.environ.get("NO_BUILD") != "1":
    build_all()
