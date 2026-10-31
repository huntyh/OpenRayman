# Roadmap

Rayman 2 runs on Ubisoft's OpenSpace engine (originally "CPA"). Most of the gameplay — Rayman's
moves, enemies, triggers, cutscenes — is compiled AI bytecode stored inside each level's `.sna`
files. The original `Rayman2.exe` is mostly a renderer, physics and collision, and a virtual
machine running that bytecode.

So instead of decompiling the executable, OpenRayman reimplements the engine and the bytecode VM.
Once the VM works, the game's own logic runs from your data files.

## Phase 0: modern foundation ✅

- C++20, CMake presets, Windows (MSVC) and Linux builds, dependencies fetched automatically
- SDL3 for window, input and audio; OpenGL 3.3 on desktop, OpenGL ES 3.0 on mobile
- Readers rewritten and tested: CNT texture archives, GF textures, APM music, DSB scripts
- Case-insensitive file lookup, since the game assumes Windows paths
- Texture browser and music player as an end-to-end check; `ortool` for exporting data
- Unit tests, plus checks against a real installation

## Phase 1: level data ✅

- ✅ SNA block decoding, relocation tables, the hidden tables in LEVELS0.DAT
  ([format notes](formats/levels0-dat.md)), and a memory image where every pointer of all 70
  levels resolves ([format notes](formats/sna.md))
- ✅ `ortool level <name|all>` loads levels and checks their pointers
- ✅ GPT and PTX files: the level header with its world roots, how Fix's actors (Rayman, the
  camera, ...) join each level, and every level's texture list ([format notes](formats/gpt.md))
- ✅ The super object hierarchy: every level's scene tree rebuilt, with all links cross-checked
- ✅ Static geometry: sectors, meshes, UVs, materials and their textures
  ([format notes](formats/geometry.md)); `ortool obj` exports a level for any 3D viewer

## Phase 2: one level, looking and moving like the game ✅

Goal: The Woods of Light (`Learn_10`) looks like the real game and comes alive, without gameplay
yet: everything that moves on its own moves, but nothing reacts to Rayman.

**Milestone 2a: the viewer ✅ (merged into master, 2026-10-26)**

- ✅ Static geometry with textures, a free-fly camera, starting from where the game's camera starts
- ✅ Level picker for all 70 levels (Page Up / Page Down)
- ✅ Actors (characters, props, platforms) posed at the first frame of their animation
  ([format notes](formats/animation.md)), placed with the game's own transforms
- ✅ Sprites (Lums and other glows), and textures' see-through, glowing and mirrored modes
- ✅ No seams at texture edges; P copies the camera position for bug reports

**Milestone 2b: things move ✅ (merged into master, 2026-10-26)**

- ✅ Animation playback: every actor loops its current animation, blending between keyframes.
  Many actors are saved in a still or placeholder state (Lums
  without flapping wings, mushroom clusters with 3 of their 5 mushrooms, cages without chains); the
  scripts pick their real state, so those come with Phase 3
- ✅ Cards that turn to the camera: mushrooms, flowers, flames, chain links
- ✅ Animated textures: Lums' wings flap, cage lights blink, flames flicker
- ✅ State preview (F4/F5): step an actor through its family's states, to check the animations the
  scripts would pick (the yellow Lum is state 4 of the Lum family)
- ✅ Moving textures: the water stream, waterfall, mist, fog and clouds scroll at the game's
  speeds (assuming its 60 frames per second: compare the speed with the game)
- Moved to Phase 3: the fireflies. They come from invisible emitter actors whose scripts spawn
  particles while the game runs, so there is no particle data to draw yet

**Milestone 2c: the game's light and sky ✅** (merged 2026-10-26)

- ✅ The game's own lighting: each sector's lights, combined with the materials exactly as
  `Rayman2.exe` does (read from the exe): colours, red glows and blue tints match side by side
- ✅ Fog: the blue-green haze of The Woods of Light, by depth like the game's Glide fog table
- ✅ Water and glass: lights also set how see-through they are, which gives the waterfall its soft
  edges; soft and glowing surfaces are drawn back to front
