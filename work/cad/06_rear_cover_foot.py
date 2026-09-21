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
# ONE BEARING, NOT THREE CLIPS. The user: "replace the 3 hooks you have and just make it a solid
# single piece that is centered and covers most of the length." Three separate clips put three
# sets of stress concentrations into a part that is loaded every time the stand is used; one
# wide bearing has one continuous bearing surface, a far larger weld to the plate, and -- the
# reason it also simplifies the STOP -- it leaves the two ends of the hinge span FREE, so the
# stop can be the big solid blocks it needs to be instead of thin sections squeezed into the
# gaps between clips.
BEARING_X0, BEARING_X1 = CASE_W/2-25.0, CASE_W/2+25.0   # 42.2..92.2, 50 mm of the 70 mm span
BEARING_W = BEARING_X1-BEARING_X0
CLIP_W = BEARING_W                                  # the bearing's axial width
# CLIP_WALL is DERIVED at KNUCKLE_Z below (it must exactly fill the gap from BORE_R up to the
# cover split plane). Do not define it here -- a duplicate is what let two validators silently
# read a stale value.
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
# ---- END WEBS: the pin's only attachment ---------------------------------------
# The user: "Why is the hinge pin attachment so narrow? There's extra room to make them wider"
# They are right, and the numbers say so. The pin is 78.00 mm long (28.20..106.20) but was
# anchored by two webs only 3.80 mm wide (28.20..32.00 / 102.40..106.20). MEASURED on the fused
# cover by sampling the pin's own circumference: the weld wraps 81.9% of the pin only over those
# two 3.80 mm bands, drops to 25.0% from x 33.0 to 42.0 (and 92.8 to 102.4), and is 0.0% over
# the whole 42..92 span, which is the clip-sweep cavity. So 10.20 mm of pin at EACH end hangs
# unsupported between the web and the bearing.
#
# The room was there: sweeping the foot through its entire working range (0..90 deg) against the
# candidate web box (y 1.00..7.20, z -3.25..3.00) shows ZERO overlap out to x=42.20, i.e. the
# web can run right up to the cavity wall. It only collides at x=43.0 (14.39 mm3 at 90 deg),
# past the cavity, and only in +Y past y=8.0 (0.0138 mm3) and in -Z past z=-4.0.
# MEASURED gain: the fused web goes from 147.3 mm3 to 326.7 mm3 per end -- 2.2x the material --
# and the unsupported pin span falls from 10.20 mm to 0.60 mm per side.
# The webs stop at the cavity wall (BEARING_X0 - CAVITY_PAD) so they meet the sweep cavity
# exactly rather than intruding into it; a web reaching past that would sit in the band the
# bearing sweeps through. PIN_WEB_X itself is derived where CAVITY_PAD is defined, below.
# ---- OPEN-POSITION DETENT (re-added 2026-09-20, a GROOVE in the pin) ---------------------
# See the long note at the pin build and [[project-kickstand-statics]]. Summary: nothing but a
# bore detent can hold the leg shut, and the feature must be on the RIGID PIN because the clip's
# material spans 272 deg of the pin at every angle, leaving no free end to ride a rib.
#
# A GROOVE, not a ridge. A ridge on the pin makes the lip climb a HILL, and a hill's top is an
# unstable resting place -- measuring one showed the resistance rising smoothly past the open
# angle with no local minimum, i.e. it would push the leg off the open position either way. A
# groove is a VALLEY the lip settles into: closing (or opening further) requires climbing out
# over its flank, which is the hold.
DETENT_GRV_D = 0.40                                 # radial depth of the scallop
DETENT_GRV_W = CLIP_W * 1.30                        # ~5.85 mm along the pin: WIDER than the clip
                                                    # band, so the lip is fully inside the
                                                    # groove at the open angle rather than
                                                    # perched on an edge. Not a thin flange --
                                                    # this is the pin's whole surface removed
                                                    # over a 5.85 mm length.
DETENT_GRV_ARC = 50.0                               # sweep; depth falls to zero at both ends,
                                                    # so the lip ramps in and out with no step
