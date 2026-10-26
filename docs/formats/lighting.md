# Lighting and fog (Rayman 2 PC)

How a level is lit. Rayman 2 PC stores no precomputed light per vertex: each sector lists the
lights that shine in it, and the engine lights surfaces from those, combined with each material's
own colours. Fog is a light too. Everything here lives in the SNA memory image; offsets are from the
start of each structure.

## Sectors

The data of a sector super object (type 0x04, see [geometry.md](geometry.md)):

| Offset | Field |
| --- | --- |
| 0x00 | actors in the sector: first, last, count |
| 0x0C | static lights: first element, last element, u32 count |
| 0x18 | dynamic lights (empty in the saved levels) |
| 0x24, 0x30, 0x3C | lists of neighbouring sectors (not decoded yet) |
| 0x60 | box: 3 floats minimum, 3 floats maximum |
| 0x7C | u8: 1 for a virtual sector |
| 0x80 | sky: a visual material, or null (then +0x84 is 100 as well) |

A light list element is the light, then the next element. Sectors share lights: Learn_10's 20
sectors list 225 distinct lights.

The box holds the sector's geometry: over all 70 levels, 605 of 102,016 static vertices lie more than
a unit outside their sector's box. **Virtual sectors** (Learn_10 has 6) overlap ordinary ones and
carry lights for parts of the scenery only: they have no actors and no lights for actors. So the
sector something stands in is the smallest box that holds it, leaving out virtual sectors; an actor
in the gap between two boxes takes the nearest.

## Lights

0x160 bytes each:

| Offset | Field |
| --- | --- |
| 0x00 | u8 on |
| 0x08 | u32 type, see below |
| 0x0C | f32 far |
| 0x10 | f32 near |
| 0x14 | f32 fog: "infinite" distance (50000 in every fog seen) |
| 0x18 | f32 fog: how much fog covers at `near`, out of 255 |
| 0x24 | transform (0x58 bytes): the light's position, and its direction (Z axis) |
| 0x94 | colour: 4 floats red, green, blue, alpha |
| 0xA4 | u8 send: 0 switches the light off |
| 0xA5 | u8 what it lights: bit 1 the scenery, bit 2 actors |
| 0xA6 | u8 painting: lights surfaces whichever way they face |
| 0xA7 | u8 channels: 0 colour and alpha, 1 alpha only, 2 colour only |
| 0xE8 | f32 fog: how much fog covers at `far`, out of 255 |
| 0xF0 | fog: background colour, 4 floats |
| 0x104 | a second transform, the same as the first in every light seen |

Types, over all 70 levels (only these occur):

| Type | Count | Meaning |
| --- | --- | --- |
| 1 | 189 | parallel: sunlight, shining along the transform's Z axis |
| 2 | 10,214 | spherical: full strength within `near`, fading linearly to nothing at `far` |
| 4 | 198 | ambient: the same everywhere |
| 5 | 11 | a parallel variant, not decoded yet: drawn as parallel |
| 6 | 128 | fog, in 62 levels |

Colours go above 1 (up to 10) and below 0: negative lights darken. Of 10,740 lights, 9,727 light the
scenery only, 736 actors only, 267 both, and 10 nothing.

In Learn_10 the three parallel lights all point downward (the matrix's third row is (0, 0, -1),
(0.22, 0.65, -0.73) and (-0.09, 0.53, -0.84)), which is how their direction was settled. The two fog
lights are teal (0, 0.5, 0.5) and pale cyan (0.5, 1, 1), 30 to 70 and 30 to 100 units, around the
waterfall.

## Lighting a surface

As `Rayman2.exe` does it (see "Where this came from"). For each corner, the lights of its sector that
are on (+0x00 and +0xA4 both set) and shine on its kind (scenery or actors) are summed per channel,
red, green, blue and alpha; the channels byte leaves out the colour (1) or the alpha (2):

| Type | Strength |
| --- | --- |
| ambient | 1, summed separately and folded into the material's ambient colour (below) |
| parallel | max(0, −N·D), D the direction it shines |
| spherical | falloff × max(0, N·L), L towards the light; painting lights leave out the facing |
| 5 (box) | not used for lighting yet |

The falloff runs over the **squared** distance: 1 within `near`, then (far² − d²) / (far² − near²),
nothing beyond `far`. The engine also scales it by a factor from the object's matrix (its scale, 1
for the level's own geometry).

