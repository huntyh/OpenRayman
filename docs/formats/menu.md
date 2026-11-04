# The main menu

The game's front end. Unlike most engines of its time, Rayman 2 does not draw its menus with a
separate UI system: the menu is an ordinary OpenSpace level (`World/Levels/Menu`) with scenery,
actors and AI scripts, and the engine adds a screen-space text layer on top. This note records what
that level is made of, what it loads and runs, and how the text on it is drawn.

What it does *not* yet answer is the exact screen layout: where each button sits and what it says.
The text itself is drawn from a table of 50 screen slots that the scripts fill a character at a
time; how it is arranged is described under [the screen-text layer](#the-screen-text-layer).

## The Menu level

`Menu` is the first of the 70 levels in `Game.dsb`, and the one the engine boots into.

- `Menu.sna` 638,463 bytes — scenery, actors, AI brains (the usual SNA block set)
- `Menu.gpt` 286,888 bytes — level header (Fix actors, world roots) and the animation bank
- `Menu.ptx` 7,180 bytes — texture slots and names
- `Menu.dsb` 369 bytes — sound banks and one vignette texture
- `Menu.snd` 1,160 bytes — the level's `(event id, slot address)` pairs

A GFX card from the original 3-CD release is not needed: the exe contains a three-entry table of
per-disc file lists (`MENU0.DAT`, `GRAPHICS0.DAT`, `LEVELS0.DAT`, …, then the `…1` and `…2` sets at
0x4a2090..0x4a20ec). Only the `0` set is ever used on the PC build, because the code that reads the
table loads `LEVELS0.DAT` unconditionally (0x44DE60, 0x44EB40). The `MENU*.DAT` entries are looked
up by nothing.

### What the level contains

Read with `ortool scene Menu`, `ortool level Menu` and `ortool brain Menu`:

- 61 super objects in one tree: 38 of type 2 (actors), 15 of type 0x20, 4 of type 4 and 4 of
  type 1 (sectors and containers).
- 58 level textures and 257 Fix textures. The level's own textures are the woods it stands in
  (`textures_foret`, `textures_petibois`, the stream and fern textures) — the same art as
  The Woods of Light, because the main menu shows Rayman standing in that scenery.
- Characters: `frans_corps`/`frans_corpstete` (Fransesco, the whale-like thing behind Rayman at the
  start of the game), `baleine_*` (a whale), Lums and their wings.
- 30 actors with a brain, 16 AI models, 163 behaviours, 325 scripts, 47,866 script nodes.

The main menu brain is node **39** (model at `0d/00 +0x1C`, 46 behaviours, 16 reflexes, 115
variables) with node **2** (11 behaviours, 97 variables) alongside it.

### What its scripts do

`ortool brain Menu 39` shows ordinary gameplay script, not a special UI dialect: it reads the input
actions from Fix (`condition_44(input @06/00 +0xC24)`, `+0xC0C`, `+0xBF4`), compares the cursor
actor's position against the entries it can point at (`actor 39.field_0 - field_0`, a 2D distance in
the XZ plane), and switches state as the cursor moves. The states of node 39 are plain animations of
Rayman and the cursor: 312 states, mostly 4–48 frames, several of them looping.

So the menu's logic is the game's own AI bytecode and needs no new interpreter features. What it
*draws* is the missing half.

## Textures the menu uses

The interesting part is where the words come from. They are not in the level.

- **`textures_alphabet`** in `Textures.cnt` holds **238** glyph textures, one per character:
  `s_a_aaa`, `s_b_aaa`, … `vs_a_aaa` (accented), `vs_cedille_aaa`, digits and punctuation
  (`s_2point_aaa`, `s_interro_aaa`, `s_guillemet_aaa`, `s_pcent_aaa`). The names separate an ASCII
  set (`s_`) from an accented one (`vs_`); the sizes differ per glyph, so each glyph is cropped from
  its own texture rather than read out of a shared atlas. They are resolved through Fix's texture
  table, so they are available in every level, not just this one.
- **`textures_background/logo_*`** are the title art: 18 textures in six groups of three
  (`logo_00/01/02` … `logo_50/51/52`), each group a short animation.
- **`menu_maa_txy.gf`** (`effets_speciaux`), plus from `textures_objets/divers`
  **`bouton.gf`**, **`support_bouton_anu.gf`** and **`haut_bouton2_nz.gf`** (button, support and
  highlight), and the two mouse cursors **`curseur_nz.gf`** and **`curseur_nz_drag.gf`**.
