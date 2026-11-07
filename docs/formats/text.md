# Text rendering: shared data, two drawing paths

Own analysis of the GOG `Rayman2.exe`, its Ghidra export and Capstone disassembly,
and the installed game's Fix/level data, 2026-11-07. This describes behaviour in our
own words; no executable code or game assets are included. It supplements
[menu.md](menu.md), including the earlier colour/spacing investigation there.

## The common font is data, not a global tint

The level loader at **0x403730** reads the font root into **0x4A727C**, after the
level's animation arrays. It is the first word of the final 16 bytes of the level
GPT, followed by a material pointer, mode and sound event. It resolves into Fix:
`04/00 +0x15CA8` in Learn_10, Jail_10 and Menu. Learn_10's GPT field is at
`+0x1CB17C`; this offset is not valid for other levels. Stored absolute pointer
values differ between levels and must pass through their relocation tables.
OpenRayman now reads this root rather than hard-coding the Fix actor offset.

There are **two drawing paths**, not one set of layout and colour settings for
every text. Both reach this actor's initial object list (3D data +0x10, entries
pointer +4, count u16 +0xC, entries 0x14 bytes, physical object at entry +4).
The examined list has 882 entries. The module descriptor labelled FON does not
establish a single global colour or kerning policy for these paths.

| Property | Generated dialog / narration | Screen slots: menu, HUD, intro digits |
| --- | --- | --- |
| Entry | function 158, 0x475680 | procedures 505..507, 0x4731B0 |
| Layout | 0x409720 -> 0x409060 / 0x408DD0 | 0x465C10 -> 0x4660B0 |
| Font selection | function 158's actor argument | root at 0x4A727C, lookup 0x424E90 |
| Shape | original glyph mesh and its UVs | texture stretched onto a rectangle |
| Width | global widths or inline `/W`, multiplied by scale | fixed advance derived from slot size |
| Colour | selected glyph's saved colours and material/render state | white top, slot RGB bottom, 0x429670 |

## Character codes and commands

0x409720 maps ASCII lowercase onto uppercase, subtracts 0x20 from other bytes
through 0x9F, and subtracts 0x40 above 0x9F. `/O` adds a list offset; it does not
multiply the resulting glyph by a uniform text colour. 0x4085E0 resets that
offset (global **0x4A6BF8**) for each layout pass. Space is entry 0; colon is entry
26. Both have null physical objects in the examined banks, but still advance.
The **equals byte 0x3D**, entry 29, supplies the visible colon. Do not substitute
ASCII colon or move punctuation based on its English name.

0x409060 positions a glyph at the previous accumulated advance plus half its
own advance, as implemented previously. `/W`, `/H`, `/Z` at 0x408DD0 set width,
line height and scale, converting integers by 0.001. `/L` resets X and changes Y;
`/C` requests centring. `/D`, `/P` and `/E` control timing and script events in
function 158. Global widths are **256 floats at 0x4FF940** (0x409B00 reset,
0x409B20 `/-w`); cyclic scales start at **0x4FF7E0**, count **0x4A6BFC**
(0x409BB0 `/-z`). Default width/height/scale are **0x49F798/0x49F79C/0x49F7A0**,
set by procedure 352 through 0x408590. No pair-kerning table was found here.

Slots uppercase through 0x465D40, but shift extended bytes only **above 0xA0**
at 0x4660B0. `/O` selects both a texture-list offset and, for recognised values,
the bottom gradient colour. Previously OpenRayman discarded the texture offset;
it now preserves it. Offsets >=800 disable the gradient. Recognised colours are
0/600 blue (110,190,255), 200 red (250,70,70), 400 yellow (255,255,120).
Other offsets retain the preceding RGB while still changing the texture lookup.
This also corrects the old test that assumed `/O` never changed the glyph bank.

Slot X/Y remain floats, including during drawing; premature truncation has been
removed. At size 18 the advance/rectangle width is 38, height 45, line height 58;
at size 20 they are 42, 50 and 66. Disassembly of 0x465DD0 confirms these values.
Spaces use trunc(0.6 * advance); punctuation keeps the fixed advance and has the
vertical offsets already documented in menu.md. Slots do not use the generated
text width/scale tables.

## Dedicated camera and dialog padding

