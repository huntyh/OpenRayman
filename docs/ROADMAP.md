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
  Walking, turning and walls feel right (user, 2026-10-28). Seen in the game, still to do:
  - Jumping doesn't work yet.
  - Animations play too fast: idle, blinking (also too often, the loop looks incomplete), shooting
    (the hand snaps back; the forehand and backhand throws alternate in the game). Likely the same
    cause: the exe plays a state at its own rate (state +0x29) scaled by procedure 89 (3D data
    +0xE0), and we use the animation's speed.
  - Learn_10 has two starts: a new game (cutscene: Rayman falls from the pirate ship, runs down a
    path and jumps in) and the return from the hub (out of the portal, what runs now). On the return,
    Rayman should stay in his spin state 04/00 +0x1210 for about 3 s before he falls; ours falls at
    once (H:/r2export notes portal.md, intro.md: not his start-up procedures; next suspect the camera
    actor's intro).
  - The portal's sparkling particles; splashes when he lands in water.
  - Seen from afar, faint straight lines like a crosshair on X and Y around Rayman, turning with him.
  - Hover timed by the user: 1.4-1.6 s in both games, so its speed is right. Near the cliff by the
    spawn he can stop in mid-air while hovering and rise a little (probably the sloped cliff face
    taken as floor).
  - Next: the game camera following Rayman (camera actor 5's scripts, procedures 491-500 and the
    other handlers it reaches). The optional modern camera waits until the game is nearly complete
    (with HDR and the like).
    In the game the player may turn the camera left/right (Q/W) only where the level allows it;
    level camera presets often overrule it, and a camera icon with a red X shows when turning is
    refused (user).
- The game's camera following him
- An optional modern camera, turned freely with the right stick like in today's platformers, next
  to the game's own guided camera. It will see what the guided camera never shows, which needs
  handling: half-built cutscene sets (e.g. above the waterfall in The Woods of Light,
  `--camera 36.2,328.9,-3.7,-50.1,24.2`) and painted "paper" trees that simply end

**Milestone 3d: sound**

- BNM sound banks: sound effects and voices, played when the scripts ask for them
- The adaptive music sequencing

## Phase 4: the whole game

Everything the PC game has, working before we port it anywhere:

- Save games
- Menus
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
