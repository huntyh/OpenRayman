# Level geometry (Rayman 2 PC)

How the static geometry of a level is found and stored. Everything here lives in the SNA memory
image; offsets are from the start of each structure. See [gpt.md](gpt.md) for the super object tree.

## From the sector tree to triangles

### Jail_10 far scenery selection (renderer improvement)

Virtual sector **7**, scene node **81** (`05/02 +0x4608`, sector data
`+0x5AC5C`), contains one IPO, node **82** (`05/02 +0x470C`, IPO data
`+0x5BA3C`). Its 31 vertices and seven material elements form the mountain and
valley backdrop at box `(-280.5,-106.0,465.8)..(163.1,324.7,598.0)`.
Textures are `mount1_nz` through `mount4_nz`, `vallee_bord1`, `vallee_bord2`,
and `vallee_fond1`. These are static triangles (including textured cut-outs),
with no look-at mode, sprites or actors in the sector. Sector 1/node 19 is
the pirate ship; sector 5/node 37 is water; sector 6 contains the low islands
and vegetation of the earlier sea passage. Those remain in the normal pass.
Sectors 2/node 21 and 3/node 23 are ordinary IPO sky shells, both using
`beach_sky2_txy` (at ship height and sea height respectively). With the recipe
enabled, visible sky shells draw first with a parallax view, then depth is cleared
before the far scenery. Otherwise these opaque shells would cover the backdrop
after its depth reset. They retain their sector fog and visibility rules.
Sector 4's cloud and lightning quads (`inv_nz`, `foudre_aaa`, `beach_sky3_aaa`)
join that initial sky pass too, with their transparency sorted from the same
parallax eye and clipped by the sky shells before clearing depth. The sky eye uses
the backdrop anchor horizontally and follows the real eye vertically between z=350
and the ship anchor height to stay inside the separate sea and ship shells. Its
translation factor defaults to 0.02 (user, 2026-11-13: 0.1 still looked near) and `OPENRAYMAN_SKY_PARALLAX=0..1` tunes it.

The Jail_10 sky pass adds three optional depth cues (our renderer improvement):

- `OPENRAYMAN_SKY_STRETCH=1..32` (default **1**, off since 2026-11-13: at 4 the clouds looked smeared) scales X/Y around the same
  sky anchor, leaving Z intact. Shells and sector 4 cards share the model,
  including transformed transparency sorting. The sea/ship height adjustment
  stays in place. Only this pass extends its far clip range; existing distance
  fog and flash culling use positions before stretching, so enlargement does
  not erase the clouds in distance fog or change the scripted flash coverage.
- `OPENRAYMAN_SKY_DRIFT=0..4` (default **0.07** since 2026-11-13; was 0.25) scales only material UV scroll
  time in that pass. Texture animation frame selection keeps the original clock,
  including lightning pulses.
- `OPENRAYMAN_SKY_HAZE=0..1` (default **0.3**) blends towards the shell sector's
  fog colour with a smooth elevation fade from full strength at/below the horizon
  to zero at 12 degrees above it. Elevation uses the stretched fragment's direction
  from the sky eye, independent of camera pitch. Sector 4 has no fog light, so its
  cards use the visible shell's colour. Additive/brightening cards lose intensity
  in haze rather than adding a fog-coloured rectangle. Haze follows the lighting
  toggle; existing fog overrides still apply afterwards.

Neutral values **1/1/0** retain the earlier parallax sky. Invalid/nonfinite or
out-of-range values fall back to defaults. `OPENRAYMAN_BACKDROP=0` bypasses these
cues along with the existing backdrop recipe. `OPENRAYMAN_LIGHTNING_CLASSIC=1`
still selects the original lightning blend and retains its animation timing.
Sea, islands, ship, mountains and actors keep their existing transforms/material
scroll/fog. Jail_20's sector 0 remains in the backdrop pass and gets none of these
three cues. Visual sweeps at 2, 4.3, 8.7, 20, 40, 60 and 90 seconds favour S=4:
it reduces the overhead swirl without leaving as much empty upper sky as S=8/12.

The optional renderer recipe selects sector 7 subject to camera-sector
visibility and draws it after sky, from `anchor + (eye-anchor)*f`, before
clearing depth for normal scenery. The anchor is the scripted camera at 90 seconds
`(-101.584,139.372,541.117)`, and `f=0.1`. This is our deliberate improvement,
not a newly discovered original-engine flag; see [IMPROVEMENTS](../IMPROVEMENTS.md).

