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
| 0xA7 | u8 alpha flag: whether the light also changes transparency (not used yet) |
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

For each corner, the lights of its sector that are on and shine on its kind (scenery or actors) are
summed per colour channel: ambient at full strength, parallel lights by how much the surface faces
them, spherical lights by their falloff and how much it faces them (painting lights skip the facing).
The material then decides the result:

```
colour = clamp(L + material ambient + sum × (L + material diffuse), 0, 1)
pixel  = texture × 2 × colour
```

where L is the game's luminosity setting as an offset from neutral (0.5). Most scenery in Learn_10
has material ambient 0.1 and diffuse 0.5. Multiplying the texture by the colour alone left the level
far too dark; raising L to 0.5 instead brightened it but washed it out. Doubling (what Direct3D calls
"modulate 2×") matched the game by eye: the red glow at the foot of the stone doors, dark tops, the
blue waterfall and teal stream. So L defaults to 0 (luminosity 0.5) for now.

Static geometry is lit once when the level loads. Actors move between lights, so they're lit every
frame by the strongest 8 lights of the sector they stand in.

**Fog**: the fog light of the sector the camera is in. A surface's fog cover rises linearly with its
distance from the camera, from the near blend at `near` to the far blend at `far`, and its colour is
mixed towards the fog colour by that much. Glows (additive surfaces) fade instead.

## Where this came from

- Own analysis: the sector light list (offsets 0x0C/0x14, element layout), the light's size, the
  offsets of type, near/far, transform and colour, the box at 0x60 (checked against the geometry of
  all 70 levels), the virtual flag at 0x7C (set on exactly the sectors that have no actors or actor
  lights), the type counts, the direction of parallel lights.
- Confirmed from Raymap (reference only): the names and meanings of the fields (on, far/near, the fog
  blends, the four flag bytes after the colour, the background colour), the type names, and how its
  viewer combines lights with the material's ambient and diffuse colours and the luminosity offset.
  The engine's own code in `Rayman2.exe` hasn't been checked yet.
- By eye against the real game (Learn_10): the doubling and the default luminosity.
