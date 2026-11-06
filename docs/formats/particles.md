# Particles

Small sprites that live for a moment on their own: the specks floating up in The Woods of Light's
dark places, sparks, dust. Scripts start them with **procedure 503**, the effect spawner. The
engine then moves, fades and draws them without the scripts. (The exe also has a second particle
system, generators attached to an actor part, but no actor in Learn_10 uses it.)

## Procedure 503

`procedure_503(kind, point, vector, game material, number)`, handler 0x463390. The kind picks the
effect: index = kind − 1 (0..0x42) into a byte table at 0x464864, which picks a case from the jump
table at 0x4647CC. What the point, vector and number mean depends on the kind. Some kinds draw on
the screen instead (the scripts' HUD: kind 125 with screen coordinates, 110 a full-screen overlay).

Kind 125 writes a persistent screen-image slot (0x4605A0, drawn by 0x460600). Learn_10's HUD
uses animated material frames for the Lum and a still material for the cage. The Lum texture has
mirror-U tiling: the stored half is reflected to form the full image. Where this came from: own
analysis of node 44's script, Fix material and texture records, and the executable.

Particles go into a pool (0x4E5800, 0x48 bytes each, 150 of them, added by 0x4600C0, at most 35
per frame). Every frame 0x4608B0 drops finished ones, then moves each by its type (0x461120, a
switch on the low byte of its flags) and draws it as a camera-facing square with the game
material's texture, faded by its alpha (0..255). Time is in seconds (0x4B9AFC now, 0x4B9B04 the
frame's length).

**Kinds 170 and 171, a tiled picture** (0x4648B0 cases 0xAA/0xAB → 0x465850, 0x465870; drawn by
0x4658B0 from 0x460670, after the kind-125 images, only while its opacity is above 0.01). Kind 170
puts the material in tile `number` (0..17, the array at 0x4E1020). Kind 171 sets the corners
(point and vector x, y, in viewport percent, 0x4E1600) and the opacity (the number, 0..255,
0x4B9B10, given to 0x42A300 as /255). The tiles are six columns of three between the corners,
tile = column × 3 + row, each column top to bottom. The exe rounds the corners and tile sizes
down to whole pixels: width × (x1 − x0) / 600 (0x49CBC8) and height × (y1 − y0) / 300
(0x49CBCC). Jail_10 node 4 spawns actor `04/01 +0x680` with var3 = 0, which loads `logo_00` …
`logo_52` into tiles 0..17 and draws the Rayman 2 title between (15, 20) and (85, 60) as its var9
rises and falls. Ours keeps the picture in the centred 4:3 area at wide sizes (IMPROVEMENTS).
Where this came from: own analysis of the decompile and the disassembly of 0x4658B0.

**Kind 28, rising specks** (0x463DE8 → 0x462C50). Node 4 of Learn_10 calls it every frame with the
camera actor's position and forward axis (−Y), the level's `plancton` material and 0.5:

- One speck every 0.05 s at most, however often scripts call it (one clock for all calls).
- Start: 8 units ahead along the vector, ±4 across, 4 to 5 units below.
- Speed: ±0.2 across, 2.5 to 4.5 units per second up. Size: number × 0.20..0.25.
- Fades in at 350 alpha per second from 5, stays at 255 for 0.5 s, fades out at 260 per second
  (about 2.2 s in all, so about 44 at a time).
- Drawn circling its position: radius ±1.5, 1 to 4 radians per second, offset
  r·(sin a, cos a, sin a + cos a).

**Kind 23, a gathering twinkle** (0x4639D7). One spark up to a unit away from the point on each
axis, 0.10 to 0.15 in size, that glides into the point (each axis closes 6.5 times its gap per
second, snapping within 0.1) and vanishes when it gets there. Node 3 of Learn_10 calls it every 4
frames at a waypoint.

**Kind 120, gauges** (0x4648B0 → 0x460530, eight slots of 0x20 bytes at 0x4E0F20, painted by
0x460670 through 0x429670 before the kind-125 images). Slot = the number; corners = point and
vector x, y in screen percent; fill percent = point.z; opacity = vector.z (an integer, 0 hides).
Wider than tall, the bar material fills from the first corner to x0 + (x1 − x0) × fill / 100;
taller, it fills up from the bottom. Below 100 the rest is painted with the track material, the
one the last **kind 50** named (0x463390 case 0x32 keeps it at 0x4B9ADC, with the point, vector
and number for other effects). Learn_10's node 44 draws Rayman's health bar in slot 0 (macro 2:
kind 50 with 05/01 +0x625E60, then kind 120 from (12, 8.5, percent) to (12 + 0.46 × maximum hit
points, 11.5, opacity) with 05/01 +0x625FD4, `jauge_in_horizont`); the opacity rises to 200 and
blinks while the shown value lags the real one or Rayman has under 5 hit points. Where this came
from: a DeepSeek reading of node 44's scripts and the exe, checked by us against the decompiled
0x460530, 0x460670 and 0x463390 and the constant 0x49CA18 (0.01).

**A breaking cage's burst** (Learn_10 node 67's break rule, the same in every cage of that model):
kinds 5, 9 and three times 45, at the cage's origin minus (0, 0, 1).

