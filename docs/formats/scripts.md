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
| 30 | not identified yet | looks like a runtime address |
| 31, 32, 36, 38 | pointers not identified yet | 32 points into the dynamic world's blocks |
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

Conditions, functions, procedures and meta-actions are numbered for now; each gets a name when its
handler is read from the exe and implemented (milestone 3b). `ortool brain <game> <level> <node>`
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
behaviour, 13 state, 14 an id (-1 = none), 15 to 17 references not identified yet, 18 to 23 arrays
(u32 element type, u8 count, elements from +8; vectors take 12 bytes), 24 super object.

From an actor to its super object: actor +0x04 (standard game) +0x0C (0x4136D0).

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
