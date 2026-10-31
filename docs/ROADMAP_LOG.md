# Roadmap log

The dated investigation notes from `ROADMAP.md`, kept here so the roadmap stays short. Newest
first. The format docs under `formats/` are the permanent home of the findings themselves.

## 2026-10-31: the shot ⬅ (in progress)

Rayman's shot is a generated actor (`formats/generated.md`): function 66 copies its template,
it flies on the fast mechanics, fades after 1.5 s and frees its slot. In today: the shot
leaves Rayman's hand at the animation's marker (368ce8f), the rank countdown, the bounce
sparkles and the story bits for its look (27fdca6), the pool sweep (6706e5f), the fade
animation playing (097275e; the opacity byte is part of the animation object choice, 6865906)
and the shot's additive sprite taking no fog (3ba745c); the animated part pivot interpolation
itself is corrected in b62c5fc. The charged shot exists in the scripts (holding the key,
var19 up to 2000 ms, particles 69/71) but only once story bit 1143 is set, later in the game.
What is missing: hitting actors (procedure 22's touch query finds nobody yet) and homing
(function 113).

## 2026-10-30: the game camera, fourth look (user)

The waterfall is passable (virtual sectors have no collision), strafing goes in every
direction with no drift (each actor thinks then moves, 0x412230), and the hover camera works
like the game's, tilting down to show where he lands (function 310 returns the offset point).
Q/W turning fixed: the exe swings the camera round in pi/8 steps (0x485760 -> 0x4855D0)
instead of cutting across to the point 90 degrees round — holding Q or W passes procedure 497
a reference axis 90 degrees from the current one every frame, and the solve's "swing round"
keeps the camera on its circle. Camera collision (0x434660, a sliding 0.35 sphere) added.
The hand snapping back after shooting while strafing also happens in the original game (user
confirmed; live state and part-position capture with `r2live.py --parts`). Optional smoothing
can be considered later.

Earlier looks that shaped this: first look (2026-10-29) — too slow, Rayman outran it, Ctrl
didn't lock it to his back, it crashed into him when he ran towards it, and near the stone
Murfy appeared and both froze (the tutorial cutscene starts but waits on something missing).
Second look — the camera followed properly and the portal spin's direction and animation were
right; still left at the time: holding Rayman at the swirl's centre while it spins,
Ctrl-strafing backward moving in one fixed world direction, the arrival pan, a possible
field-of-view difference (Rayman looked smaller in OpenRayman), missing long-idle animations
(the "basketball" one). Third look (2026-10-29) — portal hold and strafing fixed; left at the
time: clipping through walls, Q/W turning glitching and zooming onto his face, the hover view
looking off in another direction, a little backward drift per strafe, and the waterfall at
`--level Learn_10 --camera 46.7,314.9,-2.4,120.0,-3.8` having collision.

## 2026-10-30: ledge grab and climb

Done and committed (df7669c grab, b1ad3c2 climb, dc434cf contacts): Rayman grabs ledges and
hangs (his front probe sphere touching a grabbable top, seen live with `r2live.py --zones`);
grabbing only while falling works (user confirmed). He climbs up onto the ledge by the climb
animation's own motion (frame vectors, dynamics flag 0x1, base +0x54; procedures 297/298).
Procedure 218 (0x46AB20) is the hang state machine, called every frame while hanging. Also:
between the stone pillars he holds on lined up with them and stops at the joining wall (node
34's trigger, procedures 136/81, 44e7152), to be checked.

## 2026-10-30: the portal hop

The portal actors (nodes 72, 77) give Rayman a launch of (0, -10, 6) in his own axes; in the
game he hops straight up and drops onto the grass under the portal (user), ours hopped off to
the side and the speed lingered ~10 ticks. Fixed with procedures 67/68, wall contacts and
inertia the right way round — the arrival portal now drops him under it as in the game
(landing within 5 cm), to be checked. The portal also draws sparks from Rayman into itself
when he is near (particle kind 175), to be checked.

## 2026-10-30: water

Water is a contact of its own (slot 4, report bit 0x40), and jumping into shallow water no
longer makes him swim (functions 166..168, 175..177, 198, 199: the scripts' depth cast).
Splashes and ripples (particle kinds 7, 39, 60, 62) done. Swimming itself still needs
procedures 232-234, 237, 245, 258, 302.

## 2026-10-30: camera follow and obstacle avoidance

An interim obstacle search: when the target is hidden, pick a reachable candidate with a
clear view, kept across ticks so the camera can go around a corner (`formats/camera.md` has
the full notes and the user's 2026-10-30 corridor and corner recordings). The user is happy
with the camera for now; the original's candidate scoring and transition modes remain to be
validated.

## 2026-10-30: slippery slopes

Slippery slopes slide Rayman uphill instead of down — likely a sign in the slope direction
(contact normal, function 190, or the slide procedure; user: `--level Ly_10 --camera
-4776.4,-8543.0,-8238.9,-122.6,-43.9`). The rerun of task_slope_projection.md starts from
Rayman's resolver 0x437E60.

## 2026-10-29: starts and story bits

Story bit 1133 (actor 6's bit array, variable 42) means Learn_10 is being revisited from the
hub: the scripts then remove the Teensies, the chain and Murfy's tutorial (node 62 and
others), as the real game does (user confirmed). Our default start is a half-revisit: only
story bit 1133 is set, so the trapdoor opens and the Teensies leave, but the hanging cage
(node 67) is still there, asleep (brain-off bit in the data; its wake-up chain: trigger node
82 -> counter node 83 -> clear bit 17). A real revisit has the cage broken. Next: a
first-visit start (no 1133; skip or stand in for the intro cutscene) so the tutorial chain
(cage, Murfy's Lum tutorial, Lum, trapdoor) can be tested; reading real saves' story bits is
Phase 4.

## 2026-10-29: movement and walls

Walking, turning and walls feel right (user, 2026-10-28). Jumping: procedures 177..180 set
the imposed speed, and a script speed replaces the axes it names instead of adding to them,
so a jump overrides gravity and leaves the ground. Animations play at the state's rate (state
+0x29 into 3D data +0xE0, scaled by procedure 89) instead of the animation's own speed, which
is what made idle, blinking and shooting run too fast — Rayman's idles are 23 fps, not 30.
Walls push only where they cross the body's height, which fixed the invisible wall at the
cave exit (`--level Learn_10 --camera -31.0,291.2,-24.4,-178.0,-20.1`, user confirmed).

## 2026-10-28: the BNM sound research

The format work for 3d is done: `formats/sound.md` has the bank layout (validated on all 405
banks), the event/resource ids and types, the `.snd` slot fix-up, and how the scripts and
animations reach a sound. Two findings shape the implementation:

- an **event** is what the data names (`(bank << 16) | index`); event type 1 plays its
  resource, **event type 2 stops** the voices that a given event started (1472 of the 11880
  level `.snd` pairs are stops — the ambient loops get switched off this way)
- a **type-11 resource is a music theme**: an ordered list of external `.apm` segments, 147
  of them in 85 banks, referenced by 1425 `.snd` pairs across 58 levels. The adaptive
  sequencing is therefore not a separate format — the playlist is the theme record.

What to build, in order: (1) ✅ the `bnm_bank` reader in `libopenrayman` (validated against
all 405 banks; `ortool bank` / `ortool sound`), still missing an `snd_file` reader for the
`(event id, slot address)` pairs; (2) `game`: a sound module holding the loaded banks,
resolving a slot to an event, expanding groups, queueing play/stop requests, wired to the
animation player (event type 0, currently skipped) and script node type 30 plus procedures
115/117/118/119; (3) `src/audio`: an SDL mixer for the 16-bit mono 22050 PCM samples and the
external `.apm` pieces, honouring the static volume (resource `+0x14`) and the event's pitch
variation (`+0x0C`, `0x10000 ± v`); (4) adaptive music: play a theme's segments in order
from `+0x1C`, then the `+0x28` tail, `silence.apm` as a fade-out — which segment to move to,
and when, is the open question (see the `SXD_DIST_*` options in `Snd_Cpa.ini`).