- **Kind 5, smoke** (0x461B10): four puffs, flags 0x1103 (drift, grow 1.3, fade 100), 0.6 above
  the point and offset by (0, ±0.5, 0.8 / 1.5 / −0.5 / 0); speeds (1, 0.56, −0.53),
  (−0.5, −0.98, 0.23), (0.5, 0.16, 0.13), (1, −0.29, −0.63); sizes 0.8, 0.5 × 0.6, 0.3, 1.0.
  Each also stores a random 0.7..2.0 at pool +0x38 that only the draw (0x460A10) reads; not used
  by us yet.
- **Kind 45, golden sparks** (0x463390 case 0x2D): nine sparks, flags 0x2501 (fall, shrink 0.5,
  fade 160), each axis' speed random within ±2n sideways and −0.2n..6n up, size 0.16n..0.8n,
  living n seconds (n = the number, 2 for the cage). The third material is the animated golden
  sparkle, so those end with their animation.

The game's Lum appears 500 ms after the cage sets bit 840 (node 69 waits, then shows node 70), so
the burst covers that wait. Where this came from: a DeepSeek reading of the scripts and the exe,
checked by us against the decompiled 0x461B10 and 0x463390 and the float constants in the exe.

**The water effects** (Rayman's macros 36, 37 and 47). They use a flag word (pool +0): the low
byte is the type (0 stays put, 1 falls at 7 units/s² and moves, 3 moves), bits 0xF00 how fast
it grows (0x100 1.3, 0x200 3.7, 0x300 6.5 units a second), bits 0xF000 how fast it fades (0x1000
100, 0x2000 160, 0x3000 260 alpha a second), 0x10000 dies after +0x3C seconds (0x4607E0), 0x20000
turns into a ripple when it falls to the height at +0x3C, 0x10000000 is drawn flat on the ground
(0x429B20) and 0x8000000 standing on its point. Sizes are half widths (+0x30) and heights (+0x34).

- **Kind 7, a ripple** (0x46348E): flags 0x10003300, starting at the vector's Z in size and its X
  in alpha (over 300 becomes 250), at most one every 0.2 s (one clock, 0x4B9B0C). It also
  remembers its material (0x4B9AD8) for kind 60's drops. Walking in water starts one whenever
  macro 37's counter passes 64, a landing (macro 47) one more.
- **Kind 39, a splash** (0x464188): flags 0x8001100, the vector's Y × 0.5 big.
- **Kind 60, drops** (0x462460): ten, thrown ±3 across and 2 to 6 up, 0.03 to 0.05 big, flags
  0x21001, with the point's height as the floor. A drop reaching it becomes a 0x10003100 ripple
  of size 0.1 and alpha 250 there.
- **Kind 62, foam** (0x462580): ten specks drifting flat out at ±1 a second for 1 to 3 seconds,
  0.01 to 0.10 big, flags 0x10003.
- **Kind 9, a burst** (0x463569): a flash (0x4500: stays, shrinks 0.5 a second, fades 350 a second)
  0.5 to 0.8 big, and eight sparks (0x3001: fall, fade 260) flying ±2 a second sideways and -0.2
  to 6 up, 0.19 to 0.35 big. The Learn_10 portals start three when they open.
- **Kind 55, motes** (0x46469A): vector.y + 1 of them (0x3003: drift, fade 260), each axis's speed
  random within ±number, vector.x big, +0x3C = 1. The spinning portal asks for (0.2, 1, 0) with
  number 2 every frame: two motes a frame, its swirl of sparkles.
- Growth 0x500 shrinks by 0.5 a second; a size reaching 0 is kept at 0.01 and the particle gets
  0x1000000 (0x461120). Falling (type 1, and 4) is 3.5 twice a tick: 7 units/s² (0x461164,
  0x461176; a helper read it as 14, which the constant 0x49C9EC = 3.5 rules out).
- **Animated materials play once.** When a particle's material has an animated texture, the
  draw (0x424C00, with 0x4A7274 and 0x4A7278 set by the particle draws) starts it at the
  particle's birth (pool +4) and kills the particle when the last frame ends. The splash (kind 39,
  the "bigplouf" frames) lasts that long, about 0.7 s, long before its fade would end it. The exe
  scales frame times by a runtime value (0x5036A8) we take as 1, which matches the user's timing.
