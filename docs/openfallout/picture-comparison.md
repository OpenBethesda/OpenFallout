# Picture comparison

The Metal build must draw the game the way the OpenGL build does. Check 1 of the done test of milestone two says how that
is shown: a machine compares pictures from a fixed list of scenes with the OpenGL build, within a set limit, and the owner
looks only at what the machine flags. This page is that machine: what it draws, how it holds a scene still, how it
compares two pictures, how its limits are set, and what a flag means.

The tool is `scripts/openfallout/picturecompare.py`. It draws each scene on both builds through the round runner
(`scripts/openfallout/round/README.md`), hidden and one engine at a time, compares the two pictures of each scene with
`scripts/openfallout/picturediff.py`, and writes a report. It uses real game data, so it runs only on the owner's Mac;
its tests run in CI with stand-ins.

## Running it

A build is named by a round profile: a JSON file with the engine, its resources and the games' data folders (see
"The profile" in the round runner's README). Make one for the OpenGL build and one for the Metal build. Both must have
the same games and no `settings` file of their own, or the same one.

```
scripts/openfallout/picturecompare.py scenes
scripts/openfallout/picturecompare.py run --reference gl.json --candidate metal.json --out <results>
scripts/openfallout/picturecompare.py noise --profile gl.json --out <results>
scripts/openfallout/picturecompare.py compare <results>/<run>/reference <results>/<run>/candidate --out <report>
scripts/openfallout/picturecompare.py limits <results>/<noise run>/report/results.json ... --write --build "<commit> OpenGL"
scripts/openfallout/picturecompare.py rounds --out <folder> --scene nv-water
```

- `run` draws every scene on the reference build, then on the candidate, scene after scene (`--scene` for some only). Each
  round is one `round.py run`, which takes the round runner's lock, so two engines never run at once. `--runner` names
  the command that starts `round.py` when every heavy thing on the computer waits in a queue of its own, for example
  `--runner "<queue helper> --round python3 scripts/openfallout/round/round.py"`. The results go to
  `<out>/<time>-pictures/`: `rounds/` (the round file and scenario of each scene), `reference/` and `candidate/` (the
  results of the rounds), `runs.log` (what `round.py` printed) and `report/`.
- `noise` is `run` with one build on both sides: the noise floor of each scene (see "The limit").
- `compare` compares two folders of results that are there already and starts nothing. Any folder that holds round
  results with `report.json` works; where a scene was run more than once, the newest run counts.
- `limits` proposes the limits of each scene from the results of noise runs, and with `--write` records them.
- `rounds` writes the round file and scenario of each scene, to run one by hand with `round.py run`.

Every scene takes about one and a half minutes (the engine starts, the place loads, the world is paused one minute into
the game), so the twelve scenes on two builds take about forty minutes.

The exit status is 0 when no scene is flagged, 1 when a scene is flagged or a picture is missing, and 2 when nothing
could be compared (no results, or the round runner refused or a guard stopped it; the scenes after that are not run).

## What the owner is shown

`report/index.html` (and `summary.md`, the same table as text) lists every scene with its verdict, its measures, whether
its limits were set from runs or are still the allowance, and the reasons for a flag. A flagged scene links to its own
page: the reference picture, the candidate picture and the difference picture side by side, the measures with their
limits, and the reasons. In the difference picture the candidate is a dim grey copy; pixels that differ by less than the
tolerance are blue (brighter the more they differ), pixels beyond it red, and each 32-pixel window beyond a limit is
outlined in yellow. `--all-pages` makes a page for every scene. `results.json` holds all of it for machines.

The verdicts:

- **PASS**: every measure is within its limit. Nobody needs to look.
- **FLAG**: a measure is beyond its limit, or the scene was not held still on one side (its round failed: the round's
  `report.md` says why), or the two runs had other settings. The owner looks at the page and decides whether the
  difference is a fault of the candidate or a legitimate difference of renderers (see "After a flag").
- **MISSING**: one side has no picture (the engine did not start, crashed, or the round failed before the picture).

