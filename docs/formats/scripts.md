# Scripts: the actors' brains (Rayman 2 PC)

Handler census findings (own analysis of `Rayman2.exe`): function 210 (0x478AA0) returns
the global position of the selected 20-byte entry in 3D data +0x90; absent entries fall back
to the target, and channel 255 yields a zero vector. Procedure 66 (0x472F90) is an empty handler;
procedure 237 (0x4675A0) writes its argument's low byte to the target's brain +0x08, and
procedure 438 (0x471FB0) writes its argument's low byte to standard game +0x33.

Almost all gameplay lives in the levels as compiled scripts: what Rayman does when you press a
button, how a Lum flies, when a cage breaks. `Rayman2.exe` runs them with a small tree-walking
interpreter. The game keeps no names for anything here (no strings in the exe or the data), so the
names below are ours, given from what the exe's code does. Everything lives in the SNA memory
image; offsets are from the start of each structure.

## From an actor to its scripts

```
actor (perso)
  +0x0C → brain
           +0x00 → mind
                    +0x00 → AI model           shared by every actor of one kind
                    +0x04 → intelligence        the normal behaviour being run
                    +0x08 → reflex intelligence the reflex being run, if the model has reflexes
                    +0x0C → variable memory
```

**AI model**

| Offset | Field |
| --- | --- |
| 0x00 | normal behaviours: a list |
| 0x04 | reflexes: a list (the same kind of behaviours, checked before the normal one) |
| 0x08 | variable declaration |
| 0x0C | macros: a list |

A **list** is the array, then a u8 count.

**Behaviour** (0x0C bytes, "comport" in the engine): +0x00 the rules (an array of scripts), +0x04
the schedule (a script, or null), +0x08 u8 number of rules. Each frame the interpreter runs every
rule (0x480E00), then the schedule (0x480ED0). The schedule can stop partway, and the
intelligence remembers where (+0x04), so it resumes there next frame: that's how an actor waits.

**Intelligence**: +0x00 the model, +0x04 where the schedule stopped, +0x08 the current behaviour,
+0x10 an evaluation stack.

**Script**: one word, the address of its nodes.

**Macro** (8 bytes): the script as saved, then the copy the game runs (they start out the same; the
interpreter copies the first over the second before each run, 0x480220). Subroutine nodes point at
the macro. One macro in the whole game is empty (Astro_10, both pointers null).

**Variables** ("designer variables", DsgVars): the declaration is +0x00 the default buffer, +0x04
an array of descriptions, +0x08 u32 buffer size, +0x0C u8 count. A description is 16 bytes: u32
offset in the buffer, u32 type, then two words not decoded yet. The variable memory is +0x00 a
pointer to the model's declaration pointer, +0x04 the initial buffer, +0x08 the current buffer (the
actor's own values). Reading variable n: description n's offset into the current buffer, read by
its type (0x4815F0).

## Saved variables

A variable declaration's +0x08 holds save flags: 0x466BD0 registers the flagged ones for the
general (0x80) or level save. In Learn_10 only Fix actor 6 has any (variable 42 is the story-bit
array). The whole picture is in [saves.md](saves.md).

## Script nodes

8 bytes each: u32 parameter, u16 cache (the interpreter stores how many nodes to skip there the
first time it skips a block), u8 indent, u8 type. A script is a tree written out in order: a node's
arguments follow it one indent deeper. The first node has indent 1; the script ends with an end node
(type 8) at indent 0.

Evaluating a node (0x480B90) jumps through a table of 41 handlers (0x4A4180) by type. Each handler
returns the next node and leaves a value and a value type (a u32 after 12 bytes of value).

| Type | Node | Parameter |
| --- | --- | --- |
| 0 | keyword | index into 20 keywords (0x4A5E80) |
| 1 | condition | index into 149 conditions (0x4A5EE0, 8-byte entries) |
| 2 | operator | index into 28 operators (0x4A6398) |
| 3 | function | index into 314 functions (0x4A5460, 8-byte entries) |
| 4 | procedure | index into 514 procedures (0x4A43E0, 8-byte entries) |
| 5 | meta-action, takes several frames | index into 12 meta-actions (0x4A6418, 20-byte entries) |
| 6, 7 | no-ops (value "nothing") | not used by the data |
| 8 | end | |
| 9 | an actor field | 0..17 |
| 10, 11 | a variable of the running actor | its index (10 is not used by the data) |
| 12 | integer | the value |
| 13 | real | the float's bits |
| 14 | input action | pointer into block 06/00 |
| 15 | constant vector | pointer to 3 floats (not used by the data) |
| 16 | vector | none: built from its 3 arguments |
| 17 | bit mask | up to 0x4000 |
| 18 | small number, not identified yet | 0..48 |
| 20 | text | pointer to a zero-terminated string (level names) |
| 22 | family | pointer |
| 23 | actor | pointer |
| 24 | family state | pointer |
| 25 | super object | pointer |
| 26 | waypoint | pointer: position and radius |
| 27 | not identified yet | mostly 0x5000..0x5100 |
| 28 | behaviour or reflex | pointer, of any model |
| 30 | sound event | an id the sound engine looks up (procedure 115 plays it; 3d) |
| 31, 36, 38 | pointers not identified yet | |
| 32 | game material | pointer; +0 points to its visual material (procedure 503 draws particles with it) |
| 35 | AI model | pointer |
| 37 | flags, not identified yet | |
| 39 | subroutine: runs a macro in place | pointer to the macro |
| 40 | ignored | |
| 19, 21, 29, 33, 34 | not used by the data | |

Over all 70 levels every node fits: table indices stay below their table's size, and every pointer
type always holds a pointer, every other type never does.

