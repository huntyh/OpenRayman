# The main menu

See [text.md](text.md) for the 2026-11-07 comparison of dialog/narration and
menu/HUD paths, corrected screen-camera projection, font-root loading, inline
texture-bank selection, and the remaining colour/spacing live reads.

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
  0x02 lowers it towards 0 the same way. The step is the frame time (0x4B9B04, seconds) times 200
  (constants -200 at 0x49CBD0, 200 at 0x49CBD4), subtracted from the byte and truncated by ftol,
  so at 60 frames a second a fade in climbs 3 a frame and a fade out drops 4; slots at alpha 0 are
  skipped entirely, which is why setter mode 2 lifts a zero alpha to 1 (own analysis, capstone);
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

The `http://www.rayman2.com/` record is the **Site Rayman2** entry's target. Node 6 runs it from
the rule at `0d/01 +0x4A7C` — behaviour 2, guarded by input action 9 (`+0xB4C`: Enter, Space or pad
button 10) — in the branch `var17 == 4`, i.e. when the cursor sits on entry 4 of the six the menu
types (ids 0x166, 0x167, 0x16D, 0x168, 0x16A, 0x16B: New game, Load, Delete, Options, Site Rayman2,
Exit; "The boot flow" above). The statement is `procedure_369("http://www.rayman2.com/")`, and
procedure 369 is the level scripts' only way to ask the desktop to open something.

Procedure 369 (`0x470210`) checks its node's own parameter is `0x171`, evaluates its single
argument, resolves it through 0x4829D0 — the same helper 505 uses, so the argument may be a string
or a text id — and calls `ShellExecuteA(nullptr, nullptr, string, nullptr, nullptr, 0)`
(`SHELL32.dll`, import slot 0x49C220; the strings are ours to read, nothing here is copied). The
shipped executable carries this function **twice**: at 0x470210 in `.text`, where the call is
replaced by NOPs, and byte-identical at 0x587210 in the `.extra` section (`0x117000..0x1B3000`, the
section the entry point 0x5FCC0 also lives in), where the six pushes and the call are intact. So the
entry is dead twice over: the address is gone, and the copy the procedure table names no longer
calls the shell.

OpenRayman implements 369 as an engine hook (the `game/` library has no SDL): the handler resolves
the argument and hands it to `interpreter::on_open_url`, which `src/main.cc` points at
`SDL_OpenURL`. Any URL a script asks for opens `https://github.com/huntyh/OpenRayman` instead — a
deliberate difference, listed in `docs/IMPROVEMENTS.md`.

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

### Language selection and menu targets

Own analysis of the GOG executable at 0x469960: procedure 242 stores the low byte of its
argument as the current language and writes its name to the original game's ini file. OpenRayman
updates the runtime language and resolves subsequent text IDs from that language's table. It saves
the choice in its own `language.txt` in the save folder, reads it on each level load, and sets
story bit 1006 on the Menu's startup when a valid choice exists. This storage and startup handling
are OpenRayman's own implementation; the executable finding above is from our own analysis.

The Menu scripts for node 7 select from eight waypoint references according to node 6's behaviour,
then use function 18 and procedure 200 to place node 7. Its initial waypoint also depends on story
bit 1006. This is based on our own reading of the level scripts.

### The butterfly cursor

Own analysis of the Menu scripts and GOG executable: node 6 creates the butterfly from the actor
template at `04/01 +0x14` and gives it a reference back to node 6. The butterfly reads the screen
target from that controller's variables 53 and 54. It eases its own coordinates towards the target
with random offsets and calls procedure 409; when it reaches the target it switches animation and
stops calling the procedure until the target moves again. Node 32 is a different, waypoint-flying
actor.

Procedure 409 (0x4710C0, case 0x199) takes three integers, scales them by 0.001 and unprojects
them through 0x422940. It registers the actor in the screen-object list (0x45B9B0), which is drawn
in a separate pass (0x45CC80); the list persists after the last call. OpenRayman retains the screen
coordinates and updates the actor's world position after the camera moves, preserving its own
rotation and scale. It uses the viewer's 0.9-radian vertical field of view and 0.1 near plane,
with a centred 4:3 mapping consistent with the menu text at wide aspect ratios. The original
viewport scaling and separate draw pass have not been fully reproduced; the placement and size
still need comparison with the real game, especially at other aspect ratios.

