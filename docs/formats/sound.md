# Sound: banks, level placement files and the music themes (Rayman 2 PC)

Everything the game plays that isn't the music stream itself: footsteps, Lums, voices, impacts,
ambient loops. It lives in 405 **BNM banks** (`Data/World/Sound/Bnk_0.bnm` … `Bnk_404.bnm`, 76 bytes
to 6.6 MB, 38.2 MB in all), and each level's `<Level>.snd` says which of them a level loads and
where its sounds go.

A bank is `header (0x2C bytes) + events (0x20 each) + resources (0x5C each) + group data + sample
data`. Everything is addressed by a 32-bit id, `(bank << 16) | index`, and that id names an
**event** — never a resource directly. An event says what to do: play the resource it points at,
or stop the voices some earlier event started. A resource is one sound: 16-bit PCM inside the bank
or an external `.apm` file, a group of them, or — type 11 — a **music theme**, an ordered list of
`.apm` pieces. The APM stream format is in [FORMATS.md](../FORMATS.md#apm-music).

The scripts and the animations never hold a real event pointer: they hold a *slot* that `<Level>.snd`
fills in at load. That indirection is what makes the whole thing work, and it is the part easiest to
get wrong, so it has its own section below.

An earlier pass at this format (notes in `H:\r2export\sound-report.md`) got the header size, the
resource record layout and the `.snd` pair format wrong; this document supersedes it. See
[Where this came from](#where-this-came-from) for what specifically was wrong.

## The bank file

### Header (0x00–0x2B, 11 u32)

| Offset | Value in the shipped banks | What |
| --- | --- | --- |
| 0x00 | 0 | unused |
| 0x04 | 0x2C | header size, and the offset of the event array |
| 0x08 | n | **event count** |
| 0x0C | 0x2C + n·0x20 | offset of the resource array |
| 0x10 | m | **resource count** |
| 0x14 | a | start of data region 1 (MPEG) |
| 0x18 | b | start of data region 2 (MIDI) |
| 0x1C | c | start of data region 3 (PCM samples) |
| 0x20 | d | start of data region 4 (ADPCM) |
| 0x24 | e | end of data region 4 |
| 0x28 | file size | = the file's length |

All 405 banks agree on `[0x04] = 0x2C`, on the resource offset being exactly `0x2C + events·0x20`,
and on the size field. The five region offsets are non-decreasing absolute file offsets, giving
regions `[a,b)`, `[b,c)`, `[c,d)`, `[d,e)`. In every shipped bank `a == b == c` and `d == e`, so only
region 3 carries data: 269 banks have a PCM region, 136 have none and reference only external files
and groups.

The four regions belong to four decoder modules the sound engine registers, each with a "can free
data" callback that decides whether its region is copied or used in place:

| Region | Module | Test at load |
| --- | --- | --- |
| +0x2C | MPEG | `_SND_fn_bCanFreeDataMPEG@0` |
| +0x30 | MIDI | `_SND_fn_bCanFreeDataMidi@0` |
| +0x34 | PCM sample | `_SND_fn_bCanFreeDataSample@0` |
| +0x38 | ADPCM | `_SND_fn_bCanFreeDataADPCM@0` |

`Snd_Cpa.ini` in the game's `DLL` folder names the modules behind them: `WAVMW`/`WAVPW` (MMX or
plain) for samples, `APMMX`/`APMPW` for ADPCM.

`0x453990` reads the file: the header, then everything up to region 1, then each region. Then
`0x453BF0` walks the events and resources and turns their ids into pointers. The two lookups
everything else uses are `_SND_fn_pGetBinEvent@4` (0x453F50) for events and `_SND_fn_pGetBinRes@4`
(0x453F90) for resources; both take `bank = id >> 16`, `index = id & 0xFFFF`, and walk fixed strides
of 0x20 and 0x5C.

### Event records (0x20 each, at 0x2C)

1012 in the shipped banks: 875 of type 1, 137 of type 2.

| Offset | Type | What |
| --- | --- | --- |
| +0x00 | u32 | editor id, unique per record (`_SND_fn_czGetResNameFromEdId@4` looks names up by it) |
| +0x04 | u32 | **type**, see below |
| +0x08 | u32 | target: a resource id for types 1 and 6–10, an event id for types 2 and 4 (`0xFFFFFFFF` = none) |
| +0x0C | u32 | type 1: random **pitch variation** in 16.16 (0 = exact); types 4/5: the second event |
| +0x10 | u32 | 0 |
| +0x14 | u32 | types 6–8: pitch variation in 16.16 |
| +0x18 | u32 | 0 or 1, not decoded |
| +0x1C | u32 | 0 or 1, not decoded |

The types, read from the play switch `0x452D50` (jump table at 0x45300C):

| Type | What it does | In the shipped banks |
| --- | --- | --- |
| 1, 6, 7, 8, 10 | play the resource at +8 | 875 (all type 1) |
| 2 | **stop** the voices a given event started (`0x451E20`) | 137 |
| 3 | stop every voice of a channel (`0x451DC0`) | – |
| 4 | play the event at +8, then the one at +0x0C | – |
| 5 | rejected with an error | – |
| 9 | theme/music control (`0x4581E0`) | – |
| 11 | channel volume, pushed onto the channel's list (`0x451880`) | – |

Type 2 is a stop, not a play. `0x451E20` → `0x451D50` walks the running voices and kills
the ones on that channel whose owning event pointer is the one passed. 1472 of the 11880 level
`.snd` pairs are stops, which is how the ambient loops get switched off.

The pitch variation goes through `0x4527D0`: 0 means no variation, otherwise the voice plays at
`0x10000 - v + rand()·2v`, so `0x3333` is ±20%. The non-zero values in the data are `0x51D`,
`0xA3B`, `0xCCC`, `0xF5C`, `0x147A`, `0x1999` and `0x3333`.

`0x453DB0` resolves the +8 word: with `pGetBinRes` for types 1 and 6–10, with `pGetBinEvent`
for type 2, with both for types 4 and 5, and not at all for 3, 9 and 11.

### Resource records (0x5C each, at header[3])

2055 in the shipped banks. The name is 16 NUL-padded bytes at +0x48; a record with a zero name is a
perfectly good resource addressed by index only (225 of them: every group and every theme is
unnamed).

| Offset | Type | What |
| --- | --- | --- |
| +0x00 | u32 | editor id, unique (`czGetResNameFromEdId` looks names up by it) |
| +0x04 | u32 | **type**: 1, 4, 8, 10, 11 in the shipped banks |
| +0x08 | u32 | 1 = data embedded in the bank, 0 = external file |
| +0x0C | u32 | data size in bytes |
| +0x10 | u32 | data offset inside the resource's data area, 4-byte aligned |
| +0x14 | u32 | static volume 0–127 (127 in most records) |
| +0x18 | u32 | type 10: entry count; otherwise a flag |
| +0x1C | u32 | type 11: start segment; type 10: gain; otherwise a flag |
| +0x20 | u32 | type 8: entry count; otherwise a flag |
| +0x24 | u32 | type 11: entry count; otherwise a flag |
| +0x28 | u32 | type 4: entry count; type 11: offset of the tail entry, or `0xFFFFFFFF` |
| +0x2C | u32 | 1 = external file with no data region, 0 = embedded |
| +0x30 | u32 | 0 or 1, not decoded |
| +0x34 | u32 | 0 |
| +0x38 | u32 | the size again for an embedded type 1, 0 for an external one |
| +0x3C | u32 | sample rate; 22050 in every shipped type-1 record |
| +0x40 | u32 | format: `0x00010010` = 1 channel, 16-bit; `0x00020010` = 2 channels, 16-bit |
| +0x44 | u32 | data format: 1 = PCM, 2 = MPEG, 4 = external `.apm` |
| +0x48 | 16 bytes | NUL-padded file name (`*.wav` embedded, `*.apm` external) |
| +0x58 | u32 | padding up to the next record |

`0x453BF0` picks the base of a record's data from its type before calling `0x457A10`:

| Type | Data base | Loader |
| --- | --- | --- |
| 1 | the region its sub-format names: +0x34 for 1 (sample), +0x2C for 2 (MPEG), +0x38 for 4 (ADPCM); none when +0x2C is 1 | 0x455200 (WAV module), 0x4554E0 (MPEG), 0x4558A0 (CD) |
| 2 | the MIDI region, +0x30 | 0x4554E0 |
| 3 | the sample region, +0x34 | 0x4558A0 |
| 4, 6, 8, 10, 11 | the **extra area**, +0x28 | 0x45AB70, 0x457D50, 0x45A940, 0x45A440, 0x459840 |
| 5 | converted to type 8 and loaded as one (`0x45A9A0`) | – |

### Samples (type 1, 1830 records)

| Kind | Records | +0x08 | +0x2C | +0x44 | Data |
| --- | --- | --- | --- | --- | --- |
| Embedded PCM | 740 | 1 | 0 | 1 | region 3, offset +0x10, size +0x0C |
| External APM | 1090 | 0 | 1 | 4 | the loose `World/Sound/<name>` file |

Embedded data is **headerless signed 16-bit little-endian PCM**, mono, 22050 Hz (`0x00010010`). It
begins directly with samples: no `RIFF`, no `WAVE`, no `fmt ` chunk anywhere in the banks.

The embedded records **tile the PCM region exactly**. Sorted by offset, each one ends where the next
begins after rounding up to 4 bytes, and the last ends at the region's end; all 467 adjacent gaps
and all 269 tails are zero, so nothing is padded or hidden. Records can still share an offset, which
is the same sample used by several resources.

External records name a loose `.apm` file, and the banks hold no APM payload (no `00 20 02 00 22 56`
header anywhere). For all 1090 records the loose file exists and its size equals +0x0C exactly.

### Groups (types 4, 8, 10)

Their data lives in the **extra area**, from the end of the resource table (`header[3] + count·0x5C`)
to the start of the PCM region (`header[7]`); `0x453990` points the bank's +0x28 field at it and
the loaders use it as the base for +0x10. The loaders resolve the id word of each entry with
`pGetBinRes` and leave the rest as data.

| Type | Records | Entries | Stride | Count field | Id at | Other words |
| --- | --- | --- | --- | --- | --- | --- |
| 4 | 17 | 54 | 0x10 | +0x28 | +0xC | 3 parameters, then the id |
| 8 | 10 | 126 | 0x08 | +0x20 | +0x00 | a selector, 0–16 |
| 10 | 51 | 154 | 0x0C | +0x18 | +0x00 | gain, flag |

All but 16 entries point at a type-1 sample: five type-8 and nine type-4 entries point at a type 10,
and two type-10 entries at a type 11. The type-8 selector is matched against a value the play routine
derives from the resource, so a type 8 is a set of variants of one sound. The type-4 and type-10
players (`0x45AD30`, `0x45A1E0`) scale the volume by the resource's +0x1C and hand the group
to the mixer. The selection rule itself is not decoded.

### Themes (type 11) — the adaptive music

147 themes in 85 banks, 2093 segments. All unnamed, and **every segment is an external `.apm`**
(2088 of them; five entries point at a type-10 group). The theme is the ordered playlist and the
`.apm` files are the 2–3 second pieces, so this is where the game's adaptive music actually lives.

- Entries are 6 words (stride 0x18), count at +0x24, base +0x10 off the extra area. Word 0 is the
  segment's resource id; the rest are per-segment parameters, not decoded (word 1 looks like a 0/1
  "entry point" flag and word 3 like a "loops" flag).