### Keywords

| # | Keyword |
| --- | --- |
| 0 | if *condition* |
| 1 | if not *condition* |
| 2–5 | if *condition*, checked only every 2, 4, 8 or 16 frames (a frame counter at 0x4A7070; on the other frames the block is skipped) |
| 6, 7 | the statements one indent deeper run unless a global switch is set (0x4A66BC, 0x4B9C34; meaning not known yet) |
| 8 | then: the statements one indent deeper |
| 9 | else |
| 10 | jump: moves by a computed number of nodes |
| 11 | me (the running actor) |
| 12 | the main actor (Rayman) |
| 13–19 | empty values: no actor, no super object, no waypoint, three others, no state |

The every-N-frames ifs spread work over frames to save time on 1999 computers. Learn_10's scripts
use them about a hundred times. OpenRayman may check them every frame where that doesn't change
gameplay (decided when the interpreter runs, milestone 3b).

### Operators

| # | Operator |
| --- | --- |
| 0–3 | + − × ÷ (integers or floats) |
| 4 | negation |
| 5–10 | += −= ×= ÷= ++ −− (from the shared assignment handler; to be confirmed one by one) |
| 11 | := |
| 12 | another actor's variable: *actor*.*variable* |
| 13–15 | a vector's x, y, z |
| 16–20 | the same as 0–4, for vectors |
| 21–23 | assign a vector's x, y, z |
| 24 | evaluate the next expression as another actor |
| 25 | treat an actor as having a given AI model (to read its variables) |
| 26 | array element |
| 27 | assign an array element |

Every operator in the game's data has exactly the number of arguments its form needs.

Conditions, functions, procedures and meta-actions without a name yet are printed by number; each
gets a name when its handler is read from the exe and implemented. `ortool brain <game> <level> <node>`
prints an actor's scripts in this form.

## Running the scripts

**Which actor first** (0x40ADA0, the actor frame, from the main loop through 0x49F37C): pass 1
(0x40AA10) runs the active world's (0x500FD0) children from the head while they are moving
(standard block +0x28 non-zero) and stops at the first static one; pass 2 (0x40A7C0) walks the
world tree for the rest, carriers before what they carry; pass 3 (0x40BAD0) a player-slot list.
Each actor runs once per frame (standard block +0x18 stamped with the frame number 0x5003E4).
Waking an actor (0x40B4C0, from the wake pass 0x40B620) moves it from the inactive world
(0x500FC4) to the active one: a moving actor at the head, a static one at the tail. So the
order is the moving actors newest first, then the static ones in the order they woke. Per actor
(0x40A5C0) the animation steps first, then the brain and mechanics (0x412230); the clock and
frame number advance after all actors (0x409DD0); effects and screen text are drawn in a later
pass (0x40B150 → 0x419A30 → 0x460670 → 0x465C10). The Jail_10 intro actors (nodes 2..13) are
all static, so our tree order matches there. Where this came from: a DeepSeek read of the
decompile (2026-11-06), with 0x4120D0, 0x418170/0x418340, 0x40AA10 and 0x40B4C0 checked by us;
the helper had the head/tail of 0x40B4C0 reversed.

Each frame, for each actor (0x4120D0):

1. Its brain may be slowed: 3D data +0xFE is a period set by the scripts (procedure 341), +0xFF counts
   frames, and the brain runs only when the count wraps to 0. A 1999 time saver; OpenRayman runs
   every brain every frame unless asked to honour it.
2. 3D data +0x100 (u16) counts the frames the actor has thought; it is raised before the rules
   run and its low byte is copied to 0x4A7070, which the every-N-frames ifs test (keyword handler
   0x489250, jump table 0x48932C: every 2/4/8/16 frames = (count & 1/3/7/0xF) == 0). The 3D data
   constructor (0x418170) and the state reset (0x418340) start it at a pseudo-random 1..17
   (0x449C10(0, 1, 0x11)), so the actors' every-N tests are staggered and differ from run to run.
   OpenRayman keeps the value from the level file.
3. The reflex intelligence runs first (mind +0x10 = 0 while it does). If it didn't start a
   meta-action, the normal intelligence runs (mind +0x10 = 1).

Running an intelligence (0x480D80): run the behaviour's rules, then, unless a rule stopped or a
meta-action ran, its schedule from where it last stopped. If a behaviour changed (flag 8), run the
new behaviour's rules the same frame. The rules (0x480E00) run in order and the first one that
doesn't reach its end stops the rest. The schedule (0x480ED0) starts over once it reaches its end.

A statement block (0x480BD0) runs statements in order and stops on these flags in a statement's
result: 0x01 stop for this frame, 0x02 wait here (the schedule resumes at this statement), 0x08
the behaviour changed, 0x10 end of the script. Procedures report flags through a global the
procedure node returns (changing behaviour sets 0x08).

**Actions in progress** (intelligence +0x10): +0x00 entries, +0x04 u8 capacity, +0x05 u8 used,
+0x06 u8 current. An entry (0x68 bytes) is 0x60 bytes of scratch for the meta-action, +0x60 the node
to resume, +0x64 u8 used, +0x65 u8 which script (0 the schedule, n rule n), +0x66 and +0x67 saved
signal state. Meta-actions and macros waiting inside a script keep their place here.

**Changing behaviour** (procedure 69, 0x469600, 0x481080): the old behaviour goes to intelligence
+0x0C (scripts read it as fields 7 and 8), the new one to +0x08, the actions in progress are
cleared, and the schedule starts at the new behaviour's beginning.