## The scenes

The list is `scripts/openfallout/picture_scenes.json`. Its first six scenes are at the places of the fixed measurement
scenes of [measuring.md](measuring.md), so that pictures and frame rates are taken at the same places. Tale of Two
Wastelands has no scenes of its own: it is drawn by the same renderer from the same kinds of data.

| scene | what it is for | place |
|---|---|---|
| `nv-interior` | an interior: lamps, furniture, walls and floor | `GSDocMitchellHouse` |
| `nv-exterior` | the open desert by day: land, rocks, plants, sky | `WastelandNV:6,15` |
| `nv-busy` | a busy exterior: many buildings, objects and characters | `WastelandNV:-18,0` |
| `fo3-interior` | an interior: the metal rooms and lights of a vault | `Vault101d` |
| `fo3-exterior` | the open wasteland by day: land, ruins, sky | `Wasteland:18,24` |
| `fo3-busy` | a busy exterior: a town of many structures | `MegatonWorld:-1,-6` |
| `nv-shadows` | shadows of every kind under a low sun (hour 17) | `WastelandNV:-18,0` |
| `fo3-shadows` | shadows of every kind under a low sun (hour 16) | `MegatonWorld:-1,-6` |
| `nv-water` | the water shader with reflection and refraction over a lake | `WastelandNV:13,12`, camera above the water |
| `fo3-water` | the water shader with reflection and refraction over a river | `Wasteland:20,-17`, camera above the water |
| `nv-post` | post-processing (`bloomlinear`, `adjustments`) over a bright desert | `WastelandNV:6,15` |
| `fo3-post` | post-processing (`bloomlinear`, `adjustments`) over ruins and sky | `Wasteland:18,24` |

Every scene runs with the common settings of the list: a 1280x720 window at 30 frames a second, PNG screenshots, no
antialiasing, the default texture filtering, field of view and view distance, and shadows, the water shader and
post-processing off. A scene turns on what it is for (`settings` of the scene). The exteriors are drawn at 13:00 under
the game's clear weather (`NVWastelandClear`, `WastelandClear`).

### Adding a scene

Add an entry to `scenes`:

- `name` (unique; it names the round and the report's row), `game` (`nv` or `fo3`), `kind` (`interior`, `exterior`,
  `busy`, `shadows`, `water`, `post-processing` or a new word), `for` (one sentence: what a person must see in the
  picture for the scene to do its work);
- `start`: the start cell, an editor id or `<worldspace>:<x>,<y>`;
- `camera`: `yaw` and `pitch` in degrees (yaw 0 looks north, 90 east; pitch is positive down; without `yaw` the camera
  looks the way the start put the player), and `pos` ([x, y, z] in game units) for a camera at a fixed place; without
  `pos` the camera is 48 units ahead of the player's eye;
- optional `place` (`pos` [x, y] and `onGround`: where the player is put after the start), `hour`, `weather` (`master`,
  `form` as `0x` and six hexadecimal digits, `editor_id`), `settings` (`{"Section": {"key": "value"}}`), and `ignore` (a
  list of `{"box": [x0, y0, x1, y1], "why": "..."}`, fractions of the picture that are not compared, for something that
  cannot be held still; the reason is shown on the report).

A scene names places by editor id or coordinates only: no text and no file of the games goes into the list. Then run
`rounds --scene <name>` and `noise --scene <name>`, look at the picture (it must show what `for` says), and set its
limits from the noise runs. `test_picturecompare.py` checks the list's form and coverage.

## Holding a scene still