- `Vignette.cnt` supplies `Rayman.gf` and `UbiSoft.gf` (the logos shown before the menu) and one
  loading picture per level (`VigLoadLearn01.gf`, …); `Menu.dsb` names `Vignette Rayman.bmp`.

### The text is typed out, with sound

`Menu.snd` resolves bank 0, event 0 to `alphabe1.wav`, and `Menu.dsb` loads sound bank 0. Bank 0
also carries `Menu1.wav`, `Menu3b.wav` and `Menu4.wav`. Together with the 238 per-character
textures this is the familiar Rayman 2 flourish: the menu text appears letter by letter, each
letter with the alphabet sound. The scripts drive the letters; the engine only draws them.

Proven: the glyph set, the sound, that the level asks for both. The letter-by-letter order is not a
special effect at all — it falls out of the design below.

## The screen-text layer

Screen text never touches the level's geometry. The engine keeps a table of **50 text slots**, and
the scripts fill them one character at a time through procedures 505..507. A slot holds the text,
its screen position, its colour and a few flags; a per-frame pass draws every visible slot.

### The slot table: 0x4E1080, 50 slots of 0x1C bytes

Cleared at load by 0x465A20 (which walks to 0x4E1608), so `(0x4E1608 - 0x4E1080) / 0x1C` = 50.
Fields, from the setter code (0x465A50, 0x465AA0, 0x465BE0):

| Offset | What it is |
| --- | --- |
| +0x00 | `char*` to the text, or 0 for an unused slot |
| +0x04 | screen X (`param_2[1]`) |
| +0x08 | screen Y (`param_2[2]`) |
| +0x0C | size / scale, a float (`param_2[3]`) |
| +0x10 | alpha, 0..255; the drawing pass halves it while a word is flashing |
| +0x12 | pulse flag: non-zero expands and contracts each glyph on a sine |
| +0x13 | flag byte: 0x01 alpha ramping in, 0x02 ramping out, 0x04 flash the current word, 0x08 shake, 0x10 colour mode set; the setters clear single bits with 0xEF / 0xF7 / 0xFB |
| +0x14 | the word index being flashed, compared with the running word counter |
| +0x18 | colour mode: 0 default, 1..2 red, 3 yellow |

The bytes at +0x11 and past +0x19 are not read by anything found yet.

### Procedures 505..507 (handler 0x4731B0)

The same handler serves all three, switching on its own node's parameter (`[node-8]`), which holds
the procedure index, so the branches carry the constants 0x1F9, 0x1FA and 0x1FB:

