# Generated actors: what the scripts create while the game runs

Some actors aren't in the level's scene at all: they are templates that scripts copy when needed.
Rayman's shot is one. Rayman's macro 5 creates it when his throw animation reaches its marker
(condition 43 gives the point):

```
var59 := function_66(actor @04/00 +0x14708, var38)
```

## The pool

The exe keeps one pool of slots shared by every generated actor: the count at 0x4A6B18, the super
objects at 0x4A6B28 (0x48 bytes each), per slot component storage at 0x4A6B2C (0x38 each), and at
0x4A6B30 per slot the super object of the actor whose scripts made it (0 = free). Function 66
(0x47CC30 case 0x42) checks the template (0x40BC80: a known template, or already generated), builds
a transform from the engine's default (no rotation) with the given position, and calls 0x40BCC0:

- a free slot (its occupancy word 0 and its super object unlinked);
- every one of the actor's 14 components that the template has is copied through the callback table
  at 0x49F4C8, into storage the slot keeps and reuses next time;
- the standard game block gets the id 0x10000 + slot at +8, the way back to the super object at
  +0xC (0x4136E0), and bits 0x02 and 0x04 in +0x29;
- the super object goes under the dynamic world (global 0x500FD0), +0x2C = 0xFFFFFFFF, and the
  actor's sector is looked up (0x412850, 0x44BDC0, fallback 0x44BE50); no sector, no actor;
- each component is then initialised in mode 5, which resets far less than a level start: speeds,
  for one, stay as copied (0x412F20 clears only in modes 0..4).

It returns the new actor (super object +4), or none. Function 67 (case 0x43 -> 0x40C2A0) counts the
live slots a given actor made: the shot's scripts ask how many of Rayman's are out. In the real
game's Learn_10 the pool has 40 slots (read live); a second maker besides Rayman showed up there too.