**Hit points and a custom bit.** Own analysis of `Rayman2.exe` (2026-11-01): procedure 0
(0x467D90 → 0x467E80 → 0x4949C0) writes the low byte of its second argument to the named actor's
standard game +0x21 (hit points). Procedure 6 (0x467FA0 → 0x468060 → 0x494A90) writes it to
+0x22 (maximum hit points); function 46 (0x47C940 → 0x494A80) reads that byte. Neither changes
the animation or state, and these direct writes do not clamp the other hit-point fields.
The scripts use maximum hit points as a mailbox: `var0 := function_46(me)` then
`procedure_6(me, 0)` reads a number another actor left there and clears it (node 45's end scene
steps through its states this way, nodes 2 and 39 the same).
Procedure 318 (0x46DEA0) sets or clears standard game +0x24 bit 0x100 on the selected actor.
**Switching actors on and off.** Own analysis of `Rayman2.exe` (2026-11-02): standard game +0x29
bit 1 means "active"; the frame loop (0x40AB90) runs only active actors. Procedure 84 (0x467970 →
0x405FA0) moves the actor into the dynamic world and sets bits 1 and 2. Procedure 85 (→ 0x4061B0)
tears down its animation links and sounds and, if it is in the world (bit 0 clear) and active,
clears bits 0..2. The Globox kids' director switches its stand-in Rayman (node 41) off at the
start and at the hand-back. Procedure 139 on a placed actor (0x406220) sets bit 0 and, unless
type 5, bit 1, then for type 1 resets it (0x405E50 mode 4, whose 0x4132A0 puts it back in the
world) unless its id matches one of the level's always actors (0x40BC80, list at 0x4A6B1C).

**Fields** (type 9, table 0x4A6598, 12-byte entries: kind, get, set): 0 position (super object
+0x24, the global transform: position at +4), 1 a direction, 2 and 3 speed (vector, length), 4 to 6
not used, 7 and 8 the previous normal and reflex behaviours, 9 and 10 the shadow size (3D data
+0xD4, +0xD8, settable), 11 to 17 seven global registers shared by every actor (a vector, two
16-bit integers, three floats, an integer; settable).

**Variable types** (table 0x4A4278, 12-byte entries: size, get, set): 0 to 6 integers (signed and
unsigned 8, 16, 32 bits), 7 float, 8 waypoint, 9 actor, 10 a buffer reference, 11 vector, 12
behaviour, 13 state, 14 an id (-1 = none), 15 and 16 references not identified yet, 17 waypoint graph, 18 to 23 arrays
(u32 element type, u8 count, elements from +8; vectors take 12 bytes), 24 super object.

From an actor to its super object: actor +0x04 (standard game) +0x0C (0x4136D0).

**Actor variables are stored as super objects.** Type 9's getter (0x481350) reads the stored
pointer, checks that the pointed-to object's type is 2, and returns its actor at +4 as value
type 0x0B. Its setter (0x481680) converts the evaluated actor back through its standard game's
super object pointer. This applies to array elements of type 9 too. Returning the stored pointer
as an actor prevented cross-actor variable access and calls through these variables from working.

### Native startup checks and Learn_10's hanging cage

Own analysis of Rayman2.exe with GPT-6 Astra (2026-10-31), checked against the GOG executable
and Learn_10 data; no reference-project code or native payload code was copied.

There is a startup pass outside the script interpreter: 0x404550 selects actors whose standard
game custom bits include 0x01000000 (bit 25), then calls 0x467210 and 0x467350 on their minds.
The latter recognizes initial var0 = 2. It decodes embedded native code in initial var1 and its
argument in initial var3, invokes the code with the helper table built by 0x467130, and writes
the returned integer to initial var4. These are ordinary type-21 integer arrays, not special
variable getters. Their elements begin at +8; decoding XORs each word with the preceding
encrypted word, seeded from the first word XOR (first word OR 0x00EC8B55).

Learn_10 node 82 has this bit set (custom bits 0x0100A000). Its initial var0/var2/var4 are 2/5/0,
whereas its saved current buffer contains 100/1/0. Its decoded probe temporarily changes the
Windows error mode, calls helper slot 4, restores the error mode, and returns the helper result.
That drive-type helper, 0x448EC4, returns 5 unconditionally in the supplied GOG executable.
The probe's first 47 decoded bytes have FNV-1a-32 fingerprint 0xB5546264.
After the startup pass, 0x4062F0 initializes actors; the brain path reaches 0x466D00/0x466A60,
which restores each variable whose declaration +0x0C is nonzero from the initial buffer.
Thus the script compares 5 with 5, not the stale saved 1 with 0. Declaration +0x08 is separate
post-initialization processing (0x466BD0), not the restore flag.

**All the CD checks.** `ortool run <level> 0` lists every actor with custom bit 25: 65 actors in
25 levels, every one with this layout (mode 2, var1 200 bytes of code, var3 a 50-byte argument,
var4 the result, var2 the expected answer) and every one with the same script: equal results count
towards a counter actor, any other sets the pirate flag (Learn_10: `actor 6.var63 := 1`). The probe
code gets the argument (a path; with var5 set, its first letter becomes the CD's drive letter,
0x4A6AD8) and a table of file functions filled by 0x467130. The kinds seen, by what the code does:
the drive type (10 actors, Learn_10 among them: the 0xB5546264 kind), clearing a file's read-only
attribute and reading the attributes back (expected 57931 = 0xE24B x 1: still read-only), a file's
size times 0xD21F, comparing two files' times, and creating a file, which must fail (expected 0). In
the GOG executable that table points at stubs (0x448xxx) instead of the Windows functions, which
answer as the CD would, so every probe passes. `initialize_native_check` therefore gives every probe
of this layout its expected value (var4 := var2) and restores the scalar inputs/results before
scripts run; it never runs the embedded x86. A probe with another layout would be left alone (none
exist in the 70 levels). Own analysis of Rayman2.exe (0x467350, 0x467130) and the probes decoded
by `ortool run` with `ORTOOL_PROBES=1` (output kept outside the repository), 2026-10-31.

