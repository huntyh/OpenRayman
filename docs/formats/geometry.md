# Level geometry (Rayman 2 PC)

How the static geometry of a level is found and stored. Everything here lives in the SNA memory
image; offsets are from the start of each structure. See [gpt.md](gpt.md) for the super object tree.

## From the sector tree to triangles

```
father sector
└─ sector                      super object type 0x04
   └─ IPO                      super object type 0x20 ("instantiated physical object")
      → physical object
        → visual set
          → geometric object   (one per level of detail)
            → elements         (triangles sharing one material)
              → game material → visual material → texture description
```

Every super object has a transform pointer (+0x20). For static geometry in all levels seen so far it
points at Fix's identity transform, so vertices are already in world coordinates. The game's
coordinates are Z up.

## Structures

**Transform** (0x58 bytes): u32 type, 3 floats position, a 3×3 float rotation, then a 3×3 float
matrix combining rotation and scale, which is the one that places the object. Types, checked on all
11,606 transforms of the 70 levels:

| Type | Count | Matrices |
| --- | --- | --- |
| 1 | 6,951 | identity |
| 2 | 3,109 | identity: a translation only |
| 3 | 1,256 | the rotation, repeated in the second matrix |
| 4 | 290 | a rotation, and the rotation times a scale (e.g. 0.4) |

Multiplying the two matrices (as an earlier version did) rotates type 3 objects twice: the swirl in
front of the stone in The Woods of Light lay flat instead of facing out.

**IPO data**: +0x00 physical object.

**Physical object**: +0x00 visual set, +0x04 collision set (not decoded yet).

**Visual set**: +0x00 u32 type (0 = levels of detail), +0x04 u32 number of levels, +0x08 their
switching distances (floats), +0x0C list of geometric object pointers. OpenRayman uses the first.

**Geometric object**:

| Offset | Field |
| --- | --- |
| 0x00 | vertices: 3 floats each |
| 0x04 | vertex normals: 3 floats each |
| 0x10 | element types: u16 each |
| 0x14 | elements: one pointer each |
| 0x2C | u16 number of vertices, u16 number of elements |

**Element, type 1 (indexed triangles)**:

| Offset | Field |
| --- | --- |
| 0x00 | game material |
| 0x04 | u16 number of triangles, u16 number of UVs |
| 0x08 | triangles: 3 u16 vertex indices each |
| 0x0C | the same triangles as 3 u16 UV indices each |
| 0x10 | face normals: 3 floats each |
| 0x14 | UVs: 2 floats each, origin at the bottom left of the texture (v = 0 is the bottom row) |

**Element, type 3 (sprites)**: images that always face the camera (Lums, glows, sparkles).
+0x00 list of sprites, +0x04 u16 count, u16 −1. Each sprite is 0x30 bytes: +0x00 info, +0x04 a
size (2 floats, not used), then more fields not used yet, +0x28 u16 the vertex it's centred on.
The info: +0x0C pointer to the half width and half height actually drawn (2 floats), +0x14 pointer
to a pointer to the game material. A sprite's object usually has a single vertex at its origin.
Learn_10 has 31 of them, all on actors.

**Game material**: +0x00 visual material.

**Visual material**: +0x00 u32 flags, +0x04 ambient, +0x14 diffuse, +0x24 specular (4 floats each;
specular's fourth may be an exponent), +0x48 texture description (see [gpt.md](gpt.md#ptx-texture-lists)).

## Checks

- Every index of every element in all 70 levels is in range, and each array's size matches its
  count (the arrays sit back to back in memory, so a wrong size would show immediately).
- Every textured material in all 70 levels names a texture that exists in Textures.cnt.
- Learn_10: 87 meshes, 2,604 triangles, 45 materials. Its export was checked by eye in top, side and
  textured perspective views: paths, pools, the waterfall and alpha-cut ferns all look right.

## Where this came from

- The chain of structures (IPO, physical object, visual set, geometric object, element, game and
  visual material) follows the usual OpenSpace model as documented by Raymap (reference only).
- Sprites: structure from Raymap (reference only), checked against the data (centre vertices in
  range in all 70 levels) and by eye.
- The Rayman 2 PC field offsets and sizes: own analysis, by following pointers with
  `ortool peek` and checking counts against array sizes, then verified over all 70 levels.
