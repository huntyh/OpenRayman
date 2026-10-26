# Actors and animations (Rayman 2 PC)

Actors ("persos" in the engine: Rayman, enemies, Lums, platforms, props) are super objects of type
2 in the dynamic world (see [gpt.md](gpt.md)). What they look like comes from an object list, and
where each part goes from an animation.

## From an actor to its parts

```
actor super object → actor (perso)
  +0x00 → 3D data
           +0x00 initial state
           +0x04 current state → +0x04 animation reference
           +0x0C object list
           +0x10 initial object list
           +0x14 family
```

**Family**: what all actors of one kind share. +0x10 first state, +0x14 last state, +0x18 u32
number of states; +0x1C onwards, the object lists its actors use. **State**: +0x00 next state,
+0x04 animation reference. Several states can share an animation reference.

The saved state is only where an actor starts: the game's scripts usually pick another one as the
level begins. The breakable cages in The Woods of Light are an example: their family (14 states,
animations 427–437) has hanging poses that show the cage under three chain links (427, 436, 437),
but every cage is saved in the standing pose (433, one channel, the cage alone), so until scripts
run they're drawn without their chains. `ortool anim` prints what an animation shows.

**Object list**: +0x04 entries, +0x0C u16 number of entries. Each entry is 0x14 bytes: +0x00
optional scale (3 floats), +0x04 physical object (the same structure static geometry uses, see
[geometry.md](geometry.md)), then three small values.

**Animation reference** (0x20 bytes): u16 frames, u8 speed, u8 channels, events pointer, 3 floats,
morph data pointer, +0x18 u16 **animation index**, u8 number of events, u8 transition, u32 unknown.
The frame and channel counts repeat the animation's own, which makes a good consistency check.

Actors without an animation reference (or without an object list) are usually invisible helpers:
triggers, waypoints.

## The animation bank

All animations of Fix and a level live in 12 shared arrays. Fix's elements come first and the
level's are appended; every index anywhere is into the combined arrays.

Each array is described by 16 bytes: u32 (usually count − 1), u32 count, u32 capacity, u32 how
many are Fix's. In **Fix.gpt** the 12 descriptions are at 0xCC0, followed by 12 pointers to the
arrays in Fix's memory (block `05/01`). In a **level GPT** they are at 0xF6C (all 70 levels), and
each array's new elements follow inline, every array starting 4-byte aligned, in this order:

| # | Array | Element size |
| --- | --- | --- |
| 0 | animations | 0x38 |
| 1 | vectors: 3 floats | 12 |
| 2 | quaternions: 4 × i16 / 32767 (x, y, z, w) | 8 |
| 3 | hierarchy links: i16 child channel id, i16 parent channel id | 4 |
| 4 | object choices ("NTTO"): u16 flags (0x2 = hidden), u16 object list index, 2 bytes | 6 |
| 5 | frames: u16 rotation, u16 vector, u16 hierarchy link count, u16 first hierarchy link, u16 object choice | 10 |
| 6 | channels: u16, i16 id, u16 vector, u16 first object choice, u32 first frame key, u32 first keyframe | 16 |
| 7 | object choice indices (u16) | 2 |
| 8 | frame keys: per channel and frame, a keyframe index (u32) | 4 |
| 9 | keyframes: 4 floats (the last a position multiplier), u16 frame, flags (0x80 = last), rotation, a second rotation, scale vector, position vector, 4 × u16 | 36 |
| 10 | events (contain pointers, so they're not part of the pointer-free stretch) | 12 |
| 11 | morph data | 8 |

This accounts for the whole level GPT after its header: the "1.8 MB without pointers" in Learn_10
is arrays 0–9, the run of pointers after it is the events, and the last stretch is the morph data.

**Animation** (0x38 bytes): u16 speed, then u16 counts of vectors, quaternions, hierarchy links,
object choices, object choices per channel, channels, frames, keyframes, an unknown, events; 8 more
u16 (unknowns, a second speed, second start indices, morph count); then u16 start indices of its
vectors, quaternions, hierarchy links, object choices, frames, channels, events and morph data.

## Posing a frame

For animation `a` at frame `f`, with `F = frames[a.start_frames + f]`, for each channel `c` of `a`:

- keyframe `k = keyframes[frame_keys[c.first_frame_key + f]]`
- local transform: translate by `vectors[k.position] × k.position_multiplier`, rotate by
  `quaternions[k.rotation]`, scale by `vectors[k.scale]`
- shown object: `objects[choices[c.first_object_choice + F.object_choice]]`; hidden if its flag says
  so, else that entry of the actor's object list
- parent: the frame's hierarchy links (`F.hierarchy_start`, `F.hierarchy_count`) link channels by id

A part's world transform is the actor's own world transform × its parents' channel transforms ×
its own.

## Playing an animation

The animation's speed (u16 at +0x00) is in frames per second: 30 for most, 25 or 60 for some, a
handful between 15 and 55. Between keyframes the pose is blended: each frame names the keyframe at
or before it, and unless that is the channel's last keyframe (flag 0x80), the next keyframe in the
array is the next one in time. A survey of every channel and frame of all 70 levels confirmed both
(the named keyframe is never after the frame, the next one always after it). Positions, scales and
the position multiplier are blended linearly, rotations along the shorter way between the two
quaternions. Every channel's first keyframe is at frame 0. After the last keyframe, a channel holds
still until the animation loops. Which objects are shown and how channels link come from the whole
frame, without blending.

## States the scripts choose

An actor's saved state is often a still or a placeholder: the game's scripts switch it to its real
state when the level begins. `ortool states <game> <level> <node>` lists a family's states. In
Learn_10:

- The mushroom clusters' saved state plays animation 312, which shows 3 mushrooms (a stem and a cap
  each). Every other state of the family shows 5 (10 channels), as the real game does.
