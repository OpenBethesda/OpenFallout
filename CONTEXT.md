# OpenFallout

An open-source engine that plays Fallout 3, Fallout: New Vegas and Tale of Two Wastelands from the player's own copy of the games, started from OpenMW. This glossary holds the words the project uses about its own direction and platforms.

## Language

### Platform direction

**Mac first**:
macOS on Apple Silicon is the platform that decides what gets built and tested; other platforms keep building but are not what work is judged by.
_Avoid_: Apple first, Mac priority

**Mac Native**:
The further step beyond Mac first, made of four parts: a Metal renderer, OpenBethesda as the shell, native bridges to Apple's frameworks where input or sound gains from them, and a Mac-only codebase.
_Avoid_: native port, Mac port

**Metal renderer**:
The drawing part of the engine in a form where every frame reaches the screen through Metal and Apple's OpenGL is never loaded. It can be translated or native.
_Avoid_: Metal port, Metal backend

**Translated Metal renderer**:
A Metal renderer in which the engine still writes OpenGL ES and a translation layer below it turns that into Metal.
_Avoid_: native renderer (it is not), ANGLE port

**Native Metal renderer**:
A Metal renderer in which the engine's own code calls Metal, with no translation layer and no OpenGL dialect.
_Avoid_: real Metal, true Metal

**Mac-only codebase**:
A source tree in which Windows, Linux and Android are no longer built or tested, and Mac-only APIs may be used at the edges without a portable layer. The core stays plain C++.
_Avoid_: Mac exclusive

**Core**:
The parts of the engine that hold the game itself: records, scripts, world and gameplay rules. It makes no Apple calls.
_Avoid_: engine core (the whole program is the engine), backend

**Edges**:
The parts of the engine that touch the machine: window, sound, input, drawing and packaging, and every native bridge. Mac-only code is allowed here and nowhere else.
_Avoid_: platform layer, glue

**Shell**:
The application a player opens to choose a game, a set-up and a save, and which then starts the engine. On the Mac this is OpenBethesda, a separate project; it is not a launcher inside this repository.
_Avoid_: launcher (that word means the Qt launcher in this repository), front end

**Engine route**:
OpenBethesda's term for one exact combination of game, data source, engine build and platform that has passed its play-and-save journey. OpenFallout is the engine of the Fallout 3 and New Vegas routes.
_Avoid_: integration, backend

**Engine release**:
A packaged app of the engine that this project publishes with a version, signed and notarized. The shell fixes on one engine release and starts it; the shell never builds the engine.
_Avoid_: engine drop, shell build, pinned artifact

**Reference set-up**:
The original games running on the same Mac under a Windows translation layer, kept only to compare behaviour against. Never shipped and never part of the engine.
_Avoid_: reference build, original build

**Native bridge**:
A small Mac-only part that reaches one Apple framework directly for a feature a kept library cannot give. It sits beside that library and does not replace it.
_Avoid_: wrapper, shim, native layer

**Supported**:
Said of a macOS version or a Mac that the project checks itself, by automated build and package check or by the owner playing on it. The packaged app starts there and bug reports from it are accepted; anything older is refused.
_Avoid_: compatible, works on, should run

**Played on**:
Said of a Mac and macOS version on which the owner has run the real games. A narrower claim than supported, and stated separately.
_Avoid_: tested (too vague: it does not say whether a person played or a machine checked)

**Packaged app**:
The signed application bundle inside the disk image that a release publishes; the artifact a player actually runs, as opposed to a binary in a build folder.
_Avoid_: bundle build, the build

### Engine structure

**Morrowind-only code**:
Code inherited from OpenMW that runs only for Morrowind content and never in a Fallout 3, New Vegas or TTW game. It is removed in steps; old saves keep loading by skipping the records it wrote.
_Avoid_: legacy code, dead code (dead code is broader)

**Game menu name**:
The name the original game gives a screen, as in its exe and its `menus/*.xml` files (`DialogMenu`, `BarterMenu`, `ContainerMenu`, `RaceSexMenu`). The code that draws a Fallout screen, its GUI mode and its Lua name use the game menu name; a Fallout screen with no matching game menu is called `Fallout` plus what it is (`FalloutLoading`). A C++ window that is only a fallback behind a screen Lua draws keeps a plain name, so one game menu name never means two things.
_Avoid_: the OpenMW window name (`DialogueWindow`, `TradeWindow`) for a Fallout screen

**Fallout screen**:
A screen of the game as the player sees it in Fallout 3 or New Vegas, drawn from the game's own `menus/*.xml` file, almost always by a Lua script (`falloutdialogue.lua` draws `DialogMenu`). A window that exists only so Lua can replace it is not kept; Lua can register a window of its own.
_Avoid_: Morrowind window (the OpenMW window classes in `mwgui`), menu (on its own, ambiguous with the main menu)

**Fallout actor**:
An actor of the engine that follows the rules of Fallout 3 and New Vegas, in the family Actor → Character → PlayerCharacter, and Actor → Creature. A Character is an NPC_ record or the player; a Creature is a CREA record. Since step 4a the player is the PlayerCharacter of the game's own records: the reference PlayerRef (0x14) of the NPC_ record Player (0x7).
_Avoid_: NPC (for the player), Morrowind player, CharacterController

**Process tier**:
How much of an actor the game runs each frame: High, Middle High, Middle Low or Low, from the state of its cell and whether its 3D is loaded (fallout-rules.md part A). An actor whose parent cell is unknown is in the Low tier (FalloutNV.exe 0x4511E0).
_Avoid_: active actor, processing range (the Morrowind distance rule)

**Character physics**:
The body of the player as the executables make it: an upright capsule of radius 31 times the actor's size, half height 66.22, walking on slopes up to 47 degrees. Other actors stay kinematic until step 4b.
_Avoid_: actor collision (ambiguous with the NPCs' kinematic bodies)

**Executable default**:
The value a game setting has when no plugin carries it, read at start-up from the player's own FalloutNV.exe or Fallout3.exe (texts and numbers). The value of a plugin wins over it. Nothing of it is kept in the repository.
_Avoid_: GMST fallback, hand-copied default

**Out of health**:
An actor with less than 1.0 health, which the games treat as dead (or down, for an essential one) after any change of the health (`FalloutScript::outOfHealth`).
_Avoid_: health 0, zero health

**Player section**:
The FOSV section `player` of a save: the player's cell, position, rotation, last exterior position, RefNum, run and sneak flags and Lua scripts. An old save's PLAY record is converted to it once and then dropped.
_Avoid_: PLAY record (only for the old record)
