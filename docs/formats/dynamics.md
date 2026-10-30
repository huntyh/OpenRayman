# Dynamics: how actors move each tick

Every actor with dynamics has a base block at `[[actor+8]]` (0x419B90). Its state picks a
*mechanics* (state +0x24), and the base copies it when the state changes. Each tick, the scripts'
speeds and gravity give a target position. The floor stops it, and what happened is left for the
scripts to read on the next tick.

## The base block

| Offset | What |
|---|---|
| +4 | current mechanics |
| +8 | flags: 0x4 gravity, 0x200 per-axis speed limit, 0x10 / 0x20 / 0x40 the three floor resolvers; for each script speed one bit per axis it sets and a world-axes bit: imposed 0x20000/0x40000/0x80000 and 0x4000, proposed 0x100000.. and 0x8000, third 0x800000.. and 0x10000 (0x46CCB7, 0x46CCE1, 0x46CD0B) |
| +0xC | more flags: 0x8000 no gravity |
| +0x10 | gravity, units/s² downwards (procedure 263; 9.81 by default, Rayman's scripts use 25, 4, 30) |
| +0x14, +0x18 | slope limit (tangent) and its cosine |
| +0x24, +0x30, +0x190 | script speeds (procedures 176/182 and 177..181 per axis, 188/194 and 190 the forward walking speed alone, 201/207), units/s |
| +0x3C | the actor's speed from the last tick; fields 2/3 read it |
| +0x150 | contact block: +0 this tick's contact word, +4 the word conditions 27..32 test (1 on the ground, 0 nothing) |
| +0x178 | per-axis speed limit (procedure 274) |

## The mechanics

A mechanics has a u32 kind at +0, flags at +4, gravity at +8 and the slope at +0x14. Installing one
(0x435DF0) sets base +4. For kind 1 it also copies the mechanics into the base (0x435B80):

- bits 0x1..0x40 keep their place in base +8;
- bits 0x200..0x8000 move to 0x80..0x2000 of +8;
- bits 0x80, 0x100 and 0x20000 move to 0x100, 0x200 and 0x100000 of +0xC.

When base +0xC has 0x2 or 0x4, more values are copied as well.

The state procedures (72..75, 0x472345) install the new state's mechanics after a change, and the
per-actor entry (0x412150) installs the current state's every tick.

## One tick (0x431AD0)

0. The animation's own motion, if flag 0x1 (the state mechanics' bit 0x1, 0x435B80): target +=
   the actor's rotation × base +0x54 × dt (0x437A40, the vector turned at 0x444AC0). The animation
   update fills +0x54 (0x412060 via 0x40FF90): each frame record (10 bytes, the animation's frames
   from its +0x2E) names a vector at u16 +2 in the bank's vector array (0xFFFF none); the frames
   played this tick (after 3D data +0xC8 up to the current +0x74) are averaged and multiplied by
   the playback rate (3D data +0xE0), giving units per second in the actor's axes. Rayman's ledge
   climb (state @04/00 +0x1FA4) rises about 1.8 units and steps onto the ledge this way.
1. target = position; then the second script speed sets the axes it names (× dt) of the move.
   Inertia, if flag 0x400 (0x43DF70 -> 0x43DDA0): per axis in the actor's own axes (formula below),
   last = speed (+0x3C) × dt (minus +0x1B4 unless end flag 0x10), f = 1 − (1 − k)⁴ from k at +0x154.., the share of last tick's move kept: move = wanted + (last − wanted) × f (the state
   mechanics' +0x18.., 0x4370A0; a pow correction applies when dt ≠ 1/60). Flag 0x20 with end flag 0x20000 does
   the same with +0x168.. × +0x174 (procedure 292 writes +0x168..+0x170). Rayman: k = 0.4, 0.4, 1.
2. Gravity, if on (0x43DF40): target.z = z + dt·(last speed z − g·dt).
3. The first and third script speeds replace the axes they name (0x43E350, 0x43E42A; a jump is procedure 179 on z, overriding gravity while it is sent) (0x43E4E0), then the per-axis
   limit is applied (0x43EAC0) in the actor's own axes: the move is turned into them (0x444AC0 with the inverse matrix, 0x444D60), each component clipped to limit × dt, and turned back.
