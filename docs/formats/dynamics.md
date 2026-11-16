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
| +0x1B4 | last carry displacement, left out of inertia unless end flag 0x10 |
| +0x284 | supporting actor's super object, or 0 (0x43D950 / 0x43DA00) |

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
   the same with f(+0x168..) × f(+0x174), each factor corrected the same way: 0x4370A0 always
   applies 1 − (1 − k)⁴, its second argument only adds the frame-time pow (procedure 292 writes
   +0x168..+0x170). Rayman sliding (Jail_20 chute): 0.65, 1, 1 and 0.98 keep 98.5% of the sideways
   speed a tick, which is why the script's large sideways push (procedure 189) only bends the slide
   slowly (r2live, 2026-11-07; Ghidra's decompile hides the 1 − (1 − k)⁴ step, read with capstone).
   Rayman: k = 0.4, 0.4, 1.
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

The resolver's initial search direction is not simply the current floor triangle's normal:
when end flag 0x4000 supplies a nonzero contact normal and end flag 0x10 is clear,
0x43B63C uses that normal; otherwise it uses world up. At 0x43B6B7 it removes the
component of the proposed displacement along that direction for the search. This is only
one stage of the resolver, not a complete movement rule. Own inspection of the installed
Rayman2.exe with capstone, 2026-11-08.

At 0x436A30 speed is the difference of the target position at +0xD4 and the reference
position at +0x7C, divided by the step time. The contact response at 0x437E60 can
modify that reference position before this calculation, so the final displacement from
the actor's original position is not always the speed-producing displacement. Own
inspection of the installed executable and capstone at 0x436A30, 2026-11-08.

- The flag 0x20 resolver (0x431A10 -> 0x43B360) projects the proposed displacement onto a
  previously reported contact plane while searching for contacts, then resolves the actual
  contacts and recomputes speed (0x436A30). Ours approximates a sloped floor contact with gravity
  and end flag 0x20000 by removing the into-plane component, `d - min(d·n, 0)n`, and keeping the
  projected speed including its vertical component. Contact search, corrections and substeps are
  not yet implemented here. Material response coefficients come from 0x4360B0/0x436130:
  flags 0x1/0x2 select the respective default coefficient 1 instead of 0, explicit material
  data can override them, and base flag 0x80 selects the actor's coefficients instead. In the
  Ly_10 live trace, behaviour 38 copies the last speed to var87, and Rayman accelerates downhill;
  the flag 0x20 resolver is also used in ordinary movement. Ours uses material flag 0x1 or the actor
  override for the contact inertia coefficient, but not explicit material response data. Procedures
  291/292 write the per-axis factors (`dynamics_triple` in `handlers.cc`). Source: own analysis of Rayman2.exe
  and the live log.

  In the recorded Ly_10 slide the ground has collide flag 1 and Rayman's base has flag 0x80,
  so the actor's coefficient overrides the material default. In a teleported reproduction our
  actor's coefficient is 0.98. The executable sets end flag 0x20000 from the ground coefficient
  independently of updating +0x174: 0x432828 updates +0x174 from the first contact only when end
  flag 0x2 is set, whereas our approximation assigns it from the ground. Explicit response data
  is selected when the game material's +4 pointer is nonzero (0x42DE30); its values in the
  executable's contact record are at +0x58 and +0x5C (0x4360B0/0x436130). These differences and
  the contact geometry still need to be resolved. A teleported test accelerates near the recorded
  rate but has a different sideways velocity and starting posture, so it does not reproduce the
  reported slowdown conclusively. Source: own executable analysis and the Ly_10 live trace;
  measurements are in `build/slope_accel_notes.md`.

- Procedure 297 (climbing over a ledge, 0x46C710 case 0x129) sets end flag 0x400000, flags 0x182,
  +0x1C = 1, +0x20 = 0 and the height +0x1EC; 298 (0x46CE00 case 0x12A) clears 0x400000 only. With
  0x400000 the exe's collision (0x43EEC0 → 0x4377F0) leaves out floor contacts and contacts below
  the height; flag 0x80 uses +0x1C/+0x20 as the contact response, 0x100 keeps a move's vertical part
  against suitable walls (0x437E60). Ours: while 0x400000 is set, the floor isn't searched and
  walls only push from the height up, so the ledge's face doesn't hold Rayman back (without it he
  ended on the very edge, user report); 0x80 and 0x100 aren't used yet. State procedures install
  the requested state's mechanics immediately, before subsequent script calls (0x472345).

The exe finds contacts with the actor's collision spheres and picks the nearest. We treat the actor
as a small disc (Rayman spawns 12 cm off the grass edge in Learn_10) and look straight down for a triangle that faces up (at most 60° from flat), preferring the actor's origin and using rim probes when it has no support,
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
game with tools/r2live.py: one frame after the release var87 is zero). Ceilings and actor surfaces now use the same
triangle search; moving supports are described below. Rayman's mechanics has
none of the resolver bits, only 0x2, so 0x2 also counts as colliding until his resolver is read.

