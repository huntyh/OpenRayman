# Where OpenRayman differs from the game on purpose

OpenRayman aims to play like Rayman 2, but where the original shows a limit of 1999 hardware or a
glitch, we may do better. Every such change is listed here, so it's always clear what is the game
and what is ours. When reporting a difference from the game, check this list first.

Some changes are switches (G for the scripts, L for the lighting); a "faithful mode" that turns all
of them off may come later.

## Graphics

| What | The game | OpenRayman | Since |
| --- | --- | --- | --- |
| Pause swirl centre | The funnel's UVs pinch together at the narrow tip | Near the axis the pause funnel's own texture mapping (8 repeats a turn, its twist, U along its length; read from its vertices) is computed smoothly per pixel and cross-faded in, with a little extra twist towards the point, so the funnel's spiral continues without the ring seams and moves with the same scroll; the point itself fades to a soft dark. On by default, only during the pause background; `OPENRAYMAN_SWIRL_CENTRE=0` restores the original coordinates and tip. The outer funnel, other water materials and all other geometry keep their original rendering. | 4 |
| Learn_10 portal well and the Hall of Doors star lake | One flat star field scrolls across the surface, so it reads as a picture | The star texture (`foret_fondetoiles_txy`) is drawn as three layers lying 2, 8 and 25 units below the surface: each pixel follows the view ray down to each layer, so the stars slide against the camera like deep space seen through a hole (deeper layers finer, dimmer, slowly drifting). In the Hall of Doors it is a lake: the star field's mesh is a bowl whose sides would show, so every pixel of it is a window onto flat star planes below the bowl's lowest point (`OPENRAYMAN_STAR_FLOOR=0` follows the mesh instead). The endless floor is only a picture: the bowl's geometry, collision and sectors are unchanged, so anything that later reads the star field's surface (a new effect, reflections, a camera dipping below the bowl's rim, a mod that edits the Hall) still sees the original bowl, and the planes assume a camera above them (rays that don't go down are clamped); slow ripples bend the view down to the stars (deeper layers more, drifting at a quarter of the well's pace), and the see-through star overlay above ripples in place, its stars twinkling and a soft band of light gliding over it. Every star layer is scrambled so the texture's repeat doesn't show: each tile gets its own random shift, quarter-turn and mirroring, blended softly at the joins (the star materials' scroll keeps counting instead of wrapping each width, which would reshuffle the field). In Learn_10 the star field also lies flat below the well's bowl and its stars twinkle (`OPENRAYMAN_TWINKLE=0`). Renderer only, on by default in these two levels; `OPENRAYMAN_WELL=0` restores the original, `OPENRAYMAN_WELL_DEPTH=<factor>` scales the depth, `OPENRAYMAN_LAKE=0` stills the lake (`=2` also ripples the Learn_10 well) (cards 111, 39). | 4 |
| Loading screens | The level's own picture (its DSB vignette, from `Vignette.cnt`; the Hall of Doors shows the wide `Rayman.gf`) with a loading bar that fills as it loads, cut in and out | A level change fades the same picture in over the old level (scripts paused) in 0.5 s, holds it 2 s while the new level loads and runs unseen (so its first ticks, such as Rayman being placed by the portal, are hidden), then fades it out over the new level in 0.5 s. No bar (ours loads too fast for one); three yellow Lums (`lums_corps_mxy`, its glow and the Lum wings, from Textures.cnt) hop in the bottom right corner like the dots after "Saving". The black band most pictures carry above and below their art is dropped and the art drawn as large as the window allows at its own shape, nothing cut (wide art fills the width, 4:3 art keeps side bars); one of the spec's candidates is picked at random, as its `Random<...>` says. Not on the first level the engine opens; `--loading-screens off` cuts straight across. `OPENRAYMAN_LOADING_PREVIEW=<picture>` draws one fully (screenshots). (user, 2026-11-16) | 4 |
| Hall of Doors waterfall | The river below the fall (`eau_morf`, scroll -0.005 U) is mapped in a V whose two halves flow into each other, so the water seems to run towards the fall (a bug in the original) | In the Hall only, the river's texture is laid in rings around the fall's foot (measured from the column, `eau_bouillon`, at load) and moves outward, so the water spreads away from the fall towards the banks; the column itself is unchanged. `OPENRAYMAN_FALLS=0` restores the original (user, 2026-11-15) | 4 |
| Jail_10 cloud flashes | Brief white cloud-pattern cards can look inverted and show rectangular edges | The cloud flash frames brighten the existing sky with a soft 20% UV-edge falloff; `OPENRAYMAN_LIGHTNING_CLASSIC=1` restores the original blend. The texture animation still controls timing. | 3 |
| Wide screens in the Jail_10 intro | 4:3 only; with the community widescreen patch the wider view clips through the rock as the camera pans out of the cave, showing outside the world for a moment (user, 2026-11-07) | Native wide aspect ratios; the pan out of the cave shows no gap (seen by the user, not yet explained: likely our near plane or the sector draw list) | 3 |
| Tiled title picture | Kinds 170/171 (the intro's Rayman 2 logo) are placed in percent of the whole viewport, so a wide screen would stretch them | Kept in a centred 4:3 area. The game draws the 18 tiles (32x32 each, 192x96 in all) as separate quads, so the joins show; ours joins them into one picture, enlarges it 8x (Catmull-Rom) and redraws the cut-out edge about one pixel wide, and draws it at 0.6 of the game's size about the same centre so it looks less blurry (user requests, 2026-11-06) | 3 |
| Screen text and HUD at wide window sizes | The text objects (0x419A30), the screen slots (0x4660B0), the text boxes (0x460A10) and the HUD pictures and gauges (0x460600, 0x460670) are placed in percent of the whole viewport, so a wide window stretches every glyph, box and icon sideways | Two classes, one transform each (docs/formats/text.md). The edge HUD (the gauges, and the pictures and counter slots set up by the actor that draws a gauge: Rayman's face and health, the Lum and cage icons and their numbers) stays spread across the screen, each piece drawn at 4:3 proportions about the screen edge it is nearer to (user, 2026-11-08). Everything else (story texts and subtitles, text boxes and the pictures beside them such as Murfy's face, menus, titles, the intro's slaves count) moves as one block into the centred 4:3 area, x' = 320 + (x - 320) x area of 640, the look of the game with Ray2Fix (user, 2026-11-13). The cutscene bars and anything spanning the whole width (a full-screen cover) stay full width. At 4:3 nothing changes | 3 |
| Bars of a level's opening cutscene | The cutscene bars slide in over about half a second as a level that opens with a cutscene starts (Learn_10's new game, Bast_20; Fix's world actor grows its var60 by 0.4 a millisecond to 195) | They are already in place on the first frame: when the bars are requested in a level's first two seconds, var60 and view 1 jump to the final size (`bars_in_place` in src/main.cc; user request, 2026-11-14) | 4 |
| Screen-object edges | The intro's Ubisoft logo (256x64 cut-out textures stretched over the screen) has staircase edges | Screen objects are blended, and where their texture's alpha jumps between texels the magnified edge is redrawn about one pixel wide (shader `u_sharpen`); soft gradients stay as they are (user idea, 2026-11-06) | 3 |
| Character shadows | At most 10, and only for actors whose scripts switch them on (the Globox kids have none) | Every actor with a shadow gets one | 2c |
| See-through leaves | Hard, jagged cut-out edges | Soft edges: texture colour bled into the see-through texels, a solid core drawn first, back-to-front sorting per triangle | 2c |
| Sky | Jumps when the camera moves up and down (a parallax glitch, visible when Rayman jumps) | Stays put | 2c |
| Jail_10 far backdrop | The mountain/valley scenery behind the pirate ship is nearby geometry | Optional 3D skybox pass for sector 7, with one tenth of the camera translation about the scripted camera at 90 seconds. The sky shells and lightning cards also use a shared parallax eye (default 0.02, a fiftieth of the camera translation, so the sky feels much further than the mountains; tunable with `OPENRAYMAN_SKY_PARALLAX=0..1`); its height follows between the sea and ship shells. Jail_10's sky pass can stretch shells and cloud/lightning cards horizontally about that anchor (default 1, off: at 4 the clouds looked smeared and low-res, user 2026-11-13), slows the clouds' UV drift (default 0.07: slow drift is what reads as far away; texture animation timing unchanged). The lightning cards are drawn from the real camera, where the game puts them, over the far clouds they brighten (from the far eye they sank to the horizon), and fades the lowest 12 degrees towards sector fog colour (amount 0.3). `OPENRAYMAN_SKY_STRETCH=1..32`, `OPENRAYMAN_SKY_DRIFT=0..4`, and `OPENRAYMAN_SKY_HAZE=0..1` tune these cues; 1/1/0 restores the earlier parallax sky. These cues do not apply to Jail_20. Sky, backdrop, then normal scenery with cleared depth; lighting/fog retained and collision unchanged. Drawn only where the game would draw sector 7 (the camera sector's draw list), so views where the game hides it stay as they were. Also in Jail_20 (sector 0: the sky shell and the valley cards seen during the chute fall, anchored at the chute exit (393.2, 321.3, 275.9); the chute and landing stay normal scenery). Default on only in these two levels; `OPENRAYMAN_BACKDROP=0` disables it, `OPENRAYMAN_BACKDROP_PARALLAX=0..1` tunes translation (default 0.1). | 4 |
| Texture edges | Textures that don't tile are sampled past their edge, so a thin line shows along it: the warp swirl has one across its middle and along its top and bottom (seen in the real game, user 2026-11-07) | Non-tiling textures are clamped to their edge texels, so no line (F2 toggles it) | 2a |
| Texture filtering | Plain bilinear | Anisotropic filtering where the GPU has it | 2c |
| Colour depth | Textures drawn in 16-bit colour (the usual setting, `GLI_Mode ... x 16` in ubi.ini): banding and blocky gradients | The textures' full 24/32-bit colour from the files (the water looks much better for it, user 2026-11-02) | 2a |
| Soft-edged effects | The Voodoo renderer (GliVd1vf.dll) alpha-tests cut-outs at 192 of 255, so soft edges like the water splash's come out hard and pixelated | Real alpha blending: effect textures marked `_aaa` always blend with their own alpha | 3c |
| The Menu's Teensy rock | The big green rock the left Teensy stands on (Menu level) has its texture smeared into long streaks, stretched as if towards infinity | The rock's texture shows normally (user 2026-11-04, side by side; cause in the game not traced, we just draw the level's texture coordinates) | 4 |
| Looping animations and state changes | A loop whose last pose doesn't match its first jumps when it restarts (the swinging cages, 2 degrees on three channels); so does a state change whose next animation starts slightly off (the Jail_20 cell wake-up, card 100: animation 310 to 311, parts up to about 0.14 units apart) | One mechanism for both: after a loop has really started over (same state, the frame jumped back), or after a state change whose new pose is within 0.25 units of the last one drawn, the first frames (up to 4, at most a quarter of the animation) fade out of the pose drawn before the switch, in the actor's own space. Bigger changes snap as in the game (intended cuts). Nothing is blended ahead of the end: an earlier seam that glided into frame 0 in advance showed a stepping pose for two frames when the Jail_20 director switched states first (card 97, docs/formats/animation.md) | 3b |

| Cameras riding an animation part | Function 210 (a part's world position) reads the part at the whole frame the animation has reached, so a camera tied to it moves only when a new frame comes: in Jail_20 the camera following Globox down the chute (animation 354, about half a frame per tick) moved on 2 of every 3 or 4 ticks and stuttered | Our fallback poses the part at the in-between frame (elapsed ms x rate, the same frame the renderer draws), so the camera moves every tick. Held last frames stay whole | 3c |
## The scripts

| What | The game | OpenRayman | Since |
| --- | --- | --- | --- |
| Brain rate | Scripts can make an actor think only every Nth frame (3D data +0xFE, procedure 341) to save time | Every actor thinks every frame (`honour_brain_period` switches the game's way back on); "every N frames" checks still count only the frames the game would think, so timers built on them (the shot's countdown) keep the game's pace | 3b |
| Slow frames | Not known yet | At most 4 ticks per drawn frame: if the computer can't keep up, the game slows down instead of freezing | 3b |
| Particles | At most 150 at a time, and 35 new ones a frame | Room for 2000 | 3b |
| Random numbers | A table of pre-made numbers read in turn | Any uniform source | 3c |
| Ticks and frame rate | 60 ticks per second | The same 60 ticks per second; drawing runs at any frame rate, smoothed between ticks | 3b |
| The menu's "Site Rayman2" entry | Procedure 369 hands the level's string `http://www.rayman2.com/` to `ShellExecuteA`, and that address has not worked for years (the shipped `.text` copy has the call NOPped out anyway, see docs/formats/menu.md) | The entry reads "OpenRayman 0.1.0" (the project version; text id 0x16A, as Ray2Fix renames it), and any URL procedure 369 asks to open becomes `https://github.com/huntyh/OpenRayman`, opened with the desktop's default browser (`SDL_OpenURL`); the engine logs which address it opens | 3c |
| Language preference | Procedure 242 writes the choice to the original game's ini, but the menu asks again on every start: whether the prompt shows depends on story bit 1006, which isn't kept between starts | The choice is kept in OpenRayman's save folder as `language.txt`; a valid choice sets bit 1006 at startup, so the prompt is skipped on later launches | 4 |
| Optional autosave (card 40) | Asks whether to save on arrival in the Hall of Doors (and before quitting, which always asks) | After the first new-game save, the original "saved successfully, press Enter" text is replaced by "Enable autosave?" (default No). No starts the game at once; Yes shows the green Lum (hovering, wings beating) with "When this Lum is shown, the game is saving" until Enter. The answer belongs to that save: `autosave_SlotN.txt` beside the slot folders (a new game in the slot asks again); `--autosave on/off` sets it for the `--save` slot (or slot 1). When on, the original save scripts run without confirmation and a green Lum loaded from game data with Saving... appears at the bottom right for four seconds, then fades over half a second. Quitting still asks whether to save (an automatic yes there stored the Hall portal selected at Esc as the return point). When off the original prompts remain. | 4 |

Not changed yet, candidates for later: the every-2/4/8/16-frames checks in scripts (keywords 2 to 5)
are kept as the game has them, since changing their timing could change gameplay.

## Sound

| What | The game | OpenRayman | Since |
| --- | --- | --- | --- |
| 3D sound | Each sound's distance from the listener is turned into volume and panning by the sound DLL (0x40A0A0, not traced further) | The exe's own distance curve (0x459C90/0x459D30, `Snd_Cpa.ini` saturation 8, fade 30: full volume within 8 units, then linear, held at 77 beyond 30; ae1c155); panning from the angle to the listener is still ours (docs/formats/sound.md) | 3d |
| Sound loops | Loop points from the sample's decoder DLL | Whole samples loop when the (tentative) +0x30 flag is set; some seam clicks remain | 3d |
| Music themes | The theme player sequences a theme's segments and transitions between them | The same player, driven by the sectors' entry lists and the scripts; the first 12 segments of the woods' theme switch within 0.02 s of the recorded game (`OPENRAYMAN_MUSIC_LOG=1` logs each) | 3d |

## While the engine is unfinished

These aren't improvements, they keep things working until the missing parts exist, and go away
as those arrive:

Screen objects without a selected saved vertex colour set still use a white ambient stand-in;
the separate pass's full light handling has not been reconstructed. Those with saved colours use
the saved sum without that stand-in, as needed by the intro logo, whose own light is switched off.

- Script handlers that aren't written yet do nothing and read as 0; `ortool run` lists them.
- A reference to an actor that isn't set (or has no brain) reads as nobody, and a distance to
  nobody as very far, instead of stopping the actor's scripts.
- Actors other than Rayman have no dynamics: a speed the scripts set moves them straight on, with
  no gravity or collision (right for flying actors such as the fireflies).
- The game's camera runs while playing; while flying (Tab), its actor is put where the viewer's
  camera is every tick, so scripts that work around the camera do so around the view. Its target
  tracking and obstacle search approximate parts of the original, and the occlusion pass is still
  missing (docs/formats/camera.md). A look-at point that jumps more than 10 units in a frame cuts
  the view there. The game view uses the 0.9-radian vertical field of view measured in Learn_10;
  other scenes and display configurations still need checking. The obstacle search also skips
  cameras more than 20 units from their target, so a fly camera dragged far away does not slow the
  viewer down; segment casts try only the triangles in the collision grid cells under the segment.
- Input: the keyboard and gamepads through SDL3 (no rumble yet, and the Steam Controller's
  touchpads and gyro are unmapped); cheat codes (typed key sequences) don't fire.
  Tab switches the keyboard between the viewer's fly camera and the game.
- Some particle effects are not read yet (the kinds the exe's table names but we haven't traced);
  an unknown kind is started but does nothing (docs/formats/particles.md).
- Messages to the engine and the scripts' fog override (procedures 42..47) are read but have no
  effect yet.
- Physics is partial (docs/formats/dynamics.md): an actor is a small disc that finds the floor
  straight below it, not the game's collision spheres. Walls now stop it, as a cylinder from 0.5 up to
  1.8 above its feet, the radius of its first kind-1 zone (Rayman's 0.8); ceilings stop a jump
  where the top of his head (2.2 above his feet) would pass a downward-facing surface; slopes
  slide downhill but with the wrong acceleration.

OpenRayman saves live in its per-user folder rather than the game installation's Data/SaveGame. A `portable.txt` next to the executable selects a local SaveGame folder; `OPENRAYMAN_SAVE_DIR` overrides both.


Where this came from (card 40): user-requested OpenRayman addition, 2026-11-15. The save
prompt paths were independently traced at executable addresses 0x47D830, 0x47D730, 0x4702A0
and in Menu node 6 / Mapmonde node 89 bytecode; see formats/saves.md. The green Lum sprite
and wings are read from Textures.cnt at runtime. No game assets or executable code are stored
in the repository.