- **505** (`0x1F9`, `FUN_00465A50`) = `(slot, position, string, value)`: sets the slot up. The
  second argument is a vector used as its screen position, the third is the text (0x4829D0 turns
  the value into a pointer; a text value's pointer is its first word), and the fourth is stored
  beside it — the slot's +0x10 byte, the alpha field. It also zeroes the wobble and flag bytes.
- **506** (`0x1FA`, `FUN_00465AA0`) = `(slot, what, value)`: the field setter. It switches on an
  integer `what`: 0 sets the alpha, 1 the flash flag, 2/3 the colour mode, 4 the size, 5 the flash
  word index (and flag bit 0x04), 6 the shake flag, 7 a three-way mode that sets flag bit 0x10.
- **507** (`0x1FB`, `FUN_00465BE0`) = `(slot, index, character)`: writes **one character** into the
  slot's buffer at that index.

That last one is the whole typing effect. A Menu-level actor runs a loop like
`procedure_507(var17, var59 + 3, var58)` followed by a sound event and a wait, so the next letter
appears a tick later: the letters and the `alphabe1.wav` come from the same loop. The three Menu
actors that use these are nodes 3, 5 and 6 (3 and 5 set slots up with 505, 6 also walks the
characters). No Learn_10 actor calls any of them.

### Drawing: 0x465C10, once per frame

Called from the frame update at 0x460670 (after the world, 0x4658B0 and the screen rectangles).
For each slot with a non-zero text pointer it:

- runs the alpha ramps: flag 0x01 lifts the alpha towards 255 and clears itself at the top, flag
  0x02 lowers it towards 0 the same way;
- if flag 0x08 is set, it asks 0x465E30 for the string's bounding box and lays the text out with
  the shake phase folded in;
- then draws the string with 0x4660B0.

### A string becomes quads: 0x4660B0

The text is a plain byte string plus **inline control codes**, all introduced by `/`:

| Sequence | Effect |
| --- | --- |
| `/L:<x>:` | left-align and centre the block on x (the centring maths is `(1000 - width) / 2`) |
| `/C:<x>:` | centre on x, and it toggles a flag each time it is seen |
| `/O:<colour>:` | colour code: 0 and 600 are the default blue (0x6E, 0xBE, 0xFF), 200 is red (0xFA, 0x46, 0x46), 400 is yellow (0xFF, 0xFF, 0x78) |
| `/c:` | toggles a colour/state flag |

Between codes, each character advances by a slot-size-dependent fixed width. Spaces advance by 60% of that width; punctuation is vertically offset as described below. A space moves the pen back and starts a new word (the word counter is what `/L` and the
flash effect use).

The glyph comes from `0x424E90(character)`: a font object's pointer at 0x4A727C holds an array of
0x14-byte entries, one per character, each pointing at a texture. The object itself is part of the
level's loaded data — 0x403730 sets `DAT_004a727c` through 0x44CFC0, which is the engine's
relocation reader (it adds a block's load delta to a stored address), not a constructor. So the
font object is saved in Fix with the rest of the engine's data, and its glyphs are the
`textures_alphabet` set above — 238 of them, which is why every letter, digit and accented
character works in every level. The character is mapped by 0x465D40: bytes 0x61..0x7A (`a`..`z`)
are shifted down by 0x20 onto the upper-case slots, so the font is case-insensitive.

Whether the alphabet lives only in this level's Fix data or is shared by all 70 is the one link not
yet read; the glyph entry shape (0x14 bytes, texture pointer per character) matches the 238.

### The menu's own strings

The strings sit in the level's data, not in the exe. In `Menu.sna` block `0d/01` (base
0x7920020), as `u8 length + bytes + padding + u32 kind` records:

| Virtual address | Record |
| --- | --- |
| 0x7948CA0 | `/c:/o200:cheat active` |
| 0x7949380 | `http://www.rayman2.com/` |
| 0x79291F8 | `/o200:` — a colour code on its own |

So the menu's text is written in these codes inside the level the same way a level's own text is.
The `cheat active` line confirms it: the menu shows status lines, not only buttons.

**But most of the menu's labels are not strings at all.** The calls that set them up pass an *id*
(type 0x24) rather than a text value, and the exe resolves both through the same helper (0x4829D0):
a text value's pointer, or 0x494C70 for an id. That function indexes the game's string table, and
the table it picks is chosen by the low byte of 0x500FA8 — the **language**. At start-up 0x403730
reads `RAYMAN2 / Language` from the ini (the GOG install ships `Language=English` in `ubi.ini`),
matches it against a table of 0x28-byte entries, and stores the matching index there. The strings
live in Fix with the rest of the engine's data, one array per language, and the id is the index
into it.

That is why this level's own strings read like leftovers: the direct strings are the CD-insert
warnings (`"/c:cd missing!"`, `"/c:insert the cd"`, `"/c:and"`, in node 3's `if var63` branch), which
never show on a hard-disk install. The real labels (`New game`, `Options`, `Quit`) are ids.

Reading the id table is the next step, and it needs the language arrays in Fix. Until then the
engine's `screen_text` has the slot state but no words for those slots.

Named actors give the rest of the shape. The scene's own name table (first 0x2000 bytes of the SNA)
has `Levelmenu`, `YAM_GenMenu`, `YAM_CurseurMenu`, `ARG_CurseurMenu`, `YAM_PtizetreMenu`,
`YAM_GenCibleCamera`, `YAM_PosCam_Smooth`, `GenCamera`, besides the characters the level shows
(`francesco`, `baleine`, `bebe_globox`, `Krabos`). The cursor actor is `CurseurMenu`; `Levelmenu` is
the menu's own controller.

### The FON module

The font itself is a **licensed third-party module**, statically linked: its identity strings sit in
`.data` at 0x49F6C0, `Module FON`, `FON V1.0.0`, `2D-FONT module`, `Oct 15 1999`, right after the
per-module descriptor of the engine's own module table.

Its API is a 6-pointer record at 0x49F5B8 (start 0x414910, then 0x414680, 0x414690, 0x414720,
0x4147B0, 0x4148A0), reached through the engine's attribute table at 0x49F4C8 — 14 records of
0x18 bytes, walked by 0x40BCC0 with the callback at +0x10. Each record is an engine attribute
(rather than a super-object type): the init callbacks create the `+0x28`, `+0x2C`, `+0x30` and
`+0x34` objects the module owns in the engine's UI object, and the FON entry points sit at records
10, 11 and 12.

Read from the entry points: 0x414690/0x414720 create and destroy a font object at `+0x28`;
0x4156E0/0x415770 the same for `+0x2C`; 0x415440/0x415550 and 0x414F40/0x4150C0 for `+0x30` and
`+0x34`; 0x4147B0 resets an object (and asks 0x4123D0 whether it can); 0x415430 and 0x414E80 are
size queries (0x24 and 0x78); 0x4155E0 and 0x4150C0 validate. They use the engine's allocator
(0x414710) and error reporter (0x402810, message 0x30).

Raised by the Menu level's own actors, not by the engine: node 3 sets six slots up with
`procedure_505(0, vector(0, 700, 16), ...)` and its siblings, node 5 fills in more, and node 6 runs
the character loop.

Still not read: which of the four objects draws what, and the exact screen positions of the menu's
buttons. The level's data refers to them by path in the form `menu\menu.wp^Isolate^WayPoint:`
(found twice in `Menu.sna`, around offset 0x9B6E8), and no `.wp` file exists anywhere in the
install — so those are references into the level's own waypoint list rather than to a separate
file.

## The boot flow (2026-11-02)

Node 6 types the menu. At startup it reads procedure 344's two bytes (0x500FA4, both 0 at
boot; 343 sets them and 345 the third, so a level can ask the menu to open somewhere else) and,
when both are 0, tests story bit 1006 in the Fix actor's bit array (`actor 3.var42` in this
level):