The resulting wake path uses the unmodified scripts:

- Node 82's behaviour 1 adds its var7 (10) to node 83's var12 through var6, clears node 83's
  custom bit 17, then removes itself.
- Node 83 compares its count with var0 (3), clears custom bit 17 on its var1 (node 67), clamps
  its count to 3 and puts its own brain to sleep. Procedure 311 is not needed on this path.
- Node 67 can then run behaviour 0. With story bit 840 clear and var1 = 0, it selects state
  04/01 +0xFB10, animation 427 (cage and chain), and changes to behaviour 1.

Both the missing startup result and the type-9 pointer conversion were necessary fixes. No
special wake-up in Murfy or the tutorial controller, nor a change to actor ordering, was needed.
In a 600-tick `ortool run` with `ORTOOL_START=new`, node 67 changes from animation 433 to 427
at tick 1 (live state address 0x07E5FB30). The default revisit start follows the same wake path;
it sets story bit 1133 but does not imply that bit 840, the cage-broken flag, is set.

**Story bits of a new game.** Logged in the real game with `r2live.py --story-only`
(`H:\r2export\live\story_newgame_2026-11-01.txt`; a new game, the pirate ship, then all of Learn_10 into the Hall of
Doors). They are actor 6's variable 42, a type-21 bit array, numbered as function 248 counts them.

| Bit | Set when | Read by (Learn_10) |
| --- | --- | --- |
| 1089 | the pirate ship level ends | — |
| 1091 | the intro hands Rayman over, at (30.27, 295.21) | node 4 (an intro effect, particle 110, until set) |
| 1176, 1175, 1177 | Murfy's talks: by the stone, the Lum tutorial, near the bridge | set through variables |
| 840 | the tutorial cage breaks | node 69; node 4 counts 840..841 (function 249) |
| 1396..1400 | each of the five Lums is collected (one bit each) | the scene directors (1399, 1400) |
| 1092 | talking to the Globox kids | node 39: crying pose, brain off (1095, the silver shot, makes them cheer) |
| 841 | the Teensies' cage breaks | nodes 2 (the end scene's Rayman), 4, 62 |
| 1133 | jumping into the exit gate: the level is finished | nodes 4, 43, 62 |
| 960, 961, 989 | the Hall of Doors: arriving, Murfy's tutorial | — |

1027, 1028, 1059 and 1060 survive starting a new game (options or the save slot, not the story).
Our starts set 1089 and 1091 for a first visit and 1089, 1091 and 1133 for a revisit
(`first_visit.h`). The recorded completed game retains the first two bits when 1133 is set.
Node 4's startup behaviour draws a full-screen kind-110 rectangle at constant alpha 249 and
switches to behaviour 4. That behaviour keeps drawing it while the camera is in its intro
behaviour and bit 1091 is clear; otherwise it switches to behaviour 1. The portal shortcut used
to set only 1133, leaving this intro cover stuck on screen. Where this came from: own analysis of
node 4's script and the `run4` story recording.

**Scene directors.** Six Learn_10 actors (nodes 3, 38, 46, 56, 57, 58) share one model, 0d/01
+0x11BEC: a director that hands a scene from Rayman to a stand-in. Its var2 is the actor whose
custom bit 28 arms the scene, var3 the subject (Rayman), var4 the stand-in, var1 a waypoint and
var6 a distance. In behaviour 5, while var2 has bit 28 and Rayman is not in the portal arrival
(the `actor 84: condition_78(...)` test), it hides Rayman (procedure 312), sets his custom bits 14
and 17 (brain off), puts the stand-in where he stands and lets it walk the scene; at its end it
clears var2's bit 28 and removes itself. Node 38 is the intro's (var2 itself, stand-in node 40): on
a first visit it takes Rayman over at tick 1. The type-9 fix above is what lets var3 read back as
Rayman, so before it the scene never took hold of him. Until cutscenes play (Phase 4) the
first-visit start ends node 38 the way it ends itself (`game/include/openrayman/game/first_visit.h`).
Own analysis with `ortool brain`/`run` (2026-10-31).

**Custom bit 14 (0x2000) freezes the animation.** The per-actor update 0x40A5C0 skips the
animation step (0x40E9E0, which also fires its sound events) when the actor's custom bits
(standard game +0x24) carry 0x2000 and its super object has children (the parts), unless 0x800000
asks for it anyway. Scripts put an actor away with `procedure_312(1, 1)` and `procedure_174(14, 1)`
together: Learn_10's end-of-level Rayman (node 2) waits that way until the last cage breaks. Its
idle animation's "Hey!" stayed audible while we kept animating it. Brain and mechanics still run
(0x412230 follows unconditionally). Own analysis of the Ghidra decompile (2026-11-01).

**Waypoints and graphs.** A waypoint has three floats at +0 for position, a float radius at
+0x0C, and an optional super object pointer at +0x10. When attached, its position is transformed
by that object's global transform; otherwise it is already in world coordinates (0x487860).
Function 18 returns that world position (0x47B860), function 17 measures distance to it
(0x47B8D0), and condition 74 tests the actor against the waypoint sphere (0x48B190).
Learn_10's waypoint 0d/01 +0x46C7C is unattached and reads (30.2676, 295.213, -3.66803).

