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

## Phase 2: one level, looking and moving like the game (in progress)

Goal: The Woods of Light (`Learn_10`) looks like the real game and comes alive, without gameplay
yet: everything that moves on its own moves, but nothing reacts to Rayman.

**Milestone 2a: the viewer ✅ (merged into master, 2026-10-26)**

- ✅ Static geometry with textures, a free-fly camera, starting from where the game's camera starts
- ✅ Level picker for all 70 levels (Page Up / Page Down)
- ✅ Actors (characters, props, platforms) posed at the first frame of their animation
  ([format notes](formats/animation.md)), placed with the game's own transforms
- ✅ Sprites (Lums and other glows), and textures' see-through, glowing and mirrored modes
- ✅ No seams at texture edges; P copies the camera position for bug reports

**Milestone 2b: things move ⬅ we are here**

- Animation playback: every actor loops its current animation (Lums bobbing, Teensies, Rayman
  idling), interpolating between keyframes
- Moving textures: the water stream, waterfall and mist scroll the way the game scrolls them
- Effects: the drifting fireflies and other particles

**Milestone 2c: the game's light and sky**

- The game's own lighting: per-vertex colours baked into the level, and sector lights
- The sky and the distant background
- Shadows under characters, water and glass transparency done the game's way
- Later, as an option: a modern lighting mode that keeps the art direction

Phase 2 ends when Learn_10 side by side with the real game looks the same, apart from the parts
that need gameplay.

## Phase 3: make it a game

- BNM sound banks: sound effects, voices, and the adaptive music sequencing
- Actor behaviour: states and transitions (e.g. the cages hang from their chains once their
  scripts pick the hanging state)
- Collision and physics
- The AI bytecode interpreter: behaviours, macros, reflexes, DsgVars
- Input mapped to the game's controls (keyboard, gamepad, touch)
- Rayman himself walking around a level

## Phase 4: Android

- SDL3 Android project, touch controls overlay
- Picking the game data folder via Android's storage access framework
- Performance pass for mobile GPUs

## Later

- A modern renderer: Vulkan or Direct3D 12, keeping OpenGL (3.3 / ES 3.0) for older devices. The
  current OpenGL renderer is the first step and the fallback.
- Save games, menus, cutscenes and the remaining levels
- Widescreen, high resolutions, HDR, modern quality-of-life options
- Controller support with the right button icons
