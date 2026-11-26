# Rounds

A round is a scripted check of a build on the real game that does not need anybody's computer: the engine runs with
`--headless` (its window is created hidden and never shown, it takes no keyboard focus, grabs no pointer, makes no sound), a
scenario file drives the game from inside (teleports, camera, waits, screenshots, probes) and `round.py` writes a report.
Nothing presses a key or moves the pointer, so the person at the computer keeps working. A round that took an hour of
hand-driven game starts (a build, then one start for each question) is one command of a few minutes: every question of the
round is one step of the scenario, the questions that need different settings are `variants` that start side by side.

```
scripts/openfallout/round/round.py profile --engine <build>/openfallout    once per computer: writes ~/.config/openfallout-rounds.json
scripts/openfallout/round/round.py build --ref origin/<branch>              incremental build in one persistent folder
scripts/openfallout/round/round.py run scripts/openfallout/round/rounds/m13-mountains.json
scripts/openfallout/round/round.py preflight                                what the guards say about this computer now (starts nothing)
```

## What a run does

1. Makes a folder `<out>/<time>-<name>/<variant>/` with its own `config/` (a `settings.cfg` made of the player's own `settings.cfg`,
   the settings file of the profile if it has one, the defaults of `round.py`, the round file's `settings` and the variant's, each
   over the one before, and an `openfallout.cfg` of one line, `replace=config`), its own `userdata/` (a **copy** of the save) and a
   `data/` folder that holds the scenario. The player's own saves, settings and log are never written: the engine is started with
   `--config`, `--user-data` and `--data-local` (an empty folder, so that the add-ons the player installed are not loaded) pointing there. It does not read the player's configuration files (`--replace config` and the run's own `replace=config` skip them, the
   `openfallout.cfg` with its `data=` and `content=` lines too), so the runner lays the player's `settings.cfg` (view distance, field of
   view, terrain and the rest) under the run's itself.
2. Starts the engine at low priority (`taskpolicy -b` on macOS, `nice -n 10` elsewhere) with `OPENFALLOUT_HEADLESS=1`, the
   game's data folders and plugins of the profile, `--skip-menu --headless --no-grab`, `--load-savegame <the copy>` (or
   `--start <cell>`), the round's own `data` folders, and the content file `round.omwscripts` (the scenario player; a round with no
   scenario does not load it). With `"load_save": false` it gives neither
   `--skip-menu` nor `--load-savegame`: the engine stays at the main menu with the copy in its list of saves, for a menu script.
3. On macOS it asks `lsappinfo` every 5 s which application is frontmost: a run in which the engine ever was fails, because
   that is what takes the keyboard of the person at the computer.
4. Stops the engine when `limit` seconds are over, then writes `report.md` and `report.json` (the verdict, exit status, the `ROUND`
   lines of the log, the first ten distinct warnings and errors with their counts, the focus count) and copies the screenshots
   to `shots/<n>-<name>.jpg` (JPEG, quality 85; the engine numbers its files in the order they were taken and the scenario says
   which is which; with `"shot_format": "png"` they are kept as the engine wrote them). A `summary.md` in the folder of the round has
   one row for each variant.

The report holds numbers, record ids and the names the scenario gave, never a text of the game.

## The profile