A graph has a first node pointer at +0 and node count at +8. Nodes link through +0, name a
waypoint at +0x0C, hold flags at +0x10 and point at an outgoing link list at +0x18 (0x480050,
0x47FD20). A link has next at +0, destination node at +0x0C, capability bits at +0x10 and an
integer cost at +0x18 (0x480190, 0x4801F0, 0x487B10, 0x487B90). Function 114 selects the
nearest waypoint (0x477630); its nonzero second argument also requests an obstacle test, which
OpenRayman does not yet implement and these actors do not request.

Functions 122 and 123 (0x4770F0, 0x47FD20) build an actor's path starting at a waypoint, repeatedly
choosing the cheapest link to an unvisited node; 123 makes path indexing wrap. Function 126 reads
the current index, 130 advances it, 132 reads a waypoint by index and 133 reads the capability bits
of the link leaving that index (0x477300). Condition 109 (0x48B7A0) tests a numbered capability bit.
The path state belongs to the actor and is replaced by the next path-building call.
Function 181 (0x47C350) blends two vectors: the second argument weights the first vector and
one minus that weight multiplies the third argument.

These waypoint and graph findings are own analysis of Rayman2.exe with GPT-6.1 Terra.

**Turning and moving.** Procedure 171 (0x469CB0) turns an actor so that two given directions A and
B become two of its axes, picked by a mode (tables 0x46A848, 0x46A860; 0x445F20 splits a
transform into its X, Y, Z rows): A becomes -Y, -Y, Z, Z, X, X and B becomes Z, X, -Y, X, Z, -Y for
modes 0 to 5. A zero vector keeps the current axis. A stays exact, the third axis is the cross
product of the other two, and B is rebuilt from them (the mode 5 case at 0x46A086). Procedures 176
and 182 set a speed at dynamics +0x24, in the actor's own axes (176) or the world's (182, bit 0x4000
of +8). Node type 30 is a sound event: procedure 115 with one is a sound request (the invisible
actors 31 and 32 of Learn_10 only play ambient sounds).

**Dialogs: function 158** (0x475680). It makes and updates a generated text
actor, reveals its text over time and returns events to the script; procedure 139 removes it. The
text argument is a string or a text id (0x4829D0 → 0x494C70): ids below 20000 index the current
language's string table (`*(0x50026C) + language * 8`, first word; language = low byte of
0x500FA8), 20000 and up a shared table (`*(0x500270)`, id − 20000). Strings are NUL-terminated
bytes with control codes read by 0x409060: `/L:` new line, `/C:` centre, `/H`, `/W`, `/Z` layout,
`/E<n>:` returns event n, `/D<n>:` and `/P<n>:` the reveal timing, `/%I…:` and `/%F…:` insert
values. Murfy (node 42) builds each page with procedure 430 (0x470560, appending `/e9:`), calls
function 158 until it returns 9, then waits for a fresh press of Space (condition 45). His box is
drawn separately, by particle kind 110 (procedure 503) between two corners. The Globox kids'
scene (director node 3) steers the kids and the camera by function 158's events, so without it the
scene stalls. The 30 text actors are listed at 0x4FF8C0 (free markers 0x4FFD40; 0x14-byte state
records at 0x4FFDC0: +4 timing, +8 last update, +0xC last event, +0x10 i16 reveal cursor into the
prepared string). The menu's 50 screen slots (`menu.md`) are a different path. From a helper
(GPT-6.1 Terra, read-only, 2026-11-01), its addresses checked against the decompile.

**Dialogs: implementation notes.** Own analysis of the decompile at 0x475680, 0x409060,
0x409720, 0x470560 and 0x469AC0: procedure 430 resolves three text arguments and writes the
first two concatenated into the third; procedure 243 resolves its first argument as a destination
and writes its second argument as a decimal integer (real values are truncated). The reveal
cursor counts bytes in the prepared string, including controls; `/E` emits an event, `/D` changes
the stored delay and `/P` asks for a temporary delay. Function 158 writes the resulting timing
back to its fifth argument. Generated actor removal releases its pool slot.

Own analysis of Fix loader 0x402960 and the relocated GPT stream: offsets 0xCB8 and 0xCBC hold
the language-record and shared-table roots, respectively. In this installation the language 0
record is at 0e/00 +0x125A0, its pointer array at +0x456D0, and the upper half of its second word
is 666, matching the recorded English IDs 0 through 665. The meaning of the lower half remains
unconfirmed. The resolver uses relocated live memory and currently defaults to language 0; reading
the config remains to be done. The shared table's bounds are not yet established.

The `game/dialog` module is registered by the viewer and ortool, and resolves IDs through those
tables. It stores changed ID strings separately and exposes revealed text. Parser 0x408DD0's `/O`
sets the glyph index offset used by 0x409720; its relationship to highlighting remains to be
confirmed. Procedure 443 (0x470270) sets the global placement mode used when function 158 creates
an actor: zero selects world placement, nonzero selects the screen branch. The viewer now preserves
that mode. The parser and timing are preliminary: substitutions are not implemented. The first-visit 3000-tick ortool run
did not reach Murfy's conversation, so it does not establish that he waits for Space. Set
`ORTOOL_TEXT=1` to print dialog event, clock, creator, ID and cursor for further comparison.
Own comparison against `run4/text.txt`: in Jail_10, ID 1 events 10, 11, 3 occur at cursors
12, 110, 114 and clocks 8300, 14350, 14366 ms in ortool; the recording has 8209, 14257,
14273 ms at those cursors. The full Learn_10 comparison and interactive Murfy check remain open.
The recorded dump's mutable ID 286 differs from its initial Fix contents, consistent with the
script writing that destination; the table tests therefore use unchanged IDs instead.