- One family in Fix covers every kind of Lum, and they're saved in a placeholder (animation 276, 2
  frames that swap a glow and an eye 30 times a second). Its 15 states, checked by eye: 1 and 12 a
  purple ring Lum, 2 red, 3 blue, 4 the yellow Lum with wings, 5–10 a blue bubble effect, 11 a
  star, 13 another yellow Lum, 14 green.
- The cages are saved standing, without their chains (see above).

These need the script interpreter (Phase 3). Until then the viewer can show any state: F5 / F4 put
the actor in the middle of the view into its family's next / previous state (with Shift, every
actor of that family), and `--actor-state <node>:<state>` does it from the command line.
`ortool motion <game> <level>` lists each actor's animation and how far its parts move over a loop.

## Checks

- All 70 levels: Fix's and the level's descriptions agree, the level's data ends where the event
  pointers begin, every actor's animation reference matches its animation's frame and channel
  counts, and every index used to pose frame 0 is in range.
- Learn_10: 450 animations (289 from Fix), 176 meshes and 3,838 triangles at frame 0. Rendered and
  checked by eye: Rayman and the other characters stand upright with their parts in place.
- All 70 levels: every actor poses at every frame of its animation, and halfway between frames,
  with all indices in range. Learn_10's actors use 113 distinct meshes.
- Blending: a unit test with a hand-made two-keyframe animation (halfway positions, the shorter
  rotation, holding after the last keyframe, wrapping around).

## Shadows

An actor's 3D data holds its shadow: +0xCC a game material (null for no shadow), +0xD4 and +0xD8
two floats, its size along the actor's X and Y. In all 70 levels 152 of 5,107 actors have one, and
every one uses the same Fix material with the texture `effets_speciaux\shadow1_mxy_aaa`: a 32×32
quarter of a black blob (up to 49% opaque), its centre in a corner, mirrored into a whole one. Fix's
Rayman has 0.8 by 0.8; in Learn_10 the three Globox kids (one actor) have 3 by 1.

Whether it's drawn (0x44C570 in `Rayman2.exe`): the 3D data's u16 at +0xDC must be nonzero, bit
0x04 of the byte at +0x29 of the actor's standard game record (actor +0x04) set, the super object's
draw flag 0x20 clear, and fewer than 10 shadows drawn so far. No actor in the game has the standard
game bit set as saved: scripts switch shadows on (Rayman's), and leave others off, such as the
Globox kids' (byte 0x0A), which indeed have no shadow in the game. OpenRayman draws every actor's
shadow anyway, a deliberate improvement.

The game projects the blob onto the ground below the actor. Without collision yet, OpenRayman finds
the ground with a ray straight down onto the level's solid triangles, then projects the blob
straight down onto every solid, not too steep triangle around that height, turned with the actor,
the size being half its extent. So it follows creases and steps instead of sinking into them. Actors with nothing visible (scripts give
them a look later) get no shadow yet.

## Where this came from

- Own analysis: the shadow fields of the 3D data, followed back from the shadow texture through
  its visual and game material; checked across all 70 levels by the tests.

- Own analysis: the speed as frames per second and the keyframe order (surveyed on all levels), the
  states the scripts choose, and that the level GPT's tail is the animation bank, the 4-byte alignment of every
  array (found by checking every level), the Fix/level split, and the field offsets in actors,
  3D data, object lists and states.
- The animation structures, element layouts and how a frame is posed follow Raymap's description
  (reference only), each checked against the data as described above. In the level files the arrays
  keep the same layout as in memory.