- +0x1C is the segment the theme starts on, 0 in every shipped record.
- +0x28 is either `0xFFFFFFFF` (no tail) or an offset **from this theme's own entry block** holding
  one more resource id, played when the list runs out. `0x459840` computes it as
  `entry_base + resource.offset + [+0x28]`, not from the start of the extra area. 9 themes have one,
  and in all 9 it sits immediately after the last entry, i.e. +0x28 = `count·0x18`. The tails are
  `Sk10.apm`, `PL3F.apm`, `ROR07.apm`, `VU1R06F.apm`, `HE06.apm`, `BOF.apm`, `ASF.apm`, `BOF.apm`
  and `RHF.apm`.
- Examples: `Bnk_106` theme 2 is `PL301.apm` … `PL312.apm` with tail `PL3F.apm`; theme 16 is
  `PL501`–`PL504`; theme 21 is `PL505`–`PL514` then `PL501`–`PL504` again, 14 entries, so a list
  may repeat a piece. `Bnk_0` theme 15 is `JINGLE8.apm`, `silence.apm`; theme 203 is `new01.apm`,
  `silence.apm`. A `silence.apm` segment is how a theme fades out.
- Playing one: an event whose resource is type 11 goes `0x457E60` case 0xb → `0x459950`, which
  sets up the theme state at `DAT_004B95B8` and starts a segment through the APM decoder.
  `_SND_fn_lPlayTransitionSxd` and `_SND_fn_bSetNextTransitionSxd` change segment, and
  `dbgSND_fn_bGetAskedTheme` / `dbgSND_fn_bGetThemePosition` read that state. Event type 9
  (`0x4581E0`) is the matching stop request.
