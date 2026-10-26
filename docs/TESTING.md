# Testing guide

This is for checking a build by hand. Everything with an exact expected number (level counts,
pointer checks, texture and music counts, mesh and triangle counts, one-ear and clipping checks on
every music track) is checked by the automated tests, so this list only has what a person has to
judge: does it look and sound right compared with the real game?

Report a problem by writing down what you did, what you expected and what happened instead (a
screenshot helps).

Use your normal Rayman 2 folder, e.g. `F:\Games\Rayman 2`. OpenRayman only reads from it, it never
writes there.

## Build and automated tests

1. In PowerShell, from the repository folder: `$env:OPENRAYMAN_GAME_DIR = "F:\Games\Rayman 2"`
2. Run `.\build.ps1`
3. Expected: it ends with `100% tests passed` and `Built into build\windows-debug\bin`.

Without step 1 the tests still pass, but only check synthetic data. With it, they also read every
level, texture and music file of your installation (about 40 seconds) and compare a few decodes
against checksums of known-good output (`tests/checksum_tests.cc`). Only checksums and counts are
stored, never game data.

Programs are in `build\windows-debug\bin`. Open PowerShell **in the OpenRayman folder** and type
the path as shown below.

## Textures

Start `.\build\windows-debug\bin\openrayman.exe --game "F:\Games\Rayman 2"`, then press T for the
texture browser (Left/Right to step, Up/Down to jump, Home/End).

- [ ] The loading screens (the first 35 textures) look like the real game's: correct colours, not
      upside down, not mirrored
- [ ] Transparent textures (e.g. in `effets_speciaux`) show a grey checkerboard where they're see-through
- [ ] Spot check a few hundred others: no garbage, no "ERROR" in the title
- [ ] Resizing the window and F11 fullscreen keep the texture centred and undistorted

## Music

Press M / N for the next / previous track, Space to pause (in the texture browser). Most files are
2–3 second pieces of Rayman 2's adaptive music; longer complete tracks to try: `main title`, `Jail10`.

- [ ] It sounds clean: no hiss, crackle, clicks or chipmunk/slow-motion speed
- [ ] **Left/right check:** with headphones, compare a track against the original game. If
      instruments sit on the wrong side, the channels are swapped
- [ ] `outro1` (the only mono track) plays on both ears

Tip: `ortool music "F:\Games\Rayman 2" H:\r2export\music` exports everything as WAV so you can listen in
any player (about 500 MB). Keep exports like this off the C: drive, and never commit them.

## Folder picker

- [ ] Start without `--game`: it reopens the same folder without asking
- [ ] Press O, pick a folder that is **not** Rayman 2: a clear error, and the old data stays loaded
- [ ] Press O, pick the Rayman 2 folder or its `Data` subfolder: both work

## Levels

The engine opens The Woods of Light (`Learn_10`), seen from where the game's camera starts.

- [ ] You see the level's scenery with its textures: grass and jungle walls, tree trunks, ferns and
      leaves with see-through edges, the waterfall, soft clouds and mist
- [ ] Textures are not upside down or mirrored compared to the real game (signs, and which side of
      the waterfall things are on, are good checks)
- [ ] PgDn / PgUp step through all 70 levels, Home goes to the first one (Menu). Try a few you
      remember: do they look like the real game's scenery?

Controls:

- [ ] W/A/S/D move, holding the right mouse button and moving the mouse looks around
- [ ] Space or E goes up, Ctrl or Q goes down, Shift moves 4x faster, the mouse wheel changes speed
- [ ] R goes back to the start position, T switches to the texture browser and back
- [ ] F11 fullscreen and resizing keep the picture undistorted; moving and looking are smooth

## Characters and objects

Drawn frozen in the first frame of their current animation. Near the start of The Woods of Light:

- [ ] Rayman stands on a ledge near where the view starts, with his red collar at his neck
- [ ] Another Rayman stands near a golden cage with a group of Teensies (small blue-faced characters
      with long noses); they have eyes, and one wears an upright crown with no black outline
- [ ] Characters stand upright, with arms, legs and heads attached; sizes and textures look right
- [ ] The shafts of sunlight between the trees look like soft light, not black panels
- [ ] Lums glow yellow and keep facing you as you fly around them

## Round 3: seams and the warp swirl

- [ ] The warp swirl in front of the stone faces out of the stone (checked in round 2), and now has
      no line across its middle and no seams along its top and bottom edges
- [ ] Places where two textures meet show the change of texture, like the real game, but no thin
      bright or dark line along the edge. Compare a few spots with the game
- [ ] The painted jungle walls with sky above them: no thin line running through the sky from the
      top of a wall, and no light crack between the wall's strips. A good spot:
      `--level Learn_10 --camera 17.0,344.4,13.5,137.8,5.2`

If you find a spot that looks wrong, press **P**: it copies the position as `--level ... --camera ...`
to the clipboard, ready to paste into a report. **F2** switches the edge fix off and on, to see
whether a line comes from it.

## Round 4: animations

Start at the Globox kids: `--level Learn_10 --camera -67.1,205.8,1.9,321.0,-11.7`

- [ ] Characters and props make small idle movements that loop smoothly: no jerks, no snapping
      back at the end of a loop, no parts flying off
- [ ] F3 pauses and resumes them
- [ ] The mushrooms (e.g. `--level Learn_10 --camera -54.2,178.3,1.5,758.6,-12.4`) stay visible and
      upright from every side as you fly around them, with no flickering where two overlap
- [ ] Flames, sparks and chain links in other levels face you from every side

**Actor states.** Most actors are saved in a placeholder state that the game's scripts replace. Look
at an actor and press **F5** / **F4** to step it through its family's states (hold **Shift** to
change every actor of that family); the title bar shows the node and state. Compare with the game:

- [ ] Lums: `--level Learn_10 --camera -100.0,280.4,-12.6,169.0,-2.1`, state 4 is the yellow Lum
      with wings. Do its wings flap and does it bob like the game's?
- [ ] Mushroom clusters: the other states show 5 mushrooms. Which one matches the game, and do
      they move like the game's (e.g. when Rayman walks by)?
- [ ] Cages: find a state where the cage hangs from its chains
- [ ] Anything that animates wrongly in some state: press P and note the node and state from the title

## Known limitations

Don't report these as bugs, they're the next steps:

- Some actors are invisible on purpose (triggers, waypoints)
- Actors play the state they were saved in, which is often a still or a placeholder: the game's own
  scripts switch them to their real state when the level starts, and those scripts don't run yet.
  So Lums hang still without flapping their wings, the mushroom clusters show 3 of their 5
  mushrooms, the breakable cages stand without their chains, and the butterfly flies its loop
  instead of its real path
- Moving textures (the water stream, waterfall, mist) and particles (fireflies) don't move yet
- The start view uses the game camera's saved position, which can be an odd spot (e.g. among the
  treetops in The Woods of Light); press R after flying off to get back there
- The lighting is a simple stand-in, not the game's own; the sky is a plain colour
- Water and glass may look too solid

Tip: compare with the real game at the same spot.