The placement byte initially contains 1, so new dialogs default to screen placement until
procedure 443 changes it. The glyph mapper at 0x409720 rejects bytes below 0x20; lowercase ASCII
and bytes above 0x9f subtract 0x40, and other printable bytes subtract 0x20, before adding the
current `/O` object-list offset. In Learn_10, Murfy's template's initial object list has 0x372
entries; the mapped index for `A` identifies a textured four-vertex, two-triangle object. The
viewer caches those objects and textures by template and draws their own triangles and UVs; it
measures the complete block before centring, even while characters are still being revealed.
Procedure 352 (0x46FD70, setter 0x408590) resets the width table and sets default line height
and glyph scale. In the Learn_10 first visit it supplies 0.38, 0.45 and 1.0. The `/-W` and `/-Z`
strings configure the width table and scale sequence without creating a dialog. The screen-space
glyph magnification remains provisional: it was enlarged by 25% after comparison with the user's
captures; the precise screen projection still needs confirmation, as do world-space text and other
resolutions. Where this came from: own analysis of the executable and loaded game data.

The physical object's visual set points at a chain of colour blocks: visual set +0x10 -> block
list, list +0 -> first block, first block +4 -> per-vertex colour block. The latter has a u32 count
at +0 and a pointer at +4 to four u16 RGBA channels per vertex. Murfy's `/O400` A has four colours:
the lower pair is orange and the upper pair pale, producing the yellow gradient on the glyph's
white texture. Other offsets select other painted objects. A null physical object means no glyph:
the final colon in Murfy's page strings maps to a null entry and is invisible. Where this came from:
own analysis of the loaded Fix objects and comparison with the user's original-game captures.

Murfy's portrait is not a generated actor: his macro calls particle kind 125 with slot 5, viewport
corners (4, 80) and (14, 95), a material pointing to the Murfy icon texture, and opacity in the
second vector's Z component. 0x4605A0 writes the slot and 0x460600 draws nonzero slots; a later
zero-opacity write clears it. Where this came from: own analysis of Murfy's script and the executable.

Learn_10 node 44 formats its Lum and cage counts through procedures 243 and 430 and sends them
to screen-text slots 15 and 16 with procedure 505. Procedure 505 resolves IDs through the same text
table as function 158 and retains the third position component as the slot size (0x465A50). The
viewer draws these slots with the available Fix alphabet objects; its menu-slot layout is still
approximate. Dialogs and screen images are rasterized at viewport resolution using 640x480 reference
coordinates. Dialogs have a provisional four-reference-pixel left inset: 0x409500 places each glyph
at the pen plus half its advance, but the remaining observed bearing difference is not established.
Where this came from: own analysis of node 44's script, 0x465A50 and 0x409500, and user captures.

Kind 110 (0x4648B0) makes a type-0x11 particle whose two vectors are opposite screen corners;
0x460A10 draws it once through 0x429820 and 0x429670 as a black rectangle, with the supplied
opacity. The coordinates are percentages of the viewport. The alpha setter at 0x42A300 divides
the value by 255 and clamps it: 249 is nearly opaque. The draw path checks that the supplied game
material exists, but 0x429820 uses its own black material; the supplied material does not choose
the rectangle's colour or opacity. Scripts renew it while it is visible.
Where this came from: own analysis of the executable; no reference implementation was used.

**Text boxes fade themselves.** The text actor template (Fix 04/00 +0x15CA8) has its own AI
model (in Jail_10 at 0d/00 +0x5242C, 3 variables): behaviour 0 waits for custom bit 28, switches
transparency on (procedure 11(1)) and moves to behaviour 1, which raises var2 by
var1 × function 62 / 16 a frame up to var0, writing it with procedure 12; custom bit 29 starts
behaviour 2, which lowers it the same way and takes the actor out (139) at 0. The narrator
(Jail_10 node 6) sets bits 28 and 29 with procedure 174 on its event codes 10 and 2/3, and its
codes 300..399 set the speed (var1). So the renderer only has to apply the text actor's super
object transparency (draw flag 8 at +0x2C clear, level at +0x38). Own analysis of the scripts
(`ortool brain Jail_10 04/00+0x15CA8`).

Procedure 393 (0x470F20, shared with 392 and 394) evaluates one argument and calls 0x416D70
on the object at the calling actor's +0x1C. It writes mode 1 to +0x38, the low byte of the
argument to +0x39, zero to +0x3A, and floating-point zero to +0x3C. The 600-tick first-visit
run reaches it 600 times; the trace includes a generated actor, but the caller has not yet been
identified. The object's role is not established, so this belongs in the actor handler system once
identified, rather than being registered as a dialog operation. Own analysis of the executable's
handler and setter; no reference implementation was used.

## Numbers

Over all 70 levels: 5,020 actors with a brain, 1,944 AI models, 15,679 behaviours, 2,151 reflexes,
6,920 macros, 37,845 scripts, 3,319,729 nodes and 47,915 variables. Learn_10: 82 actors with a brain
and 38 models; Rayman (node 84, from Fix) has 46 behaviours, 16 reflexes, 65 macros and 115
variables.

## Where this came from

- Own analysis of `Rayman2.exe` with capstone: the interpreter (0x480B90) and its handler tables,
  the behaviour runner (0x480D80, 0x480E00, 0x480ED0), the keyword and operator handlers, the
  macro runner (0x480220), variable access (0x4815F0). The structures were then confirmed against
  the data of all 70 levels (the tests in `tests/level_tests.cc`).
- Raymap was consulted for orientation (that actors have a brain, mind, AI model, behaviours, macros
  and designer variables); every offset and meaning above was confirmed in the exe and the data, and
  the names are our own.