- **Kind 167** (0x4648B0 case 0xA7) sets a switch (0x4B9AF8) while its number is over 0.01: then
  kind 60 starts nothing, kind 7 uses 0x10003100 and kind 62 lives 0.2 to 1 s.
- **Kind 175, sparks drawn into a point** (0x4648B0 case 0xAF): nothing if the number is 0 or the
  two points coincide. Twelve particles of type 0x12, 0.05 to 0.30 big (rand × 0.25 / 32767 + 0.05),
  each starting number × 0.5 from the vector's point. The direction mixes two axes square to the
  line between the points (0x4656E0 picks a perpendicular and crosses it with the line) with a
  random -8..8 of each per axis, then is normalised. At birth (0x4600C0) the type stores
  (point - start) × 1/9 (0x49CA10) as its step and zeroes +0x3C; every frame (0x461120) it moves one
  step and counts +0x3C up by 1, and at 8 (0x49CA40) it is marked done: eight frames covering 8/9 of
  the gap. The Learn_10 portal (node 77, behaviour 1, while Rayman is within its var2) calls
  `procedure_503(175, field_0, main actor.field_0 + (0, 0, 1), material, 3)` every frame: the sparks
  streaming from Rayman into it. We take which perpendicular 0x4656E0 picks as the largest of the
  three plain ones (only the random disc's orientation depends on it).

## Where this came from

- Own analysis of `Rayman2.exe` with capstone: the spawner, the kind table, the pool, the per-frame
  update and draw, kinds 23 and 28, and the water kinds 7, 39, 60, 62 and 167, the portal's 9, 55 and 175 (0x463390, 0x4648B0, 0x4656E0,
  0x4600C0, 0x461120, 0x460A10, 0x4607E0, 0x462460, 0x462580, read from the Ghidra decompile). Parts were first traced by helper models (DeepSeek and Qwen through Reasonix, kind 175
  by GPT 6.1 Terra through Codex) and then checked against the exe in this project: every address and constant
  above was re-read.
- Which scripts call it, and with what, from `ortool brain` (Learn_10 nodes 3 and 4).
