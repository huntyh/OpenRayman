# The main menu

The game's front end. Unlike most engines of its time, Rayman 2 does not draw its menus with a
separate UI system: the menu is an ordinary OpenSpace level (`World/Levels/Menu`) with scenery,
actors and AI scripts, and the engine adds a screen-space text layer on top. This note records what
that level is made of, what it loads and runs, and how the text on it is drawn.

What it does *not* yet answer is the exact screen layout: where each button sits and what it says.
The text layer goes through a third-party font module that we have located but not read (see
[The 2D text layer](#the-2d-text-layer-open)).

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

Proven: the glyph set, the sound, that the level asks for both. Not proven: the per-frame order of
letters, which is inside the FON module below.

## The 2D text layer: open

The screen-space text and buttons are drawn by a **licensed third-party module**, not by OpenSpace
itself. Its identity strings sit together in `.data` at 0x49f6c0..0x49f6f4:

```
Module FON      FON V1.0.0      2D-FONT module      Oct 15 1999
```

Immediately before them, 0x49f5c0..0x49f614, is a table of 22 code pointers (its API), starting
at `FUN_00414690`. What the functions read so far show:

- `FUN_00414690` / `FUN_00414720` create and destroy a font object at `+0x28` of the engine's
  UI object.
- `FUN_004156e0` / `FUN_00415770` do the same for a second font object at `+0x2c`.
- `FUN_00415800` and `FUN_00415890` re-initialise and validate those two objects.
- They use the engine's own allocator (0x414710) and error reporter (0x402810, message 0x30).

So the engine keeps two text objects (probably attacking/normal text, or two sizes) and the module
draws glyph quads with them. Not yet read: how a string becomes quads, how the button frames and
the cursor are positioned, and what `menu ???` at 0x49f800 and `music ???` at 0x49f7f4 (the
engine's own debug strings, next to `Perso:`, `Ghost Object`, `no intelligence`) print.

**Where to continue from.** The FON API table is 0x49f5c0 (22 pointers, 4 bytes apart); the module's
identity block is 0x49f6c0; the engine's UI object fields are `+0x28` and `+0x2c`. Copy these
addresses into a Ghidra run (`analyzeHeadless … -postScript DumpDecomp.java`) on the Windows box and
read `00414690.c` onwards — the call sites of the `+0x28` object are what place the menu's text.

## Where this came from

- Own analysis of the retail GOG install (`Rayman2.exe`, timestamp 1999-10-15 23:19:39) and of the
  game files, on 2026-10-30.
- Which level it is, its super objects, textures, Fix textures and brains: `ortool scene`,
  `ortool level`, `ortool brain` against the real install.
- The glyph set and the logos: `Textures.cnt` and `Vignette.cnt` names, read through `ortool cnt`
  (the names are XORed with the archive's key byte, `mask`).
- The sound side: `ortool dsb Menu.dsb`, `ortool sound`, `ortool bank 0`.
- The disc table, the `Module FON` identity strings, the FON API table and the font-object fields:
  own analysis of `Rayman2.exe` (Ghidra decompilation in the project's notes, plus raw PE reads).
- Nothing here was taken from Raymap, Rayman2Lib or any other project; no game data is included.
