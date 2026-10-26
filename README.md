# OpenRayman

An open source reimplementation of the *Rayman 2: The Great Escape* engine in modern C++,
targeting Windows, Linux and Android. Free for anyone to use, under the GPL/LGPL (see [License](#license)).

This continues [imaginaryPineapple's OpenRayman](https://github.com/imaginaryPineapple/OpenRayman)
(2016), rebuilt on a current toolchain. See [docs/ROADMAP.md](docs/ROADMAP.md) for where it's heading.

> **OpenRayman is an unofficial fan project. It is not affiliated with, endorsed by, or connected to
> Ubisoft in any way.** Rayman and Rayman 2: The Great Escape are trademarks of Ubisoft
> Entertainment. You need your own legally obtained copy of the game (GOG) to use OpenRayman.

**Status: Phase 2.** The engine opens the original levels and lets you fly through them, with the
game's textures, characters, objects and glowing Lums. Nothing moves yet, and the game's lighting
and sky aren't drawn: it's a viewer, not a game, for now. It renders with OpenGL today; the plan is
Vulkan or Direct3D 12, keeping OpenGL for older devices.

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
| T | Switch to the texture browser and back |

In the texture browser:

| Key | Action |
| --- | --- |
| Left / Right, mouse wheel | Previous / next texture |
| Up / Down, Page Up / Page Down | Jump 25 textures |
| Home / End | First / last texture |

Anywhere: M / N for the next / previous music track, Space to pause it (in the texture browser),
O to choose a different game folder, F11 for fullscreen, Esc to quit. The window title shows where
you are.

### ortool

`ortool` is a command line tool for checking the game data:

```
ortool info "<game folder>"                 Summary: levels, archives, music
ortool textures "<game folder>" <out>       Export every texture as PNG
ortool music "<game folder>" <out>          Export every music file as WAV
ortool level "<game folder>" <name|all>     Load a level and check all its pointers
ortool obj "<game folder>" <level> <out>    Export a level's scenery as OBJ with textures
ortool scene "<game folder>" <level>        Print a level's scene tree
ortool dsb <file.dsb>                       Print a DSB script
ortool cnt <file.cnt>                       List a texture archive
```

## Testing

See [docs/TESTING.md](docs/TESTING.md). Setting `OPENRAYMAN_GAME_DIR` to your install folder also
runs the unit tests against your real game files.

## Layout

| Folder | Contents | License |
| --- | --- | --- |
| `libopenrayman/` | Readers for the game's file formats, no dependencies | LGPLv3 |
| `src/` | The engine | GPLv3 |
| `tools/ortool/` | Data inspection tool | GPLv3 |
| `tests/` | Unit tests (doctest) | GPLv3 |
| `lib/` | Vendored glad (OpenGL loader) and stb_image_write | their own, see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) |
| `android/` | The 2016 Android launcher, kept for reference until the Android port (Phase 4) | GPLv3 |
| `docs/` | Roadmap, testing guide, [file format notes](docs/FORMATS.md) | GPLv3 |

## How this is made

This continuation of OpenRayman is written by AI. The code, the reverse engineering of the file
formats and the documentation are done by Claude, an AI model by Anthropic, working in Claude Code,
with a smaller local model drafting some routine pieces (always reviewed before use).
**huntyh** runs the project: deciding what to build, testing every step against the real game and
reporting what looks wrong, and keeping the project's rules.

Every commit made this way says so in its message ("Co-Authored-By: Claude"), so the history shows
exactly which work came from where.

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

### And

- **Ubisoft Montpellier** and everyone who made Rayman 2, a game worth preserving.