# Contact angle, in pin-angle degrees (0=+Z, 90=+Y, 180=-Z, 270=-Y). The clip's leading edge
# sweeps to 109 deg at 65 deg open (MEASURED, see the band table in the probes), so the groove is
# centred where the lip sits AT the deployed angle.
DETENT_PIN_ANGLE = 140.0
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
# The pin's end webs run from the pin's own end right up to the cavity wall, so the pin is
# anchored almost over its whole length (see the END WEBS note at PIN_ROOT for the measurements).
PIN_WEB_X = ((HINGE_X0-PIN_ROOT, BEARING_X0-CAVITY_PAD),
             (BEARING_X1+CAVITY_PAD, HINGE_X1+PIN_ROOT))
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
# THE STOP IS NOW TWO SOLID BLOCKS AT THE ENDS OF THE HINGE SPAN.
# The user: "the stops you put on the hinge pin are way too tiny and will just break. Stop it!!"
# They are right about every previous version, and the reason each was small is worth recording
# because it is what finally forced the bearing redesign above. The old stop had to be squeezed
# into the gaps BETWEEN three clips: anywhere under a clip it would collide with the clip's
# mouth as the leg swung (MEASURED: the mouth sweeps through pin-angle 40..105, which is exactly
# the sector a stop under the leg has to occupy). So it was always a thin section in whatever
# sliver was left over -- 0.28 mm3 of bearing contact on the original, 2.85 mm3 of weld.
#
# With ONE centred bearing (42.2..92.2) the ends of the hinge span are simply EMPTY: from
# x 32.2..42.2 and 92.2..102.2, ten millimetres at each end, there is no clip to collide with.
# The stop goes there as a solid block -- ten millimetres wide against the old 3.4, and merging
# into the end webs (28.20..31.99 / 102.41..106.19), which is the load path back into the cover.
# MEASURED: no contact with the bearing at any angle; the blocks are clear of the whole sweep.
STOP_SECTIONS=((28.00, 42.20), (92.20, 106.40))   # full free ends, merged into both
                                                 # end webs (28.20..31.99 / 102.41..106.19)