| Part | Size | What the copy does |
|---|---|---|
| actor | 0x38 | 14 component pointers |
| +0x00 3D data | 0x118 | copied (0x4186F0); the instance's own +0x90, +0x98, +0x9C, +0xB8 and u16 +0x82 kept; state clock +0xBC, +0xC4 = now |
| +0x04 standard game | 0x34 | copied (0x4136A0), then the fields above; +0x18 cleared (0x4132A0) |
| +0x08 dynamics | 0x0C | +0 the base block: 0x154, or 0x1DC with flag 0x2 at base +0xC, 0x338 with 0x4 (0x412CA0); with flag 0x4000 base +0x150 points to a 0x16C contact report; +4 a 0x1C block |
| +0x0C brain | 0x0C | +0 the mind, +4 0xCDCDCDCD, +8 0 (0x417E80) |
| mind | 0x14 | +0 the model (shared), +4 normal and +8 reflex intelligence (0x18 each: +4 schedule position, +8 behaviour, +0xC the one before, +0x10 actions in progress, +0x14 the initial behaviour), +0xC variables (+0 the model's declaration through a pointer, whose +8 is the byte size; +4 initial values; +8 current values) (0x466ED0) |
| +0x14 collision set | 0x68 | only the first 0x44 bytes (0x417710) |
| +0x20 sector | 0x14 | |

The shot template in Learn_10: the intelligence's actions table holds 3 entries of 0x68 bytes, the
variables are 0x145 bytes with an initial buffer, and the 3D data names an initial state (+0) but no
current one (+4).

## Going away

Procedure 139 takes an actor out (0x4062A0); for a generated one (id 0x10000 and up) 0x40C0C0 frees
the slot. The pool's own sweep (0x40C2F0, called from 0x40ADEF) also frees an actor whose custom bits
(standard game +0x24) carry 0x20000 once its animation has played out for good (0x410310: the marker
0x40E800 sets at 3D data +0x8E when no pass is left and no state follows, and no other state is
waiting). The shot's custom bits are 0x00020008: after 1500 ms its behaviour 1 puts it in its fading
state (04/00 +0x149B8: 25 frames at 60 per second, nothing after), and the sweep removes it at the
end. Its animations carry no events.

## The shot

Viewer lifetime bug found 2026-10-31 (own source analysis): generated templates must save their
numeric state addresses during level loading. Their `memory_location` values refer to blocks in
the local `level_files`, destroyed when `load_level` returns. Resolving those locations when a
shot spawns in interactive play reads freed memory and can prevent the viewer from matching its
current state, leaving its displayed animation unchanged until removal. `--time` screenshots
masked this because their simulation and initial pose run inside `load_level`, before destruction.
The viewer now captures these addresses at load, as it already did for placed actors.

- Behaviour 0 picks the object list by charge and story (procedure 94 writes 3D data +0x10, which the
  pose code 0x40E0D0 draws; four lists at 04/00 +0x14A44.. +0x14AC8), its flying state, a launch
  vector of length 25 along Rayman's facing (`var13`), a lifetime of 1500 ms (`var2`), moves itself
  0.8 up in Rayman's axes (procedure 160) and sets the vector as its imposed speed (procedure 182),
  then switches to behaviour 1.
- Its rank: `var1` = how many of Rayman's shots are out, itself included. It thinks every 2nd
  frame (procedure 341(2)), and every 4th of those (128 ms) while 5 or more are out (condition 9 is
  "at least"), every shot counts its rank down; at 0 it fades early. Fading shots still count, so
  with five out the oldest fade one after another until fewer than five are left. Seen live (real
  game, five shots 190 ms apart): ranks 1..5, the countdown in 128 ms steps, the oldest four fading
  after 0.9, 0.8, 0.7, 0.7 s, the fifth at its 1.5 s timeout; the fade itself takes about 0.43 s.
- Its state's mechanics (05/01 +0x62A454, kind 1, flags 0x300) puts flag 0x200 in base +0xC: the
  fast mechanics, which moves it by its speed every tick (see [dynamics.md](dynamics.md)).
- Behaviour 1 casts ahead (functions 198/199, macro 3) and bounces off what it meets up to twice
  (`var3`, function 209), then removes itself; procedure 22 lists the actors its zones touch, and
  procedures 36/37 drop other shots (its family) and one model from that list before it hits the
  first one left (procedures 232..234 on the target's collision set). Function 113 looks for a target
  to home in on (mask 32, angles 35/35).

Actor lists (0x483120..): u8 count, u8 capacity, then super objects, 4 bytes each. Condition 41 is
"empty", functions 49/50 the count and the entry at an index (255 the last).

## Where this came from

Own analysis of Rayman2.exe (Ghidra decompile and disassembly, read for understanding only), with
GPT-6.1 Terra (Codex, read-only) tracing the pool, the copy callbacks, the fast mechanics and the
sweep; every size and offset above was checked against the Learn_10 template's memory with
`ortool peek` (actor 04/00 +0x14708, its mind at 0d/00 +0x4E3EC, its intelligence at 0d/00 +0x52164,
its variables at 0d/00 +0x4E8A0) and the dynamics allocator 0x412CA0. The rank countdown and the pool size
were checked against the real game with `tools/r2live.py --generated`. Not done yet in OpenRayman:
where the pool's capacity comes from (we use Learn_10's 40), function 113's target search.

## Shot collision events

The procedure 22 query (0x4837A0, 0x40CCA0 and 0x40CD30) compares a kind-2 zone
(collision set +0x04, modes at +0x34) against other actors' kind-2 zones, with a
material flag mask and the source/target exclusion flags. Sphere and box entries
participate. OpenRayman tests all actors rather than sector lists, checks mode 0 against the selected group, and omits box-box and part geometry tests.

The shot template's kind-2 collection selects a group containing zone 0 in both of its states.
Selecting that group on creation and state changes enables the zone when its 2-bit state is 0
(own analysis of Rayman2.exe with GPT-6.1 Terra; see [collision.md](collision.md)).

The target collision set stores event vectors at +0x44 and +0x50, floats at +0x5C
and +0x60, a byte at +0x64 and the priority at +0x65. Procedures 232..234
(0x469B30) write at equal or higher priority; functions 205..207 (0x4788E0)
read the running actor's values. Condition 66 reads and clears the priority byte.
Procedure 333 (0x46E360) clears super object flag 1 for a nonzero argument and
sets it for zero. These details come from the project's own executable analysis,
provided in the implementation task, and have not yet been checked in live play.
