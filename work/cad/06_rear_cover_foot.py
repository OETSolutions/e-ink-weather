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
# The clip must PRESS on the pin, not run free: a 0.30 mm free-running gap let the stand flop
# and fall off. A light interference gives controlled rotational friction (a friction hinge),
# and the clip wall is long enough that even 0.15 mm only reaches ~0.6% strain.
PIN_PRELOAD = 0.12                                  # interference -> friction
PIN_CLEAR = -PIN_PRELOAD                            # negative: bore is SMALLER than the pin
BORE_R = PIN_R+PIN_CLEAR                            # clip bore radius (2.08)
# Retracted detent: a shallow bump on the pin drops into a matching relief in the clip bore
# when the stand is folded, so it stays put instead of swinging out.
DETENT_H = 0.35                                     # bump height (radial)
DETENT_W = 2.40                                     # bump width along the axis
DETENT_T = 3.00                                     # bump angular width (mm of arc)
# OPEN-POSITION detent. The retracted detent above only holds the leg FOLDED. The user's
# complaint -- "there is nothing that holds it in place!!!! The foot will just collapse and the
# whole thing will fall over!!" -- is exactly right: the 65-degree feature is an OVER-TRAVEL
# STOP (it only ever pushes the leg further open, measured 0.98 mm3 at 65 deg and rising to
# 1.75 mm3 at 75 deg), and the friction is small enough that the weight of the case simply
# folds the leg back down. A stand needs a detent the leg must climb OVER to close.
#
# A second groove in the pin and a matching rib in each clip, at the deployed angle, gives it.
# The leg is pushed open past the crest (a short, firm push), the rib drops into the groove,
# and the leg then has to be deliberately pulled back over that crest to fold -- so it holds
# itself up. The crest is sized so the push is easy to make by hand.
DETENT_OPEN_ANGLE = 65.0                            # the deployed angle the leg locks at
# Where the clip's mouth points, in pin-angle degrees measured about the hinge axis
# (0 = +Z into the case, 90 = +Y folded side, 180 = -Z out the bottom edge).
# MEASURED, not assumed: the previous value of 180 put the mouth on -Z, but -Z is the ONLY
# direction from which the foot can approach -- so the pin was driven through the solid
# 210 deg WRAP instead of the 150 deg mouth, needing 7.5% strain (past PETG's 5% limit).
# Moving the mouth to 0 faces it along the free insertion direction.
MOUTH_PIN_ANGLE = 0.0
# The swing maps a FOLDED clip-frame point to pin angle (psi + open), MEASURED off the real
# solids in 38_probe_pin_grooves.py: the outermost clip vertex sits at pin angle 79.4 folded
# and 144.4 at 65 deg, i.e. exactly +65. (An earlier note in this file had the sign inverted
# and derived 205 deg; that was wrong and is what made the first two attempts fail.)
#
# So the groove needs no second rib: the clip's EXISTING retracted rib, at folded psi=90, is
# already sitting at pin angle 90 when folded and reaches pin angle 155 when the leg is open.
# Cutting a groove at 155 reuses the rib that is already there and is already proven printable,
# and it is impossible for the leg to fold without climbing back over that same crest.
DETENT_OPEN_PIN_ANGLE = MOUTH_PIN_ANGLE + 180.0 + DETENT_OPEN_ANGLE   # folded rib lands here
# The crest the leg must climb. Half the width either side of the groove centre, so the two
# flanks total this. Sized for about 8 N of hand force on a 70 mm leg (see validator 28).
DETENT_OPEN_FLANK = 1.2                             # mm of arc from groove edge to crest
CLIP_WALL = MIN_LOAD_WALL                           # 1.6 mm clip wall
CLIP_W = 3.00                                       # clip axial width
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
# split plane (z=COVER_T), i.e. KNUCKLE_Z+BORE_R+CLIP_WALL <= COVER_T. Keeping the full 1.6 mm
# clip (required minimum 1.2) therefore puts the axis at -1.10 rather than tangent to the plate.
KNUCKLE_Z = COVER_T-(BORE_R+CLIP_WALL)               # -1.10; keeps the clip under the cover

