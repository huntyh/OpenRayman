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
where the pool's capacity comes from (we use Learn_10's 40).

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

## Target search and aim correction

Function 113 (0x476960, candidate ranking 0x476300) is implemented. Its seven arguments are a
custom-bit mask, three scoring weights, a world search direction, and horizontal/vertical limits
in degrees. It returns an actor or no actor. Candidates need every requested custom bit at
standard game +0x24; standard game +0x29 bit 0 excludes them. The shot supplies mask 32,
Rayman's variables 107..109, its flight direction and limits 35/35.

The executable walks the current sector's actor list, then its +0x24 neighbour list. The latter
has first/last/count at sector data +0x24/+0x28/+0x2C; each link stores the sector super object
at +4 and the next link at +8. Only this list is used, without recursively visiting neighbours.
OpenRayman discovers sectors through the scene links when `set_actors` runs. Queries classify
placed and generated actors by their current position using the same smallest non-virtual box,
or nearest box, rule as `level_lights::sector_at`. This avoids using the saved actor lists after
movement or generation. Within each sector candidates follow runtime actor order; exact ties can
differ from the executable's live list insertion order.

The search frame's back axis is the negative normalised flight direction; its up axis comes
from the source actor. A cross product supplies the sideways axis. When back and up nearly agree
(abs(dot - 1) < 0.002), the old sideways axis supplies a replacement up axis. The search then uses
the **transposed rotation** (0x444B80), ignoring source stretch and keeping the cross product's
length, even for tilted shots; this is not a general matrix inverse. For a candidate centre's
dot products with those axes `(x,y,z)`, let
`h = sqrt(x*x+y*y)` and `d = sqrt(x*x+y*y+z*z)`. The angular tests are strict:

```
-y > h * cos(horizontal_limit)
z / d < sin(vertical_limit)
```

The elevation comparison really is signed in the executable: a target far below can pass.
For a forward candidate (`y < 0`), the score is `w0/abs(x) + w1/h + w2/d`. Exactly zero lateral
offset gets 3.402822e38 (slightly below the largest finite float), without division. Wider-than-90-degree searches can also
admit rearward candidates: their score is `-abs(y/x)`, or the negative of that constant when x is
zero. A strictly greater score replaces the winner;
equal scores keep the first candidate.

Function 154 (0x4782C0) returns the target's kind-2 collision centre as an **offset**, zero if
none qualifies. Its helper 0x476630 searches active zones using the same state/group selection as
collision queries (0x40C700). Zone material matching (0x40C7C0, 0x42DB70, 0x42DE00) requires all
bits of the requested mask in the collide material's u16 +2; mask zero accepts any material.
It checks materials throughout the zone, then 0x476880 obtains the centre from only the first
element: the first sphere's actual centre vertex (kind 7), or the midpoint of the first box's
two vertices (kind 8). Other first element kinds do not supply a centre. The material accessors
are 0x41E360 (triangles), 0x41E5F0 (single sphere), 0x41E790 (sphere array), 0x41EA50 (boxes),
and 0x41EE00 (capsules).

For direct zones, function 154 includes the actor's stretch in that offset but leaves rotation
to the script. If there is no qualifying direct zone, the helper searches 3D data +0x98's part
list: link +0 names a part super object, +8 is the next link; part flags need 0x18 and must not
have +0x30 bit 1. The part's physical object kind-2 geometry is at
`[[part+4]+4]+8`, transformed by part +0x24. For this fallback the executable applies the inverse
full actor transform for the local result (0x444D60). This path reads the available runtime part
transforms; animated part attachment tracking is not added by this change.

Ranking asks that same helper for a **world** centre with material mask 1, falling back to the
actor origin. After selection the script calls 154 with mask 1, applies function 53 in the target's
axes, adds its world position, and aims at the resulting point. Its world cast rejects a direction
blocked before the target. This line-of-sight check stays in the script. The search runs at launch
and after bounces, rather than continuously during flight; the correction is a change of launch
direction, not a per-frame turn.

### Reproduction and evidence (2026-11-02)

The last Learn_10 cage is **node 53**, origin approximately (32.6,106.1,33.9). Use the default
revisit start, `ORTOOL_PLACE=24.8,125,24.25,270@240`, and:

```
ortool run "F:\Games\Rayman 2" Learn_10 410 53 "Space:300-305"
```

`ORTOOL_SHOTS=1` adds each Rayman-generated actor's position, imposed velocity, selected target
and current sector every tick. Space creates the shot at tick 309. Before these handlers it flies
at z=25.89 with x=24.78, missing the cage by about 7.8 units sideways. With them it selects node 53
and its velocity becomes (9.6236,-21.4180,8.5824), length 25:

| Tick | Before: position | After: position |
| --- | --- | --- |
| 309 | 24.78,123.11,25.89 | 24.9300,123.1703,26.0233 |
| 320 | 24.78,118.51,25.89 | 26.7007,119.2293,27.6025 |
| 340 | 24.78,110.19,25.89 | 29.9054,112.0972,30.4604 |
| 350 | 24.78,106.01,25.89 | 31.5125,108.5203,31.8937 |

The cage gets priority 10 and strength 1 at tick **352**, then changes state at **353** (behaviour
4, state index 9 / animation 436). Facing yaw 90 from the same position leaves no target in the
cone: the shot keeps velocity (0,25,0), e.g. (24.8240,131.4871,25.8860) at tick 320.
Regression tests cover the hit, the same shot with search disabled, the empty cone, moving between
sectors, linked-sector ranking, masks and exclusions, the signed elevation test, and a rotated,
scaled box's centre.

**Where this came from:** our own reading of the Ghidra decompilation at the addresses above,
checked against the original executable's x86 disassembly for the score, angle signs, frame
construction and special float constants (0x49CCB8/0x49CCBC, tolerance double at 0x49CCD0).
The shot's script comes from `ortool brain ... Learn_10 04/00+14708`; positions and reactions
above come from before/after runs against the GOG data. The real-game `run4/generated.txt`
records nearby shots at 391.078s and 391.484s, but logs creation/state/removal rather than every
flight position, so it does not validate the exact trajectory or hit tick. Visual comparison
in the running game remains a human check. No reference-project code was used.
