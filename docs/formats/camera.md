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
| +0x30 | | 0.35 | copied to the camera object's +0x208 (the field of view) |
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
     direction is first turned by at most pi/8 a frame towards the angle limits in +0x28..+0x34
     (0x484390, 0x484470). Then the height is clamped into +0x60..+0x64.
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

The block's `+0x20` settle distance belongs to the other follow path (mode 0, 0x4870C0): the camera
is "close enough" when the distance between the current and the desired position is under it, and
0x486710's orbit search uses 1.25 as the epsilon at which a direction counts as settled.

## What our engine leaves out

- **The eight tracked points.** The exe's solve averages up to eight points on the target, each
  with its own weight (camera object +0x78 vectors, +0xD8 points, +0x138 weights; procedure 159
  starts them). We use the target's position only; for every game camera the points start at that
  position and only some cutscenes move them.
- **The pi/8 direction turn** (0x484390, 0x484470) that walks the back direction towards the angle
  limits in the block. The wanted direction is the one the camera already lies in from the aim point.
- **The orbit search around obstacles** (0x486880, with 0x486710 deciding whether a step may run):
  the exe tries directions around the target when the straight one is blocked. Our camera takes the
  straight one.
- **The occlusion pass** (0x434660): the exe casts from the aim point to the camera and pulls it in
  when scenery is in the way; it also overwrites the per-tick turn carry with 0 or another angle.
- **The cutscene modes** (cam+0x60: mode 1 scripted keyframes, mode 4 snap, mode 5 target switch)
  and the global tuning block 0x4FF62C.. beyond what procedures 491..493 store.

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
