# GPT and PTX files, super objects (Rayman 2 PC)

SNA files are memory dumps, but a level also needs entry points into that memory: where the scene
tree starts, which textures to load. Those come from two small companion files per level (and one
pair for Fix): `.gpt` and `.ptx`.

## Streams with a pointer list

GPT and PTX files are not masked and not memory dumps. They are streams the game reads field by
field. Their relocation tables (`Fix.rtp`/`Fix.rtt`, and the level tables in LEVELS0.DAT, types 1 and
3) have the [usual layout](sna.md#relocation-tables), with a single list filed under block `0a/00`.
Unlike SNA tables, each entry's "address" is the **pointer value** itself, listed in the order the
pointers appear in the file:

- Fix.gpt: 67 entries, Fix.rtp: 67 entries, all matching in order.
- Null pointers have no entry.
- A pointer's target block is the entry's target, and its value is an address as the *writing file*
  announced it (Fix for Fix's files, the level for the level's files; see [sna.md](sna.md)).

`pointer_stream` reads these files, and checks every pointer against the next table entry, so a
reader that gets out of step with the real layout fails immediately instead of reading garbage.

## Level GPT header

| Field | Notes |
| --- | --- |
| u32 count | Actors stored in Fix: 10 in every level |
| actors | `count` times: a pointer to the actor, optionally followed by a placement (below) |
| pointer | actual world: the root super object |
| pointer | dynamic world: parent of the level's actors |
| pointer | inactive dynamic world |
| pointer | father sector: root of the level geometry, a tree of sectors |
| ... | not decoded yet (more pointers, then the 70 level names, 0x1E bytes each) |

A **placement** follows an actor if the next pointer is a super object whose data pointer leads back
to that actor. It holds: the super object, a transform (0x58 bytes: u32 type, 3 floats position, two
3×3 float matrices), u32 draw flags, f32 transparency, then the next, previous and parent pointers.
Fix's own super objects are saved without links, because Fix doesn't know the level it will join;
these records supply them. In every level the same 5 of the 10 actors are placed this way; the other
5 have no super object in the level.

The world roots sit at the same offsets in every level (block `05/02`, +0x108, +0x1B0, +0x258,
+0x300), and the actual world's 3 children are the other three.

After the header comes about 1.8 MB (in Learn_10) without a single pointer, then a few hundred
pointers into `04/01`. Both parts are still unknown.

## Super objects

The nodes of the scene tree. Each one carries an engine object (an actor, a sector, geometry, ...)
and its position relative to its parent.

| Offset | Field |
| --- | --- |
| 0x00 | u32 type: `1` for the world roots, `2` for actors, other values not identified yet |
| 0x04 | pointer to the engine object |
| 0x08, 0x0C | first and last child |
| 0x10 | u32 number of children |
| 0x14, 0x18 | next and previous sibling |
| 0x1C | parent |
| 0x20 | pointer to the transform |
| 0x30 | u32 draw flags |
| 0x38 | f32 transparency (255 = opaque) |

`scene` rebuilds the tree and cross-checks every link (count, first and last child, siblings in
both directions, parent). This holds for all 70 levels: from 14 super objects in Raycap to 475.

## PTX texture lists

| Field | Notes |
| --- | --- |
| u32 | 1024: the number of texture slots the engine has |
| u32 count | slots used by this file (Fix: 257, Learn_10: 767) |
| pointers | `count` slots, null for unused ones, pointing to texture descriptions |
| ... | one small value per slot (1 for Fix's textures, 2 for the level's), not decoded yet |

Texture description, as far as known:

| Offset | Field |
| --- | --- |
| 0x14 | u32 flags |
| 0x18 | u16 height, u16 width: often half of the size at 0x1C; meaning unknown |
| 0x1C | u16 height, u16 width |
| 0x46 | file name, NUL terminated, Latin-1, e.g. `textures_foret\foret_bordtri_nz.tga` |

The file named is in Textures.cnt with a `.gf` extension instead of `.tga`. Every one of the 7,570
texture references in the 70 levels exists there. The size at 0x1C matches the GF file for 97% of
them; the rest are off by exactly a factor of two, presumably textures replaced at another
resolution after the levels were built. The GF file's own size is what counts.

## Where this came from

- Own analysis: the stream model and what the GPT/PTX tables contain, the level header and the Fix
  actor placements, the PTX layout, the texture description fields, the LEVELS0.DAT type numbers
  (1 = GPT, 2 = SND, 3 = PTX, confirmed by matching each table to its file).
- The super object field layout and the names of the world roots follow the usual OpenSpace
  terminology, as documented by Raymap (reference only). Each field was confirmed by own analysis:
  the links agree across every super object of every level.
