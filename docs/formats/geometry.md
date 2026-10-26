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

The matrices are stored for row vectors (the game computes v × M), so for the usual M × v they
need transposing. The proof is in type 4: wherever the scale is uneven, the second matrix equals the
rotation with each **row** scaled (to 1e-4 in every case, with designer-looking factors like 1.7,
1.5, 1.5), never each column. This matches the animation quaternions, which also come out transposed
compared with the textbook formula ([animation.md](animation.md)).

Two earlier mistakes, both visible in The Woods of Light: multiplying the two matrices rotated type
3 objects twice (the swirl in front of the stone lay flat instead of facing out), and reading them
untransposed turned objects the wrong way (the three Globox kids faced the wall instead of the
stream).

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
| 0x28 | u32 look-at mode: 0 none, 1 upright card, 2 card facing the camera |
| 0x2C | u16 number of vertices, u16 number of elements |

**Look-at cards**: a mesh with a look-at mode is a flat card in its own YZ plane, facing +X, and the
game turns it towards the camera every frame. Mode 1 turns only about the mesh's own Z axis, so it
stays upright: mushrooms, flowers, Murfy's mittens (30 meshes in all levels). Mode 2 faces the camera
fully: flames, sparks, chain links, the rings and eyes of Lums (505 meshes). Every one of them is
flat along X, and no other value occurs. Drawn fixed, the cards vanish edge-on and overlap each
other, which is how the missing mushrooms near the Learn_10 start were found.

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
specular's fourth may be an exponent), +0x48 texture description (see [gpt.md](gpt.md#ptx-texture-lists)),
+0x4C scroll speed (2 floats, u and v), +0x54 the same two again, +0x5C u32 1 if it scrolls, +0x64
first frame of an animated texture, +0x68 the frame showing now, +0x6C u32 number of frames.

**Scrolling textures**: the speed is in texture widths per frame, with v counting up as everywhere
in the game (0.0065 for the Learn_10 stream). 177 materials in all levels scroll: water, foam and
mist, fog, skies and star fields, rain, a vortex. In every one the flag is 1 exactly when a speed is
set, and the second pair always equals the first. OpenRayman assumes the game's 60 frames per second
for the speed; that is not confirmed from the code, so compare with the game.

**Animated texture frame** (0xC bytes): +0x00 texture description, +0x04 f32 how long it shows, in
seconds, +0x08 the next frame (none after the last; the animation loops). The frames swap whole
images: the Lums' wings flap through `lum_aile01`, `03`, `02`, `01` at 0.035 s each, the cages'
lights blink, and other levels have flames, lava splashes, lightning, blinking eyes and pulsing
floor spikes (1,254 uses in all levels). A few single-frame entries have odd durations around 100;
they only ever show one image. In every material of all 70 levels the chain is exactly as long as
the count says.

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
- Scrolling textures: own analysis. The water, foam and mist materials of Learn_10 have small
  floats after the texture pointer that the riverbank's material doesn't; a survey of every material
  of all 70 levels gave the pattern above. The frame rate is an assumption.
- Animated textures: own analysis, starting from the Lum's three wing textures and walking back
  from them with `ortool refs`; checked on all levels as described above and by eye (the wings
  flap). The fields match what Raymap reads for animated textures (reference only).
- Look-at mode: own analysis. The word differs between a mushroom card and a character mesh; a
  survey of all meshes in all 70 levels found only 0, 1 and 2, and every non-zero mesh flat along
  X. Which way the card turns (upright vs. fully) was inferred from what uses each mode and checked
  by eye; the name "look at" matches what Raymap calls this field.
- The Rayman 2 PC field offsets and sizes: own analysis, by following pointers with
  `ortool peek` and checking counts against array sizes, then verified over all 70 levels.
