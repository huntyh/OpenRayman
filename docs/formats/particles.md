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

Particles go into a pool (0x4E5800, 0x48 bytes each, 150 of them, added by 0x4600C0, at most 35
per frame). Every frame 0x4608B0 drops finished ones, then moves each by its type (0x461120, a
switch on the low byte of its flags) and draws it as a camera-facing square with the game
material's texture, faded by its alpha (0..255). Time is in seconds (0x4B9AFC now, 0x4B9B04 the
frame's length).

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

## Where this came from

- Own analysis of `Rayman2.exe` with capstone: the spawner, the kind table, the pool, the per-frame
  update and draw, kinds 23 and 28, and the water kinds 7, 39, 60, 62 and 167, the portal's 9 and 55 (0x463390,
  0x4600C0, 0x461120, 0x460A10, 0x4607E0, 0x462460, 0x462580, read from the Ghidra decompile). Parts were first traced by helper models (DeepSeek and Qwen,
  through Reasonix) and then checked against the exe in this project: every address and constant
  above was re-read.
- Which scripts call it, and with what, from `ortool brain` (Learn_10 nodes 3 and 4).
