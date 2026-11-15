# The game camera

The game's camera is an ordinary actor (Fix actor 5) with a *camera object* of its own. Its scripts
fill a handful of numbers every frame, and two engine routines turn them into a position: the
*solve* (0x485810) works out where the camera would like to be, and the *mechanics* (0x4359D0)
moves it there a little at a time. Everything below was read from `Rayman2.exe`; where our engine
does something else it says so.

## The camera actor and its camera object

The camera object is `actor+0x10`. Its first four dwords point at four *parameter blocks* of 0x7C
bytes each (the Fix data spaces them 0x80 apart):

| Pointer | Block | What it is |
|---|---|---|
| cam +0x00 | default | the level's numbers, as saved by the game's editor |
| cam +0x04 | current | what the camera actually uses |
| cam +0x08 | spare | a saved copy (0x4140D0 saves, 0x414100 restores) |
| cam +0x0C | override | the per-frame copy the scripts write into (0x4140A0) |

`cam+0x74` bit 0 says the override holds a copy of the current block (set by 0x4140A0, cleared by
procedure 444 and by procedure 491 index 0x27); `cam+0x75` bit 1 says the spare was saved. Procedure
444 always ends with the override copied away again, so what the scripts write lives for one frame
unless they write it every frame.

### The parameter block

| Offset | Field | Learn_10 default | What it means |
|---|---|---|---|
| +0x00 | vector A | (0, 0, 0) | offset point, from the aim point in the target's own axes |
| +0x0C | vector B | (0, 0, 1.4) | aim point, from the target's position in its own axes |
| +0x18 | min distance | 8 | the camera never comes nearer the aim point than this |
| +0x1C | max distance | 8 | nor further away |
| +0x20 | settle distance | 3 | "close enough" for the mode-0 smoothing (0 = never settles) |
| +0x24 | | 9 | not read by the solve stages we model |
| +0x28, +0x2C | direction limits | 0, 0 | the angle bounds the pi/8 turn walks towards |
| +0x30 | pitch centre | 0.35 | pitch-window centre in radians; also copied to dynamics +0x208 |
| +0x34 | | 0 | |
| +0x38 | speed | 4 | largest position step a second |
| +0x3C | speed ramp | 5 | how fast the step may grow (maxstep/ramp + last step) |
| +0x40 | arrive | 0 | arrival ease for the position: step ≤ distance/arrive |
| +0x44 | turn speed | 1.5 | largest turn about the aim point a second |
| +0x48 | turn ramp | 5 | its growth rate |
| +0x4C | turn arrive | 0.5 | its arrival ease |
| +0x50 | aim speed | 4 | largest aim-point step a second |
| +0x54 | aim ramp | 2 | its growth rate |
| +0x58 | aim arrive | 2 | its arrival ease |
| +0x60 | min height | -1e21 | floor for the wanted position's height |
| +0x64 | max height | 1e21 | ceiling |
| +0x68 | target | 0 in the file | the super object the camera looks at and follows |
| +0x70, +0x72 | option words | 0 | latched switches (see below) |
| +0x79 | | 0 | |

The defaults are the camera actor's default block in Learn_10 (Fix block 04/00, offset 0x13558).
The file's current block is all zeros: the engine fills it from the default block when it makes the
camera object, which our loader does not, so our procedure 444 copies it once (see
"What we do differently").

Bits of the `+0x70` word our mechanics reads: 0x01 snap the position, 0x04 keep the orientation,
0x80 take the aim point from the camera's own axes. Bits of `+0x72` the solve reads: 0x1000 the
offset point is the vector a script fixed (495), 0x2000 the wanted position is the vector a script
fixed (496, 499).

## The scripts

The camera's own model (Fix actor 5, model `0d/00 +0x17748`) has 11 behaviours; other actors (the
camera controllers placed in the level) drive it through the same procedures. Procedures 444..500:

