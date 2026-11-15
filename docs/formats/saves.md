# Saves

OpenRayman saves can be restored in game mode with `--save 1`, `--save 2` or `--save 3`.
Without that option the interim revisit snapshot remains the default. Scripted writing
(functions 90/93/95, card #24) now drives the original success and failure screens.

## Files and loading

Each slot has `General.sav` and level files such as `Learn_10_0.sav`. The final number is the
level variant returned in the upper half of 0x404DB0 (0x410450), not the user slot number; the
installed files all use variant 0. OpenRayman currently loads variant 0 only. `--save` chooses
`Slot1..3` under OpenRayman?s save root. A missing general
file, invalid slot, checksum failure or registry-size mismatch is an error. A missing level
file leaves that level's initial state. `HiddenSlot` is not a selectable user slot.

0x403730 reads level and general files separately (0x410450, 0x410410), applies the registered
values (0x449E80), then refreshes activity (0x406340). OpenRayman restores the bytes before the
first script tick and adjusts type-5 activity. With a slot selected, neither the revisit snapshot
nor first-visit hand-over runs. Explicit `--story-bits` overrides run after restoration. The slot
seeds the initial load. Script-requested level changes carry registered general progress and
retain registered state for visited levels in memory; they do not reread the initial slot over
new progress. Function 90 writes the general registry and visited levels to the active slot.

On Windows the root is `%APPDATA%\OpenRayman\OpenRayman\SaveGame`. A `portable.txt` beside the executable selects its `SaveGame` folder; `OPENRAYMAN_SAVE_DIR` overrides either location. The game?s own saves are never used by default. `ortool mksave <level> <slot> [--story-bits a,b,...]` writes a synthetic revisit state and refuses destinations inside the game folder.

## Registry order

0x4062F0 initializes the dynamic subtree, children before parent (0x406620), then the always list
in forward order (0x40BC30). Registration order is **not Fix header order**. 0x466BD0 walks variable
declarations in increasing index order. Flag 0x80 selects current general values, 0x200 current
level values; 0x40/0x100 select registration snapshots. 0x44A0F0 adds load-group flags 4/8.
0x44A240 **prepends ordinary entries but appends packed bits** (storage type 0x8000).

Arrays register elements in increasing order, so they appear reversed in the file. Their eight-byte
runtime header is not saved. Non-vector elements occupy four-byte runtime slots, even if their
saved scalar width is smaller. `read_save_registry` reconstructs the list from declarations, the
dynamic tree and always actors; every entry identifies its actor, variable/element or standard
field, file offset and size. Unsupported flagged types, including pointers, fail explicitly;
none occur in the installed general/level registries checked here.

## General.sav: all 196 bytes matched

The global controller is **scene node 6** in Learn_10, Fix header index 2; Rayman is scene node 84,
Fix header index 0. Earlier notes called node 6 "Fix actor 6". All entries below carry 0x80.
Types 0 and 2 are signed/unsigned bytes; type 5 is signed 32-bit integer.

| Offset | Bytes | Destination |
| --- | --- | --- |
| 0 | 1 | Rayman standard game +0x23 (meaning not yet identified) |
| 1 | 1 | Rayman standard game +0x22 (maximum hit points) |
| 2 | 1 | Rayman standard game +0x21 (hit points) |
| 3 | 4 | Global controller var67, type 5 |
| 7 | 1 | Global controller var50, type 0 |
| 8 | 1 | Global controller var49, type 0 |
| 9 | 1 | Global controller var46, type 2 |
| 10 | 1 | Global controller var45, type 2 |
| 11 | 180 | Global controller var42: 45 words, word 44 first, word 0 last |
| 191 | 1 | Global controller var20, type 0 |
| 192 | 4 | Global controller var6, type 5 |

There are 55 registrations: three standard bytes, seven scalar variables, 45 story words.
Story word `i` starts at `11 + 4*(44-i)`; bit numbers are one-based (`i*32 + bit-in-word + 1`).
The requested seven scalar variables account for only eight bytes before and five after the
story words. The remaining three prefix bytes are **not script variables**: 0x4135E0 registers
standard game +0x21, +0x22, +0x23 for actors with custom bit 31 (mask 0x80000000 at +0x24).
Rayman has this flag and is initialized after the controller, so his bytes precede its variables.
The scalar variables' gameplay meanings beyond their script indices remain unnamed.

Slot1 and HiddenSlot's general saves both decode to this layout with zero checksum. Restoration
tests check all destinations. Slot1's story array agrees with the previously identified Learn_10
bits. **Slot2 and Slot3 are empty as of 2026-11-06**, including hidden files, so the requested
comparison of three installed user-slot saves cannot be completed. No raw saves or value dumps
are stored in the repository.

## Level saves: exact packed entries in the installed files

0x4135C0 registers standard game +0x29 bit 0 of type-5 actors under 0x200. 0x44ACD0 packs bits
LSB first in initialization order. 0x44AD80 flushes the final partial byte. No placed actor's
script variable has 0x100/0x200 in these levels. Always actors can exist outside the scene tree.

| Level | Decoded bytes | Entries, in file/bit order |
| --- | --- | --- |
| Learn_10 | 1 | byte 0 bit 0: node 71; byte 0 bit 1: always actor 04/01 +0x29B8 |
| Learn_30 | 1 | byte 0 bit 0: always actor 04/01 +0x1D00 |
| Bast_20 | 1 | byte 0 bit 0: always actor 04/01 +0x43B4 |
| Ski_60 | 1 | byte 0 bit 0: always actor 04/01 +0x2830 |
| Chase_10 | 1 | byte 0 bit 0: always actor 04/01 +0x7130 |
| Ly_10 | 1 | byte 0 bit 0: always actor 04/01 +0x30A0 |
| Water_10 | 1 | byte 0 bit 0: always actor 04/01 +0x3EF4 |
| Menu, Jail_10, Jail_20, Mapmonde, Bonux, Learn_31, Bast_22, Learn_60, Ski_10, Batam_10, Chase_22, nego_10, Water_20, poloc_10, Raycap | 0 | No entries |

The block/offset identifies the actor structure. There are 25 installed `.sav` files, including
HiddenSlot's general and empty Menu save. Current tests use generated data and do not open installed save files.

## File encoding (0x48D890)

Every `.sav` and `Options/Current.cfg` uses the same wrapping:

- Byte 0 XOR 0x53 is a checksum seed; little-endian bytes 1..4 XOR 0x54555657 are expanded length.
- Each further byte is XORed with 0xB9 and the bit-reversed byte counter starting at 0x57 and
  wrapping at 256 (0x48D5A0, 0x48D420). Subtract each deobfuscated **compressed-stream** byte from
  the seed modulo 256: the result must be zero. The checksum is not over expanded bytes.
- The dictionary has 256 eight-byte blocks, block `i` initially eight copies of `i` (0x48D660).
  Each token selects a block and provides an LSB-first replacement mask, followed by one byte
  per set bit. Changes persist when that block is reused.
- Each complete token supplies the next eight-byte output block. EOF after a complete token
  emits the last block whole; a lone final byte instead specifies how many bytes (1..7) of the
  pending block to emit. A zero-length file needs no stream.
- The reader checks checksum, token bounds, partial-block count and exact expanded length, and
  bounds allocation by token stream size. Malformed files throw `format_error`.

The encoder emits full-mask dictionary blocks, padding the last block and appending its partial length when needed. It computes the checksum over the unwrapped compressed stream.

The current `Current.cfg` expands to 87 bytes with zero checksum; the earlier 72-byte observation
was a different options/slot-name state. Options are not applied.
`ortool save <slot>` (using `OPENRAYMAN_GAME_DIR`) prints scalar entries, sorted set story bits
and each installed level file's actor bits. `ortool save <game folder> <slot>` also works.

## Registry writing notes

0x44A650 iterates the same registry with selected flags, writes a temporary plain stream, flushes
pending packed bits, then calls the compression/wrapping path through 0x48D2C0. 0x44A730 chooses
cached snapshots (0x40/0x100) or current bytes (0x80/0x200). Pointer entries, if present, store a
two-byte module identifier and four-byte offset; null pointers use module 0xFFFF. 0x44AAF0 reads
cached entries, 0x44A500 applies them, 0x44AD20 consumes packed bits LSB first. A writer must
preserve registry order, reversed array elements, Rayman's standard bytes and always actors,
and checksum the compressed stream. The temporary revisit snapshot is not a save format.

## The HUD's counters

Node 44 shows Fix actor 6's variables: the cages as vars 4 / 5 (freed / in the level), the Lums as
var 44 − var 43 out of var 44. A new game starts Learn_10 with 2/0 cages and 5 − 5 Lums; the
revisit start, through the story bits the level's actors check at startup, gives 2/2 and 5/5.

## Which actor checks which bit (Learn_10)

The five placed Lums are nodes 12, 11, 13, 70 and 36, for bits 1396..1400; each removes itself at
startup when its bit is set (node 70, the caged Lum, is frozen by the tutorial controller node 69
before it can). The kids' director, node 3, starts its scene unless bit 1092 and its own var115
are both set. Node 62 removes itself for bit 1133. The last cage (node 53) checks its cage bit and
skips the Lums it has already given.

## The revisit snapshot (ours, interim)

Story bits alone don't give a finished level: some actors only leave through their own scenes.
When no `--save` is supplied, `apply_revisit_state` (game/include/openrayman/game/first_visit.h) takes
nodes 11, 12, 13, 36, 53, 56..61, 70 and 71 out of the world (bit 0 set; type 5 also loses bit
1), which matches the finished level the user sees in the game. It is a stand-in for the requested
end state, not a decoded save. Node 3, the kids' director, was on this list until 2026-11-14
(card 78): that hid the kids' scene on the default revisit start, although the game replays it
when only Learn_10 is cleared (recording run10) and a `--save` start of ours did too. The level
removes the director itself (node 4, story bits 1133 and 1095), so `--story-bits 1095` gives the
revisit without the scene. `ortool run` takes `ORTOOL_SAVE=1..3` to start from a slot of our save
folder, as the viewer's `--save` does.

## Where this came from

Own independent analysis on 2026-11-06 of the GOG `Rayman2.exe`: decoder 0x48D890 / 0x48D660 /
0x48D5A0 / 0x48D610, registry 0x466BD0 / 0x466B50 / 0x481260 / 0x44A240 / 0x44A0F0,
initialization 0x4062F0 / 0x406620 / 0x40BC30 / 0x4132A0 / 0x4135C0 / 0x4135E0,
IO 0x44A650 / 0x44A730 / 0x44AAF0 / 0x44A500 / 0x44ACD0 / 0x44AD20,
level loading 0x403730 / 0x410410 / 0x410450. No decompiled code is copied.
Cross-checked against Fix declarations, each saved level's dynamic tree and always list,
Slot1 and HiddenSlot, with synthetic malformed-stream and installed-file restoration tests.
The exact meaning of standard +0x23, visual parity with the real game and Slot2/3 values remain
unverified. Earlier HUD/story/snapshot observations came from independent executable and
Fix/Learn_10 analysis (2026-11-02), including `ortool run` with `ORTOOL_IDS=1` /
`ORTOOL_VARS=6:4,5,43,44`.

## Scripted writing (card 24, 2026-11-15)

Function **90** (`0x47D830`) evaluates one slot argument. The executable writes the level
and general registries to HiddenSlot (`0x4103A0`, `0x410360`, `0x449EC0`, `0x449EA0`),
then publishes the working files to the selected slot (`0x410870`). It returns a nonzero
byte on success. Function **94** returns the active slot without arguments. In OpenRayman,
a zero write argument selects the active slot, or Slot1 before one has been selected.

Function **93**, in the same handler, starts a fresh slot: `0x4110E0` clears the working
folder and the handler sets a deferred reset flag. The next level request (`0x4054D0`)
runs `0x47D9C0`/`0x47D9E0` and restores designer values with reset level zero
(`0x466D30`, `0x466950`). OpenRayman clears carried level progress and performs that reset
on transition. To make the initial save immediately loadable, it writes registered initial
designer values at name confirmation, with the 15/30 health/max that Menu behaviour 16 sets
on Continue. This snapshot is our equivalent of the fresh working folder. Array reset now
copies the full runtime storage; serialization still omits its header and reverses elements.

Function **95** (`0x47D730`) evaluates a name, an unused value and an excluded slot. It skips
leading formatting commands, keeps **three bytes** of the name and returns a colliding slot,
or zero. Procedure **431** (`0x4702A0`) sets the name and completion count (0..1000).
Functions **215..217** (`0x478E10`) supply the scripts' slot text. The original options file's
11-byte display record per slot (`0x407B80`) does not allow an 11-character player name.
Typed name entry (card 74, including function 218) is unchanged. Functions 91/92 (menu
Load/Delete) remain outside this writing change; `--save` uses the existing loader.

The writer stages a complete slot beside its destination, keeping unchanged level saves on
ordinary saves and discarding them for a new game. It encodes the original payload format,
closes every file, then replaces the destination directory, rolling back failed publication.
A failure returns zero to the scripts. Destinations and copied files must be outside the game
installation. Labels/completion use our own `SlotN/slot.txt`; original options are not applied
or modified. A process/power failure between the two directory renames can leave a
`.SlotN-*.previous` backup next to the slots.

Mapmonde actor **89**, model `0d/01 +0x1C178`, owns the Hall of Doors save question.
Behaviour 1 calls 431, 94, 216 and 90; success displays text **0x183**, and behaviour 2
waits for Enter before restoring the clock, screen-object stack and world. Menu node 6 uses
the same success text after name confirmation. These original branches now run.

## Optional autosave (card 40, ours)

Ours: the first new-game success text (0x183) is replaced by **Enable autosave?** (a replaced text takes its slot as an empty string, so the name screen's text doesn't stay behind). Yes then shows the saving Lum with a short explanation until Enter. Left/right selects Yes/No
(default No); Enter stores the answer and continues. The choice belongs to that save:
`autosave_SlotN.txt` in our save root (beside the slot folders, since a save replaces the slot
folder itself) contains `1` for on or `0` for off. A new game in a slot forgets that slot's choice
and asks again; a missing/invalid file means the original prompts. Change it later with
`--autosave on` or `--autosave off` (for the `--save` slot, or slot 1), or edit the file while
closed. Later an entry in our own Options (card 61) can change the loaded save's choice.

With autosave on, the Hall question (0x18F/0x190) uses the scripts' original Yes/write/success/cleanup
path. The Menu's save-before-exit question (node 6 var55 = 6, composed text 0x5043) is always
asked: answering it automatically also saved the Hall portal selected when Esc left the Hall
(Fix actor var67, set by Mapmonde node 5 behaviour 1), so the next load started at that portal
rather than at the last level's (user, 2026-11-15). Only that actor's Enter conditions are supplied automatically; the question and success
acknowledgement are hidden. Failed writes follow the original failure branch. With autosave
off none of these substitutions run. Successful automatic writes show **Saving...** with the
game's `lums_corpsvert_mxy.gf` and `lums_aile_adnzw.gf`, loaded from Textures.cnt at runtime.
The bottom-right indicator holds for two seconds, then fades over half a second; its timer
survives level changes. No game assets are included in sources.

### Where this came from

Own independent reading of the GOG executable's function table at `0x4A5460` and the handlers
and callees listed above (2026-11-15), plus Menu node 6 and Mapmonde node 89 bytecode read with
`ortool brain`. The deferred reset was traced through `0x4054D0`. Tests run Menu through name
confirmation, compare all 196 general bytes with the reset state, restore them through
Learn_10's independent registry, and exercise current scalar/array/packed level writing.
Hall tests run both manual confirmation and input-free autosave through its bytecode.
Captures and progress notes: `build/helpers/saves_report.md`. Installed-save observations
elsewhere in this document are historical: this work reads no installed saves. All generated
saves use OpenRayman's isolated save root. Autosave UI, metadata and publication are our own
design. No executable code or other project's implementation was copied.
