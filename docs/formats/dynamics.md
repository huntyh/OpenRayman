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

1. target = position; then the second script speed sets the axes it names (× dt) of the move.
2. Gravity, if on (0x43DF40): target.z = z + dt·(last speed z − g·dt).
3. The first and third script speeds replace the axes they name (0x43E350, 0x43E42A; a jump is procedure 179 on z, overriding gravity while it is sent) (0x43E4E0), then the per-axis
   limit is applied (0x43EAC0).
4. speed = (target − position) / dt (0x436A30). Landing drops it.
5. Flags are masked with 0x5C003FFF, and the script speeds +0x24..+0x38 are cleared (0x4327D9).
6. The contact word +0 is copied to +4 (0x412201 and others).

The time step is the frame length rounded to the mechanics step 0x50043C (n steps per tick).

## What we do differently (for now)

The exe finds contacts with the actor's collision spheres and picks the nearest. We treat the actor
as a disc 0.5 units across its middle (Rayman spawns 12 cm off the grass edge in Learn_10) and look straight down for the highest triangle that faces up (at most 60° from flat),
following the floor up to 0.3 units down while grounded. Water surfaces aren't floors. Walls stop
the actor too, as a cylinder: a triangle steeper than 60° (`|normal.z| < 0.5`, not water) that meets
the height from 0.5 (the step it can climb) to 1.8 above the target pushes it horizontally out, along
the line from the triangle's nearest ground-plane point to the cylinder's axis (the triangle's own
normal when the axis is inside it). The cylinder looks up every 4-unit cell it covers, and three
passes let two walls meet in a corner. The push happens before the floor search, so the floor under
where the actor ends up is the one it lands on, and the drop in the target makes the speed the scripts
read back fall to zero. Ceilings and moving supports are still not handled. Rayman's mechanics has
none of the resolver bits, only 0x2, so 0x2 also counts as colliding until his resolver is read.

## Where this came from

Own analysis of `Rayman2.exe` with capstone. Two studies by a helper model (DeepSeek) found the
functions. The gravity formula, the mechanics copy (0x435B80, read in full), the install after a
state change and procedure 263's float conversion (0x4825B0) were checked by hand against the
disassembly. The rest, marked by address above, follows the helper's notes and has not been
checked again.