| | What it does |
|---|---|
| 444 | commit: back the current block up, reset on a target change, solve, apply, clear the override |
| 445, 446 | set vector A (+0x00) or vector B (+0x0C): mode_temp, mode (-1 copy the default, 0 add, 1 store), vector |
| 447..466 | the same for one float each, +0x18 up to +0x64 |
| 467 | the target (+0x68): mode_temp, mode (-1 the default's, 0 nobody, else the actor), actor |
| 472..480 | one bit of the +0x72 word (472 takes the default block's whole word as the mask) |
| 481..490 | one bit of the +0x70 word |
| 491 | one entry of the global tuning block 0x4FF62C.. (index 0x27/0x28 masks, 0x2C the snap flag) |
| 492, 493 | save and restore that block (the camera model saves once and restores at the end of every frame) |
| 495, 496, 497 | fix the offset point, the wanted position, or both, and set bit 0x1000 / 0x2000 / 0x4000 |
| 499 | aim at the target (cam+0x34 and bit 0x2000), or cut when cam+0x17C is set |
| 500 | restore the current block from the spare one (cam+0x75 bit 1) and clear cam+0x60 |
| 159 | start the eight tracked points at the target's position, weight 0.03 each |

Function 310 asks the camera where its offset point would be this frame without committing (it backs the block
up, runs the first two solve stages and returns that world point, context +0xA0); the hover camera feeds it to 495; functions 186 and 190 give the
scripts the signed angle between two directions and an actor's movement direction.

## The solve (0x485810, from procedure 444)

Given the block in force (the override while cam+0x74 bit 0 is set, else the current block) and its
target (+0x68):

1. **Aim point.** The target's position, plus vector B turned into the target's axes
   (0x483F90). The exe also averages eight tracked points on the target; they all start at its
   position (procedure 159), so the position is what we use.
2. **Offset point.** The aim point plus vector A in the target's axes, or the vector procedure 495
   fixed in the camera object when bit 0x1000 is set (0x4840A0).
3. **Wanted position** (0x4847D0).
   - Bit 0x2000 set: the vector procedure 496 or 499 fixed (cam+0x34).
   - No target: stay where the camera is.
   - Otherwise: on the sphere about the aim point whose radius is the distance the camera has now,
     clamped to +0x18..+0x1C, along the direction the camera lies in from the aim point. The
     direction is the one the angle limits in +0x28..+0x34 ask for (0x484390, 0x4841F0, 0x484470).
     Then the height is clamped into +0x60..+0x64.
   - **Swinging round** (0x485760 -> 0x4855D0 -> 0x4853C0, camera modes 0 and 1). When the yaw
     the limits ask for is pi/8 or more from the measured one (0x4841F0 sets context +0x74 and
     keeps the measured yaw moved pi/8 that way at +0x78), the exe builds the wanted position again
     with the yaw window centred on that stepped angle and, if the camera could stand there
     (0x486400, its collision test), takes it, with its movement. So a far target makes the camera
     chase a point pi/8 round the circle at its own distance: it orbits instead of cutting across.
     Holding Q or W gives the block a reference axis 90 degrees round every frame (procedure 497 with
     the cross product of camera − Rayman and up), so this is what turns the camera round him.
     The way round is kept in the camera object (+0x76: 1 positive, 0xFF negative, 0 not chosen,
     0xFE blocked; +0x5C the gap it chose on) until the gap is under pi/8 again; a gap that changes
     sign away from the back of the circle (under pi − pi/8) lets it choose again, and when the
     stepped angle is the wrong way for the kept choice, 0x485580 steps the other way. Not while
     the block's +0x72 bit 4 is set. 0x486400 rejects a candidate strictly closer to the current
     target anchor than block +0x20 plus the global tolerance (0x4FF698, normally 0.05). On a
     rejected first step, 0x4855D0 tries the opposite direction only when no direction has been
     chosen and the gap is at least pi minus the step; otherwise it latches 0xFE and retains the
     normal wanted position. The latch resets when the gap is smaller than the step or +0x72 bit
     4 is set. The step is global 0x4FF6D0 (normally pi/8).

     0x4855D0 also latches blocked without trying a step when bit 0 of the camera mechanics report
     (+0x168, reached through the solve context's first pointer) is set and global 0x4FF6DC bit 0
     is clear. 0x434660 clears that report bit each movement and sets it on collision. Our sphere
     slide reports a collision when its returned position differs from the proposed position.
     0x4853C0 requests obstruction checks only in mode 1 with camera +0x75 bit 2: mask 3, or 2
     when block +0x72 bit 3 is set. 0x485FD0 samples offset rays from anchor and camera position;
     our approximation tests the static triangle world, not dynamic objects or sector selection.
4. **What the mechanics gets**: the movement (wanted − current position), the look-at, the offset
   point, the back direction, the +0x70 option word and the nine speeds and eases from +0x38..+0x58
   (the ramps are clamped to at least 0.001).

When the target changes, procedure 444 resets the camera object's runtime scratch (0x413E30) and
procedure 444 records the target in the global 0x4B9C14. A fresh camera — one that has not moved
yet — takes the "mode 4" path of the solve (0x4858F6): its block is reset to +0x70 = 0x55 and
+0x72 = 0x0A, and cam+0x60 = 4, a one-frame snap to the target.

## The mechanics (0x4359D0)

### Returning from Bast_20's opening scene

Camera initialization (0x413DB0) sets the pending cut byte at `cam+0x17C` to 1;
the serialized camera has zero there. This request survives the initial fixed shot.
Bast_20's director (scene node 89, model `0d/01 +0x30764`) clears the camera's custom
bit 30 when behaviour 3 ends. Its behaviour 1 rule at `0d/01 +0x336E0` applies
489/486/475/482/484 and calls 499 on the camera (scene node 3).

With the request set, 499 (0x47EDD0) computes the active block's target/offset/view
and fitted follow position, searches for a visible candidate (0x486EA0), then places
the eye immediately (0x486F40). Placement resets the mechanics aim (0x473480) and
fits a mode-1 follow transition (0x487520). It sets active-block position-snap and
orientation flags and consumes the request. It does not run ordinary movement timers
or append another target-history sample. Without the request, 499 fixes the wanted
position at the camera's current position for that frame instead.

OpenRayman now initializes the request and executes that immediate search/placement.
The real-data regression verifies a one-frame jump at the director handoff and one
frame in mode 1, followed by mode 0. The recording lands at (14.412,61.645,-58.309);
our replay lands at (8.00,71.84,-58.52). The jump and nearby framing are reproduced,
but the existing candidate/collision approximation does not select the recorded
azimuth. Measurements and replay commands are in `build/helpers/cam20_report.md`.

Where this came from: own reading of Rayman2.exe at 0x413DB0, 0x47EDD0,
0x486EA0, 0x486F40, 0x473480 and 0x487520; Bast_20's director scripts; and the
user's `rec_bast20cam1` camera/actor logs. No executable code is reproduced.

### Bast_20's fight framing and floor slope

The fight-region controller is scene node 39, model `0d/01 +0x15890`. It keeps
Rayman as the target and supplies fixed reference axes (497), pitch 0.22 (453),
distance 12 (447/448), and local offset A (0,0,2.5) (445). With B's usual 1.4,
the view aims 3.9 units above Rayman, framing the elevated pirate. This is distinct
from the camera model's selected-enemy rule at `0d/00 +0x181F0`, which uses
Rayman variables 9 and 41 and procedures 495/497.

After constructing the normal wanted eye, 0x485810 calls 0x4845E0 unless the
startup flag, fixed-position flag, slope-disable flag or obstruction-waypoint
flag suppresses it. This projects the reference direction onto the target's floor,
weights the resulting tilt by the eye-to-target direction, and adjusts pitch.
0x484B00 also applies it when calculating ideal points and search candidates.
OpenRayman formerly used this only for candidates; ordinary following now uses
the same correction. The pitch is kept local in our direction calculation rather
than mutating executable-style scratch blocks.

At the recorded fight endpoint, our settled eye is (23.14,76.84,-59.72), versus
(23.157,76.820,-59.707) in `rec_bast20cam2`; yaw -35.0 and pitch +21.5 degrees
also agree. A real-data regression checks this framing within 0.2 units. This
general correction changes the checked Learn_10 route by at most 0.332 units;
the checked Jail_20 trace is unchanged. It is not a Bast_20-specific offset.

Where this came from: own analysis of 0x4845E0, its callers 0x485810 and
0x484B00, the node-39 scripts and designer values, and the user's fight recording.

### Floor contact loss over a crest (card 120)

The normal lookup at **0x473660** follows the target's dynamics report, requires
report `+4` bit 0, and reads `+0x34..+0x3C`: **ground slot 1**, not the first
contact in slot 0. It does not remember or average a normal. With that bit clear,
0x4845E0 returns without changing pitch. This also skips fitting during a jump
without testing Rayman's jump behaviour; a valid contact enables it again.

The fitted pitch changes immediately in the per-frame parameter block.
0x485810 rebuilds the wanted eye with 0x4847D0, then 0x484B50 supplies the usual
speeds. 0x474420 passes these to mechanics; 0x4359D0 calls 0x433D70 (step/orbit),
0x434660 (collision) and 0x435930 (publish). There is no separate floor-pitch
interpolator or angular-rate state. The ordinary position and orbit limits apply,
along with the existing configurable direction smoothing at 0x433930.

Our 0x433B00 approximation incorrectly bypassed arrival and acceleration whenever
a displacement fitted inside the speed cap. Both limits still apply in the exe;
only a snap or a displacement at most 0.001 bypasses them. Option bits 1 and 3
bypass the position/orbit eases respectively; bit 5 does so for aim movement.
A zero permitted orbit keeps the old direction and the post-position-step radius.
These mechanics discrepancies are corrected and covered by a synthetic regression.

The user's `rec_20261115_132731` target/contact replay still rises **1.18 units
over ticks 2806..2810**, before and after these corrections: largest height step
0.26 units and pitch step 1.2 degrees. Independently evaluating the decoded
position/orbit laws for ticks 2806..2818 agrees with the corrected mechanics
within 0.00006 units. Thus this crest is not evidence for a missing camera pitch
filter: intermittent ground validity produces a wanted-eye change and the ordinary
mechanics follows it. This comparison uses recorded OpenRayman target states,
not a matched recording from the original game. Whether the original target
mechanics would also lose ground contact here remains unverified.

Where this came from: own reading of Rayman2.exe at 0x473660, 0x4845E0,
0x485810, 0x484B50, 0x474420, 0x433B00, 0x433CD0 and 0x433D70;
Capstone inspection of 0x4359D0 and the 0.001 tolerance at 0x49C6F8;
the user's engine recording and independently written replay/reference helpers.
No executable or decompiler code is reproduced. Commands, all four crest
measurements, test results and limits are in `build/helpers/bob120_report.md`.

The recorded hallway route (`rec_bast20cam3`) travels south with its camera to
the north. The replay in `build/helpers/cam20_replay_recordings.py` preserves that
direction for all 591 checked frames: eye Y minus Rayman Y is 10.64..12.91. The
hallway's node-64 controller supplies a reference pointing north, an 11-unit boom
and pitch 0.22. This replay injects recorded player positions and seeds the first
recorded eye; it does not establish that every freely played approach avoids the
reported frontal view. No separate hallway-specific fix has been established.

### Movement steps

Per frame, with dt in seconds (0x50043C):

1. **Position.** `maxstep = speed·dt`. A snap flag (+0x70 bit 0), a move shorter than 0.001 or one
   that fits in maxstep takes the whole move; otherwise the step is narrowed three ways and the
   camera moves along the wanted direction by that much:

   ```
   step = maxstep
   if arrive != 0:    step = min(step, |move| / arrive)
   if speed_ramp != 0: step = min(step, maxstep / speed_ramp + last_step)
   ```

2. **Orientation.** The camera orbits the aim point: it turns the direction from the aim point to
   the camera by a limited angle, then puts the position back on the sphere it was on, so the view
   swings round the point it looks at. The turn is the angle between (aim − old position) and
   (aim − new position), narrowed the same way with +0x44..+0x4C and the previous turn. Bit 0x04 of
   +0x70 skips this step.

   The handoff at 0x474420 also sets that bit for a fixed wanted position (+0x72 bit 0x2000).
   Thus procedure 496's position moves directly under the position step law, while the aim point
   still eases independently. The Menu controllers use this path: a replay showed mode 0,
   +0x70 = 0x40 and +0x72 = 0x2080 before the handoff. Previously OpenRayman omitted the forced
   bit and orbited even for these fixed destinations, bending the menu camera's path. This finding
   is from our own analysis of 0x474420 and 0x433D70 and the Menu scripts and replay; the exact
   original menu transition has not yet been compared with a live recording.
3. **Aim point.** The aim point steps towards the offset point with +0x50..+0x58; bit 0x80 of +0x70
   means the aim is a point in front of the camera instead (position − Y axis).
4. **Publish.** Y = the direction from the aim point back to the camera (so −Y is where it looks),
   X = Y × world Z, Z = X × Y, position as found. The exe's occlusion pass and the two dead blend
   steps are left out (see below).

The mechanics also smooths directions (0x433930, called at 0x434139 for the position and 0x43432B
for the aim point, with the mechanics' +0x26C/+0x27C). It turns the tick's movement, and the aim
point's step, a part of the way from the previous tick's direction towards its own and keeps the
length: the angle between the two unit directions is divided by the divisor (2 leaves it halfway),
and a divisor of 1, or either vector under 0.1, leaves the movement alone (0x49C6FC = 1, 0x49C708 =
0.1). The divisors are the camera settings' +0xA8/+0xAC (0x4FF6D4/0x4FF6D8, 1 at start-up, 0x4138B0),
which 0x474420 copies to the mechanics and holds at 1 while the block snaps the position or keeps the
orientation (+0x70 bits 0x01 and 0x04), snaps the aim (bit 0x10), or the tracked points stand still
(0x473570), and procedure 491's indices 42 and 43 set them (Learn_10 never does). The carried vectors
are the tick's whole movement at +0xF8 (written by 0x434660; its length is what the step law 0x433B00
grows from) and the smoothed aim step at +0x130 (written at the end of 0x433D70).

The block's `+0x20` settle distance belongs to the other follow path (mode 0, 0x4870C0): it tests
the distance between the current camera position (context +0x20) and the target anchor (+0x44),
not the desired camera position (+0x2C). `0x486710` also tests candidate distance against the
block's settle distance plus a global tolerance at 0x4FF698.

### Obstacle search and transitions

The executable uses two distinct collision queries. `0x4863D0` tests the view from the
instantaneous target anchor (context +0x44) to the eye. `0x4862B0` tests movement from the eye
to a candidate. Both use `0x485FD0`: one ray when the count is one, otherwise a ring of rays
around the line, with no additional centre ray. The initial perpendicular is scaled by the
start radius and is multiplied by the endpoint radius again, so the default movement ring's
start offset is 0.35 squared, or 0.1225. The view defaults to one centre ray. `0x4862E0` uses
the movement ring on a view extended 0.35 beyond the proposed eye. Endpoints are truncated to
thousandths by `0x485CA0` before intersection tests.

`0x4870C0` runs in modes 0 and 1, except with block +0x72 bit 3, a fixed position (0x2000),
or camera +0x74 bit 1. It accumulates elapsed milliseconds at camera +0x68 and tests the view
after the configured number of frame durations (four by default). A clear view clears the
blocked timer (+0x6C). A blocked view advances that timer; after 60 tested frame durations
it requests the fallback search. Otherwise a rejected desired follow point starts a reachable
search immediately. `0x486400` rejects points nearer the instantaneous anchor than settle
distance + tolerance, and optionally tests view and movement rays (mask bits 1 and 2).

The static scenery pass can be suppressed while the camera has stopped for more than ten frame
durations, or while its last movement and the anchor line have absolute dot product greater
than 0.9. Actor tests remain enabled unless block +0x72 bit 4 is set. This bit disables actor
queries, not scenery. The stationary timer at camera +0x70 uses movement threshold 0.005 and
caps at 60 frame durations. `0x486FA0` also searches when the camera is within settle distance
and stopped or colliding. `0x4873B0` searches when collision report +0x168 has both bits 0 and 1.
`0x434660` sets the second bit for a first head-on contact (movement dot outward normal < -0.7)
while travelling towards the mechanics aim (dot > 0.7).

`0x486880` starts with the ordinary follow block (the spare block while a transition has saved
one), holds its angle centres, and walks yaw in pi/4 steps. At each yaw it samples the saved
pitch, zero, +pi/4 and -pi/4, with the usual distance and height clamp and `0x4845E0` floor-slope
pitch correction. The fallback additionally samples half the ideal follow distance at the saved
pitch unless global bit 0x40 disables it, and mask bit 8 adds 0.33 distance. With default tuning
there are 32 reachable-search candidates or 40 fallback candidates (48 with the extra distance).

`0x486540` scores distance, yaw and pitch differences from the ordinary follow point. For a
nonnegative difference d it forms q(d) = -trunc(-200d) - trunc(100d). The score is
0.001 q(distance error) + 0.01 q(yaw error) + 0.1 q(pitch error). Yaw is reflected about pi
before subtraction. For pitch, the reference above pi is reflected while a candidate above pi
is reduced by 2pi; the asymmetry is present in the assembly. `0x486D50` sorts ascending, then
`0x486710` checks minimum distance and proximity to the desired/current eye (tolerance 0.5).
It adds an offset to the instantaneous anchor for these checks, whereas `0x4864D0` adds it to
the tracked aim point for the final view/movement tests. Improving searches (mask bit 8) also
require a score reduction of at least 0.05 and `0x4875D0`'s transition progress gate.

`0x487520` starts **mode 1**. It saves the current block once, fits its distance/yaw/pitch to
the accepted world point (`0x487440`/`0x4874F0`), copies those four fields into the per-frame
block, clears the swing direction and stores the start and destination at camera +0x10/+0x1C.
Camera +0x75 bit 2 marks fitted follow parameters; bit 3 holds an obstruction waypoint.
`0x485180` keeps that waypoint, projecting the ordinary follow point onto its line towards the
tracked target while the target moves (unless global bit 8 disables projection). `0x4875D0`
releases the hold once the ordinary follow point makes more progress from the start than the
waypoint, or the stationary timeout expires. There is **no special transition speed constant**:
`0x484B50` still supplies the usual position/turn gains and caps, and mechanics still slides its
sphere. Mode 1 can restore the spare and return to mode 0 when a moving target's fitted follow
point is within tolerance and global bit 0x10 is enabled (`0x485810`). `0x487730` also checks
every 20 frame durations when camera +0x17C is requested (procedure 491 index 44): an improving
search with mask 11 restores ordinary follow when the first sorted candidate is accepted,
otherwise starts another transition. Global bit 0x20 disables this improvement pass.

If no reachable candidate passes, `0x486EA0` searches with mask 5 (13 with global bit 4): it
checks the extended view but does not require movement from the old eye. **Mode 2** then relocates
the eye directly (`0x486F40`/`0x473420`) and enters mode 1. Thus a full collision-free swept-sphere
path is not a prerequisite for every recovery. The removed interim search required such a path
and used its own retry and waypoint lifetime rules.

Offsets below are relative to global tuning block 0x4FF62C:

| Offsets | Use | Defaults |
|---|---|---|
| +0x34/+0x38 | movement ring endpoint scales | 0.35/0.35 |
| +0x3C/+0x40 | view ring endpoint scales | 0.1/0.1 |
| +0x44/+0x48 | reachable/fallback yaw spacing | pi/4, pi/4 |
| +0x54/+0x58 | camera/target movement thresholds | 0.005/0.01 |
| +0x68/+0x6C | proximity tolerance/min-distance tolerance | 0.5/0.05 |
| +0x74/+0x76 | movement/view ray counts, int16 | 3/1 |
| +0x78/+0x7A | route/improvement periods, int16 frame counts | 4/20 |
| +0x7C/+0x7E | stationary/blocked timeouts, int16 frame counts | 60/60 |
| +0xB0 | global options, uint16 | 0 |

OpenRayman now uses these candidate and transition routines, including the view-only relocation
fallback. It tests all loaded static triangles rather than the executable's camera/target sector
lists. Its actor pass uses the movement system's nearby posed triangles; actor sphere/box
occluders and the executable's collision-set group filter are not reproduced. Stable ordering
breaks equal scores by generation order; the executable's C qsort does not specify tie order.
The mechanics slides against both static scenery and nearby posed actor triangles. Its first
contact normal supplies the head-on report bit. Resolving scenery, actor geometry and then
rechecking scenery remains an approximation of the executable's combined sphere solver.
Relocation also resets the mechanics aim and carried aim step (`0x473480`), so the new view
looks at the target rather than the pre-cut aim point. The solver uses the interpreter's elapsed
frame time for its timers.

The Learn_10 trapdoor replay now passes the approximate recovery check. With the first-visit
door present and sliding open, camera mechanics stops on its posed triangles and reports a
head-on obstruction. `0x4873B0` requests the search; after the reachable candidates fail, the
view-only fallback relocates below the door and enters mode 1. The earlier replay used a
completed revisit (which removes the door), and the camera mechanics also omitted actor
surfaces. Adding actor tests to the search alone could not reproduce the obstruction.

The reproducible headless run (`build/helpers/cam13_replay.ps1`) enters mode 1 0.650 s after
the z < -11.8/behaviour-8 fall threshold, versus 0.625 s in the recording. Its eye is then
(-25.84,291.38,-23.28), 4.67 units from Rayman; after another 0.1 s it is
(-26.83,291.31,-24.42), 5.51 units away. Mode 0 resumes after 0.267 s, with the eye at
(-27.72,291.27,-25.40), 5.38 units away. This meets the approximate timing and below-floor
proximity requirement, without matching every recorded frame. The approach/fall trajectories
differ; the recording's mode-1 duration is 0.375 s. The replay uses the Lum-collected signal and
releases the door's frozen brain, then lets its scripts move it. It seeds the recorded approach
eye/aim once, and never forces a camera mode or selected candidate.

The synthetic wall test verifies the route timer, a relocation with no swept-sphere path, and
spare-block restoration. The real-data trapdoor regression verifies the mode change, aim reset,
and return to mode 0 within six units below the floor. The corridor replay enters mode 1 and
returns to normal following. Ordinary Learn_10 route differences are at most 0.018 units in
two-decimal traces; the Jail_20 introduction and a playable follow route match at that precision.
Commands, measurements and remaining limits are in `build/helpers/cam13_report.md`. Visual
checks should cover the first-visit trapdoor, the corridor corner and Rodeo_10's reported area.

In the user's original corner recording (`build/helpers/camera-wall-original.txt`), mode 1 began
near Rayman (-36.38,191.67,-1.42), returned to mode 0 near (-20.76,196.12,-2.94), and was then
4.34 units away. In `build/helpers/rec_camsnap/camera.txt`, the trapdoor's behaviour 8 returns to
0 at 3.922 s, mode 1 begins at 3.937 s, and at 4.047 s the eye is
(-26.824,291.108,-22.921), 5.90 units from Rayman. Mode 0 resumes at 4.312 s. These recordings
constrain the behavior but do not identify which query or collision triggered each transition.

The user also reports that the view shows more scenery from the same place than the original. The
distance clamp above uses the live block's min and max distances, but that alone does not establish
where the original camera actually settles. OpenRayman originally used a fixed 60-degree vertical
field of view in both game and fly views. Block +0x30 is a pitch-window centre, not a projection
field; the earlier field-of-view identification was wrong. Both camera position and projection need
measuring before attributing the difference to either one. The user's matched corridor screenshots
(2026-10-30) visibly show Rayman smaller in OpenRayman, though the views are not perfectly aligned.
An earlier helper report identified globals 0x5004F4..0x500500 as four projection floats. A live
read by the user instead yielded the bit patterns for integers 0, 1199, 0, 1599, consistent with
viewport bounds; do not use them as focal length or clipping planes. The call at 0x419A47 passes
them through 0x421FB0 to `GLI_DRV_vClearZBufferRegion` (the function pointer is resolved by name
at 0x421880). The actual projection code at 0x422AB0 computes the angles and focal factor in the
renderer record, and 0x42C8A0 uses that factor to project vertices. `tools/r2live.py --camera` reads
the current record and the camera's position for comparison. A live read in Learn_10 at the
corridor reported perspective kind 103, horizontal angle 1.479 radians and vertical angle 0.900
radians. The two angles at record +0x64/+0x68 are used by the perspective setup at 0x422AB0;
OpenRayman's game view uses the measured 0.900-radian vertical angle pending checks in other scenes
and display configurations. The free-fly camera retains its own field of view. A second live read
with the camera actor's standard-object link corrected gave Rayman at (-37.449, 184.421, 0.015),
the camera at (-32.520, 190.093, 4.158), and their distance as 8.581 units while standing still.
An equivalent OpenRayman measurement at the same position is still needed.
The user reports the new framing looks good (2026-10-30). The reader also estimates camera speed
from the change in world position divided by elapsed game-clock time; missed frames give net
displacement rather than the full path length. This measures motion, not the camera's speed setting.
In the user's 2026-10-30 run (`build/camera-corner.txt`), Rayman moved from roughly
(-64.45, 210.64, -8.38); in the early straight section the camera reached roughly 9–10 units/s,
with its distance rising from 8.58 to about 9.56. After Rayman stopped it returned to about 8.59.
The run also contains direction changes and short speed spikes; the reader samples a live process
without synchronizing its reads with the game, so individual spikes are not reliable acceleration
measurements. Compare a matched OpenRayman run before drawing conclusions about its speed.
The subsequent OpenRayman recording (`build/openrayman-camera.csv`) follows a similar but not
identical path: at nearby positions in the shared stretch its camera was often 10.9–11.7 units
from Rayman versus 8.9–9.4 in the original. At the end, both settled near 8.6. This supports the
user's report of extra lag while moving, but the routes and timing differ, so it does not identify
whether the speed law, obstacle search, or another camera behavior causes it.

## Target tracking

The exe keeps eight samples of the target's movement and position (camera object +0x78 vectors,
+0xD8 points, +0x138 weights). Procedure 444 (0x47DA40) shifts in the target mechanics report's
movement (+0x114), position (+0x14C), and elapsed time in seconds. The mechanics writes these when
end flags 0x4 and 0x4000 are both set (0x431AD0). The weighted average of movement (0x414420,
0x473570) contributes minimum position and aim speeds at 0x484B50; the average of positions
(0x414550, 0x483E40) also affects the aim point. OpenRayman now records and uses these at its fixed
60 Hz step. Its report movement is approximated by the speed it writes for scripts; the executable
derives its sample from the mechanics matrices. Confirm the resulting movement against the
original, including cuts and other levels. Own analysis of these executable routines and the
user's 2026-10-30 live and OpenRayman traces.

At the Learn_10 hand-over, a scripted cut puts the camera close to the destination before
Rayman arrives. Our target history can still contain his old position: following it undid the
cut on the next tick. When the camera is already close to Rayman but the history is far away,
we reseed that history and do not apply our teleport snap. In the headless trace the camera then
moves from (33.66, 289.73, -5.52) to (33.67, 289.77, -5.44), versus run4's next recorded point
near (33.82, 289.59, -5.28). This is our approximation, not a decoded executable reset; confirm
it visually and on other teleports. The executable clears samples when the target changes
(0x47DA40, 0x413E30), independently checked in its decompilation.

## What our engine leaves out
- Menu: node 7 places itself on waypoints selected by node 6's menu behaviour, and nodes 8..10
  and 12..16 can set camera overrides when their zones contain it. The fixed-position mechanics
  correction above needs a visual comparison with the original across menu entries.
- **Complete camera collision queries**: sector selection, actor sphere/box occluders and collision-set
  filtering are still approximated in the obstacle search. Camera mechanics resolves static and actor
  triangles in separate passes; it does not reproduce every combined contact manifold.
- **The cutscene modes** (cam+0x60: mode 4 snap, mode 5 target switch)
  and the global tuning block 0x4FF62C.. beyond what procedures 491..493 store.

The portal startup exposed a limitation of our teleport cut: its destination is calculated from
the old camera direction, and may lie beyond a wall even though it is near the target. When that
destination is occluded, our cut now searches rotated positions at the same and shorter distances,
checking line of sight and the camera sphere. This is an approximation, not the original arrival
pan. Own analysis of 0x485810 confirms the startup reset enters mode 4; 0x487730 handles a separate
obstacle transition. The available recordings do not show a return from the Hall of Doors.

## Camera collision (0x434660)

After each tick's movement the exe moves the camera's collision sphere from where it was to where
the tick put it (0x434390 casts it, skipping collide flags 0xC000: water and 0x8000) and slides it
along what it meets: it stops 0.02 short of the surface and carries the rest of the movement along
it, along the crease when it meets a second surface, at most five times, and stops when the slide
turns more than 150 degrees (0x49C720) from the movement. A camera that starts inside a surface is
pushed out first. Block +0x70 bit 0x40 turns this off. Afterwards the per-tick turn carry is the
angle the camera really swung round the look-at point. The Fix camera's sphere is the first zone
of its kind-1 list (actor +0x14 → +8): radius 0.35 round its origin.

Ours (`world_collision::slide_sphere`) finds the first touch by stepping a quarter of the radius at
a time and halving, rather than casting, and skips the collision for a snap (the level-start cut),
which the exe places by resetting the camera instead of moving it.

## What we do differently (for now)

- **The empty current block.** The game fills it from the default block when it makes the camera
  object; our loader leaves the zeros the file has, so procedure 444 copies the default block into
  it the first time it finds it empty. Without that the camera would have a speed of 0 and a
  distance of 0.
- **The target also goes into the current block.** Procedure 467 writes only the override block in
  the exe, and the override is refilled from the current one each frame, so a target survives only
  while a script names it every frame. Our level scripts (the camera controllers) name it only
  every few frames, so 467 stores it in the current block too.
- **A fresh camera snaps.** When the camera state has not been placed yet (or the target changed),
  procedure 444 sets bits 0 and 2 of the block's +0x70 word before the solve, which our mechanics
  takes as "snap the position": the camera lands on its wanted position in one frame instead of
  easing there at `speed` units a second. This stands in for the exe's mode 4.
- **The zone test does not check the standard game flag.** Condition 10 tests whether an actor is
  inside a zone; the exe first requires bit 2 of the actor's standard game +0x29, which the engine
  sets when it puts the actor into the world (0x40614F). Our engine never sets it (Rayman's byte is
  0x0A), so the test would hide every actor and every camera controller. The flag is not checked.

## Where this came from

Obstacle search: own reading of Rayman2.exe at 0x4870C0, 0x4863D0, 0x485FD0, 0x486400,
0x486D50, 0x486880, 0x486710, 0x4864D0, 0x487520, 0x487730 and 0x485810, with
0x486540/0x486840 (scoring/sort), 0x4862E0 (extended view), 0x486EA0/0x486F40 (relocation),
0x485180/0x4875D0 (waypoint hold/progress), 0x4876E0 (restore), 0x486FA0/0x4873B0
(additional triggers), 0x434390/0x434660 (actor contacts and report bits), 0x473480 (relocated aim),
0x4845E0 (slope correction), and 0x4138B0..0x413A06
(default tuning). The score's x87 stack expressions, yaw-loop count, comparator order and
0x4862E0's negative extension constant were independently checked in capstone. Descriptions and
C++ are our own; no executable or decompiled code is included. The two user's camera recordings
above supply behavioral evidence; the headless results and remaining discrepancies are in the
cam13 report.

Jail_20's chute-end rig selects animation 354 and sends the world positions of its animation
channels 1 and 0 to procedures 495 and 496. Own analysis of its node 2 scripts and the executable
handlers 0x478AA0 (function 210, part world position) and 0x47EC20 (camera vectors), compared with
the user's run4 recording. OpenRayman evaluates the animation pose on demand when the executable's
part-object table is absent; it does not construct that table.

Own analysis of `Rayman2.exe` with capstone, following the scripts from the level data. The
addresses are the ones above:

- the camera object and its four blocks: 0x4140A0 (back the current block up), 0x4140D0 (save the
  spare), 0x414100 and 0x414120 (restore), 0x413E30 (reset the camera's runtime scratch);
- the procedures' handlers: 0x47DDE0 (445, 446), 0x47DEB0 (447..466), 0x47E200 (467), 0x47E670
  (472..480), 0x47E830 (481..490), 0x47EA70 (491), 0x47EBD0 (492, 493), 0x47EC20 (495..497),
  0x47EDD0 (499), 0x47EF20 (500), 0x467CF0 (159), 0x488CB0 (function 310), 0x479BE0 (functions
  186..188), 0x4785B0 (functions 190, 191), 0x489D60 (conditions 10..14);
- swing validation and collision report: 0x485760, 0x4855D0, 0x4853C0, 0x486400, 0x485FD0,
  0x474270 and 0x434660 (own executable analysis; the static-ray approximation is ours);
- the solve: 0x485810, with 0x483F90 (aim and wanted position), 0x4840A0 (offset point), 0x484540
  (look-at), 0x4847D0 (distance clamp), 0x484390 and 0x484470 (the pi/8 turn), 0x4858F6 (the reset
  branch and mode 4);
- the mechanics: 0x4359D0, 0x433B00 (step towards), 0x433CD0 (turn a frame), 0x435930 and 0x435640
  (publish), 0x434660 (sphere slide and head-on report bit);
- procedure 444's body: 0x47DA40, which calls 0x474270 (build the context), 0x485810 and 0x474420
  (apply to the camera object).

The zone structures the camera controllers use (an actor's collision set at +0x14, its zone lists
and 2-bit states, and the point-in-zone test 0x48F3B0 with zones of spheres, boxes, triangles and
capsules) were read from 0x417BC0, 0x417C10, 0x40C700, 0x40C960, 0x40D290, 0x490510, 0x4915E0,
0x492250, 0x48F3B0 and the handler 0x489D60, checked against the data of Learn_10's camera
controllers (nodes 33, 34, 35, 64, 65 and 77).

The numbers in the field table are the default block of the camera actor in Learn_10
(Fix block 04/00, offset 0x13558, 0x7C bytes).

## Cutscene view boundaries

Fix actor 6 animates the black bars in its scripts. Variables 58 and 57 request the small
and large openings; 59 withdraws them and takes precedence. Variable 60 moves at 0.4 per
script millisecond to 195 or 325.417. Procedure 314 (handler 0x46BD40, view update 0x404E70)
sets view 1's top, left, bottom and right in thousandths: top is trunc(amount * 0.23076923),
bottom is 1000 - trunc(amount). Small bars end at 45 and 805. OpenRayman paints opaque
rectangles over the world and before screen overlays, controlled by a default-on renderer bool.
It currently stores only view 1 and draws the top and bottom boundaries.

Provenance: own reading of the Learn_10 actor 6 scripts, Rayman2.exe at 0x46BD40 and
0x404E70, and the user's live intro and Globox recordings of the world view at 0x500458.