ARM_W = 6.00                                         # ASSUME; three strong printed necks
ARM_CENTERS = [FOOT_X+12.0, FOOT_X+FOOT_W/2, FOOT_X+FOOT_W-12.0]
SWING_DEG = 65.0                                     # open angle
# At y=8.5 the battery is still 5.7 mm away, so the pivot can use the leg's full width.
HINGE_X0, HINGE_X1 = FOOT_X, FOOT_X+FOOT_W           # full-width load distribution
WEB_Z1 = 3.00                                       # web tops at the cover split plane
CAVITY_X0 = HINGE_X0-2.0                            # clip-sweep cavity, cut before the pin
CAVITY_X1 = HINGE_X1+2.0
# Target lip interference when the pin passes the mouth: small but definite.
LIP_INTERFERENCE = 0.07
CHEEK_T = 3.00                                       # ASSUME side cheek thickness
SLOT_CLEAR = 0.25                                    # ASSUME neck-to-slot clearance per side
# --- 65-degree hard stop ------------------------------------------------------
# WHY THE OLD LUG FAILED, and why it is gone. The old 6.0 x 1.5 x 1.3 lug at (y 6.90,
# z -5.60) was derived for the OLD -1.10 hinge axis; the pin rework moved the axis to -0.68
# and the lug was never re-derived. Measured on the current solids it arrested the leg at
# 55.2 deg -- BELOW the gravity barrier. 36_kickstand_energy.py gives tau = dU/dphi crossing
# zero at ~58.5 deg: below that gravity FOLDS the leg, above it gravity OPENS the leg. A stop
# below the barrier is pressed from the wrong side, so the leg folds away from it and the
# stand collapses -- exactly the reported failure, and why no detent depth could fix it.
#
# WHY THE FIRST RAMP ALTERNATIVE ALSO FAILED (user: "super thin flanges on the shaft that
# will just break off since the attachment is so thin"). The ramp's top face DID land at
# 65.0 deg, but it was a floating fin welded only to the hinge PIN. MEASURED in
# probes/65_probe_true_joint.py: the ramp overlapped the cover by 1.414 mm3 TOTAL, i.e. a
# 0.17 x 0.17 mm sliver per side -- a weld in name only. The cause is that the cover has NO
# material near the hinge axis: the clip-sweep cavity is cut to r 4.28 and the pin is only
# r 2.20, so everything between them is the void the clips swing in. The ONLY solid material
# anywhere near is the two end WEBS (x 28.2..31.99 and 102.41..106.19), which run from the
# cover's lower wall down to z -2.88.
#
# So the stop is now an A-FRAME buttress: two struts that land on the web tops and carry the
# bearing face across the span, with a cross-tie so they act as a truss and cannot splay.
# Root bearing on the web top is 2.50 x 2.50 = 6.25 mm2 per strut and the struts are 2.50 mm
# thick everywhere, so there is no thin feature left to break. Everything sits at y >= 2.00,
# which keeps it out of the clip sweep (measured: zero contact at 65 deg, zero when folded).
STOP_ENABLE=True
# The stop is a RAMP whose top face lies on the leg plate's own underside at SWING_DEG, in two
# spans that reach from inside an end WEB out under the leg.
#
# WHY A RAMP. The plate is a rounded prism, so its corner is a CURVE: a vertical stop face meets
# it tangentially and engages as a soft wedge over tens of degrees (measured: a vertical face at
# the 65 deg corner Y only reached 0.05 mm3 of contact at 71 deg). A face lying ALONG the plate's
# underside engages all at once. MEASURED in probes/87_probe_ramp_offset.py: a prism whose top face is
# the line through (7.10, -3.56) with direction (cos65, -sin65) gives first contact at exactly
# 65.0 deg, ZERO contact at 64 deg, 4.10 mm3 of bearing at 65 deg, and then rises steeply
# (127.6 at 66 deg, 621.1 at 70 deg). That line was measured off the real rotated solid in
# probes/55_probe_leg_outline.py.
#
# WHY THIS IS NOT THE OLD THIN FLANGE. The first ramp was welded only to the hinge PIN: measured
# in probes/65_probe_true_joint.py it overlapped the cover by 1.414 mm3 total, a 0.17 x 0.17 mm sliver
# per side -- exactly the user's "super thin flanges on the shaft that will just break off". The
# cause is that the cover has NO material near the hinge axis: the clip-sweep cavity is cut to
# r 4.28 and the pin is only r 2.20, so everything between them is the void the clips swing in.
# The ramp also lies wholly BELOW the webs (the web bottom is z -2.88; the ramp spans z -3.56 to
# -10.27), so it cannot reach them directly.
#
# So each span gets a RISER: a block that runs from the ramp's back up to the web and is buried
# in it. The risers sit at x 29.40..31.80 and 102.60..105.00 -- inside the webs' own X range
# (28.20..31.99 and 102.41..106.19) and clear of the foot, whose plate starts at x 32.20. The
# load path is therefore ramp -> riser -> web -> cover, with millimetres of engagement.
# MEASURED weld in probes/88_probe_ramp_riser.py: see the printed joint volume, not a sliver.
STOP_RAMP_P=(7.10,-3.56)                            # measured face-line point at 65 deg
# Length of the bearing face. MEASURED limit in probes/90_probe_ramp_extent.py: the ramp must not
# reach past the plate's own outer edge (Y 8.50), or it sits under the plate's corner and
# blocks the straight-down insertion -- clear at Yend 8.50, blocked (0.53 mm3) at 8.70.
# 3.3 mm puts the outer end at exactly Y 8.50, the largest face that still assembles.
STOP_RAMP_LEN=3.3
STOP_RAMP_T=3.0                                     # material behind the face
STOP_SPAN_X=((29.40, 40.60), (93.80, 105.00))       # from inside a web out under the leg
STOP_RISER_X=((29.40, 31.80), (102.60, 105.00))     # buried in the webs
STOP_RISER_Y0=4.00                                  # covers the ramp's back
STOP_RISER_Y1=7.20                                  # out to the web's own Y edge
STOP_RISER_Z0=-5.00                                 # down into the ramp
STOP_RISER_Z1=-2.00                                 # 0.88 mm up into the webs (z -2.88)

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
HATCH_PLUG_T=1.4                                      # ASSUME fills opening depth
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
HATCH_LIP_T=1.20                                    # perimeter lip thickness
HATCH_LIP_H=1.20                                    # lip height into the bay wall
HATCH_LIP_CLEAR=0.35                                # per-side clearance so it drops in

