# OpenRayman

An open source reimplementation of the *Rayman 2: The Great Escape* engine in modern C++,
targeting Windows, Linux and Android. Free for anyone to use, under the GPL/LGPL (see [License](#license)).

This continues [imaginaryPineapple's OpenRayman](https://github.com/imaginaryPineapple/OpenRayman)
(2016), rebuilt on a current toolchain. See [docs/ROADMAP.md](docs/ROADMAP.md) for where it's heading.

> **OpenRayman is an unofficial fan project. It is not affiliated with, endorsed by, or connected to
> Ubisoft in any way.** Rayman and Rayman 2: The Great Escape are trademarks of Ubisoft
> Entertainment. You need your own legally obtained copy of the game (GOG) to use OpenRayman.

## Where it stands

OpenRayman opens every level of the original game and lets you fly through it, looking like the
real thing. You can't play yet: making it a game is the current phase.

| Phase | What it's about | Status |
| --- | --- | --- |
| 0. Foundation | Modern toolchain, texture and music readers, test tools | ✅ Done |
| 1. Level data | Reading the levels: scenery, scene tree, pointers, all 70 levels | ✅ Done |
| 2. Looking like the game | Characters, animation, the game's lighting, fog and sky | ✅ Done |
| 3. Making it a game | Sounds, collision, the game's scripts, Rayman walking around | ⬅ **We are here** |
| 4. The whole game | Saves, menus, cutscenes, every level, widescreen | Planned |
| 5. Android | Touch controls, phone performance | Planned |
| Later | Vulkan or Direct3D 12, modern lighting and HDR, controller icons | Planned |

What works today:

- All 70 levels load, with their scenery, textures and characters where the game puts them.
- Things move: characters play their animations, water and waterfalls flow (animated and
  scrolling textures).
- The game's own scripts run: the cages hang from their chains and swing, the mushrooms dance, the
  Lums float and turn towards Rayman, the fireflies hover, specks float up in the dark.
- The game's own lighting and fog, worked out from the original program, plus the sky, glass,
  see-through water and characters' shadows. The Woods of Light look the same as the real game
  side by side.
- The game's music.
- It is becoming playable: in the first level Rayman walks, runs, jumps, grabs ledges and shoots
  (the shots home in on targets); the sound effects and voices play; the game's music themes play;
  dialogs and the HUD are on screen in the game's own letters; the game's camera follows him.
- A texture browser, `ortool` for looking inside the game's files, and `tools/r2record.py`, which
  records a playthrough of the real game for studying it.

Not yet: some dialog padding, counters for lums and cages are not in the right spot, saves, and basically everything
beyond the first level. See [docs/ROADMAP.md](docs/ROADMAP.md) for the details. It renders with
OpenGL today; the plan is Vulkan or Direct3D 12, keeping OpenGL for older devices.

Where OpenRayman deliberately does something differently from the game (no shadow limit, soft leaf
edges, smoother animation loops, ...), it's listed in [docs/IMPROVEMENTS.md](docs/IMPROVEMENTS.md).

Some images from OpenRayman:

|Murfy Tutorial| Woods of Light |
|---|---|
|<img width="2560" height="1440" alt="Base Profile Screenshot 2026 11 05 - 18 22 28 10" src="https://github.com/user-attachments/assets/b5975001-5dfc-46b3-88d7-e03a3a555b43" />| <img width="2560" height="1440" alt="Base Profile Screenshot 2026 11 05 - 18 26 48 11" src="https://github.com/user-attachments/assets/74148afc-40de-417f-b943-1d7521b5e23a" />|

| Ly | The Machine |
|---|---|
|<img width="2560" height="1440" alt="Base Profile Screenshot 2026 11 05 - 18 32 03 84" src="https://github.com/user-attachments/assets/2867cac5-8412-432e-b03b-d68ad7059d01" />|<img width="2560" height="1440" alt="Base Profile Screenshot 2026 11 05 - 18 32 59 07" src="https://github.com/user-attachments/assets/03a5b82e-a0f5-4562-9e02-ac2545b6b05c" />|


## You need the game

OpenRayman ships no game data. It reads the files of your own copy of Rayman 2 (PC), for example the
[GOG version](https://www.gog.com/game/rayman_2_the_great_escape). Nothing is converted or copied:
point OpenRayman at the install folder and it reads it as is.

## Building

### Windows

Install [Visual Studio 2022](https://visualstudio.microsoft.com/) or the free *Build Tools for
Visual Studio 2022* with the **Desktop development with C++** workload. It includes CMake and Ninja.
Then, in PowerShell from the repository folder:

```powershell
.\build.ps1             # debug build, then runs the tests
.\build.ps1 -Release    # optimized build
```

Programs end up in `build\windows-debug\bin` (or `windows-release`).

### Linux

You need a C++20 compiler (GCC 13+ or Clang 17+), CMake 3.25+, Ninja, and the development packages
SDL3 needs for windowing and audio (on Debian/Ubuntu: `libx11-dev libxext-dev libwayland-dev
libxkbcommon-dev libegl-dev libpulse-dev libasound2-dev`).

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Dependencies (SDL3, glm, doctest) are downloaded automatically during the first configure.

## Running

```
openrayman [--game "<Rayman 2 folder>"] [--level <name>]
```

For checking things by eye there are also `--camera x,y,z,yaw,pitch` (as P copies it), `--time <seconds>`,
`--screenshot <file>`, `--actor-state <node>:<state>` and `--behaviour <node>:<index>` (start an actor's
scripts in another behaviour, e.g. `--behaviour 4:1` for The Woods of Light's floating specks),
`--lighting on|off`, `--luminosity <n>`, `--edge-clamp on|off` and
`--collision on` (what the level collides with, coloured by surface type; L, F6/F7, F2 and C in the viewer).

Gameplay (The Woods of Light first):

```
openrayman --game "<Rayman 2 folder>" --level Learn_10 [--start new|portal] [--story-bits 1095,1143]
            [--keys <name:from-to,...>] [--camera x,y,z,yaw,pitch]
```

`--start new` starts a first visit (Rayman at the hand-over, the tutorial cage waiting); without
it the start is as if the level were reached from the Hall of Doors. `--story-bits` sets a save's
story bits at load, `--keys` holds keys on schedule as in `ortool run`, and `--camera-log <file>`
writes the camera's position every tick as CSV. Without `--camera` it plays seen through the
game's camera.

On first start without `--game`, a folder picker asks for your Rayman 2 folder and remembers it.
It opens The Woods of Light (`Learn_10`), or the level given with `--level`, seen from where the
game's camera starts.

In a level:

| Key | Action |
| --- | --- |
| W A S D | Move |
| Hold right mouse button | Look around |
| Space or E / Ctrl or Q | Up / down |
| Shift | Move 4× faster |
| Mouse wheel | Change movement speed |
| Page Down / Page Up | Next / previous level |
| Home | First level |
| R | Back to the start position |
| P | Copy the current position (as `--level ... --camera ...`) to the clipboard, for bug reports |
| F3 | Pause or resume the animations |
| L | Switch between the game's lighting and fog and a plain light |
| F6 / F7 | Darker / brighter, like the game's luminosity setting |
| F5 / F4 | Put the actor you're looking at into its next / previous state (Shift: its whole family), to preview animations the game's scripts would pick |
| G | Turn the level's scripts on and off |
| T | Switch to the texture browser and back |
| Tab | Play: the keyboard goes to the game (arrows, A jump, Space shoot, Shift walk, Q/W camera, Ctrl recentre) until Tab again |

In the texture browser:

| Key | Action |
| --- | --- |
| Left / Right, mouse wheel | Previous / next texture |
| Up / Down, Page Up / Page Down | Jump 25 textures |
| Home / End | First / last texture |

Anywhere: M / N for the next / previous music track, Space to pause it (in the texture browser),
O to choose a different game folder, F11 for fullscreen, Esc to quit. The window title shows where
you are.

Performance: F8 shows where each frame's time goes (scripts, movement, drawing, the GPU, waiting
for the monitor) and how many draw calls and triangles it took, updated every second;
`OPENRAYMAN_PROFILE=<file.csv>` logs the same once a second. F9 cycles vsync: on (waits for the
monitor, but shows a late frame at once instead of a whole refresh later, where the driver
supports it), strict (always waits) and off. `--vsync on|strict|off` and `--fps-limit <n>` set
them at start; with an adaptive-sync (FreeSync, G-Sync) monitor, a cap a few frames under the
refresh rate is the smoothest. Use the release build (`.\build.ps1 -Release`) for judging speed:
the debug build is several times slower.

### ortool

`ortool` is a command line tool for checking the game data:

```
ortool info "<game folder>"                 Summary: levels, archives, music
ortool textures "<game folder>" <out>       Export every texture as PNG
ortool music "<game folder>" <out>          Export every music file as WAV
ortool bank "<game folder>" <bank|all> [out] A BNM sound bank's events, and its samples exported
ortool sound "<game folder>" <id>           Resolve a bank << 16 | event id to what it plays
ortool level "<game folder>" <name|all>     Load a level and check all its pointers
ortool gpt "<game folder>" <level>          The level header: Fix actors, world roots, animation bank
ortool obj "<game folder>" <level> <out>    Export a level's scenery as OBJ with textures
ortool scene "<game folder>" <level>        Print a level's scene tree
ortool actors "<game folder>" <level>       The level's actors and their 3D data
ortool pose "<game folder>" <level>         The main actor's animation channels at frame 0
ortool motion "<game folder>" <level>       Every actor: node, position, animation, how far its parts move
ortool states "<game folder>" <level> <node> A family's states and their animations
ortool anim "<game folder>" <level> <index> <frame> One animation bank entry at one frame
ortool lights "<game folder>" <level>       Sectors and their lights
ortool collide "<game folder>" <level|all> Collision data: collide sets, element types, surface flags
ortool brain "<game folder>" <level> [node|block+offset] The scripts of one actor by scene node or by memory address
ortool run "<game folder>" <level> <ticks> [node] [keys] Runs the level's scripts for that many 60 Hz ticks and lists what changed (keys held as Up:60-200,A:100-110)
ortool camrun "<game folder>" <level> <ticks> <keys> The same run, with the game camera's blocks and view each tick
ortool input "<game folder>" <level>        The input actions and the keys each one tests
ortool refs "<game folder>" <level> <block> <hexoff> Every pointer that leads to this spot, walked backwards
ortool mesh "<game folder>" <level> <block> <hexoff> What a physical object shows: sprites, elements, material colours
ortool peek "<game folder>" <level> <block> <hexoff> <words> Raw words at a memory spot
ortool dsb <file.dsb>                       Print a DSB script
ortool cnt <file.cnt>                       List a texture archive
```

`ortool run` also reads settings from the environment: `ORTOOL_PLACE=x,y,z,yaw@tick` puts Rayman
somewhere at that tick (several separated by `;`), `ORTOOL_START=new` starts a first visit,
`ORTOOL_COST=1` ranks the actors by how long their scripts take, `ORTOOL_VARS=node:i,j` prints an
actor's variables after the first tick, `ORTOOL_IDS=1` lists every actor's id, flags and type,
`ORTOOL_TEXT=1` logs each dialog event, and `ORTOOL_SHOTS=1`, `ORTOOL_TARGETS=1`,
`ORTOOL_SOUNDS=1`, `ORTOOL_CONTACTS=1`, `ORTOOL_ACTOR_SURFACES=1` and `ORTOOL_NEAR=x,y,r` add
traces and checks.

## Testing

See [docs/TESTING.md](docs/TESTING.md). Setting `OPENRAYMAN_GAME_DIR` to your install folder also
runs the unit tests against your real game files, and checks `ortool`'s output for a few commands
against pinned checksums (only the checksums are in the repository). After a deliberate change, the
failing check saves the new output in `build/<preset>/tests/ortool_output` and prints its checksum to
pin in `tests/CMakeLists.txt`.

## Layout

| Folder | Contents | License |
| --- | --- | --- |
| `libopenrayman/` | Readers for the game's file formats, no dependencies | LGPLv3 |
| `src/` | The engine | GPLv3 |
| `tools/ortool/` | Data inspection tool | GPLv3 |
| `tests/` | Unit tests (doctest) | GPLv3 |
| `lib/` | Vendored glad (OpenGL loader) and stb_image_write | their own, see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) |
| `android/` | The 2016 Android launcher, kept for reference until the Android port (Phase 5) | GPLv3 |
| `docs/` | Roadmap, testing guide, [file format notes](docs/FORMATS.md) | GPLv3 |

## How this is made

This continuation of OpenRayman is written by AI. Claude, an AI model by Anthropic working in
Claude Code, leads the code, the reverse engineering of the file formats and the documentation, and
reviews everything that goes in. It hands bounded jobs to a few helpers:

- **GPT** (OpenAI, through Codex): tracing the executable and implementing features from those
  findings (standing on actors, the dialogs and their glyphs, the arrival camera, ...)
- **DeepSeek** (through Reasonix): smaller exact investigations of the executable's handlers
- **Qwen**, running locally: routine pieces such as boilerplate and test scaffolds

Their results are always checked against the game data and the executable before use.
**huntyh** runs the project: deciding what to build, testing every step against the real game and
reporting what looks wrong, and keeping the project's rules.

Every commit made this way says so in its message ("Co-Authored-By: Claude", plus the helper when
one did the work, e.g. "Co-Authored-By: GPT-6.1 Terra"), so the history shows exactly which work
came from where.

## FAQ

### Why?

I wanted to play the game on my phone and emulating it would've been lazy, so we're doing it this way.
And it seemed like a fun project.

### Is this vibe-coded? 

Yes. [How this is made](#how-this-is-made).

### Will this exactly match the original game?

I'm not sure if that's possible, there are a lot of things that need to be figured out, it won't ever be 1:1. E.g. sliding on slopes has a fractional acceleration difference in our build, tiny differences like that.

### When will it be ready?

Hard to say, there are a lot of things to do that will involve me playing through both the original and this game probably several times.
I'll try to aim for a release so people can try it though.

### What's the point?

I wanted to preserve this game as I played it a lot in my childhood, though I mostly played the PS1 version which is... different.
This project will add improvements and do its best to preserve - and boost - the original's art direction.
It already runs at uncapped framerates, does texture clamping better than the original and even fixed a few bugs the original had. The goal is to bring the game on phones, Linux, your fridge, whatever.


## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) first: it covers how we
keep the project free of Ubisoft assets and copied code.

## License

OpenRayman is free software: anyone may use, study, share and modify it.

- **The engine and tools** (everything outside `libopenrayman/`) are licensed under the
  **GNU General Public License v3**, inherited from the original OpenRayman. See
  [LICENSE_openrayman](LICENSE_openrayman).
- **libopenrayman** (everything in `libopenrayman/`), the library that reads the game's files, is
  licensed under the **GNU Lesser General Public License v3**, so other tools can use it more freely.
  See [LICENSE_libopenrayman](LICENSE_libopenrayman).
- Third party components keep their own licenses. Full texts are in
  [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

These licenses cover OpenRayman's code only. They grant nothing for Rayman 2's game data, which
remains Ubisoft's property.

## Credits

OpenRayman stands on the work of many people in the Rayman community who reverse engineered and
documented this game over two decades. Thank you.

### Original project

- **imaginaryPineapple** (Hannes Mann): created [OpenRayman](https://github.com/imaginaryPineapple/OpenRayman)
  in 2016, including the first open decoders for CNT, GF, APM and DSB files and the original Android
  launcher. This project continues that work under the same licenses.

### This continuation

- **huntyh**: project lead, testing and bug reports (Teensies aren't upside-down anymore)
- **Claude** (Anthropic): code, format research and documentation, see [How this is made](#how-this-is-made).
- **GPT** (OpenAI), **DeepSeek** and **Qwen**: helpers for executable tracing, features and
  routine pieces, see [How this is made](#how-this-is-made).

### Rayman 2 research

- **szymski** (Szymon Jankowski): [Rayman2Lib](https://github.com/szymski/Rayman2Lib), tools for
  Rayman 2 modding and content extraction. The original libopenrayman drew heavily on it, and our
  understanding of the formats builds on it (MIT, see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)).
- **Synthesis**: [Ray2Get](https://github.com/Synthesis/ray2get), the APM music converter first
  released in 2009, whose format description the APM decoder is partly based on.
- **The Raymap team** (byvar and contributors): [Raymap](https://github.com/byvar/raymap), the map
  viewer for OpenSpace games, used as a reference for how the engine's data fits together. No code
  is taken from it.
- **The UnLockRay community**, for years of Rayman 2 research and knowledge sharing.
- **The RaymanPC community** and everyone who documented OpenSpace on its
  [wiki](https://raymanpc.com/wiki/en/OpenSpace) and forums.

### Libraries

- [SDL 3](https://libsdl.org) by Sam Lantinga and contributors (zlib license)
- [GLM](https://github.com/g-truc/glm) by G-Truc Creation (MIT)
- [glad](https://github.com/Dav1dde/glad) by David Herberth, with Khronos OpenGL definitions
  (WTFPL/CC0 and Apache-2.0)
- [stb_image_write](https://github.com/nothings/stb) by Sean Barrett (MIT or public domain)
- [doctest](https://github.com/doctest/doctest) by Viktor Kirilov (MIT), for the tests

The 2016 version also used gl3w, nlohmann json, lodepng, mingw-std-threads and FLAC; thanks to their
authors as well. They're no longer part of the project.

### Tools

- [Vikunja](https://vikunja.io) ([source](https://github.com/go-vikunja/vikunja)) by kolaente and
  contributors (AGPL-3.0): the self-hosted task board, with its built-in MCP server, where we and
  our AI helpers keep track of the open work.

### And

- **Ubisoft Montpellier** and everyone who made Rayman 2, a game worth preserving.
