# Jail_20 hand-over: Globox, dialogue and settling

## Provenance (2026-11-07/08)

Own reading of the installed GOG Rayman2.exe, its local Ghidra export and raw
Capstone instructions; the user's recordings rec_jail20, rec_jail20_2 and
rec_jail20_3 dated 2026-11-07; installed level/animation/mesh data inspected
with ortool; OpenRayman script/transform traces and GUI captures. Findings
are described in our own words; no executable code or game assets are included.

## Which Globox remains visible

The two actors with AI model 0d/01 +0x15288 (2 behaviours / 16 variables),
nodes 13 and 14, are not the only actors whose geometry depicts Globox.
**Node 7**, initially (733.035,62.085,455.529), uses a different AI model,
0d/01 +0x1540 (3 behaviours / 12 variables). Its 103-entry object list at
04/01 +0x2480 contains Globox's body, feet and hands, confirmed by their
`globo_peau_txy`, `globo_ventre3`, `globo_bouche` and `globo_pied_nz` materials.
Animation 308 (state 04/01 +0x1900) and animation 342 (states +0x23D0/+0x2450)
pose eleven Globox channels. Combined scene animations can depict more than
one character; AI model counts do not establish visual identity.

The recording's **actor 5** is node 7, super object 0x21C8C840. It reaches
(709.071,130.369,455.705) and remains active after hand-over, cycling
+0x1900 -> +0x23D0 -> +0x2420 -> +0x2450 -> +0x1900. Its gameplay behavior 2
also creates dialogue ID 504 and the Globox portrait. It is drawn by the
ordinary sector actor pass. Node 14 is correctly switched off at hand-over;
there is no need for a new draw pass or a flag override to keep it visible.
The prior identification of node 7 as merely a pirate carrying Globox was
misleading. Counting only the two 2/16 AI-model entries missed this actor.
The 4/3 entries in order.txt are other AI models, not these Globox actors.

## Why our visible Globox collapsed

The combined Globox actor passed the visibility checks and produced eleven
posed parts, but its world matrix had zero axes. Transform tracing locates
the first collapse at its scripted teleport near tick 695. Procedure 200
(0x46BF20 -> 0x432DA0) changes the dynamics matrices' positions, then copies
the next dynamics matrix at base +0xD0 over the super object's world matrix.
The original mechanics maintains those full matrices; OpenRayman's mechanics
uses the super-object matrix directly and had left stale/zero saved axes in
+0xD0. Copying those axes collapsed the combined actor. During the scene,
node 14 still supplied visible geometry, concealing the problem until it
switched off. The same stale copy could flatten Rayman at hand-over.

Set `ORTOOL_START=new` and `ORTOOL_TRANSFORMS=1` when running
`ortool run "F:/Games/Rayman 2" Jail_20 5750 7` to include matrix axes and
scale in the position trace; `ORTOOL_EVERY` controls the sampling interval.

OpenRayman now preserves the authoritative current actor frame while
teleporting and synchronizes its complete frame into dynamics +0x78/+0xD0
(and +0x288/+0x2E0 for the extended block). Position changes and velocity
reset remain immediate. A unit regression starts with empty dynamics caches
and a rotated, nonuniformly scaled actor; a real-data regression checks
Globox and Rayman retain unit scale after the scene and that Globox has
11 drawable parts. No level-specific visibility exception is used.

## Switching off is distinct from removing an actor

0x4061B0 (procedure 85) releases animation links through 0x40F840 and stops
sounds through 0x453040. For an in-world actor with standard +0x29 bit 0x2,
it clears bits 0..2. It does **not** relink the super object to the inactive
world. The previous remove(false) and run_order erasure were incorrect.
Procedure 84 (0x405FA0) does relink an actor under the inactive root and sets
activity bits 0x2 and 0x4. The regression for the actor's scripted walk-in
remains in place.

rec_jail20_3 confirms node 14 (actor 12, 0x21C92210) changes 0x2E to 0x28 at
92.094 s, staying under 0x21C401B0 until spatial sleep at 104.609 s reparents
it to 0x21C40258. Retained membership does not itself enable thinking/drawing.

