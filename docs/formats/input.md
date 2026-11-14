# Input

How the game turns keys and joystick buttons into what scripts test: **input actions**, each a
small expression over keys, buttons and other actions, evaluated every frame.

## Where it is

Fix.gpt, after the pointer at 0x184: the exe copies 0xB20 bytes into its input state (0x509E60).
Inside those, at file offset 0x888, a u32 count (138) and a pointer to the action array (Fix
`06/00` +0xA74 on PC). The array is 0x18-byte entries:

| Offset | Field |
| --- | --- |
| 0x00 | u32 slots: the length of the expression, in 8-byte slots |
| 0x04 | pointer to the expression (Fix `06/00`, after the array) |
| 0x08 | pointer into block `0a/00`, which has no data in the files (filled at run time) |
| 0x0C | i32 counter (below), -2 in the files |
| 0x10 | f32 analog value: cleared every frame, set by joystick axes |
| 0x14 | u8 enabled: 1 to be evaluated |

Script nodes of type 14 (input) point at an entry.

## Expressions

8-byte slots in prefix order. The first byte of a slot says what it tests (the poll function
table at 0x4A0F18); operands follow in the next slots. The low bytes of the first word read
0xF0nn in the files; only the low byte matters.

| Kind | Slots | Test |
| --- | --- | --- |
| 1, 2 | 1 + two expressions | both / either. Both sides are always evaluated |
| 3 | 1 + one expression | not |
| 4, 5 | 2 | key went down / up this frame. Operand: i16 DirectInput scan code |
| 6, 7 | 2 | key down / up, for between min and max frames (operand bytes +5, +6; 0..255 = any) |
| 8..11 | 2 | another action's counter (operand: pointer): == 1, == -1, > 0, < 0 |
| 12, 13 | 3 | joystick button went down / up this frame (pad, button) |
| 14, 15 | 3 | joystick button down / up, for between min and max frames (the button slot's bytes +5, +6) |
| 16 | 5 | joystick axis in a range (pad, axis, min, max); sets the action's analog value |
| 26 | 3 | joystick button held, for between min and max frames (the analog pads' test) |
| 33 | 2 + steps | a sequence typed in order (u32 step count), from a history of key events: cheat codes |
| 34, 35 | 2 | sequence step: key down, key up (36, 37: the same for a joystick, 3 slots) |

Other kinds (17..25, 27..32) are not read yet; Learn_10's actions don't use them.

The "frames" of kinds 6, 7, 14, 15 and 26 count frames since the button or key last changed: reset
on a press or a release, then counted up to 255 (0x430880 does this for the keys; the pads keep the
same table per button).

Kinds 12..15 read the pad's button mask directly (0x42EDB0..0x42F120): 12 is `~previous & current`,
13 is `previous & ~current`, 14 and 15 are the held and the released test for a range of frames.
Kind 26 (0x42FE80) is the analog pads' version and also requires the button to be down for the
frames; the game's data uses it for every button it names, so it is effectively "held".

Kind 16 (0x42F270) reads the pad's axis over the pad's range, multiplies by 100 (0x49C698) and
tests the result against min..max. When it fits it *also* stores the value in the action's analog
value at +0x10, which the frame update clears every tick. So a stick at rest reads 0, which is
inside the game's -110..110 windows: movement actions 0 and 1 are always down, and their analog
value is where the stick is.

## How the stick moves Rayman

Rayman's scripts read movement through procedure 346 (0x46E450). With a direction key held it builds
-100/+100 per axis from the key actions (eased, halved for walking). With none held it reads the
stick instead: the two actions procedure 347 named (0x4B9B54/58, Fix's actions 0 and 1, `pad 0 axis
4/5`), whose analog values (+0x10, read by 0x42E8D0) the axis tests set. The exe measures each
against a calibrated centre (0x4B9B5C/60, taken by 0x46FB30) and the extremes seen so far
(0x4A40A4..B0, saved with the options by 0x407B80), scaling to -100..100; our SDL feed is already
centred and scaled, so we use the values as they are. If the pair is longer than 90 (0x49CC24 =
8100, compared at 0x46E908), it is scaled to a length of exactly 100: 90% of the travel is a full
push. The keys' easing and walking half don't apply to the stick. Own analysis (capstone and the
decompile), card 101.

## The pads, and how SDL3 maps onto them

The exe keeps up to four pads (its state block at 0x509E60, 0x28 bytes each, with type 3 and 4 =
digital and analog pads), buttons 0..41 (bit masks in a table at 0x4A0E70) and axes 4..9 (the
first six of a pad). Every level's input actions test the same ones, and only these:

| Action | What it is | Tests |
| --- | --- | --- |
| 0, 1 | movement, horizontal and vertical | `pad 0 axis 4` / `axis 5`, -110..110 |
| 2..5 | move up, down, right, left | keys only (arrows) |
| 9, 11 | confirm, shoot | `pad 0 button 10` |
| 8, 10, 15 | back, jump | `pad 0 button 11` |
| 12 | the action Ctrl and Z also give (13, 14) | `pad 0 button 14`, or `pad 0 axis 6` in -110..-50 |
| 16, 17 | turn the camera | `pad 0 button 13`, `pad 0 button 12` |
| 18 | Num0 | buttons 12 and 13 together |
| 24 | the J key | `pad 0 button 15` |
| 25, 26 | menu left, right | `pad 0 button 10`, `11` |

So the range a stick has to produce is **-100..100 for full deflection** (the data allows a little
past it, -110..110), and the six buttons the game names are 10..15. `src/main.cc` maps SDL's
standard gamepad onto them (`pad_map`):

| SDL | Game | Why |
| --- | --- | --- |
| `SOUTH` (A / cross) | button 11 | jump (and back, menu right) |
| `EAST` (B / circle) | button 10 | shoot (and confirm, menu left) |
| `WEST` (X / square) | button 14 | action 12: recentres the camera, as Ray2Fix maps X (user, 2026-11-15) |
| `NORTH` (Y / triangle) | button 15 | the J action |
| `LEFT_SHOULDER` / `RIGHT_SHOULDER` | buttons 12 / 13 | turn the camera (Q / W); both: action 18 |
| right stick, past half way right / left | buttons 12 / 13 | turn the camera, as in most games (our choice): Q turns the view right |
| `LEFT_STICK` / `RIGHT_STICK` (click) | button 14 | action 12 |
| `LEFT_TRIGGER` / `RIGHT_TRIGGER` | axis 6, negative | action 12 (DirectInput's combined trigger axis) |
| d-pad up / down | buttons 15 / 14 | the menus' up and down (actions 101, 102 test only these) |
| left stick (d-pad left/right override axis 4) | axes 4 / 5 | movement (actions 0, 1); up is negative, as in DirectInput |
| `START` | the Esc key | pause |

`feed_pad` in `src/main.cc` reads the whole pad from SDL after any of its events. Sticks are
-32768..32767 in SDL and the game scales them to -100..100; a 15% dead zone around the centre keeps
a worn stick from walking Rayman. In play the d-pad's up/down are buttons 15/14 (the J action and
action 12), because that is all the menus test; walk with the stick. Pads are opened
with `SDL_OpenGamepad` on `SDL_EVENT_GAMEPAD_ADDED` (and the ones already plugged in at start-up),
fed only while playing, like the keyboard; a lost window focus or a removed pad releases
everything. The first pad open is the game's pad 0, the only one the data tests. The SDL hints
`SDL_HINT_JOYSTICK_HIDAPI` and `SDL_HINT_JOYSTICK_HIDAPI_STEAM` open a Steam Controller without
Steam. Typed sequences on a pad (kinds 36, 37) are not implemented, like the key ones.

The joystick kinds come from disassembling their poll functions:

- 12 at 0x42EDB0 and 13 at 0x42EEB0: the pad's button mask (`pad state +8`, current, and `+0x101`,
  previous), `~previous & current` and `previous & ~current`.
- 14 at 0x42EFE0 and 15 at 0x42F120: the same masks, then the per-button frames at `0x509FC0`
  compared with bytes +5 and +6 of the **button's** slot (node +0x15/+0x16), not the pad's.
- 26 at 0x42FE80: the same frames, with the mask test limited to the holdings the table 0x4A0E70
  masks describe (button codes 10..41).
- 16 at 0x42F270: `fild` of the axis, `fidiv` the pad's range from its capabilities, `fmul` by
  100.0 (0x49C698), `ftol` and the min..max compare; on success `fstp` into `[action + 0x10]`
  (0x509E34 is the action pointer while the expression is evaluated).
- The button numbers the masks cover are 0..31 in `+8` and 32..41 in `+4` (table 0x4A0E70: 10..17
  are bits 0, 1, 6, 7, 4, 5, 2, 3 there).

The pad table (0x509E60, 0x28-byte records) and the four pads' pointers (0x509E7C) come from
procedure 418 and the re-acquire code the roadmap log mentions (0x42E1E0/0x42E220). Which SDL button
becomes which game number is our choice, made from what the actions name (the table above); the
original's own layout is a DirectInput pad's, which SDL's standard mapping mirrors.

Own analysis, as everything else here.
