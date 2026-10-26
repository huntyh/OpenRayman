# Contributing to OpenRayman

Thanks for helping. A few rules keep the project legally safe and free for everyone to use. Please
follow them in every contribution.

## No Ubisoft assets

Never commit textures, music, logos, extracted game data or anything else from Rayman 2. Everyone
reads their own copy of the game. Tests use your install via `OPENRAYMAN_GAME_DIR` and may store
checksums of decoded data, never the data itself.

## No leaked or copied code

Never use leaked Ubisoft source code. Never copy code from Ubisoft binaries, or from projects whose
license doesn't allow it (for example [Raymap](https://github.com/byvar/raymap), which has no license).
Implement everything yourself, from your own understanding of the file formats.

Studying how the game's own program (`Rayman2.exe` and its DLLs) reads a file, to understand the
format, is allowed: that's how the LEVELS0.DAT tables were found. Copying its code is not. Describe
what you learned in your own words in [docs/FORMATS.md](docs/FORMATS.md), then write the
implementation from that description.

## Record where knowledge comes from

When you add or correct something about a file format, update [docs/FORMATS.md](docs/FORMATS.md):
what we know, and where it came from (your own analysis, Rayman2Lib, community documentation...).

## Keep copyright notices

Existing copyright notices stay in place, and upstream code is never relicensed. New files in
`libopenrayman/` start with the LGPL header used by the existing ones. Licenses are explained in the
[README](README.md#license).

## Testing

Run the tests with your game install before sending changes, see [docs/TESTING.md](docs/TESTING.md).