The material then turns the sums into a vertex colour:

```
ambient' = L + material ambient + ambient lights × material diffuse      (alpha: no L)
diffuse' = L + material diffuse                                          (alpha: no L)
colour   = clamp(2 × (ambient' + sum × diffuse'), 0, 1)
pixel    = texture × colour
```

L is the luminosity option minus 0.5; the exe's default is 0.5, so L = 0. The doubling and the
clamp happen together when the engine converts the colour to a byte: it adds 24576.0 to the float,
which leaves the value times 512 in the low bits, and clamps that to 0..255. So a surface is fully
lit at half and never brighter than its texture. (An earlier version of ours doubled after clamping,
which let strong lights such as Learn_10's 8.0 pink and 5.0 teal ones by the river tablet glow white,
where the game shows the plain texture.)

**Alpha**: the same sum over the lights' alpha, through the same conversion. Many Learn_10 lights
carry alpha −1 or −0.22 near the water: they make the stream and the waterfall more see-through close
to them, fading out with distance, which gives the waterfall its soft edges and lets the riverbed
show through the stream. The water's material has ambient alpha 0, so it's only as opaque as its
lights make it. Solid surfaces ignore alpha.

Static geometry is lit once when the level loads. Actors move between lights, so they're lit every
frame by the strongest 8 lights of the sector they stand in.

**Fog**: the fog light of the sector the camera is in. A surface's fog cover goes by its depth along
the view direction (not its straight-line distance): the near blend up to `near`, rising linearly to
the far blend at `far`, staying there up to the "infinite" distance, and full beyond it. Its colour
is mixed towards the fog colour by that much. Glows (additive surfaces) fade instead. Objects past
the infinite distance aren't drawn at all.

The game hands the fog light's values (blends clamped to 0..255, near/far ordered) to its 3dfx
driver, `GliVd1vf.dll`, which builds a Glide fog table: 64 entries indexed by W, Glide's depth, filled
as above (0x10007BA0). Using straight-line distance instead gave slightly too much fog away from the
middle of the screen.

## Sky

Behind everything the game clears the screen with the background colour of the first fog light in
the camera's sector (0x40AD00 packs it and hands it to the screen clear; Learn_10's is teal). On top
of that each sector can name a **sky material** (+0x80): in Learn_10 the 11 open-air sectors all
point to the same one, `foret_sky_txy`, white swirling clouds on cyan that scroll slowly (0.002 per
frame). How the game maps it onto the sky wasn't found yet; OpenRayman draws it on a dome around the
camera (from a little below the horizon to the top, repeating 4 times around), unlit and unfogged,
which looks like the game's from the one spot compared so far.

## Where this came from

- Own analysis: the sector light list (offsets 0x0C/0x14, element layout), the light's size, the
  offsets of type, near/far, transform and colour, the box at 0x60 (checked against the geometry of
  all 70 levels), the virtual flag at 0x7C (set on exactly the sectors that have no actors or actor
  lights), the type counts, the direction of parallel lights.
- Confirmed from Raymap (reference only): the names and meanings of the fields (on, far/near, the fog
  blends, the four flag bytes after the colour, the background colour) and the type names.
- Own study of `Rayman2.exe` (disassembled with capstone, read, not copied): the per-type dispatch
  (0x428CB0: types 4, 2, 1, 5, 3), the point light with and without painting (0x427400, 0x427720:
  squared falloff, Lambert facing), the parallel light (0x4284C0), the channels byte, the material
  setup with the luminosity offset (0x42A580: default 0.5 at 0x4A0488), and the float to byte
  conversion that doubles and clamps (0x42D520 and its variants, constant −24576.0). Fog: the setup
  that picks the first fog light (0x44AE80, getters at 0x4270E0..0x427110), the fog record
  (0x424010), object culling past the infinite distance (0x42AF80), and the Glide fog table in
  `GliVd1vf.dll` (0x10007BA0; its exported GLI_DRV_vComputeFogEffect is an empty stub). Sky: the
  background colour reader (0x40AD00) and the screen clear (0x4204B0).
- Own analysis: the sector's sky material at +0x80 (followed back from the `foret_sky_txy`
  texture, which no geometry uses, to the open-air sectors).
- By eye against the real game (Learn_10): the river tablet, the stone doors, the mushroom cave and
  the stream, which is how the earlier guesses were found wrong.