`round.py profile --engine <path of the openfallout program>` looks for the games under `~/Games` (`Fallout New Vegas`,
`Fallout 3`, with or without `/Data`), and writes the machine's profile: `engine`, `resources` (for a macOS bundle:
`Contents/Resources/resources`), `games` (`data` folders and `content` plugins of each of `nv`, `fo3`, `ttw`; `content` is empty by default, which loads the plugins the selected game makes active, the same as the game does, so a save finds the plugins it was made with; the player's own `openfallout.cfg` is never read by a round (the run's `openfallout.cfg` says `replace=config`), because its `data=` and `content=` lines may name the other game; a save that needs a plugin which the game does not make active (a mod that only the player's own configuration named) needs it in the profile's `content` list, otherwise the round fails for the missing plugin; a non-empty `content` list replaces the automatic choice, so it must name every plugin the save needs, the game's own and its add-ons included, in load order), `saves` (the
folder that `"save": "latest"` takes the newest file from: the engine's own user-data folder,
`~/Library/Application Support/openfallout`, unless `profile --saves <folder>` says
where the game is played, and `profile` warns when the folder holds no save), `out` and, if wanted, `settings` (a settings file laid over the player's own `settings.cfg` and under the
round's). Edit it by hand for Tale of Two Wastelands (`data` = the New Vegas folder, then the output of its installer).
`round.py check <round file>` prints what a run would use without starting anything.

## The round file (JSON)

| key | meaning |
|---|---|
| `name` | name of the round (the folder of the results) |
| `game` | `nv`, `fo3` or `ttw`: a key of the profile's `games` |
| `scenario` | the Lua file of the scenario, relative to the round file or to this folder. A round whose every variant has `expect_exit` may have none: the scenario player (`round.omwscripts`) is then not loaded either (with nothing to play it would quit the game at once), and the round is judged by how the engine ends and by its log. The autoplay rounds below are such rounds |
| `data` | data folders of the run, relative to the round file (so that the file names no folder of one computer), each given to the engine with `--data` after the game's; `["../../autoplay"]` loads the autoplay tool |
| `save` | a file, or `"latest"`; without it `start` (a cell for `--start`, with `new_game` for `--new-game`). A `${NAME}` in it is the environment variable NAME (a save kept outside the repository, a fixture: `"save": "${OPENFALLOUT_ROUND_SAVE_NV}"`); a variable that is not set stops the round and says which one to set |
| `load_save` | `false`: the save is copied into a character folder of the run (`saves/round/`, where the engine's list of saves finds it) but not loaded; the engine starts at the main menu (no `--skip-menu`, which with no save would start a new game), where a menu script of the round loads it. The scenario runs once a game is loaded; until then the round waits, up to its `limit` |
| `settings` | `{"Section": {"key": "value"}}` for every variant, for example `{"Terrain": {"game lod": "true"}}` |
| `resolution`, `framerate` | the size of the window (so of the screenshot) and the frame rate limit (defaults 1280x720 and 20) |
| `limit` | seconds after which the engine is stopped (300) |
| `shot_format` | `"png"`: the screenshots are kept as the engine wrote them (`shots/<n>-<name>.png`, no JPEG, no resize), for pictures that a machine compares; a screenshot that is not a whole PNG is then missing (so the round needs `[General] screenshot format = png`) |
| `shader_values` | `{"technique": {"uniform": number or boolean}}`: the values of the uniforms of post-processing techniques, laid in the run's own `shaders.yaml` (where the engine reads the values a player chose; the engine's techniques have no uniform that a script may set, so a scenario cannot do it). The technique still has to be turned on, by `[Post Processing] chain`. The run's `report.json` records the values (the engine writes the file again in its own words when it quits). Used by the picture comparison |
| `variants` | a list of `{"name", "settings"}`: one engine start each, one after the other (never two engines at a time; `--parallel` above 1 is ignored), `--variant name` only that one; a variant can also have its own `env`, `extra_args` and expectations (below) |
| `extra_args` | more arguments for the engine (a variant's come after the round's) |
| `expect_exit` | the engine ends by itself with this exit status; the scenario then need not run, and may be left out (a refused start save: `--missing-content=refuse` ends with 4; an autoplay that played to its end: 0). An engine that is still running at the `limit` fails such a round |
| `expect_log`, `forbid_log` | regular expressions: each of `expect_log` must match a line of the log, none of `forbid_log` may; otherwise the round fails (`(?s)` lets one pattern span lines, to say what comes after what) |
| `expect_count` | `{"<regular expression>": n}`: exactly `n` lines of the log match, otherwise the round fails |
| `expect_gap` | a list of `{"from": regex, "to": regex, "min": s, "max": s}`: the first line that matches `to` after the first one that matches `from` is `min` to `max` seconds later by the time stamps of the engine's log (they pass midnight); a missing line, or a gap outside the limits, fails the round and the report says the gap measured |
| `keep_sound` | `true`: a run with sound (`OPENFALLOUT_HEADLESS_SOUND=1`) keeps it in `sound.wav` in its folder (32-bit float, about 23 MB a minute), for a script that measures it; without it the sound goes to /dev/null. Never to a device (see "What it cannot do") |
| `allow_missing_content` | accept a save that wants a game plugin (`.esm`, `.esp`) the run does not load; without it such a round fails (a missing `.omwscripts`, such as the plugin of a play session, is a note) |

## The scenario (Lua)

`scripts/roundscenario.lua` returns a function that is given `r` and runs as a coroutine, or a table `{ run = function(r) ...
end, limit = <seconds>, delay = <seconds before the first step> }`. Steps that wait (`wait`, `waitFor`, `shot` and all that ask
the player script) must be called from the function itself, not from inside a `pcall`. Every step logs a line that starts with
`ROUND`; a step that fails (a teleport to a place that is not there, a quest that does not exist) logs a `ROUND error ...` line and the
scenario goes on, so that the report shows everything that went wrong, but the round fails; an error in the scenario's own code
ends it (`ROUND error scenario`) and makes the round fail too. A screenshot that was asked for and never written fails the round
as well.

| step | does |
|---|---|
| `r.wait(seconds)`, `r.waitFor(fn, timeout, label)` | real seconds go by, or until `fn()` is true |
| `r.teleport{ pos = {x, y, z}, yaw, onGround, cell, cellId, worldspace, label }` | takes the player to a place (the cell of the position in the worldspace of the cell the player is in, or `cell` by name, `cellId`) |
| `r.look(yaw, pitch)`, `r.camera('FirstPerson')` | the camera (radians; a positive pitch looks down, as in the rest of the Lua API) |
| `r.shot(name[, false])` | an engine screenshot (what the game draws, the same as the screenshot key); it first waits (`r.settle`) until no message box has been open for 6 s, because the add-ons of a save open their notices one after the other when a place has loaded; `false` takes the picture at once |
| `r.settle([quiet[, timeout]])` | waits until no message box has been open for `quiet` seconds (6), at most `timeout` seconds (40); it gives at once when no box has been seen yet (a notice that has not opened by then is not waited for) and after `r.boxes(false)`, even if a box is open; a timeout is a note (`ROUND settle timeout`), and the `shot` line then says `box=<number>` if one is open |
| `r.eye(label)`, `r.ground(label, {{dx, dy}...})`, `r.actors(label, radius, count)`, `r.fps(label, seconds)` | log the player's place and camera, the height of the ground under the eye and offsets, the characters near the player (record id, distance, place), the frame times |
| `r.hour(h)`, `r.global(name[, value])`, `r.quest(name[, stage])`, `r.speed(scale)` | the game's clock, a global, a quest, the speed of the world |
| `r.object(form[, label])`, `r.activate(form)` | what the world says about an object (`"FormId:0x..."` or `{file, id}`), activate it |
| `r.boxes(false)` | a message box that the game opens covers the picture and nobody clicks it in a round, so it is answered with its first button after a second (`ROUND box` lines name its number, form and buttons, never its text; a `shot` line says `box=<number>` when one is open as the picture is taken); `r.boxes(false)` leaves the boxes open, `r.boxes(true)` answers again |
| `r.log(...)` | a note |
| `r.world`, `r.core`, `r.util`, `r.player()`, `r.lookup(form)` | the Lua modules and helpers, for steps of your own |
| `r.spawn{ record, bearing, dist, pos, yaw, count, label }`, `r.place(object, { dist, bearing, pos, yaw, label })`, `r.putAt(object, { pos, yaw, onGround, label })`, `r.give(record, count[, actor])` | M14: make an object of a record, put one at `dist` units from the player along `bearing` (radians, 0 north; it looks at the player unless `yaw`), or at a place of the player's worldspace (the height is taken to the ground there unless `onGround = false`), put items in an inventory |
| `r.equip([record[, pick]])`, `r.drawn(on)`, `r.weapon(label)`, `r.inventory(label[, actor])`, `r.god(on)` | the weapon of the player: put one of the Pip-Boy's list in the hand (the first ranged one with rounds when no record is given), out or away, what the rules say of it, what an actor carries, god mode |
| `r.fire(label[, target])`, `r.shoot{ shots, interval, target, shotName }`, `r.aimAt(object, label[, up])`, `r.use(on)` | the player shoots (at what the camera ray hits, or at `target`), turns the camera at an object |
| `r.move{ forward, side, run, sneak, seconds }`, `r.crouch(on)`, `r.mode(label)`, `r.status(label)`, `r.hp()` | walk, run or sneak for some seconds, crouch, the stance and the sneak meter's word, the player's health |
| `r.fight(actor, target[, label])`, `r.peace(actor)`, `r.state(label, actor)`, `r.track({{label, actor}...}, every)`, `r.untrack()` | start or end a fight of the engine (the player is `0x14`, as in the games), say what an actor is doing, sample the animation groups that play on actors now and then |
| `r.pause(on)`, `r.gameTimeScale(scale)`, `r.simulationTime()`, `r.weather(form)` | hold a scene still: the world paused (the simulation stops, the engine goes on drawing, and the scenario goes on), the speed of the game's clock (0 stops the sun), the simulation time that water, clouds, particles and animations follow, a weather (`"FormId:0x..."`) there at once and held over the region's (`ForceWeather <form> 1`) |
| `r.ai(on)`, `r.hud(visible)`, `r.view{ pos, yaw, pitch, forward, label }` | the characters' AI off or on, the head-up display and crosshair hidden or shown, a fixed view: the camera in its static mode at `pos` or `forward` units ahead of the player's eye along `yaw`, turned to `yaw` and `pitch` (radians). Used by the picture comparison (`docs/openfallout/picture-comparison.md`) |
| `r.pauseMenu([false])` | the pause menu opened the way Escape opens it (the window manager's main menu over the game), or closed with `false` |
| `r.vatsInfo(label[, actor])`, `r.vats([button])`, `r.messages(label)` | V.A.T.S.: its state, whether an actor can be a target and whether the ray to its head is clear (`clear` in the answer), open it or press its pad buttons, the notices the HUD gave |

`scenarios/m14_fight.lua` (`rounds/m14-fight.json`) is the survey of moving and fighting in New Vegas (first and third person, a fight with two
armed Powder Gangers, V.A.T.S., sneaking up on one), `scenarios/m14_arms.lua` (`rounds/m14-arms.json`, and `m14-arms-tune.json` with
`OPENFALLOUT_FIRSTPERSON_ARMS` variants) the pictures of the player's arms in the first person.

`scenarios/m14p_reactions.lua` (`rounds/m14p-reactions.json`; `m14p-effects.json` is the same with `OPENFALLOUT_EFFECT_HOLD=40`, which makes
muzzle flashes and impacts play 40 times slower so that a picture catches them) is M14 part two on New Vegas: a blow throws a character back,
two crippled legs slow it and make it limp, a character put up in the air falls and is hurt by the fall, the player's own body in the air and
turning. `scenarios/m14p_reactions_fo3.lua` (`rounds/m14p-reactions-fo3.json`) does the same with two placed raiders of Fallout 3 (the blows
are made with the form of a pistol, as the player's round has no way to give itself items), and `scenarios/m14p_swim.lua`
(`rounds/m14p-swim.json`) lets the player's body fall into the lake of the east and swim.

`rounds/m1-unattended-load.json` (scenario `scenarios/m1_unattended_load.lua`) loads the newest New Vegas save, whose content list
differs from the run's (it wants the plugin of a play session), once with `--missing-content=accept` (loaded, no box, the
`Load: ... loading it anyway (missing-content accept)` line) and once with `--missing-content=refuse` (not loaded, no box, exit status
4). A box would log `asking the player` and fail it; an engine that waited would time out and fail it.

`rounds/m1-refused-retry.json` starts at the main menu with the newest New Vegas save copied but not loaded (`"load_save": false`)
and adds the menu script `scripts/roundretry.lua` with `--content roundretry.omwscripts` (this folder is a data folder of every
round, so the file is found by name; nothing loads it unless a round names it). The script asks for the save with
`missingContent = 'refuse'`; the engine refuses it and calls `onLoadRefused`, and the handler asks for the same save once more
with `'accept'`. It passes when the refusal and the handler's line come once each, then the accept line and the load, with no box,
and the scenario of `m1-unattended-load` runs to its end in the loaded game. An engine that loses the second request stays at the
main menu: the round ends at its limit (60 s, so the engine is stopped after 90 s) and fails, its report says the accept and load
lines were not found and shows the last lines of the log.

`rounds/book-pickup-fo3.json` (scenario `scenarios/book_pickup_fo3.lua`) starts Fallout 3 in Arefu's abandoned shack and activates
the book that the game places there (a book made by `world.createObject` has no reference the scripts of Fallout know): the round passes when the book is in the
inventory, is gone from the world and no screen opened (the UI mode stays empty for three seconds). `r.inventory(label, actor,
record)` counts the items of a record (`carried`) and `r.mode` answers the UI mode (`ui`) for such checks.

`scenarios/smoke.lua` is the smallest scenario; `scenarios/m13_mountains.lua` with `rounds/m13-mountains.json` is the check of the
distant land of New Vegas in seven variants of `game lod debug`. Its places are in the Mojave and the save can be anywhere:
the scenario takes the worldspace from the cell of a reference that lives there (`anchor` lines say which one was found and which
worldspace it names; `eye` lines name the worldspace the pictures were taken in). It stops with a `ROUND error` when no such
reference is found and the save is in an interior.

## The autoplay rounds

`rounds/autoplay-nv.json`, `rounds/autoplay-fo3.json` and `rounds/autoplay-ttw.json` play the New Game of each game with the
autoplay tool (`scripts/openfallout/autoplay`, its README says what it does) through the intro to the first stages of the main
quest; `rounds/saveload-nv.json` and `rounds/saveload-fo3.json` do the same with its probe of the saves (`autoplaysaves.omwscripts`: the game is saved where the
`saves` table of `autoplaygates.lua` says, loaded back in the same engine, checked and saved again, and the intro then plays on to
its end). They have no scenario: the tool ends the game itself (`core.quit()`) when the run reaches the stages of its `finish`
table (`AUTOPLAY intro over`), so each round says `"expect_exit": 0` and is judged by the end of the engine and by the
`AUTOPLAY` lines of the log:

- every round expects `AUTOPLAY intro done` (the intro reached its last stage) and `AUTOPLAY intro over` (the run reached its
  `finish` stage and the tool quit the game), and forbids the lines by which the tool gives up or cannot go on: `time is up` (its
  own time limit), `level stuck`, `screens error`, `talk wait error`, `fire error`, `fire done` for `time`, `ammo` or `error`, and a
  `gate` line that does not end in `ok`;
- the save/load rounds also expect `digest saved`, `save autoplay-save ok`, `digest loaded`, `digest check ok` and
  `save autoplay-again ok`, in that order and before `intro over`, the save, the load, the check and the second save once each;
  each save line must name the file it wrote (`files=` not empty: the engine catches a failed write itself, so the probe's
  `pcall` says `ok` either way). They forbid `digest check MISMATCH`, a load with `no slot` or an `error`, a save that does not
  say `ok`, and the engine's `Failed to save game:`.

A crash (any exit status but 0), an engine still running at the round's `limit` (the intro stood still), or any of these lines
fails the round; nothing has to be read by hand. The error `Unable to attach OpenGL debug callback.` that a run with
`OPENFALLOUT_DEBUG_OPENGL=1` logs on macOS (its OpenGL has no debug output) does not decide a round: the verdict counts the log's
warnings and errors but does not fail on them, it fails on `ROUND error` lines of a scenario and on the expectations. The
autoplay rounds do not set the variable.

The rounds name the master of the game (`--content FalloutNV.esm`, `--content Fallout3.esm`; none for Tale of Two Wastelands,
whose plugins the game makes active), so the profile's `content` for `nv` and `fo3` must be empty for them (a master named twice
stops the engine). The tool is the one of the checkout the round file is in: run the round from a checkout of the commit the
engine was built from, since the `saves` table and the gates go with the engine. They run with sound sent to /dev/null, as the
Mac runs that made the tool's tables did, and take minutes: New Vegas about 15, Fallout 3 and Tale of Two Wastelands 25 to 40
(the `limit`s are 1,800 and 3,300 s; `--wall` and any wall limit around round.py must be longer).

```
scripts/openfallout/round/round.py run scripts/openfallout/round/rounds/saveload-nv.json
```

## The rounds of step 4: the player's rules, measured

Step 4 makes the player a Fallout actor that follows the rules of the original games (the rules spec: `movement`, `jump-fall-swim`,
`death-condition-settings`, `stats-levelling`, from the exes of both games). These rounds check the numbers of the rules, within a
tolerance that is stated in the scenario and comes from the way the number is measured, and a number outside it **fails the round**
(a `ROUND error` line: round.py's verdict is FAIL and the report lists it under the scenario errors). They never retry, widen a tolerance
or skip. Each logs what it measured in one line that is found with `grep 's4 measure'`:

```
ROUND note s4 measure walk_holstered_api=84.700 expect=84.700 tol=0.05 ok
ROUND note s4 measure walk_holstered_ground=84.690 expect=84.700 tol=5% ok
ROUND error s4 measure run_holstered_ground=270.1 expect=338.800 tol=5% FAIL the player does not cover the ground at the speed of the rule
```

`tol` is the difference that is allowed: a percentage of the expected number, an absolute number, or `3%|1` (either is enough).
`s4 observe` lines hold numbers that are looked at and not judged.

| round (`rounds/s4-<name>-nv.json` and `-fo3.json`) | what it checks |
|---|---|
| `movement` | the walk, run and sneak speeds, holstered and with a pistol drawn (84.7 and 77.0 walking, ×4 running, ×0.57 sneaking); each is measured twice, **the speed number of the game** (`Actor.getCurrentSpeed`, ±0.05) and **the ground the player really covers** (the sum of the steps in x and y in a window of 3 to 4 s that starts a second after the first step, ±5 %), and the ratio of the two is logged (the spec has not verified that they are the same); the weight (Fallout 3 loses speed from the first pound up to 150 lb, New Vegas does not), and over the carry weight: the message of the game, and no running |
| `jump` | the height of a jump standing (`fJumpHeightMin` 64 units, ±8) and that three more presses of the key in the air do not lift the player again (no double jump) |
| `fall` | the height lost and the health taken in two drops from 350 and 700 units (the rule `0.025 × (lost − 600) ^ 1.65` at the height measured, ±3 % or 1 point; 0 and 49.8), with god mode off and the health set first |
| `death` | the player is dead at a health of 0.5 (below 1.0); the game is saved, the player dies, and **no box shows** while the newest save **loads 5.0 s ± 0.6 s later** (`expect_gap` from the line of `falloutdeath.lua` that sees the player dead to its line that the timer is over; `expect_log`: the engine's `Loading saved game 's4-death'` comes after that). The scenario saves under that name first and leaves a mark (`r.mark`) in the save: the loaded game starts the scenario again from its first line and finds the mark, and checks that the player is alive with the health of the save. The case of no save at all (the main menu opens) is not covered |
| `levelup` | the experience of the level (`xp_to_next_level`), the skills from the SPECIAL of now (and again after Luck and Intelligence are put up by two: they must follow), the experience to the next level and then the screen: the points to spend (13 New Vegas / 15 Fallout 3 at Intelligence 5), the perk rate, the maximum health after the level (205 / 210), and two levels at once (two screens in order) |

Their scenarios are `scenarios/s4_<name>_<game>.lua` (one line each) and `scripts/s4rounds.lua`; the rules and the way a number is judged
are in `scripts/s4common.lua`. The numbers come from the player script (`roundplayer.lua`, handlers `s4stats`, `s4move`, `s4jump`,
`s4drop`, `s4levelup`, `s4message`, `s4dead`), which reads the game through the Fallout-backed calls only: `self.position`,
`Actor.isOnGround`, `Actor.getCurrentSpeed`, `core.pipboy.stats()` / `attributes()` / `skills()` and the level-up calls of `core.pipboy`,
`core.hud.takeMessages()`. None is a Morrowind stats function. The first lines of that block of `roundplayer.lua` list them.

**The saves.** The rounds start from a save that is not in the repository (it is a game file). New Vegas: `scripts/openfallout/autoplay` saves the game
once `VMQ01` stage 50 has stood for five seconds with no screen open (description `s4-fixture-nv`, in `saves.fixtures` of `autoplaygates.lua`),
so `saveload-nv` leaves one: the player is outside the intro, with its controls, at level 1. Fallout 3: the tool cannot write one (the
stage after the vault, `MQ01` 40, is a conversation with a screen open the whole time, and a save then stalls the run), so make one by
hand: play New Game to the Wasteland and save, or take any save past the vault (the one the rounds of this branch ran on was written by an
earlier build of the tool at `MQ01` 40, level 2, in the interior cell 0x1003a35; the rounds teleport the player to the arena and turn god mode
on or off themselves, so where it stood does not matter). Give the files to the rounds with environment variables (for New Vegas the path
is the one the log names on the line `AUTOPLAY save s4-fixture-nv ok dir=... files=...`, in the `userdata/saves` of the run):

```
export OPENFALLOUT_ROUND_SAVE_NV=/path/to/s4_fixture_nv.omwsave        # for rounds/s4-*-nv.json
export OPENFALLOUT_ROUND_SAVE_FO3=/path/to/s4_fixture_fo3.omwsave      # for rounds/s4-*-fo3.json
export OPENFALLOUT_ROUND_OLDSAVE_NV=/path/to/master94366e3-autoplay_save.omwsave   # for rounds/s4-oldsave-nv.json
```

A save is the engine's own and loads on the engine that wrote it and later ones; `s4-oldsave-nv` is the save written by master's engine
at `94366e3` (before the step that removed the Morrowind dialogue), kept to check that old saves go on loading with the whole player.

**The old-save round** (`rounds/s4-oldsave-nv.json`, no scenario) loads that save with the probe of the saves (`autoplaysaves.omwscripts`)
and lets the tool play the intro on to its end: it expects `digest loaded` with the quests of the save, the line that says the player of
the loaded game, `digest check ok`, the save written after (`autoplay-again`) whole, and `intro over` (the engine ends by itself with
status 0), and forbids every line by which the tool gives up. It was `s3-oldsave-nv.json` of the step-3 proof, which waited a fixed
minute: a wait of a fixed time failed when the Vigor tester held the world for longer, so the end is the tool's own.

**The digest of the saves** (`scripts/openfallout/autoplay/scripts/autoplayprobe.lua`, `saveload-nv` and `saveload-fo3`): besides the
quests, the probe saves the **player** with the game (in `onSave`) and compares it on the first frame after the load; `digest check ok`
means the quests and the player are those of the save. The player is: where it stands (the cell, and the position within 150 units), the
level, the experience, the health (within 1 point) and the maximum health, SPECIAL (base and now of each of the seven) and how many items
it carries. Each is read from a call of the Fallout player, none from a Morrowind stats function:

| field | call |
|---|---|
| cell, position | `world.players[1].cell`, `.position` |
| level, experience | `core.pipboy.stats().level`, `.experience` |
| health, maximum health | `core.pipboy.stats().health`, `.maxHealth` |
| SPECIAL (base, now) | `core.pipboy.attributes()[i].base`, `.value` |
| items (units and rows) | `core.pipboy.items()`: the sum of `count`, and the number of rows |

The lines are `AUTOPLAY player saved ...` and `AUTOPLAY player loaded ...` (a mismatch is `AUTOPLAY player MISMATCH <field>` and the check
says `MISMATCH`; a call that fails is `AUTOPLAY player error`); a save from before the player was in the digest says `no digest in this save`
and only the quests are compared.

## What it cannot do

- `round.py run` works on macOS only, as the engine does; elsewhere it stops with a message. `profile` and `check` do not
  need a Mac.
- A screenshot is matched to its `shot` line by file order (the engine writes `screenshotNNN` one after the other and `r.shot` waits
  for its file); with `preload num threads` above 1 and two shots asked for back to back (`r.shot(name, false)`) two writes could swap.
- It sees what the engine draws, not what the player's eye would: for a look at a screen that a pointer or a key opens (the
  Pip-Boy, a conversation) the scenario must open it through the game's own scripts or events, the way `autoplay` does.
  The screens of the making of the character (the SPECIAL book, the Vigor Tester, the face) take a key or a pad's button
  from the event `FalloutChargenPress` sent to the player (`{ key = <scancode> }` or `{ button = <pad button> }`; the
  triggers are 100 and 101), which goes where the real key or button goes.
- Sound is off unless `OPENFALLOUT_HEADLESS_SOUND=1`. Nobody may hear a round: with sound on, the runner sends it to `sound.wav`
  in the run's folder (with `"keep_sound": true` in the round file; otherwise to /dev/null) through OpenAL Soft's wave backend (`ALSOFT_DRIVERS=wave` and a configuration file of the run named by
  `ALSOFT_CONF`; a round or a variant cannot change either), and it refuses an engine that links Apple's OpenAL framework (or one whose libraries it cannot read), which
  reads neither and would play through the speakers. The file is written in real time; a run the runner stops may leave its header
  sizes unfinished, so read it as a stream after the header.
- The engine never reads headphone motion in a round: head tracking stays off in a headless run whatever the settings say
  (a request for motion could show macOS's permission box, or macOS could end the run). The log says so.
- An engine that died early once (at launch, in the loader) could make macOS offer to reopen its windows at the next start.
  The engine tells AppKit to ignore saved window state for itself before anything makes its application object; the log
  line `Saved window state: ignored (told before any application object existed)` shows it.
- Software and hardware drawing differ: on a Mac without a GPU (a hosted CI runner, which gives Apple's software renderer)
  the frame times mean nothing, and a driver-specific fault (a texture format) may not show.
- The first time on a Mac, whether a hidden window really stays off the desktop and never takes the focus is what the focus
  count of the report says; a report with `frontmost in 0 of N samples` is the proof, anything else is a failed round.

## Building

`round.py build --ref origin/<branch>` keeps one worktree and one build folder (next to the repository: `round-worktree`,
`round-build`), checks the ref out in it and builds only the `openfallout` target at low priority with ccache, so a change of a few
files rebuilds those files and not the whole engine. The first run configures with `-DCMAKE_BUILD_TYPE=Release` (more
arguments of the configuration with `--cmake-arg`); later runs reuse the folder.

## On a Mac

```
cd <the repository>
python3 scripts/openfallout/round/round.py build --repo . --ref origin/<branch> \
    --worktree ../round-worktree --build-dir ../round-build --cmake-arg ...    # the arguments of your last configuration
python3 scripts/openfallout/round/round.py profile --engine ../round-build/OpenFallout.app/Contents/MacOS/openfallout
python3 scripts/openfallout/round/round.py run scripts/openfallout/round/rounds/mac-focus-proof.json
```

An existing worktree and build folder can be given to `build` (`--worktree`, `--build-dir`): it checks the ref out in the worktree and
builds what changed. `rounds/mac-focus-proof.json` starts the smoke scenario three times, each with another way of keeping the
application off the desktop (`hidden`: only the hidden window, `accessory` and `prohibited`: also the activation policy that
`OPENFALLOUT_HEADLESS_POLICY` sets, which makes the application one that cannot be made the frontmost). Its summary table has the
focus column: `0/N` is what a headless run must show. While it runs, use the computer as usual; if a window appears, a Dock icon
bounces or a key goes missing, say which variant it was (they run one after the other, about a minute each).

## Guards

On 2026-11-12 a Mac panicked during a round. WindowServer was blocked for 40 s behind a stuck system daemon (`runningboardd`), the
watchdog killed it, its replacement never finished starting, and `watchdogd` brought the machine down 120 s later. No build was
running; the hidden engine was one of the processes that were stuck, and why the daemon stalled was not found (the one lead is a
lock wait in the kernel's network code). A hidden engine is still a client of the window system: every frame asks it for a flush.
The guards (`roundguard.py`, tests in `test_roundguard.py` and `test_round.py`) follow from that. They are meant to stay out of the
way: they refuse only where a round cannot do its work, and warn about the rest.

- **One heavy thing at a time.** `build` and `run` take the same lock (`<out>/guard/round.lock`); the second is refused with the name
  of the holder. On a Mac a run waits out 15 s after a build or a run that ended a moment ago. The variants of a round run one after the
  other (`--parallel` above 1 is ignored). A build is held to six jobs and runs at low priority (`--allow-heavy` lifts it).
- **Pre-flight** (`round.py preflight` prints it without starting anything; every build and run does it first). It reads the free memory,
  the swap, the thermal state, the load, the processes (a build, the game, GeForce NOW, browser helpers), the crash reports (a
  WindowServer report since the boot, a panic or reset report nobody has acknowledged: `round.py ack-panic`) and the WindowServer
  process, and **warns** about each; the warnings are kept in the summary. It **refuses** only when the kill switch is set or there is
  no WindowServer to ask (a run also asks it once, and refuses when the call is not answered: a heartbeat that cannot reach it would
  guard nothing).
- **Heartbeat** (Mac). One thread of the runner asks WindowServer for the windows on screen (`CGWindowListCopyWindowInfo`) every
  2 s, and reads its pid every 10 s: no process is spawned for it. When an answer is more than 10 s late, never came, or the pid is
  another one (WindowServer was killed and started again), the engine's process group gets SIGKILL at once, the round ends, and the
  report says `STOPPED BY A GUARD` (exit status 3). Every report and the summary say `WindowServer same pid: yes (pid N)`.
- **Kill switch and time limit.** `round.py stop` (or `touch <out>/guard/STOP`) ends a running engine at once and refuses new rounds and
  builds until `round.py resume`. `--wall <seconds>` (default 3600) ends the engine when the whole round takes longer.
- **A smaller engine.** On a Mac the engine is started as an accessory application (`OPENFALLOUT_HEADLESS_POLICY=accessory`: no Dock icon,
  no menu bar, never frontmost; `"OPENFALLOUT_HEADLESS_POLICY": "foreground"` in the round's or the variant's `env` keeps the old
  behaviour), without the display-sleep power assertion that SDL takes (`SDL_VIDEO_ALLOW_SCREENSAVER=1`, also set by the engine itself
  when it is headless), at 20 frames per second and with the focus asked every 5 s. The report says which policy the engine set
  (`activation policy: accessory set`).
- **No debugger, crash log kept.** The engine's `PATH` is an empty folder of the run (`<run>/no-programs`) and its `TMPDIR` is
  `<run>/tmp`: its crash handler starts `gdb` or `lldb` from `PATH`, and a debugger that attaches can put a password box on the screen;
  the crash log (`openfallout-crash.log`) lands in `<run>/tmp`. The runner starts `taskpolicy` by its full path. A round's or a
  variant's `env` can set either.
- **Evidence for next time.** When the computer has a window system to watch, each run keeps in `<run>/diagnostics/` the crash reports
  that appeared during it (WindowServer, `.panic`, reset counters) and the last five minutes of the log of `runningboardd`.

None of this can prevent a hang inside the kernel; it ends the load of the round quickly and leaves the evidence.
