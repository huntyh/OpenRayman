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

## Where this came from

- Own analysis of `Rayman2.exe` with capstone: the spawner, the kind table, the pool, the per-frame
  update and draw, kinds 23 and 28. Parts were first traced by helper models (DeepSeek and Qwen,
  through Reasonix) and then checked against the exe in this project: every address and constant
  above was re-read.
- Which scripts call it, and with what, from `ortool brain` (Learn_10 nodes 3 and 4).