## Draw flags and the paths that were checked

The sector pipeline 0x44B2E0 calls 0x44B1B0 for the camera sector and its
associated sectors, submitting sector-list actors to 0x45C450. Raw instructions
at 0x45C466..0x45C475 confirm its standard +0x29 mask 0x4 gate. The later
parent-actor branch applies parent transform/render state when standard
+0x28 marks a parent actor; it does not bypass the child's activity gate.

Standard +0x29 bit **0x20 is the last draw/culling result**. 0x45C450 sets
it for accepted drawing and clears it for rejection, alongside custom
+0x24 bit 0x800. Its early activity return precedes those writes, so a
switched-off actor can retain 0x20 without drawing. Standard bit **0x8 is
initialization bookkeeping**: 0x405E50 mode 1 sets it, and 0x4132A0 tests it
before saving initial standard values via 0x4135A0. Neither bypasses visibility.

Other examined paths:

- 0x44AF30 -> 0x45C210 traverses static sector children, not the active world.
- 0x419A30 -> 0x45CC80 -> 0x45CA30 draws registered screen objects without
  the world activity gate. Registration at 0x45B9B0 sets super-object +0x30
  mask 0x21; node 14's recorded flags are zero after hand-over.
- 0x45C450 calls 0x45C360 on non-actor children only after passing the actor
  gate. Children cannot evade that early return.

## Dialogue padding

ID 504 is left aligned: /o400:/D0:Well, go ahead... use/L:your magic fist!
It has no /c command. In rec_jail20_2, the slot made by actor 5 at 92.203 s
has local position approximately (-8.4375,7.78048,-5.55129), consistent with
anchor (140,850,16). OpenRayman uses the same script anchor.

At 1280x720, the old renderer mapped generated text through a centered 4:3
area while its box and portrait covered the whole viewport. This added about
114 window pixels to this text's left edge. The original text paths share
the viewport (0x419A30); boxes and portraits use viewport coordinates too.
Glyph text and slot text now both cover it. This preserves the intro
narration/counter relationship and restores padding relative to dialogue
boxes. At 4:3 this change is an identity. The deliberately resized title
picture is independent. The /c branch was not the cause of this dialogue bug.

## Rayman's second settling movement

The baseline trace starts at Z 455.5384, reaches 455.5299 on its first gravity
step, then freezes until hand-over. Afterwards it settles to 455.4879. The
recording instead settles to 455.482 at game clock 81 ms and stays there.
Procedure 159 (0x467CF0) resets eight camera tracking points and weights;
it does not move Rayman or reset his dynamics.

Our collision approximation's point cast could not reach the nearby floor
on the single initial mechanics step before scripts froze it. It also used
the highest rim sample as the height on a slope. An external placement
(dynamics end mask 0x80) now permits the existing nearby-support distance
when descending, and the selected support plane is evaluated at the actor
origin. This improves our documented disc/cast approximation; it does not
claim to implement the original sphere solver (0x43EEC0 / 0x43F510). The
trace settles to 455.4816 before freezing and remains there at hand-over.
No level-specific position or timing is inserted.

## Update order at the slide hand-over

Live order capture (huntyh, 2026-11-13) identifies the director as node 3,
one of the moving actors at the head of the dynamic world, with Rayman later
in the list when the stand-in (node 13) is placed. The later movement of node
11 and the stand-in ahead of Rayman occurs several hundred frames after that
hand-over. Thus that later reorder cannot explain the earlier condition-56
wait. The recording of a separate run shows Rayman hidden on the stand-in's
first visible frame and its landing state on the next; why the condition has
the observed result remains open.

Where this came from: our analysis of the read-only live order capture and
`run4/actors.txt`, checked against the scene nodes and the executable. The
frame driver runs the moving pass (0x40AA10) before the world walk (0x40A7C0);
condition 56 (0x48AE60) compares the target's update stamp with the frame
counter. Sector change handling (0x40B7B0, 0x40B290, 0x40B4C0) unlinks and
wakes world actors; procedure 84 (0x405FA0) links an actor from the inactive
world. These observations do not establish the missing timing mechanism.
