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

## Phase 1 checklist

Nothing to look at in the engine yet: Phase 1 reads the level data that Phase 2 will draw. These
checks show that every level reads cleanly. Run them from the OpenRayman folder as above.

### All levels

Run `.\build\windows-debug\bin\ortool.exe level "F:\Games\Rayman 2" all` (takes about a minute)

- [ ] Each level prints two lines: a pointer count ending in `0 dangling`, and a `scene:` line
- [ ] It ends with `70 of 70 levels loaded cleanly`
- [ ] The level with the fewest super objects is the last one, `Raycap` (14)

### One level in detail

Run `.\build\windows-debug\bin\ortool.exe level "F:\Games\Rayman 2" Learn_10`

- [ ] It says `10 Fix actors, 5 placed in this level`
- [ ] It lists 112 textures with readable names, like `textures_vegetaux\...` and `effets_speciaux\...`
- [ ] The `scene:` line shows 194 super objects, 83 actors and 108 nodes in the sector tree

### Level geometry

Run `.\build\windows-debug\bin\ortool.exe obj "F:\Games\Rayman 2" Learn_10 H:\r2export\obj\Learn_10`

- [ ] It reports `87 objects, 2604 triangles, 45 materials` and `0 textures missing`
- [ ] Open `H:\r2export\obj\Learn_10\Learn_10.obj` in Windows' 3D Viewer or Blender: you see the
      Woods of Light (the paths, pools and waterfall), with textures, not mirrored or upside down
- [ ] Try a level you remember well the same way (replace `Learn_10` in both places)

Only the level's static scenery is exported. Characters, Lums and other objects come later.

## Phase 2 checklist

The engine now shows levels. Start `.\build\windows-debug\bin\openrayman.exe --game "F:\Games\Rayman 2"`

### First level

- [ ] It opens straight into The Woods of Light instead of the texture browser
- [ ] The window title shows `[5/70] Learn_10 (2604 triangles)` followed by a short controls reminder
- [ ] You see the level's scenery with its textures: grass and jungle walls, tree trunks, ferns and
      leaves with see-through edges, the waterfall, soft clouds and mist
- [ ] Textures are not upside down or mirrored compared to the real game. Signs or anything with text
      are a good check, and so is which side of the waterfall things are on

### Controls

- [ ] W/A/S/D move, holding the right mouse button and moving the mouse looks around
- [ ] Space or E goes up, Ctrl or Q goes down, Shift moves 4x faster
- [ ] The mouse wheel changes the movement speed
- [ ] R goes back to the start position
- [ ] T switches to the texture browser and back; in the browser the old keys still work
- [ ] F11 fullscreen and resizing the window keep the picture undistorted
- [ ] Looking around and moving are smooth

### All levels

- [ ] PgDn steps through all 70 levels, PgUp goes back, Home goes to the first one (Menu)
- [ ] No level shows "ERROR" in the title
- [ ] Try a few levels you remember: do they look like the real game's scenery?

### Actors

Characters and objects are drawn frozen in the first frame of their current animation.

Run `.\build\windows-debug\bin\ortool.exe level "F:\Games\Rayman 2" Learn_10`

- [ ] The last line reads `actors: 450 animations (289 in Fix), 176 meshes and 3838 triangles at frame 0`

In the engine, fly around near the start of The Woods of Light:

- [ ] Rayman stands on a ledge near where the view starts
- [ ] Another Rayman stands near a golden cage with a group of small hooded characters (the game
      keeps extra copies of characters for its cutscenes)
- [ ] Characters stand upright and aren't twisted; arms, legs and heads are attached, not floating
      apart or piled up in one spot
- [ ] Sizes look right next to the scenery, and textures look right (Rayman's purple top, orange hair)

### Known limitations

Don't report these as bugs, they're the next steps:

- Nothing moves: actors are frozen, and some are invisible on purpose (triggers, waypoints)
- Lums and other glowing sprites aren't drawn yet
- The start view uses the game camera's saved position, which can be an odd spot (e.g. among the
  treetops in The Woods of Light); press R after flying off to get back there
- The lighting is a simple stand-in, not the game's own
- The sky is a plain colour
- Some black patches, probably shadows, which the game blends differently
- Water and glass look solid instead of see-through

Tip: compare with the real game at the same spot.
