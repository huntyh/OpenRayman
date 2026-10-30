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
     the block's +0x72 bit 4 is set. We leave out the collision test (every candidate passes), the
     "try the other way" when it fails, and the cam+0x168 / option-bit-1 block.
4. **What the mechanics gets**: the movement (wanted − current position), the look-at, the offset
   point, the back direction, the +0x70 option word and the nine speeds and eases from +0x38..+0x58
   (the ramps are clamped to at least 0.001).

When the target changes, procedure 444 resets the camera object's runtime scratch (0x413E30) and
procedure 444 records the target in the global 0x4B9C14. A fresh camera — one that has not moved
yet — takes the "mode 4" path of the solve (0x4858F6): its block is reset to +0x70 = 0x55 and
+0x72 = 0x0A, and cam+0x60 = 4, a one-frame snap to the target.

## The mechanics (0x4359D0)

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

### Obstacle search: investigation in progress

The original's mode-0 follow path (0x4870C0) periodically tests the route between the target anchor
and the camera (0x4863D0 -> 0x485FD0). When that test reports an obstruction and the other gates
pass, it checks the desired position (0x486400) and asks 0x486D50 for a different one. That function
builds candidates (0x486880), sorts them by a score, and tests them with 0x486710 and 0x4864D0.
An accepted candidate enters a separate transition mode through 0x487520, which resets the swing
direction and records both the current position and the candidate. The obstruction check can sample
several offset paths around the line (0x485FD0); it is not simply the camera sphere's wall slide.
OpenRayman now has an interim search: when the target-to-camera segment meets level geometry or
the camera is within 1.25 times its collision radius of a surface, or its proposed follow path would
come within that clearance of a surface,
it tries candidate positions in 16 directions at three distances, preferring its current distance
from the target before falling back to shorter distances, and choosing the candidate closest to the
desired follow position at each distance with a clear line to the target and a collision-free sphere movement
from the camera. It first prefers candidates with the same wider clearance, falling back to the
physical collision radius if the view is blocked and no wider candidate fits. This clearance is an
OpenRayman tuning choice prompted by the user's corner feedback, not a measured executable value.
It keeps that waypoint across ticks and retries every four ticks when it finds no
candidate; it discards the waypoint when the target moves two units, or the camera comes within
0.5 units. This is our own approximation, not a reconstruction of the original's candidate scoring
or transition. It is skipped for fixed-position/vector modes, cuts and disabled camera collision.
The user tested the interim search through the Learn_10 corridor near `-19.8,195.1,0.1` and is
happy with it for now (2026-10-30); exact candidate generation, scoring, timing and transition
remain to be validated.
In the user's original-game corner recording on 2026-10-30 (`build/camera-wall-original.txt`),
the camera entered mode 1 around Rayman (-36.38, 191.67, -1.42), returned to mode 0 near
(-20.76, 196.12, -2.94), and was then 4.34 units from him. A later mode-1 transition was brief.
This confirms that the original can move substantially closer through this route; the trace alone
does not prove which obstruction caused each mode change.

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

The exe keeps eight samples of the target's movement and position
  (camera object +0x78 vectors, +0xD8 points, +0x138 weights). Procedure 444 (0x47DA40) shifts in
  the target mechanics report's movement (+0x114), position (+0x14C), and elapsed time in seconds.
  The mechanics writes these when end flags 0x4 and 0x4000 are both set (0x431AD0). The weighted
  average of movement (0x414420, 0x473570) contributes minimum position and aim speeds at 0x484B50;
  the average of positions (0x414550, 0x483E40) also affects the aim point. OpenRayman now records
  and uses these at its fixed 60 Hz step. Its report movement is approximated by the speed it writes
  for scripts; the executable derives its sample from the mechanics matrices. Confirm the resulting
  movement against the original, including cuts and other levels. Own analysis of these executable
  routines and the user's 2026-10-30 live and OpenRayman traces.

## What our engine leaves out
- **The original's orbit search around obstacles** (0x486880, with 0x486710 deciding whether a step
  may run): our interim candidate search does not yet reproduce its scoring or transitions.
- **The collision test of a swing-round step** (0x486400) and the "try the other way" that follows
  a blocked one: every step passes.
- **The cutscene modes** (cam+0x60: mode 1 scripted keyframes, mode 4 snap, mode 5 target switch)
  and the global tuning block 0x4FF62C.. beyond what procedures 491..493 store.

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

Own analysis of `Rayman2.exe` with capstone, following the scripts from the level data. The
addresses are the ones above:

- the camera object and its four blocks: 0x4140A0 (back the current block up), 0x4140D0 (save the
  spare), 0x414100 and 0x414120 (restore), 0x413E30 (reset the camera's runtime scratch);
- the procedures' handlers: 0x47DDE0 (445, 446), 0x47DEB0 (447..466), 0x47E200 (467), 0x47E670
  (472..480), 0x47E830 (481..490), 0x47EA70 (491), 0x47EBD0 (492, 493), 0x47EC20 (495..497),
  0x47EDD0 (499), 0x47EF20 (500), 0x467CF0 (159), 0x488CB0 (function 310), 0x479BE0 (functions
  186..188), 0x4785B0 (functions 190, 191), 0x489D60 (conditions 10..14);
- the solve: 0x485810, with 0x483F90 (aim and wanted position), 0x4840A0 (offset point), 0x484540
  (look-at), 0x4847D0 (distance clamp), 0x484390 and 0x484470 (the pi/8 turn), 0x4858F6 (the reset
  branch and mode 4);
- the mechanics: 0x4359D0, 0x433B00 (step towards), 0x433CD0 (turn a frame), 0x435930 and 0x435640
  (publish), 0x434660 (occlusion, left out), 0x486880 and 0x486710 (orbit search, left out);
- procedure 444's body: 0x47DA40, which calls 0x474270 (build the context), 0x485810 and 0x474420
  (apply to the camera object).

The zone structures the camera controllers use (an actor's collision set at +0x14, its zone lists
and 2-bit states, and the point-in-zone test 0x48F3B0 with zones of spheres, boxes, triangles and
capsules) were read from 0x417BC0, 0x417C10, 0x40C700, 0x40C960, 0x40D290, 0x490510, 0x4915E0,
0x492250, 0x48F3B0 and the handler 0x489D60, checked against the data of Learn_10's camera
controllers (nodes 33, 34, 35, 64, 65 and 77).

The numbers in the field table are the default block of the camera actor in Learn_10
(Fix block 04/00, offset 0x13558, 0x7C bytes).
