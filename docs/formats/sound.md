# Sound: BNM banks, SND placement files and the script/animation sound ids

This covers the in-game sound effects (`.bnm` banks, `.snd` level files) and how the scripts,
animations and level data name a sound. It also covers the adaptive music: the banks hold the
**themes** — ordered lists of `.apm` pieces — that the game sequences at runtime. The APM stream
format itself is in [FORMATS.md](../FORMATS.md#apm-music).

Everything below is own analysis of the GOG game data (`Data/World/Sound/*.bnm`,
`Data/World/Levels/**/*.snd`) and of `Rayman2.exe` (Ghidra decompilation and capstone, notes under
`H:\r2export\re\`). No code was taken from Rayman2Lib, Raymap or Ray2Get. The function addresses
are the exe's.

## Short version

* A **BNM bank** is `header (0x2C bytes) + events (0x20 each) + resources (0x5C each) +
  resource data + sample data`. Events and resources are addressed by a 32-bit id:
  `(bank << 16) | index`.
* An **event** says what to do, and is what the game data names. Shipped banks use two kinds:
  type 1 **plays** the resource at `+8`, type 2 **stops** the voices a given event started.
* A **resource** is one sound. Type 1 is a sample (16-bit PCM inside the bank, or an external
  `.apm` file). Types 4, 8 and 10 group or layer samples. **Type 11 is a music theme**: an ordered
  list of external `.apm` segments.
* A **`.snd` file** is a list of `(u32 event id, u32 slot address)` pairs. At load the engine
  resolves the id with `_SND_fn_pGetBinEvent` and writes the resulting event pointer into the
  slot. This is what makes the hard-coded sound pointers in the level and Fix data work: the
  stored pointers are only the sound editor's addresses, and `.snd` replaces them with the
  pointers for the current load.
* Both **animation event type 0** and **script node type 30** (sound event) store such a slot:
  the animation descriptor's id field and the script node's parameter. After the `.snd` pass they
  hold an event pointer.

## BNM bank file

405 banks, `Bnk_0.bnm` … `Bnk_404.bnm`, 76 bytes … 6.6 MB, 38.2 MB total. The bank id is the file
number; the loader builds the name with `"bnk_%d.BNM"` (`FUN_00453870`).

### Header (0x00–0x2B, 11 u32)

| Offset | Value in the shipped banks | Meaning |
| --- | --- | --- |
| `0x00` | 0 | unused |
| `0x04` | `0x2C` | header size, and the offset of the event array (all 405 banks) |
| `0x08` | n | **event count** |
| `0x0C` | `0x2C + n·0x20` | offset of the resource array (holds in all 405) |
| `0x10` | m | **resource count** |
| `0x14` | a | start of data region 1 (MPEG) |
| `0x18` | b | start of data region 2 (MIDI) |
| `0x1C` | c | start of data region 3 (PCM samples) |
| `0x20` | d | start of data region 4 (ADPCM) |
| `0x24` | e | end of data region 4 |
| `0x28` | file size | = the file's length in all 405 banks |

The five offsets are non-decreasing and are absolute file offsets. Data regions are
`[a,b)`, `[b,c)`, `[c,d)`, `[d,e)`. In every shipped bank `a == b == c` and `d == e`, so only
region 3 (PCM) carries data: 269 banks have a PCM region, 136 have none (they reference only
external files and groups). The four regions belong to four decoder modules registered by the SND
engine:

| Region | Module | Test at load (decompiled) |
| --- | --- | --- |
| `+0x2C` | MPEG | `_SND_fn_bCanFreeDataMPEG@0` |
| `+0x30` | MIDI | `_SND_fn_bCanFreeDataMidi@0` |
| `+0x34` | PCM sample | `_SND_fn_bCanFreeDataSample@0` |
| `+0x38` | ADPCM | `_SND_fn_bCanFreeDataADPCM@0` |

`Snd_Cpa.ini` in the game's `DLL` folder names the modules behind them: `WAVMW`/`WAVPW` (MMX or
plain WAV) for samples and `APMMX`/`APMPW` for ADPCM.

`FUN_00453990` reads `[0x2C, a)` into the bank image and each region either in place or into a
separate allocation, depending on the module's "can free data" callback. `FUN_00453BF0` then walks
the events and resources and fixes their ids up to pointers. `_SND_fn_pGetBinEvent@4`
(`0x453F50`) and `_SND_fn_pGetBinRes@4` (`0x453F90`) do the id → record lookup:
`bank = id >> 16`, `index = id & 0xFFFF`, event stride `0x20`, resource stride `0x5C`.

### Event records (0x20 each, starting at 0x2C)

1012 events in the shipped banks: 875 of type 1, 137 of type 2.

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | editor id (unique per record; used by `_SND_fn_czGetResNameFromEdId@4`) |
| `+0x04` | u32 | **type** (see below) |
| `+0x08` | u32 | target: a resource id for types 1 and 6..10, an event id for types 2 and 4 (`0xFFFFFFFF` = none) |
| `+0x0C` | u32 | type 1: random **pitch variation** in 16.16 (0 = exact); types 4/5: the second event |
| `+0x10` | u32 | 0 |
| `+0x14` | u32 | types 6..8: pitch variation in 16.16 |
| `+0x18` | u32 | flag, 0 or 1 (not decoded) |
| `+0x1C` | u32 | flag, 0 or 1 (not decoded) |

Event types, from the play switch `FUN_00452D50` (`0x452D50`, jump table at `0x45300C`):

| Type | Action | Records shipped |
| --- | --- | --- |
| 1, 6, 7, 8, 10 | play the resource at `+8` | 875 (all type 1) |
| 2 | **stop** the voices a given event started (`FUN_00451E20`) | 137 |
| 3 | stop every voice of a channel (`FUN_00451DC0`) | – |
| 4 | play the event at `+8`, then the one at `+0xC` | – |
| 5 | rejected with an error | – |
| 9 | theme/music control (`FUN_004581E0`) | – |
| 11 | channel volume: push a volume onto the channel's list (`FUN_00451880`) | – |

So an event type 2 means "stop that sound": `FUN_00451E20` matches the running voices on that
channel whose owning event pointer is the one passed and stops them. 1472 of the 11880 level
`.snd` pairs are stops, which is how ambient loops get switched off.

The pitch variation is applied in `FUN_004527D0`: it returns `0x10000` when the field is 0,
otherwise `0x10000 - v + rand()·2v`, so `0x3333` gives ±20%. Observed non-zero values are
`0x51D`, `0xA3B`, `0xCCC`, `0xF5C`, `0x147A`, `0x1999`, `0x3333`.

`FUN_00453DB0` resolves `+8` with `pGetBinRes` for types 1, 6, 7, 8 and 10, with `pGetBinEvent` for
type 2, and with both for types 4 and 5. `_SND_fn_iGetNumberOfEventInBank` returns the event count.

### Resource records (0x5C each, at header[3])

2055 resources in the shipped banks. The 16-byte NUL-padded ASCII name is at `+0x48`; a record
with a zero name is a valid resource that is addressed by index only (225 such records; every
group and theme is unnamed).

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | editor id (unique; `czGetResNameFromEdId` looks a name up by it) |
| `+0x04` | u32 | **resource type** (1, 4, 8, 10, 11 in the shipped banks) |
| `+0x08` | u32 | 1 = data embedded in the bank, 0 = external file (observed) |
| `+0x0C` | u32 | data size in bytes |
| `+0x10` | u32 | data offset in the resource's data area (4-byte aligned) |
| `+0x14` | u32 | static volume 0…127 (127 in most records) |
| `+0x18` | u32 | type 10: entry count; otherwise a flag |
| `+0x1C` | u32 | type 11: start segment; type 10: gain; otherwise a flag |
| `+0x20` | u32 | type 8: entry count; otherwise a flag |
| `+0x24` | u32 | type 11: entry count; otherwise a flag |
| `+0x28` | u32 | type 4: entry count; type 11: offset of one extra entry, or `0xFFFFFFFF` |
| `+0x2C` | u32 | 1 = external file (no data region), 0 = embedded data |
| `+0x30` | u32 | flag 0/1 (not decoded) |
| `+0x34` | u32 | 0 |
| `+0x38` | u32 | size again for an embedded type 1, 0 for an external one |
| `+0x3C` | u32 | sample rate; 22050 in every shipped type-1 record |
| `+0x40` | u32 | format: `0x00010010` = 1 channel / 16-bit, `0x00020010` = 2 channels / 16-bit |
| `+0x44` | u32 | data format id: 1 = PCM, 2 = MPEG, 4 = external `.apm` |
| `+0x48` | 16 bytes | NUL-padded file name (`*.wav` for embedded, `*.apm` for external) |
| `+0x58` | u32 | 0 padding / next record |

`FUN_00453BF0` picks the data base for a record from its type, then calls `FUN_00457A10`:

| Type | Data base | Loader |
| --- | --- | --- |
| 1 | the region its sub-format names: `+0x34` (1 = sample), `+0x2C` (2 = MPEG), `+0x38` (4 = ADPCM); none when `+0x2C` = 1 | `0x455200` (WAV module), `0x4554E0` (MPEG), `0x4558A0` (CD) |
| 2 | MIDI region `+0x30` | `0x4554E0` |
| 3 | sample region `+0x34` | `0x4558A0` |
| 4, 6, 8, 10, 11 | the **extra area** `+0x28` | `0x45AB70`, `0x457D50`, `0x45A940`, `0x45A440`, `0x459840` |
| 5 | converted to type 8 and loaded as one (`FUN_0045A9A0`) | – |

### Type 1 samples (1830 records)

| Kind | Records | `+0x08` | `+0x2C` | `+0x44` | Data |
| --- | --- | --- | --- | --- | --- |
| Embedded PCM | 740 | 1 | 0 | 1 | region 3, offset `+0x10`, size `+0x0C` |
| External APM | 1090 | 0 | 1 | 4 | loose `World/Sound/<name>` file |

* Embedded data is **headerless signed 16-bit little-endian PCM**, mono, 22050 Hz
  (`0x00010010`). It starts directly with samples; there is no `RIFF`/`WAVEFORMATEX` header.
* The embedded records **tile the PCM region exactly**: sorted by offset, each entry ends where
  the next one begins after rounding up to 4 bytes, and the last one ends at the region's end.
  All 467 adjacent gaps and all 269 tails are zero, so nothing is padded or hidden. Records can
  still share one offset (the same sample used by several resources).
* External records name a loose `.apm` file. For all 1090 records the loose file exists and its
  size equals `+0x0C` exactly; there is no APM payload inside the banks (no `00 20 02 00 22 56`
  header anywhere in them). The `.apm` files are the IMA ADPCM streams described in
  [FORMATS.md](../FORMATS.md#apm-music).
* All 1830 type-1 records use rate 22050.

### Group resources (types 4, 8, 10)

Their data lives in the **extra area**: from the end of the resource table
(`header[3] + count·0x5C`) to the start of the PCM region (`header[7]`). `FUN_00453990` points the
bank's `+0x28` field at that area and the loaders treat it as the base for `+0x10`. The loaders
resolve the id word of each entry with `pGetBinRes`; the other words stay data.

| Type | Records | Entries | Stride | Count field | Id at | Other entry words |
| --- | --- | --- | --- | --- | --- | --- |
| 4 | 17 | 54 | `0x10` | `+0x28` | `+0xC` | 3 parameters, then the id |
| 8 | 10 | 126 | `0x08` | `+0x20` | `+0x00` | selector 0…16 |
| 10 | 51 | 154 | `0x0C` | `+0x18` | `+0x00` | gain, flag |

All but 16 entries point at a type-1 sample; five type-8 and nine type-4 entries point at a type 10,
and two type-10 entries at a type 11. The type-8 selector is matched against a value the play
routine derives from the resource, so a type 8 is a set of variants of one sound. The type-4 and
type-10 players (`FUN_0045AD30`, `FUN_0045A1E0`) scale the volume by the resource's `+0x1C` and
hand the group to the mixer. The exact selection rule is not fully decoded.

### Theme resources (type 11) — the adaptive music

147 themes, in 85 banks; 2093 segments in total. All are unnamed, and **every segment is an
external `.apm`** (2088 of them; five entries point at a type-10 group instead). This is where the
game's adaptive music lives: the theme is the ordered playlist, and the `.apm` files are the
2–3 second pieces.

* Entries: stride `0x18` (6 words), count at `+0x24`, base `+0x10` off the extra area. Entry word
  0 is the resource id of the segment; the other words are per-segment parameters (word 1 looks
  like a 0/1 "entry point" flag and word 3 a "loops" flag, not confirmed).
* `+0x1C` is the segment the theme starts on (0 in every shipped record).
* `+0x28` is either `0xFFFFFFFF` (= no tail) or an offset in the same extra-area block holding one
  more resource id — the last piece to play when the theme runs out. 9 themes have one;
  `FUN_00459840` loads it into `param_2[8]` and the segments into `param_2[0xB]`/`[0xC]`.
* Examples: `Bnk_106` theme 2 = 12 segments `PL301.apm` … `PL312.apm` with tail `PL3F.apm`;
  theme 16 = `PL501`…`PL504`; theme 21 = `PL505`…`PL514` followed by `PL501`…`PL504` (14 entries:
  the list may repeat a piece). `Bnk_0` theme 15 = `JINGLE8.apm`, `silence.apm`; theme 203 =
  `new01.apm`, `silence.apm`. The `silence.apm` segment is how a theme fades out.
* Playing: an event of resource type 11 goes through `FUN_00457E60` case 0xb → `FUN_00459950`
  (`0x459950`), which sets up the theme state at `DAT_004B95B8` and starts a segment through the
  APM decoder; `_SND_fn_lPlayTransitionSxd` / `_SND_fn_bSetNextTransitionSxd` change segment, and
  `dbgSND_fn_bGetAskedTheme` / `dbgSND_fn_bGetThemePosition` read that state.
* Event type 9 (`FUN_004581E0` → the module's stop) is the matching "stop the theme" request.
* **Who asks for a theme:** the scripts, through an ordinary sound id. 1425 of the 11880 level
  `.snd` pairs name an event that plays a type-11 theme, spread over 58 of the 70 levels; Fix
  adds two (`Fix.snd` ids `0x13`, `0x75`) for the `Bnk_0` jingles.

### Validation

A parser built from the above passes all 405 banks:

* `header[0]=0`, `header[1]=0x2C`, `header[3]=0x2C+events·0x20`, `header[10]=file size`;
* `header[5] <= header[6] <= header[7] <= header[8] <= header[9] <= file size`;
* event types 1/2 and resource types 1/4/8/10/11 only;
* every embedded type-1 payload fits the PCM region, offsets are 4-byte aligned, and the payloads
  tile the region with no gaps;
* every group `size`, offset and count fits the extra area, and each theme tail offset lands on a
  readable id (the nine themes whose `+0x0C` size exceeds `count·0x18` are exactly those with a
  tail);
* every external type-1 name exists as a loose file with the same size.

Totals: 1012 events, 2055 resources, 1830 type-1 (740 PCM + 1090 APM), 147 themes (85 banks,
2093 segments), 78 group resources, 225 unnamed resources, 1764 distinct names.

## Sound ids in scripts and animations

### Events are the public ids

The 32-bit id used everywhere in the data is `(bank << 16) | index` and it always names an
**event**, not a resource. `_SND_fn_pGetBinEvent` is the lookup; `pGetBinRes` is used only from
inside an event (and from group entries). A level's `.snd` file may therefore only contain event
ids.

### Animation events (type 0)

From `H:\r2export\re\handlers\events.md`: an animation event entry points to a descriptor;
descriptor `+0` is the "id", `+4` the type, `+6`/`+7` the gates. For type 0 `FUN_0041AFB0`
(`0x41AFB0`) calls `FUN_00452D20(actor, sound_channel, id, 0, 0)`; the id is the descriptor's
`+0` field, i.e. a pointer to an event record once the `.snd` pass has run.

### Script node type 30

The interpreter's sound-event node (type `0x1E` = 30) is `{u32 param, u16 skip, u8 indent,
u8 type}`. Its `param` is the same kind of slot. In Learn_10's script block `0d/01`,
`+0x8720` is `[0x066A02F0][0x00000000][0x07][0x1E]`: param = sound pointer, type 30.

### The `.snd` file fixes the pointers

`<Level>.snd` and `Fix.snd` are lists of 8-byte pairs:

| Type | Meaning |
| --- | --- |
| u32 | event id (`bank << 16 | event index`) |
| u32 | address of a slot in the level's (or Fix's) memory image |

`FUN_0044D2F0` (called after `FUN_0044D1E0` opens the file) reads the pairs, calls
`_SND_fn_pGetBinEvent(id)`, adjusts the slot address for the current block layout and writes the
event pointer into the slot. `FUN_0044D2F0` runs for `Fix.snd` in `FUN_00402960` and for each
level's `.snd` in the level loader.

Verified on the whole data set: every one of `Fix.snd`'s 281 slot addresses falls inside a block
Fix announces, and all 11880 level slot addresses fall inside the level's **own** data blocks — so
the adjustment in `FUN_0044D2F0` is a per-block delta, and a stored address already points into
the block the slot lives in.

The slots are ordinary fields in the level/Fix data: script node parameters (block `0d/01`),
animation descriptor ids (block `04/00`), and sound fields in object data (block `04/01`). Their
on-disk values are the sound editor's 1999 addresses (e.g. `0x066C05F0`); they are **overwritten**
at load and must not be trusted.

Cross-check on Learn_10 / Fix: the descriptor at `04/00 +0xF660` has kind 4, id
`0x066C05F0` at `+0xF664`. `Fix.snd` contains the slot `0x001B1864` (= Fix base `0x001A2200` +
`0xF664`) with id `0x2D` = bank 0, event 45. Bank 0 event 45 is type 1 → `Swhish5.wav`. The
stored pointer `0x066C05F0` equals `0x066C0050 + 45·0x20`, the build-time event table; the runtime
pointer is whatever address bank 0 has in this run.

For reference, the build-time event tables seen in the data start at `0x066A0050` and
`0x066C0050` (both 143 entries = bank 0's event array; the two bases differ by `0x20000`). This
explains the "sound definition pointers" in the earlier research report: they are bank 0 events.

### Playing an event

`FUN_00452D50` is the entry point for a play request. It switches on the event's type (see the
event table), and for the playing kinds resolves the resource and hands it to `FUN_00457E60`,
which switches on the *resource* type:

| Resource type | Player |
| --- | --- |
| 1 | sample voice (`0x454F40`, the WAV module) |
| 2 | MIDI (`0x455580`) |
| 3 | CD audio (`0x455940`) |
| 4 | group (`0x45AD30`) |
| 6 | `0x457BF0` |
| 8 | variant set (`0x45A610`) |
| 10 | layered (`0x45A1E0`) |
| 11 | **theme** (`0x459950`) |

The scripts reach this through procedures **115, 117, 118 and 119** (`FUN_004691E0`, switch on node
ids 0x73/0x75/0x76/0x77): each evaluates the script argument into a sound id and calls
`FUN_00452D20` with a different sound-channel object (`DAT_004FF7D0`, `DAT_004FF788`,
`DAT_004FF78C`), which is why the same sound id is 3D-positioned or not depending on which
procedure was used.

## Which banks a level loads

The level DSB's section `0x5C` holds the bank ids: entry `0x5D` is a counted list and `0x5E` is a
single id. `FUN_004507B0` reads those entries and calls `_SND_fn_bInitBank@4` for each, then
`_SND_fn_bEndLoadBanks@0`. `Game.dsb` loads banks 3 and 9
(already pinned by `tests/format_tests.cc`); `Learn_10.dsb` lists 72 banks, starting
`0, 3, 4, 9, 10, 11, …`. The ids in the `.snd`/animation/script data refer to those banks.

`Snd_Cpa.ini` also has `FORCE_LOAD_BANKS=ON`, the switch behind `_SND_fn_bStartLoadBanks2@4`
(the `0x5D` bulk path).

## Music (APM) in the sound context

* The 1284 loose `.apm` files are the music pieces: IMA ADPCM 4-bit, typically 2–3 s, format in
  [FORMATS.md](../FORMATS.md#apm-music). The banks reference them by name only.
* **The sequencing is in the banks**: a type-11 theme is the ordered segment list (see above), so
  "which piece follows which" is answered by the theme records, not by a separate file. The DSB
  sections `0x5A`/`0x5E`/`0x6E` are unrelated to it.
* Transitions and mixing run through the APM mixer server DLLs (`APMmxSVR.dll` / `APMP5SVR.dll`,
  chosen by `Snd_Cpa.ini`'s `[DLL_Adpcm]` section); the in-process side is the
  `_SND_fn_lPlayTransitionSxd` / `bSetNextTransitionSxd` / `vPauseTransitionSxd` family, reached
  from the theme player.
* Still open: how the runtime decides *when* to move between segments (`SXD_DIST_SATUR` and
  `SXD_DIST_FADE` in `Snd_Cpa.ini` suggest a distance-based mix), and what the per-segment
  parameter words mean.

## Provenance and open questions

* BNM header, event/resource records, type dispatch and the id lookups: own analysis of the 405
  bank files, confirmed against the exe (`FUN_00453990`, `FUN_00453BF0`, `FUN_00453F50`,
  `FUN_00453F90`, `FUN_00453870`, `FUN_00452D50`, `FUN_00457E60`).
* Event type 2 = stop: `FUN_00452D50` case 2 → `FUN_00451E20` → `FUN_00451D50`, which matches the
  running voices by channel and by their owning event pointer and terminates them. Cross-checked
  against the data: 1472 `.snd` pairs name stop events, all for looping ambience.
* Pitch variation at event `+0x0C`: `FUN_004527D0` (rand-based, `0x10000 ± v`).
* PCM payload is headerless 16-bit mono 22050 Hz, and the region tiles exactly: own analysis.
* External APM: own analysis (all 1090 names exist loose with identical sizes; no APM magic in any
  bank).
* Themes: own analysis of the entry tables, plus `FUN_00459840` (load) and `FUN_00459950` (play).
  That the theme player is the adaptive-music sequencer is inferred from the data (ordered
  external `.apm` lists, 1425 script-side references) and the `bGetThemePosition` debug getter.
* `.snd` pairs and the pGetBinEvent fix-up: own analysis of `FUN_0044D2F0` and `FUN_0044D1E0`,
  cross-checked with Fix.snd and Learn_10 data. The per-block delta table `DAT_005121E0` is built
  by `FUN_0044D4F0` from the `.sna`; the delta is zero for every shipped slot.
* Group selection rules (types 4/8/10) and the type-11 per-segment words: not decoded.
* Resource flags `+0x18`/`+0x1C`/`+0x20`/`+0x24` when they are not counts, and `+0x30`: observed
  0/1, meaning unknown.
* Event types 3, 4, 5, 9, 11 and resource types 2, 3, 5, 6: handled by the exe but absent from the
  shipped banks; the layouts are known only from the handlers.