- ✅ The sky: the fog's background colour behind everything, and each sector's sky material (the
  scrolling clouds of The Woods of Light) on a dome around the camera
- ✅ Shadows under characters: the actor's shadow blob, laid on the ground below it
- Moved to Later: a modern lighting mode that keeps the art direction, together with HDR, since
  HDR changes how lights have to work anyway

Phase 2 ended when Learn_10 side by side with the real game looked the same, apart from the parts
that need gameplay (2026-10-26).

## Phase 3: make it a game ⬅ we are here

Goal: Rayman runs around The Woods of Light (`Learn_10`) as in the real game, with its characters
behaving as their scripts say. Everything targets Learn_10 first; the other levels get their turn
once it plays right (they have issues of their own, found on a first look).

Most of the gameplay is in the scripts saved inside each level, so they come first.

**Milestone 3a: reading the scripts ✅**

- Each actor's brain: its AI model, behaviours, reflexes, macros and designer variables (DsgVars)
- The script nodes as a tree, and the interpreter's 41 node types (from `Rayman2.exe`)
- `ortool brain` prints an actor's scripts as readable text, with our own names for the keywords
  and operators (the game keeps no names). Conditions, functions and procedures get theirs as each
  is read from the exe and implemented in 3b

**Milestone 3b: the scripts run ✅ (merged into master, 2026-10-28)**

- ✅ The interpreter, rebuilt from `Rayman2.exe`: behaviours, reflexes, macros, designer variables,
  waits and other actions that take several frames, at the game's 60 ticks per second whatever the
  frame rate. About 150 conditions, functions and procedures so far, read from the exe; `ortool run`
  lists the ones Learn_10's scripts still reach that aren't written yet
- ✅ States and transitions picked by the scripts, with the game's animation player: the cages
  hang from their chains and swing, the mushroom clusters dance with all five mushrooms, the Lums
  float and turn to face Rayman
- ✅ Actors move and turn where their scripts put them
- ✅ Animations blend as the game's do (eased keyframes); looping animations whose ends don't meet
  glide into their start, one of the improvements listed in [IMPROVEMENTS.md](IMPROVEMENTS.md)
- ✅ The fireflies: not particles after all, but four small actors (nodes 73 to 76) that hover
  around their spot, steered by their scripts (procedure 171, read from the exe), and fly to Rayman
  when he comes near. Until the physics of 3c, a speed the scripts set moves an actor freely. The
  invisible actors once taken for firefly emitters (nodes 31 and 32) play ambient sounds (3d)