- bit 1006 clear: the **language prompt**, ids 0x5030..0x5034 (English, Français, Español,
  Italiano, Deutsch), with the current language (function 252, 0x47AAD0: the low byte of
  0x500FA8) highlighted. The GOG game shows it at every start too (user, 2026-11-02): the
  installed saves don't carry bit 1006.
- picking a language sets bit 1006 (procedure 351) and types the **main menu**, ids 0x166,
  0x167, 0x16D, 0x168, 0x16A, 0x16B: New game, Load, Delete, Options, Site Rayman2, Exit.
  Macro 0, the new game's reset of the general variables, sets 1006 as well.
- Load and Delete are greyed (`procedure_506(slot, 7, …)`) by the save count, function 265
  (0x47AD30 -> 0x408290 -> 0x408200): the folders in the save folder whose names start with
  `Slot` (so not `HiddenSlot`). Its argument is evaluated and ignored.
- With a request pending (344 gives non-zero bytes), the menu opens on a variant whose first
  entry is id 0x16C instead.

Our engine implements 252, 265 and 343..345, so the prompt, the main menu and the cursor's
skipping of greyed entries all run from the level's own scripts.

### How an entry looks (0x4660B0)

The slot path draws a textured screen-space rectangle per glyph. Own analysis of the GOG
executable, 2026-11-04: 0x429670 supplies white at both upper corners and the slot colour at
both lower corners, with the slot alpha throughout, to `GLI_DRV_vSendSpriteToClipWithCol`.
The texture and interpolated colour produce the gradient. Colour mode 0/1 (or flag 0x10 clear)
is blue (110, 190, 255), 2 red (250, 70, 70), and 3 yellow (255, 255, 120); `/O` overrides it.

Disassembly of 0x465DD0 shows a single width for the slot, not per-character widths: with
`d = size - 15`, the advance and quad width are trunc(32 + 2d), height trunc(38 + 2.5d),
and line height trunc(46 + 4d). 0x465D60 measures spaces at trunc(0.6 * advance).
At 0x4660B0 a period is shifted down by height / 4, a comma by height / 2, and an apostrophe
up by height / 4 (integer shifts); their horizontal advance remains unchanged. The coordinates
are scaled by 0.1 into the driver's percent-of-viewport space, so they do not depend on camera
depth. The font textures themselves have differing contents, but there is no per-glyph advance
in this screen-slot path.

A nonzero +0x12 expands the top and bottom edges by `sin(5t) * 7` slot units, rather than
lifting the whole string. Flag 0x04 applies the same effect to one character at half alpha;
our renderer does not yet implement that flag. Flag 0x08 draws a box behind the text
(0x465E30 measures it, 0x429820 draws it). Node 6 uses alpha 90 for greyed entries.

### Conversation continue arrow

Its placement remains unresolved. The screen-image pass at 0x460600 forwards the corners
stored by 0x4605A0 to 0x429670; this alone does not identify the arrow or explain its inset.
No arrow-position adjustment has been made. Own analysis of the GOG executable, 2026-11-04.

