# Roadmap

Rayman 2 runs on Ubisoft's OpenSpace engine (originally "CPA"). Most of the gameplay — Rayman's
moves, enemies, triggers, cutscenes — is compiled AI bytecode stored inside each level's `.sna`
files. The original `Rayman2.exe` is mostly a renderer, physics and collision, and a virtual
machine running that bytecode.

So instead of decompiling the executable, OpenRayman reimplements the engine and the bytecode VM.
Once the VM works, the game's own logic runs from your data files.

Dated investigation notes and the user's look-by-look feedback live in
[ROADMAP_LOG.md](ROADMAP_LOG.md); this file keeps only what is done and what is open.

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

## Phase 2: one level, looking and moving like the game ✅ (merged 2026-10-26)

Goal: The Woods of Light (`Learn_10`) looks like the real game and comes alive, without gameplay
yet: everything that moves on its own moves, but nothing reacts to Rayman.

- ✅ **2a the viewer**: static geometry with textures, a free-fly camera starting from the game's
  camera start, level picker for all 70 levels (Page Up / Page Down), actors posed at the first
  frame of their animation, sprites and textures' see-through, glowing and mirrored modes, no
  seams at texture edges; P copies the camera position for bug reports
- ✅ **2b things move**: animation playback with keyframe blending, cards that turn to the
  camera, animated textures (Lum wings, cage lights, flames) and scrolling textures (assuming the
  game's 60 fps: compare the speed with the game), state preview (F4/F5). Many actors are saved
  in a still or placeholder state (Lums without flapping wings, mushroom clusters with 3 of
  their 5 mushrooms, cages without chains); the scripts pick their real state, so those came with
  Phase 3
- ✅ **2c the game's light and sky**: the game's own lighting exactly as `Rayman2.exe` does it,
  fog by depth like the Glide fog table, water and glass, the sky, shadows under characters
- Moved to 3/Later: the fireflies (emitter actors whose scripts spawn particles while the game
  runs), a modern lighting mode with HDR (designed together, both change how the lights are used)

Phase 2 ended when Learn_10 side by side with the real game looked the same, apart from the parts
that need gameplay (2026-10-26).

## Phase 3: make it a game ⬅ we are here

Goal: Rayman runs around The Woods of Light (`Learn_10`) as in the real game, with its characters
behaving as their scripts say. Everything targets Learn_10 first; the other levels get their turn
once it plays right (they have issues of their own, found on a first look).

**Done**

- ✅ **3a reading the scripts**: each actor's brain (AI model, behaviours, reflexes, macros,
  designer variables), the interpreter's 41 node types (from `Rayman2.exe`), `ortool brain`
  prints the scripts as text with our own names, and conditions/functions/procedures get theirs
  as each is read from the exe and implemented in 3b
- ✅ **3b the scripts run** (merged into master 2026-10-28): the interpreter rebuilt from the
  exe at 60 ticks per second (about 150 conditions, functions and procedures; `ortool run` lists
  the ones Learn_10 still reaches that aren't written), states and transitions picked by the
  scripts (the cages hang and swing, the mushroom clusters dance, the Lums float and face
  Rayman), the fireflies (four small actors, nodes 73 to 76, steered by their scripts), and
  particles (the effect spawner, procedure 503)
- ✅ **3c Rayman**: walking, running, jumping and animation rates (user confirmed), the portal
  exit's spin state, walls and actors push him out (only by their slice at body height),
  inertia, ledge grab and the climb, the portal hop (dropping under it as in the game) and
  its pull with the spark particles, the shallow-water depth cast with splashes and ripples,
  Rayman's shot as a generated actor (its flight, hit on actors, fade and slot freeing
  ([formats/generated.md](formats/generated.md)); homing still to come), the
  long-idle animations (the "basketball" one), and the game camera following him: scripts
  fill the parameter blocks, the solve and mechanics move it, Q/W turning swings round in pi/8
  steps like the exe, camera collision, and an interim obstacle search the user is happy with
  ([formats/camera.md](formats/camera.md)). Look-by-look details in
  [ROADMAP_LOG.md](ROADMAP_LOG.md).
- ✅ **3d sound, format and reader**: [formats/sound.md](formats/sound.md) (validated on all
  405 banks) and the `bnm_bank` reader in `libopenrayman` (`ortool bank` / `ortool sound`). Two
  findings shape the implementation: an **event** is what the data names (`(bank << 16) |
  index`), event type 1 plays its resource and **event type 2 stops** the voices an event
  started (1472 of the 11880 level `.snd` pairs are stops); a **type-11 resource is a music
  theme** — an ordered list of external `.apm` segments (147 in 85 banks, 1425 `.snd` pairs
  across 58 levels), so the adaptive sequencing is not a separate format.
- ✅ **First-visit start**: `--start new` works (user 2026-11-01): Rayman starts where the
  intro hands him over (the intro's director, node 38, is ended at the hand-over, since the
  intro itself is Phase 4 — `first_visit.h`), the tutorial cage hangs from its chain (it waits
  for the game's CD check, node 82, which we answer as the GOG release does; every level's CD
  checks, 65 actors in 25 levels, get the passing answer), Murfy appears and whispers, the
  cage breaks, its Lum flies loops like the game's and collecting it opens the trapdoor.
  Without `--camera` the game starts playing, seen through the game's camera.

**Open** (agreed order with the user, 2026-10-30; state 2026-11-01)

