# Collision

What the level and the actors collide with. The same geometric objects as the visible meshes
([geometry.md](geometry.md)), reached through a second pointer of the physical object.

## Where it is

**Physical object** +0x04 → **collide set**: four geometric object pointers (+0x00, +0x04, +0x08,
+0x0C), then a u32 (0x0F, 0x0D seen: not decoded).

- Static level geometry (IPOs) uses only the last slot, +0x0C: the mesh the level collides with.
  In all 70 levels 3663 IPOs have one: 2808 reuse the visible mesh, 855 have their own (invisible
  walls, simpler floors). 1147 IPOs have no collide set and collide with nothing.
- Actors' physical objects mostly have an empty collide set (in Learn_10 465 of 474 objects). The
  few filled slots hold spheres and similar shapes (element types 7 and 9) in slots 1 and 2, and
  triangles in slot 3. Actor zone lists and their state-selected groups are described below.

The IPO's super object also has a world box for quick rejection: super object +0x34 points to two
corners (min, max: 6 floats) when flag 0x10 of +0x30 is set (Learn_10's IPOs have flags 0x14).

## Elements

The exe's collision tests (0x48E820 cast a segment through a geometric object, 0x48EE70 overlap
test) switch on the element type: 1 triangles, 7 and 8 other shapes; other types are skipped. Static
level collision is triangles only (8856 elements in all levels; type 3, sprites, only occurs in
meshes shared with the visible geometry and is skipped).

Type 1 triangles are read like visible ones: +0x00 game material, +0x04 u16 count, +0x08 three u16
vertex indices each, +0x10 a face normal each. Geometric object +0x18 points to an octree the exe
uses to find candidate triangles quickly (not read: OpenRayman can build its own).

## Surface types