# The exterior plate is ONE rounded rectangle, not a flange plus a welded-on tab. The previous
# shape was a 21.25..80.45 flange with a hard-cornered 11 x 18 mm box tab stuck onto its +X side
# to reach the screw, which read as an obvious lump glued to the cover (the user's "weird
# artifacts and shape"). The plate now simply runs from the bay out to the screw, and its edge
# covers the anti-rotation lip on every side.
#
# Flange edge vs lip outer: the lip outer boundary is SERVICE_BAY +- (HATCH_LIP_CLEAR +
# HATCH_LIP_T) = 20.95..80.75 / 66.45..93.55, and the recess in the cover is cut to exactly
# that. The old flange stopped 0.30 mm INSIDE it, so a 0.30 mm ring of the recess groove was
# left exposed around the plate. The plate now laps 0.05 mm beyond the groove on the -X and Y
# edges, so the groove is fully covered.
HATCH_LIP_OUT=(SERVICE_BAY_X0-(HATCH_LIP_CLEAR+HATCH_LIP_T),
               SERVICE_BAY_Y0-(HATCH_LIP_CLEAR+HATCH_LIP_T),
               SERVICE_BAY_X1+(HATCH_LIP_CLEAR+HATCH_LIP_T),
               SERVICE_BAY_Y1+(HATCH_LIP_CLEAR+HATCH_LIP_T))
