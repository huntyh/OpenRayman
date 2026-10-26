# File formats

What OpenRayman knows about each Rayman 2 (PC, GOG release) file format, and where that knowledge
came from. Update this whenever a format is added or corrected.

**Sources** used below:

- **Own analysis**: our own experiments on the game files (hex dumps, statistics, trial decoding),
  or our own study of how `Rayman2.exe` reads a file. In both cases we write the implementation
  ourselves from that understanding; no code is copied.
- **Old OpenRayman**: the 2016 code by imaginaryPineapple.
- **Rayman2Lib**: [szymski/Rayman2Lib](https://github.com/szymski/Rayman2Lib) (MIT).
- **Ray2Get**: [Synthesis/ray2get](https://github.com/Synthesis/ray2get) and its APM description.
- **Raymap**: [byvar/raymap](https://github.com/byvar/raymap), used as a reference only (it has no
  license, so no code may be taken from it).

## Summary

| Format | Files | Status | Implementation |
| --- | --- | --- | --- |
| CNT archive | `Textures.cnt`, `Vignette.cnt` | Complete | `cnt_archive.cc` |
| GF texture | inside CNT | Complete for the formats in use (888, 8888) | `gf_texture.cc` |
| APM music | `World/Sound/*.apm` | Complete, see open question | `apm_audio.cc` |
| DSB script | `Game.dsb`, `<Level>.dsb` | All sections in the game parsed; some meanings unknown | `dsb_script.cc` |
| SNA memory blocks | `Fix.sna`, `<Level>.sna` | Block layout complete; contents not yet interpreted | `sna_file.cc`, `memory_image.cc` |
| Relocation tables | `Fix.rtb/.rtp/.rtt/.rts` | Complete | `relocation.cc` |
| LEVELS0.DAT | `World/Levels/LEVELS0.DAT` | Complete | `relocation.cc` |
| GPT | `Fix.gpt`, `<Level>.gpt` | Level header (Fix actors, world roots) and the animation bank; some header fields unknown | `pointer_stream.cc`, `level_header.cc` |
| Super objects | inside SNA | Tree links complete | `scene.cc` |
| Actors, animations | inside SNA, GPT | Object lists, animation bank, posing a frame; interpolation, events and morphs not yet | `actor.cc`, `animation.cc` |
| Static geometry | inside SNA | Meshes, triangles, UVs, materials; sprites (type 3) and collision not yet | `geometry.cc` |
| PTX | `Fix.ptx`, `<Level>.ptx` | Texture slots and names; trailing values unknown | `texture_table.cc` |
| Scripts (AI) | inside SNA | Brains, models, behaviours, reflexes, macros, variables, script nodes; keywords and operators named | `brain.cc`, `script_text.cc` |
| BNM sound banks | `World/Sound/*.bnm` | Not started | |
| SND | `<Level>.snd` | Not started | |

## CNT texture archives

u32 directory count, u32 file count, u8 "names XORed" flag, u8 checksum flag, u8 name XOR key; then
length-prefixed directory names, a checksum byte, and per file: directory index, name, a 4 byte
content XOR key, checksum, offset and size. File contents are XORed with the 4 byte key, except the
last `size % 4` bytes, which are stored as is.

- Sources: old OpenRayman (header and names); the unencoded tail: Raymap; confirmed by own analysis
  (all 1,671 textures decode).

## GF textures

u32 format (888 = BGR, 8888 = BGRA, the values are decimal), u32 width, u32 height, u8 channels,
u8 run-length marker byte; then each channel as a separate run-length encoded plane (B, G, R, A),
rows stored bottom to top. A marker byte is followed by (value, count).

- Sources: old OpenRayman; confirmed by own analysis (decoded loading screens checked by eye).

## APM music

A WAVEFORMATEX header (format tag `0x2000`), `vs12` at `0x14`, the right channel's initial predictor
and step index at `0x2C`, the left channel's at `0x38`, `DATA` at `0x60`, nibbles from `0x64`.
Standard IMA ADPCM, 4 bits per sample. Stereo data is byte pairs of (left, right), high nibble
first; mono files (only `outro1.apm`) are one nibble per sample.

- Sources: Ray2Get and old OpenRayman (header fields, IMA ADPCM).
- Own analysis: nibble order (high first, by measuring smoothness), and the channel pairing. The old
  decoder paired the first byte with the `0x2C` state, which made 54 tracks clip on one side; the
  correct pairing removes all of it.
- Open question: Ray2Get's README says the game's own decoder DLL clears the 3 lowest bits of `step`
  for every sample, which builds up a DC offset. We decode standard IMA ADPCM, which may sound slightly
  different from the game. To compare by ear later.
- Music is adaptive: most files are 2–3 second pieces the game sequences at runtime. Ray2Get ships a
  list of the sequences; where the game stores them is not yet known (probably the BNM banks).

## DSB scripts

The whole file after the first 4 bytes is XOR masked; the first 4 bytes are the initial mask (see
the mask under [LEVELS0.DAT](formats/levels0-dat.md#table-data)). Decoded, it's a list of sections:
u32 section id, entries (u32 id + arguments), u32 `0xFFFF`. Strings are u16 length + characters.
Known sections: memory pools (`0x00`), level list (`0x1E`), data directories (`0x28`), texture archives
(`0x40`), loading screens (`0x46`), options (`0x64`), sound banks (`0x5C`), and `0x20`, `0x5A`, `0x6E`
whose meaning is unknown.

- Sources: old OpenRayman (mask algorithm, section structure, several sections); own analysis
  (loading screen colours are floats, the option entry has three strings, sections `0x5A`/`0x5C` in
  level DSBs).

## SNA memory blocks, relocation tables

See [formats/sna.md](formats/sna.md). Memory dumps cut into blocks, each with its original address;
relocation tables list every pointer slot.

- Sources: Raymap (general model of blocks and relocation tables); own analysis (exact Rayman 2 PC
  layout, runtime-only regions, and that each file's pointers use the addresses that file announced,
  which differ by 8 bytes for some Fix blocks).

## LEVELS0.DAT

See [formats/levels0-dat.md](formats/levels0-dat.md). The per-level relocation tables, encrypted and
hidden at computed positions in 150 MB of random filler.

- Sources: own analysis, from studying how `Rayman2.exe` locates and decodes the tables. Verified on
  all 70 levels.

## GPT, PTX and super objects

See [formats/gpt.md](formats/gpt.md). Streams whose relocation tables list pointer values in file
order; the level header with the world roots and how Fix's actors join a level; the scene tree;
texture lists.

- Sources: own analysis (stream model, header, Fix actor placements, PTX, texture descriptions);
  super object layout and world root names from Raymap (reference only), every field confirmed by own
  analysis across all 70 levels.

## Static geometry

See [formats/geometry.md](formats/geometry.md). The chain from sectors down to triangles, UVs and
materials.

- Sources: structure chain from Raymap (reference only); Rayman 2 PC offsets and sizes from own
  analysis, verified over all 70 levels and by eye on Learn_10.

## Lighting and fog

See [formats/lighting.md](formats/lighting.md). Each sector's static lights (ambient, parallel,
spherical, fog), its box and virtual flag, and how lights combine with material colours.

- Sources: own analysis (offsets, list layout, box, virtual flag, counts over all 70 levels); field
  names and the way lights combine with materials from Raymap (reference only); the overall
  brightness by eye against the real game.

## Scripts

See [formats/scripts.md](formats/scripts.md). Each actor's brain leads to an AI model shared by
its kind: behaviours (rules every frame, plus a schedule that resumes where it stopped), reflexes,
macros and designer variables. Script nodes are 8 bytes (parameter, cache, indent, type), run by
the exe's tree-walking interpreter through 41 node types.

- Sources: own analysis of `Rayman2.exe` and the data of all 70 levels; Raymap for orientation only.

## Actors and animations

See [formats/animation.md](formats/animation.md). Actors' object lists, and the animation bank shared
by Fix and the level, which turned out to be the large pointer-free part of each level GPT.

- Sources: own analysis (where the bank lives, the Fix/level split, alignment, actor offsets);
  animation structures and posing from Raymap (reference only), checked across all 70 levels and by
  eye on Learn_10.