0x475680 unprojects screen positions through **0x5004D4**, not the world camera.
0x4052C0 creates its camera (**pointer at 0x50054C**) with horizontal angle 1.5,
vertical angle 1.125 (0x422660 multiplies 1.5 by 0.75), near 0.1, far 1000.
0x420520 initializes both renderer viewport scales to **1/3200**.
0x422940 uses reciprocal depth; its constant at 0x49C490 is **-1**. Combining
unprojection with projection (0x422AB0) at 640x480 gives, for truncated input
coordinates and `d = trunc(z)/1000`:

- anchor X = 320 + (0.64*x - 320)/(1+d)
- anchor Y = 240 + (0.48*y - 240)/(1+d)
- pixels per local X unit = 3200*d/(1+d)
- pixels per local Z unit = 2400*tan(0.75)/tan(0.5625)*d/(1+d)

0x475680 also applies viewport width/640 and height/480 to the glyph actor's
two relevant matrix axes. The expressions above are in our 640x480 reference
space. Centred text subtracts half the measured width on the horizontal axis.
The zero-depth unprojection branch is separate; the expressions describe the
positive depths used by the examined dialog strings.

OpenRayman previously used the **world** projection and one magnification for
both axes, plus an arbitrary four-pixel inset. It now uses the dedicated camera's
two magnifications and projected anchor. Example `(60,850,16)` projects to
`(42.8346,405.3543)` with magnifications `(50.3937,55.8500)`. This is a confirmed
cause of dialog size/padding differences, and also affects narration spacing.
Visual agreement with the real game still needs a matched capture.

## The intro counter and remaining colour discrepancy

The English table is selected through 0x494C70, with language byte **0x500FA8**
and language records rooted at **0x50026C**. Own decoding of Fix.sna and the
table pointers identifies intro narration **ID 12**. The relevant suffix is
`board=/D1:`: there is **no literal space before or after the equals byte**.
Timing/events around the line include P1500, E4, E10, D0 before the letters,
then D1, P5000, E11, E4, E3. The rendered number is not appended to this string.
Jail_10's logo/counter actor (`04/01 +0x680`, model `0d/01 +0x8F8`) formats the
integer into common ID 20035 and writes **slot 26 at (700,620,20)**. Its creator
starts it near 18900 and its script increments it. Thus the gap must be checked
between a projected mesh string and an independently positioned sprite string;
adding spaces to the text would conceal the cause.

**Confirmed widescreen spacing bug:** our slot path uses a centred 4:3 area but
the generated-text path used the whole window. At 1280x720, slot X 700 mapped
to reference X 416 rather than 448. The glyph-object narration's colon could
therefore run into the number even though its source string and advances were
correct. 0x419A30 draws both paths through the same viewport. OpenRayman now
applies its existing centred 4:3 text mapping to generated screen text as well.
This preserves their relative gap instead of adding literal spaces. At 4:3 the
mapping is unchanged. Compare both 4:3 and widescreen captures to distinguish
this confirmed cause from any remaining glyph/width discrepancy.

The examined A in bank 0 has saved signed channels including RGB (125,94,201)
and (32,29,49), so a lavender contribution exists in the saved data. Bank 600
shares that visual set. Bank 200 has a different painted palette, bank 400
another; bank 800 includes different geometry/materials. These are not evidence
that narration should be forced white. 0x45CDD0 -> 0x42AF70 -> 0x42AAE0 selects
the active saved colour set, 0x431400 converts signed channels by 1/256, and
0x42A410 plus 0x42D520/0x42D680 combines material, luminosity and renderer state
before clamping packed colours. OpenRayman's existing doubling is retained.
**The observed whitish narration versus lavender remains unresolved.**

For HUD slots 15/16, the examined scripts specify X 650 or 690, Y 70 / 180,
size 18. They already use the same font root as the menu; changing to another
font actor is not supported by the data. Default HUD colour and its reported
left/up offset are **not explained by the fixes to inline `/O` or fractional
coordinates**. A matched live snapshot is required before moving or tinting them.
Its horizontal position does differ deliberately from a whole-window mapping at
widescreen sizes, because of our centred 4:3 area. That explains a horizontal
comparison against a stretched original, but does not explain a vertical offset.

## Exact live reads to settle the remaining questions

Run the existing read-only tools as administrator. Record Jail_10 while ID 12
and slot 26 are visible and fully faded in; record Learn_10 during Murfy's first
page, the Globox kids' dialog, and normal gameplay with slots 15/16 visible.
Keep resolution, language, luminosity and screenshot frame/time with each read.
`r2record.py` already dumps strings and text actor slots; extend its `game_memory`
reads with the following fields (all addresses below are virtual addresses in the
examined exe, not saved-data addresses):