The separate pass (own analysis, 2026-11-06): 0x405180 sets up a second viewport record at
0x5004D4 (display 100 x 100, 0..1000) with its own camera 0x50054C, built once by 0x4052C0: kind
0x67, angles 1.5 and 0.75 (0x422660), near 0.1, far 1000, at (0, 0, 5), turned a quarter about X
(0x445BE0) and a half about Z (0x445C70). 0x419A30 draws the world 0x5003E0 through it every
frame. Procedure 409 also makes the actor's transform type 4 and writes viewport width / 640
(0.0015625 at 0x49CC58) to the diagonal entries 0 and 8 of its combined matrix, leaving the rest.
So a screen object keeps its own orientation relative to that fixed camera. The intro's Ubisoft
logo (Jail_10 node 4 spawns actor `04/01 +0x680` with var3 = 1; object list `04/01 +0x9A0`: the
"Ubi Soft"/"Entertainment" plate and the arc, both lying in their XZ plane facing -Y) has an
identity transform. Left in its world orientation in front of the Jail_10 camera it showed its back,
mirrored. OpenRayman now turns each screen object's own orientation with the game camera,
followed by a half turn about the camera's up axis. Originally settled by eye on the logo, this
orientation is now checked against the fixed view and projection signs (see below). The menu camera looks
along +Y, where that turn is nearly none. Since the pass is a world of its own, the level's lights
don't reach it. The logo arc's white-to-yellow gradient comes from its saved vertex colours,
selected by procedure 393; its own light is switched off by procedure 387. OpenRayman applies
those colours without its stand-in white ambient light. Other screen objects without saved colours
still use that stand-in. The 409 scale (width / 640) isn't applied.

The logo fades in and out with procedures 11 and 12 (0x472450, cases 0xB and 0xC; first read by
a DeepSeek helper, checked against the decompile). 11 sets draw flag 8 at super object +0x2C
(and 3D data +0xE8) when its argument is 0, and clears it otherwise. Actors are saved with +0x2C =
0xFFFFFFFF, so flag 8 (opaque) is on by default. 12 writes a transparency level, clamped to
0..255 (0x49CC60 = 255, below 0 gives 0x49CBFC = 0), as an f32 to +0x38. Then 0x40F260 copies it
to the super objects listed at 3D data +0x98 (entry: super object, -, next) that lack flag 0x10000
at +0x30, passing flag 8 on to them. The logo's actor calls 11(1) once, then 12 each frame
while its var9 rises by var4 x frame time / 16 and falls again. OpenRayman draws an actor whose
flag 8 is clear at level / 255, blended.

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
exit path itself. Other actors also reach missing handlers. These findings come from our own analysis of the GOG
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

Further own analysis of 0x409060, 0x409500, 0x409B20 and 0x409BB0 (2026-11-06): the
character position is the accumulated previous advances plus half its own width times scale.
The `/-w` and `/-z` commands populate global width and scale tables respectively. The current
dialog layout follows this rule, but the intro counter's observed spacing remains unexplained.
The renderer already doubles saved vertex colours; applying the material ambient and diffuse terms
in an experiment did not remove the pink narration gradient. Its cause remains unverified.

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
- Procedure 369 and the Site Rayman2 entry: own analysis — the rule and node from `ortool brain Menu
  <node>`, the procedure table entry at 0x4A43E0 + 369 × 8, capstone 5.0.7 disassembly of 0x470210
  and of its duplicate at 0x587210, and pefile for the `ShellExecuteA` import slot 0x49C220 and the
  `.extra` section's bounds (2026-11-08).
- Nothing here was taken from Raymap, Rayman2Lib or any other project; no game data is included.

### View aspect

Own analysis of 0x465DD0 and 0x4660B0 confirms the metrics above. The latter multiplies
both coordinates by 0.1 (0x49CA04); 0x429670 then multiplies X and Y by 0.01 of the
view width and height respectively. Thus both slot axes are thousandths of the view.
OpenRayman keeps the screen-slot layer in a centred 4:3 area in wider windows (the HUD's
counters at the screen edge instead, see text.md), while the 3D view still fills the window. At 4:3 its glyph dimensions retain the executable's ratio.
The reported difference at 4:3 remains to be checked with a matched capture.

