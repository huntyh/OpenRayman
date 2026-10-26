# SNA files and relocation (Rayman 2 PC)

A level is loaded from `Fix.sna` (data shared by every level) plus `<Level>/<Level>.sna`. Each SNA is
a dump of the original game's memory, cut into blocks, with absolute 1999 memory addresses as
pointers. Rebuilding that memory image is what `memory_image` in libopenrayman does.

## File layout

The whole file is XOR masked like DSB files: the first 4 bytes are the initial mask, see
[levels0-dat.md](levels0-dat.md#table-data) for the algorithm. After unmasking, a list of entries:

| Field | Type | Notes |
| --- | --- | --- |
| module | u8 | engine module owning the block |
| id | u8 | Fix uses 0 (one block uses 1), levels use 1 or 2 |
| unknown | u8 | `0xFF` for runtime-only regions, else 1 (Fix) or 2 (level) |
| base | i32 | address of the block in the game's memory; -1 ends the entry here |
| reserved end | u32 | end of the memory reserved for the block |
| unknown | u32 | |
| unknown | u32 | |
| size | u32 | bytes of data that follow (0 for announcements) |

Every file first **announces** the full memory map with data-less entries, then repeats the
entries that carry data. Blocks `0a/00` and `11/00` never carry data: the game allocates them at
runtime, but pointers in the files still target them.

## Relocation tables

`Fix.rtb` (and, for levels, the tables in LEVELS0.DAT) list every pointer slot: its address and the
block it points into. Values between the target's base and its reserved end are valid; about 2% of
pointers target runtime-only memory.

## Each file has its own view of the memory map

A level announces Fix's blocks too, and for some of them (`04/00`, `06/00`, `0e/00`) at an address
**8 bytes higher** than `Fix.sna` itself, and `11/00` at a different address entirely. Pointers
written by a file must be resolved with the addresses *that file* announced. Checked on Learn_10:
with the level's view, all 746 pointers into `04/00` land on the start of a structure Fix itself
points to; with Fix's own base, none do.
