# LEVELS0.DAT (Rayman 2 PC)

`Data/World/Levels/LEVELS0.DAT` holds the relocation tables of every level. `Fix` keeps its tables
as plain files next to it (`Fix.rtb`, `.rtp`, `.rtt`, `.rts`); the levels don't. The file is about
150 MB, almost all of it random filler (≈8 bits of entropy per byte), with the real data scattered
inside — a copy protection measure.

Worked out from `Rayman2.exe` (GOG release): parameter setup at `0x44de60`, lookup at `0x44eb40`,
table stream setup at `0x44e8a0`. Implemented in `libopenrayman/src/relocation.cc` (`levels_dat`);
verified on all 70 levels, every pointer resolves.

## Encoded values

The file starts with a 16 byte key `a, b, c, d` (u32). The k-th u32 after a key (k = 0, 1, ...) is
stored as

    raw = (value XOR (b + d·k)) + (a + c·k)        (mod 2^32)

so `value = (raw − (a + c·k)) XOR (b + d·k)`.

## Header (key at offset 0)

| k | Value (GOG) | Meaning |
| --- | --- | --- |
| 0 | 0x00B0F800 | unused |
| 1 | 73620 | number of 2048 byte sectors after the first |
| 2 | 280 | 4 × number of levels (70) |
| 3 | 0 | unused |
| 4 | 12 | number of copies of the index |
| 5 | 24 | chunk divisor (used by the removed CD check) |
| 6 | 2048 | base offset |
| 7 | 2048 | sector size |

## Finding a table

Inputs: level `L` (index in the Game.dsb level list), table type `T` (0 = rtb, 1 = rtp, 2 = rtt,
3 = rts) and any copy `n` in `0..11`. All copies point at identical data.

1. `index = L·4 + T`
2. `v = 1.06913`, then for `i` in `0..n-1`: `v = v + 0.69314·|sin(i²·1.69314)| + 0.52658`
3. `x = floor(frac(v)·10⁶)·10⁻⁶`, `offset = floor(x·73620)·2048 + 2048`
4. At `offset` is another 16 byte key. The table position is encoded value number `index` after it.

## Table data

At the table position, the data is XOR masked byte by byte, like DSB and SNA files:
`byte ^= (mask >> 8) & 0xFF`, then `mask = step(mask)`, where

    step(m) = 16807·(m XOR 0x75BD924) − 0x7FFFFFFF·((m XOR 0x75BD924) / 0x1F31D)

The initial mask is `step(L | T<<8 | n<<16 | (~n & 0xFF)<<24)`.

Decoded, the first 4 bytes differ per copy (meaning unknown). The rest has the same layout as
`Fix.rtb`: u8 block count, u32 unknown, then per block: u8 module, u8 id, u32 pointer count, and
8 byte entries (u32 address, u8 target module, u8 target id, 2 padding bytes `CD CD`).