**Taking an actor out: procedure 139** (0x467850 → 0x4062A0). Own analysis of the decompile
(0x4062A0, 0x406220, 0x405E50, 0x466CC0/0x481060, 0x466950, 0x418340), checked against the
`run4` recording. A generated actor (standard game +0x08 at or above 0x10000) frees its slot
(0x40C0C0). A placed actor leaves the world and is marked inactive (standard +0x29 bit 0x1),
and when its standard type (+0x14) is 1 it is reset as the level started it (0x405E50, mode 4):
its saved transform, both intelligences back to their initial behaviour (intelligence +0x14),
the designer variables whose reset level (declaration entry +0xC) is 4 or more from the initial
buffer (type 10: first byte cleared, second restored), and its 3D data's initial state and object
list. Learn_10's tablet tip (node 42, recorded actor 40) is such an actor: after each tip, and
after Murfy's first full scene, it is back at (7.414, 311.965, -6.993) in behaviour 0 and then
waits (behaviour 5) for Rayman to come near again. We keep type-1 actors in the world after the
reset; what re-activates them in the exe isn't traced yet. Only that one actor took this path in
our Learn_10 runs; the others procedure 139 removes are of type 0, 2, 3 or 5 and stay out.


## Handler census, second pass (2026-11-05)

Where this came from: own analysis of the local GOG executable's decompiled handlers and
small callees; descriptions and implementation are independently written. Addresses below
identify the observations, not source code to copy.

- Procedure 62 (0x472C40) takes a super object, zero-based draw bit, and mode 0/1/2
  (clear/set/toggle); it changes only super object +0x2C. Other modes leave it alone.
  Procedure 311 (0x46D8A0) instead changes +0x30, masking the shift to five bits;
  any nonzero third argument sets the bit.
- Procedures 334..339 (0x46E180, callees 0x4179C0..0x417A60) set/clear bits
  1, 2, 4, 8, 0x20, 0x40 of collision set +0x42. Only the argument's low byte matters.
- Procedure 439 (0x472020) stores its two arguments' low bytes at standard-game +0x2A/+0x2B.
- Procedures 264,265,267..269,271..273,275,277,293..295 (0x46C710) store a real at
  dynamics base +0x1C,+0x20,+0x154,+0x158,+0x15C,+0x1DC,+0x1E0,+0x1E4,
  +0x160,+0x164,+0x168,+0x16C,+0x170 respectively. Procedure 266 stores
  tan(degrees * pi/180) at +0x14. These are direct writes, with no clamping.
- Condition 69 (0x48A950, case 0x45) compares the named actor's current reflex with
  a behavior pointer. Condition 104 (0x48B6E0) checks the target's supporting actor
  (+0x284) when dynamics end flag 4 is set, otherwise checks whether the target is
  an immediate child of the named actor (children +8, siblings +0x14).

- Functions 126..132,137..138 (0x477300, callees 0x4170D0..0x417210): actor +0x18
  is a graph-path cursor (graph +0, current index +4, loop byte +8). Index 0 names the
  last node in the graph's forward list. 126 reads the index, 127 sets it (wrap/clamp),
  128 gives 0, 129 gives count-1, 130/131 advance/retreat without changing on failure,
  132 reads a waypoint by index, 137 finds an index by waypoint, 138 finds and selects it.
  An absent cursor/graph returns integer -1, including 132's absent-cursor case.
- Procedures 25/27/28 (0x4682B0; 0x483120/0x483340/0x4832B0) append an actor's super
  object, remove by byte index (255 = last), or remove the first matching actor.
  Procedures 32/33 retain a family's / model's actors; 36/37 remove those actors.
  Procedure 303 (0x46DA50) fills a list with the target's immediate actor children.
- Function 1 (0x47B830) reads the running actor's global position without an override.
  Functions 169..171 (0x474B60) read the midpoint of a kind-1/2/0 box, or vertex 0
  for sphere elements. No geometry returns zero.
- Procedure 146 (0x472600 -> 0x4249E0) clears dynamics base +0 bit 0x400000 for a nonzero
  argument, otherwise sets it. Procedure 150 (same handler -> 0x424B80) selects the
  nth (1-based) dynamics entry: list head +0x64, next +8, selected pointer +0x68;
  any index not found falls back to the head. Procedure 305 (0x46E3C0) sets/clears
  dynamics +8 bit 0x40000000 according to the argument's low byte.
- Procedure 270 (0x46C980) writes three real arguments to +0x154..0x15C. Procedures
  276/278 (0x46CA40) replace/add a vector at +0x184. The scalar/vector fields accept
  their real bits directly; unlike the byte-index helpers they do not coerce integers.
- Procedure 1 (0x467D90 -> 0x4949B0) writes a byte at standard-game +0x30;
  4/5 add/subtract a converted byte of health, bounded by maximum / zero
  (0x467EE0/0x467F40 -> 0x4949E0/0x494A30).
- Condition 124 (0x48BB70) returns menu-request byte 3 (truth as a condition).