Where this came from: own inspection of the installed PC Jail_10 using
`ortool lights`, `scene` and `obj`, exported object bounds/material names,
and renderer screenshots (Codex, 2026-11-07). No external implementation used;
exports and screenshots kept in the system temporary directory. The sky parallax
and its height adjustment are our own renderer improvement (Codex, 2026-11-13),
not an original-engine format finding. The horizontal stretch, drift multiplier,
and elevation haze are also our own renderer improvements (Codex, 2026-11-13),
chosen through local screenshot comparisons; no original asset/format changes.
The intro camera positions were sampled with `--start new --time 95
--camera-log <temporary CSV>`: at 76 s `(-156.81,310.65,547.46)`, 80 s
`(-144.60,282.15,547.38)`, 85 s `(-123.59,210.78,544.49)`, 90 s
`(-101.58,139.37,541.12)`, 95 s `(-57.41,79.60,539.92)`.

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

**Turning a transform at run time** (0x445D00, used by the script turns such as procedures 163..165,
171, 172): types 1..3 get the new axes as both matrices and become type 3. Other types keep their
scale part as 0x4462A0 reads it: each row of the combined matrix in the old rotation's rows
(sᵢⱼ = rᵢ · cⱼ), rebuilt on the new rows (cⱼ = Σᵢ sᵢⱼ r′ᵢ). Ours also makes the rows unit length first. A fan
in Jail_20 (node 29) turns every tick, and float rounding makes the rows a little longer each time.
The rebuild multiplies that into the scale, so it compounds: our first version (each combined row's
length as its scale) grew the fan about 2% in 30 s until it poked out of its enclosure. *Where this
came from: own reading of the decompiled 0x46AB20, 0x445D00 and 0x4462A0; the fan found by a DeepSeek
reading (2026-11-08), the growth measured with `ortool run` and ORTOOL_TRANSFORMS.*

**IPO data**: +0x00 physical object.

**Hidden scenery**: super object +0x30 bit 0x2 suppresses drawing that object;
its children are still visited. This applies to IPOs as well as actors. Geometry remains
available for collision and inspection. Jail_20 node 44 (`05/02 +0xE2C`, physical object
`+0xFB74`, mesh `+0xFBB0`) is a hidden, untextured white quad across the cell doorway;
drawing it covers the real laser actor. Node 46 is hidden too. OpenRayman preserves the
meshes but skips their rendering. Where this came from: own analysis of Rayman2.exe
0x45C210, 0x45C360 and 0x45CA30, checked with `ortool scene`, `mesh`, `peek` and
before/after renders, Codex, 2026-11-07. No executable code copied.

**Physical object**: +0x00 visual set, +0x04 collision set (see [collision.md](collision.md)).

**Visual set**: +0x00 u32 type (0 = levels of detail), +0x04 u32 number of levels, +0x08 their
switching distances (floats), +0x0C list of geometric object pointers. OpenRayman uses the first.
At +0x10 is an array of pointers to saved vertex colour sets; +0x14 is the number of sets.
Each set has an LOD count at +0 and a pointer to LOD records at +4. Each 8-byte LOD record
contains a vertex count at +0 and a pointer at +4 to signed 16-bit RGBA values, four per vertex.
Procedure 393 selects a set using a one-based index (zero means none). The renderer starts each
vertex's light sum with these components divided by 256, then adds applicable lights before applying
the material. Where this came from: own analysis of the executable by GPT-6.1 Sol/Terra via Codex,
checked in the data; 0x42B2B0, 0x42AF70, 0x4315D0, 0x431400 and 0x428CB0.
`ortool colours` finds 1,735 scene actors with at least one saved colour set across all 70 levels;
this count excludes generated templates.

**Geometric object**:

| Offset | Field |
| --- | --- |
| 0x00 | vertices: 3 floats each |
| 0x04 | vertex normals: 3 floats each |
| 0x10 | element types: u16 each |
| 0x14 | elements: one pointer each |
| 0x28 | u32 look-at mode: 0 none, 1 card upright on the screen, 2 card turning about its own Z axis |
| 0x2C | u16 number of vertices, u16 number of elements |

