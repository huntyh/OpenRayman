# Testing guide

This is for checking a build by hand. Each phase adds a section. Report a problem by writing down
what you did, what you expected and what happened instead (a screenshot helps).

Use your normal Rayman 2 folder, e.g. `F:\Games\Rayman 2`. OpenRayman only reads from it, it never
writes there.

## Build and automated tests

1. In PowerShell, from the repository folder: `$env:OPENRAYMAN_GAME_DIR = "F:\Games\Rayman 2"`
2. Run `.\build.ps1`
3. Expected: it ends with `100% tests passed` and `Built into build\windows-debug\bin`.

Without step 1 the tests still pass, but only check synthetic data.

With step 1 they also compare a few decoded textures and music tracks against checksums of
known-good decodes (`tests/checksum_tests.cc`). Only the checksums are stored, never game data. If
one fails after an intentional decoder change, check the new output by eye or ear before updating
the value.

## Phase 0 checklist

Programs are in `build\windows-debug\bin`. They aren't installed anywhere, so typing just `ortool` won't
find them: open PowerShell **in the OpenRayman folder** and type the path as shown below.

### Game data summary

Run `.\build\windows-debug\bin\ortool.exe info "F:\Games\Rayman 2"`

- [ ] It lists 70 levels, starting with Menu and ending with Raycap
- [ ] It ends with "Everything listed in Game.dsb was found."
- [ ] Vignette.cnt has 35 textures and Textures.cnt has 1636

### Textures in the engine

Start `.\build\windows-debug\bin\openrayman.exe --game "F:\Games\Rayman 2"`

- [ ] A window opens showing the Rayman logo; the title shows `[1/1671] Rayman.gf`
- [ ] Left/Right steps through textures, the title updates each time
- [ ] The loading screens (first 35 textures) look like the real game's: correct colours, not upside down,
      not mirrored
- [ ] Transparent textures (e.g. in `effets_speciaux`) show a grey checkerboard where they're see-through
- [ ] No texture shows "ERROR" in the title (End jumps to the last one; spot check a few hundred)
- [ ] Resizing the window and F11 fullscreen keep the texture centred and undistorted
- [ ] Close it, start it **without** `--game`: it reopens the same folder without asking

### Music

In the engine, press M.

Most files are 2–3 second pieces: Rayman 2's music is adaptive, and the game stitches these pieces
together as you play. Until that sequencing is implemented (Phase 1), M plays one raw piece on a loop.
Longer complete tracks to try: `main title`, `Jail10`.

- [ ] Music plays; the title shows the track name and a running clock
- [ ] It sounds clean: no hiss, crackle, clicks or chipmunk/slow-motion speed
- [ ] No track plays in only one ear (fixed after the first round of testing, e.g. BA102, BA1r03)
- [ ] **Left/right check:** with headphones, compare a track against the original game. If instruments
      sit on the wrong side, the channels are swapped. This is the one thing the automated checks can't verify.
- [ ] Space pauses and resumes, M/N switch tracks, short tracks loop
- [ ] `outro1` (the only mono track) plays on both ears

Tip: `ortool music "F:\Games\Rayman 2" H:\r2export\music` exports everything as WAV so you can listen in
any player (about 500 MB). Keep exports like this off the C: drive, and never commit them.

### Folder picker

- [ ] Press O, pick a folder that is **not** Rayman 2: a clear error, and the old data stays loaded
- [ ] Press O, pick the Rayman 2 folder or its `Data` subfolder: both work
