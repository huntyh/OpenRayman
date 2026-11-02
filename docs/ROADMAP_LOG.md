# Roadmap log

The dated investigation notes from `ROADMAP.md`, kept here so the roadmap stays short. Newest
first. The format docs under `formats/` are the permanent home of the findings themselves.

## 2026-11-02 (late night): the cutscene bars, round two

DeepSeek (read-only, build/helpers/black_bars_report2.md), checked by us: no exe code reads the
camera actor's custom bit 30 or its var6 (every 0x20000000 test is on render objects or particle
kinds; var6 is an owner token the camera controllers compare among themselves, 65000 = a scene
owns the camera). The engine's letterbox is procedures 313 (size in percent, display record
0x5004D4 +0x30/+0x34) and 314 (bands per 1000, +0x48..+0x54): 0x420660 shrinks the viewport
(+0x38..+0x44) and paints the margins with the driver's black-polygon call. The Menu's node 3
animates 314 for its own effect; `ortool brain Learn_10` has no call to 313..317, so the Learn_10
bars come from elsewhere. Cheapest next step: add the record's fields to `r2live.py --camera` and
watch them during the kids' scene; if they stay put, read the kind-120 rectangles (0x4E0F20) and
the screen sprites (0x4E1638). The report's Learn_10 line numbers for 314 were wrong (they are
the Menu's).

## 2026-11-02 (late night): ticked off

- The slippery slope (Ly_10) feels like the game's (user); Codex measured 10.3 u/s² down the
  fall line in the recording and 10.7 in ours; the slow first step onto the slide is the game's
  too. The two contact details that still differ are in formats/dynamics.md.
- The faint crosshair lines around Rayman are gone (user): his shadow's edge texels were
  stretched along his axes outside the blob (1ff8529).
- The arrival pan works (user): starting without `--start new` no longer leaves the camera
  behind a wall. The kids' cave loop is fixed (5f5545e).
- The end scene: "Stop!" plays once and the occasional vibration couldn't be reproduced (user).
- Procedure 502 (0x47F560) only sets a byte (0x500FAC) that pauses some actors' frames
  (0x40A5C0, 0x44B2E0); it is not the black bars.
- Performance (moved from the roadmap): about 960 draw calls a frame for 6,500 triangles (fine on
  a desktop, too many for phones: batch them), and a script spike of up to 6 ms once a second.
  In the Globox kids' scene (debug build) the scripts took 14 ms a frame with about 90 actors
  thinking; `ORTOOL_COST=1 ortool run` ranks the actors by script time (the camera, node 5,
  about a fifth, the rest spread out), so the interpreter itself is the cost. The release build
  plays the same scene at about 740 fps (scripts 0.1 ms a frame, user).

## 2026-11-02 (night): dialogs, HUD, scenes

- Dialog colours confirmed (user: "look amazing", no aliasing): the glyphs' vertex colours are
  signed 16-bit, divided by 256 (0x431400) and doubled and clamped by the material conversion
  (0x42D520); we had divided by 255, which made Rayman's white lines blue (Codex). The font list
  has 882 objects: blue at /O 0, red 200, yellow 400, white 800.
- Health bar works (user); Rayman starts Learn_10 below his maximum on a new game, as in the game
  (he collects red Lums on the prison ship). Found by DeepSeek in node 44's macro 2 (kinds 50/120).
- The first cage's burst (kinds 5, 9, 45) looks right (user). The game's Lum also waits 500 ms
  after bit 840; the burst hides the wait.
- Revisit start looks finished (user): no Lums, Teensies, final cage or kids' scene, HUD 5/5 and
  2/2. The save findings are in [formats/saves.md](formats/saves.md).
- The twin Rayman after the kids' scene is gone (user): procedures 84/85 switch the scene's
  stand-in (node 41) on and off.
- End scene: the loop seam overshoot is fixed (parts pulling apart once per loop); the user still
  sees the Teensies vibrate now and then. Dialog cues after an event are read at once (0x475680),
  so the Teensies appear with the camera cut (user confirmed).
- Black bars slide in from the top and bottom as a scene starts (user). DeepSeek: no script draws
  them; the camera actor's custom bit 30 marks the scene's length.