**Look-at cards**: a mesh with a look-at mode is a flat card in its own YZ plane, facing +X, and the
game turns it towards the camera every frame (0x41DAD0, called from the mesh draw 0x42AAE0 on a
copy of the part's world transform). The card keeps one axis as its Z and swings about it until its
+X points at the camera as nearly as that axis allows: Y = axis x direction to the camera,
X = Y x axis; seen exactly along the axis it is left alone. The mode's bit 2 picks the axis.
Mode 2 (bit set): the card's own Z axis (the rotation matrix's third axis, transform +0x28), so a
long card stays along its part: the pirates' neck and leg rods (`tallpi_cou3`), chain links, flames,
sparks, the rings and eyes of Lums (505 meshes). Mode 1 (bit clear): the camera's up axis (the
second axis of the inverted view matrix), so the card stands upright on the screen whatever its
part does: mushrooms, flowers, Murfy's mittens (30 meshes in all levels). A scale in the transform
is kept (0x445D00 takes it out with 0x4462A0 and puts it back on the new axes; OpenRayman keeps the
length of each axis, which is the same for a scale along the part's own axes).
Every one of them is
flat along X, and no other value occurs. Drawn fixed, the cards vanish edge-on and overlap each
other, which is how the missing mushrooms near the Learn_10 start were found.

Own data inspection: Learn_31 node 8's dust body and two eyes are sprite elements in geometric
objects 05/02 +0x70DB8, +0x70FB0 and +0x70AF4. All three have look-at mode 0; their sprite
rendering is distinct from the geometric-object look-at modes above. Its saved transform has a
tilted Z axis (approximately -0.291, 0.802, 0.522). Once it wakes near Rayman, its script sets
the Z axis upright; the inactive actor is not drawn by the executable (see scripts.md).

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

**One-sided surfaces** (own analysis of the exe and drivers by GPT-6.1 Sol, Codex, 2026-11-06): the
visual material's flags are ANDed with the draw flags passed in (exe 0x42A410; the level's are
0xFFFFFFFF, GPT offset 0x328, global 0x5003E0). Flag 0x400 turns face culling on; then, with the draw
flags' 0x02000000 set, flag 0x80000 set keeps triangles whose projected area (corners in stored
order) is positive and clear keeps the negative ones (`GliVd1vf.dll` 0x1000311B, tests 0x10003520 /
0x10003580; `GliDX6vf.dll` 0x1000237B). In OpenGL terms "positive" is counter-clockwise front faces,
settled on Rayman (the other way shows his face through the back of his head). Jail_10's materials
are 0x4DDC07FF (cull, keep positive; 0x08000000 depth writes, 0x40000000 normal depth test): its
mountain ring hides from behind. Before, OpenRayman drew everything double-sided;
`OPENRAYMAN_NO_CULL=1` still does, for comparison.

  The mask is object-dependent: 0x42AAE0 places the submitted draw flags at context +0xC,
  while 0x42AA30 and 0x41E310 select the visual material through context +0x2D3B8. The
  scene traversal at 0x45CA30 combines super-object flags, and 0x40F140 initializes newly
  created nodes to 0xFFFFFFFF; 0x40E0D0 clears 0x80000 on a negatively scaled part.
  Sprite drawing at 0x4291E0 and 0x428F10 clears 0x400 in the draw flags. The full
  runtime masks for individual actor parts have not yet been measured, so the renderer's
  material-only rule for those parts remains an assumption.

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

  - One-sided surfaces and object masks: own analysis of the executable and renderer drivers at
    the addresses above; the actor-part runtime mask still needs a data and runtime survey.
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
  X. Which axis each mode keeps: own analysis of Rayman2.exe 0x41DAD0 (the `test al, 2` at
  0x41DB05), 0x445F00, 0x445D00, 0x444B80 and the projection signs at 0x422AB0/0x42C8A0
  (2026-11-14, card 98). Until then the two modes were swapped (inferred by eye): the pirates' rods
  in Jail_20 stood upright on the screen instead of lying along the neck and legs. Checked with
  before/after renders of Jail_20 and Learn_10; not yet compared live. The name "look at" matches
  what Raymap calls this field. No executable code copied.
- The Rayman 2 PC field offsets and sizes: own analysis, by following pointers with
  `ortool peek` and checking counts against array sizes, then verified over all 70 levels.
- Learn_10's portal well: sector 1's IPO node 94 uses an eight-vertex, ten-triangle mesh and
  visual material `05/02 +0x157E4` with `foret_fondetoiles_txy`, scroll `(0, -0.003)` per
  frame and no texture animation frames. Hiding that material removes the well's white stars
  in the reported camera view. The extra independently scrolling star sample is our own optional
  renderer improvement, not a decoded game feature; `OPENRAYMAN_WELL=0` turns it off.

### Animated textures choose their blending from every frame

An animated material's first frame can be invisible (`inv_nz`) while later frames are soft
(`foudre_aaa`, `beach_sky*_aaa`): Jail_10's sky cards cycle like "5 s invisible, 0.05 s foudre2,
0.2 s invisible, 0.02 s foudre", which is its lightning. Blending must come from all frames: taking it
from the first one alone put the whole animation in the opaque pass, so the lightning frames showed as
hard white patches. No Jail_10 script switches lights during the strike (procedures 104..114 and
388..390, handlers 0x468E90 and 0x470B20, are never called); the white full-screen fog at 6.1..6.7 s is
the logo's transition (actor 4, message 30), not the lightning. The crack the intro camera leaves is
sector 0's IPO node 17 (rock), not part of the backdrop.

Where this came from: own inspection of the Jail_10 materials and scripts and the exe handlers named
above (Sol, 2026-11-07), screenshots before and after at the same camera and time.