4. speed = (target − position) / dt (0x436A30). Landing drops it.
5. Flags are masked with 0x5C003FFF, and the script speeds +0x24..+0x38 are cleared (0x4327D9).
6. The contact word +0 is copied to +4 (0x412201 and others).

The time step is the frame length rounded to the mechanics step 0x50043C (n steps per tick).

## What we do differently (for now)

- Procedure 297 (climbing over a ledge, 0x46C710 case 0x129) sets end flag 0x400000, flags 0x182,
  +0x1C = 1, +0x20 = 0 and the height +0x1EC; 298 (0x46CE00 case 0x12A) clears 0x400000 only. With
  0x400000 the exe's collision (0x43EEC0 → 0x4377F0) leaves out floor contacts and contacts below
  the height; flag 0x80 uses +0x1C/+0x20 as the contact response, 0x100 keeps a move's vertical part
  against suitable walls (0x437E60). We store these but don't act on them yet: the climb in
  Learn_10 works without them. When an actor's state changes, its mechanics are installed in our
  dynamics step, after the scripts, so a 297 in the same tick loses 0x182 (the exe installs them at
  once, 0x472345).

The exe finds contacts with the actor's collision spheres and picks the nearest. We treat the actor
as a disc 0.5 units across its middle (Rayman spawns 12 cm off the grass edge in Learn_10) and look straight down for the highest triangle that faces up (at most 60° from flat),
following the floor up to 0.3 units down while grounded. Water surfaces aren't floors. Walls stop
the actor too, as a cylinder: a triangle steeper than 60° (`|normal.z| < 0.5`, not water) that meets
the height from 0.5 (the step it can climb) to 1.8 above the target pushes it horizontally out, along
the line from the triangle's nearest ground-plane point to the cylinder's axis (the triangle's own
normal when the axis is inside it). The cylinder looks up every 4-unit cell it covers, and three
passes let two walls meet in a corner. The push happens before the floor search, so the floor under
where the actor ends up is the one it lands on, and the drop in the target makes the speed the scripts
read back fall to zero. The cylinder's radius is the actor's first kind-1 zone sphere (collide set +8,
the list the exe's sphere cast gathers: 0x494290 -> 0x417870), 0.5 when it has none; Rayman's is 0.8
(centred 0.8 above his feet). The exe uses the zones the scripts switched on (0x40DBD0); the first one
stands in until those are read. An actor a script placed this tick (its position isn't where the
mechanics left it, kept at base +0x7C like the exe's previous matrix; the exe's teleport resets both
matrices, 0x432DA0) isn't pushed by walls that tick: the exe sweeps along the move, and a held actor
doesn't move. The horizontal speed the scripts read is the move asked for, less what went into a
wall, not where the push left it (0x436A30 runs before the collision step): the real game pushed
Rayman 0.3 off the portal's wall in one frame with a sideways speed of -0.07.
A wall within the radius + 0.05 sets contact bit 0x4 (condition 28) and contact slot 2 (function 190(2))
to the push's direction and the point; Rayman's reflex macro 40 bounces his push (var87) off that normal,
scaled by max(0, n · push direction), which is what ends the Learn_10 portal hop (seen in the running
game with tools/r2live.py: one frame after the release var87 is zero). Ceilings and moving supports are still not handled. Rayman's mechanics has
none of the resolver bits, only 0x2, so 0x2 also counts as colliding until his resolver is read.

## Where this came from

- The animation motion (flag 0x1, +0x54, 0x412060, 0x40FF90, 0x41A830) and procedures 297/298
  were traced by GPT 6.1 Terra through Codex and checked against the exe and the Learn_10 climb.

Own analysis of `Rayman2.exe` with capstone. Two studies by a helper model (DeepSeek) found the
functions. The gravity formula, the mechanics copy (0x435B80, read in full), the install after a
state change and procedure 263's float conversion (0x4825B0) were checked by hand against the
disassembly. The rest, marked by address above, follows the helper's notes and has not been
checked again.
