# Where OpenRayman differs from the game on purpose

OpenRayman aims to play like Rayman 2, but where the original shows a limit of 1999 hardware or a
glitch, we may do better. Every such change is listed here, so it's always clear what is the game
and what is ours. When reporting a difference from the game, check this list first.

Some changes are switches (G for the scripts, L for the lighting); a "faithful mode" that turns all
of them off may come later.

## Graphics

| What | The game | OpenRayman | Since |
| --- | --- | --- | --- |
| Character shadows | At most 10, and only for actors whose scripts switch them on (the Globox kids have none) | Every actor with a shadow gets one | 2c |
| See-through leaves | Hard, jagged cut-out edges | Soft edges: texture colour bled into the see-through texels, a solid core drawn first, back-to-front sorting per triangle | 2c |
| Sky | Jumps when the camera moves up and down (a parallax glitch, visible when Rayman jumps) | Stays put | 2c |
| Texture filtering | Plain bilinear | Anisotropic filtering where the GPU has it | 2c |
| Looping animations | A loop whose last pose doesn't match its first jumps when it restarts (the swinging cages, 2 degrees on three channels) | A state that loops onto itself glides into its first pose over its last 4 frames (at most a quarter of the loop). Everything else plays as the game does | 3b |

## The scripts

| What | The game | OpenRayman | Since |
| --- | --- | --- | --- |
| Brain rate | Scripts can make an actor think only every Nth frame (3D data +0xFE, procedure 341) to save time | Every actor thinks every frame (`honour_brain_period` switches the game's way back on) | 3b |
| Slow frames | Not known yet | At most 4 ticks per drawn frame: if the computer can't keep up, the game slows down instead of freezing | 3b |
| Particles | At most 150 at a time, and 35 new ones a frame | Room for 2000 | 3b |
| Ticks and frame rate | 60 ticks per second | The same 60 ticks per second; drawing runs at any frame rate, smoothed between ticks | 3b |

Not changed yet, candidates for later: the every-2/4/8/16-frames checks in scripts (keywords 2 to 5)
are kept as the game has them, since changing their timing could change gameplay.

## While the engine is unfinished

These aren't improvements, they keep things working until the missing parts exist, and go away
as those arrive:

- Script handlers that aren't written yet do nothing and read as 0; `ortool run` lists them.
- A reference to an actor that isn't set (or has no brain) reads as nobody, and a distance to
  nobody as very far, instead of stopping the actor's scripts.
- The game's physics isn't there yet: a speed the scripts set moves the actor straight on, with no
  gravity or collision (right for flying actors such as the fireflies).
- The game's camera doesn't run yet: its actor is put where the viewer's camera is every tick, so
  scripts that work around the camera (the floating specks) do so around the view. Learn_10 only
  starts the specks once the camera has started up, so for now they need `--behaviour 4:1`.
- Only particle effects 23 and 28 are known; the others are started but do nothing.
- Messages to the engine, on-screen text and the scripts' fog override are read
  but have no effect yet.