## Pause menu and Murfy (investigation, 2026-11-14)

The shared Fix controller at `04/00 +0x13E64` owns the in-game menu. Its behaviour 1
(rule `0d/00 +0x230BC`) tests input action 7 (Esc); its next rule tests action 23
(F1) with additional story and player-state guards. The tutorial tablet's Learn_10
node 42 behaviour 7 instead tests action 22, also F1, to replay its scene.

The Esc branch creates six labels from text IDs 0x16F, 0x167, 0x168, 0x16A,
0x170 and 0x169, at y=200..700 in steps of 100, with slot 0 initially selected.
In the English data these appear as Resume Game, Load, Options, OpenRayman 0.1.0
(the project's replacement text), Abandon and The Hall of Doors. The scripts disable
Load when there are no saves and conditionally disable the Hall of Doors. They generate
two temporary actors, including the visual effect, and fill screen slots through 505/506.
Behaviour 2 (`0d/00 +0x25410`) removes both temporary actors, restores Rayman's
saved variable, clears the menu state and returns the controller to behaviour 0.
The scripts request `menu` or `mapmonde` in different exit branches; the latter is the
Hall of Doors level, and its full flow still needs verification.

The executable's procedure 504 handler `0x4730A0` sets byte `0x4B9BC8`; the
particle update `0x4607E0` and draw routines `0x44B1B0`/`0x44B2E0` inspect it.
Procedure 306 (`0x467970`, branch `0x4679EE`) stops and resumes the game clock via
`0x409E90` and `0x409F00`. Procedure 413 (`0x4710C0`, branch `0x47131E`) calls
`0x45BA60`, which saves and restores a stack of special actors. Procedure 426
(`0x471E40`) and 427 call the sound pause/resume paths `0x4536D0`/`0x453700`.
How the pause works (implemented 2026-11-15):

- **306** (case 0x132) with a nonzero argument sets byte `0x500FAA` and notes the clock
  (`0x409E90`); with zero it sets the clock back to that note (`0x409F00`), so script timers
  skip the time spent paused. While the byte is set, the three actor passes (`0x40AA10`,
  `0x40A7C0`, `0x40AB90`) skip every actor whose custom bits (standard block +0x24) lack
  0x100000. The menu controller and its temporary actors carry that bit; Rayman, the camera
  and the rest of the world don't, so they stand still. The particle draw `0x460600` keeps
  only a few slots visible while paused.
- **413** (case 0x19D, `0x45BA60`) keeps a stack over the screen-object pass (up to 32
  objects at `0x514E20`, count `0x4B98DC`, level `0x4B98D8`): nonzero puts the current
  objects aside at a new level, zero ends the level and brings them back if their actor still
  exists (`0x413790`). Ours keeps a stack of our screen-object maps.
- **504** sets byte `0x4B9BC8`. While it is set the sector draw `0x44B2E0` skips the level's
  geometry and the actor draw `0x44B1B0` skips every actor without custom bit 0x100000, so
  only the menu shows. Each time the byte changes, the particle update `0x4607E0` empties the
  whole particle pool (its copy of the byte is `0x4B9B14`).
- **The swirl** behind the menu: the Esc rule generates actor `04/00 +0x1B0A4` (function 66)
  at the camera. Its script also calls 503 with op 83 every frame:
  `0x463030` emits one particle (flags 0x87007) every 0.05 s, 5 units along the camera's
  sight and 4 below, up to 4 to either side, rising 2.5..4.5 units a second. Fade 0x7000
  starts at alpha 5 and rises 350 a second; at 255 it holds (0x8000) for the particle's
  life (0.5 s), then fades out at 260 a second (0x3000) (`0x4600C0`, `0x461120`).

Before this, ours ran every actor during the pause, so the swirl's generated actor drifted
off as the camera moved (the "stuck swirl") and Rayman kept moving. Ours now freezes the world, lets the menu run
(selection skips the disabled Load) and resumes on Esc. Still open: the F1 glossary, and
checking Abandon / Hall of Doors against the game.

### Pause swirl colours (card 38, 2026-11-15)

The generated swirl actor `04/00 +0x1B0A4` places mesh `05/01 +0x656BB4` at the camera,
turned along its view. Its physical object is `+0x65BF84`; there are 258 vertices, a saved colour
set starting with `(109,109,109,255)`, and two 240-triangle elements. The pale water material
`+0x659B68` (`0x45DC03FF`, `foret_eau_anu_nzwtxy`, texture flags `0x80000048`) is submitted
before the darker vortex material `+0x65B3D0` (`0x4DDC03FF`, `bato_vortexnzw_txy`, texture flags
`0x80000008`). Both use ordinary alpha blending. Texture `0x40` controls depth writes; the
deferred alpha queue ultimately disables writes for both. Equal-depth triangles draw in reverse
submission order, placing pale water over vortex; see [geometry.md](geometry.md#texture-binding-and-deferred-alpha-triangles-card-38).

The actor's super-object flag `0x20000` disables sector **lights**, not depth testing
(`0x45C450` -> `0x426BD0`). Its single-LOD object selects its saved colour set automatically
(`0x45CDD0`), with the usual ambient/diffuse conversion and no specular contribution. Sector fog
remains independent. The material scroll offsets start at `(0.01,0)` and `(0.005,0)` respectively;
their speeds are the same values per nominal frame, advancing at the renderer's 60 Hz clock.

There is also a separate **dark animated background**, not just the level's fog-clear colour.
After the sector draw, `0x40B003` tests pause byte `0x4B9BC8` and calls `0x424440`. It submits
a full-screen grid of 6 by 5 vertices, before the alpha queue flush. `0x4241C0` generates
horizontal and vertical sine waves, interpolating byte RGB between cool `(8,23,34)`, dark
`(5,5,5)` and warm `(94,17,9)`, with four central vertices fixed at the dark colour. Its five
phases advance per paused render frame. The transparent swirl reveals this grid; leaving the
teal level clear underneath was the main source of OpenRayman's wrong colours. Missing default
saved colours, actor light suppression and the queue's layer ordering also needed correction.

Where this came from: own analysis of the installed GOG executable and Glide driver at the
addresses above, `ortool mesh`/`peek` on Learn_10, and comparison with the user's
`real_pause.webp` and local before/after screenshots (Codex, 2026-11-15). The captures are not
at a synchronized animation phase. The renderer implements these general draw rules without
recolouring either texture. Images and the detailed report stay under git-ignored `build/helpers/`.

When **The Hall of Doors** is available: the Esc rule greys it (`var12[5] := 1`) unless the
first byte of the entry pair that procedures 343/344 carry between levels (0x500FA4, read with
344 into var61) is 145. So it unlocks only in a level entered from the Hall itself, and stays
grey in Learn_10 reached from the intro, as in the real game. Not yet checked in ours: entering
the Fairy Glade from the Hall and pressing Esc should show it available.

F1 (Murfy's help, behaviour 10) closes on F1 *pressed this frame* (condition 45) and returns
through behaviours 2, 0 and 11 to behaviour 1, whose F1 rule opens it on F1 *held*
(condition 44). Behaviour 11 waits only for Esc and an action with no keys (+0xEF4) to be
released, not F1, so a press that lasts until behaviour 1 runs again opens the help again.
Ours does this (user, 2026-11-15); whether the real game's frame-by-frame behaviour changes
leave enough time for the key to come up is not checked yet.

### Where this came from

Own analysis of the installed GOG executable, the game's Fix and Learn_10 scripts
(`ortool input` and `ortool brain`), and OpenRayman baseline screenshots. Addresses
above are executable virtual addresses unless marked as data block offsets. No original
code or game assets are included here. This describes observed script and executable
paths, not a claim that OpenRayman implements them all.

### Selection Lum: screen depth and ordering (follow-up to card 38)

The generated selection Lum is `04/00 +0x1B40C`. Procedure 409 keeps it in the persistent
screen-object list while procedure 173 pulses its scale. Animation 287 supplies channels 57
(face, object 7) and 58 (orange body sprite, object 8, channel scale 1.95). The face mesh is
`05/01 +0x65C97C`: keyed teeth/eyes plus additive wings; the body sprite is `+0x65DCB4`.

The fixed camera at `0x4052C0` composes a quarter turn about X and a half turn about Z
(`0x445BE0`, `0x445C70`, `0x445130`), then orthonormalizes it (`0x444F90`). Including the
projection signs from `0x422AB0`, own +X goes screen-right and own +Z goes upward. The
existing equivalent turn in `screen_object_placement` is therefore retained; flipping it would
mirror the Ubisoft logo. The placement still preserves the actor's own scripted rotation and
scale independently of the matrix drawn after reprojection. A regression checks handedness,
upright orientation, nonuniform scale, and that a face in front of a sprite stays nearer.

The keyed teeth and eyes must occlude the orange body. Texture binding (`0x424A00`) keeps
material depth writes unless texture `0x40` suppresses them; the Glide material setup applies
that bit at `0x10007E18`. Source flags `0x12` on these textures include keyed-edge preparation:
exe `0x425240` prepares transparent texels, and the driver can clear `0x10` during loading
(`GliVd1vf.dll 0x1000894F`). The keyed material can then draw immediately, while the additive
body/wings are deferred (`0x10007EAD`). OpenRayman had instead blended every screen cut-out
without writing depth to keep its sharpened edges smooth, then added all sprites afterwards.
It now writes depth for opaque screen cut-outs while retaining their smooth edges, before the
screen view's deferred glow draws. Faded screen objects continue to blend without depth writes.
Screen sprite width and height also use separate row lengths of the model/view matrix
(`0x4291E0` calling `0x446200`/`0x446250`); the old volume-average scale made this Lum's
nonuniformly pulsing glow too wide. Its channel scale of 1.95 and sprite half size 1.10 are retained.

World particles also precede screen objects: `0x44BBFB` calls `0x4607E0`, which updates and
draws them through `0x461120` and `0x460A10`. The later `0x40B1A4` calls `0x419A30`, drawing
the screen list through `0x45CC80`/`0x45CA30`, without sector fog. Ours previously drew world
particles after screen objects, allowing a blue pause sparkle to burn over the Lum's face even
after the orange glow was correctly occluded. Screen meshes and their sprites now share their
own depth pass after world geometry, sprites and particles.

The optional swirl-centre twist and dark tip are a deliberate renderer improvement, separate
from these executable findings; see [IMPROVEMENTS](../IMPROVEMENTS.md). Its switch is
`OPENRAYMAN_SWIRL_CENTRE=0`. Identification requires both funnel materials, its 258-vertex,
two-element shape, and the active pause background; ordinary water is unaffected.

Where this came from: own reading of the installed GOG executable and `GliVd1vf.dll` at the
addresses above, `ortool mesh`/`anim`/`peek`, renderer winding/normal diagnostics, and before/after
captures of Learn_10's pause, Jail_10's Ubisoft logo at 2 seconds, and Menu at 4 seconds
(Codex, 2026-11-15). Compared with the user's `real_pause2.webp` and `lum_cmp.png`; captures
are not synchronized to the real game's animation phase. No executable code or assets copied
into sources. Images, diagnostic output and the progress report stay in `build/helpers/`.

## Menu saves (cards 24 and 40, 2026-11-15)

Menu node 6's name-confirmation rules call function 95 (collision), procedure 431
(name/completion), then 93 for a new game or 90 for current progress. A nonzero return
shows text 0x183, **The game has been saved successfully / press Enter to continue**,
and enters behaviour 16. The previous zero stub skipped that screen on a new game.
The handlers now write into OpenRayman's own save root; see [saves.md](saves.md).
Name storage is three bytes; typed name entry (card 74) remains unchanged.

Our optional autosave asks once below the first success message. Left/right selects Yes/No,
Enter persists it and continues; a known preference adds no question. `--autosave on|off`
changes it later. When on, the Hall and save-before-exit questions use automatic confirmation
only for the actor handling the save. Its original write and cleanup branches still run;
a runtime green Lum and Saving... replace the prompts. When off the original prompts run.

Where this came from: own analysis of executable handlers 0x47D830 (90/93/94),
0x47D730 (95), 0x4702A0 (431), 0x478E10 (215..217), and Menu node 6 / Mapmonde node 89
bytecode. Scripted tests and screenshots are recorded in `build/helpers/saves_report.md`.
Autosave UI and settings are our own additions, listed in IMPROVEMENTS.md. No executable
code, external implementation or game assets were copied into source files.
