# Saves

What a Rayman 2 save holds and how the game puts it back, as far as we know (2026-11-02). Reading
real saves is Phase 4; until then the revisit start (without `--start new`) applies the story bits
and a finished-level snapshot of its own (see the end).

## Two files

The game keeps a **general** save (`General.sav`) and one **level** save per level and slot (e.g.
`Learn_10_0.sav`). While a level loads, 0x403730 reads the level file and the general file
separately (0x410450, 0x410410), and 0x449E80 applies what they hold to the entries registered
for them. Both files are compressed; 0x48D890 expands them (the installed `General.sav` files
expand to 196 bytes each, the three installed `Learn_10_0.sav` files to one byte each).

## What gets registered

A save does not store the world. It stores a list of registered values, in registration order:

- **Script variables with a save flag.** 0x466BD0 walks each AI model's variable declarations and
  registers those whose save flags (declaration +0x08) are set. The registry (0x44A240) keeps the
  address, type and flags; 0x44A650 writes the entries, 0x44AC10 / 0x44A500 read and apply them.
  Flag 0x80 means the general save. In Learn_10 only Fix actor 6 has such variables: 6, 20, 42,
  45, 46, 49, 50 and 67. Variable 42 is the story-bit array (functions 248/249, procedure 351;
  [scripts.md](scripts.md) lists the bits). No scene actor's variable carries 0x100 or 0x200.
- **The out-of-world bit of type-5 actors.** 0x4135C0 registers standard game +0x29 bit 0 (mask 1,
  turned into bit index 0 by 0x44A630) of every placed actor of type 5 under flag 0x200, the level
  save; 0x44ACD0 packs these bits into bytes and 0x44AD20 unpacks them. Learn_10 has one type-5
  actor, node 71 (the tutorial's Rayman double), and the installed level saves' one byte is 0x01:
  node 71 is out of the world, as procedure 139 (0x406220) leaves it.

## The HUD's counters

Node 44 shows Fix actor 6's variables: the cages as vars 4 / 5 (freed / in the level), the Lums as
var 44 − var 43 out of var 44. A new game starts Learn_10 with 2/0 cages and 5 − 5 Lums; the
revisit start, through the story bits the level's actors check at startup, gives 2/2 and 5/5.

## Which actor checks which bit (Learn_10)

The five placed Lums are nodes 12, 11, 13, 70 and 36, for bits 1396..1400; each removes itself at
startup when its bit is set (node 70, the caged Lum, is frozen by the tutorial controller node 69
before it can). The kids' director, node 3, starts its scene unless bit 1092 and its own var115
are both set. Node 62 removes itself for bit 1133. The last cage (node 53) checks its cage bit and
skips the Lums it has already given.

## The revisit snapshot (ours, interim)

Story bits alone don't give a finished level: some actors only leave through their own scenes.
Until saves are read, `apply_revisit_state` (game/include/openrayman/game/first_visit.h) takes
nodes 3, 11, 12, 13, 36, 53, 56..61, 70 and 71 out of the world (bit 0 set; type 5 also loses bit
1), which matches the finished level the user sees in the game. It is a stand-in for the requested
end state, not a decoded save.

**Where this came from:** own analysis of the GOG `Rayman2.exe` (the addresses above), the Fix and
Learn_10 data and the three installed save folders, by GPT-6.1 Terra (Codex), reviewed by us;
`ortool run` with `ORTOOL_IDS=1` / `ORTOOL_VARS=6:4,5,43,44` shows the counters and removals.