- **Who asks for one:** the scripts, through an ordinary sound id. 1425 of the 11880 level `.snd`
  pairs name an event that plays a type-11 theme, across 58 of the 70 levels; `Fix.snd` adds two
  (ids `0x13` and `0x75`) for the `Bnk_0` jingles.

## The ids are events

The 32-bit id used everywhere in the data is `(bank << 16) | index`, and it always names an **event**.
`_SND_fn_pGetBinEvent` is the lookup; `pGetBinRes` is only used from inside an event and from group
entries. A level's `.snd` file can therefore only contain event ids.

A resource id inside an event is the same format with a different meaning: `(bank << 16) | resource
index`. Both are resolved by the same arithmetic, against the event table and the resource table
respectively.

## How the scripts and animations name a sound

Neither one stores a real event pointer. Both store a **slot** — a word somewhere in the level's or
Fix's memory image — and `<Level>.snd` fills it in at load.

### Animation events (type 0)

An animation event points at a descriptor; descriptor +0 is the id field, +4 the type and +6/+7 the
gates (see [animation.md](animation.md#events)). For type 0, `0x41AFB0` calls
`0x452D20(actor, sound_channel, id, 0, 0)` with the descriptor's +0 field. Before the `.snd` pass
that field holds an editor address; afterwards it holds an event pointer.

### Script node type 30

The interpreter's sound-event node (type 0x1E) is `{u32 param, u16 skip, u8 indent, u8 type}`, and
`param` is the same kind of slot. In Learn_10's script block `0d/01`, `+0x8720` is
`[0x066A02F0][0x00000000][0x07][0x1E]`.

### The `.snd` file fills the slots

`<Level>.snd` and `Fix.snd` are lists of 8-byte pairs:

| Type | What |
| --- | --- |
| u32 | event id (`(bank << 16) \| event index`) |
| u32 | address of a slot in the level's (or Fix's) memory image |

`0x44D2F0`, after `0x44D1E0` opens the file, reads the pairs, calls
`_SND_fn_pGetBinEvent(id)`, adjusts the slot address for the current block layout and writes the
event pointer into the slot. It runs for `Fix.snd` from `0x402960` and for each level's `.snd`
from the level loader.

Across the whole data set, every one of `Fix.snd`'s 281 slot addresses falls inside a block Fix
announces, and all 11880 level slot addresses fall inside the level's **own** data blocks. So the
adjustment is a per-block delta, and a stored address already points into the block the slot lives
in.

The slots are ordinary fields in the level and Fix data: script node parameters (block `0d/01`),
animation descriptor ids (block `04/00`), and sound fields in object data (block `04/01`). Their
on-disk values are the sound editor's 1999 addresses and must not be trusted; `.snd` overwrites them.

A cross-check on Learn_10/Fix: the descriptor at `04/00 +0xF660` has kind 4 and id `0x066C05F0` at
+0xF664. `Fix.snd` maps slot `0x001B1864` (= Fix base `0x001A2200` + `0xF664`) to id `0x2D` = bank 0,
event 45; bank 0 event 45 is type 1 → resource 39 `Swhish5.wav`. The stored pointer `0x066C05F0` is
`0x066C0050 + 45·0x20`, the sound editor's event table. Those build-time tables start at
`0x066A0050` and `0x066C0050` in the data, both 143 entries (bank 0's whole event array), the two
bases 0x20000 apart.

### Playing one

`0x452D50` takes a play request. It switches on the event type (the table above), and for the
playing kinds resolves the resource and hands it to `0x457E60`, which switches on the *resource*
type:

| Resource type | Player |
| --- | --- |
| 1 | sample voice (0x454F40, the WAV module) |
| 2 | MIDI (0x455580) |
| 3 | CD audio (0x455940) |
| 4 | group (0x45AD30) |
| 6 | 0x457BF0 |
| 8 | variant set (0x45A610) |
| 10 | layered (0x45A1E0) |
| 11 | **theme** (0x459950) |

The scripts reach this through procedures **115, 117, 118 and 119** (`0x4691E0`, switching on
node ids 0x73/0x75/0x76/0x77). Each evaluates its script argument into a sound id and calls
`0x452D20` with a different sound-channel object (`DAT_004FF7D0`, `DAT_004FF788`,
`DAT_004FF78C`), which is why the same id is 3D-positioned or not depending on which procedure was
used.

## Which banks a level loads

A level's DSB section 0x5C holds the bank ids: entry 0x5D is a counted list, 0x5E a single id.
`0x4507B0` reads them and calls `_SND_fn_bInitBank@4` for each, then `_SND_fn_bEndLoadBanks@0`.
`Snd_Cpa.ini`'s `FORCE_LOAD_BANKS=ON` is the switch behind `_SND_fn_bStartLoadBanks2@4`, the 0x5D
bulk path. `Game.dsb` loads banks 3 and 9 (pinned by `tests/format_tests.cc`); `Learn_10.dsb` lists
72, starting `0, 3, 4, 9, 10, 11, …`. Every id in the `.snd`, animation and script data refers to
these loaded banks.

## Music

The 1284 loose `.apm` files are the pieces: IMA ADPCM, 4 bits per sample, typically 2–3 s, format in
[FORMATS.md](../FORMATS.md#apm-music). The banks only ever reference them by name.

The sequencing is in the banks, not in a separate file: a type-11 theme is the ordered segment list
above. The DSB sections 0x5A, 0x5E and 0x6E have nothing to do with it.

Transitions and mixing go through the APM mixer server DLLs (`APMmxSVR.dll` / `APMP5SVR.dll`, chosen
by `Snd_Cpa.ini`'s `[DLL_Adpcm]` section); the in-process side is the `_SND_fn_lPlayTransitionSxd` /
`bSetNextTransitionSxd` / `vPauseTransitionSxd` family, reached from the theme player.

What is still open: how the runtime decides *when* to move between segments (`SXD_DIST_SATUR=8` and
`SXD_DIST_FADE=30` in `Snd_Cpa.ini` suggest a distance-based mix) and what the per-segment words
mean.

## Checks

A parser built from the above passes all 405 banks:

- header `[0]=0`, `[1]=0x2C`, `[3]=0x2C+events·0x20` and `[10]=file size`, with the five region
  offsets ascending and inside the file;
- only event types 1 and 2, and only resource types 1, 4, 8, 10 and 11;
- every embedded type-1 payload fits the PCM region, its offset is 4-byte aligned, and the payloads
  tile the region with no gaps;
- every group's size, offset and count fit the extra area, and each of the nine themes with a tail
  (+0x0C exceeding `count·0x18` by exactly 0x0C) resolves its +0x28 word to a real `.apm` resource;
- every external type-1 name exists as a loose file with the size the bank records.

Totals: 1012 events, 2055 resources — 1830 type-1 (740 PCM + 1090 APM), 78 groups, 147 themes across
85 banks with 2093 segments — 225 unnamed resources and 1764 distinct names.

## Where this came from

- Own analysis: the whole bank layout, the event and resource records, the four regions, the PCM
  tiling and the external-file sizes, all checked against the 405 files rather than a sample.
- Own analysis of `Rayman2.exe` (Ghidra decompilation and capstone, notes under `H:\r2export\re\`):
  `0x453990` (loading), `0x453BF0` (id fix-up and the region choice per type),
  `0x453F50`/`0x453F90` (the two lookups), `0x453870` (the `bnk_%d.BNM` name),
  `0x452D50` and `0x457E60` (the play dispatch and the type-2 stop), `0x4527D0` (pitch
  variation), `0x459840`/`0x459950` (theme load and play), `0x44D2F0` and
  `0x44D1E0` (the `.snd` fix-up).
- Own analysis of `DLL/Snd_Cpa.ini`: the module names behind the four regions and the
  `SXD_DIST_*`/`FORCE_LOAD_BANKS` options.
- Not decoded yet: the group selection rules (types 4, 8, 10), the per-segment parameter words of a
  theme beyond the id, and the meaning of the resource flags +0x18/+0x1C/+0x20/+0x24 when they are
  not counts and of +0x30. Event types 3, 4, 5, 9, 11 and resource types 2, 3, 5, 6 are handled by
  the exe but absent from the shipped banks, so their layouts are known only from the handlers.

What was wrong in the earlier notes (`H:\r2export\sound-report.md`), for anyone who still has them
open:

- the header is 0x2C bytes, not 0x40; the words it called header fields at 0x2C/0x30 are event 0's
  first two words;
- there is no variable pre-name metadata block, and the "outlier name gaps" in `Bnk_9` are simply
  unnamed resource records at the same 0x5C stride;
- `.snd` entries are `(event id, slot address)`, not `(size, pointer)`;
- the ids name **events**, not resources;
- `.apm` music is not embedded in the banks — the `.apm` names are external files;
- event type 2 stops rather than plays;
- resource type 11 is the music theme list.

Implemented in `libopenrayman` as `bnm_bank` (`include/openrayman/bnm_bank.h`, `src/bnm_bank.cc`),
which reads a bank's header, events, resources, PCM payloads, group and theme entries, including the
theme tail. `game_data::sound_bank(id)` finds `Bnk_<id>.bnm`. The data checks above live in
`tests/bnm_tests.cc`; `ortool bank` lists a bank and exports its samples, and `ortool sound` resolves
an id. Not implemented: a `.snd` reader, and any runtime that plays a queued sound.
