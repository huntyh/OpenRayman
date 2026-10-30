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
**The fast mechanics.** With end flag 0x200 (base +0xC; the mechanics' bit 0x100, 0x435B80) the
tick takes 0x431AD0's first branch instead: move = speed (+0x3C, world axes) × dt, added by
0x437A40; the imposed speed, if sent, replaces the axes it names (0x43E350), a queued move (base +8
bit 0x20000000) is applied (0x43E870), and collision runs only with flag 0x2. No gravity, inertia
or floor. A nonzero speed also keeps the idle test (0x437BB0) from skipping the tick. Rayman's shot
(mechanics 05/01 +0x62A454: kind 1, flags 0x300) flies this way: procedure 182 sends (25, 0, 0)
once and the stored speed carries it on (see [generated.md](generated.md)). Base flag 0x80 (the
mechanics' bit 0x200) only makes contacts use the actor's own coefficients at +0x1C/+0x20 instead
of the touched material's (0x4360B0, 0x436130). Ours moves such an actor by its speed alone.

Procedure 160 (0x46AB20 case 0xA0) moves the actor by a vector (world axes when its second argument
is set, else its own) as a queued move (0x432D30), which clears the speed unless base +0xC has 0x80.

## What we do differently (for now)

- Procedure 297 (climbing over a ledge, 0x46C710 case 0x129) sets end flag 0x400000, flags 0x182,
  +0x1C = 1, +0x20 = 0 and the height +0x1EC; 298 (0x46CE00 case 0x12A) clears 0x400000 only. With
  0x400000 the exe's collision (0x43EEC0 → 0x4377F0) leaves out floor contacts and contacts below
  the height; flag 0x80 uses +0x1C/+0x20 as the contact response, 0x100 keeps a move's vertical part
  against suitable walls (0x437E60). Ours: while 0x400000 is set, the floor isn't searched and
  walls only push from the height up, so the ledge's face doesn't hold Rayman back (without it he
  ended on the very edge, user report); 0x80 and 0x100 aren't used yet. When an actor's state changes, its mechanics are installed in our
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

## Holding on between pillars, and walls that can be held

- **The Learn_10 pillars** (seen live with tools/r2live.py): an invisible trigger, node 34, sets
  Rayman's var61 = 1 while he is inside its zone (condition_10), var62 = its own forward made
  horizontal and unit, var64 = its position; leaving the zone sets var61 = 0. Jumping there (macro
  48 from macro 33) starts behaviour 11 with reflex 9: reflex 9 turns him onto the line's nearer
  direction (function 186 folded into ±90°, procedure 136 when over 1°, then procedure 81) and puts
  him on the line through var64 along var62 (procedure 157) every frame. He drops (state +0x1210,
  macro 4) when var61 goes 0. The pillar walls carry no collide flags (0x0); the hold mechanics
  has flags 0x3 (animation motion, collision; no gravity, no inertia).
- **Procedure 136** (0x4697D0): turns the actor about the vertical by degrees (× 0x49CBF0 =
  π/180, 0x432EE0 on the dynamics' next matrix), not while end flag 0x80 is set. **Procedure 81**
  (0x467770 case 0x51): the running intelligence starts its script over; flow 0xB.
- **End flag 0x80** means the transform was set from outside: 0x431AD0 takes the actor's transform
  as it is, zeroes the speed and clears the flag. The level data starts Rayman with it, so a turn
  before his first mechanics step would be refused.
- **Procedure 157 is a move, not a teleport**: 0x432D30 queues it (flag 0x80000000, +0x6C) and the
  dynamics still collides it, so walls stop the hold at the wall joining the pillars. (The
  teleport 0x432DA0, procedure 200, resets both matrices and isn't collided.)
- **Walls that can be held** (base flag 0x40 without 0x10: resolver 0x43A260; mechanics such as
  @05/01 +0x601944 with flags 0x43, Rayman's behaviour 27, entered by macros 29/45 when a wall has
  collide flag 0x40 and function 201 finds another 0x40 surface opposite): the move loses its part
  along the held normal (+0x1CC); the nearest steep 0x40 surface within H (0.75 with end flag 0x10,
  else |move| × tan 60°, at least 0.45; 0x49C7B8, 0x49C7B0, 0x49C7A0) whose normal · Y ≥ −0.707
  (0x49C760) is held; 0x4394A0 turns him to it (Y = n, X = n × up, Z = X × n, position + 0.9 × (old
  Z − new Z), 0x49C79C); none left sets end flag 0x10000, which condition 80 (0x48B380) reads and
  clears. Ours looks with a sphere at body height instead of sweeping his zones; Learn_10 has no
  0x40 surfaces, so this is untested in play.
- **Function 201** (0x479910): function 198 with a material mask: only surfaces sharing a collide
  flag with it count (0x4B9C44, tested at 0x48E820); returns the hit point or zero.

## Where this came from

- The pillar hold: node 34's and Rayman's scripts (`ortool brain`), a live log of the real game,
  procedures 136/81 and the wall resolver traced by GPT 6.1 Terra through Codex and checked here.

- The animation motion (flag 0x1, +0x54, 0x412060, 0x40FF90, 0x41A830) and procedures 297/298
  were traced by GPT 6.1 Terra through Codex and checked against the exe and the Learn_10 climb.

Own analysis of `Rayman2.exe` with capstone. Two studies by a helper model (DeepSeek) found the
functions. The gravity formula, the mechanics copy (0x435B80, read in full), the install after a
state change and procedure 263's float conversion (0x4825B0) were checked by hand against the
disassembly. The rest, marked by address above, follows the helper's notes and has not been
checked again.
