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
- Moved on: the fireflies (done in 3b), a modern lighting mode with HDR (Later)

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
  ([formats/generated.md](formats/generated.md)); homing (user confirmed 2026-11-02)), the
  long-idle animations (the "basketball" one), and the game camera following him: scripts
  fill the parameter blocks, the solve and mechanics move it, Q/W turning swings round in pi/8
  steps like the exe, camera collision, and an interim obstacle search the user is happy with
  ([formats/camera.md](formats/camera.md)).
- ✅ **3d sound**: the BNM format and reader ([formats/sound.md](formats/sound.md), all 405 banks;
  `ortool bank` / `ortool sound`), effects and voices from the scripts and animations, and the
  music: themes play segment by segment on the recorded game's beat, started by sectors as Rayman
  walks in (the woods' theme at the hand-over) and by scripts (the Globox kids' scene, the Teensie
  end scene). Music and effects sit at the game's own levels, measured from recordings of the real
  game with `tools/r2audio.py` (user, 2026-11-02: "way better").
- ✅ **First-visit start**: `--start new` works (user 2026-11-01): Rayman starts where the
  intro hands him over (`first_visit.h`; the intro itself is Phase 4), the tutorial cage hangs
  until the game's CD check passes as on the GOG release, Murfy appears and whispers, the cage
  breaks, its Lum flies its loops and collecting it opens the trapdoor.
- ✅ **Standing on actors** ([formats/collision.md](formats/collision.md)): actors' collision
  zones and animated parts count as floor, wall and ceiling, so the tutorial's closed trapdoor
  holds Rayman, and moving supports carry him. Ceilings (user retested 2026-11-02): the bridge,
  the ears and the Globox kids' cave roof all stop him.
- ✅ **Dialogs and the HUD, first pass** (user 2026-11-02): dialogs in the game's own glyphs and
  colours, sharp and fast (OpenGL), Murfy's box and portrait, the tablet tips coming back and F1
  replaying Murfy; the HUD's icons and Rayman's health bar (particle kinds 50/120); a breaking
  cage's golden burst (kinds 5/45); the revisit start looking like a finished level (interim
  snapshot, [formats/saves.md](formats/saves.md)).

Dated details of everything above, and the user's look-by-look feedback, are in
[ROADMAP_LOG.md](ROADMAP_LOG.md).

**Open.** From 2026-11-04 the open items are tracked on the maintainers' own task board; in short:

- 3c leftovers (the slope's acceleration), the dialog text's size and padding, the HUD counters
- The cutscene black bars, as the game does them (and later an option to turn them off)
- Sound: theme fades, loudness by distance, music and effect levels
- The game's camera: the "turning refused" icon, the exe's orbit search around obstacles
- Unported exe pieces: script handlers Learn_10 still calls, particle kinds, animation morphs
- Flying actors bobbing where the game's hover, F1's glossary, an optional modern camera

## Phase 4: the whole game

Everything the PC game has, working before we port it anywhere. The order the user set
(2026-11-02):

1. The main menu ([formats/menu.md](formats/menu.md)): the language prompt, the main menu and
   New Game -> name -> Jail_10 run from the level's scripts (user confirmed, 2026-11-03). Left:
   writing the save, the Load / Delete / Options screens, the menu's scenery.
2. The new-game intro cutscene in front of the first-visit start (Jail_10, Jail_20).
3. Save games: writing one on New Game, reading the real saves ([formats/saves.md](formats/saves.md)).
4. The pause menu and the F1 glossary.
5. The Hall of Doors: the way from one level to the next.
6. Optional autosave, off by default (into IMPROVEMENTS when built).

Then: cutscenes, all the levels from start to end, widescreen and high resolutions, plus modern
quality-of-life options.

## Phase 5: Android

- SDL3 Android project, touch controls overlay
- Picking the game data folder via Android's storage access framework
- Performance pass for mobile GPUs

## Later

- A modern renderer: Vulkan or Direct3D 12, keeping OpenGL (3.3 / ES 3.0) for older devices. The
  current OpenGL renderer is the first step and the fallback
- A modern lighting mode that keeps the art direction, and HDR, designed together (both change how
  the lights are used). Two separate things to research first: rendering in high dynamic range
  (lighting in floating point, then tone mapping down to the screen) and HDR output (HDR10 / scRGB
  to an HDR display, which needs the swap chain and the OS to cooperate)
- Controller support with the right button icons
- Sharper logos: the Ubisoft logo and the tiled Rayman 2 logo look rough in the original; upscale
  them, or use a better texture if the data has one (optional, the user's wish)
- Antialiasing: MSAA, with alpha to coverage for cut-out textures, first; SMAA as a cheap
  post-process option. TAA only if it can be done without ghosting (the user's condition); it
  needs motion vectors for every moving thing (skinned actors, scrolling and animated textures,
  sprites), and the game's many see-through and additive surfaces are where TAA usually smears
- Smoother cut-out textures: colour-keyed and alpha-tested textures have jagged, "cut out with
  scissors" edges (Globox's eyes); smoothing their edges, as we did for the leaves in 2c
- Richer water while running (optional, off by default): the game only leaves ripples when Rayman
  runs through shallow water, while jumping in throws droplets apart (particle kind 60) and
  bubbles; a few of those droplets and bubbles on each step would look livelier (user's idea)
