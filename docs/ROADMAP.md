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

## Phase 3: make it a game ⬅ in progress, alongside Phase 4

Goal: Rayman runs around The Woods of Light (`Learn_10`) as in the real game, with its characters
behaving as their scripts say. Everything targets Learn_10 first; the other levels get their turn
once it plays right (they have issues of their own, found on a first look). Phase 4 work runs
alongside it, the menu and the pirate ship's two intro levels first.

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
- ✅ **The main menu** ([formats/menu.md](formats/menu.md)): the language prompt and its run-time
  selection (remembered between launches since 0b49d29), New Game through the name screen into
  Jail_10, the Exit prompt (Yes quits), the white-to-colour text gradient with the selected
  entry's height pulse, the menu camera's path, the butterfly placed at a screen point, and the
  startup theme played from the level's GPT.
- ✅ **The cutscene black bars**: they slide in and out with a scene (procedure 314 sets the world
  view's bounds).
- ✅ **The intro, first pieces**: the Ubisoft logo and the tiled Rayman 2 logo as screen objects
  (smooth edges, turning with the camera), the text boxes' fades, and the logo arc's saved vertex
  colours (procedure 393); the animation morph records are read and blended (the Teensy's hat).
- ✅ **The exe's per-frame actor order**: when Rayman changes sector the run order relinks his
  sector first, so the intro's "SLAVES ON BOARD" counters no longer flash
  ([formats/scripts.md](formats/scripts.md)).
- ✅ **Sector rendering**: only the camera's sector and its +0x24 drawn-with list are drawn, with
  fog per drawn sector, and `--screenshot` draws solid scenery again; the texture colour keys
  (+0x2C) and the one-sided materials (flag 0x400) match the game
  ([formats/geometry.md](formats/geometry.md)).
- ✅ **Reading real saves** ([formats/saves.md](formats/saves.md)): `--save 1..3` restores a slot,
  `ortool save` decodes one, and saves live in OpenRayman's own folder (per-user, or `portable.txt`
  beside the executable, `OPENRAYMAN_SAVE_DIR` overrides); `ortool mksave` writes test slots.
- ✅ **Woken actors only**: an actor thinks only after the exe's wake pass moves it to the active
  world, with the sector wake lists, so the scripts' pacing holds
  ([formats/scripts.md](formats/scripts.md)).
- ✅ **Revisit settling**: a `--save` start runs four ticks before the first frame (no flash of
  Rayman at the new-game spot), and a teleport moves a dynamic actor at once (its dynamics block
  too).
- ✅ **The jail levels, Jail_10 and Jail_20** (board #33..#36, #65, #71, #73, #75, #79, #83, #84,
  #85, #88, #90..#92, #95): Jail_10's sea-and-ship pan with its far backdrop, the cave's lighting
  and lightning flash, the Ubisoft and the Rayman 2 logo, the sky and fog there, and no falling
  through the map after the cage; then Jail_20's cell with its bars, laser bars and cage light,
  Rayman and Globox in it, the chute with its ceiling and fan and its fall backdrop, and the
  chute's end camera and cutscene.
- ✅ **Wide windows** (#93, #109): the HUD stays at the screen edges at 4:3 proportions; all other
  screen text, boxes and pictures sit as one block in the centred 4:3 area (docs/formats/text.md).
- ✅ **Particles, sounds and animation details**: particle kinds 30, 46, 49 and 99 (the Jail_20
  grate's burst and a shot's stars, #99), actors' own sound distances with the camera's hearing
  list (the chute fan is silent in the cell), and animation transitions that keep their place, so
  starting to move mid-throw no longer replays the throw and its sounds (#87).
- ✅ **Slopes and jumps** (#32, #89, #96): jumps on slopes and from the slide, a roof that slopes
  across the move stops the body, and the slide steers and throws its dust.

Dated details of everything above, and the user's look-by-look feedback, are in
[ROADMAP_LOG.md](ROADMAP_LOG.md).

**Open.** The open items are tracked on the maintainers' own task board (Vikunja), which is the
source of truth and has the detail; in short:

- 3c leftovers: the slope's acceleration (#1), the dialog text's exact size and left padding (#2),
  the blue "next" arrow's spot (#62), the HUD counters (#3), the font running off the screen (#6)
- The game's camera (#8, #9, #13, #14, #63): the "turning refused" icon, the exe's orbit search
  around obstacles, snapping back to Rayman, the cutscene camera modes, Rodeo_10's narrow hole
- Sound (#7): the theme transitions' fades, and music and effect levels (loudness by distance
  follows the exe already)
- The cutscene black bars: an option to turn them off, with darker text behind (#5)
- Unported exe pieces: particle kinds, their per-particle draw value and screen dimming (#17);
  the end scene's pupils (#10, not reproduced in play, the user to check)
- The pirate ship (Jail_20): the slide's steering and collision (#86), a frame of the wrong pose at
  the slide's end (#97), the pirates' heads in front of their spines (#98), Rayman and Globox
  snapping left in the cell (#100)
- The other levels' first issues: Learn_31's creature and its trailing segments (#66, #81),
  Bast_20's pond and piranhas (#82), Bast_22's barrels and its silent shots and steps (#68),
  Ski_10's swamp creatures and missing kill zone (#69), Chase_22's exit animation and camera
  hunting (#70)
- Performance measured with the profiler and tuned (#12)

## Phase 4: the whole game ⬅ in progress, alongside Phase 3

Everything the PC game has, working before we port it anywhere.

**Done** (the closed cards, one line each; the detail is in the Phase 3 list):

- The menu: the language prompt, now remembered between launches (#56), the menu font (#57), the
  Exit prompt (#58), the menu camera's pans (#59), F1 and Esc's elements (#21)
- The intro: the sea pan without its jitter (#33), the logos (#65, #75), the counters (#76, #77),
  and the text colours (#80)
- The cutscene bars, engine side and sliding in (#4, #16)
- Reading the real saves (#19, #37)

**Open**, in the order the user set (2026-11-02):

1. The main menu ([formats/menu.md](formats/menu.md)): the language prompt, the main menu and
   New Game -> name -> Jail_10 run from the level's scripts (user confirmed, 2026-11-03). New Game
   already gets through the name screen into Jail_10, whose intro plays (the name screen highlights
   the whole name instead of the picked letter, #105). Left: writing the save
   (#24), the Load / Delete / Options screens (#25), the menu's scenery, a per-character flash on
   the menu text (#26), and the music still missing in a few places (#31).
2. The new-game intro cutscene in front of the first-visit start (Jail_10, Jail_20); the logos,
   text boxes and bars are in (see 3c), and there is a diagnostic `--start intro` for Learn_10's
   opening scene (#23).
3. Save games: reading the real saves is done (`--save 1..3`,
   [formats/saves.md](formats/saves.md)); writing one on New Game is left.
4. The pause menu and F1 Murfy's help (board #38).
5. The Hall of Doors: the way from one level to the next (board #39).
6. Optional autosave, off by default (into IMPROVEMENTS when built; board #40).

Then: cutscenes (board #41), all the levels from start to end (#42), widescreen and high
resolutions with quality-of-life options (#43, and ultrawide later #94), room for our own options
inside the game's Options menu (#61), an optional modern free camera (#22).

Vikunja is the source of truth for the open work; the card numbers point to the detail.

## Phase 5: Android

- SDL3 Android project, touch controls overlay (board #44)
- Picking the game data folder via Android's storage access framework (#45)
- Performance pass for mobile GPUs (#46), after batching the draw calls (#55)

## Later

- A modern renderer: Vulkan or Direct3D 12, keeping OpenGL (3.3 / ES 3.0) for older devices
  (board #47); the current OpenGL renderer is the first step and the fallback
- A modern lighting mode that keeps the art direction, and HDR, designed together (board #48;
  both change how the lights are used). Two separate things to research first: rendering in high
  dynamic range (lighting in floating point, then tone mapping down to the screen) and HDR output
  (HDR10 / scRGB to an HDR display, which needs the swap chain and the OS to cooperate)
- Gamepads through SDL3, the Steam Controller first (#101; SDL is 3.4.20), with the right button
  icons (#49) and optional rumble, our own extra (#102)
- Sharper logos (#50): the Ubisoft logo and the tiled Rayman 2 logo look rough in the original;
  upscale them, or use a better texture if the data has one (optional, the user's wish)
- Antialiasing (#51): MSAA, with alpha to coverage for cut-out textures, first; SMAA as a cheap
  post-process option. TAA only if it can be done without ghosting (the user's condition); it
  needs motion vectors for every moving thing (skinned actors, scrolling and animated textures,
  sprites), and the game's many see-through and additive surfaces are where TAA usually smears
- Smoother cut-out textures (#52): colour-keyed and alpha-tested textures have jagged, "cut out
  with scissors" edges (Globox's eyes); smoothing their edges, as we did for the leaves in 2c.
  The menu's foliage has the same kind of edge (#60)
- Richer water while running (#53, optional, off by default): the game only leaves ripples when
  Rayman runs through shallow water, while jumping in throws droplets apart (particle kind 60) and
  bubbles; a few of those droplets and bubbles on each step would look livelier (user's idea)
- Ultrawide (21:9 and beyond, #94), and mouse selection in the menu with a typed save name and a
  Continue entry (#64, #74)
- Rumble, our own addition, with the moments the user picks (#102); the Lightning cards in
  Jail_10 showing their edges, with optional soft edges (#103)
- The Globox kids' scene in Learn_10 replays when the level is revisited: the real game does that
  too (an original bug, kept on purpose); an optional fix may come later (#78)