## Standing on actors and moving supports

Actor triangles join the scenery queries in `move_actor`; the highest acceptable floor across
both wins. The live state, part animation and actor transform are read for each movement step,
so moving or hiding an object changes its collision immediately. See
[collision.md](collision.md#actor-surfaces) for the sources and the Learn_10 trapdoor evidence.

The exe handles carrying through the scene hierarchy. At the end of full dynamics (end flag
0x4), 0x43D820 checks the selected actor contact and its supporting normal. A support is eligible
only when its standard-game byte +0x28 is 1 (0x413730); the rider must not itself have that value.
0x43D950 records the support at base +0x284 and reparents the rider under it. 0x43DA00 clears it
and returns the rider to the dynamic world. Base flag 0x40000000 forbids changing the parent.
When a support moves, 0x43D7B0 visits its riders and 0x43DA80 updates their transforms using the
support's change of frame. A rider stays upright unless base flag 0x1000 allows tilting.

OpenRayman's actors already store world transforms, so it keeps a support placement alongside
each rider instead of changing the scene links. Its own affine transform calculation applies the
support's translation and turn since the rider's previous movement step to the rider's position
and orientation, then resolves the move against scenery and actor surfaces. Landing on an
eligible actor records the relationship; leaving the surface, disabling its geometry, removing
it or teleporting the rider releases it. Static actors such as Learn_10 node 66 (byte +0x28 = 0)
can still be floors, without carrying the rider sideways as they move out from under it.

Carry displacement is kept at base +0x1B4, and the reported speed includes it. Inertia subtracts
that displacement before retaining last tick's motion (0x43DDA0); our vertical gravity step also
leaves it out, to avoid continuing a stopped platform's vertical movement. The installation flag
0x10, which temporarily bypasses that subtraction, clears after one tick (0x4327D9's mask at
0x4327EB). Synthetic regression coverage includes an off-centre rider, horizontal and vertical
translation, a 90-degree turn, inertia without drift, and loss of support.

Remaining differences: contact resolution still uses our disc/cylinder approximation rather
than swept collision zones and substeps. Carrying is applied when the rider next steps, whereas
the exe updates children when the support steps; actor order can therefore produce one tick of
lag. Receiving spheres/boxes/capsules, scenery-only segment casts and ledge probes, explicit part
super-object collision overrides, and real playthroughs on later levels' moving platforms remain
to be checked. The full transform-delta carry is our implementation of the observed hierarchy
behavior, not a reproduction of the exe's dynamics matrices.

Where this came from: own analysis of Rayman2.exe at the addresses above; the support pointer,
eligibility byte, transform updates and tick mask were checked with capstone. Learn_10 geometry,
scripts and the run4 recording independently confirm the closed height and sideways opening.

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

  Where this came from: own analysis of the GOG executable at 0x432DA0 and the
  Learn_10 director script (level offset 0d/01 +0x1443C), which uses procedure
  200 when returning the stand-in's position to Rayman.
- **Procedure 172's turn about a point is a move too**: 0x469CB0 (case 0xAC) turns the actor and
  shifts it so the given point keeps its place, then 0x469C70 hands that to the dynamics through
  0x4329B0 (unless end flag 0x80): base flag 0x20000000, the new axes at +0x128..+0x148, and the
  shift (new position − target +0xD4) at +0x6C, which the next step collides. Unlike 0x432D30 it
  keeps the speed. Rayman's slide turns this way every tick (about a point 0.9 above his feet).
  Ours had written only the transform, so our mechanics took every slide tick for a teleport and
  skipped the walls; Rayman then went through the overhanging lips at the top of the Jail_20
  chute's sides and fell out. Live (r2live, 2026-11-08, no steering): the real slide is caught by
  those lips (normals about (−0.1, 0.95, −0.3)) and loses its sideways speed in one tick (end flag
  0x2000000 for that tick, 0x43B360's "a contact was resolved" bit), then slides back down; speed
  caps at 40 (the limit), and it reaches the end trigger about 14 s after the slide starts.
  *Where this came from: own analysis of the decompiled 0x469CB0/0x469C70/0x4329B0/0x43B360, checked
  against the live slide; after the fix our no-input slide stays within about 8 units of the real
  path and stops at the end trigger.*
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

## Initial floor settling (2026-11-07)

The Jail_20 recording settles Rayman before the scene freezes mechanics. Our
point cast previously stopped short of the nearby floor on that first step,
leaving a second descent at hand-over. An external placement (end mask 0x80)
now permits nearby support within the existing follow-down distance when
descending. Rim probes select a support triangle; its plane is evaluated at
the actor origin, preventing the rim height from lifting him on a slope.
This improves our disc approximation rather than implementing the original
sphere solver. See [jail20-handover.md](jail20-handover.md) for measurements,
original procedure 159 behavior, and provenance.

**Where this came from (2026-11-08):** read-only Ghidra analysis of Rayman2.exe function
238 at 0x47A660 confirms that its vector result uses the current actor's dynamics gravity,
the supplied endpoints, and an apex at destination height plus the requested height; if that is below the start, it uses start height plus the requested height. Negative height or absent/zero
gravity returns zero. Learn_10's node 40 uses it for the opening jump, but the jump's live
inputs and the mechanics after procedure 182 were then investigated as described below.

**Where this came from (2026-11-08, intro 23c):** the procedure table at 0x4A43E0 has eight-byte entries; entry 182 at 0x4A4990 points to 0x46CBD0. The state procedure installs mechanics during the script call (0x472345, 0x435DF0 and 0x435B80), before a following function 238 reads gravity. In Learn_10, delaying that install until movement made the ballistic function see zero gravity and return a zero vector. The resulting trace first diverged at tick 1013; installing the state mechanics immediately produces an airborne arc. The disc floor remains an approximation.

**Where this came from (intro 23c review):** read-only Ghidra inspection of 0x472290, 0x435DF0, 0x435B80 and 0x412150 shows the pointer comparison is by mechanics address, not kind. A kind-1 install copies the specified flags and parameters; it does not directly overwrite speed, second-inertia factors at +0x168 or the contact coefficient at +0x174. Reproducing Jail_20 with immediate installs reveals a regression whose first divergence is earlier than the slide: after a state transition at 86000 ms, a subsequent script sets gravity to 40; the deferred implementation instead installs the playing state later and leaves gravity 25. This changes the approach trajectory. The subsequent intro 23d investigation found that the altered trajectory exposed the rim-probe floor-selection problem described below; the full executable resolver is still not implemented.

**Where this came from (intro 23d):** the Jail_20 headless reproduction with immediate state mechanics and the recorded gravity 40 reached a side of the chute where a rim probe selected a steep surface while the actor's origin was still over the floor. Its extrapolated plane gave an upward velocity and led to loss of contact. Preferring the origin's support, with rim probes as fallback, keeps the sampled slide grounded through ticks 5300–6099 and reaches the end sequence. This is a correction to our disc approximation, not a claim that the executable uses point probes; its swept sphere resolver remains to be implemented. The headless path and the real live recording were compared, but the chute still needs a visual check.


## Bast_20 thrown barrels (2026-11-15)

Node 40 behaviour 11 creates template `04/01+0x1F04` (model `0d/01+0x5144`),
not the different projectile from behaviour 9. Its state `04/01+0x228C` selects
mechanics `05/02+0x77440`, flags `0x1A06`: gravity and collision, without fast
mechanics. The script sets gravity to 30 and calls function 238 with height equal
to the positive launch-to-target drop plus 4. At 0x47A731 the executable first
sets apex = target.z + height; only if that is below start.z does it replace the
apex with start.z + height. Adding height to the higher endpoint unconditionally
double-counts a downward drop. This was verified against the installed executable
and the actor scripts with ortool. Collision still uses our unswept disc/cylinder
approximation; a fast barrel can tunnel through thin level geometry.

The other projectile, template `04/01+0x8B0`, uses state `04/00+0x1551C` and
mechanics `05/01+0x62A454`, flags `0x300`, gravity zero, with fast mechanics and
no scenery collision bit. Procedure 182 sets world-axis components once;
0x43E350 replaces those components and 0x431AD0 carries the resulting speed.

## Swept basic actor collision (card 127, 2026-11-16)

Sources: own read-only inspection of the installed GOG executable's decompilation
in `H:/r2export/re/decomp`, functions listed below; `handler.py condition 6`
and `handler.py function 184`; local installation data inspected with `ortool
peek` for Bast_20. No executable implementation or game assets are included here.

Dispatch at **0x431AD0** is significant. End flag `0x200` selects fast movement,
which only calls **0x431960** when base flag `0x2` enables collision. Ordinary
movement prefers base flag `0x10` (**0x439730**), `0x40` (**0x43A260**), `0x20`
(**0x431A10**), end flag `0x100` (**0x43D440**), then flag `0x2`
(**0x431960**). Thus neither speed nor actor identity enables collision by itself.

**0x431960 -> 0x439660 -> 0x43EEC0 -> 0x494290** passes previous/target
matrices to collision. **0x494610** expands the broad-phase bounds by the move,
visits the current sector and its collision neighbours, and gathers scenery and
actor surfaces. **0x494420** includes selected receiving kind-3 geometry and
physical parts; both feed **0x4990F0 -> 0x499070 -> 0x49A690**. The mover's kind-1
zones are selected by **0x40DBD0** from the current state group, with 2-bit script
overrides (kind 1: group at collide set `+0x2C`, overrides at `+0x38`, confirmed
by **0x417BC0/0x417C10**). These are list-node/selection properties, not an
extra word in each sphere: **0x499A80** iterates **12-byte** sphere entries
(radius, game material, vertex index), transforms each vertex with both
matrices, and forms its displacement. **0x49AA20** tests triangle features;
**0x491D50** supplies swept edge intersections. This is continuous zone travel,
not an overlap check at the destination. Radius-zero casts are supported by our
query, but original radius-zero behavior has not been established separately.

**0x43EEC0** filters report-only probes/water and special material contacts;
**0x437520/0x437780** remove invalid/away-facing contacts. **0x43FF20** handles
coincident edge normals. **0x437E60** selects the first valid contact, computes
correction (**0x43FA40**) and the response over the remaining travel fraction.
It adjusts the reference position before **0x436A30** recomputes speed. Default
tangent/rebound coefficients come from material bits `0x1/0x2`, otherwise zero;
explicit response data can override them, then base flag `0x80` selects actor
coefficients `+0x1C/+0x20` (**0x4360B0/0x436130**).

**0x43F510** classifies the selected contact by normal.z and base `+0x18`:
ground bit `0x1`/slot 1, wall `0x4`/slot 2, ceiling `0x10`/slot 5. **0x43F940**
publishes first-contact slot 0 and category slots to the script report. Entries
carry normal, surface point, own/hit materials and receiving actor. No contact
clears the simple resolver's end flag `0x10000` (**0x439660**); base end flag
`0x4000` gates report publication. The `0x2000000` resolved bit belongs to the
specialized slope path; it is not fabricated for this simple sweep. End flag
`0x20000` reflects nonzero ground tangent response at the end of `0x431AD0`.

OpenRayman's initial rollout selects **basic dynamics** (end flag `0x2`, without
full dynamics `0x4`) with base collision bit `0x2`, no specialized resolver
bits `0x10/0x20/0x40`, and no end flag `0x100`. It gathers every sphere in the
selected kind-1 zones, keeps the earliest sphere/triangle hit across scenery and
actor surfaces, stops the actor at that travel fraction, and publishes the
contact and response speed. Candidates reuse the existing 4-unit cell grid over
the swept bounds; each triangle is tried once per sphere, with analytic face,
edge and vertex roots, no speed-dependent substeps. Actor surface reach also
covers the destination so a surface more than 10 units from the start can stop
a long move. Disabled zones are honored. Full dynamics, including Rayman's
walking, jumps, slide and ledge logic, keeps its existing approximation.

Bast_20's gravity barrel `04/01+0x1F04`, state `04/01+0x228C`, uses mechanics
flags `0x1A06` and basic dynamics (initial end flags `0x4083`): it is in this
rollout. Its selected sphere has radius 0.6, centered at the local origin (confirmed
by the generated-barrel regression test against the installation data). The cited
`04/01+0x8B0`, state `04/00+0x1551C`, has fast mechanics flags `0x300`, without
collision bit `0x2`: it still flies without mechanics scenery collision, as
shown by the executable dispatch. Function **184** at **0x47C350**, case `0xB8`,
is vector length; its condition-6 distance check does not enable collision.
`ortool brain` for this template confirms behavior 0 computes var3 with function
198 from its position along normalized var2 times 100, then imposes speed once.
Behavior 2 tests distance to that precomputed point (<1), the separate distance
to its target actor, and a 4500 ms lifetime; it does not test scenery contact
report bits. Its intended live pre-cast/proximity behavior still needs checking.
Rayman's non-colliding fast shots likewise retain their path.

Remaining differences: first-contact stopping is implemented; a nonzero response
retains the response velocity for the following tick rather than recursively
consuming the remaining tick against more surfaces. Explicit material response
records, initial deep penetration correction, relative motion of receiving actors,
rotation/scale changes between the two transforms, receiving spheres/boxes/capsules,
and the full/specialized resolvers are not implemented by this change. Water keeps
its previous query path rather than gaining continuous sensor reporting. Visual
verification of the bottom-right pirate throw remains outstanding.

## Swimming ceiling and pivot motion (card 118b)

Sources: own analysis of the installed PC Rayman2.exe, read-only decompiles
`H:/r2export/re/decomp/00435b80.c`, `00437e60.c`, `0043eec0.c`,
`004329b0.c`, `0043e900.c`, `0046ab20.c`; float constants checked with
pefile against the original PE on 2026-11-16. No original code or assets included.
Learn_30 node 90's behaviour 5 and swim states were inspected through `ortool brain`
and live state/mechanics pointers. The user's OpenRayman recording
`rec_20261116_001354/rayman.txt` supplied the failing trajectory, not original-game evidence.

Mechanics bit `0x20000` installs dynamics end flag `0x100000` (0x435B80).
The swim states use this bit. In 0x43EEC0 a water contact records the surface Z
in global 0x4B72D4. At 0x438DB1 within 0x437E60, this flag rejects a candidate
Z greater than water Z minus **1.2** (float at 0x49C78C): it restores the previous
Z and removes the vertical response. This is a mechanics constraint; behaviour 5
continues swimming at the surface. It does not require a behaviour exit when contact
bit 0x40 disappears. Water remains a separate report category (0x43F510/0x43F940,
slot 4, bit 0x40), not a solid floor for other mechanics. OpenRayman's query includes
the swept body's vertical interval so an upward step cannot skip the water plane.

Script pivot shifts must also pass through collision. 0x4329B0 stores the new
rotation and a translation at dynamics +0x6C, setting base flag `0x20000000`;
0x43E900 adds that translation after the script speeds, before speed limiting
and collision. Applying the translation directly as the mechanics' starting
position lets a swimmer tilt his body upward through the ceiling. OpenRayman
keeps the script-visible transform, removes the pending pivot translation when
starting mechanics, and includes it in the colliding move. Procedures 172 and
167 use this path; it preserves the turn's pivot without treating it as a teleport.

Learn_30's swim behaviour calls procedure 346 in **mode 1**, computes var48
from movement fields 15/16, then explicitly turns with procedure **167**
(0x46AB20 case 0xA7). Like 166, it rotates about a local axis; its third
argument is a local pivot whose world position is preserved. The missing handler
was why the swim script could not steer. Mode 2's disabled automatic turning
in 0x46E450 is intentional and belongs to other movement, including behaviour 27.

Regression: real Learn_30 data drops Rayman into the recorded river region,
uses Z/Up then A to exercise submerged ascent, checks that he remains near
z=-9.54 with surface z=-8.339, and that Right changes his heading. The drop
also checks there is no water report or swim entry while his feet are above
the water plane. Exact visible entry timing against original Rayman 2 remains
unverified; physical/report zones and rendered body position need a real-game comparison.

## Learn_31 climbing script survey

Rayman's Learn_31 node 75 has a separate macro 52 (0d/00 +0x16504) for a
contact whose slot 5 material has collide flag 0x20: condition 64 (0x48B000)
checks the contact, and function 201 (0x479910) probes for the same flag from
1.85 units above Rayman along minus 0.95 times contact normal 5. The script
then selects behaviour 30 or 31 and reflex 5; behaviour 31 calls procedure
190 with a speed scaled by -2.4. This describes the script, but its relation
to the tree ladder at camera (-210.7, 371.8, 30.9) is not yet verified: the
level's eight static 0x20 triangles are far from that camera. Nearby scenery
contains steep 0x40 triangles, which may use the separately documented behaviour
27 instead. Sources: `ortool brain` for Learn_31 node 75, `ortool collide`
and `ORTOOL_NEAR`, and handler addresses checked using the Rayman2.exe handler
mapping.

The camera at (-210.7, 371.8, 30.9), yaw 22.3 and pitch -7, first meets
Learn_31 scenery node 84 at distance approximately 8.39. Its game material
(0x07a81fc8 after relocation) points to a collide material with flag 0x40.
Rayman's macro 29 (0d/00 +0x14470) and macro 45 (0d/00 +0x15E00) test that
flag in wall contact slot 2 with condition 62 (0x48B000), then cast five units
opposite contact normal 2 using function 201 (0x479910), mode 2 and mask 0x40.
Macro 21 selects behaviour 27 and reflex 8. Our full-dynamics approximation had
not copied the wall's game material into slot 2, so that test could not pass.
The cylinder resolver now reports the selected triangle's material, point,
normal and owning actor; its contact selection is still an approximation to the
executable's zone sweeps. The hold probe uses the actor's tilted local Z for
its centre, consistent with keeping the 0x4394A0 pivot in place. With a
partway-up placement, Up enters behaviour 27 and increases height in our
real-data replay; several floor-level approaches still stop at an unflagged
surface and need investigation. Sources: the Learn_31 scene, collision and
Rayman scripts via `ortool`, contact replay and own analysis of the executable
at 0x48B000, 0x479910 and 0x4394A0. This is not a live-game verification.