# Bearing-face line. RE-DERIVED 2026-09-20 from the leg's REAL solid at 65 deg, not by
# rotating the plate's sharp corner: the plate is an r4.0 rounded prism, so its nominal corner
# (8.50,0.00) is CUT AWAY and the material there begins ~1.2 mm inward. Rotating the sharp
# corner therefore put the face 0.85 mm outside the leg (onset late at 66 deg, and the earlier
# (7.10,-3.56) was left over from the old -0.68 axis besides). MEASURED off the rotated solid,
# the leg's bearing surface at 65 deg is a convex corner whose 65 deg TANGENT passes through
# (8.00,-4.25); that line reaches the cover floor (z -2.88) at (7.361,-2.88) and the plate's
# outer edge at (8.50,-5.32).
STOP_RAMP_P=(7.089,-3.007)                          # inner end of the face
STOP_RAMP_LEN=3.57   # reaches the plate's outer edge; max that still assembles                                  # outward along the 65 deg direction
STOP_RAMP_T=3.0                                     # material behind the face
STOP_BACK_Y0=3.20                                   # section back face, inside the cover
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
HATCH_FLANGE_T=1.2                                    # the plate's own thickness
# ---- RECESSED SEAT (2026-09-20) --------------------------------------------------------
# The user: "Make the hatch cover recessed in the rear cover instead of sitting on top. Make
# sure there's still a lip on the rear cover to support it."
# The plate used to sit PROUD: it spanned z -1.2..0, standing 1.2 mm off the cover's rear face
# at z=0. It now drops INTO a seat cut in that face, so its exterior face finishes flush.
# WHAT SUPPORTS IT is the cover left UNDER the seat -- the LIP. The cover is COVER_T = 3.0 and
# the seat is HATCH_RECESS_T = 1.2 deep, so a 1.8 mm lip remains all the way round the bay
# (1.8 mm clears the 1.6 mm FDM floor by 0.2). Outside the plate's outline the cover keeps its
# full 3.0 mm, so the step down into the seat is a 1.2 mm rim: rim above, lip below.
# The seat is cut HATCH_RECESS_CLEAR wider than the plate on every side, so the plate drops in
# without binding and prints no tighter than the bay already does.
HATCH_RECESS_T=HATCH_FLANGE_T                         # seat depth; = plate thickness -> flush
HATCH_RECESS_CLEAR=0.25                               # per-side, so the plate drops in freely
# The plug fills the bay from the plate's INNER face (z = HATCH_FLANGE_T) up to the cover's
# inner face (z = COVER_T), so it is 1.8 mm rather than the full 3.0 it was when the plate was
# proud. That still beats the 1.4 mm plug once rejected here, and rotation is stopped by the two
# KEYS, not by plug depth. Letting it run the full 3.0 from the plate's new inner face would push
# it to z 4.2 -- through the cover's inner face and into the chassis at z 2.95.
HATCH_PLUG_Z0=HATCH_FLANGE_T                          # 1.2, the plate's inner face
HATCH_PLUG_T=COVER_T-HATCH_FLANGE_T                   # 1.8, reaches the cover's inner face
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
# ---- -X RETENTION LIP (2026-09-20) ------------------------------------------------------
# The user: "still needs a lip that goes on the INSIDE of the rear cover on the opposite edge
# from where the screw attaches. Do it." The screw clamps the +X end only; this is the -X end.
# The lip must reach PAST the key-1 notch to bite under SOLID cover at all (the notch is open
# through the cover at x 19.30..23.30). MEASURED: cover material at the -X rim is x 12.0..22.5
# for z 0..3.0, and NOTHING is behind it (chassis 0.000 mm3 in x 16..23, y 71..89, z 2.8..6.5),
# so there is ample solid cover to bite.
# MEASURED on the built cover: solid material at the notch line ends at x=19.5, so the lip's
# tip must reach x < 19.3 (the notch's own edge) to bite under the rim rather than sit in the
# notch void. 1.4 mm of engagement under the rim.
HATCH_LIP_X0 = 17.90                                # outer tip, under the solid rim
HATCH_LIP_X1 = 22.50                                # bay wall; the root laps onto the plug
HATCH_LIP_T = 1.50                                  # thick: installed by tilt, never flexed
HATCH_LIP_Z0 = 3.00                                 # sits on the cover's inside face
# NARROWER THAN THE HATCH EDGE. User: "The inner hookin lip needs to be narrower than the edge
# on the hatch." The hatch's inner portion spans Y 68.25..91.75 (23.50 mm). The lip is inset
# HATCH_LIP_INSET per side, so it is clearly a narrower tongue on the edge rather than a
# continuation of the full edge -- and it stays well inside the bay's r3 corners. Kept simple:
# a plain rectangle, no tip shaping.
HATCH_LIP_INSET = 3.00                              # per side -> 17.50 mm wide vs the 23.50 mm edge
HATCH_LIP_Y0 = 68.25 + HATCH_LIP_INSET              # 71.25
HATCH_LIP_Y1 = 91.75 - HATCH_LIP_INSET              # 88.75
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
# The head still sits PROUD of the plate's exterior face, which is normal for a service cover.
# An earlier attempt to thicken the plate so the head sat flush put its face 2.8 mm below the
# flange -- and the hatch prints exterior-face down, so that lifted the whole plate off the bed
# (validator 23 caught it). Nothing changes now that the plate is recessed: the plate keeps ONE
# thickness, and the head stands in the void above it inside the recess.
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


