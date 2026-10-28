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
| 12..15, 26 | 3 | joystick buttons (pad, button) |
| 16 | 5 | joystick axis in a range (pad, axis, min, max); sets the action's analog value |
| 33 | 2 + steps | a sequence typed in order (u32 step count), from a history of key events: cheat codes |
| 34, 35 | 2 | sequence step: key down, key up (36, 37: the same for a joystick, 3 slots) |

Other kinds (17..25, 27..32) are not read yet; Learn_10's actions don't use them.

The "frames" of kinds 6 and 7 count frames since the key last changed: reset on a press or a
release, then counted up to 255 (0x430880).

## The counter

Every frame, for each enabled action (0x42E710):

- down and counter > 0: one more (frames held)
- down and counter < 0: 1 (the press frame)
- up and counter > 0: -1 (the release frame)
- up and counter < 0: one less (frames since release)
- a counter of 0 never moves, so the files start them at -2.

Script conditions 44..47 test it: held (> 0), just pressed (== 1), not held (< 0), just released
(== -1). Actions can test each other (kinds 8..11): Learn_10's "recentre camera" is Ctrl or a held
action 12, a joystick button or the right stick pushed down.

## The default keys

`ortool input <game folder> <level>` lists every action. On PC they match the game's controls:

| Action | Keys |
| --- | --- |
| 2..5 | arrows: up, down, right, left |
| 6 | LShift, Num7: walk |
| 10 | A, Num8: jump |
| 11 | Space, Num1: shoot |
| 13 | Ctrl: camera back behind Rayman |
| 16, 17 | W, Q: turn the camera |
| 28..38 | cheat codes (kind 33) |

Many more are debug keys (F-keys, letters, the numeric keypad). Procedure 435 hands five actions
to the movement code (procedure 346): in Learn_10 actions 2, 3, 5, 4 and 6 (up, down, left, right,
walk). Scan codes are positions on the keyboard, so on a non-US layout the keys keep their places
(the game's "Z" is the key left of X).

## Where this came from

Own analysis of Rayman2.exe (the Fix loader at 0x402D0F, the frame update at 0x42E710, the poll
functions of table 0x4A0F18, the key snapshot at 0x430880, procedure 435 at 0x46FAA0), checked
against the Fix data and against the game's real controls as the project lead plays them. A helper's
first reading of 0x42E710 claimed a one-frame press lag; the code shows the counter jumps from
negative to 1 on the press frame itself.