1. **3c leftovers.** The shot homing is being implemented (functions 113 and 154). The tunnel
   clip after the Globox kids is still open. The ceilings that let him jump through (the bridge
   at `--camera 40.8,305.1,-8.3,5.2,14.2` and the Globox kids' cave roof at `--camera
   -66.7,198.4,1.2,279.7,24.1`) have a fix in since 800c6c2, awaiting the user's test. The
   slippery slope slides downhill now but its acceleration is still off (the real slide
   accelerates about 11 u/s² down the fall line, a 2026-10-31 recording). Faint crosshair-like
   lines are seen around Rayman from afar. Nothing lets him stand on an actor, so the
   tutorial's closed trapdoor and every moving platform let him fall through; next: actors'
   collision shapes as floor. The river condor snaps upwards a little on each wing flap
   (`--camera -65.1,289.0,-18.7,-28.6,-15.6`). The first-visit start has its own open checks: the
   condor should leave once Rayman exits the cave, and node 71 (the tutorial's stand-in) walks a bit and
   stops. (The Globox kids cheering from the start was `--story-bits 1095`: their script picks
   that pose for the silver shot; without it they idle, and their scene should make them cheer,
   then cry once Rayman says Globox is missing, story bit 1092.)
2. **Dialogs.** Function 158 runs: the string table comes from Fix, and the events match a
   recorded playthrough, and the Globox kids' scene now plays like the game's (user,
   2026-11-02). Left: the text is drawn with a stand-in font in the wrong place (Murfy's lands in
   the world); next are function 158's placement and the game's own glyphs; procedure 393.
   Font wishes for later: smooth edges, and fixing the original's text that runs off the screen
   (the "Yahoo!" for all Lums, the menu's overwrite-save message).
3. **Music.** The themes and how the game moves between their segments are now understood from
   a recorded playthrough ([ROADMAP_LOG.md](ROADMAP_LOG.md) 2026-11-01,
   [formats/sound.md](formats/sound.md)); playing them is open (no music plays yet).
4. **The game's camera**, still to check against the game: the arrival pan. In the game the
   player may turn the camera (Q/W) only where the level allows it, level presets often
   overrule it, and a camera icon with a red X shows when turning is refused.
5. **The end-of-level cutscene**: breaking the last cage plays the Teensie and Rayman scene
   through to the end (user, 2026-11-02). When Rayman shouts "Stop!" at the arguing Teensies and
   spreads his hands, that animation loops (repeats the hand spreading) instead of playing once. Also: his pupils still
   show a little when he closes his eyes.
6. **Performance checks**: the 3 fps drop at `--camera -69.3,207.6,2.0,297.6,-0.4` has a fix in
   (the camera's obstacle search no longer runs at a distance, and the segment casts use the
   collision grid — 155 ms to 6 ms a frame); the user should confirm it runs smoothly. The
   big-nosed flier's buzz is still louder than in the game's and goes on after it flies up and
   vanishes; the game is silent there. The user will also try a Linux laptop with a Fermi-era
   Quadro 2000M.

**Later in Phase 3 / Phase 4**

- An optional modern camera, turned freely with the right stick like in today's platformers,
  next to the game's own guided camera. It will see what the guided camera never shows, which
  needs handling: half-built cutscene sets (e.g. above the waterfall in The Woods of Light,
  `--camera 36.2,328.9,-3.7,-50.1,24.2`) and painted "paper" trees that simply end.
- Phase 4 starts with the real intro cutscene in front of the first-visit start: after the Ubisoft
  logo the camera pans over the dark sea to the pirate ship (Jail_10, Jail_20), then the crash into
  the woods. `tools/r2record.py` records a playthrough of the real game, a file per topic (camera,
  actors, text boxes, sounds, music, story bits); recordings of a whole new game are in
  `H:\r2export\live\run3` and `run4`.

## Phase 4: the whole game

Everything the PC game has, working before we port it anywhere:

- The real intro cutscene
- Save games (reading the real saves' story bits)
- Menus. The pause menu already half-runs from its own scripts: Esc while playing Learn_10
  brings up the menu's swirl in front of Rayman, and it stays there (user, 2026-10-30)
- Cutscenes
- All the levels, from start to end
- Widescreen and high resolutions, plus modern quality-of-life options

## Phase 5: Android

- SDL3 Android project, touch controls overlay
- Picking the game data folder via Android's storage access framework
- Performance pass for mobile GPUs

## Later

- A modern renderer: Vulkan or Direct3D 12, keeping OpenGL (3.3 / ES 3.0) for older devices. The
  current OpenGL renderer is the first step and the fallback
- A modern lighting mode that keeps the art direction, and HDR, designed together (both change how
  the lights are used)
- Controller support with the right button icons
- Sharper logos: the Ubisoft logo and the tiled Rayman 2 logo look rough in the original; upscale
  them, or use a better texture if the data has one (optional, the user's wish)
- Antialiasing (MSAA, with alpha to coverage for cut-out textures)
- Smoother cut-out textures: colour-keyed and alpha-tested textures have jagged, "cut out with
  scissors" edges (Globox's eyes); smoothing their edges, as we did for the leaves in 2c
- Richer water while running (optional, off by default): the game only leaves ripples when Rayman
  runs through shallow water, while jumping in throws droplets apart (particle kind 60) and
  bubbles; a few of those droplets and bubbles on each step would look livelier (user's idea)