### New Game

New Game (entry 0) opens the name entry (`AAA`, behaviour 7: arrows pick the three letters).
Enter on it checks for a slot of that name (function 95, 0x47D730: the slot number holding the
same three letters, or 0), names the slot (procedure 431, 0x4702A0: name plus percentage, as
`Current.cfg` lists them) and writes the save (function 90/93, 0x47D830). When the write works the
menu shows message 0x183 and the next Enter calls `procedure_86("jail_10")`; when it fails it
calls `procedure_87("jail_10")` at once. Procedures 86/87 (0x467970 -> 0x4054D0) change the level
by name; 86 passes a flag the level loader gets (not read yet).

Ours doesn't write saves yet (functions 90/93 and 95 are missing and give 0, so the menu goes
straight to Jail_10, the intro), so nothing in the install's save folder changes. The engine
loads the requested level after the tick and carries the Fix story-bit array over, as Fix stays
loaded in the game; the Menu level itself starts with Fix's own bits (no save loaded), not with
our revisit bits.

### Exit game (2026-11-04)

Node 6 behaviour 11 handles the main menu. Entry 5 sets var55 to 6 and switches to behaviour 9
and reflex 3. Reflex 3 composes slot 9 from text IDs 0x18D and 0x193 (initial choice) or
0x192 (other choice); directional input toggles var41. These strings contain `/O` colour codes,
so the two choices are coloured within the same slot. The screen renderer must apply those codes
as gradient endpoint colours to subsequent glyphs, independently of the slot's starting tint. The default is No;
selecting Yes and confirming calls procedure 101. No returns to the main menu.

Procedure 101 (handler 0x468AB0) requests engine state 2 through 0x404BF0 and sets script state 3
through 0x4808E0. OpenRayman raises a quit request and closes through SDL after the current tick.
An `ortool run` from language selection through Yes reached no missing node 6 handlers on the
exit path itself; procedure 242 (0x469960, language selection) remains missing earlier in the boot
path. Other actors also reach missing handlers. These findings come from our own analysis of the GOG
executable's handlers and 0x4660B0, and the Menu scripts and an input replay, 2026-11-04.

## Where this came from

The dialog glyph path is distinct from the menu's slot renderer: own analysis of 0x409720 and
0x409500 shows that each byte selects an entry in the generated actor's font object and creates a
glyph child using the entry's object pointer. Lowercase maps to the uppercase range; bytes above
0x9F use a different offset. The font-object root, glyph geometry, and byte mapping have not yet
now connected to the viewer. The template's initial object list contains the glyphs; `/O` changes
the list offset (0x408DD0), reset to zero by 0x4085E0. The visual set supplies signed 16-bit RGBA
per vertex: 0x431400 divides by 256, and the material conversion at 0x42D520 doubles and clamps
before rasterization. The examined glyphs in ranges 0, 200, 400 and 600 have black ambient and unit diffuse;
range 800 has white ambient. This is own
analysis of the GOG executable and Fix data, 2026-11-02; matching the finished colours and positioning
by eye against the game remains open.

- Own analysis of the retail GOG install (`Rayman2.exe`, timestamp 1999-10-15 23:19:39) and of the
  game files, on 2026-10-30.
- Which level it is, its super objects, textures, Fix textures and brains: `ortool scene`,
  `ortool level`, `ortool brain` against the real install.
- The glyph set and the logos: `Textures.cnt` and `Vignette.cnt` names, read through `ortool cnt`
  (the names are XORed with the archive's key byte, `mask`).
- The sound side: `ortool dsb Menu.dsb`, `ortool sound`, `ortool bank 0`.
- The disc table, the FON module's identity strings and API record, the attribute table at 0x49F4C8
  and the four font-object fields: own analysis of `Rayman2.exe` (the project's Ghidra
  decompilation, plus raw PE reads to find the pointers and the call sites).
- The 50-slot table, procedures 503..507, the drawing pass and the `/L` `/C` `/O` `/c` control
  codes: own analysis of `Rayman2.exe`, reading the decompiled setter and drawing functions
  (0x465A20, 0x465A50, 0x465AA0, 0x465BE0, 0x465C10, 0x4660B0, 0x465E30, 0x424E90).
- The menu's own strings and the actor names: the decoded `Menu.sna` (block `0d/01`, base
  0x7920020) read with the project's own mask decoder.
- Nothing here was taken from Raymap, Rayman2Lib or any other project; no game data is included.