Two pictures can only be compared when everything except the renderer is the same. The scenario of a scene
(`scripts/openfallout/picture_scene.lua`, with the steps of the round runner's scenario library) does this:

| what would differ | how it is held |
|---|---|
| characters walking about | the AI is turned off as the first step, so the characters stop where they stand |
| the time of day, the sun | the game's clock is stopped (`TimeScale` 0) and set to the scene's hour |
| the weather | the scene's weather is forced and held over the region's (`ForceWeather <form> 1`) |
| random choices of the game | the engine is started with `--random-seed 1` |
| the player getting hurt (a player who stands in water drowns, and the menu the game then opens holds the world) | god mode; a scene whose simulation time stops before its pause time fails its round |
| the interface over the picture | the engine's head-up display and the crosshair are hidden, and message boxes (the notices of add-ons) are answered before the picture. The Fallout display (health, compass, action points) is a script of its own with no switch and stays in the picture: it is the same in both runs, and it is drawn by the interface renderer, which the Metal build changes too, so comparing it is wanted |
| the player's own body and its idle movements | the camera is in its static mode, ahead of the eye or at a fixed place |
| water waves, cloud drift, animated textures, particles, animations | the world is paused when the simulation time reaches 60 s: they stop at the same phase on both builds (the two pause times are in the report; they differ by at most one frame, 33 ms at 30 frames a second) |
| loading in progress | the place has 15 real seconds to load before the view is set, and the picture is taken 3 s after the pause; a scene that is not ready by 60 s of simulation time fails its round, and so does one that does not reach it within 150 real seconds: the round says whether something held the world (`held`) or the engine ran too slowly on a busy computer (`slow`) |

What is not held exactly, and why the comparison tolerates it:

- **The phase within one frame.** The pause comes on the first frame at or after 60 s of simulation time, so water,
  clouds and particles can be up to one frame apart. They move a few pixels at most; the tolerance per pixel and the
  windows absorb that, and the noise floor measures it.
- **Particles.** The particle emitters draw their random numbers from the engine's one shared generator, which
  `--random-seed` seeds, but everything else that is random draws from it too, frame by frame. Two runs that differ by one
  frame give the particles other numbers from then on, so smoke, dust and sparks differ between any two runs. What they
  change is in the noise floor; a scene whose noise runs are flagged by smoke or fire is turned away from it, or the
  region goes into the scene's `ignore` with the reason.
- **The first moments before the AI goes off.** Characters may have taken a step in the first frames of the scenario.
  The busy and shadow scenes measure this in their noise floor.
- **Loading.** That the place has finished loading (distant land, object paging) is not detected; it is given time.

Two small engine changes would close the first two exactly. Neither is made (the picture comparison does not change the
engine):

1. A fixed frame time for rounds: an environment variable (say `OPENFALLOUT_FIXED_FRAME_TIME=0.0333`) that makes the main
   loop of `OF::Engine::go` (`apps/openfallout/engine.cpp`) advance the simulation and the rendering simulation time by
   that many seconds every frame instead of the measured frame time. The scenario's pause would then come at exactly the
   same simulation time, after the same number of frames, on both builds.
2. A generator of their own for the particle emitters (`components/nifosg/particle.cpp` draws from `Misc::Rng`, the
   shared one), seeded from `--random-seed` where `OF::Engine::go` seeds the shared one (`Misc::Rng::init`) and drawn by
   nothing else: with the fixed frame time, the particles would then be the same from the start to the pause.

## The comparison

`picturediff.py` compares two pictures of the same size (it also runs alone:
`picturediff.py reference.png candidate.png --scene nv-busy --difference diff.png`). Two renderers never agree to the last
bit, and what must be caught is often small, so it takes several measures:

| measure | what it is | catches |
|---|---|---|
| `bad_share` | the share of pixels whose largest channel difference is above the tolerance (24 levels of 255) | many wrong pixels anywhere |
| `worst_tile_bad` | the same share in the worst window of 32 x 32 pixels (windows overlap by half) | one small wrong or missing object |
| `mean_shift` | the largest difference of the mean of one colour channel over the whole picture, in levels | a colour shift, the lighting changed everywhere |
| `worst_tile_shift` | the same in the worst window | lighting blown out in one part, a missing shadow |
| `ssim` | the structural similarity of the brightness, mean over 8 x 8 blocks (1 is the same structure) | a wrong or blurred texture |
| `worst_tile_ssim` | the mean structural similarity of the worst window | one wrong texture |
| `max_difference` | the largest difference of any pixel | shown only, never a limit |

A per-pixel tolerance lets last-bit rounding and dithering pass (a level or two everywhere). The share of bad pixels
lets texture filtering pass (a few scattered pixels far off). The windows make a small object count: an object of 12 x 12
pixels moves the mean of a 1280 x 720 picture by about a hundredth of a level, but fills 14 % of its window. The structure
measure sees a texture that is wrong but as bright as the right one. The mean per channel sees a colour shift that is
smaller than the tolerance everywhere. Antialiasing is off in every scene, so the edges of objects are not a difference.

The comparison is written with the standard library (it reads and writes PNG with `zlib`), because the hosted CI runner's
test environment has no numpy; it compares a pair of pictures in about a second.

## The limit

The limits are in one file, `scripts/openfallout/picture_limits.json`:

- `method`: the tolerance per pixel (24 levels), the side of a window (32 pixels) and of a block of the structure measure
  (8 pixels);
- `allowance`: what two renderers may legitimately differ by, the limits of every scene until its own are set: 1 % bad
  pixels, 10 % in the worst window, a mean shift of 2 levels, 12 levels in the worst window, a structural similarity of
  0.97, 0.75 in the worst window. They are first estimates from what rounding, filtering and dithering do;
- `margin`: how far above its noise floor a scene's limit sits (the worst value x 1.5 plus a pad for each measure);
- `scenes`: the limits of each scene, set from runs;
- `noise_floor`: what those runs measured, with the date, the number of runs and the build.

The limit is set from the first runs, on the OpenGL build, before the Metal build is judged:

1. Run `noise --profile gl.json` twice. Each run draws every scene twice on the same build and compares the two: what it
   measures is what one build varies by between two runs, the noise floor.
2. Look at every picture of the noise runs (`--all-pages`, or the PNG files of the rounds): each must show what its scene
   is for. A scene that shows the wrong place, or does not hold still (a flag in a noise run: a character moving, a large
   difference of water or sky), is fixed in the list and its noise runs are made again.
3. `limits <run 1>/report/results.json <run 2>/report/results.json --write --build "<commit> OpenGL"` records each scene's
   limits: for each measure, the worst value of the noise runs plus the margin, but never inside the allowance. A quiet
   scene keeps the allowance; a noisy one gets more room, by as much as it was seen to vary.
4. Commit the limits file. Its `noise_floor` says what was measured and when.

After the first run of the Metal build the owner looks at every flag. A flag that is a fault of the Metal build stays a
flag until the fault is fixed. A flag that the owner judges a legitimate difference of renderers (for example texture
filtering that differs on a high-contrast texture) raises that scene's limit for that measure by hand in the limits file,
in a commit whose message names the scene, the measure, the value and the owner's reason. Limits are never raised to make
a fault pass.

## After a flag

A flag means "a person must look", not "the Metal build is wrong". The page shows where the two differ (the red pixels and
the yellow windows). If the candidate shows a fault (a missing object or shadow, a wrong texture, a colour or lighting
change), it is a fault of the Metal build. If both pictures show the same thing and the difference is the kind two
renderers make, the limit is raised as above. If the round of a side failed, its `report.md` says why (often a scene that
was not ready before its pause time); run the scene again before judging it.

## Where things are

- `scripts/openfallout/picture_scenes.json`: the scene list.
- `scripts/openfallout/picture_scene.lua`: the scenario of a scene.
- `scripts/openfallout/picturecompare.py`: the tool; `scripts/openfallout/test_picturecompare.py` its tests.
- `scripts/openfallout/picturediff.py`: the comparison of two pictures; `scripts/openfallout/test_picturediff.py`.
- `scripts/openfallout/picture_limits.json`: the limits.
- The round runner's steps that hold a scene still (`r.pause`, `r.gameTimeScale`, `r.simulationTime`, `r.weather`,
  `r.ai`, `r.hud`, `r.view`) and its `"shot_format": "png"`: `scripts/openfallout/round/README.md`.