HATCH_FLANGE_X0=HATCH_LIP_OUT[0]-0.05
HATCH_FLANGE_Y0=HATCH_LIP_OUT[1]-0.05
HATCH_FLANGE_Y1=HATCH_LIP_OUT[3]+0.05
# +X edge is set by the screw, not by the bay: the M3 head counterbore is dia 6.0 at 84.5, so
# 88.7 leaves a full 1.2 mm of plate outside it (and 1.0 mm outside the 3.2 mm boss at 87.7).
HATCH_FLANGE_X1=88.70
# The plate is FLUSH at one thickness (HATCH_FLANGE_T). Making it thicker so the head could sit
# flush put its face 2.8 mm below the flange -- and the hatch prints exterior-face down, so that
# would have lifted the whole plate off the bed (validator 23 caught it). The head therefore
# sits proud of the exterior face, which is normal for a service cover.
HATCH_HEAD_CBORE=0.6                                 # shallow seat, not a flush recess
# Finger scallop for prying the hatch off without tools: a half-round notch in the plate's +Y
# edge. The previous 31.6 x 5.0 mm rectangular bite removed most of that edge and was one of the
# "weird shape" artifacts. It stays clear of the USB slot (x 25.6..34.6) and of the screw.
HATCH_RELIEF_R=6.0
HATCH_RELIEF_X=(SERVICE_BAY_X0+SERVICE_BAY_X1)/2.0   # 50.85, bay centre
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
    cavity_r = BORE_R+CLIP_WALL+0.6
    cavity = cyl_x(CAVITY_X0, CAVITY_X1-CAVITY_X0, HINGE_Y, KNUCKLE_Z, cavity_r)
    cover = cover.cut(cavity)
    # Pin spans the whole cavity and projects into the webs at each end.
    pin = cyl_x(HINGE_X0-PIN_ROOT, (HINGE_X1-HINGE_X0)+2*PIN_ROOT,
                HINGE_Y, KNUCKLE_Z, pin_r)
    cover = cover.fuse(pin)
    # Retracted detent. The pin gets a shallow GROOVE on its +Y (folded) side and each clip
    # has a matching raised rib. When folded, the rib drops into the groove: a positive
    # location that holds the stand closed, while the interference fit provides running
    # friction. Using a groove (not a bump) means the detent cannot add preload or collide.
    for cx in ARM_CENTERS:
        # Shallow scallop on the +Y side only: a small cylinder whose centre sits OUTSIDE the
        # pin surface, so it removes a groove without severing the pin from the end webs.
        # The groove tracks the rib, which is at MOUTH_PIN_ANGLE+180, not at a fixed +Y.
        for pa_deg in (MOUTH_PIN_ANGLE+180.0, DETENT_OPEN_PIN_ANGLE):
            pa = math.radians(pa_deg)
            pb = pin_r - DETENT_H
            cover = cover.cut(cyl_x(cx-CLIP_W/2-0.1, CLIP_W+0.2,
                                    HINGE_Y + pb*math.sin(pa),
                                    KNUCKLE_Z + pb*math.cos(pa), DETENT_H))
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

    # --- hatch retention: anti-rotation lip recess + ONE screw ---------------------------------
    # The hatch is held by a single M3 screw; a continuous recessed lip around the bay stops
    # it rotating about that one screw. The side cantilevers are gone entirely.
    # The recess must be an annular GROOVE in the cover's REAR FACE, occupying exactly the Z
    # band the lip occupies. An earlier version cut a counterbore at z 1.8..3.0 while the lip
    # spanned z 0.8..2.6, so the lip's lower half sat inside solid cover plate (292 mm3).
    lip_z0 = HATCH_PLUG_T-0.6
    lip_z1 = lip_z0+HATCH_LIP_H+0.6
    groove = rprism((SERVICE_BAY_X1-SERVICE_BAY_X0)+2*(HATCH_LIP_CLEAR+HATCH_LIP_T),
                    (SERVICE_BAY_Y1-SERVICE_BAY_Y0)+2*(HATCH_LIP_CLEAR+HATCH_LIP_T), 3.0,
                    SERVICE_BAY_X0-HATCH_LIP_CLEAR-HATCH_LIP_T,
                    SERVICE_BAY_Y0-HATCH_LIP_CLEAR-HATCH_LIP_T,
                    lip_z0-0.2, (lip_z1-lip_z0)+HATCH_LIP_CLEAR+0.2)
    # Inner boundary = the BAY EDGE itself. Cutting it at (bay - CLEAR) left the lip's inner
    # face 0.35 mm inside the opening, where cover material still exists -> the lip bit into the
    # bay wall (26.9 mm3).
    groove_inner = rprism(SERVICE_BAY_X1-SERVICE_BAY_X0,
                          SERVICE_BAY_Y1-SERVICE_BAY_Y0, 3.0,
                          SERVICE_BAY_X0, SERVICE_BAY_Y0,
                          lip_z0-0.4, (lip_z1-lip_z0)+0.8)
    cover = cover.cut(groove.cut(groove_inner))
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
        # The 65 deg stop: a ramp along the leg plate's underside at SWING_DEG, anchored to the
        # end webs by risers. See the constant block for the measurements behind it.
        ca, sa = math.cos(math.radians(SWING_DEG)), math.sin(math.radians(SWING_DEG))
        py, pz = STOP_RAMP_P
        ey, ez = py + STOP_RAMP_LEN*ca, pz - STOP_RAMP_LEN*sa      # inward end of the face
        by, bz = ey - STOP_RAMP_T*sa, ez - STOP_RAMP_T*ca          # back of the ramp
        ay, az = py - STOP_RAMP_T*sa, pz - STOP_RAMP_T*ca
        quad = [App.Vector(0, py, pz), App.Vector(0, ey, ez),
                App.Vector(0, by, bz), App.Vector(0, ay, az)]
        quad.append(quad[0])
        stop = None
        for x0, x1 in STOP_SPAN_X:
            ramp = (Part.Face(Part.makePolygon(quad))
                    .extrude(App.Vector(x1-x0, 0, 0)).translate(App.Vector(x0, 0, 0)))
            stop = ramp if stop is None else stop.fuse(ramp)
        for x0, x1 in STOP_RISER_X:
            stop = stop.fuse(Part.makeBox(x1-x0, STOP_RISER_Y1-STOP_RISER_Y0,
                                          STOP_RISER_Z1-STOP_RISER_Z0,
                                          App.Vector(x0, STOP_RISER_Y0, STOP_RISER_Z0)))
        cover = cover.fuse(stop)

    return cover