- Procedures 307..309 (0x46BFE0, assembly checked at 0x46C01F) evaluate actor,
  zone index, then position. The index survives in EDI (the decompiler misleadingly
  prints the vector's low word). They translate kind-1/0/2 zones to put vertex 0
  or the first box corner at that position, keeping a box's extent. Procedure 420
  (0x471A90 -> 0x41E6C0) replaces kind-1 sphere radius. Ours invalidates the surface
  cache; native cached normals/bounds are not used by our zone tests.
- Functions 211/212/213 share 0x478AA0 with 210: local part position, world scale
  lengths, world minus-Y direction. Verified the second switch's mapping in executable
  table 0x478DD8. Like 210, channel 255 returns zero without selecting a part.
- Conditions 38..40 (0x48A6F0 -> 0x4831D0/0x483210/0x483260) ask whether a list
  contains the named actor / an actor of a model / an actor of a family.
- Procedure 340 (0x46E2A0 -> 0x417AA0) stores the low byte in collision +0x40.

- Condition 33 (0x48A430 -> 0x437A20): report +4 bit 0x10000 must be set;
  the super object at report +0xA4 (or its first actor ancestor via +0x1C) must name
  the actor argument. A zero report/object is false.
- Function 238 (0x47A660) computes a launch velocity from start to finish with an apex
  height above the higher endpoint, using the target's gravity. Negative height or
  zero gravity returns zero. Own analysis includes constants 0x49CCEC=-0.5 and
  0x49CCF0=4; the apparent integer conversion in the decompile is a float sign flip.
- Functions 178/179 (0x4750F0) return the displacement from a point to its closest
  point on a line (origin, direction) / segment (two endpoints). A line direction with
  all components below 0.0001 returns zero; a degenerate segment returns start-point.
- Functions 140..143 (0x476F20, 0x47F7A0..0x47F8C0, 0x487A80..0x487B90)
  query/change a directed link's capability (+0x10) or cost (+0x18). Capability modes
  0/2 OR, 1/3 replace, 4 remove, 5 zero. A missing link returns -1 (capability query: 0).
  Function 144 (0x477CC0) reads the matching graph node's capability +0x10.
  Procedure 400 (0x470D60 -> 0x47F920) restores every node's capability from +0x14
  and each link's capability/cost from +0x14/+0x1C.

- Procedure 80 (0x467770 -> 0x412040 -> 0x419990 -> 0x435DF0) reinstalls the
  current state's mechanics immediately, even when the mechanics pointer did not change.
- Condition 56 (0x48AE60) compares standard-game +0x18 with the frame number at
  0x5003E4 (incremented by 0x409DD0). 0x40A5C0 stamps that field before updating
  animation/brain/mechanics. Ours stamps at the beginning of the actor's tick too.

- More scalar speed forms share 0x46CE00: 183..187 impose world X/Y/Z/XY/XYZ;
  189..193 propose local X/Y/Z/XY/XYZ; 195..199 propose world X/Y/Z/XY/XYZ;
  202..206 write the third local speed; 208..212 set the third world-speed flags.
  Local forms clear the group's world bit; world forms retain it. All scalar values
  are stored as raw real bits. The 208..212 cases actually write +0x24..0x2C, despite
  setting the third group's flags (confirmed in the decompile; retained as observed).
  Procedures 281..284 write X/Y/Z/XYZ at +0x19C, setting 0x4000000..0x10000000;
  285..288 clear the corresponding bits without touching the stored vector.

- Functions 70..80 (0x47D190) work with four packed percentage bytes (type 0x27).
  75/76 construct four/three channels with numeric conversion and clamping to 0..100;
  77..80 extract a channel, capped at 100. 70 adds channels with saturation; the native
  helper 0x47D5D0 crosses the second input's middle bytes (also verified in assembly).
  71..74 use that same helper after byte truncation and shifting their scalar argument.
- Function 311 (0x488DC0 -> 0x42DE30) reads the vector at +0x10 of the secondary
  dynamics block, the pointer at [actor+8]+4; it is not the primary dynamics base.
  Procedure 364 (0x46FF00 -> 0x42DB20/0x42DE40) stores its two arguments shifted
  left 16 into actor +0x2C's block, words +0/+4. Procedure 262 (0x46C1D0) controls
  primary dynamics +8 bit 0x2000, like the neighboring flag setters.

- Function 121 (0x4770F0 -> 0x47F960) makes a shortest-cost directed path from
  start to finish. Only links sharing a bit with the target actor's capabilities
  (standard-game +0x1C, 0x494B60) participate. The executable uses an ordered open
  list: the first minimum wins, a strictly cheaper route updates a queued node,
  and a closed node is not revisited. The result is an owned path with cursor 0,
  no wrapping, copied node/link capabilities, and the number of waypoints returned.
  No route clears the existing path; absent cursor/graph returns 0 without changing it.

- Procedures 97/98/99/216 (0x468970 -> 0x419530) change the running actor's channel
  entry byte +0x10 (20-byte table at 3D data +0x90, count +0x94): enable one,
  enable the first N, disable one, disable all. Index 0xFFFF means all channels.
  These store the native channel state; downstream per-part update gating is not added here.
- Function 148 (0x477F90 -> 0x40FE50) returns the current state's animation frame count
  (u16 at the animation reference's start).
- Function 118 (0x477A80) returns the waypoint at the cheapest outgoing link sharing a
  capability bit with its mask; ties keep the earlier link. No match returns no waypoint.
- Procedure 395 (0x4713E0) changes a graph node's capability. Modes 2/3 first remove the
  mask / clear all graph nodes; 0/2 OR and 1/3 replace the selected node. Native mode 4
  falls through the clear case and clears the selected node entirely, as does mode 5.

- Functions 270..272 (0x47AE10), newly exposed by the census after implementing
  procedure 439, read standard-game +0x33/+0x2A/+0x2B as unsigned bytes. These pair
  with procedures 438/439, use the redirected actor, and return zero without standard data.

- The final path-enabled census exposed condition 107 and function 239. Conditions
  107/108 (0x48B7A0) test all/any bits between two values; 110/111 apply all/any to
  the target's standard-game capabilities +0x1C; 112 tests a low-byte bit number
  (0..31). Function 239 (0x47A150) reads the primary dynamics motion vector +0x54;
  without dynamics its native result is real zero.