**Game material** +0x0C → **collide material** (null for most triangles: 78080 of 102755 in all collision meshes, actors included, over all
levels). **Collide material** +0x02 = u16 surface flags. Scripts test them with condition 67 ("the
surface touched has all these bits"); the engine also has a global mask (0x4B9C44) that makes
segment casts only hit surfaces with some of the given bits.

Bits in use, by triangle count over all levels: 0x0004 (10213), 0x0001 (5857), 0x0800 (2321),
0x0100 (1115), 0x0040 (1037), 0x4000 (524), 0x8000 (281), 0x0002, 0x0008, 0x0020, 0x0200, 0x0400,
0x1000, and some combinations (0x0408, 0x1001, 0x4400, 0x8200, ...).

Confirmed in the game (Learn_10, by comparing the collision overlay with what Rayman does):

| Bit | Meaning |
| --- | --- |
| 0x0004 | grabbable: Rayman hangs from the edge of such a surface (the ledge by the Globox children, the tops of the columns near the end); he doesn't grab plain edges. Whole floors carry it, and also some ceilings, where it does nothing |
| 0x4000 | water |
| 0x0008 | on an actor's own zone: a ledge probe. Rayman's are four spheres (radius 1) 2 units ahead, behind and to each side at 1.9 up; only the front one is in use (its entry's last word is 5, the others 2) |

## Actor zones and ledges

An actor's kind-1 zone list (collide set +8) holds spheres (element kind 7; entry: radius, game
material, u16 vertex index, a word 5 for zones in use, 2 for the others). Their materials' collide
flags say what each is: Rayman's body 0x4001 (radius 0.8, centred 0.8 up), water sensor 0x4000,
ledge probes 0x8. The dynamics contact report's slots hold, besides the touched surface's game
material (+0x20), the actor's own zone that touched (+0x1C); conditions 34..37 test its collide
flags (0x48A4F0: 34 slot 1 with word bit 0x1, 35 slot 2 with 0x4, 37 slot 5 with 0x10, 36 none of
them has the flags). In the real game (tools/r2live.py) a ledge grab starts on the frame the front
probe touches the **top** of a grabbable (0x4) floor: word 0x1, slots 0 and 1 with the probe as
+0x1C, the ledge as +0x20, its up normal and the touch point 1.0 from the probe's centre. Rayman's
macro 3 then finds the edge with segment casts (functions 198, 199) in a script loop (procedures
76..78) and puts him at the hanging point (procedure 157); behaviour 37 checks he got there and
hands over to 32 (hanging). OpenRayman tests the probes in use against grabbable triangles only, in
the air while descending. This falling check is an approximation based on the desired gameplay;
the original game's mechanism for preventing grabs on the way up has not yet been established.
Done: the grab and the climb. The climb is animation-driven (procedure 297 sets flags 0x182
and a target height at +0x1EC; flag 0x1 adds base +0x54 each tick, 0x437A40); procedure 298
clears only 0x400000. Procedure 218 (0x46AB20) is the hang state machine, called every frame
while hanging (`procedure_218(var42, type18 0x2, 0)`) — it steps Rayman's script and decides
bail-back-to-idle, keep hanging, or go up for the climb.

## State-selected zone groups

An actor's collision set (actor +0x14) has four internal zone kinds. In kind order 0, 1, 2, 3,
the zone-list pointers are at +0x00, +0x08, +0x04, +0x0C; the per-state collection pointers at
+0x10, +0x18, +0x14, +0x1C; the selected group pointers at +0x20, +0x2C, +0x24, +0x28; and the
2-bit zone-state words at +0x30, +0x38, +0x34, +0x3C. A group contains a linked list of zone
indices (u16 at each node's +4). State 1 enables a zone, state 0 enables it only if the selected
group contains its index, and states 2 and 3 disable it (0x40C700).

On installing a state, 0x40C9F0 counts its position in the family's linked state list and takes
the entry at that position from each kind's collection. Each entry has a next pointer and a group
pointer at +4. All four selected pointers are updated; an absent collection selects null
(0x417B60). Own analysis of Rayman2.exe with GPT-6.1 Terra.

## Water

Water triangles (0x4000) take no part in the floor and wall resolve: the movement solvers skip them
(0x43EEC0, 0x43F510), so actors sink through. Instead a water entry whose **both** elements carry
0x4000 becomes a contact of its own: bit **0x40** of the dynamics contact report (`base +0x150`,
+4; +0 is the tick's own), with the entry in **slot 4** (`report + 0x08 + 4*0x28`, its normal at +4,
the surface point at +0x10, the game material at +0x20; 0x43F940). Rayman's scripts test it with
condition 32 (bit 0x40) and condition 65(`function_197(4)`, 0x4000), and read the surface with
`function_192(4).z` (the water height) and `function_190(4)` (its normal, which they push splash
particles along). OpenRayman does the body side of this (`game/src/mechanics.cc`:
`world_collision::water_at`, slot 4 in `move_actor`). Segment casts skip both 0x4000 and 0x8000
(0x43441C); whether the body resolves against 0x8000 is unconfirmed (we treat it as no contact).

Deep or shallow is the scripts' own call. When Rayman comes down into water, his macro 36 casts
from just under the surface straight down for 1.1 times his body's height (zones 5 and 6 of his
kind-1 zone list: `function_166(n)` is a zone's first vertex or box corner, `function_175(n)` its
radius or box extent; 0x4754D0, 0x474D40) with `function_198` (0x479720, the segment cast 0x474780:
first point met, 199 its normal). Ground within that reach means he stands in the water; none means
he swims (macro 64, behaviour 9 with var8 = 8, then behaviour 5). Our cast only meets the level's
solid triangles (water and 0x8000 let it through): the cast also tries actors, but only those with
standard game +0x29 bit 4, which our engine never sets.

The table above is the confirmed set; the other bits are still unknown.

## Ceilings

Surfaces facing down (normal Z at most -0.5) are solid from below: the Learn_10 bridge by the
waterfall is a slab 0.6 thick whose underside (z -6.8, normal straight down) stops a jump from
under it. OpenRayman stops a move up where the top of the body (1.8 above the feet) would pass the
lowest such surface (`world_collision::ceiling_at`). This is a stand-in for the exe's sphere
contacts, which meet a ceiling like any other face. `ORTOOL_NEAR=x,y,r ortool collide <game>
<level>` lists the collision triangles around a point.

## Where this came from

- Bit meanings: compared in the game by the project lead, with the viewer's collision overlay
  (`--collision on`) next to the real game.
- Own analysis of the level data: the collide set and its slots, element types and counts, and the
  collide material flags, surveyed over all 70 levels with `ortool collide`.
- Own analysis of `Rayman2.exe` with capstone: the element type switch (0x48E86D), the game
  material → collide material → flags chain. A helper model (DeepSeek, through Reasonix) traced
  these first; the switch and the chain were re-read and checked against the data. Its claim that
  super object +0x34 holds the box in place was wrong: it is a pointer to it.