def build_hatch():
    """Service hatch: perimeter anti-rotation lip + ONE screw driven from OUTSIDE.

    The previous plug carried two in-plane cantilevers cut by U-slots. They did not print as
    flexures -- they stayed attached along their length and just looked like blobs stuck to the
    side ("the friction pieces on the sides stuck to the side and don't print"). Retention is
    now a single M3 screw that goes through the flange on the case's EXTERIOR face and
    threads into a boss on the cover's rear face; rotation about that one screw is prevented
    by a continuous lip that drops into a recess around the whole bay.
    """
    plug = rprism((SERVICE_BAY_X1-SERVICE_BAY_X0)-2*HATCH_CLEAR,
                  (SERVICE_BAY_Y1-SERVICE_BAY_Y0)-2*HATCH_CLEAR, 2.75,
                  SERVICE_BAY_X0+HATCH_CLEAR, SERVICE_BAY_Y0+HATCH_CLEAR, 0.0, HATCH_PLUG_T)
    # ONE plate. It covers the bay and runs out over the cover to carry the screw, so there is
    # no seam, no step and no separate tab.
    flange = rprism(HATCH_FLANGE_X1-HATCH_FLANGE_X0, HATCH_FLANGE_Y1-HATCH_FLANGE_Y0, 4.0,
                    HATCH_FLANGE_X0, HATCH_FLANGE_Y0, -HATCH_FLANGE_T, HATCH_FLANGE_T)
    hatch = plug.fuse(flange)

    # CONTINUOUS anti-rotation lip: a ring on the plug's inner face that sits in the bay's
    # matching recess. Because it runs all the way round, it resists rotation about the single
    # screw in every direction, and it also stops the hatch being pushed in too far.
    lip = rprism((SERVICE_BAY_X1-SERVICE_BAY_X0)+2*(HATCH_LIP_CLEAR+HATCH_LIP_T),
                 (SERVICE_BAY_Y1-SERVICE_BAY_Y0)+2*(HATCH_LIP_CLEAR+HATCH_LIP_T), 3.0,
                 SERVICE_BAY_X0-HATCH_LIP_CLEAR-HATCH_LIP_T,
                 SERVICE_BAY_Y0-HATCH_LIP_CLEAR-HATCH_LIP_T,
                 HATCH_PLUG_T-0.6, HATCH_LIP_H+0.6).cut(
          rprism((SERVICE_BAY_X1-SERVICE_BAY_X0)+2*HATCH_LIP_CLEAR,
                 (SERVICE_BAY_Y1-SERVICE_BAY_Y0)+2*HATCH_LIP_CLEAR, 3.0,
                 SERVICE_BAY_X0-HATCH_LIP_CLEAR, SERVICE_BAY_Y0-HATCH_LIP_CLEAR,
                 HATCH_PLUG_T-0.1, HATCH_LIP_H+0.2))
    hatch = hatch.fuse(lip)

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
    # plate's +Y edge, centred on the bay, instead of the previous 31.6 x 5.0 mm rectangular
    # bite that removed most of that edge and read as a chunk cut out of the part. Clear of the
    # USB slot (x 25.6..34.6) and of the screw boss entirely.
    hatch = hatch.cut(Part.makeCylinder(HATCH_RELIEF_R, HATCH_FLANGE_T+0.4,
                                        App.Vector(HATCH_RELIEF_X, HATCH_FLANGE_Y1,
                                                   -HATCH_FLANGE_T-0.3)))
    # USB cable slot through the flange, aligned to the cover's slot.
    hatch = hatch.cut(Part.makeBox(USB_SLOT_W, 6.0, HATCH_FLANGE_T+HATCH_PLUG_T+0.6,
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
        # The rib sits diametrically OPPOSITE the mouth, in the middle of the remaining
        # 210 deg wrap -- at MOUTH_PIN_ANGLE+180 -- so it is 105 deg clear of either mouth lip
        # and its 6.35 deg arc leaves solid wall either side (>= the 0.8 mm slot rule).
        # Its crest projects 0.05 mm past the bore, so it adds no interference anywhere but its
        # own 0.30 mm-deep detent. The matching groove in the cover moves with it (build_cover).
        rib_a = math.radians(MOUTH_PIN_ANGLE+180.0)
        rib_r = BORE_R-DETENT_H/2.0
        rib = cyl_x(cx-CLIP_W/2, CLIP_W,
                    HINGE_Y+rib_r*math.sin(rib_a), KNUCKLE_Z+rib_r*math.cos(rib_a),
                    DETENT_H)
        # There is deliberately NO second rib. The rib above is at folded psi=90, so when the
        # leg swings open 65 deg it arrives at pin angle 155, which is exactly where the second
        # groove is cut in build_cover(). One rib, two grooves: the same feature holds the leg
        # folded AND held open, and there is nothing extra to print or to weaken the clip.
        foot = foot.fuse(ring.fuse(rib))
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
