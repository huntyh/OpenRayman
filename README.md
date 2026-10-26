# OpenRayman

An open source reimplementation of the *Rayman 2: The Great Escape* engine in modern C++,
targeting Windows, Linux and Android.

This continues [imaginaryPineapple's OpenRayman](https://github.com/imaginaryPineapple/OpenRayman)
(2016), rebuilt on a current toolchain. See [docs/ROADMAP.md](docs/ROADMAP.md) for where it's heading.

**Status: Phase 1.** The engine reads the original game files and can show every texture and play
every music track. All 70 levels load into memory with every pointer resolved; nothing is drawn
from them yet.

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
openrayman [--game "<Rayman 2 folder>"]
```

On first start without `--game`, a folder picker asks for your Rayman 2 folder and remembers it.

| Key | Action |
| --- | --- |
| Left / Right, mouse wheel | Previous / next texture |
| Up / Down, Page Up / Page Down | Jump 25 textures |
| Home / End | First / last texture |
| M / N | Next / previous music track |
| Space | Pause / resume music |
| O | Choose a different game folder |
| F11 | Fullscreen |
| Esc | Quit |

The window title shows the current texture and music track.

### ortool

`ortool` is a command line tool for checking the game data:

```
ortool info "<game folder>"                 Summary: levels, archives, music
ortool textures "<game folder>" <out>       Export every texture as PNG
ortool music "<game folder>" <out>          Export every music file as WAV
ortool level "<game folder>" <name|all>     Load a level and check all its pointers
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
| `lib/` | Vendored glad (OpenGL loader) and stb_image_write | their own |
| `android/` | The 2016 Android launcher, kept for reference until the Android port (Phase 4) | GPLv3 |

## Ground rules

- **No game data in the repository**, ever. Tests read your own install from `OPENRAYMAN_GAME_DIR`.
- **Clean-room only.** Work from the game's files, public format documentation and our own analysis.
  Never use leaked Ubisoft source code.
