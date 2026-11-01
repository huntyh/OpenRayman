# Scripts: the actors' brains (Rayman 2 PC)

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

Each frame, for each actor (0x4120D0):

1. Its brain may be slowed: 3D data +0xFE is a period set by the scripts (procedure 341), +0xFF counts
   frames, and the brain runs only when the count wraps to 0. A 1999 time saver; OpenRayman runs
   every brain every frame unless asked to honour it.
2. 3D data +0x100 (u16) counts the frames the actor has thought; its low byte is what the
   every-N-frames ifs test.
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
