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
  triangles in slot 3. How an actor's own collision zones are found is not read yet.

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

The other bits are still unknown.

## Where this came from

- Bit meanings: compared in the game by the project lead, with the viewer's collision overlay
  (`--collision on`) next to the real game.
- Own analysis of the level data: the collide set and its slots, element types and counts, and the
  collide material flags, surveyed over all 70 levels with `ortool collide`.
- Own analysis of `Rayman2.exe` with capstone: the element type switch (0x48E86D), the game
  material → collide material → flags chain. A helper model (DeepSeek, through Reasonix) traced
  these first; the switch and the chain were re-read and checked against the data. Its claim that
  super object +0x34 holds the box in place was wrong: it is a pointer to it.