def _detent_cutter():
    """Cutter for one open-position detent GROOVE in the pin.

    A groove, not a bump. As the leg opens, the clip's leading lip sweeps along the pin; a ridge
    on the pin makes the lip climb a HILL, and the top of a hill is an unstable resting place --
    the detent would push the leg off the open position in whichever direction it was leaning.
    A groove is a VALLEY: the lip settles into it at the open angle, and to close (or to open
    further) the lip must climb out over the groove's flank, which is the holding force.

    Profile: a smooth scallop whose depth is DETENT_GRV_D at the centre and falls to zero at both
    ends of DETENT_GRV_ARC, so there is no step and no stress riser. Cut into the RIGID pin, so
    nothing on the compliant clip is asked to flex over a feature -- which is what made the old
    bore ribs splay the clip's free lips.
    """
    n = 24
    a0 = math.radians(-DETENT_GRV_ARC / 2.0)
    a1 = math.radians(DETENT_GRV_ARC / 2.0)
    inner, outer = [], []
    for i in range(n + 1):
        t = i / n
        a = a0 + (a1 - a0) * t
        depth = DETENT_GRV_D * math.sin(math.pi * t)      # zero at both ends, max in the middle
        ri = PIN_R - depth
        inner.append((ri * math.cos(a), ri * math.sin(a)))
        outer.append(((PIN_R + 1.0) * math.cos(a), (PIN_R + 1.0) * math.sin(a)))
    pts = inner + list(reversed(outer))
    wire = Part.makePolygon([App.Vector(0.0, u, v) for u, v in pts] +
                            [App.Vector(0.0, pts[0][0], pts[0][1])])
    # Profile in the Y-Z plane; extrude along X so the groove runs the clip's full width.
    return Part.Face(wire).extrude(App.Vector(DETENT_GRV_W, 0, 0))


