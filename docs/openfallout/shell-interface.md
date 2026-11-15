# The interface the shell relies on

The shell is a native Mac app in another repository. It starts this engine and tests it unattended: play, save, quit,
load. It drives the engine only through what this page lists: the command line, the Lua functions of the menu package,
two engine handlers of menu scripts, the log lines below and the exit status. There is no second control interface.

## The rule

What this page lists is not changed without notice. A change (a name, an argument, a value, a log line, an exit
status) needs an entry on this page and in the release notes of the engine release that brings it.

A machine holds the rule: `apps/openfallout_tests/shellinterface.cpp` asks the real option parser and the real Lua
menu package for every option and function listed here, and fails when one disappears or is renamed.
`CI/check_documentation.py` fails when this page and that test do not name the same options and functions, or when a
handler listed here is not an engine handler of menu scripts.

## Command-line options

| option | what it does |
|---|---|
| `--config <folder>` | a configuration folder (`openfallout.cfg`, `settings.cfg`); the log `openfallout.log` is written in the last one read |
| `--replace config` | leave out the configuration folders of the user, so only the ones given are read |
| `--user-data <folder>` | the folder for saves (`saves/<character>/<slot>.omwsave`) and screenshots |
| `--resources <folder>` | the engine's resources folder |
| `--data <folder>` | a data folder (repeatable; later ones win) |
| `--data-local <folder>` | the data folder with the highest priority |
| `--content <file>` | a content file to load (repeatable): a game plugin, or an `.omwscripts` file that names the caller's own Lua scripts (a line `MENU: scripts/<name>.lua` adds a menu script) |
| `--load-savegame <file>` | load this save when the engine starts |
| `--skip-menu` | do not show the main menu at the start |
| `--new-game` | with `--skip-menu`, start a new game |
| `--headless` | the window is created hidden and never shown; no focus, no pointer, no sound (`OPENFALLOUT_HEADLESS=1` in the environment does the same) |
| `--no-grab` | do not grab the pointer |
| `--no-sound` | no sound |
| `--missing-content <answer>` | `ask`, `accept` or `refuse`: see below |

## The answer for a save with missing content

A save lists the content files it was made with. When some of them are not loaded now, the engine asks the player
in a box whether to load the save anyway. The caller can answer that box in advance:

| answer | without `--headless` | with `--headless` |
|---|---|---|
| `ask` (the default) | the box is shown and waits for the player | no box; the save is loaded, as before this option |
| `accept` | no box; the save is loaded | the same |
| `refuse` | no box; the save is not loaded | the same |

`--missing-content` applies to every load of the run: the save of `--load-savegame`, the load screen, quick load and
`menu.loadGame`. One `menu.loadGame` call can give its own answer, which wins for that load. A save whose content files
are all loaded is always loaded. A word that is not an answer stops the engine at the start with exit status 1.

## Lua: the menu package

Menu scripts reach these as `require('openfallout.menu')`. The full reference is
[menu.lua](../../files/lua_api/openfallout/menu.lua).

| function | arguments | result |
|---|---|---|
| `getState()` | none | one of `STATE.NoGame`, `STATE.Running`, `STATE.Ended` |
| `STATE` | (a table) | `NoGame`, `Running`, `Ended` |
| `newGame(options)` | `options` optional: `{ bypass = true }` skips the start of a new game | nothing; the game starts on the next frame |
| `loadGame(directory, slotName, options)` | the save's folder and file name, as `getSaves` gives them; `options` optional: `{ missingContent = 'ask' \| 'accept' \| 'refuse' }` | nothing; the load happens on the next frame. An error when the save does not exist or the answer is another word |
| `saveGame(description, slotName)` | a description; `slotName` optional, an existing save of the current character to write over | nothing; the save is written at once. Only from an engine handler or an event handler, while a game runs |
| `getSaves(directory)` | a save folder | a table: file name of each save to `{ description, playerName, playerLevel, timePlayed, creationTime, contentFiles }` |
| `getAllSaves()` | none | a table: folder name to the table of `getSaves` |
| `getCurrentSaveDir()` | none | the folder of the current character, or nil |
| `deleteGame(directory, slotName)` | the save's folder and file name | nothing; an error when the save does not exist |
| `quit()` | none | nothing; the engine ends after the frame with exit status 0 |

## Lua: engine handlers of menu scripts

| handler | when |
|---|---|
| `onStateChanged()` | the result of `getState()` changed: a game started, was loaded, or ended |
| `onLoadRefused(info)` | a load did not happen because the answer was `refuse`; `info.directory`, `info.slotName` name the save, `info.missingContent` lists the content files that are not loaded |

A load that the player refuses in the box does not call `onLoadRefused`; it calls `onStateChanged`, with no game.

A refused load ends the game that was running, the same as a "no" in the box: the engine leaves the current game
before it reads the save, and after a refusal it is at the main menu with no game. A caller that wants to keep the
running game saves it first. An `onLoadRefused` handler may ask for the same save again with `loadGame` and another
answer, such as `{ missingContent = 'accept' }`; that load happens on the next frame.

## Outcomes: log lines and exit status

The log is `openfallout.log` in the last configuration folder read (with `--config <folder> --replace config`, that folder). Each line starts with a time stamp and a level. These lines
keep their words:

| line | means |
|---|---|
| `Load: <n> content file(s) of the save are not loaded, loading it anyway (missing-content accept)` | the answer accepted the save |
| `Load: <n> content file(s) of the save are not loaded, not loading it (missing-content refuse)` | the answer refused the save |
| `Load: <n> content file(s) of the save are not loaded, asking the player` | the box was shown |
| `Headless: <n> content file(s) of the save are not loaded, loading it anyway` | a headless run with the answer `ask` loaded the save |
| `Load: the save given with --load-savegame was refused, quitting with exit status 4` | see exit status 4 |
| `Loading saved game '<description>' for character '<name>'` | a save is being loaded |
| `'<description>' is saved in <time>ms` | a save was written |

| exit status | means |
|---|---|
| 0 | the engine ended normally (`menu.quit`, the game's own quit) |
| 1 | a fatal error, including a wrong `--missing-content` word |
| 4 | the save of `--load-savegame` was refused by `--missing-content=refuse`; the engine quits at once and does not wait at the main menu |

An unattended caller tells a refused start by the exit status alone. It does not need to read the log or wait for a
time-out.

## What is not promised

- Any Lua function, field or handler that is not on this page, in the menu package or elsewhere.
- Any command-line option that is not on this page.
- The words of any log line that is not on this page, and the time stamp format of the log.
- The save file format between engine releases: a save may need a newer engine, and an old save may stop loading.
- How long a load, a save or a frame takes.
- Boxes other than the one for missing content. A load or a save that fails (a damaged file, a full disk) still shows a
  box with one button and waits for it.
- Anything about the window, the screen or the sound of a run without `--headless`.
