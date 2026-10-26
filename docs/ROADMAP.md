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

## Phase 2: render one level (in progress)

- ✅ Static geometry with textures, a free-fly camera, starting with The Woods of Light (`Learn_10`),
  from where the game's camera starts
- ✅ Level picker for all 70 levels (Page Up / Page Down)
- ✅ Actors: characters, props and platforms, posed at the first frame of their animation
  ([format notes](formats/animation.md))
- Sprites (Lums and other glows)
- Lighting, transparency and sky

## Phase 3: make it a game

- BNM sound banks: sound effects, voices, and the adaptive music sequencing
- Actors with animation (families, states, channels)
- Collision and physics
- The AI bytecode interpreter: behaviours, macros, reflexes, DsgVars
- Input mapped to the game's controls (keyboard, gamepad, touch)
- Rayman himself walking around a level

## Phase 4: Android

- SDL3 Android project, touch controls overlay
- Picking the game data folder via Android's storage access framework
- Performance pass for mobile GPUs

## Later

- Save games, menus, cutscenes and the remaining levels
- Widescreen, high resolutions, modern quality-of-life options
