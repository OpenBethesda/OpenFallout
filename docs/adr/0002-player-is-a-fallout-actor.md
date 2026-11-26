---
status: accepted
date: 2026-11-24
---

# The player is a Fallout actor with the original games' rules

The player used to be a Morrowind NPC: a placeholder NPC_ record with a made-up race and class, Morrowind stats (NpcStats, CreatureStats), the Morrowind CharacterController and skeleton, and Morrowind magic, AI and combat code around it. We decided to make the player the Fallout actor of the game's own records (PlayerRef 0x14 of NPC_ 0x7), on character physics, with the rules read from each game's executable (TTW uses New Vegas's), and to remove the Morrowind player mechanics in the same step (step 4a of the Morrowind removal). The rules are in `docs/openfallout/fallout-rules.md`; the terms are in `CONTEXT.md`.

## Considered options

- **Keep the Morrowind actor and patch Fallout values into it.** Rejected. Every rule would be a translation between two models, and the Morrowind code (magic, AI, combat, stats) would keep running beyond construction for no Fallout purpose.
- **Rewrite the player and the NPCs together.** Rejected for this step. The NPCs stay kinematic and keep their packages; step 4b moves them onto the same actor family and physics, which the class split (Actor → Character → PlayerCharacter, Creature) is made for.
- **Copy the executables' default settings into the repository.** Rejected. The defaults are read from the player's own executable at start-up (`components/falloutscript/exesettings.*`), as the texts already were, so nothing of the games is stored here and the 44 hand-copied fallbacks are gone.

## Consequences

- Saves have a FOSV `player` section. An old save's PLAY record is converted once (position, cell, last exterior position, Lua scripts, RefNum, run and sneak flags) and dropped, with the placeholder NPC_, race and class.
- A game without its executable next to its data has no setting defaults: the engine logs an error and each missing setting reads 0. The Lua scripts keep a few nil guards for that case.
- The death flow is the games' own: after `fPlayerDeathReloadTime` the newest save loads, or the main menu opens; the box shows only when the setting is 0 or below.
- The player animation is the Fallout one (`FalloutPlayerAnimation` and `FalloutPlayerBody`); the Morrowind CharacterController, NpcAnimation and placeholder skeleton are gone.
- Morrowind magic, AI, combat, pathing and stats are removed; the Lua API loses their functions (stats, spells, active effects, AI packages, crimes, skill progression, the animation controller).