1. Snapshot 0x4E1080 + slot*0x1C for slots 15,16,26: 0x1C bytes, plus the
   NUL-terminated string at its first word. Decode +4/+8/+0xC as float X/Y/size,
   +0x10 alpha, +0x12 pulse, +0x13 flags, +0x18 colour. Our expected default
   bottom RGB is (110,190,255), white at the top; full alpha is 255.
2. Read **0x4A727C** (font actor pointer), then its first word (3D data),
   +0x10 (initial list), list+4 (entries). Compare entries 29,33,233,433,633:
   the physical pointer is entries + index*0x14 +4. This settles live font
   selection and whether the loaded list differs from the saved one.
3. Read 256 floats at **0x4FF940**, the three defaults at **0x49F798**, the
   scale count at **0x4A6BFC** and that many floats at **0x4FF7E0**. Check width
   indices 0 (space), 26 (blank colon), 29 (visible colon), 33 (A). Record them
   at ID 12 and during Learn_10, not only at menu startup.
4. Read screen camera P = u32(**0x50054C**): floats P+0x64 (horizontal angle),
   +0x68 (vertical), +0x74 (focal length), +0x78/+0x7C (axis projection factors),
   +0x84/+0x88 (centre); viewport size u32 at **0x5004DC/0x5004E0** (height/width).
   Renderer viewport scale floats are at V+8/V+0xC, V = u32(**0x5036AC**)
   followed by u32(V), i.e. the rendering context's viewport pointer.
5. For narration, text actors are the 30 pointers at **0x4FF8C0**. Find the
   active actor S from r2record's text topic. Capture S+0x20 (local transform,
   0x58 bytes), S+0x2C flags, S+0x38 alpha. First glyph child is u32(S+8),
   next sibling at child+0x14; capture child+4 (physical object) and transform
   child+0x20. This identifies the actual visible-colon glyph and its position.
6. To settle colour, the rendering context is R = u32(**0x5036AC**). It is
   reused, so **sample while drawing a narration glyph**, not after the slot
   pass. Best breakpoint/read points: **0x42AA30** (triangle material setup),
   **0x42D520** or **0x42D680** (final saved-colour conversion). Capture R+0xC
   and +0x10 flags, R+0x2D3B8 material pointer, 8 floats at R+0x2D2C4
   (ambient and diffuse terms), 4 RGBA float records at R+0x14C14 (light sum),
   and 4 packed colours at R+0x9C78, stride 0x10. Capture luminosity
   float **0x4A0488**. Repeat for a pink highlighted glyph in the same box.
   Also capture saved-colour mode bytes **0x4B98E8/0x4B98EC/0x4B98F0**.
   The packed result compared with saved channels distinguishes changed colour
   selection, added lighting, and the driver's texture/colour combination.

The normal polling tools cannot guarantee sampling inside a glyph draw. If they
only provide frame snapshots, collect items 1..5 first and keep item 6 explicitly
unsettled; arbitrary samples of this shared context cannot prove narration colour.

Use `python tools/r2record.py <folder> --only text,text_draw` to write
`text_draw.txt` alongside the text events. It samples only while a text actor or
screen slot is present and visible. Renderer context values are frame snapshots,
not samples inside 0x42AA30/0x42D520/0x42D680; the reused context cannot establish
a narration or highlighted glyph's colour without a draw-time breakpoint. Scale
counts over 56 are reported but not read, because the actor table follows the array.
Record screenshot frame/time and display settings separately.

## The 2026-11-07 recording

The user's recording (Menu, Jail_10 intro, Jail_20, Learn_10's kids scene, 1600x1200) confirms the
counter's layout (slot 26 at 700,620, size 20) and its click (see sound.md). Rechecking 0x466153..0x466179:
its outputs are distinct (+0x20 height, +0x24 advance, +0x30 line height, +0x34 a second advance), so
no size change follows; drawn size 18 is 38x45 slot units, size 20 is 42x50 (pinned in tests). The user
compared the narration: "Look what the pirates have done to our world" is lavender in the real game
too. The recorder read the glyph actors' local transforms as object bytes; r2record now follows those
pointers and reads the viewport scales from the rendering device (0x420450, device list 0x506B20), so a
new recording is needed for per-glyph colour questions. The engine gained `--window-size WxH` and
`--place x,y,z,yaw` for captures at a recording's resolution and position.