def _add_open_detent(cover):
    """Cut one detent groove per clip station into the pin, at the deployed contact angle."""
    for cx in ARM_CENTERS:
        cutter = _detent_cutter()
        # ANGLE CONVENTION: _detent_cutter builds its profile with the angle measured from +Y
        # toward +Z (a point at cutter-angle 0 lies on +Y); DETENT_PIN_ANGLE and the clip band
        # are measured from +Z toward +Y, i.e. psi = 90 - a. MEASURED the hard way: rotating by
        # +109 directly put the groove at psi 341, because the two conventions differ.
        cutter.rotate(App.Vector(0.0, 0.0, 0.0), App.Vector(1, 0, 0), 90.0 - DETENT_PIN_ANGLE)
        cutter.translate(App.Vector(cx - DETENT_GRV_W / 2.0, HINGE_Y, KNUCKLE_Z))
        cover = cover.cut(cutter)
    return cover


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
    # ONE cavity band, matching the one bearing (it was three bands at the three clips). The
    # cavity must follow the bearing's own extent, or the bearing sweeps through cover material
    # at the stations where the cavity is missing -- and the stop blocks now occupy exactly the
    # span either side of it, so a stale three-band cavity would also eat the new stop.
    cover = cover.cut(cyl_x(BEARING_X0-CAVITY_PAD, BEARING_W+2*CAVITY_PAD,
                            HINGE_Y, KNUCKLE_Z, cavity_r))
    # Pin spans the whole cavity and projects into the webs at each end.
    pin = cyl_x(HINGE_X0-PIN_ROOT, (HINGE_X1-HINGE_X0)+2*PIN_ROOT,
                HINGE_Y, KNUCKLE_Z, pin_r)
    cover = cover.fuse(pin)
    # ---- RE-ADDED: OPEN-POSITION DETENT (2026-09-20) --------------------------------
    # The user: "Add an open-position detent, but it must be ROBUST, no weak, breaking features
    # like you've been doing." And the measurement that forces it to live HERE: the leg can only
    # be held shut by friction or by a detent that protrudes into the bore (see PIN_PRELOAD and
    # [[project-kickstand-statics]]) -- no stop can reach it, because the leg's material moves
    # AWAY from the cover as it closes.
    #
    # WHY A PIN LOBE AND NOT A CLIP RIB. This is the fix for the failure that got the nubs
    # removed. Measured: the clip's material spans 272 deg of the pin at every open angle
    # (e.g. 65 deg open is 109..360..21), so at 65 deg there is NO free clip end anywhere to
    # ride over a rib. A rib inside the clip could therefore only act in the mouth's small
    # window, and there it pried the two free mouth LIPS apart -- the splay the user saw. The
    # lobe is instead cut into the RIGID pin, so the lip rides over it as a smooth ramp, and the
    # only material that flexes is the same thin lip that already passes the pin on assembly.
    #
    # RADIAL ROOM, and why this is not a thin feature: the lobe grows into the CAVITY, not into
    # the clip or through it. Bore wall at BORE_R = 2.02, clip outer wall at BORE_R+CLIP_WALL =
    # 4.05, cavity at BORE_R+CLIP_WALL+CAVITY_CLEAR = 4.65. So the lobe has 1.85 mm of radial
    # room before it even reaches the clip's outer wall, and it is a solid body of revolution
    # fused to the pin -- not a cantilever, not a flange, nothing that can snap off.
    # _add_open_detent(cover)  # DISABLED -- see the DETENT note: proved not to work.
    # The pin is a plain bearing surface. The earlier retracted/open detent relied on grooves
    # here matching raised ribs inside the clips; the ribs are gone (see build_foot) because a
    # radial bump in a C-clip splays the clip's free mouth lips outward as it seats, which is
    # what made the hooks flare and catch on the cover. No grooves, no ribs. Holding force is
    # the bore interference alone.
    # The web must OVERLAP the cover solidly. Setting both to y=2.0 gave a bare face contact,
    # which does not fuse in a Boolean and left the webs (and the pin with them) as separate
    # solids. WEB_Y0 is 1.0 mm INSIDE the cover's material so the join is real.
    for wx0, wx1 in PIN_WEB_X:
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

    # --- RECESSED SEAT for the hatch plate --------------------------------------------
    # Cut from the EXTERIOR face (z=0) down HATCH_RECESS_T, so the plate's outer face finishes
    # flush with the cover's instead of standing 1.2 mm proud of it (the user: "recessed in the
    # rear cover instead of sitting on top"). Only HATCH_RECESS_T of the 3.0 mm cover is removed:
    # the 1.8 mm left beneath it is the LIP the hatch rests on, and it runs all the way round the
    # bay because the seat cut is HATCH_RECESS_CLEAR larger than the plate on every side.
    # Cut AFTER the bay so the two openings join into one profile, and cut at the plate's own
    # outline rather than the bay's -- the plate laps past the bay to reach the screw, and the
    # seat has to follow the plate or the screw end would sit on un-recessed cover.
    seat = rprism((HATCH_FLANGE_X1-HATCH_FLANGE_X0)+2*HATCH_RECESS_CLEAR,
                  (HATCH_FLANGE_Y1-HATCH_FLANGE_Y0)+2*HATCH_RECESS_CLEAR, 4.0,
                  HATCH_FLANGE_X0-HATCH_RECESS_CLEAR, HATCH_FLANGE_Y0-HATCH_RECESS_CLEAR,
                  -0.1, HATCH_RECESS_T+0.1)
    cover = cover.cut(seat)

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
    # It starts at the SEAT FLOOR (z = HATCH_RECESS_T), not at z=0: the recess is cut over this
    # whole area, and a boss running from z=0 would refill it and stand through the plate
    # (MEASURED: 14.87 mm3 of that). The boss is simply the lip thickened locally.
    sb = Part.makeCylinder(HATCH_BOSS_R, COVER_T-HATCH_RECESS_T,
                           App.Vector(HATCH_SCREW_X, HATCH_SCREW_Y, HATCH_RECESS_T))
    cover = cover.fuse(sb)
    # Screw passes from outside -- through the hatch's flange and plug -- and threads into
    # this boss. The bore is cut only through the boss itself (z 1.2..COVER_T); cutting it
    # deeper would hole the chassis bridge behind, which is deliberately omitted: the
    # "bosses need through holes so long screws can go through" request applies to the four
    # chassis stand-offs, not here.
    cover = cover.cut(Part.makeCylinder(HATCH_SCREW_D/2, COVER_T-HATCH_RECESS_T+0.4,
                                        App.Vector(HATCH_SCREW_X, HATCH_SCREW_Y,
                                                   HATCH_RECESS_T-0.2)))

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
                  SERVICE_BAY_X0+HATCH_CLEAR, SERVICE_BAY_Y0+HATCH_CLEAR,
                  HATCH_PLUG_Z0, HATCH_PLUG_T)
    # ONE plate. It covers the bay and runs out over the cover to carry the screw, so there is
    # no seam, no step and no separate tab.
    # The plate now sits INSIDE the cover: z 0..HATCH_FLANGE_T, its exterior face flush with the
    # cover's rear face at z=0 (it used to occupy z -1.2..0, standing proud by its own thickness).
    flange = rprism(HATCH_FLANGE_X1-HATCH_FLANGE_X0, HATCH_FLANGE_Y1-HATCH_FLANGE_Y0, 4.0,
                    HATCH_FLANGE_X0, HATCH_FLANGE_Y0, 0.0, HATCH_FLANGE_T)
    hatch = plug.fuse(flange)

    # Two keys on the plug's inner face, inside the bay footprint so they pass straight through
    # the opening and drop into the matching notches in the bay rim. Each laps 0.25 mm into the
    # plug so the join is a real overlap, not a tangent line. Nothing projects past the bay's
    # outer boundary, so nothing has to lap behind the cover and nothing has to bend.
    for kx0, kx1, ky0, ky1 in HATCH_KEYS:
        hatch = hatch.fuse(rprism(kx1-kx0, ky1-ky0, 3.0, kx0, ky0, 0.0, HATCH_KEY_T))
    # ---- -X RETENTION LIP: BITES UNDER THE COVER'S INSIDE FACE ----------------------
    # The user: "still needs a lip that goes on the INSIDE of the rear cover on the opposite
    # edge from where the screw attaches." The screw clamps the +X end only; this is the -X end,
    # and this lip is what holds that end down.
    #
    # INSTALLED BY TILT, so it can be THICK and RIGID. Its tip ends up under the cover's inside
    # face (x < 22.5, z 3.0..4.5) but has to reach there through the opening. Two ways:
    #   (a) bend sideways by its own engagement -- MEASURED: a 2 mm block needs 37.5% strain to
    #       move 0.5 mm on a 4 mm flexure length, far past PETG's 5% limit. A rigid block cannot
    #       do this, and this is exactly why the ORIGINAL perimeter lip could never be assembled;
    #   (b) TILT the hatch in -- hook the -X lip under the cover first with the hatch angled up a
    #       couple of degrees, then swing the +X end down and drive the screw. NO bending at all.
    # (b) is how a lipped cover normally goes on, and it lets the lip be a solid block with a
    # chamfered lead-in: thick, and nothing that can snap.
    # A plain rectangle: no tip chamfer, no shaping (user: "get rid of the weird shapes on the
    # tip, no need"). The corner radius just matches the rounded-rect convention used elsewhere.
    lip = rprism(HATCH_LIP_X1-HATCH_LIP_X0, HATCH_LIP_Y1-HATCH_LIP_Y0, 1.2,
                 HATCH_LIP_X0, HATCH_LIP_Y0, HATCH_LIP_Z0, HATCH_LIP_T)
    hatch = hatch.fuse(lip)

    # Single screw: clearance through the flange, the plug and the lip, counterbored for the
    # head in the flange. Both cuts use HATCH_SCREW_X (84.5) -- cutting the flange's head
    # counterbore at CASE_W/2 left a solid flange column at the true axis.
    # The z references all rose by HATCH_FLANGE_T when the plate went from z -1.2..0 to 0..1.2:
    # the head now sits in the recess (z -0.1..0 is the void above the plate), so the counterbore
    # is cut from z=-0.1 down and the shank bore from the plate's new outer face.
    hatch = hatch.cut(Part.makeCylinder(HATCH_SCREW_D/2, 8.0,
                                        App.Vector(HATCH_SCREW_X, HATCH_SCREW_Y, -0.2)))
    hatch = hatch.cut(Part.makeCylinder(HATCH_SCREW_HEAD_D/2, HATCH_HEAD_CBORE,
                                        App.Vector(HATCH_SCREW_X, HATCH_SCREW_Y, -0.1)))

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
                                                   -0.3)))
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
                                              -0.3)))
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
    # ONE solid bearing, centred, covering most of the hinge span.
    #
    # The user: "replace the 3 hooks you have and just make it a solid single piece that is
    # centered and covers most of the length." Three clips gave three stress concentrations and
    # three chances to crack; this is a single continuous C-bearing with one bearing surface and
    # one large weld to the plate. It also frees the ENDS of the hinge span, which is what lets
    # the stop be a solid block instead of a thin section squeezed between clips.
    #
    # It is still a C, not a closed ring: the mouth must face the direction the foot approaches
    # from (MOUTH_PIN_ANGLE), or the pin is driven through the wrap. The bore is undersized
    # (interference fit for the friction that holds the leg), so the pin expands it as it passes
    # and the wrap must be open enough to let that happen without over-straining PETG.
    cx = (BEARING_X0+BEARING_X1)/2.0
    ring = Part.makeCylinder(BORE_R+CLIP_WALL, BEARING_W,
                             App.Vector(BEARING_X0, HINGE_Y, KNUCKLE_Z),
                             App.Vector(1,0,0)).cut(
           Part.makeCylinder(BORE_R, BEARING_W+0.2,
                             App.Vector(BEARING_X0-0.1, HINGE_Y, KNUCKLE_Z),
                             App.Vector(1,0,0)))
    MOUTH_OPEN_DEG = 150.0
    half_chord = BORE_R*math.sin(math.radians(MOUTH_OPEN_DEG/2.0))
    mouth_h = (BORE_R**2 - half_chord**2)**0.5
    mouth = Part.makeBox(BEARING_W+0.4, BORE_R*2.6, BORE_R+CLIP_WALL-mouth_h,
                         App.Vector(BEARING_X0-0.2, HINGE_Y-BORE_R*1.3,
                                    KNUCKLE_Z-(BORE_R+CLIP_WALL)))
    mouth.rotate(App.Vector(cx, HINGE_Y, KNUCKLE_Z), App.Vector(1,0,0),
                 MOUTH_PIN_ANGLE-180.0)
    ring = ring.cut(mouth)
    foot = foot.fuse(ring)
    # Root gusset across the bearing's whole width: fills the wedge between the bearing's outer
    # wall and the plate's underside so the two are one solid. Without it they meet on a TANGENT
    # line -- MEASURED at 0.0870 mm3 for the old clips, i.e. effectively no weld at all, which is
    # the user's "the hooks just fall right off after printing". One gusset now spans the bearing.
    gus = Part.makeBox(BEARING_W, CLIP_GUSSET_Y1-CLIP_GUSSET_Y0,
                       CLIP_GUSSET_Z1-CLIP_GUSSET_Z0,
                       App.Vector(BEARING_X0, CLIP_GUSSET_Y0, CLIP_GUSSET_Z0))
    gus = gus.cut(Part.makeCylinder(BORE_R, BEARING_W+0.4,
                                    App.Vector(BEARING_X0-0.2, HINGE_Y, KNUCKLE_Z),
                                    App.Vector(1,0,0)))
    foot = foot.fuse(gus)
    # Acute-corner fillets at the gusset's roots, on the FUSED solid. The corners that matter
    # only exist once the gusset, the bearing and the plate are one body; the sharpest is where
    # the gusset's outer face meets the bearing's outer CYLINDER, an acute tangent junction that
    # a box's own edges do not contain. One pass over the whole bearing, not three.
    _root = []
    _tangent_y = HINGE_Y + ((BORE_R+CLIP_WALL)**2 - (CLIP_GUSSET_Z0-KNUCKLE_Z)**2)**0.5
    for e in foot.Edges:
        bb = e.BoundBox
        if bb.XMax-bb.XMin < 20.0:                  # the long along-X edges of the bearing only
            continue
        if not (BEARING_X0-0.1 <= bb.XMin and bb.XMax <= BEARING_X1+0.1):
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