- ✅ Particles: the effect spawner the scripts use (procedure 503), read from the exe. The specks
  floating up in the dark parts of The Woods of Light (and a twinkle at one spot) are in; other
  effects (sparks when a cage breaks, Rayman's dust) follow as they're needed. Until the game's
  camera runs (3c), the level only starts the specks with `--behaviour 4:1`
- Moved to 3c, since they need collision: the Lum in the cage by the river door keeps inside its
  cage in the game, most likely by bumping into the bars; the condor's flight along its path

**Milestone 3c: Rayman ⬅ we are here**

- Collision with the level and with actors, and the game's physics. Level collision read and shown
  (C), surface types grabbable and water confirmed. Physics started: gravity, script speeds and
  landing on the floor ([formats/dynamics.md](formats/dynamics.md)); walls and actors next ⬅
- Input mapped to the game's controls (keyboard, gamepad). Keyboard done: the game's own input
  actions are read from Fix and fed every tick (Tab in the viewer), and the movement input
  (procedure 346) turns the arrows into the camera-relative vector Rayman's scripts read. Rayman
  reacts (jump, shoot), but stays airborne until the physics lands him. Gamepad not yet
- Rayman himself walking, running and jumping, moved by his own scripts
  Walking, turning and walls feel right (user, 2026-10-28). Seen in the game and working:
  - ✅ Jumping: procedures 177..180 set the imposed speed, and a script speed replaces the axes it
    names instead of adding to them, so a jump overrides gravity and leaves the ground (b621de7).
  - ✅ Animations play at the state's rate (state +0x29 into 3D data +0xE0, scaled by procedure 89)
    instead of the animation's own speed, which is what made idle, blinking and shooting run too
    fast. Rayman's idles are 23 fps, not 30.
  - Both of Learn_10's starts: the new game's cutscene (not done yet), and the return from the hub (✅), where he
    stays in his spin state (04/00 +0x1210) before he falls (user, 2026-10-29; the portal exit is
    04/00 +0x739C, played because his "came through a portal" variable is set at load, 3475231).
  Still to do:
  - ~~The portal's sparkling particles~~ (kinds 9 and 55, 2026-10-30); ~~splashes when he lands in water~~.
  - Seen from afar, faint straight lines like a crosshair on X and Y around Rayman, turning with him.
  - Hover timed by the user: 1.4-1.6 s in both games, so its speed is right. Near the cliff by the
    spawn he can stop in mid-air while hovering and rise a little (probably the sloped cliff face
    taken as floor).
  - The game camera (docs/formats/camera.md) runs: its scripts fill the parameter blocks, the solve
    and the camera mechanics move it, and the view follows it while playing (Tab). Left out so far:
    the original's obstacle-search scoring and the occlusion pass. Target tracking and an interim
    obstacle search are in; the user is happy with the camera for now (2026-10-30). The search is
    still an approximation, not the original's algorithm. ⬅ we are here.
    First look (user): too slow, Rayman outruns it (the game's keeps up at his back and lags only
    without room, e.g. dropping into the hole by the river cage, then flies after him; it seems to
    collide with walls and ground). Ctrl doesn't lock it to his back yet; Ctrl+left/right strafes
    correctly. Near the stone Murfy appears and the camera closes in on Rayman, then both freeze
    (the tutorial cutscene starts but waits on something missing).
    Second look (user): the camera follows him properly; the portal spin's direction and animation
    are right. Still to do: hold Rayman at the swirl's centre while it spins (he isn't locked there);
    Ctrl-strafing works forward but backward/left/right move in one fixed world direction; when he
    runs towards the camera (Down) the game's camera backs off and pans to show his front, ours
    crashes into him; he may look closer in the game (field of view?); his long-idle animations (the
    "basketball" one) are missing.
    Third look (user): portal hold and strafing fixed. Left: the camera clips through walls (no
    collision yet); Q/W turning makes it glitch and zoom onto his face; while hovering it looks off
    in another direction; strafing left/right also drifts backwards a little each time; shooting
    while strafing/hovering snaps the hand back (missing keyframe?); the waterfall at
    `--level Learn_10 --camera 46.7,314.9,-2.4,120.0,-3.8` has collision. The optional modern camera
    waits until the game is nearly complete (with HDR and the like).
    Fourth look (user, 2026-10-29): waterfall passable (virtual sectors have no collision), strafing
    right in every direction with no drift (each actor thinks then moves, 0x412230), the hover
    camera works like the game's, tilting down to show where he lands (function 310 returns the
    offset point). The decompile now covers every script handler. Q/W turning fixed (2026-10-30): the
    exe swings the camera round in pi/8 steps (0x485760 -> 0x4855D0) instead of cutting across to
    the point 90 degrees round. Camera collision (0x434660, a sliding 0.35 sphere) added. The hand
    snapping back after shooting while strafing also happens in the original game (user confirmed;
    live state and part-position capture, 2026-10-31). Optional smoothing can be considered later.
    Left: the shot while hovering, the
    arrival pan, the field of view, Murfy's freeze, long-idle animations. Slippery slopes slide
    Rayman uphill instead of down (likely a sign in the slope direction: contact normal, function
    190, or the slide procedure; user: `--level Ly_10 --camera -4776.4,-8543.0,-8238.9,-122.6,-43.9`).
    Story bit 1133 (actor 6's bit array, variable 42) means Learn_10 is being revisited from the hub:
    the scripts then remove the Teensies, the chain and Murfy's tutorial (node 62 and others), as
    the real game does (user confirmed). Our default start sets it (portal arrival); a first visit
    (`--start new`) needs the intro cutscene. Still to check: an invisible wall at the cave exit,
    `--level Learn_10 --camera -31.0,291.2,-24.4,-178.0,-20.1`: fixed, walls push only by their slice
    at body height (user confirmed). Lums can be collected but vanish without their effect. Rayman
    grabs ledges and hangs (2026-10-30: his front probe sphere touching a grabbable top, seen live
    with tools/r2live.py); grabbing only while falling is working (user confirmed). He climbs up onto the ledge by the climb animation's own motion (frame
    vectors, dynamics flag 0x1, base +0x54; procedures 297/298), to be checked by the user; the hands
    on the edge (procedure 218) are still missing. The portal draws sparks from Rayman into itself
    when he is near (particle kind 175), to be checked. Between the stone pillars he holds on lined up with
    them and stops at the joining wall (node 34's trigger, procedures 136/81), to be checked. The arrival portal drops him
    under it as in the game (procedures 67/68, inertia, wall contacts; landing within 5 cm).
    Our default start is a half-revisit: only story bit 1133 is set, so the trapdoor opens and the
    Teensies leave, but the hanging cage (node 67) is still there, asleep (brain-off bit in the data; its
    wake-up chain: trigger node 82 -> counter node 83 -> clear bit 17). A real revisit has the cage broken.
    Next: a first-visit start (no 1133; skip or stand in for the intro cutscene) so the tutorial chain
    (cage, Murfy's Lum tutorial, Lum, trapdoor) can be tested; reading real saves' story bits is Phase 4.
    Portal exit: the portal actors (nodes 72, 77) give Rayman a launch of (0, -10, 6) in his own axes;
    in the game he hops straight up and drops onto the grass under the portal (user), ours hops off to
    the side (world +X, his facing) and the speed lingers ~10 ticks. Next: why the exe's hop has no
    horizontal part (facing at release, or how the mechanics applies proposed speeds; procedure 198
    0x46CE00 local/world bits). Water: contact slot 4 / bit 0x40 done; jumping into shallow water no
    longer makes him swim (functions 166..168, 175..177, 198, 199: the scripts' depth cast); splashes
    and ripples (particle kinds 7, 39, 60, 62) done. Swimming itself still needs procedures 232-234,
    237, 245, 258, 302. Slope slide: the rerun of
    task_slope_projection.md starts from Rayman's resolver 0x437E60.
    In the game the player may turn the camera left/right (Q/W) only where the level allows it;
    level camera presets often overrule it, and a camera icon with a red X shows when turning is
    refused (user).
- The game's camera following him
- An optional modern camera, turned freely with the right stick like in today's platformers, next
  to the game's own guided camera. It will see what the guided camera never shows, which needs
  handling: half-built cutscene sets (e.g. above the waterfall in The Woods of Light,
  `--camera 36.2,328.9,-3.7,-50.1,24.2`) and painted "paper" trees that simply end

**Order for the rest of Phase 3 (agreed with the user, 2026-10-30)**

1. Finish 3c: shooting first (the normal shot and the arm snapping back while hovering;
   there is no charged shot as far as the user knows, so only add one if Rayman's scripts
   show it) ⬅ now. Rayman's scripts do have a charged shot (holding the key, var19 up to 2000 ms,
   particles 69/71), but only once story bit 1143 is set, later in the game. The shot itself is a
   generated actor ([formats/generated.md](formats/generated.md)): function 66 copies its template,
   it flies on the fast mechanics, fades after 1.5 s and frees its slot; to be checked by the user.
   Still missing: hitting actors (procedure 22's touch query finds nobody yet), homing (function
   113), and checking the hand snap while hovering against the original. The strafing hand snap is
   present in the original; optional smoothing is a later improvement. Then the leftovers: the hands on the ledge (procedure 218), the clip out of the
   tunnel after the Globox kids, Q/W turning, long-idle animations, the arrival pan, Murfy's
   freeze, slippery slopes, the Lum collect effect.
2. A first-visit start: story bit 1133 left off, Rayman placed where the intro cutscene would end,
   so the tutorial chain (cage, Murfy, Lum, trapdoor) runs.
3. 3d: sound, together with the dialogs (Murfy, the Teensies), which need the voices. Goal: the
   first-visit start playable with its conversations.
4. Phase 4: the real intro cutscene in front of the first-visit start.

**Milestone 3d: sound**

- BNM sound banks: sound effects and voices, played when the scripts ask for them
- The adaptive music sequencing

The format work is done: [formats/sound.md](formats/sound.md) has the bank layout (validated on all
405 banks), the event/resource ids and types, the `.snd` slot fix-up, and how the scripts and
animations reach a sound. Two findings shape the implementation:

- an **event** is what the data names (`(bank << 16) | index`); event type 1 plays its resource,
  **event type 2 stops** the voices that a given event started (1472 of the 11880 level `.snd`
  pairs are stops — the ambient loops get switched off this way)
- a **type-11 resource is a music theme**: an ordered list of external `.apm` segments, 147 of
  them in 85 banks, referenced by 1425 `.snd` pairs across 58 levels. The adaptive sequencing is
  therefore not a separate format — the playlist is the theme record.

What to build, in order:

1. ✅ `libopenrayman`: a `bnm_bank` reader — `bnm_bank.h`/`bnm_bank.cc` read the header,
   events, resources, PCM payloads, group and theme entries; `tests/bnm_tests.cc` validates it
   against all 405 banks, and `ortool bank` / `ortool sound` inspect and export from them.
   Still to do: an `snd_file` reader for the `(event id, slot address)` pairs. Tests store counts
   and checksums only, never assets.
2. `game`: a sound module holding the loaded banks for the level (its DSB list, plus Fix), resolving
   a slot to an event, expanding groups, and queueing play/stop requests. Wire it to
   `game/src/animation_player.cc` (animation event type 0, currently skipped) and to script node
   type 30 plus procedures 115/117/118/119 (`not_yet` in `handlers.cc`).
3. `src/audio`: an SDL mixer for the 16-bit mono 22050 PCM samples and the external `.apm` pieces,
   alongside the existing `music_player`. Honour the static volume (resource `+0x14`) and the
   event's pitch variation (`+0x0C`, `0x10000 ± v`).
4. adaptive music: play a theme's segments in order from `+0x1C`, then the `+0x28` tail; treat
   `silence.apm` as a fade-out. Which segment to move to, and when, is still the open question (see
   the `SXD_DIST_*` options in `Snd_Cpa.ini`).

Implementation is deferred; the research above is complete enough to start from.

## Phase 4: the whole game

Everything the PC game has, working before we port it anywhere:

- Save games
- Menus. The pause menu already half-runs from its own scripts: Esc while playing Learn_10 brings
  up the menu's swirl in front of Rayman, and it stays there (user, 2026-10-30)
- Cutscenes
- All the levels, from start to end
- Widescreen and high resolutions, plus modern quality-of-life options

## Phase 5: Android

- SDL3 Android project, touch controls overlay
- Picking the game data folder via Android's storage access framework
- Performance pass for mobile GPUs

## Later

- A modern renderer: Vulkan or Direct3D 12, keeping OpenGL (3.3 / ES 3.0) for older devices. The
  current OpenGL renderer is the first step and the fallback.
- A modern lighting mode that keeps the art direction, and HDR, designed together (both change how
  the lights are used)
- Controller support with the right button icons
- Antialiasing (MSAA, with alpha to coverage for cut-out textures)
- Smoother cut-out textures: colour-keyed and alpha-tested textures have jagged, "cut out with
  scissors" edges (Globox's eyes); smoothing their edges, as we did for the leaves in 2c
- Richer water while running (optional, off by default): the game only leaves ripples when Rayman
  runs through shallow water, while jumping in throws droplets apart (particle kind 60) and
  bubbles; a few of those droplets and bubbles on each step would look livelier (user's idea)