- In the kids' cave, running towards the camera made it circle round to Rayman's back as long as
  he moved (`--camera -67.6,206.3,3.1,-74.8,-10.5`). Cause: our interim obstacle search started
  with a clear view and parked the camera on his path; procedure 499 then held it above him.
  Fixed (5f5545e, Codex); the user says it feels a lot better. Rodeo_10: the camera stays on top
  of a narrow hole Rayman drops into.

## 2026-11-02 (evening): the user's retests

- Ceilings hold (user retested): the bridge, the ears and the Globox kids' cave roof. Standing on
  actors moves to Done.
- Dialogs: the text still lands somewhere far off in the world; placement and font stay open.
- Arrival pan: started without `--start new`, the camera begins behind a wall, out of bounds,
  and can't get through to Rayman.
- Next machine: the Fermi laptop (Quadro 2000M) on Linux with the AUR 390xx driver.

## 2026-11-02 (afternoon): standing on actors, and a profiler

- Rayman stands on actors (GPT-6.1 Sol's job, reviewed): the Learn_10 trapdoor (node 66) holds
  him at Z -10.824 as in the recorded game and slides away once the Lum is collected; moving
  supports carry their riders. It cost frame rate (user: 160 fps to 80-100 in the debug build):
  every colliding actor re-posed every actor each tick. Cached and limited to the few actors with
  surfaces, it is back to its old cost (user: 130 fps in the same place).
- The user found the release build far smoother (about 275 fps, held at the monitor's 280 Hz by
  vsync). A profiler now shows where frames go (F8, `OPENRAYMAN_PROFILE=<file.csv>`), and vsync
  is a setting (F9, `--vsync on|strict|off`, "on" adaptive where the driver allows) with an
  optional cap (`--fps-limit`). Frame times are measured in nanoseconds now: whole milliseconds
  made each frame 3 or 4 ms at 280 fps, an uneven step.
- First numbers, Learn_10 start, vsync off, release: about 550 fps (1.8 ms a frame). Drawing on
  the CPU takes 1.1 ms for about 960 draw calls of only 6,500 triangles; the GPU needs 0.35 ms;
  the scripts 0.2 ms on average but up to 6 ms in one frame a second. In the debug build the
  scripts take 12 of 18 ms. On the user's machine (5800X3D, RTX 3090) the release build runs
  at about 840 fps with vsync off.
- The condor's bobbing is parked (user: fine as it is); the first-visit checks are closed: node 71's
  short walk is right, and the condor stays on every visit in the game too.

## 2026-11-02 (midday): the sound mix, and what the user confirmed

- Confirmed in play by the user: shot homing; the Globox kids' theme; the big-nosed flier falls
  silent when it puts itself away; the tunnel after the Globox kids no longer lets Rayman clip
  through; the 3 fps drop near the Globox kids is gone (the camera's
  obstacle search no longer runs at a distance, segment casts use the collision grid); the
  bridge ceiling stops Rayman. The mix "way better" after the measured levels below.
- First-visit checks closed (user): node 71's short walk is right, it is Rayman stepping forward
  after the cage over the trapdoor breaks, before Murfy explains the Lum. The river condor is
  there on every visit in the game too (also coming back from the Hall of Doors), so it should
  not leave; only its flight is wrong: ours bobs with each wing flap, the game's glides smoothly.
- Lum pickups were silent: stopping every voice of a hidden actor (for the flier) also cut the
  Lum's pickup sound, played in the frame it hides. Now only looping voices stop.
- Themes decode on a loader thread with a segment cache: decoding under the mixer's lock stalled
  the audio when the kids' scene asked for its theme. (A first version dropped every theme on any
  stop event, so no music played; fixed the same hour.)
- `tools/r2audio.py` measures the sound output (Windows loopback, every 50 ms), alone or as
  r2record's `audio` topic. Against run5 (music only) and run6 (a run through the woods), game
  volume options at maximum: our music was 6.4 dB too loud (now 0.48, within 0.8 dB), and
  Rayman's take-off, landing roll, ricochet and splash peak 6-12 dB under their samples in the
  game (effects now 0.38). Ours had peaked at -0.1 dBFS over the same route, the game at -6.9.
