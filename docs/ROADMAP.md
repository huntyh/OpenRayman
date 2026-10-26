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

## Phase 1: level data

- BNM sound banks (sound effects and voices)
- SNA/GPT/PTX/RT* level files: block decoding, pointer relocation, then the object graph
  (sectors, geometric objects, materials, "super objects" hierarchy)
- `ortool level <name>` to dump a level's contents

## Phase 2: render one level

- Static geometry with textures, a free-fly camera, starting with The Woods of Light (`Learn_10`)
- Level picker for all 70 levels
- Lighting, transparency and sky

## Phase 3: make it a game

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
