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

**Transform** (0x58 bytes): u32 type (1 = identity), 3 floats position, 3×3 floats rotation, 3×3
floats scale. How rotation and scale combine isn't confirmed yet (all static geometry is identity).

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
| 0x14 | UVs: 2 floats each, origin at the top left of the texture |

Type 3 elements exist too (22 in the whole game, none in Learn_10, probably sprites); not decoded
yet.

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
- The Rayman 2 PC field offsets and sizes: own analysis, by following pointers with
  `ortool peek` and checking counts against array sizes, then verified over all 70 levels.