- Who asks for Learn_10's themes: a sector list (the woods' theme, at the hand-over), node 45 the
  level director (the intro's theme at level start, the Teensie theme when the last cage breaks),
  node 39 a Globox kid (the kids' theme), node 41 (bank 239's theme 30 s later).

## 2026-11-02 (morning): shot homing, third try

- GPT-6.1 Sol (task with the right shoot key, Space, and the user's test spot at the last cage)
  wrote functions 113 (target search: custom bit mask, cone, three-weight score, the current
  sector and its +0x24 neighbours, sectors followed live by box) and 154 (collision-zone centre).
  `ORTOOL_PLACE=24.8,125,24.25,270@240 ORTOOL_SHOTS=1 ortool run ... Learn_10 410 53 Space:300-305`:
  a shot fired 23 degrees off turns towards cage node 53 at launch and hits it at tick 352; with
  nothing ahead it flies straight. Pinned in the tests. Reviewed and reproduced here.

## 2026-11-02 (night): music plays; homing second try came back empty

- Two GPT-6.1 Terra jobs. Music: a theme player in `sound_player` (segments, repeat points,
  restart, tail, stop), built and tested, but its own runs logged no music: nothing asked for a
  theme. The asker turned out to be the sectors (0x412880: a sector's list of sounds for walking
  in, from a given sector or any). Learn_10's only entry plays 9:5 entering the woods' sector from
  the intro's, which the hand-over does; the engine now follows Rayman's sector and plays these
  lists. The user heard the music in game; the segment log matches the run4 recording to 0.02 s.
- Homing (functions 113/154): no code landed. The job found the cage's sector (12) and the zone
  centre helper 0x476880, but could not make Rayman shoot in its ortool trace, so it had nothing
  to test against (`formats/generated.md`).

## 2026-11-01 (evening): fixes and dialogs

- Ceilings stop jumps (800c6c2): `world_collision::ceiling_at` stops a move up where the head
  would pass a downward-facing surface — the bridge and the Globox kids' cave roof. Awaiting the
  user's test at both spots.
- The hidden end-of-level Rayman (`--camera 21.6,115.8,29.6,266.5,-25.8`) no longer calls
  "Hey!": actors put away with custom bit 14 (0x2000) skip their animation step as in the exe
  (0x40A5C0).
- Dialogs run: function 158, procedures 430/243 wired, the string table from Fix (6facc6f).
  Jail_10's first narration line returns events 10, 11, 3 at the same reveal positions as the
  recorded game, about 90 ms later. Drawing the text and procedure 393 are still open; the
  Globox kids' scene doesn't start its dialog.
- Homing (functions 113 and 154) investigated, notes in `formats/generated.md`; implementation
  in progress.

## 2026-11-02: the Globox kids' scene plays (user)

- The Globox kids' cutscene plays with Rayman talking and the same animations as the game (the
  dialog text isn't there yet).
- Murfy's text shows (the stand-in font), placed in the world; text placement is not right yet.
- A long "380:380:..." line floated over the river (from `--camera -3.6,6.1,1.6,294.1,-11.8`,
  where a Ly head used to be): a scene director's cue string; text without letters is no longer
  drawn.
- "Hey!" from the hidden end-scene Rayman is gone; the cave spot runs smoothly (no 3 fps).
- The bridge: Rayman no longer passes, but his ears did; the ceiling test now uses the top of
  his ears (2.2) instead of his body (1.8).
- The big-nosed flier puts itself away after checking Rayman but kept buzzing: removed or hidden
  actors now fall silent.
- The river condor (`--camera -65.1,289.0,-18.7,-28.6,-15.6`) snaps upwards a little on each wing
  flap before settling.
- Music still doesn't play.
- Breaking the last cage plays the end-of-level Teensie scene through to the end; Rayman is a
  bit buggy in it, but it works.
- Rayman's pupils still show a little when he closes his eyes (blinks).
- The font, later: smooth (antialiased) edges instead of the old PC aliasing, and fix the
  original's placement slips (the big "Yahoo!" when all of a level's Lums are collected and the
  menu's "overwrite save" text run off the screen).

## 2026-11-01 (late): text overlay and homing, first tries

- The dialog text is drawn by a stand-in overlay (SDL's debug font) until the game's glyphs are
  decoded. At `--level Jail_10 --time 10` nothing shows yet: the narration line is there (55
  letters revealed) but flagged as world text at (60, 200, 17), which look like screen
  coordinates, so procedure 443's "screen/world" reading is likely wrong. A scene director's cue
  string (id 20016, `/E105:/E100:...`, timing events only) is also listed as text; the game
  never shows those. Next: function 158's placement branches and which text actors are visible.
- The Jail_10 screenshots show the camera already panning over the sea from its scripts.
- Homing: a first implementation didn't hit the cage and was removed; the exe's search walks
  per-sector actor lists our engine doesn't keep. Findings in `formats/generated.md`.
- Procedure 393 (reached every tick on a first visit since function 158 runs) sets fields on an
  object attached to the calling actor; caller and meaning still unknown.

## 2026-11-01: a recorded playthrough (`tools/r2record.py`)

The user recorded a new game from the menu through the pirate ship (Jail_10, Jail_20), Learn_10
and into the Hall of Doors (Mapmonde): `H:\r2export\live\run3\`. Everything but the level split
worked first time (Jail_20 into Learn_10 keeps Rayman's super object; fixed). What it showed:

- Music is scripted per moment. Learn_10 opens with theme 12 of bank 9 (event 0x90009), its
  segments 1..3 repeated on a countdown; at the hand-over (the frame story bit 1091 is set) a
  script asks for event 0x90008, theme 5 (30 segments, `FO02.apm`...), and on the way to the Globox
  kids' cave event 0x9000A, theme 13. Segments last 2.3..4.8 s and follow on the frame one ends.
- The intro narration's text boxes: each line returns event 10 as it starts showing, 11 at its
  end, then 3, and the box goes; `texts.txt` has the strings with their control codes
  (`/o200:` highlights, `/p<ms>:` pauses, `/D<n>:` reveal speed).
- The story bits match the earlier log exactly.
- `run4` repeats the run with Learn_10 as its own level (L4): Murfy's talks, the Globox kids'
  scene, the Teensies' cage and the end scene. In Learn_10 the recorder's "actor N" is node N+2:
  text boxes come from nodes 38 (the intro), 42 (Murfy), 3 (the kids' director), 46, 47, 68.
  The user also noted the shots home in on cages (function 113, still missing).

## 2026-11-01: the tutorial chain works (user)

- The tutorial cage breaks, its Lum breaks out and flies its little loops as in the game, and
  collecting it opens the trapdoor.
- The field of view matches: the game's is 51.6 degrees vertical, which the game camera uses
  (0.9 rad); the fly camera keeps 60.
- The hand no longer snaps back when shooting while hovering.
- The Lum collect effect exists (it came with the shot's bounce sparkles) and Lums play their
  pickup sound; the user checks whether the game shows any other effect.
- 3 fps at `--camera -69.3,207.6,2.0,297.6,-0.4`: the camera's obstacle search ran with the
  game camera dragged far from Rayman by the fly camera, so every cast and sweep spanned the level.
  It now skips cameras more than 20 units from their target, and segment casts use the collision
  grid (155 ms to 6 ms a frame). `OPENRAYMAN_FRAMES=1` logs frame times.
- With `--story-bits 1095` the Globox kids cheer from the start: their own script picks that
  state for bit 1095 (the silver shot, much later in the game). The user logs a real
  playthrough's story bits with `r2live.py --story-only` to see which bits a first visit has.
- New finds: Rayman jumps up through ceilings (the bridge near the waterfall, the Globox kids'
  cave roof); the Globox kids start in the wrong state; a "Hey!" voice repeats where the debug
  Rayman was; the flier's buzz goes on after it vanishes.

## 2026-10-31: the first visit plays

- Murfy's freeze at the stone is gone: since the first-visit start runs (node 38's intro ended at
  the hand-over, the CD checks answered), Murfy appears, whispers, waits on his (still invisible)
  dialog until Space, then waves and leaves (user).
- The tutorial cage hangs from its chain and says "help!"; breaking it frees its Lum (user). The
  trapdoor under it has no collision yet (standing on actors is missing).
- Without `--camera` the game starts on Rayman through the game camera (user).
- Ly_10's slippery slope: Rayman slides down instead of backwards; the acceleration is still off
  (user).
- The big-nosed flier leaving Rayman to sit by the camera is the original game's behaviour too;
  ours keeps buzzing there and its loop seam is harsh (user).

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

Story bit 1133 (actor 6's bit array, variable 42) was first read as "Learn_10 is being revisited
from the hub" — a 2026-11-01 recording showed the bit is set when Rayman jumps into the exit
gate, i.e. when the level is **finished** ([formats/scripts.md](formats/scripts.md) has the
story bit table). The practical consequence noted here is unchanged: the scripts then remove
the Teensies, the chain and Murfy's tutorial (node 62 and others), as the real game does (user
confirmed). Our default start is a half-revisit: only
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
