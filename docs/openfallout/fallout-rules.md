# Fallout rules: what the engine follows

This page lists the rules of the original games that OpenFallout follows for the player and for actors. Each rule says where it came from, what it is, and where today's engine differs. It also lists what was not found.

The rules were read from `FalloutNV.exe` and `Fallout3.exe` (the exes) and from the game data (`FalloutNV.esm`, `Fallout3.esm` and the add-on plugins). Both exes are 32-bit PE files with base 0x400000. The spec behind this page has seven parts, A to G.

## How to read this page

- **Games.** Each game follows its own exe's rules. TTW (Tale of Two Wastelands) runs on New Vegas, so it follows NV's rules and NV's addresses. Where TTW's data changes a value, the text says so.
- **Addresses.** An address is a virtual address in the exe. "NV 0x… / FO3 0x…" gives both games. NV and FO3 are different binaries, so the same function has two addresses. "Not found" means the spec did not find it.
- **Settings.** A setting is a game setting (GMST) with an `f`, `i`, `s` or `b` prefix. The value that runs is the value in the data if the data has one. If the data has none, the exe's own default runs. The tables give both: "ESM" is the data value, "exe" is the exe default. "absent" means no record in that file.
- **Confidence.** "exe-read" means read from the disassembly. "Data-read" means read from the game files. "Inferred" means reasoned from the code, not proved. "Online" means from a web page, and is never the only source for a number.
- **Today.** "Today" means the engine at `9c0cdf6895` (the head of PR #288, which master `2a23b2a5f1` contains), with file:line from the spec. These lines will move as step 4 changes the code.
- **Rule names.** A rule is named by its part letter and the spec's own name, for example `B.M1`.
- **Time and distance.** Seconds are real seconds unless the text says game time. 1 m is 69.99 game units (69.99125 in the engine's own constant).

| Part | Topic | Section |
|---|---|---|
| A | Actor family, process tiers, character controller, life state, knock state, ragdoll | [A](#a-actors-and-tiers) |
| B | Movement speeds | [B](#b-movement) |
| C | Jump, fall, water, breath | [C](#c-jump-fall-and-water) |
| D | Death, knock-down, limbs, regeneration, the Morrowind settings Fallout code still reads | [D](#d-death-condition-and-settings) |
| E | Derived stats and levelling | [E](#e-stats-and-levelling) |
| F | New Vegas Hardcore mode | [F](#f-hardcore-new-vegas-only) |
| G | Companions | [G](#g-companions) |

The last two sections list [what was not found in the exe](#not-found-in-the-exe), [what is unverified](#unverified) and [where the spec parts disagree](#where-the-spec-parts-disagree).

---

## A. Actors and tiers

### A.0 Summary

- **Class family.** Actor sits below Character and Creature. PlayerCharacter derives from Character. Characters and creatures differ by a few virtuals (`IsCharacter` vt+0x218, `IsCreature` vt+0x21c) and by the update. The actor vtable has about 330 slots. The player overrides only a handful.
- **Where an actor "lives".** AI, animation and physics hang off one pointer, `Actor.baseProcess` (NV +0x68, FO3 +0x60).
- **Four process tiers.** `HighProcess` ⊃ `MiddleHighProcess` ⊃ `MiddleLowProcess` ⊃ `LowProcess` ⊃ `BaseProcess`. The level number in the object is 0 High, 1 Middle High, 2 Middle Low, 3 Low. -1 means no process. The level is `process+0x28` in both games.
- **What picks a tier.** One function (NV 0x9334b0, FO3 0x766d80). It is about cells, not distance. No distance setting picks the tier.
- **What High actors get.** Only High and Middle High actors can have a character controller (`bhkCharacterController`) and a ragdoll controller (`bhkRagdollController`). The movement and animation of a High actor run every frame.
- **Controller shape.** The exe default capsule radius is 31 game units times the actor scale. The maximum slope is 47 degrees.
- **Today.** Fallout NPCs are not engine actors. They have no process object. The player is a Morrowind actor.

### A.1 The class family

| Class | NV vtable (main) | FO3 vtable (main) | Note |
|---|---|---|---|
| TESObjectREFR | 0x102f55c | – | |
| MobileObject | 0x108a49c | 0xe1799c | adds `baseProcess` |
| Actor | 0x1084254 (secondary MagicTarget vtable 0x10841f4) | 0xe12b9c | |
| Character | 0x1086a6c | 0xe1447c | NPCs |
| Creature | 0x10870ac | 0xe14a7c | |
| PlayerCharacter | 0x108aa3c | 0xe18114 | derives from Character; singleton pointer `[0x11dea3c]` in NV |
| BaseProcess / LowProcess / MiddleLowProcess / MiddleHighProcess / HighProcess | 0x1085e2c / 0x10886e4 / 0x1089bcc / 0x108904c / 0x1087864 | 0xe13a34 / 0xe15d9c / 0xe17124 / 0xe1685c / 0xe15224 | |
| bhkCharacterController (3 vtables: main, +0x3e0, +0x410) | 0x10c49c4 / 0x10c49b4 / 0x10c4998 | 0xe336f4 / 0xe336e0 / 0xe336c4 | |
| bhkCharacterProxy | 0x10c45cc | 0xe3321c | |
| bhkCharacterListenerArrow (a controller subclass for arrows) | 0x1086684 | – | |

Fields that matter. The field layouts come from the xNVSE headers (online), checked against the exe where the table says so.

| Field | NV | FO3 | Evidence |
|---|---|---|---|
| render state (3D holder) | REFR+0x64 | REFR+0x5c | exe: 0x43fcd0 reads `[this+0x64]+0x14`; FO3 0x766de7 reads `[esi+0x5c]+0x14` |
| parent cell | REFR+0x40 | REFR+0x3c | exe: 0x8d6f30 returns `[this+0x40]`; FO3 0x766dc3 |
| baseProcess | MobileObject+0x68 | +0x60 | exe, both |
| ragdoll controller | Actor+0xAC | not checked | exe: 0x87e130, 0x931394 test `[actor+0xac]` |
| life state (int) | Actor+0x108 (0x4f8960) | +0xFC (inferred from `cmp [ecx+0xfc],2` at 0x769301) | exe, NV |
| is swimming | Actor+0x14D | not checked | NVSE only |
| actor mover (player: movement flags) | Actor+0x190 | not checked | NVSE only. Player mover flags: bit 0 keep moving, 7 walk, 8 run, 9 sneak, 11 swim |
| process level | process+0x28 | process+0x28 | exe: 0x45cd60 returns `[ecx+0x28]`; FO3 0x766da8 |
| character controller | MiddleHigh+0x138 | not checked | exe: virtual 0xa3 (vt+0x28c) of MH/High is 0x8d8480; 0x9306d0 calls it |
| knock state (byte) | MiddleHigh+0x13c | not checked | exe: 0x8d8540 / 0x8d8560 read and write `[this+0x13c]` |
| bounding box (BSBound) | MiddleHigh+0x224 | not checked | NVSE only |
| process sizes | High 0x46c, MH 0x25c, ML 0xc8, Low 0xb4 | 0x45c, 0x254, 0xc8, 0xb4 | exe `new` sizes in 0x9320e0 and FO3 0x765650 |
| player: teammate list | PlayerCharacter+0x5FC | not checked | NVSE |
| player: disabled control flags | PlayerCharacter+0x680 | not checked | NVSE (bit 0 movement, 1 look, 2 Pip-Boy, 3 fight, 4 POV switch, 5 rollover, 6 sneak). Part F names the same bits by value: 1 movement, 2 looking, 4 Pip-Boy, 8 fighting |
| player size | 0xE50 | – | NVSE |

The virtuals that matter (NV offsets; FO3 slots above 0x210 sit 4 lower):

| vt offset (NV) | Meaning | Actor | Character | Creature | PlayerCharacter |
|---|---|---|---|---|---|
| 0x100 | IsActor | stub true (0x8d0360) | same | same | same |
| 0x1c4 | attach Havok: make the character controller and ragdoll after the 3D loads | 0x930c70 | same | same | same |
| 0x1d0 | get the actor's 3D root | 0x43fcd0 | same | same | 0x950b60 |
| 0x214 | read `[actor+0x1ac]` (meaning of +0x1ac not found) | 0x8d8800 | same | same | same |
| 0x218 | IsCharacter | false | true | false | true |
| 0x21c | IsCreature | false | false | true | false |
| 0x22c | IsDead(bool arg): life state 1 or 2 (arg true) or 1, 2, 6 (arg false) | 0x8844f0 | same | same | same |
| 0x230 | IsKnockedDown: `process.GetKnockedState() != 0` | 0x884560 | same | same | same |
| 0x240 / 0x244 / 0x248 / 0x24c | move to High / Low / Middle Low / Middle High | 0x881d30 / 0x882b90 / 0x883240 / 0x883800 | same | same | same |
| 0x25c | per-frame Process(dt) for High actors | 0x886360 | same | same | same |
| 0x268 | UpdateAnimation | 0x888070 | | | |
| 0x2f8 | Update | 0x888b50 | 0x8d3550 | 0x888b50 | 0x93e860 |
| 0x348 | UpdateMovement | 0x886cb0 | same | same | same |
| 0x410 | reaction to a hit or force (nine arguments, about twelve callers; sets the knock state through the process; role inferred) | 0x897de0 | same | same | same |
| 0x4c8, 0x4cc, 0x494 | called each frame by `UpdateAnimationMovement` (0x8885e0) | 0x8a3100, 0x8a3c50, 0x8b2c30 | same | same | …, …, 0x962de0 |

Process vtable (NV; slot numbers match the NVSE `BaseProcess` list):

| Slot (vt offset) | Meaning | Base/Low/ML | MH | High |
|---|---|---|---|---|
| 4 (0x10) | the tier update (profile sections "LowUpdate", "MiddleLowUpdate", "MiddleHighUpdate", "HighUpdate") | 0x90ad40 Low, 0x92d050 ML | 0x919f60 | 0x8eeec0 |
| 0xa3 (0x28c) | GetCharacterController | none | 0x8d8480 | same |
| 0x103 / 0x104 (0x40c / 0x410) | Get / Set KnockedState | stub | 0x8d8540 / 0x8d8560 | same |
| 0x106 (0x418) | PushActorAway | stub | 0x91fee0 | same |
| 0x115 / 0x116 | IsWeaponOut / SetWeaponOut | | 0x915d40 / 0x915d60 | same |

**Today.**
- Fallout NPCs and creatures are not engine actors. `ESM4Npc` and `ESM4Creature` are not `OFClass::Actor` (`esm4npc.hpp`). They have no process object. Their state is in `FalloutScripts` tables.
- The player is a Morrowind `LiveCellRef<ESM::NPC>`.
- No equal of `IsCharacter`, `IsCreature`, `IsDead` or `IsKnockedDown` exists as a class virtual. `FalloutScripts::isDead` and `FalloutActors::Mind::mThrown` / `mFell` stand in (`mwmechanics/falloutactors.cpp:265,2368-2401`).
- This does not match the exe model, by design.
- Confidence: exe-read for the vtables and the offsets marked exe. Online (NVSE headers) for the rest.

### A.T1 The tier levels and what each tier holds

- **Games.** Same in FO3 and NV. The names "High", "Middle High", "Middle Low", "Low" are in the exes' debug-print table (NV 0x11a363c, FO3 0xf6e52c).
- **Rule.** Four process classes sit in a chain. The level is stored in the process. -1 means none. The save game writes the level as one signed byte (NV 0x9320e0, FO3 0x765650: -1, 0, 1, 2, 3). Loading creates the process at that level and puts the actor in the level list.
- **What each tier holds** (NVSE headers, checked by size and by the creator code):
  - Low (0xb4): alert flag, idle form, temp actor-value damage list (`gameDayDied`, `playerDamageDealt`, damage modifiers). Enough for an actor that is not near.
  - Middle Low (0xc8): adds temp modifiers.
  - Middle High (0x25c): idle form, weapon and ammo info, `weaponInfo`, `charCtrl` (+0x138), `knockedState` (+0x13c), anim data (+0x1c0), `limbNodes[15]`, `boundingBox` (+0x224), last-hit data, face-gen animation data, interrupt package.
  - High (0x46c): adds the target and combat bookkeeping, the sit/sleep state, animation action, detection, the AI "queued" data.
- **Formula.** None.
- **Exe.** Process constructors: NV 0x8d7510 (High), 0x913fe0 (MH), 0x92c950 (ML), 0x906dc0 (Low), 0x8d0270 (Base). FO3 0x73a860, 0x75d070, 0x763770, 0x74e430.
- **Today.** No process objects. `FalloutActors::Mind` (`mwmechanics/falloutactors.cpp:240-300`) is a single record per actor. Does not match.
- **Round check.** None (structural).
- **Confidence.** Exe-read for sizes and level numbers. Online only for the field labels of the tiers.

### A.T2 Choosing the level

- **Games.** Same rule in FO3 and NV. FO3 reads the cell state with an explicit "5 or 6" at the controller creator.
- **In words.**
  1. While the player's post-load counter is above zero (NV `PlayerCharacter+0x654`, set after a load or a teleport and counted down by 0x94df80; this is the 0x94df60 test), the actor keeps its current level.
  2. If the global "bulk-load" flag is set, or the actor has a loaded 3D, or the actor is in the follower/teammate list, then:
     - parent cell state 5 or 6: High (0);
     - parent cell state 2, 3 or 4: Middle High (1);
     - otherwise fall through.
  3. If the parent cell is null, or has flag bit 9 (0x551620), or has state 2 to 6: Middle Low (2).
  4. Else Low (3).
- **Formula** (NV 0x9334b0, returns 0 to 3):
  ```
  if (keepLevel counter > 0) return currentLevel;
  cell = actor.parentCell;
  interesting = bulkLoadFlag || actor.has3D() || teammateList.contains(actor);
  if (interesting) { if (cellState(cell) in {5,6}) return 0; if (cellState(cell) in {2,3,4}) return 1; }
  if (cell != null && (cell.flag9 || cellState(cell) in {2..6})) return 2;
  return 3;
  ```
  A null cell gives Low (3). The exe tests `cell != null && cell.flag9` first, then `cellState(cell, 0)` (states 2 to 6). The null case failing both tests is inferred from the disassembly, not run. (Step 3 of "In words" says a null cell gives Middle Low. The formula note says Low. Part A contradicts itself here; see [the disagreements](#where-the-spec-parts-disagree).)
- **Settings.** None. The cell load distance is set by INI `uGridsToLoad` (exe default 5, NV 0xf3f196), `uInterior Cell Buffer` (3) and `uExterior Cell Buffer`.
- **Exe.** NV 0x9334b0. FO3 0x766d80 (the bit test is `[0x1079858]+0x244>>1 & 1`, the cell state test is 0x433170(cell,1) and 0x433170(cell,0), the flag test is 0x4d3b60). Cell state helper: NV 0x4511e0(cell, strict). States 5 and 6 are true for both values of `strict`. States 2, 3 and 4 are true only when `strict == 0`. NV 0x450ff0 is "state == 6". NV 0x4523c0 is "state == 5".
- **Today.** OpenFallout runs an actor if it is within 7168 units of the player across the ground (`mwmechanics/falloutactors.cpp:2360-2371`, `files/settings-default.cfg:281`). The exe has no radius.
- **Round check (live).** In a loaded exterior at the player's start, count the ESM4 actors that move against the actors in the 5×5 loaded cells. Expected: all High actors have 3D in cells of the attached grid, and none are outside. Tolerance: exact at cell borders.
- **Confidence.** Exe-read for the structure and constants. Inferred for the names (bulk-load flag, teammates).

### A.T3 Moving between tiers

- **Games.** Same. The virtuals are Actor vt+0x240 / 0x244 / 0x248 / 0x24c (FO3: same offsets at the call sites in 0x765650).
- **Rule.** `MobileObject::SetProcessLevel(level)` (NV 0x9319c0, FO3 0x765110) deletes the process if level is -1. It creates a Low process if none exists. Then it calls the right `MoveTo…`. Each `MoveTo…` replaces or upgrades the process object and moves the actor between the lists of `ProcessLists` (`AddActor` NV 0x96d450, `RemoveActor` NV 0x96d470; FO3 0x78bce0, 0x78bd00).
- **Re-judging.** `Actor::UpdateProcessLevel` (NV 0x8860d0, FO3 0x769a50) compares the current level with the desired level and applies it. It keeps the player's process, and a dead actor's process in some cases (NV 0x886152..0x8861b0: a level-0 actor whose process is type 6 is torn down and its reference is released; type 5 calls 0x574400). The check is queued per actor: NV 0x96e870 puts an actor on a pending list (`ProcessLists+0x78`) and sets `[actor+0x82] = 1`. When `desired != current`, the actor is also kept if it is dead, in a conversation, or the player flag at 0x94df60 is up.
- **What changes.** The actor object, position, inventory, packages and actor-value damage are not touched. Only the process object is replaced. Data that lives only in a higher tier is dropped when going down: the 3D link, the controller, the ragdoll, `limbNodes` and animation data. It is rebuilt when the actor comes back. The ML and Low data (`damageModifiers`, `gameDayDied`, `playerDamageDealt`, `byte30` "alerted") is kept. (This list is inferred; the `MoveTo…` code paths were not read in full.)
- **Formula.** None.
- **Today.** Not present. Fallout NPCs far from the player are not processed. They are not saved either, except through `FOSV` tables.
- **Round check (for 4b).** Move the player across a cell border. Actors in the old cell stop being updated and actors in the new cell start. Positions of unprocessed actors stay unchanged (tolerance 0.1 unit).
- **Confidence.** Exe-read for the call structure. Inferred for what is dropped.

### A.T4 When each tier is updated

- **Games.** Same structure. FO3's `ProcessLists::Update` was not found, so the frame-skip numbers are NV only.
- **Per frame (NV).** The main loop part 0x8c7bd0 runs `ProcessLists::0x96cca0`, 0x96cda0 (prepare), the TES update, 0x9784c0, then `ProcessLists::HighUpdate` (0x96bcd0; dt = 0 means "use the frame timer"), then 0x96d520 (player-centred work), 0x96c330, 0x96db30, 0x8d0600, 0x96c7c0, 0x96cb50. High actors update every frame.
- **The other tiers.** `ProcessLists::Update` (NV 0x96d810) runs Low (0x96b050) if `[0x103a9]` is set, Middle Low (0x96b470) if `[0x103ab]` is set, and Middle High (0x96b810) if `[0x103aa]` is set. These bytes are the console toggles `ToggleLowProcess` and so on. `bRunMiddleLowLevelProcess` = 1 is the INI default for ML.
- **Frame-rate gate** (NV 0x96d996..0x96da06): `fps = (int)(1 / frameTime)`.
  - `fps >= 50`: the three tiers run every frame.
  - `10 <= fps < 50`: an accumulator `a` (`ProcessLists+0`) takes `a += dt`. The tiers run when `a >= 1.0` (then `a = 0`). Otherwise `a += (1 - dt) / 2` and they are skipped. At 30 fps this is one frame in three.
  - `fps < 10`: they run when `a >= 1.0`, so about once a second.
  - The three-frame reading is the spec's reading of the code. It was not measured.
- **Low tier budget.** `iLowProcessingMilliseconds` = 2 (NV and FO3 exe default; NV 0x96b0d1). The loop over Low actors stops when the budget is used and resumes from `ProcessLists[0x11e0e30]` (a cursor) next time.
- **Bookkeeping.** NV `9723f0` (written without 0x in the spec; every time the tiers run) clears per-level lists and writes `ProcessLists+0x103b4 = count(High) + 10.0`.
- **Per-tier work.** Each tier's vt slot 4 is its AI and package update. `HighUpdate` (0x8eeec0) needs an actor that is not disabled or dead (0x574900, 0x440d80, 0x440da0) and a parent cell in state 6 (0x450ff0). Otherwise it returns without work. The rest of that function (package evaluation, detection, animation request, `fShaderShadowUpdateDistance` shadows) was not decoded. Task data classes exist for threaded work: `ActorUpdateTaskData`, `AnimationTaskData`, `DetectionTaskData`, `MovementTaskData`, `PackageUpdateTaskData`, `ActorsScriptTaskData`. INI: `bTaskletActorHavokSync` = 1, `bTaskletActorSceneGraphUpdates`, `bTaskletActorAnimMovementUpdates`, `bRunActorMovementDuringRendering`.
- **Per frame, per High actor** (`Actor::Process`, vt+0x25c, NV 0x886360): if `[this+0xbc]` and 0x440da0 allow, it sets `[this+0x15d] = 0x8a40e0(0)`. It decrements two timers at +0x150 and +0x154 by the frame time. It runs 0x8b3230(dt) and 0x8c3c40. If the actor is not dead (`IsDead(false)`), the global `[0x11f1220]` (the `TAI` toggle: AI enabled) is set, and the actor is not in process type 6, it runs the package and animation update. `Actor::Update` (vt+0x2f8, NV 0x888b50) syncs the character controller position (see A.C3).
- **Counts and caps** (INI or setting; the exe default):

  | Name | NV | FO3 |
  |---|---|---|
  | iNumberActorsInCombatPlayer | 20 (exe) | 20 |
  | iNumberActorsAllowedToFollowPlayer | 6 | 6 |
  | iNumberActorsGoThroughLoadDoorInCombat | 2 | 2 |
  | iAINumberActorsComplexScene | 20 | not found |
  | iMaxHiPerfCombatCount | 4 | 4 |
  | iMaxHiPerfPCTargetCount, iMaxHiPerfNPCTargetCount | 4, 4 | not found |
  | fHiPerfNPCTargetLOSTimer, fLowPerfNPCTargetLOSTimer | 0.5, 1.0 | not found |
  | iLowProcessingMilliseconds | 2 | 2 |
  | bRunMiddleLowLevelProcess, bPreCullActors | 1, 1 | 1, 1 |

  These caps are about combat, not about the tiers. Nothing in the exe caps the number of High actors by count.
- **Today.** `FalloutActors::update(duration)` (`mwmechanics/falloutactors.cpp:2345`) runs every frame for all actors within the range. `duration` is clipped to 0.25 s (`:60`, `:2359`). It is per frame like the exe, but the cell rule differs.
- **Round check.** At fps below 50 the non-High tiers run about once every 3 frames. This is a performance throttle. Do not copy it, and do not expect per-frame updates of the non-High tiers.
- **Confidence.** Exe-read for the order and the numbers. Inferred for the 3-frame reading.

### A.C1 Which actors have a character controller

- **Games.** Same in FO3 and NV. FO3 creator 0x769230, NV 0x930c70. Both are Actor vt+0x1c4.
- **Rule.** When an actor's 3D is attached, vt+0x1c4 runs. It makes a controller only if all of these hold:
  1. the actor has a process and its level is 0 (High) or 1 (Middle High);
  2. the actor's parent cell is in state 5 or 6 (NV 0x450ff0 or 0x4523c0; FO3 `cmp al,6` and `cmp al,5` at 0x76929c);
  3. the actor is not in a "sit/ride" type state. NV 0x930d8b..0x930d9c: 0x5a29b0 of a Character returning 2 or 4 means no controller. FO3 0x7692ef..0x7692fb: `[ecx+0x100] - 2`, `-2` again, jumps out.
- **What it builds.** A `bhkCharacterControllerCinfo` (NV 0xc6da50) with:
  - the 3D root (`cinfo+0x80`) and the position;
  - scale × race height (`+0xac`);
  - flags from the race record (`+0x97` race flag 0x1000000, `+0x98` race flag 0x2000000);
  - `+0x96` = (life state != 2 and the actor is "player-ish" (0x4f8960 != 2 and 0x8aad40)). The meaning of `+0x96` is inferred;
  - `+0x8c` = `vt+0x2e4` (1.0).
  The controller is allocated 0x650 bytes (NV 0x931047) and constructed by NV 0xc741e0.
- **The ragdoll.** The same routine makes a ragdoll controller when the actor is not dead (`IsDead(false)` false): 0x87e130(root, 1, 1, 0) (NV 0x931384). For the player the ragdoll is then switched off (0x87ea60(0), 0x9315a0(1)).
- **Teardown.** The controller is removed when the process goes below Middle High, the 3D is unloaded, or the actor dies into a state that tears it down (destructor 0xc70c60). Each teardown call was not traced.
- **Today.** Fallout NPCs have a kinematic body: `PhysicsSystem::addActorBody` (`mwphysics/physicssystem.cpp:449`) from `ESM4Npc::insertObjectPhysics` (`mwclass/esm4npc.cpp:383-387`). There is no controller and no solver. The player has the Morrowind `mwphysics::Actor` with the solver.
- **Round check.** After a cell loads, every ESM4 actor with a 3D in an attached cell has a body (count equals). Every actor beyond has none. Tolerance 0.
- **Confidence.** Exe-read for the conditions and sizes. Inferred for the meaning of cinfo +0x96.

### A.C2 The shape and the parameters

- **Games.** Same in FO3 and NV. The constants were checked in NV. FO3 has the float 47.0 at 0xe3563c.
- **Rule.** A capsule-like shape (`hkpCharacterProxy` with a shape phantom: NV 0xc6c7a0) around the actor, sized by the actor scale. A proxy with a maximum slope of 47 degrees. The proxy and the Havok world use the Havok scale: 1 Havok unit = 6.9991255 game units (NV `[0x11b0194]`; `[0x1267bcc]` = 1/6.9991255).

| Number | Value | Where |
|---|---|---|
| Capsule radius (cinfo `+0xa8`) | 31.0 / 6.9991255 Havok units = **31.0 game units**, multiplied by `cinfo+0xac` (actor scale × race height) in the controller constructor | NV 0xc7423f..0xc742a3; `controller+0x470 = radius × scale` at 0xc73f2b..0xc73f4b. The static data holding 31.0 is 0x1035828 |
| Maximum slope | **47.0 degrees** (NV `[0x11b0148]`), converted with 0.01745329… (0x1023128) and passed to the proxy, which keeps the cosine | 0xc6c7a0 stores the cosine at `+0x20` and the radius at `+0x60` |
| Warp distance squared | INI `fCharControllerWarpDistSqr:HAVOK` = **6 000 000** (so 2449.5 units) | NV 0xfbcd1b; FO3 INI present at 0xe33820 |
| Havok world step | `fTimePerSubStep:HAVOK` = 0.008, `iMinNumSubSteps:HAVOK` = 8, `fMaxTime:HAVOK` = 0.016667; `iHavokSkipFrameCountTEST` = 0 | |
| Push mass limit | `fMoveLimitMass:HAVOK` = 95 | |
| Turning | `fCharacterDefaultTurningSpeed` = 90, `fCreatureDefaultTurningSpeed` = 45, `fCharacterControllerMultipleStepSpeed` = 25 | exe GMST defaults in NV and FO3 |
| Fall check | `fCheckPositionFallDistance` = 72 | exe default, NV and FO3 |
| Jump animation delay | `fJumpAnimDelay:HAVOK` = 0.75 s | |
| Unknown constants | the constructor sets `+0x5f0` = 2.0, `+0x5f4` = 10.0, `+0x5f8` = 10.0, `+0x5fc` = -1.0 | meanings not known |

- **Collision size of a character.** The human skeleton's `BSBound` ("BBX") block gives centre (0, 0, 66.22) and half extents (23.1, 17.64, 66.22) for `meshes\characters\_male\skeleton.nif`. That is 46.2 wide, 35.3 deep and 132.4 tall. Other skeletons (half extents):

  | Skeleton | Half extents |
  |---|---|
  | dog | 29.56, 62.75, 35.51 |
  | radroach | 16.17, 37.83, 13.91 |
  | deathclaw | 50, 95.14, 76.18 |
  | molerat | 24, 70, 32 |
  | giant rat | 15, 41, 20 |
  | Mr House | same as human |
  | Securitron | 67.45, 47.34, 90.73 |
  | giant ant | 205, 269, 165 |

  The controller constructor reads the 3D root (`cinfo+0x80`). `Actor::Update` passes the 3D to the controller (0xc6d6b0). It was not proved that the capsule height comes from `BSBound`.
- **Race height.** The exe does not read the race record's `male height` and `female height` into a shape. It multiplies the scale by the race height (0x567400 reads `REFR+0x3c` scale and, for NPC and creature bases, `race+0x13c`).
- **Step height.** Not found in the exe as a setting.
- **Gravity.** From the Havok world. The INI list scanned has no gravity setting.
- **Today.**
  - NPC: kinematic `btCylinderShapeZ`, half extents (20, 20, 64) × race height, clamped 0.25 to 4 (`mwclass/esm4npc.cpp:35-41`, `physicssystem.cpp:449-466`). That is a cylinder with radius 20 and height 128.
  - Creature: from the record's object bounds, radius = min(width)/2 clamped 8 to 160, half height clamped 8 to 250 (`esm4creature.cpp:20-38`).
  - Player: Morrowind actor box from the mesh. Step up 34, step down 62, maximum slope 46 degrees (`components/misc/constants.hpp:43-44`, `mwphysics/constants.hpp:6`). Gravity 8.96 m/s² × 69.99 (`components/misc/constants.hpp:10,22`; used in `mwphysics/movementsolver.cpp:432`, and for NPC falls at `mwmechanics/falloutactors.cpp:680`).
  - Match: the slope is 46 against the exe's 47. The NPC cylinder (radius 20) differs from the exe radius 31 × scale.
- **Round check.**
  - Body radius of a male human at race height 1.0 and scale 1.0. Expected 31 ±1 if the exe is followed, 20 if today's value is kept. The spec decision should name which.
  - Maximum climbable slope: walk up ramps of 46 and 48 degrees. Expected: 46 climbable, 48 not (exe 47; ±1 degree tolerance because the proxy uses a cosine test with margins).
- **Confidence.** Exe-read for 31, 47, 6 000 000, the Havok scale and the INI numbers. Inferred for "31 is the capsule radius": it is the cinfo default and is multiplied into the controller radius, but the shape height and offsets were not decoded.

### A.C3 How the controller is driven each frame

- **Games.** NV confirmed. FO3 not read.
- **Rule.**
  - A controller exists per actor. The process points to it (MH+0x138). `Actor::GetCharController` (NV 0x9306d0) reaches it through process virtual 0xa3.
  - `Actor::Update` (vt+0x2f8) for a non-player actor with a controller calls 0xc6d6b0(controller, position, actor 3D). The position is the actor's own. For the player's followers it is a position from the player's data (`[0x11e07d4]`, `[0x11e07d0]`) or 0x8be940. This syncs the body to the actor. The velocity was not decoded.
  - `UpdateAnimationMovement` (0x8885e0, per frame) calls `vt+0x4c8(dt)`, `vt+0x4cc(dt)` and `vt+0x494()` (0x8a3100, 0x8a3c50, 0x8b2c30 for non-players; 0x962de0 for the player's third one). Then 0x888960 and 0x8bcdf0, which are the movement from the animation. Then a six-way state switch on `vt+0x214()` (the value at actor+0x1ac). The character body is moved by Havok toward the velocity the animation moved the 3D by. This is inferred from the class names `bhkCharacterStateOnGround`, `InAir`, `Jumping`, `Swimming`, `Climbing`, `Flying`, `Projectile` and from `fCharControllerWarpDistSqr`.
  - If the controller's position and the actor's differ by more than `fCharControllerWarpDistSqr` (squared), the controller is warped to the actor.
  - The player's jump uses `fJumpAnimDelay` 0.75 and the jump settings of part C: `fJumpHeightMin` 64, `fJumpMoveBase` 0, `fJumpMoveMult` 0.3, `fJumpSwimmingMult` 2.0. (Part A says "in both games" for the move settings; part C says NV does not read them. See [the disagreements](#where-the-spec-parts-disagree).)
  - Push: `fMoveLimitMass` 95 (a body of more mass than 95 is not pushed by the character). `fPerkHeavyArmorSinkGravityMult` 15 (exe default) is the sink rate of a heavily armoured character in water.
- **Formula.** None known.
- **Today.** NPCs: `FalloutActors` moves the kinematic body with `moveObject` (`mwmechanics/falloutactors.cpp:727,1476,1794`) at the walk or run speed of the animation (`:496-505`, `:1447`), with ground probes (`groundProbeUp` 48, `groundProbeDown` 160, `:74-75`). Player: Morrowind solver.
- **Round check.** On a level walk, the actor's speed equals the animation's `walkVelocity` (the NPC part exists today). For the player, use the movement rules (part B).
- **Confidence.** Exe-read for the call structure. Inferred for the semantics.

### A.K1 Life state

- **Games.** Same. Names read from the exes (NV table 0x118c690; FO3 0xf50b94).
- **Rule.** `Actor+0x108` (NV; FO3 `+0xfc`): 0 Alive, 1 Dying, 2 Dead, 3 Unconscious, 4 Reanimate, 5 Restrained. The exe also treats 6 as dead-like in `IsDead(false)`. Part A did not find a name for 6. Parts D and G name it "essential down" (see D.D4).
- **`IsDead(arg)`** (NV 0x8844f0): if `arg` is true, life state is in {1, 2}. If false, life state is in {1, 2, 6}.
- **Today.** `FalloutScripts::isDead` from `ActorState` (`mwmechanics/falloutactors.cpp:2368`). Equivalent in effect. No states 3 to 6.
- **Confidence.** Exe-read for the table and `IsDead`.

### A.K2 Knock state

- **Games.** Same enum in both exes (NV 0x118c6d4, FO3 0xf50bd8).
- **Rule.** A byte at `MiddleHighProcess+0x13c`: 0 Normal, 1 Explode, 2 Explode Lead In, 3 Knocked Out, 4 Knock Out Lead In, 5 Queued, 6 Getting Up. The script command `GetKnockedState` maps to 0 normal, 1 knocked out, 2 knocked down (GECK wiki). The exe's seven states are a finer version of that. `IsKnockedDown` (Actor vt+0x230) is "state != 0". Only actors with a Middle High or High process can be knocked down.
- **Writers.** The hit entry (Actor vt+0x410, 0x897de0) is called with nine arguments from about twelve places (NV 0x5d0b64 script command, 0x80cb46, 0x8bf18c, 0x8db928, 0x8ddcaf, 0x9034ee, 0x916a41). The process's `SetKnockedState` (0x8d8560) writes the byte. Transition times and the getting-up sequence were not decoded.
- **Settings that trigger it** (values from the A spec's table, `tmp-actors/gm_all.txt`):

  | Setting | Value (NV and FO3) |
  |---|---|
  | fKnockdownChance | 0.25 |
  | fKnockdownCurrentHealthThreshold | 50 (both ESMs; D.D5 gives 25, see [disagreements](#where-the-spec-parts-disagree)) |
  | fKnockdownBaseHealthThreshold | 75 |
  | fKnockdownDamageMult | 0.3 |
  | fKnockdownDamageBase | 0 |
  | fKnockdownAgilMult | 1.0 |
  | fKnockdownAgilBase | 0 |
  | fKnockbackForceMax | 512 |
  | fKnockbackTime | 1.0 |
  | fKnockbackDamageBase | 50 |
  | fKnockbackDamageMult | 10 |
  | fKnockbackAgilBase | 1.0 |
  | fKnockbackAgilMult | -0.008 |
  | fAIPowerAttackKnockdownBonus | 15 |
  | iPerkMarksmanKnockdownChance | 5 |
  | fExplosionForceKnockdownMinimum | 0.15 |
  | fProjectileKnockMultBiped | 0.25 |
  | fWeaponClutterKnockBipedScale | 5 |

  The formulas are in D.D5 and D.D6.
- **Today.** No knock states for NPCs. A blow "throws it back" with `mKnock` (units/s, falling to zero over `mKnockTotal`, `mwmechanics/falloutactors.cpp:263-268,653-671`), and the actor "falls" with gravity (`:680`). The player has a Morrowind `Flag_KnockedDown` state that Fallout does not set.
- **Round check (needs the combat rules).** Shoot a standing NPC with enough damage. The knock state leaves 0 within one frame of the hit. After `fKnockbackTime` 1.0 s ±0.2 it goes through Getting Up (6) to Normal (0).
- **Confidence.** Exe-read for the enum, accessors and call sites. Inferred for the transitions.

### A.K3 The ragdoll controller

- **Games.** Both. FO3 has the string "Could not initialize Ragdoll Controller" at 0xe131d0, but its creator address was not found. NV creator `Actor::InitRagdollController` 0x87e130 (called at 0x931384).
- **Rule.** A `bhkRagdollController` (`Actor+0xAC`) is created, together with the character controller, for every living actor with a 3D and a High or Middle High process. If it cannot be made, the engine logs "AI: Could not initialize Ragdoll Controller for %s".
  - The race's `BGSRagdoll` form supplies the constraint data. `RagdollConstraint\Default.rdt` is the default (`sRagdollDataDefault`).
  - The male skeleton has 18 rigid bodies, 18 capsule shapes, 14 malleable constraints and 3 ragdoll constraints.
  - A live actor with ragdoll data in the save logs "MASTERFILE: Ragdoll data found on alive actor %08X. Ragdoll data will be removed" (NV 0x4174b8).
  - INI for the ragdoll solver (`RagdollAnim` section, exe strings only): `bRagdollAnim`, `bFootIK`, `bGrabIK`, `bLookIK`, `bRagdollFeedback`, `bPoseMatching`, `fHierarchyGain`, `fVelocityDamping`, `fAccelerationGain`, `fVelocityGain`, `fPositionGain`, `fPositionMaxLinearVelocity`, `fPositionMaxAngularVelocity`, `fSnapGain`. Their default values were not read.
  - The console prints "Has RagDoll: Yes/No" from `[actor+0xAC] != null` (NV 0x5c3d3c, FO3 0x533540).
- **Death.** Dead bodies are driven by the ragdoll. A dead actor (life state 2) gets a ragdoll at death if none exists (0x87e130 is also called by the hit entry). The exe has `ToggleRagdollAnim` to switch the system off.
- **Today.** `FalloutActorAnimation::die(bool fallen)` plays a pose (`mwrender/falloutactoranimation.cpp:473`). There is no ragdoll.
- **Round check.** Kill an NPC. The body comes to rest within 5 s and moves no more than 2 units after that. The tolerance is loose, because a ragdoll is not deterministic.
- **Confidence.** Exe-read for creation and the debug strings. Inferred for the death use.

### A.4b What 4b needs from these rules

1. **A tier model, or a stand-in.** The minimum that gives the same results: an actor is "High" (animated, AI-updated, has a body) exactly when its 3D is loaded and its cell is attached around the player. OpenFallout can keep one "active set" of ESM4 actors per loaded cell. It can drop the 7168 range test as the rule, or keep it as an upper bound.
2. **Create the body when the 3D attaches, not when the AI starts.** Drop it when the actor leaves the set. Today the body is added in `insertObjectPhysics`, at placement time for all actors (`esm4npc.cpp:383`).
3. **Body size from the 3D and the scale, not a fixed cylinder.** Decide: keep 20 × 20 × 64 (a decision of the project), or use the exe's 31 × scale radius and a height from the 3D bound (see [not found](#not-found-in-the-exe)). A round should check the choice.
4. **A body that follows the animation's movement,** with the same ground rules as the player (same step, slope and gravity). The exe runs one body per actor under Havok with the same solver for NPCs and the player. So the player and NPC move rules can share a body class.
5. **Knock states and ragdoll.** Add the life state and knock state as fields of the NPC record (7 knock states, 6 life states). Add a ragdoll for dead and knocked-out actors (from the race's `BGSRagdoll`, the skeleton's `bhkRigidBody` set). This replaces `mThrown` and `mKnock` (`falloutactors.cpp:263-268`) and `FalloutActorAnimation::die`.
6. **What to keep when an actor drops to Low.** Position, inventory, packages and damage live in the actor, not the process (A.T3). Only 3D, body and ragdoll go.
7. **Do not copy** the frame-rate slicing of the lower tiers (A.T4), or the console toggles.

### A.rounds Rounds the A spec suggests

| Round | Measures | Expected | Tolerance | Rule |
|---|---|---|---|---|
| tier-cells | count of actors with a body and 3D against actors in the 5×5 loaded grid | equal | 0 | A.T2, A.C1 |
| tier-crossing | actors after a cell border crossing | old cell actors lose the body, new cell actors gain one | 0 | A.T3 |
| body-radius | radius of a male human's body at scale 1 | decided in the spec: 31 or 20 | ±1 | A.C2 |
| slope | climb 46° and 48° ramps | 46 yes, 48 no | ±1° | A.C2 (exe 47°) |
| knock | state after a heavy hit | 0 → 3 within one frame of the hit; → 6 → 0 after about 1 s | ±0.2 s | A.K2, `fKnockbackTime` |
| death | bodies at rest | no motion after 5 s | 2 units | A.K3 |
| warp | teleport the NPC more than 2449.5 units by script | the body follows within one frame | 1 unit | A.C2 |

### A.unclear Open items in part A

1. What bit 1 of the global flag at `[0x11ddf38]+0x244` (NV; FO3 `[0x1079858]+0x244`) means. It is set only while one bulk routine runs (NV 0x84a840 brackets it with 0x848ca0(1) and 0x848ca0(0)). When set, every actor is judged "interesting". The spec reads it as a "cells are being (re)attached" phase. Not proved.
2. What the third "interesting" test is (NV `[0x11c3b3c].0x445750(actor)`, FO3 `[0x106ca70].0x4298f0(actor)`). It looks a container up by the actor reference. The spec reads it as "is a teammate or follower". Not proved. It does not matter for ordinary NPCs, because they get a 3D when their cell loads.
3. Cell state values. The byte at cell+0x26 is read as 2 to 6. The writers of each value were not traced. The names "attached" and "loading" are the spec's reading.
4. How the capsule is built. The numbers found are the cinfo defaults (radius 31, slope 47). The capsule height was not found (0xc72410 reads the 3D root bounds at `cinfo+0x80`; its arithmetic was not decoded).
5. Step height and gravity of the character body (see [not found](#not-found-in-the-exe)).
6. The per-frame movement path of the controller: how the animation's root motion and the input become the body's velocity. The creator, the position sync and the warp threshold were located. The velocity code (`bhkCharacterState*` classes) was not.
7. How the knock state and the ragdoll get switched. The state enum, the accessors and the places that call the hit entry (vt+0x410) were found. The transitions were not.
8. FO3 addresses. FO3's virtual table offsets differ from NV above vt+0x210 by 4 (IsDead is vt+0x228 in FO3, +0x22c in NV). FO3 addresses are given where the same structure was confirmed. FO3's `ProcessLists::Update` was not found.
9. The 3-frame slicing rule (see A.T4). It was not measured.
10. TTW: the NV rules apply by definition. The TTW plugins were not read for part A.

---

## B. Movement

### B.0 Summary

- **A humanoid's speed is a formula, not an animation lookup.** This holds for the player and for ordinary NPCs. Walk speed in game units per second is `fMoveBaseSpeed × SpeedMult/100 × (other factors)`. Run speed is the same number times `fMoveRunMult`.
- **Both games ship `fMoveBaseSpeed` = 77** in the ESM (the exe default is 85, so the ESM wins) and `fMoveRunMult` = 4.
- **A fresh level-1 character** (SpeedMult 100, no armour penalty, no weapon penalty) walks at 84.7 units/s with the weapon holstered and 77.0 with it drawn. It runs at 338.8 or 308.0. The 1.1 holstered bonus is `fMoveNoWeaponMult` (exe default, in neither ESM). Sneaking multiplies by `fMoveSneakMult` 0.57 (both ESMs).
- **The two games differ on weight.**
  - FO3 slows you from the first pound: speed × (1 − k × min(carried weight, 150) / 150), with k = 0.3 holstered and 0.4 weapon drawn. These come from the exe: `fMoveEncumEffect` and `fMoveEncumEffectNoWeapon` (neither is in `Fallout3.esm`).
  - NV removed those settings and that term. It has armour-class and two-handed-weapon penalties instead (`fMoveHeavyArmorPenalty`, `fMoveMediumArmorPenalty`, `fMove2HBigPenalty`, `fMove2HRPenalty`).
- **Over the carry weight, in both games,** the player can never run (the run flag is never set), gets the message "You are overencumbered and cannot run!", and cannot fast travel. Only NV's Long Haul perk lifts the fast-travel block. Over-encumbrance itself adds no extra slowdown.
- **Crippled legs.** One crippled leg × 0.85, two × 0.75. A leg counts as crippled when its limb condition is 0 or less. The `IgnoreCrippledLimbs` actor value (AV 72) cancels the penalty, but not when the player is over-encumbered.
- **Dead settings.** Nothing reads `fMoveSwimWalkBase`, `fMoveSwimRunBase`, `fMoveSwimWalkAthleticsMult`, `fMoveSwimRunAthleticsMult` or `fMoveLegConditionMult` in either exe. There is no Athletics skill. In both exes swimming uses the same walk and run formulas with the sneak factor forced off.
- **Backward and strafe.** The input code builds all four directions from one scalar, so no setting scales a direction. Whether a diagonal is normalised is not confirmed.
- **First or third person.** No camera input is in the speed chain.
- **Creatures** (not humanoids) take their speed from the walk or run animation's own movement speed times SpeedMult/100.
- **Today.** The player is driven from Morrowind settings and a placeholder Speed attribute (150 walk, 270 run, computed). It matches the exe only in the crippled-leg factors and the forced-sneak factor. NPCs move at animation speed instead of 84.7 and 338.8.

### B.defs Common definitions

- Units: game units per second (about 1.4 cm each).
- `SpeedMult` is actor value 21 (0x15). The player's base is 100: the player's `NPC_` record (form id 0x7) has ACBS speed 100 in both ESMs. Most `NPC_` records have 100. The exceptions are scripted or special (130 on 129 NV NPCs, creatures up to 250).
- Movement flag word (player input, NV 0x93e840; FO3 0x788350 area): 0x1 forward, 0x2 backward, 0x4 left, 0x8 right, 0x10 turn left, 0x20 turn right, 0x100 walking, 0x200 running, 0x400 sneak, 0x800 swim. The names come from how the exe uses them. 0x4997b0 is "0x400 and not 0x800".
- Over-encumbered = `InventoryWeight` (AV 46) is strictly greater than the actor's carry weight (AV 13 with the carry-weight perk entry applied).

### B.M1 Base speed of a humanoid (walk)

- **Games.** FO3 and NV. The formulas differ in the weight and armour terms (B.M5, B.M6). TTW uses NV.
- **Rule.** A humanoid's walk speed is the base speed scaled by its SpeedMult and by a handful of conditions. The game computes it for the player and for ordinary NPCs alike (class `Character`). The result is cached on the actor and recomputed when something marks it dirty (equipping, drawing, sneaking, changing SpeedMult).
- **Formula (NV, per call):**
  ```
  legMult   = 1 / fMoveOneCrippledLegSpeedMult (0.85) / fMoveTwoCrippledLegsSpeedMult (0.75)   (B.M7)
  if (!overEncumbered && AV72 IgnoreCrippledLimbs != 0) legMult = 1
  penalty   = armourClass(heavy: fMoveHeavyArmorPenalty, medium: fMoveMediumArmorPenalty, light/none: 0)
            + (weaponDrawn ? weaponPenalty : 0)                                                (B.M6)
  speed     = fMoveBaseSpeed × (SpeedMult × 0.01) × legMult
  if (holstered && isHumanoid) speed ×= fMoveNoWeaponMult                                      (B.M3)
  speed    ×= (1 − penalty)
  if (sneaking) speed ×= fMoveSneakMult                                                         (B.M4)
  speed     = max(speed, 0)
  ```
- **Formula (FO3):**
  ```
  w     = min(carriedWeight, fMoveWeightMax)
  enc   = (w + fMoveWeightMin) / (fMoveWeightMax − fMoveWeightMin)
  enc  ×= (holstered && isHumanoid) ? fMoveEncumEffectNoWeapon : fMoveEncumEffect
  speed = fMoveBaseSpeed × (SpeedMult × 0.01) × legMult
  if (holstered && isHumanoid) speed ×= fMoveNoWeaponMult
  speed ×= (1 − enc)
  if (sneaking) speed ×= fMoveSneakMult
  speed = max(speed, 0)
  ```
  No armour or weapon penalty exists in FO3.
- **Settings** (the ESM value wins over the exe default in every row where both exist):

  | Setting | NV ESM | FO3 ESM | Exe default |
  |---|---|---|---|
  | fMoveBaseSpeed | 77.0 | 77.0 | 85.0 |
  | fMoveNoWeaponMult | not in ESM | not in ESM | 1.1 (both exes) |
  | fMoveSneakMult | 0.57 | 0.57 | 0.6 (both exes) |
  | fMoveOneCrippledLegSpeedMult | 0.85 | 0.85 | 0.85 (FO3: 0.85) |
  | fMoveTwoCrippledLegsSpeedMult | 0.75 | 0.75 | 0.75 |
  | fMoveHeavyArmorPenalty | 0.15 | n/a (FO3 has no such term) | NV 0.20 |
  | fMoveMediumArmorPenalty | 0.075 | n/a | NV 0.10 |
  | fMove2HBigPenalty | 0.10 | n/a | NV 0.15 |
  | fMove2HRPenalty | not in ESM | n/a | NV 0.10 |
  | fMoveEncumEffect (FO3 only) | n/a | not in ESM | 0.4 |
  | fMoveEncumEffectNoWeapon (FO3 only; the name is cut at "…NoWea" in the exe table) | n/a | not in ESM | 0.3 |
  | fMoveWeightMin (FO3 only) | n/a | not in ESM | 0.0 |
  | fMoveWeightMax (FO3 only) | n/a | not in ESM | 150.0 |

- **Exe, NV.** Core function 0x647D10 (cdecl, 7 stack args: actor value owner, weapon, armour, sneak, holstered, isHumanoid, overEncumbered).
  - Armour penalties: 0x647D36 heavy, 0x647D53 medium.
  - Weapon penalties: 0x647D9C big, 0x647DBD 2HR.
  - Leg count: 0x647DCE-0x647DFC. One-leg multiplier 0x647E14, two-leg multiplier 0x647E36. AV 72 check 0x647E4C-0x647E5D.
  - SpeedMult read (`push 0x15`, vtable +0xC): 0x647E64-0x647E73.
  - `fMoveBaseSpeed` use 0x647E7D. `fMoveNoWeaponMult` 0x647EA8. `fMoveSneakMult` 0x647ECF. Clamp at 0: 0x647EE0.
- **Exe, FO3.** Core function 0x5845B0.
  - Weight clamp 0x5845B7-0x5845C8 (against `fMoveWeightMax` at 0xF610B0).
  - Leg count 0x5845D5-0x5845F8 via helper 0x582B30. Leg multipliers 0x58460A-0x584634 (0xF62298, 0xF622A4).
  - AV 72: 0x584638-0x58464E.
  - SpeedMult read 0x584659. Encumbrance fraction 0x58466E-0x584684.
  - `fMoveBaseSpeed` 0x58468A. Holstered branch 0x58469B-0x5846BE. Non-holstered 0x5846C0. Sneak 0x5846E5.
- **Callers.**
  - NV `Character::GetWalkSpeed` 0x885A50 (vtable 0x1086A6C +0x458, also PlayerCharacter 0x108AA3C). It calls 0x647D10 at 0x885B88. For a non-player it then multiplies by the +0xD8 field of the object returned by vtable +0x428 (see B.unclear 6).
  - FO3 `Character::GetWalkSpeed` 0x6FB200, calling 0x5845B0 at 0x6FB2A4. The weight argument comes from actor vtable +0x310 (cached total weight, recomputed when 0).
- **Today.** `Npc::getWalkSpeed` (`apps/openfallout/mwclass/npc.cpp:1347-1363`): `fMinWalkSpeed + 0.01 × Speed attribute × (fMaxWalkSpeed − fMinWalkSpeed)` with Morrowind defaults 100 and 200 and the placeholder Speed 50. That is 150 u/s. It is multiplied by `1 − fEncumberedMoveEffect × normalizedEncumbrance` (always 0 for a Fallout player). Sneak × `fSneakSpeedMultiplier` (0.75). Then `moveSpeedMultiplier` (`mwscript/falloutinterface.cpp:234-248`) multiplies by the legs factor, and by `fMoveSneakMult` when scripts force sneak. Does not match (150 against 84.7 and 77.0; no SpeedMult; no holstered factor). NPCs (`mwmechanics/falloutactors.cpp:490-502`) use the walk animation velocity clamped to 40-400 (default 120), then the leg factor. Does not match.
- **Round check.**
  - Unit level: set SpeedMult 100, 200 and 150, legs healthy, holstered and drawn, and read the computed walk speed. Expect `84.7 × SpeedMult/100` holstered and `77.0 × SpeedMult/100` drawn (NV and FO3 with weight 0). Tolerance ±0.05 u/s (float32 rounding only).
  - World level: walk on flat ground for at least 3 s after the start-up, and divide the horizontal displacement by the time. Tolerance ±5% until B.unclear 1 and 3 are settled, then ±2%.
- **Confidence.** Exe-read for the formula, both exes. The ESM numbers are read directly. The unit (units/s) is inferred: creatures' speed comes from animation movement data times the same SpeedMult/100 (B.M12), and the NV ESM script comments say "SpeedMult 200 = 2x normal speed". Online agreement only: GECK wiki Movement Settings lists base speed 77, run mult 4.0, sneak 0.57 (known from a search summary; the page was blocked).

### B.M2 Run speed and the run flag

- **Games.** Both. TTW uses NV.
- **Rule.** Run speed is the walk formula times `fMoveRunMult` (4.0, both ESMs). In NV, a perk entry point (42, "Travel Light") then multiplies the run speed again (B.M10). The player runs only when the run flag (0x200) is set. The input code withholds it while over-encumbered (B.M5).
- **Formula.** `run = ((walkFormula) + 0) × fMoveRunMult`. NV adds `run ×= perkEntry42(run)`. Non-player characters also get the +0xDC scale (B.unclear 6).
- **Settings.** `fMoveRunMult` 4.0 (NV ESM, FO3 ESM, exe default 4.0).
- **Exe, NV.** Run wrapper 0x647F00 (calls 0x647D10 at 0x647F29, multiplies by `fMoveRunMult` at 0x647F38). `Character::GetRunSpeed` 0x885BF0 (vtable +0x45C): calls 0x647F00 at 0x885D24, then the perk entry call 0x5E58F0(0x2A, actor, &speed) at 0x885D60.
- **Exe, FO3.** Run wrapper 0x584710 (0x584732 calls the core, 0x584741 multiplies by `fMoveRunMult`). `Character::GetRunSpeed` 0x6FB2E0, calling 0x584710 at 0x6FB37E. No perk entry in FO3.
- **Current-speed selector** (which of the speeds the player uses). NV `Actor::GetCurrentMoveSpeed` 0x8A0B10. With the flag word: 0x200 set → run, else walk. 0x800 (swim) → the swim variants (B.M8). 0x2000 (flying) → fly speed 0x886010. The result is cached through 0x884DC0 (walk) and 0x884EB0 (run). FO3's selector sits around 0x6FB3B0-0x6FB560 (calls 0x584760 swim walk, 0x584790 swim run, 0x5847E0 fly). It was not read in full.
- **Today.** `Npc::getRunSpeed` (`npc.cpp:1365-1371`): walk × (0.01 × Athletics 5 × 1.0 + 1.75) = 270 u/s. Does not match (338.8 and 308.0). No SpeedMult, no perk entry.
- **Round check.** Run flag set, not over-encumbered, SpeedMult 100: expect 338.8 (holstered) and 308.0 (drawn) in NV. FO3 the same at weight 0. Tolerance as B.M1.
- **Confidence.** Exe-read.

### B.M3 Holstered bonus (`fMoveNoWeaponMult`)

- **Games.** Both. Same multiplier 1.1.
- **Rule.** With the weapon holstered, a humanoid is 10% faster. It does not apply to creatures (their "is humanoid" test is false) or while a weapon is drawn.
- **Formula.** `speed ×= fMoveNoWeaponMult` when `holstered && isHumanoid`. `holstered` is the actor's process saying no weapon is drawn (NV process vtable +0x454, FO3 +0x450; both inverted to "holstered"). `isHumanoid` is actor vtable +0x218: true for `Character`, false for `Creature`.
- **Settings.** `fMoveNoWeaponMult` is in neither ESM. Exe default 1.1.
- **Exe.** NV 0x647EA8 (use), 0x885A87-0x885A98 and 0x885C1B-0x885C2D (holstered byte). FO3 0x5846B2. FO3 holstered byte 0x6FB20E-0x6FB223.
- **Today.** Nothing. Does not match.
- **Round check.** Same inputs, toggle the weapon drawn. The ratio holstered/drawn is 1.1 (±0.001).
- **Confidence.** Exe-read.

### B.M4 Sneak speed

- **Games.** Both.
- **Rule.** Sneaking multiplies the speed by `fMoveSneakMult` (0.57). It applies to the walk and the run result alike, because the sneak factor is inside the core function. The exes have no separate sneak setting for run.
- **Formula.** `speed ×= fMoveSneakMult` if sneaking. Sneaking = movement flag 0x400 set and 0x800 (swim) clear (NV 0x4997B0, read; the FO3 equivalent 0x45C200 is inferred from its position in the call, not read).
- **Settings.** `fMoveSneakMult` 0.57 (both ESMs).
- **Exe.** NV 0x647ECF. FO3 0x5846E5.
- **Today.** The Morrowind rule `× fSneakSpeedMultiplier` (0.75) on the walk speed, so 112.5. The Fallout interface adds 0.57 only when scripts force sneak. Does not match.
- **Round check.** Sneak walk, holstered, NV: 48.28. Drawn: 43.89. Tolerance ±0.1 at unit level, ±5% in the world.
- **Confidence.** Exe-read. Sneak plus run is B.unclear 4.

### B.M5 Encumbrance: weight effect, running, fast travel, carry weight

**B.M5a Carry weight.**
- **Games.** Both. Same settings and values.
- **Formula.** `carryWeight = fAVDCarryWeightsBase + fAVDCarryWeightMult × Strength`. Then perk entry point 22 "Get Max Carry Weight" is applied (FO3 name; the NV table is built lazily, but the code uses the same index: NV 0x8A0C20 pushes 0x16).
- **Settings.** `fAVDCarryWeightsBase` 150, `fAVDCarryWeightMult` 10 (both ESMs). NV exe defaults 150 and 25 (this is part B's reading; part E says the exes have the same values as the ESMs, see [disagreements](#where-the-spec-parts-disagree)). Strength 5 gives 200.
- **Exe.** NV 0x643A10 (the stat formula), 0x8A0C20 (`GetCarryWeight`, AV 13 plus entry 22). The FO3 over-encumbered check uses 0x6F6BC0.
- **Today.** `maxCarryWeight` in `falloutperks.cpp:21`. The Morrowind capacity (Strength × `fEncumbranceStrMult`) is a different formula but is bypassed because Fallout items are not in the Morrowind store.
- **Confidence.** Exe-read.

**B.M5b Slowdown with weight (FO3 only).**
- **Games.** FO3 only. NV has no weight term in its speed function.
- **Rule.** FO3's walk (and so run) speed falls linearly with the weight carried, up to 150. At 150 lb or more the speed is lower by 30% (holstered) or 40% (weapon drawn). It does not depend on the carry capacity, so a character with 400 capacity is slowed too.
- **Formula.** See B.M1 (FO3 branch). `speed ×= 1 − e × min(W, 150)/150`, with e = 0.3 holstered-humanoid and 0.4 otherwise.
- **Settings.** `fMoveEncumEffect` 0.4, `fMoveEncumEffectNoWeapon` 0.3, `fMoveWeightMin` 0, `fMoveWeightMax` 150 (exe defaults; neither ESM contains them).
- **Exe.** FO3 0x5845B7-0x5845C8 (clamp), 0x58466E-0x584684 (fraction), 0x5846A2-0x5846C6 (the two e values). The fly speed function 0x5847E0 also uses `fMoveEncumEffect`.
- **Today.** Morrowind `1 − fEncumberedMoveEffect × normalizedEncumbrance` (0.3 × 0 = 0 for a Fallout player). Does not match for FO3.
- **Round check (FO3).** Empty inventory: no change. W = 75: factor 0.85 holstered and 0.80 drawn. W ≥ 150: 0.70 and 0.60. Tolerance ±0.005 on the factor. NV: no change at any weight below the carry limit.
- **Confidence.** Exe-read for the formula and constants. That W is the actor's total carried weight is inferred (B.unclear 8).

**B.M5c Over the carry weight (both games).**
- **Rule.** When `InventoryWeight > carryWeight` the player is over-encumbered. The results:
  1. **No running.** The run flag 0x200 is set only when `!overEncumbered` (and not blocked, B.unclear 5). The player keeps walking at the normal walk formula.
  2. **No fast travel.** The request shows `sNoFastTravelOverencumbered` (FO3 text read from the exe: "You cannot fast travel while overencumbered."; the NV text was not read, same setting name). NV: blocked when over-encumbered unless perk entry point 51 (Long Haul, "Set Value 1") returns at least 1.0. FO3: blocked with no exception.
  3. **A message on crossing.** When an inventory change makes the player over-encumbered (and it was not before), the HUD shows `sOverEncumbered` ("You are overencumbered and cannot run!") for 2.0 s. Nothing shows when you become unencumbered.
  4. **Leg penalty quirk (NV and FO3).** `IgnoreCrippledLimbs` does not apply while over-encumbered.
  5. NV: no extra slowdown. FO3: the slowdown of B.M5b, the same as at any weight.
- **Exe, NV.** `PlayerCharacter::IsOverEncumbered` 0x954CC0 (vtable 0x108AA3C +0x358). It reads AV 46 base against 0x8A0C20, and a debug flag 0x9526B0 (when set, never over-encumbered). Callers: the run-flag gate in 0x93E840 at 0x941725, 0x941A00, 0x941A8C, 0x941B06. Fast travel in 0x93D500 at 0x93D902 (message at 0x93D944, entry point 0x33 at 0x93D927, compare with 1.0 at 0x93D937). Message hook 0x8BA2F0 (called at 0x4D0F29 from the inventory-weight update 0x4D0900).
- **Exe, FO3.** `IsOverEncumbered` 0x76D310. The run-flag gate is in the player update 0x788350: `[vtable+0x358]` at 0x7897EE and 0x789851, flag 0x200 set at 0x789814 and 0x78986E. Fast travel 0x774CF0 (0x7750A4-0x7750B6). Message hook around 0x6F95CF.
- **Today.** Nothing of this. Weight does not reach the Morrowind store, so the player is never over-encumbered by any item. The Pip-Boy weight line and the travel and wait restriction use `maxCarryWeight` (`falloutneeds.cpp:209-211`). The run restriction and the message do not exist. Does not match.
- **Round check.** Give the player 1 lb above the carry weight, hold run and forward. Expect walk speed (the run flag is never set) and the over-encumbered message once. Fast travel returns the blocked message (NV with Long Haul: allowed). Equal weight (W = carry weight) is not over-encumbered. Boolean outcomes are exact. Speed ±5%.
- **Confidence.** Exe-read for NV and FO3. The Long Haul text and entry come from the ESM (PERK 0x146098: entry 51, function "set", value 1.0, description "Being over-encumbered no longer prevents you from using Fast Travel").

### B.M6 Armour and weapon penalties (NV only)

- **Games.** NV (and TTW). FO3 has none.
- **Rule.** Body armour class and a drawn two-handed weapon cut the speed. The two penalties add up, then multiply the speed by `(1 − sum)`. The armour penalty always applies. The weapon penalty applies only while a weapon is drawn.
- **Formula.** `penalty = (armourHeavy ? fMoveHeavyArmorPenalty : armourMedium ? fMoveMediumArmorPenalty : 0) + (drawn && weaponTwoHanded ? (type ∈ {8,9} ? fMove2HBigPenalty : type ≠ 2 ? fMove2HRPenalty : 0) : 0)`.
  - Two-handed set from the jump table: WEAP animation types 2, 5, 6, 8, 9. Type 8 (two-hand handle: minigun, grenade machinegun) and type 9 (two-hand launcher) take the "big" penalty. Type 5 (rifle) and type 6 (automatic) take the "2HR" penalty. Type 2 (two-hand melee) takes none. Type 7 (laser and plasma rifles in the NV data) is excluded. That is a literal read, odd but real.
  - Armour class: heavy when the armour's general flag byte has 0x80, medium when it has 0x08 (read from the code; the field names were not verified).
- **Settings (NV).** `fMoveHeavyArmorPenalty` 0.15, `fMoveMediumArmorPenalty` 0.075, `fMove2HBigPenalty` 0.10 (all NV ESM). `fMove2HRPenalty` 0.10 (exe default; not in the ESM).
- **Exe.** NV 0x647D1B-0x647DCE (armour 0x514410, weapon 0x646CB0). The weapon type byte is at WEAP object +0xF4 (0x446390).
- **Today.** Nothing. Does not match (no armour penalty).
- **Round check.** NV, holstered, heavy armour: walk `84.7 × (1 − 0.15) = 71.995`, run 287.98. Medium armour: 78.348 and 313.39. Drawn minigun in light armour: 77 × 0.9 = 69.3. Tolerance ±0.05 unit level, ±5% world.
- **Confidence.** Exe-read for the structure. The ARMO flag meanings are inferred.

### B.M7 Crippled legs

- **Games.** Both.
- **Rule.** The two leg actor values (AV 29 `LeftMobilityCondition`, AV 30 `RightMobilityCondition`) are limb condition. A leg counts as crippled when its condition is 0 or less. One crippled leg multiplies the speed by 0.85, two by 0.75. The factor sits inside the core function, so it scales walk, run and sneak, and it applies to NPCs too.
- **Formula.** `crippled = (cond29 ≤ 0) + (cond30 ≤ 0)`. `legMult = [1, fMoveOneCrippledLegSpeedMult, fMoveTwoCrippledLegsSpeedMult][crippled]`. If `IgnoreCrippledLimbs` (AV 72) is non-zero and the actor is not over-encumbered, `legMult = 1`.
- **Settings.** 0.85 and 0.75 (both ESMs). `fMoveLegConditionMult` (0.9) has no code user in either exe: dead.
- **Exe.** NV leg helper 0x646800 (clamps the AV value, then `value > 0 ? 1 : 0`; called with AV 0x1D and 0x1E at 0x647DD6 and 0x647DED). FO3 helper 0x582B30 with AV 0x1D and 0x1E at 0x5845DA and 0x5845EC.
- **Today.** `FalloutScripts::crippledLegs` and `legSpeedMultiplier` (`mwscript/falloutinterface.cpp:220-247`, `components/falloutscript/hitreactions.cpp:68-80`): same AVs, same threshold (≤ 0), same multipliers from the same settings. Matches the rule. But it is applied on top of the wrong base speed, and it does not check `IgnoreCrippledLimbs` (AV 72). NPCs use the same helper (`falloutactors.cpp:1444-1450`).
- **Round check.** Left leg condition 0: walk (NV, holstered, SpeedMult 100) = 71.995, run = 287.98. Both legs: 63.525 and 254.10. With AV 72 = 1 and not over-encumbered, the speed returns to 84.7 and 338.8. Tolerance ±0.05 unit level, ±5% world.
- **Confidence.** Exe-read.

### B.M8 Swimming

- **Games.** Both (same behaviour).
- **Rule.** When the swim flag (0x800) is set, the game uses a swim variant of the same formulas with the sneak factor forced off. Swimming at walk gives the non-sneaking walk speed (84.7 holstered, 77.0 drawn, in NV). Swimming with the run flag gives the run speed. The Morrowind-style swim settings and Athletics terms are not used anywhere.
- **Formula.** `swimWalk = core(…, sneak = 0)`. `swimRun = core(…, sneak = 0) × fMoveRunMult`.
- **Settings.** `fMoveSwimWalkBase`, `fMoveSwimRunBase`, `fMoveSwimWalkAthleticsMult`, `fMoveSwimRunAthleticsMult` (exe defaults 0.5, 0.5, 0.02, 0.1; in neither ESM) have no code reader in either exe: dead. `fJumpSwimmingMult` (2.0) is only a jump setting (NV 0x930684).
- **Exe.** NV swim walk 0x885D90 (core via 0x647F50 at 0x885EB8), swim run 0x885ED0 (run wrapper 0x647F90 at 0x885FF8), selector 0x8A0B10 (flag 0x800 tests at 0x8A0B55 and 0x8A0B9A). FO3 swim walk 0x584760, swim run 0x584790, callers at 0x6FB445 and 0x6FB4F5.
- **Today.** `Npc::getSwimSpeed` (`npc.cpp:1373-1377`, `mwclass/actor.hpp:28-35`): the base walk or run speed × `fSwimRunBase 0.5 + 0.01 × Athletics 5 × fSwimRunAthleticsMult 0.1` = 150 × 0.505 = 75.75 (walk) and 270 × 0.505 = 136.35 (run). Computed, not measured. Does not match (NV expects 84.7 and 338.8 before water effects).
- **Round check.** In water at SpeedMult 100, holstered, walk: expect 84.7 in NV (unit level ±0.05). World-level displacement is not a safe check until B.unclear 9 is resolved. Mark it as an observation, not a failure, within ±20%.
- **Confidence.** Exe-read for the formula and the dead settings. The physical water speed is unclear.

### B.M9 Direction, camera and weapon in hand

- **Games.** Both.
- **Backward and strafe.** The input code (NV 0x93E840 at 0x941B60-0x941D0F) gets one speed from `GetCurrentMoveSpeed` and builds two scaled axis vectors from it (`× speed × ramp`). It adds or subtracts them per key (forward +v1, backward −v1, right +v2, left −v2). So all four directions have the same speed number, and no setting scales backward or strafe. If the original game moves slower backwards, that comes from the animation or the physics, not from a rule found in the exe. Diagonals are B.unclear 2.
- **First or third person.** No camera input is in the speed chain.
- **Weapon in hand or holstered.** B.M3 (holstered bonus) and, in NV, B.M6 (heavy-weapon penalty). In FO3 the drawn state also selects the larger encumbrance coefficient (0.4 against 0.3).
- **Today.** The Morrowind movement code scales no direction either. Camera and weapon state are not inputs. No rule to match, except the holstered and drawn factors (missing).
- **Round check.** With SpeedMult 100, move straight forward, straight backward and sideways for 3 s each. Record the three speeds. Expect them equal within ±5% (this tests B.unclear 1, not a formula).
- **Confidence.** Exe-read for "one scalar". Inferred for "no other direction term".

### B.M10 Perk, chem and armour effects on speed

- **Games.** NV has the Travel Light perk. FO3 has no speed perk.
- **Travel Light (NV, PERK 0x146096).** Perk entry point 42 (multiply, ×1.1): "While wearing light armor or no armor, you run 10% faster." It is applied only in the run function, so it does not change walk, sneak walk, or the walking-only state of over-encumbered.
- **Long Haul (NV, PERK 0x146098).** Entry 51. It lets the player fast travel while over-encumbered (B.M5c).
- **Chems, armours, enchantments and abilities.** No effect with actor value 21 (SpeedMult) exists in `FalloutNV.esm` or `Fallout3.esm` (all ALCH, ENCH and SPEL effects were scanned). The only writers of SpeedMult are scripts. NV scripts set NPC speed (125 for Legion soldiers, 150-175 in several quests, NCR radio operator 200 and 1000 with comments "Set speed to 2x normal speed (SpeedMult should be 200)"). FO3 scripts: none. An MGEF `IncreaseSpeedMult` exists in both ESMs, but no record uses it.
- **Needs.** Dehydration, hunger, sleep and radiation do not feed this function (no AV read of 73-75 or RadiationRads in the core).
- **Formula.** Run (NV) `×= 1.1` when light armour or none is worn. The value comes from the perk record (EPFD 1.1).
- **Exe.** NV 0x885D60 (`ApplyPerkModifiers(0x2A, actor, &run)`).
- **Today.** Not implemented. Does not match.
- **Round check.** NV, holstered, light armour, Travel Light: run 372.68 (338.8 × 1.1). Walk unchanged at 84.7. Tolerance ±0.1 unit level.
- **Confidence.** Exe-read (entry call and index) plus ESM read. The meaning of entry 42 is confirmed by the perk's own description.

### B.M11 Turning speed

- **Games.** Both.
- **Rule.** Mouse turning has no speed constant. The keyboard and pad turn flags 0x10 and 0x20 start a turn animation after `fPCTurnAnimMinTime` (0.1 s). No angular rate was found. INI-style `fPlayerCharacterTurnMult:Controls` and, for NPCs, `fAITurnSpeedScale:Pathfinding` exist in the exe. Their defaults and use were not read.
- **Exe.** `PlayerMover::SetMovementFlags` NV 0x9EA3E0 (vtable 0x109297C +0xC), uses `fPCTurnAnimMinTime` at 0x9EA415. FO3 0x7E8F90.
- **Today.** Not a movement rule today (Morrowind camera code). Not comparable.
- **Round check.** None until B.unclear 7 is read.
- **Confidence.** Exe-read for the flags. The angular rate was not found.

### B.M12 Creatures and humanoid NPCs

- **Games.** Both.
- **Humanoid NPCs** (class `Character`) use exactly B.M1 to B.M7 with their own SpeedMult from their `NPC_` ACBS (mostly 100; scripts change it). They are also 84.7 and 338.8 (NV, holstered, SpeedMult 100). They are not animation-speed driven.
- **Creatures** (class `Creature`) do not use the setting formula. NV `Creature::GetWalkSpeed` 0x8D4EB0 and `GetRunSpeed` 0x8D4F10 return 0 when over-encumbered (never true for creatures). Otherwise they return `SpeedMult/100 ×` the movement speed of the creature's own animation group 3 (Forward) for walk or group 7 (FastForward) for run, read through 0x8D4F70. The group index meanings come from the exe's animation numbering. The speed itself comes from the animation's movement data.
- **Path-following AI movement** (NV 0x9EA8A0) steps along the path by `GetCurrentMoveSpeed() × time`. So an NPC's travel speed on a path is the same number.
- **Cached values.** An actor's speeds are cached on its process (flag 0x1000 marks "recompute"). A SpeedMult change shows only after the next recompute (NV thunks 0x884C80 walk and 0x884D20 run).
- **Today.** `falloutactors.cpp:490-502`: NPC walk velocity from the animation (`FalloutActorAnimation::getWalkVelocity` and `getRunVelocity`), clamped to 40-400 (walk) and to 800 (run), defaults 120 and 300. Humanoids do not match (should be the formula). Creatures match the rule's idea (animation speed) but lack the SpeedMult factor.
- **Round check.** One humanoid NPC at SpeedMult 100, holstered, walking a path: expect 84.7 (NV), tolerance ±5%. One creature: its animation's movement speed × SpeedMult/100 (compare to the measured kf speed, ±5%).
- **Confidence.** Exe-read for the class split and the formulas. The creature animation speed was not read for a specific creature.

### B.numbers Expected speeds for a fresh level-1 character (units per second)

Assumptions: SpeedMult 100 (player ACBS), light or no armour, no armour penalty, legs healthy, not sneaking unless stated, inventory weight 0 (FO3 depends on weight), carry weight 200 (Strength 5). "Holstered" means the weapon is not drawn. "Drawn" means a one-handed weapon is drawn (no NV weapon penalty). NV and TTW use the NV column.

| Case | NV / TTW | FO3, weight 0 | FO3, weight 75 | FO3, weight ≥ 150 |
|---|---|---|---|---|
| Walk, holstered | **84.7** | 84.7 | 71.995 | 59.29 |
| Walk, weapon drawn | **77.0** | 77.0 | 61.6 | 46.2 |
| Run, holstered | **338.8** | 338.8 | 287.98 | 237.16 |
| Run, weapon drawn | **308.0** | 308.0 | 246.4 | 184.8 |
| Sneak walk, holstered (0.57) | **48.279** | 48.279 | 41.037 | 33.795 |
| Sneak walk, weapon drawn | **43.89** | 43.89 | 35.112 | 26.334 |
| Sneak run, holstered (if the run flag is allowed, B.unclear 4) | 193.12 | 193.12 | 164.15 | 135.18 |
| Swim walk, holstered (formula; B.unclear 9) | 84.7 | 84.7 | 71.995 | 59.29 |
| Swim run, holstered (formula; B.unclear 9) | 338.8 | 338.8 | 287.98 | 237.16 |
| One crippled leg, walk, holstered (×0.85) | **71.995** | 71.995 | 61.196 | 50.396 |
| One crippled leg, run, holstered | 287.98 | 287.98 | 244.78 | 201.59 |
| Two crippled legs, walk, holstered (×0.75) | **63.525** | 63.525 | 53.996 | 44.468 |
| Two crippled legs, run, holstered | 254.10 | 254.10 | 215.99 | 177.87 |
| Over-encumbered (weight above carry weight), walk, holstered | **84.7** (no run flag; no slowdown) | 59.29 (the weight term is at its cap; no run) | n/a | n/a |
| Over-encumbered, wants to run | walks at the walk speed; message once | same | same | same |
| NV, heavy armour (×0.85), holstered walk / run | 71.995 / 287.98 | n/a | n/a | n/a |
| NV, medium armour (×0.925), holstered walk / run | 78.348 / 313.39 | n/a | n/a | n/a |
| NV, two-hand rifle or automatic drawn (×0.9) walk / run | 69.3 / 277.2 | n/a | n/a | n/a |
| NV, minigun or launcher drawn (×0.9 in the NV ESM) walk / run | 69.3 / 277.2 | n/a | n/a | n/a |
| NV Travel Light, run, holstered (×1.1) | 372.68 | n/a | n/a | n/a |

Run is a factor 4 on every walk entry. Sneak, crippled-leg and weight factors multiply; they do not add. The armour and weapon penalties are summed first, then used as `1 − sum`.

A fresh FO3 character carries items after the Vault 101 start (a pistol, ammunition, clothes, Pip-Boy). For a deterministic round, use an empty inventory or set the weight explicitly.

**Tolerances, and why.**
- Rule outputs (the function's own result): ±0.05 u/s. The inputs are float32 settings and one multiplication chain. Any difference beyond rounding is a bug.
- Measured displacement in the running game: ±5% (walk, run, sneak) until B.unclear 1 and 3 are resolved by measuring the real game. Then ±2%. The measurement window starts after the ramp (ignore the first 0.5 s) and lasts at least 3 s on flat ground.
- Swimming: ±20%, reported as an observation (B.unclear 9).
- Ratios (holstered/drawn, legs, sneak): ±0.5% on the ratio, because the common factors cancel.
- Boolean outcomes (can run, can fast travel, message shown): exact.

### B.today Today's engine, in one table

| Item | Today | File:line | Matches? |
|---|---|---|---|
| Player walk | Morrowind formula, 150 u/s (computed) | `mwclass/npc.cpp:1347-1363` | No (84.7 and 77.0) |
| Player run | walk × 1.8 = 270 u/s (computed) | `npc.cpp:1365-1371` | No (338.8 and 308.0) |
| SpeedMult actor value | not read | `falloutinterface.cpp:234-248` (comment: "Fallout `SpeedMult` does not feed it") | No |
| Holstered bonus 1.1 | absent | n/a | No |
| Sneak | ×0.75 (Morrowind); ×0.57 only when scripts force it | `npc.cpp:1360`, `falloutinterface.cpp:238-243` | No |
| Weight | Morrowind store, always 0 for a Fallout player | `class.cpp:495-508`, `npc.cpp:883` | No |
| Over-encumbered no-run, fast-travel block | absent (travel and wait use `maxCarryWeight` separately) | `falloutneeds.cpp:209-211` | Partly (travel and wait only) |
| NV armour and weapon penalties | absent | n/a | No |
| Crippled legs 0.85 and 0.75, threshold ≤ 0 | same settings, same AVs | `falloutinterface.cpp:220-247`, `hitreactions.cpp:68-80` | Yes (base is wrong); AV 72 not checked |
| Swim | base × (0.5 + 0.005) = 75.75 / 136.35 | `mwclass/actor.hpp:28-35`, `npc.cpp:1373-1377` | No |
| NPC speed | animation velocity, clamped 40-400, default 120 / run 300 | `mwmechanics/falloutactors.cpp:490-502` | No for humanoids |

### B.index Exe address index for movement

NV (base 0x400000):
- Core speed 0x647D10. Run wrapper 0x647F00. Swim-walk wrapper 0x647F50. Swim-run wrapper 0x647F90. Fly 0x647FD0. Leg helper 0x646800.
- `Character::GetWalkSpeed` 0x885A50, `GetRunSpeed` 0x885BF0 (vtable 0x1086A6C +0x458 and +0x45C; also PlayerCharacter table 0x108AA3C). Swim walk 0x885D90. Swim run 0x885ED0. Fly 0x886010. Selector 0x8A0B10. Cached wrappers 0x884DC0 and 0x884EB0. Cache thunks 0x884C80 and 0x884D20.
- Creature walk and run 0x8D4EB0 and 0x8D4F10. Creature animation speed 0x8D4F70.
- `PlayerCharacter::IsOverEncumbered` 0x954CC0 (vtable +0x358). Carry weight 0x8A0C20. Carry formula 0x643A10. Inventory-weight update 0x4D0900. Message hook 0x8BA2F0.
- Player input and run-flag gate 0x93E840. Fast-travel gate 0x93D500.
- `PlayerMover::SetMovementFlags` 0x9EA3E0. Path-following mover 0x9EA8A0.
- Setting objects: `fMoveBaseSpeed` 0x11D0448, `fMoveRunMult` 0x11D0898, `fMoveNoWeaponMult` 0x11D0C30, `fMoveSneakMult` 0x11CFFF4, `fMoveOneCrippledLegSpeedMult` 0x11D11B8, `fMoveTwoCrippledLegsSpeedMult` 0x11D0268, `fMoveHeavyArmorPenalty` 0x11D0EAC, `fMoveMediumArmorPenalty` 0x11D0024, `fMove2HBigPenalty` 0x11D09AC, `fMove2HRPenalty` 0x11D08BC.
- Actor value indexes (from the exe's name table at 0x66EFE0): 13 CarryWeight, 21 SpeedMult, 29 and 30 left and right mobility condition, 46 InventoryWeight, 72 IgnoreCrippledLimbs.

FO3 (base 0x400000):
- Core speed 0x5845B0. Run wrapper 0x584710. Swim walk 0x584760. Swim run 0x584790. Fly 0x5847E0. Leg helper 0x582B30.
- `Character::GetWalkSpeed` 0x6FB200. `GetRunSpeed` 0x6FB2E0. `PlayerCharacter::IsOverEncumbered` 0x76D310. Carry weight 0x6F6BC0.
- Run-flag gate in the player update 0x788350 (sites 0x7897EE, 0x789851). Fast travel 0x774CF0.
- Setting value fields (the record layout in the exe's table is value, name, tag): `fMoveEncumEffect` 0xF61080, `fMoveEncumEffectNoWeapon` 0xF6108C, `fMoveRunMult` 0xF61098, `fMoveWeightMin` 0xF610A4, `fMoveWeightMax` 0xF610B0, `fMoveNoWeaponMult` 0xF610BC, `fMoveSneakMult` 0xF610C8, `fMoveBaseSpeed` 0xF610D4.

### B.unclear Open items in part B

Do not build tests on these without a measurement.

1. How the speed number becomes displacement. The exe-read chain ends at "desired velocity = GetCurrentMoveSpeed() × ramp × direction vectors", stored into the player mover (0x93e840 → `PlayerMover` +0x88). The Havok character controller and the animation root motion were not traced. The spec assumes the number is the target ground speed in units/s. This needs a real-game measurement before a round enforces it end to end.
2. Diagonal movement. The forward, back, left and right vectors are summed with equal magnitude. A later call (0x94278d → 0x439ef0) may normalise the sum. Not read.
3. The ramp scalar. A global float (NV 0x11A3B3C) multiplies the speed. It is eased between 0 and 1 and forced to 1.0 under some conditions (including over-encumbered). The easing was not decoded. Steady state is taken as 1.0 (inferred).
4. Sneak plus run. If both flags can be set, the exe gives run × sneak (193.1 u/s holstered in NV). It was not proved that the input code allows the run flag while sneaking.
5. A process flag that blocks running. The run flag (0x200) is also withheld when a HighProcess byte at +0x228 (NV, through process vtable +0x404) is set, and when the player is in process state 7. What these mean was not identified (a "movement locked" or restrained state is likely).
6. Scale for non-player NPCs. For non-player `Character` actors, vtable +0x428 (NV 0x8A02D0) can return a form of type 0x12 whose floats at +0xD8 (walk) and +0xDC (run) multiply the result. What this form is was not identified. The player always gets "none".
7. Turning speed (see B.M11).
8. FO3 weight argument. The formula shape and the clamp were read exactly. That the argument is the actor's total carried weight (the same value as AV 46 `InventoryWeight`) is inferred from the call shape (a cached "inventory weight" slot filled by an inventory loop), not proved. Whether equipped items count is not shown.
9. Swimming. The formulas were read. The physical swim speed (water drag, buoyancy) was not traced.
10. Armour class and slot. Which equipped item is passed as "armour" (a call with slot index 2), and that the class comes from flags 0x80 (heavy) and 0x08 (medium) of the armour record's flag byte, are read from the code. The ARMO subrecord field names are not verified.
11. Weapon types. Which WEAP animation types count as "two-handed" (2, 5, 6, 8, 9) is exact from the jump table. That type 7 is excluded is a literal read.
12. Travel Light condition. The description says "light armor or no armor". The condition record was not decoded.
13. Exact first frame and acceleration. No ramp-up time constant was read for the start or end of movement.

---
## C. Jump, fall and water

### C.0 Summary

- **Jump.** A jump goes up exactly `fJumpHeightMin` = 64 game units (about 0.91 m) times the character's size factor. Nothing else counts: no Acrobatics (there is no such skill), no Agility, no level, no weight, no sneak, no leg condition. The only change is ×2 when the character is swimming (`fJumpSwimmingMult`). The jump keeps the speed the character already has. The exe turns the height into a launch speed with `sqrt(2 × gravity × height)`, so the top of the jump is the stated height.
- **Fall.** The game remembers the height where the character left the ground (take-off of a jump, or the edge of a ledge). On landing, the height lost is that start height minus the landing height. A fall of 600 units or less hurts nothing (about 8.57 m). Above that, damage = `0.025 × (height lost − 600) ^ 1.65`. Nothing reduces it: no skill, no perk, no armour. Only the difficulty setting scales it. The player also gets a landing sound and, for each leg, a 50% chance to lose half the damage in leg condition.
- **Water.** There is no `fSwimHeightScale`. The exe measures how much of the body is under water: `(water level − feet height) / (body height × size factor)`. At 0.01 the body counts as in water (wading). At 0.7 the character swims. At 0.875 the head is under. Swimming starts and stops at the 0.7 line.
- **Breath.** Only the player drowns. Breath time is `10 + 0.5 × int(Healing Rate)` seconds: 10 s for a fresh character (Healing Rate 0 at Endurance 5). It counts down by one per second while the head is under, and refills at once when the head comes out. When it passes zero, the player loses 0.2 × current health per second (20% of what is left each second), and the drowning sound plays.
- **TTW.** TTW uses the NV rules. The TTW ESM overrides none of the settings in this part (its only override of the FO3 set is `fDamageMeleeWeapCondMult`).
- **Today.** Today's engine uses a Morrowind jump formula (placeholder Acrobatics 5). It measures fall height by adding up every downward step (not start minus end). It suppresses all fall damage in water. It swims by Morrowind's `fSwimHeightScale`. The fall damage formula, the leg rule and the landing sounds already match the exe.

### C.J1 Jump height (take-off)

- **Games.** FO3 and NV identical. TTW = NV.
- **Rule.** Pressing jump asks the actor for a jump. The jump height is 64 units times the actor's size, doubled when the actor is swimming.
- **Formula.**
  `h_game = fJumpHeightMin × sizeFactor × (swimming ? fJumpSwimmingMult : 1)`
  `sizeFactor = refScale × npcHeight` for an NPC base (type 0x2A), or `refScale × creatureScale` for a creature base (0x2B).
  Then `h_hk = h_game / 6.9991257` (the Havok length unit is 6.9991257 game units, i.e. 0.1 m at 69.99 units per metre) is stored in the character controller, and state 1 (jump) is set.
- **Settings.**

  | Setting | NV ESM | FO3 ESM | Exe default |
  |---|---|---|---|
  | fJumpHeightMin | absent | absent | 64.0 |
  | fJumpSwimmingMult | 2.0 | 2.0 | 2.0 |

- **Exe, NV.** Jump function 0x930640 (reads `fJumpHeightMin` at 0x93065C, swimming test 0x9306B0 = movement type 5, `fJumpSwimmingMult` at 0x930684, call to controller jump 0xC6D4B0 at 0x93069F). Size factor 0x567400 (ref float +0x3C × 0x944300 NPC +0x1F4, or 0x567470 creature +0x13C). Controller jump 0xC6D4B0: state (+0x520) = 1, `ctrl+0x540 = h × (1/6.9991257)`, where `1/6.9991257` is the global 0x1267BCC computed at 0xFBCCD0 from 0x11B0194 (= 6.9991255). The actor-level wrapper is 0x884AA0 (Actor vtable slot 0x254).
- **Exe, FO3.** Jump function 0x7668B0 (`fJumpHeightMin` value read at 0x7668D5, size 0x4E6D80, swimming test 0x919EF0 == 5 at 0x7668EA, `fJumpSwimmingMult` 0x7668EF). Controller jump 0x8E9260 (state `ctrl+0x350` = 1, `ctrl+0x3CC = h × 0.1428767`).
- **Launch speed** (the proof that the apex equals h). NV 0xCD42F3 region (key: `fabs [ctrl+0x540]`, `fadd st,st`, `fmulp`, `call sqrt 0xEC6040` at 0xCD43D9). FO3 0x96BB23-0x96BB41. Vertical launch speed = `sqrt(2 × |g × gravityMult| × h_hk)`. The existing horizontal velocity is kept. The gravity multiplier `ctrl+0x54C` (NV) or `ctrl+0x3D8` (FO3) is 32.0 from the controller info (`[0x101E340]` NV, `[0xDBEB38]` FO3 = 32.0).
- **Fall start.** At take-off the controller stores the start height `ctrl+0x544` = current z (NV 0xCD446F; game units: 0xC6E300 position, then ×6.9991257 by 0x458620).
- **Today.** `Npc::getJump` (`apps/openfallout/mwclass/npc.cpp:888`) is the Morrowind formula (`fJumpAcrobaticsBase`, `fJumpAcroMultiplier`, Acrobatics placeholder 5, encumbrance term, run multiplier, fatigue term, minus gravity, /3). The result is applied as a vertical speed (`mwmechanics/character.cpp` about line 2220, with a 0.707 factor when moving). Does not match: the exe has no skill, run, fatigue or encumbrance term, and the height is a constant 64 units (×1, ×2 swimming).
- **Round check.** Fresh level-1 character, standing still on flat ground, jump once, and sample the player z every frame. Expected apex above the take-off z: 64.0 units (0.914 m) in FO3, NV and TTW (size factor 1.0, see C.unclear 1). Tolerance ±8 units: the exe's formula gives the apex of a continuous parabola, Havok steps it in discrete frames (error about v × dt / 2, a few units), and frame sampling adds up to one step. A swimming run (head up, depth ≥ 0.7) is not a clean test; if wanted, expect 128 units with ±16.
- **Confidence.** Exe-read for the formula, constants and launch. Inferred for the size factor and for "apex = h" (the in-flight update was seen using the same `ctrl+0x54C` product but not traced line by line).

### C.J2 Air control, double jump, dead settings

- **Games.** Same. TTW = NV.
- **Rule.** In the air a character has a small steering factor. Three jump settings of the Oblivion era do nothing in these exes.
- **Formula (air steering).** In the jump state, the controller scales the requested horizontal movement by `v = fJumpMoveBase + fJumpMoveMult × [ctrl+0x534]`, or by 1.0 when controller flags 0x1800 are set (swimming or other). `[ctrl+0x534]` comes from the controller info and equals `fJumpFallHeightMin` (see C.unclear 7).
- **Settings (exe defaults; none in the ESMs).** `fJumpMoveBase` 0.0, `fJumpMoveMult` 0.3.
  - NV does not read the settings. The 0.3 is a constant at 0x11B0140 and the base is a zero global at 0x1267BBC (code 0xCD40AB and 0xCD40B1, function 0xCD3FB0).
  - FO3 copies the settings into globals at start (0x56C3A8-0x56C3FD: 0xF79ACC = `fJumpMoveMult`, 0x10F2E88 = `fJumpMoveBase`) and reads them at 0x96B72B (function 0x96B630).
- **Dead settings** (no code reads them in either exe): `fJumpDoubleMult` (0.5), `fJumpFallVelocityMin` (exe 600, ESM 2785; FO3 copies it into a global 0xF79AD8 that nothing reads), `fJumpFallSkillBase` (1.25), `fJumpFallSkillMult` (-0.01), `fJumpFallRiderMult` (0). There is no double jump and no skill-based fall reduction. NV addresses: the only references of the five setting objects are their static initialisers at 0xF65CF3-0xF65E73.
- **Today.** None of this exists. The Morrowind jump has its own air behaviour. Nothing to match for the dead settings.
- **Round check.** None (air steering needs the unknown scalar of C.unclear 7). Implement the dead settings as no-ops.
- **Confidence.** Exe-read for "dead" and for the formula. Unclear for what the formula multiplies.

### C.J3 Who may jump (sneak, encumbered, crippled legs)

- **Games.** Same as far as read.
- **Rule.** The jump function itself asks nothing: no sneak flag, no weight, no leg condition. The result of a jump with crippled legs is the same height. Whether another layer refuses the key press is not known.
- **Evidence.** NV 0x884AA0 and 0x930640, FO3 0x7668B0: the only inputs are the size factor and the swimming state. Crippled legs change speed only (B.M7: `fMoveOneCrippledLegSpeedMult` 0.85, `fMoveTwoCrippledLegsSpeedMult` 0.75). Over-encumbered blocks running only (B.M5). The C spec cites these as "M4" and "M5"; the crippled-leg rule is B.M7.
- **Today.** `Npc::getJump` returns 0 when over-encumbered (normalised encumbrance > 1; always 0 for a Fallout player because Fallout items are not in the Morrowind store), and when paralysed, knocked down or dead. This partly differs: the exe's jump functions give no basis for the encumbrance rule.
- **Round check.** Jump with both legs crippled: expect the same 64-unit apex (an inference: the exe has no leg term). Do not test over-encumbered until C.unclear 3 is resolved.
- **Confidence.** Exe-read for what the jump functions do not read. Unclear for the input layer.

### C.F1 How the fall height is measured

- **Games.** Same. TTW = NV.
- **Rule.** The height lost is the start height minus the height at the moment of landing. The start height is taken once: at the take-off of a jump, or when the character leaves the ground at a ledge. Going up and down again in the air adds nothing. The top of a jump does not count, only where it began.
- **Formula.** `lost = ctrl.startZ − currentZ` (game units). `currentZ` is the controller position (0xC6E300) converted from Havok units to game units (×6.9991257 by 0x458620 / 0x4587D0). It returns 0 when `startZ == 0.0` (C.unclear 10).
- **Exe.** NV 0xC70550 (function: `if [ctrl+0x544] != 0: [ctrl+0x544] − z`). The start is written at jump take-off 0xCD446F and at walking off a ledge 0xCD4855 (state 2 = in air; the start comes from 0x812B00, which uses the same 0xC6E300 + 0x458620). FO3: distance 0x8ED050.
- **When the landing counts.** The controller sets the "landed" bit 0x80 of its flags (NV `ctrl+0x414`, set at 0xCD3FE7 when the ground contact bit 0x100 is set while the state is not 0; FO3 `ctrl+0x294`, set at 0x96B65A). The damage routine runs once per update, tests that bit and clears it (NV clears it in the player branch, 0x8A6630; FO3 clears it first, 0x709387). So every jump landing runs the formula. A flat jump has about 0 lost.
- **Today.** The player: `CreatureStats::mFallHeight` (`mwphysics/mtphysics.cpp:270`) adds up every downward step while airborne and resets on landing. It is not start minus end. A character that goes up on stairs in the air, or is pushed up, keeps the sum of all falls. A water or flying frame resets it (`mtphysics.cpp:267`). NPCs: `falloutactors.cpp:731`: `mFallFrom - place.z()`, start minus end (matches). The player differs in odd paths (climbing in the air, ledge steps). A plain drop is the same.
- **Round check.** Drop the player from an exact height by teleport and release. The log line `Fall of ... units` (and the exe-style number) must equal the dropped height within ±6 units (capsule and skin offset, and the frame in which the landing is seen). Test a drop onto a higher ledge (negative lost: no damage) and a jump-and-land on flat ground (< 10 units lost, no damage).
- **Confidence.** Exe-read (NV and FO3 function bodies read; start writers read in NV only; the FO3 start writer by analogy).

### C.F2 Fall damage

- **Games.** Same formula and same values in FO3 and NV (the NV and FO3 ESMs have the same overrides). TTW = NV.
- **Rule.** A fall of 600 units (8.57 m) or less is free. Beyond that, take 0.025 times the excess to the power 1.65. There is no cap. Level, skills, perks, armour and weight do not change it. At very easy the player takes half. At very hard the player takes double.
- **Formula.**
  `excess = lost − fJumpFallHeightMin`. If `lost <= min` there is no damage.
  `damage = fJumpFallHeightMult × excess ^ fJumpFallHeightExponent` (float `pow`, NV 0x4DD130 → 0x4DD150; FO3 0xC03980).
  Skipped (damage 0) for a flying creature: base type 0x2B with ACBS flag 0x20 (NV 0x5F0C40; FO3 0x54A8F0).
  Applied through the actor's "damage health" slot (Actor vtable +0x338, NV 0x89D6F0, FO3 0x6FD240) as `damage(dmg, 0.0, no attacker)`.
- **Settings.**

  | Setting | NV ESM | FO3 ESM | Exe default |
  |---|---|---|---|
  | fJumpFallHeightMin | 600.0 | 600.0 | 256.0 |
  | fJumpFallHeightExponent | 1.65 | 1.65 | 2.0 |
  | fJumpFallHeightMult | absent | absent | 0.025 |
  | fHardLandingDamageThreshold (INI group "Audio", full name `fHardLandingDamageThreshold:Audio`) | absent | absent | 500.0 |
  | fFallLegDamageMult | absent | absent | 0.5 |
  | iFallLegDamageChance | absent | absent | 50 |
  | fJumpFallSkillBase / SkillMult / RiderMult / VelocityMin | dead, see C.J2 | | |

- **Exe.**
  - NV: function 0x8A62B0 (Actor vtable slot 0x250; the same function for Actor, Character, PlayerCharacter, Creature). Key instructions: distance read 0x8A6361 (call 0xC70550), min compare 0x8A636A and 0x8A6386, `pow` 0x8A63BA, mult 0x8A63C2, flying-creature zero 0x8A63CA-0x8A63E7, `damage > 0` test 0x8A63EF, health damage call 0x8A648D.
  - FO3: function 0x709320 (slot 0x250): distance 0x709393, min 0x7093A0, pow 0x7093C3, mult 0x7093D5, damage call 0x709458.
  - First guard (both): when 0x933840 (NV) or 0x765E10 (FO3) says the actor is in a reduced-simulation state, the routine runs only on every 64th frame (`arg3 & 0x3F == 0`).
- **Who takes it.** The player always. Other actors: not when the NPC exemptions apply (C.unclear 5), and NV additionally skips an actor with the byte at +0x18D set.
- **Difficulty** (from 0x89D6F0 → 0x8808A0 → 0x648CB0). Every health damage through that slot is multiplied by `fDiffMultHPToPC[difficulty]` when the victim is the player. It is multiplied by `fDiffMultHPByPC[difficulty]` for any other victim, whatever the attacker (0x8808A0 does not read the attacker argument). `difficulty` is the player's field `+0x7B8` (0 very easy to 4 very hard). FO3 0x6FD240 has the same shape (0x585150).
- **Today.** `FallDamage::damage` (`components/falloutscript/falldamage.cpp:29`) has exactly this formula, with the same defaults in `Settings` (`falldamage.hpp`). `fromGameSettings` reads the ESM4 values. `FalloutScripts::landed` (`apps/openfallout/mwscript/falloutinterface.cpp:296`, player only) and `fell` (`:250`, NPCs) apply it. Player damage is multiplied by `damageToPlayerMultiplier` (0.5, 0.75, 1, 1.5, 2). Matches for the player. It differs for NPCs: `fell` does not scale by the difficulty (exe: `fDiffMultHPByPC`, e.g. ×2 at very easy). It skips the player but not the creature-flies exemption. It has neither the 64th-frame guard nor the NV +0x18D and cell-flag skips. `fell` also plays the landing sound for NPCs (the exe does it for the player only).
- **Round check.** Use the table below. For a measured height lost `L`, the damage must be `0.025 × (L − 600)^1.65` (difficulty normal) within ±3% or ±1 HP, whichever is larger. The slope near 10 m is 0.82 HP per unit of height, so a 6-unit error in `L` moves the damage by ±5 HP. Check the formula against the measured `L`, not the nominal drop.

  | Drop (metres, 69.99 u/m) | Units lost | Excess | Damage (normal) | Against 200 HP (level 1, Endurance 5) |
  |---|---|---|---|---|
  | 5 m | 350.0 | none | **0** | untouched |
  | 8.57 m | 600 | 0 | 0 | threshold |
  | 10 m | 699.9 | 99.9 | **49.8** | 150 left |
  | 12 m | 839.9 | 239.9 | 211.3 | dead |
  | 15 m | 1049.9 | 449.9 | 596.4 | dead, heavy sound |
  | 20 m | 1399.8 | 799.8 | **1541** | dead |

  The deadly line for 200 HP is about 832 units (11.9 m). The unit conversion is 69.99125 game units = 1 m (the engine's own constant; the Havok unit 6.9991257 units = 0.1 m).
- **Confidence.** Exe-read for the formula and defaults (both exes), the ESM values and the difficulty chain. The 69.99 conversion: exe constant 0x10120E8 (69.99125671) / 10 = 6.9991 at 0x11C3D18.

### C.F3 What else a landing does

- **Games.** Same.
- **Rule.** After the damage, the player only gets a landing sound (heavy above 500 damage, otherwise light). Then each leg is rolled: 50% chance per leg, and each hit takes half the damage off that leg's condition. A leg at condition 0 or less counts as crippled (movement penalty, B.M7). Then the engine clears the landing flag and, in both exes, calls a "landed" helper (NV 0x8A7D50(this, 1, 1), FO3 0x6FF9E0). That helper was not decoded (it probably updates animation or state).
- **Formula.** `if player: sound = damage > fHardLandingDamageThreshold ? "FSTLandHardHeavy" : "FSTLandHardLight"`. `legLoss = damage × fFallLegDamageMult`. For each of the two legs, independently: `rand() % 100 < iFallLegDamageChance`, then `legCondition -= legLoss` through the actor-value slot (vtable +0x3AC NV / +0x3A8 FO3; actor values 0x1D = 29 left leg condition, 0x1E = 30 right leg condition).
- **Exe.** NV 0x8A64B0-0x8A65F1 (sounds at 0x8A64DC and 0x8A651D, leg mult 0x8A656A, chance 0x8A658C and 0x8A65C7, AV 0x1D at 0x8A65A4, 0x1E at 0x8A65DF). FO3 0x709466-0x709584. The damage the legs get uses the damage before the difficulty multiplier.
- **Today.** `landed` plays the sounds, rolls each leg with a mutex-guarded RNG, and removes `damage × mLegMultiplier` from AV 29 and 30 (`falloutinterface.cpp:317-345`). Matches. NPCs get the leg roll in `fell` too (the exe does it for the player only). The player's health goes through `mActorValues.damage` (difficulty-scaled). Death of the player: `mActors.kill(mPlayer)` when health ≤ 0.
- **Round check.** From a 10 m drop at normal difficulty: health 200 → 150.2 (±1.5). For each leg, over 40 drops, the number of legs hurt is Binomial(40, 0.5) per leg (accept 12 to 28). A hurt leg has condition 100 − 24.9 = 75.1 (±1) if the player's leg condition starts at 100 (assumed; check the AV table). A deadly drop must kill with the landing sound heavy if damage > 500.
- **Confidence.** Exe-read.

### C.F4 Landing in water

- **Games.** Unknown.
- **Rule.** The exe's damage function has no water test. Whether landing in water avoids damage depends on whether the controller still raises "landed" when it enters the swimming state. That was not traced (C.unclear 4).
- **Today.** `character.cpp:2248` (`inwater` → no fall damage) and `mtphysics.cpp:267` (`isUnderWater` resets the fall height). No exe basis.
- **Round check.** Not specified until C.unclear 4 is resolved by a run or more reading. Treat "no damage in deep water" as an inference only.
- **Confidence.** Inferred.

### C.W1 How much of the body is in water

- **Games.** NV read. FO3 assumed identical (C.unclear 9). TTW = NV.
- **Rule.** Each frame, the actor's depth fraction `v` is computed: the water height above the feet, divided by the body height times the size factor, capped at 1. Three lines on `v`: 0.01 = standing in water (wading, byte at actor+0x14C). 0.7 = swimming (movement flag 0x800, movement type 5; `IsSwimming` reads byte +0x14D). 0.875 = head under water.
- **Formula.**
  `v = min(1, (waterZ − pos.z) / H)` if `waterZ > pos.z`, else 0, with `H = (boundsTop.z − boundsBottom.z) × sizeFactor` (`vt+0x1DC` and `vt+0x1D8` of the actor, factor 0x567400), cached in the process (actor process +0x42C, 0x8853A0 and 0x8854B0). `waterZ` is the cell's water height (0x57B0A0, or 0x5471E0 for the other cell).
  `inWater = v >= 0.01` (byte +0x14C). `swimming = v >= 0.7` (sets movement flags `|= 0x800`, movement type 5). `headUnder = v >= 0.875`.
- **Exe.** NV 0x885560 (the fraction function; compare 1.0 at 0x8855C7). Thresholds in the actor update around 0x88991x: 0.01 at 0x889917, 0.7 at 0x889946, 0.875 at 0x889975 (constants 0x1016408, 0x104EE98, 0x1073828). Swim entry 0x88A3B0-0x88A3E7 (`or flags, 0x800`; movement type set to 5 by 0x88B0A0). Flag test 0x8846E0 (`& 0x800`) → byte +0x14D (0x884990). Script `IsSwimming` = byte +0x14D (0x5A2030, command 0x10B9). The settings named `fMoveSwim*` are not read by name.
- **Related settings** (no ESM override unless stated):

  | Setting | NV ESM | FO3 ESM | Exe default | Read? |
  |---|---|---|---|---|
  | fMoveSwimWalkBase | absent | absent | 0.5 | no code reads it by object in either exe (B.M8) |
  | fMoveSwimWalkAthleticsMult | absent | absent | 0.02 | same |
  | fMoveSwimRunBase | absent | absent | 0.5 | same |
  | fMoveSwimRunAthleticsMult | absent | absent | 0.1 | same |
  | fBuoyancyWater | absent | absent | 0 | NV 0x62B27F, FO3 0x56FB81 (physics) |
  | fSubmergedMaxWaterDistance | absent | absent | 48.0 | NV 0x62EECB, 0x630088; FO3 0x57211A |
  | fSwimRadiationDamageMult | 0.1 | 0.1 | 0.5 | NV 0x88A9A3 |
  | fWadeRadiationDamageMult | 0.1 (NV ESM) | not checked | exe-only | NV 0x88A98E |

- **Radiation in water (aside).** The actor update takes the water's radiation value × (`fWadeRadiationDamageMult` when `v < 0.7` and not swimming, else `fSwimRadiationDamageMult`) × a per-actor factor (0x8C4330) × frame time (NV 0x88A946-0x88AA30). The player only gets the rad meter effect through `vt+0x760`. Not decoded further.
- **Today.** Morrowind: `PhysicsSystem` `mSwimLevel = waterLevel - halfExtentZ × 2 × fSwimHeightScale` (`mwphysics/physicssystem.cpp:948-953`), and `World` reads `fSwimHeightScale` (`worldimp.cpp:252`, `:2039`, `:2044`). It is the same idea (swimming when the water is above a fraction of the height), but the fraction is Morrowind's setting, not the 0.7 line. It has no wading (0.01) and no head (0.875) line. Does not match.
- **Round check.** Put a standing player in water of known depth `d` above the feet (a placed water plane or an interior pool). With body height `H` measured from the actor's bounds (log it), expect `IsSwimming` true when `d/H >= 0.70` and false at 0.69. Step the depth in 2% of `H` increments and report the first true. The head-under flag is true at `d/H >= 0.875`. Tolerance ±0.03 on the fraction (the controller has to react, the actor moves, bounds are discrete). NV only until the FO3 constants are confirmed.
- **Confidence.** Exe-read (NV). Inferred (FO3).

### C.W2 Swimming speed and jump in water

- **Rule.** Jumping while swimming (movement type 5) doubles the jump height (C.J1). Swim speed uses the swim rows of the movement rules (B.M8). The `fMoveSwim*` settings have no readers by object, so the speed comes from elsewhere. Not decoded in part C.
- **Today.** Morrowind `getSwimSpeed` (`npc.cpp:1373`). Does not match.
- **Round check.** None here (movement part).
- **Confidence.** Exe-read for the jump multiplier. The speed is delegated to part B.

### C.W3 Weapons in water

- **Rule.** Not read from the exe. Online: in Fallout 3 you cannot draw or use a weapon while swimming. The only water message in the exe is `sMagicCastRangedUnderwater` ("You cannot cast ranged spells underwater"), which belongs to Oblivion-style magic.
- **Confidence.** Online only. Sources: https://www.giantbomb.com/wiki/Concepts/Swimming (FO3: a weapon cannot be drawn while swimming), https://forums.nexusmods.com/topic/193074-water-combat. See C.unclear 8.

### C.B1 Breath time and refill

- **Games.** Same in FO3 and NV. TTW = NV.
- **Rule.** The player has a breath counter. It is full (10 s plus half a second per point of Healing Rate) whenever the head is out of the water. Under water it drops by one per second. When it goes below zero, the drowning damage starts.
- **Formula.**
  `M = fActorSwimBreathBase + fActorSwimBreathMult × int(HealRate)`, with HealRate the actor value 15 (0xF) read through the owner (NV helper 0x648A10; the call at 0x8899B2).
  Per frame (NV 0x889930-0x889B75): `canBreathe` when `v <= 0` (via 0x87F620), when `0 < v < 0.875`, or when `v >= 0.875` and the actor has a water-breathing effect (`MagicTarget+0x94` vtable +0x10, or 0x87F5C0). If it can breathe, `breath = M` at once. Else `breath = breath − dt` (frame time, 0x84D030 on 0x11F6394).
- **Settings.**

  | Setting | NV ESM | FO3 ESM | Exe default |
  |---|---|---|---|
  | fActorSwimBreathBase | absent | absent | 10.0 |
  | fActorSwimBreathMult | absent | absent | 0.5 |

  Healing Rate (see E.1.8): 0 for Endurance below 6; 5 for 6, 7, 8; 10 for 9 and 10.
- **Exe.** NV: breath max 0x648942 (function; base 0x648A18, mult 0x648A27), the per-frame block 0x889930-0x889D50, HUD tile 0x77E4F0. FO3: max 0x584ECB (base 0x584FEC, mult 0x584FE6), drowning damage helper 0x585000, the call in the actor update 0x712633.
- **Only the player drowns.** The damage branch needs actor vtable +0x360 (NV) true. PlayerCharacter 0x8D0360 returns 1, the others 0x47C850 return 0. In FO3 the test is vtable +0x35C (player 0x52A5E0, others 0x8257F0). Other actors only have their breath clamped.
- **Today.** Not implemented as a rule (the HUD breath tile is a separate item; the 12BM note describes the tile). Missing.
- **Round check.** Fresh character (Endurance 5, Healing Rate 0): put the head under water (`v >= 0.875`) at time `t0`. The breath meter reaches 0 at `t0 + 10.0 s` and the first damage appears within `t0 + 10.0 s` to `t0 + 10.3 s`. Tolerance ±0.3 s (frame time, the 0.875 line crossing delay, log sampling). With Endurance 6, expect 12.5 s. With Endurance 9, expect 15.0 s. On surfacing, the counter is full again in the next frame (check `getBreath` == M within 1 frame).
- **Confidence.** Exe-read (NV, from 12BM and re-read: 0x889AB6-0x889B75). FO3 the same by 12BM and by the address list.

### C.B2 Drowning damage

- **Games.** Same. TTW = NV.
- **Rule.** When the breath is below zero, every frame the player takes `0.2 × current health × frame time`. That is 20% of what is left per second, so a big health pool does not buy much time. The sound "NPCHumanDrowning" starts if it is not playing, and the breath is held at 0.
- **Formula.** `damage = fActorSwimBreathDamage × int(Health_current) × dt` (NV 0x648A50: `fild`, then × `fActorSwimBreathDamage`, then × frame time at 0x889B44). Applied through the damage-health slot (Actor vtable +0x338, 0x89D6F0) with the same difficulty multiplier as every health damage (C.F2). `Health_current` is actor value 0x10 (16).
- **Settings.** `fActorSwimBreathDamage` 0.2 (exe default, no ESM override in NV or FO3).
- **Exe.** NV 0x889AB8-0x889C3D, 0x648A50, sound at 0x889BB8 (string "NPCHumanDrowning" at 0x10848B8). FO3 helper 0x585000, call 0x712633.
- **Results.** From 200 HP at normal difficulty: after 1 s 163.7, after 5 s 73.6, after 10 s 27.1. Below 1 HP after about 26.5 s (but see C.unclear 6: `int()` makes the damage 0 below 1 HP).
- **Today.** None. Missing.
- **Round check.** After the breath runs out, sample the player's health every 0.5 s for 5 s: `H(t) = H0 × exp(-0.2 × t)` within ±8% (frames are discrete, the `int()` truncation, difficulty 1.0). The first damage lands in the same frame the breath passes 0.
- **Confidence.** Exe-read.

### C.numbers Expected numbers for rounds

| Quantity | FO3 | NV | TTW | Tolerance | Why |
|---|---|---|---|---|---|
| Jump apex (fresh level-1, size 1.0, walking surface, not swimming) | 64.0 units (0.914 m) | 64.0 | 64.0 | ±8 units | discrete integration and sampling; size factor inferred |
| Jump apex swimming | 128 units | 128 | 128 | ±16 | same, state change |
| Fall damage at 5 m (350 u) | 0 | 0 | 0 | exact | below the 600 line |
| Fall damage at 10 m (699.9 u) | 49.8 | 49.8 | 49.8 | ±3% / ±1 HP against the measured L; ±5 HP against the nominal | slope 0.82 HP per unit |
| Fall damage at 20 m (1399.8 u) | 1541 (dead) | 1541 | 1541 | the player must die | far above any health |
| Breath time, Endurance 5 | 10.0 s | 10.0 s | 10.0 s | ±0.3 s | frame time and line crossing |
| Breath time, Endurance 6 to 8 | 12.5 s | 12.5 s | 12.5 s | ±0.3 s | Healing Rate 5 |
| Breath time, Endurance 9 and 10 | 15.0 s | 15.0 s | 15.0 s | ±0.3 s | Healing Rate 10 |
| Drowning, health after 5 s of drowning from 200 | 73.6 | 73.6 | 73.6 | ±8% | exponential, `int()` |
| Swimming line | 0.7 of body height (assumed) | 0.7 | 0.7 | ±0.03 | controller reaction |
| Head-under line | 0.875 | 0.875 | 0.875 | ±0.03 | same |

Level-1 health of 200 comes from E.1.1 (record 100 + Endurance 5 × 20).

### C.today Today's engine, in one list

| Rule | Today | Match |
|---|---|---|
| Jump height | Morrowind `Npc::getJump` (`npc.cpp:888`), placeholder Acrobatics | no |
| Fall height | sum of downward steps (`mtphysics.cpp:270`); NPCs start minus end (`falloutactors.cpp:731`) | player partly, NPCs yes |
| Fall damage formula, legs, sounds | `FallDamage::damage` (`falldamage.cpp:29`), `landed` and `fell` (`falloutinterface.cpp:250-345`) | yes (player) |
| Fall difficulty for NPC victims | not applied | no (exe: `fDiffMultHPByPC`) |
| Fall damage in water | suppressed (`character.cpp:2248`, `mtphysics.cpp:267`) | unknown (C.unclear 4) |
| Swim line | Morrowind `fSwimHeightScale` (`physicssystem.cpp:948`, `worldimp.cpp:252`) | no |
| Breath and drowning | none | missing |
| Dead jump settings | none | n/a |

### C.unclear Open items in part C

1. Size factor of the fresh player. The jump multiplies by the ref scale (float at ref+0x3C) and by the NPC record's raw height (NPC_ +0x1F4). The Player record (NPC_ 0x7) has `NAM6` height = 0.0 in both ESMs, and the race (Caucasian, 0x19) has height 1.0. The exe's `TESNPC::GetHeight` (NV 0x604250) substitutes the race height for the player, but the jump reads the raw field. The player cannot be jumping at 0, so the runtime value must be set (inferred 1.0) somewhere that was not found. The expected apex assumes 1.0.
2. World gravity magnitude and air time. The launch speed uses `|world gravity| × 32.0` (the controller's gravity multiplier is 32.0 in both exes), but the world gravity vector was not read, so the time in the air is not given. Only the apex is specified.
3. Input gating of a jump (sneaking, over-encumbered, crippled legs). The jump functions (NV 0x884AA0 and 0x930640; FO3 0x7668B0) read none of these. The caller that decides when a jump starts (NV movement update near 0x9421D2, tests controller flag 0x400) was not traced to the input layer. Whether the player's input code blocks jumping when over-encumbered is not known. Over-encumbered certainly blocks running (B.M5).
4. Landing in water. The damage routine has no water test. Whether the controller raises the "landed" flag when the character enters water was not traced. Today's code suppresses fall damage in water (not confirmed by the exe).
5. The NV-only non-player skip (byte at actor+0x18D, read by NV 0x566950, about 100 callers) and the cell and worldspace exemption for non-player humans (NV 0x575D70 + 0x586320: skip when a flag 0x40 is set on the thing the cell points to; FO3 0x4E8880 + 0x501190). Their meaning is unknown. Neither touches the player.
6. Drowning end. Damage is `0.2 × int(Health)`. `int()` makes it 0 when current Health is below 1. Whether Health can then still reach the death line was not read (the death test is after `vt+0x22C` in 0x89D6F0, not decoded).
7. Meaning of the air-control scalar. NV 0xCD4095 and FO3 0x96B6FE: air factor = `0.3 × [ctrl+0x534] + 0`, and `ctrl+0x534` is loaded from the controller info that is filled with `fJumpFallHeightMin` (600). So the factor is 180 (game units per second?). What it multiplies (a speed cap or an acceleration) was not decoded.
8. Weapons in water. Not read from the exe. Online only (FO3: weapon cannot be drawn while swimming; no confirmation for NV).
9. The FO3 submerged-fraction function and its 0.01, 0.7 and 0.875 constants were not located. They are assumed identical to NV (12BM said the breath rule is the same).
10. Walk-off start height when the actor is at world height exactly 0.0. A stored start height of 0.0 means "none" in the distance function (`if start == 0 then 0`). Inferred from the code. A real fall that starts at z = 0.0 exactly would hurt nothing.

---

## D. Death, condition and settings

### D.0 Summary

**Dying.** An actor dies when its current health falls below 1.0 (not "≤ 0"). The check runs after any change of health. A fatal blow sends the actor to the "dying" state, then "dead".

**Player death follows the exe** (owner decision Q28). The player's death is not shown with a box by default. Five real seconds after the player is first seen dying or dead, the exe reloads the most recent save. It goes to the main menu when there is no save. The message box with `Reload` and `Main Menu` exists in the exe, but it runs only when `fPlayerDeathReloadTime` is 0 or below. Both ESMs leave it at the exe default 5. So today's Lua flow (`falloutdeath.lua`) differs: it shows the box at 5 s. This reverses the current box behaviour. The spec read the branch from the code, not from a run (see D.unclear 1).

**Essential actors and companions.** The INI switch `bEssentialTakeNoDamage` (default 1, `[GamePlay]`) makes essential actors fall unconscious instead of dying.
- An actor is essential if its base record says so, or a script set it, or (NV) it is a companion and Hardcore mode is off.
- It lies down in "essential down" state (life state 6) for `fEssentialDeathTime` = 10 s, then gets up.
- A hit that would kill an actor that is already unconscious or restrained instead sets its health to 30% of its maximum (`fEssentialHealthPercentReGain` = 0.3).
- What health the actor has when it gets up is disputed (D.unclear 2 and [the disagreements](#where-the-spec-parts-disagree)).

**Knock-down.** A hit that does not kill can knock the target down. NV and FO3 use the same formula, in two steps: a big hit always does it. A medium hit rolls a chance. Knock-back force is a separate formula.

**Limbs.** Limb condition is actor values 25 to 31. A limb is crippled when its condition is 0 or below.
- Legs: one crippled leg multiplies walking and running speed by 0.85, two by 0.75.
- Arms: crippled arms add aim spread (NV: +0.2 or +0.4 for one arm and a one-hand or two-hand weapon, +0.4 or +0.6 for both arms). FO3 has a different, smoother arm and head spread term.
- Head: NV has no spread term. The head effect (`Perception −4`) is a spell in the ESM, `HeadCrippleEffect`, which the exe does not name. How it is applied is D.unclear 4.
- The player takes half the limb damage (`fCombatPlayerLimbDamageMult` 0.5, both games).

**Regeneration outside Hardcore.**
- Health: `healRate / 3600` per simulated second, where `healRate` comes from Endurance (6 to 8 give 1, 9 and 10 give 2 in the exe; 5 and 10 in the ESMs). Sleeping uses a different formula.
- AP: NV `fActionPointsRestoreRate` × max AP per second (0.06 in the NV ESM). FO3 the rate (4.0 in the FO3 ESM) as absolute AP per second. Both only while V.A.T.S. is closed.
- Today the engine does the AP part per frame. It does not regenerate health per frame in play (only on wait, travel and rest). It has no sleeping formula.

**The Morrowind settings the Fallout code still reads:**

| Morrowind setting | Fallout replacement | Value |
|---|---|---|
| `iMaxActivateDist` (192) | `iActivatePickLength` (exe default; no ESM value; the INI can override) | 150 |
| `iMaxInfoDist` | none in the exe: nothing measures a talk distance while a conversation is open | n/a |
| `fSwimHeightScale` | no setting of that kind in the exe (D.unclear 5) | n/a |
| six `fAudio*` | none in the exe. Each SOUN record carries its own min and max attenuation | per record |
| `iMonthsToRespawn` | `iHoursToRespawnCell` | 72 hours |
| `sDefaultCellname` | `sDefaultCellName` | "Wilderness" |

**Defaults that plugins do not carry.** `gameSettingValues()` reads only the ESM4 GMST records. Numeric exe defaults are not merged anywhere. Every caller passes a hand-copied fallback literal: 44 `gameSetting("name", fallback)` call sites. `exesettings.cpp` merges only the default texts of string settings (Pip-Boy words). See D.S7.

A function address is "NV 0x… / FO3 0x…". NV and FO3 are different binaries. TTW uses the NV rules.

### D.D1 Death trigger (health below one point)

- **Games.** Same in NV and FO3 (TTW: NV).
- **Rule.** After any change of an actor's health, if the current value is below 1.0, the actor is killed.
- **Formula.**
  ```
  if (lifeState is alive-ish) and currentHealth < 1.0:  Kill(killer, damage)
  ```
  The life state is read through vtable slot 0x22C (the dead check). The hit that causes it can come from a script `Kill`, a fall, drowning, or damage.
- **Settings.** None (a literal 1.0).
- **Exe.** NV function 0x898650 (called from 0x9508F0): reads current health (AV 0x10, owner slot +0xC) and maximum (slot +4). Death needs `fcomp [0x1012070]` (1.0) to give "below". Key instruction: `89877B fld [ebp-0x14]; fcomp qword [0x1012070]; test ah,5; jp 0x8987E9`. Then either the immediate kill `call 0x89D900` (NV; same call chain) or an event (when 0x8C7AA0() is true).
  - The same function sets a mood flag when the ratio current/maximum is under two thresholds (0x8D5CB0(this, 5) or (…, 4)). Not read.
  - FO3: the matching function is the death function 0x71AC50. The caller that tests health was not matched.
- **Today.** `falloutinterface.cpp:289, 344` and `:140`: death when `current(health) <= 0`. A health between 0 and 1 (for example 0.4 from a fall) keeps the player alive in OpenFallout but dies in the exe. Differs slightly.
- **Round check.** Set health to 0.5 with the probe and tick one frame. Expect death (`pipboy.playerDead`). Tolerance: none (a boolean).
- **Confidence.** Exe-read (NV). FO3 inferred from the identical structure of the settings and the death function.

### D.D2 The death-to-reload sequence

- **Games.** Same structure in NV (0x93E840, the player's per-frame update) and FO3 (0x788350). Default `fPlayerDeathReloadTime` = 5.0 in both exes. Neither ESM carries the setting.
- **Rule.** While the player's life state is 1 (dying) or 2 (dead), a timer runs. It starts at the first such frame, hides the HUD, and runs down in real frame time. When it passes zero, the exe loads the most recent save. If there is none, or the load cannot start, it goes to the main menu. The timer is reset to -1 when the player is alive again. If `fPlayerDeathReloadTime` ≤ 0, the exe instead waits for life state 2, shows a two-button message box and does nothing until the player answers.
- **Formula (default branch).**
  ```
  if fPlayerDeathReloadTime > 0:
      if lifeState in {1,2}:
          if timer < 0: { hideHUD(); timer = fPlayerDeathReloadTime }   // 0x709C40(0)
          timer -= dt
          if timer < 0:
              timer = -1
              if !reloadMostRecent(): goMainMenu()    // 0x8512F0, 0x7D0A70
      else: timer = -1
  else:   // box branch
      if lifeState == 2 and not boxShown:
          closeMenus(); hideHUD()
          if saveExists: showBox(sMiscPlayerDeadMessage, [sMiscPlayerDeadLoadOption, sMiscPlayerDeadMenuOption], callback)
          else goMainMenu()
          boxShown = true
  ```
  The box callback (0x961D50): button 1 → 0x8512F0 (reload). Button 2 → 0x7D0A70 (main menu).
- **Settings.** `fPlayerDeathReloadTime` = 5.0 (exe default; not in either ESM). `dt` is the frame time passed to the player update (`[ebx+8]`). The spec takes it as real seconds, as 12BQ measured +5.02 s. Texts (exe defaults): `sMiscPlayerDeadMessage` = "Reloading the most recent save game", `sMiscPlayerDeadLoadOption` = "Reload", `sMiscPlayerDeadMenuOption` = "Main Menu".
- **Exe.**
  - NV 0x93E840: the check at 0x93FEE9 `fld [eax]; fcomp qword [0x1012060] (=0.0); test ah,0x41; jne 0x93FF9D`. The `jne` is taken when the setting is ≤ 0, which leads to the box code at 0x93FF9D. The default path is 0x93FEFC to 0x93FF98. State test: `call 0x4F8960; cmp eax,2 / cmp eax,1` (life state is `[this+0x108]`). Timer variable `[0x11A3B34]`, start -1.0 (0x1012054). Reload helper 0x8512F0 (calls the load routine 0x850760 with the name at `[manager+0x14]`; returns 0 when that name is empty). Main menu 0x7D0A70 (loads `Data\Music\Special\MainTitle.mp3`). Box flag `[0x11E07C0]`.
  - FO3 0x788350: reload setting read at 0x78B196 (`fcom [0xF5EC14]`), timer `[0xF6EA20]`, timer set at 0x78B1E3, 0x78B217 end of timer. On timer end FO3 calls 0x6D5960 (reload) and, when it returns false, sets a flag to go to the main menu (0x78B23C). Box branch 0x78B242.
- **Today.** `falloutdeath.lua:50-61`: counts `core.getRealFrameDuration()` while `pipboy.playerDead()`. At `fPlayerDeathReloadTime` (default 5) it calls `show()`: no save → `openMainMenu`; save → a box with Reload and Main Menu. `pipboybindings.cpp:839` defines dead. It differs on the box: the exe default reloads without a box. It also hides the HUD while dead (`falloutstatus.lua:522`), which matches.
- **Round check (default).** From the death frame to the load of the most recent save (log line "loaded", or the new quest-stage state): expect 5.0 s. Tolerance ±0.6 s (the first frame at which the player is seen dying can be up to 2 s after the last damage, since the dying state lasts `fDyingTimer` = 2 s before dead; the timer starts at the first frame in state 1 or 2). With no save: main menu at 5.0 s ±0.6.
- **Round check (box branch).** Only if a run sets `fPlayerDeathReloadTime` ≤ 0 in a plugin: the box appears at the first frame of state 2. Not recommended for the standard round.
- **Confidence.** Exe-read for NV and FO3. The order of the `jne` is the one thing that disagrees with the earlier note (see D.unclear 1). 12BQ's "+5.02 s" matches the 5-second number either way.

### D.D3 Dying timer and the corpse

- **Games.** `fDyingTimer`: NV exe default 2.0, no ESM value. FO3 has the same setting (2.0), read at 0x71B8AA. `fKillCameraTime` is NV only (3.5, 0x89F400).
- **Rule.** When an actor is killed, the exe stores the dying time in the actor's process, with the value `fDyingTimer` (2 s). That is the time in state 1 before state 2. When the killer is the player or a teammate, outside V.A.T.S. and with no V.A.T.S. targets left, NV also starts the kill camera for `fKillCameraTime` = 3.5 s (0x93E530). The killer test uses 0x566950 (is teammate). 0x11E0A30 (`bTrackProgress`, default 0) is an unrelated debug flag.
- **Formula.** State 1 lasts `fDyingTimer`. Then state 2.
- **Exe.** NV Kill function 0x89D900 (the 0x89D8B0 label used earlier is a small wrapper). The dying time at 0x89EDB6 `mov ecx, 0x11CCF7C; call 0x403E20; … call [edx+0x4B4]`. The kill camera at 0x89F3FB: `push -1; push 1; (fKillCameraTime); call 0x93E530`. FO3 Kill 0x71AC50, dying time at 0x71B8AA.
- **Today.** No dying phase for the player: dead at once (`ActorState::kill`, `actors.cpp:476`). The Lua box timer starts at the first dead frame. No kill camera. Not implemented (visual only).
- **Round check.** None needed (not observable without the camera).
- **Confidence.** Exe-read for the setting reads. The meaning of the process slot +0x4B4 is inferred from the setting name.

### D.D4 Essential actors and non-Hardcore companions (knock-out)

- **Games.** NV: companions count as essential while Hardcore is off. FO3: no Hardcore; essential only by record or script. Both: the switch `bEssentialTakeNoDamage` (INI `[GamePlay]`, default 1).
- **Rule.** When an essential actor would die or begin dying, it goes into life state 6 ("essential down", shown as unconscious) instead. It stays down for `fEssentialDeathTime` = 10 s. A new fatal blow on one that is already unconscious (3) or restrained (5) resets health to 30% of maximum and does not put it down again.
- **Formula.**
  ```
  essential(actor) =
      (player exists and actor is a teammate and not Hardcore)    // NV only
      or baseData.flags & Essential   // or a template's base data
      or scriptEssentialFlag(actor)     // SetEssential / SetActorRefEssential
  SetLifeState(new): if bEssentialTakeNoDamage and essential and new in {1,2}: new = 6
                     if new == 6 and not canBeDown(actor): new = 0
  Kill(essential, state in {3,5}):  health += 0.3*maxHealth - current     // fEssentialHealthPercentReGain
  Kill(essential, other):           startDownTimer(fEssentialDeathTime); SetLifeState(6)
                                    ReviveStats()    // NV 0x8A0960, see D.unclear 2
  frame (life state 6, process mode 3 or 1): tick the timer;
      for a teammate: tick only when the player is not in the state tested by 0x953C50
      when timer <= 0: SetLifeState(0); ReviveStats(); event 0xB
  ```
- **Settings.** `fEssentialDeathTime` 10.0, `fEssentialHealthPercentReGain` 0.3 (exe defaults, both games; neither ESM has them). `bEssentialTakeNoDamage` 1. Message: `sEssentialCharacterDown` = " is unconscious." (shown as "<name> is unconscious." with the icon `glow_message_vaultboy_surprised`).
- **Exe.**
  - NV `IsEssential` 0x87F3D0 (teammate branch at 0x87F41B…0x87F439; the Hardcore test is 0x4D1360(player)). `SetLifeState` 0x8A1800 (essential conversion at 0x8A1828…0x8A1854; INI byte `[0x11E0888]`). Kill essential branch 0x89DE32…0x89E0CA (regain at 0x89DE9C, down time at 0x89DF50, state 6 at 0x89E057). Wake-up in the actor update 0x888B50 (state 6 check at 0x889070; `SetLifeState(0)` at 0x889141 and 0x8891B1). `ReviveStats` 0x8A0960.
  - FO3 Kill 0x71AC50 (regain at 0x71B032, down time at 0x71AF1A, state 6 at 0x71AFA8, message at 0x71AFCC). The companion rule is NV only: FO3 has no Hardcore.
- **Today.**
  - `falloutneeds.cpp:85-92` `essentialByMode` is the NV companion rule (matches).
  - `falloutscripts.cpp:2689` `essentialByRecord`, `combatcommands.cpp:134` `isEssential` (matches).
  - `falloutscripts.cpp:3214-3239` `fallsInsteadOfDying`: already unconscious or restrained → heal to 30%; else `setUnconscious(10 s)` with `fEssentialDeathTime` (matches).
  - `falloutscripts.cpp:4039-4056`: on waking, a character with 0 health gets 30% (the code comment says "what the games do for one that gets up is not known"). Part D reads the games as healing in full on both lying down and getting up. Differs, medium confidence. Part G reads it differently (see [the disagreements](#where-the-spec-parts-disagree)).
  - Not in OpenFallout: the `bEssentialTakeNoDamage` switch, the "is unconscious" message and icon, and the teammate-in-combat pause of the timer.
- **Round check.** Shoot an essential NPC (for example Doc Mitchell in NV) to 0 health. Expect `GetUnconscious` true at the next frame, and standing again after 10.0 s ±1.0 s (frame granularity and the paused teammate tick). Expect health on getting up: full (part D's reading) or 30% (today). Record both and compare. Second check: hit a downed essential, and health is set to 30% ±1 point of maximum.
- **Confidence.** Exe-read for the setting reads, conversion, timers and the 30% rule. Inferred for the full-health claim (D.unclear 2) and for the teammate tick condition (the callee 0x953C50 is not named).

### D.D5 Knock-down on a non-fatal hit

- **Games.** Same formula. NV 0x646400, FO3 0x5827F0. The caller is NV 0x9B00A0 (the hit-damage function; 0x9B0820), FO3 0x7BF200 (0x7BF6B4).
- **Rule.** A hit that leaves the target alive knocks it down if it is big enough (75% of maximum health), or if it is over a share of current health and a roll succeeds.
- **Formula.**
  ```
  cur = currentHealth; if cur <= 0 or cur - damage <= 0: no knock-down
  if damage/maxHealth*100 > fKnockdownBaseHealthThreshold (75): knock down
  else if damage/cur*100 > fKnockdownCurrentHealthThreshold (25 in this spec; 50 in part A):
        agil = Agility * 10
        p = (fKnockdownDamageBase + fKnockdownDamageMult*damage) / (fKnockdownAgilBase + fKnockdownAgilMult*agil)
        p = max(p, fKnockdownChance)
        knock down if rand()%1000 < trunc(p*1000)
  ```
- **Settings** (exe defaults, "no ESM value in either game" in this spec): `fKnockdownBaseHealthThreshold` 75, `fKnockdownCurrentHealthThreshold` 25, `fKnockdownChance` 0.25, `fKnockdownDamageBase` 0, `fKnockdownDamageMult` 0.3, `fKnockdownAgilBase` 0, `fKnockdownAgilMult` 1.0. FO3 and NV have the same defaults. Both use the constants 100, 10 and 1000.

  **Check against the data.** The dumps `tmp-death/gmst_nv.txt` (line 487), `tmp-death/gmst_fo3.txt` (line 368) and `tmp-actors/gm_all.txt` (line 83) all show `fKnockdownCurrentHealthThreshold` = 50.0 in both ESMs. So the value that runs is 50, as part A says. Part D's 25 is the exe default. The ESM value wins (see [the disagreements](#where-the-spec-parts-disagree)).
- **Exe.** NV 0x646400 (comparison of `damage/max*100` at 0x646469; threshold 0x64647A; second at 0x6464AB; agility term 0x6464C7; roll 0x646556…0x646574). FO3 0x5827F0 (thresholds at 0x582873 and 0x5828A6; roll 0x582921…0x582953).
- **Today.** `components/falloutscript/hitreactions.cpp:53-60` reads the same settings. The formula body was not read; the names match.
- **Round check (as written in part D, which uses 25).** A 40-damage hit on a 100-HP actor (above 25% of current, below 75% of maximum) with Agility 5: p = (0 + 0.3×40) / (0 + 1×50) = 0.24, floored to 0.25, so a 25% chance. Run 200 trials: expect 50 ±15 (3 sigma ≈ 19; use ±20). A 76-damage hit on 100 max HP: always. With the ESM value 50, a 40-damage hit on a 100-HP actor is 40% of current health, which is below 50, so it would not roll at all. The round must be checked with the value that runs.
- **Confidence.** Exe-read.

### D.D6 Knock-back force

- **Rule.** A hit pushes the target with a force that falls as the target's Agility rises.
- **Formula.** `force = (fKnockbackAgilBase + fKnockbackAgilMult * Agility*10) * (fKnockbackDamageBase + fKnockbackDamageMult * damage)`, clamped to `fKnockbackForceMax`. The knock-back lasts `fKnockbackTime`.
- **Settings** (exe defaults, no ESM value): `fKnockbackAgilBase` 1.0, `fKnockbackAgilMult` −0.008, `fKnockbackDamageBase` 50, `fKnockbackDamageMult` 10, `fKnockbackForceMax` 512, `fKnockbackTime` 1.0. NV's own ESM has none of them. A test file overrides `fKnockbackForceMax` to 300; the real value is 512.
- **Exe.** NV 0x646580 (formula). Callers 0x89A760 (0x89C91C force max; 0x89C9A1 time) and 0x5D6B60. FO3 0x582980. Callers 0x71C930 (0x71D70F force max; 0x71D78A time) and 0x52D040.
- **Today.** `hitreactions.cpp:60-65` reads the settings.
- **Round check.** Agility 5, damage 20: force = (1 − 0.4) × (50 + 200) = 150. Tolerance ±5%. Not observable without physics numbers, so skip it as a round check.
- **Confidence.** Exe-read (the clamp site was seen but not read in detail).

### D.L1 Limb condition: thresholds and damage

- **Games.** Same (NV and FO3).
- **Rule.** The player has seven conditions, all actor values: AV 25 head (Perception condition), 26 torso (Endurance condition), 27 left arm (left attack condition), 28 right arm (right attack condition), 29 left leg (left mobility condition), 30 right leg (right mobility condition), 31 brain. Which is which comes from the layout of the AV names and the code below (29 and 30 are the legs in the speed function, 27 and 28 are the arms in the spread function, 25 is the head). A limb is crippled when its condition is 0 or below. A hit takes `damage × limbShare` off the struck limb (the share function is NV 0x647A30). The player takes half of that damage.
- **Formula.** `limbDamage = share(victim, limb, damage); if victim is the player: limbDamage *= fCombatPlayerLimbDamageMult (0.5)`. Crippled: the condition helper NV 0x646800 returns 1.0 when the condition is above 0, else 0.0. It applies to AV 25 to 31 only.
- **Settings.** `fCombatPlayerLimbDamageMult` 0.5 (exe and both ESMs). Messages: "Your left leg is now crippled." etc. (`sPartCrippledByPlayerMessage`, `sHeadDamagedMessage`).
- **Exe.** NV 0x646800 (range 0x19..0x1F at 0x646811…0x64681B; result 1/0 at 0x64685F…0x646863). Player half-damage NV 0x9B6F6D (inside 0x9B6620). FO3 0x7C3770 (0x7C3E7D).
- **Today.** `falloutscripts.cpp:3054-3056` (half damage, matches), `falloutneeds.cpp:103-107` and `falloutinterface.cpp:380-389` (crippled = current ≤ 0, matches).
- **Round check.** Shoot the player's arm with a known damage D from a non-player weapon. The limb condition falls by `share × D × 0.5`. Compare with the limb share table (another area).
- **Confidence.** Exe-read for the range, the crippled test and the 0.5. AV names inferred (they match the inventory table `actorvalues.hpp:42-43`).

### D.L2 Legs: movement

- **Rule.** One crippled leg slows walking and running to 85%, two to 75%.
- **Formula.** `crippled = (1 − ok(AV29)) + (1 − ok(AV30))`. `mult = 0.85 if crippled == 1; 0.75 if crippled == 2; else 1`. The result is one factor of the move-speed product, together with the armour, two-hand weapon and encumbrance factors (B.M1).
- **Settings.** `fMoveOneCrippledLegSpeedMult` 0.85, `fMoveTwoCrippledLegsSpeedMult` 0.75 (both ESMs and exes). `fMoveLegConditionMult` 0.9 is read nowhere (init only, NV and FO3).
- **Exe.** NV 0x647D10 (legs 0x647DCE…0x647E3F). FO3 0x5845B0 (reads at 0x584619 and 0x584630).
- **Today.** `falloutinterface.cpp:221-248` and `falloutneeds.cpp:242` (`legSpeedMultiplier`) match. The player's actual speed still comes from the Morrowind formula; only this factor is Fallout.
- **Round check.** With one leg crippled, run speed ratio to the healthy run is 0.85 ±0.02. Two legs: 0.75 ±0.02.
- **Confidence.** Exe-read.

### D.L3 Arms and head: aim spread

- **Games.** Differs. NV: arms only, flat additions. FO3: a smoother term for arms and head.
- **Rule (NV).** The spread of a shot is a sum of flat penalties: standing, walking or running, unaimed, then the crippled arm penalty, then a skill and condition term. It is multiplied by a wobble factor and floored at `fMinGunSpreadValue`.
- **Formula (NV, arms part, 0x646A54…0x646B2E).**
  ```
  both arms crippled: spread += two-handed ? fCrippledArms2HSpreadPenalty : fCrippledArms1HSpreadPenalty
  one arm crippled:   spread += two-handed ? fCrippledArm2HSpreadPenalty  : fCrippledArm1HSpreadPenalty
  (not applied when the actor has the "ignore crippled limbs" flag)
  ```
  FO3 (0x582C90): `spread += fGunSpreadArmBase + fGunSpreadArmMult × armFactor` and `fGunSpreadHeadBase + fGunSpreadHeadMult × clamp(headCondition)`, with Base 1.0 and Mult −1.0 for both (0 when healthy, +1 when crippled). The shape of `clamp` and `armFactor` is inferred, not read.
- **Settings (NV exe default; neither ESM has them).** `fCrippledArm1HSpreadPenalty` 0.2, `fCrippledArm2HSpreadPenalty` 0.4, `fCrippledArms1HSpreadPenalty` 0.4, `fCrippledArms2HSpreadPenalty` 0.6. FO3 exe default: ArmBase 1.0, ArmMult −1.0, HeadBase 1.0, HeadMult −1.0. NV keeps `fGunSpreadHeadBase` and `fGunSpreadHeadMult` (0, −1.0), but the NV function reads the head condition and does not use it.
- **Exe.** NV 0x646910 (callers 0x8B0F99 and 0x8B0FD7). FO3 0x582C90 (caller 0x701E5C). V.A.T.S. uses its own arm term, already in OpenFallout (`falloutvats.cpp:251-255`, `vats.cpp:39-42`).
- **Today.** Only V.A.T.S. uses the arm penalties (`vats.cpp`). Real-time weapon spread is not in this part's files. Whether the real-time path reads the arm penalties was not checked.
- **Round check.** NV: compute the spread for a pistol with both arms crippled and with healthy arms. The difference must be exactly 0.4 (before the wobble multiplier). Tolerance ±0.01.
- **Confidence.** Exe-read for NV. FO3 inferred in shape.

### D.L4 Head: vision and perception

- **Rule.** The head is crippled when AV 25 is 0 or below. The game's message is "Your head is now damaged." What the player feels is Perception −4 (spell `HeadCrippleEffect`, effect "Reduced Perception", magnitude 4 on actor value 6 = Perception). A concussion (`PlayerConcussed`, magnitude 4 also; archetype 33, effect "Concussion") runs the image effect `ImageSpaceConcussion` / `FXCrippleHead` and a pain reaction (`fGetHitPainMult` 1.5) when the concussion update fires, every `fConcussionTimer` = 40 s while it lasts (ESM value in both games; the exe default is 15).
- **Settings and ids.** `fConcussionTimer` 40 (both ESMs). `fGetHitPainMult` 1.5 (exe). Spell form ids 0x67572 (`HeadCrippleEffect`) and 0x6533A (`PlayerConcussed`). Magic effects 0x65339 and 0x32DE3 (`ReducePerception`). Image space 0x164. The ids are the same in NV and FO3.
- **Exe.** NV 0x80BE40 (concussion update; `FXCrippleHead` at 0x80BF20). FO3 0x6A6650 reads `fConcussionTimer`. No immediate reference to the two spell ids (see D.unclear 4).
- **Today.** Nothing applies `HeadCrippleEffect` (a grep found only `GetConcussed`, `falloutdamage.cpp:179`). Not implemented.
- **Round check.** None yet. After implementation: a crippled head makes the `Perception` current value drop by 4. Tolerance 0.
- **Confidence.** The ESM data is confirmed by the exe. The trigger is unclear.

### D.R1 Health regeneration outside Hardcore

- **Games.** Same structure in NV and FO3. The Endurance bonus differs only through the data (the ESMs override the exe defaults).
- **Rule.** The player's health goes up by a small amount each simulated second, from the heal rate (an actor value derived from Endurance). Sleeping uses a different, larger formula. Regeneration only heals an actor that is damaged (0x88B740).
- **Formula.**
  ```
  healRate = bonus(Endurance)          // Endurance 6,7,8,9,10 → fAVDHealRateEndurance6..10Bonus; else 0 (NV 0x643A70)
  perSecond = healRate / 3600
  if sleeping: perSecond = perSecond * fHealingRateSleepingMult + fHealingRateSleepingBase / 3600     // NV 0x648B00
  gain = perSecond*dt + perkTerm*dt     // perkTerm: the entry point 0xC (heal-rate perks), NV 0x88B510
  health += gain      // only if the process flag 0x20000000 is set (D.unclear 10)
  ```
  The sleeping flag is `[actor+0x1AC] == 9` (NV 0x579670).
- **Settings.**
  - `fAVDHealRateEndurance6Bonus` … `10Bonus`: exe defaults 1, 1, 1, 2, 2 (NV and FO3). Both ESMs override them to 5, 5, 5, 10, 10. The values the games run with are those of the ESMs.
  - `fHealingRateSleepingBase` 2.0, `fHealingRateSleepingMult` 2.0 (exe, both games; ESM none).
  - `dt` is the simulated second (D.unclear 3).
- **Exe.**
  - NV: bonus table 0x643A70 (switch on Endurance − 6 at 0x643A8B). Sleeping 0x648B00. Apply 0x88B510 (health regen; 0x88B531 `call 0x648B00`; perk entry point 0xC via 0x5E58F0(0xC, player, …) at 0x88B567; heal at 0x88B58D `call 0x88B740(health)`). The caller 0x8C3C40 (0x8C3ED8 `call 0x88B510`, guarded by the process flag test at 0x8C3EB7).
  - FO3: bonus table 0x581620. Sleeping 0x5850B0.
  - The hour-length input: 0x8C3CB8…0x8C3CBD: `dt = 3600 / x` while waiting.
- **Today.** `derivedstats.cpp:96-98` `regeneration(healRate, seconds) = healRate/3600*seconds`, called only by wait and rest (`falloutinteraction.cpp:1501-1505`) and travel (`:1319-1329`). No per-frame regeneration in play, no sleeping formula, no perk term. `fHealingRateSleeping*` is read nowhere.
- **Round check.** With Endurance 9 in NV (heal rate 10): standing still for 600 s of the simulation clock, health rises by `10/3600*600` = 1.67 HP ±0.2. That is slow. A round that waits 24 hours at timescale 30 (24×120 = 2880 s) expects +8.0 HP ±0.5. Tolerance: ±10% of the gain plus 0.2 HP of rounding. FO3 and NV differ only through the ESM bonuses.
- **Confidence.** Exe-read for the formula and apply path. The `dt` unit is unclear (D.unclear 3). The perk term is exe-read but not detailed.

### D.R2 Action-point regeneration

- **Games.** Differs. NV: a share of the maximum AP per second. FO3: absolute AP per second.
- **Rule.** The player's AP refill continuously while V.A.T.S. is not open.
- **Formula.**
  ```
  NV:  AP += fActionPointsRestoreRate * dt * perkMult * maxAP     // NV 0x88B660
  FO3: AP += fActionPointsRestoreRate * dt                        // FO3 0x6FBF40
  conditions: the actor is one that uses AP (vtable +0x360 in NV, +0x35C in FO3), and V.A.T.S. mode == 0 ([0x11F2250] in NV, [0x108D0A0] in FO3)
  ```
  `perkMult` is the entry point 0x27 (0x5E58F0(0x27, actor, held weapon, &x)), start 1.0 (the held weapon is read from `[process+0x148]`).
- **Settings.** `fActionPointsRestoreRate`: exe default 1.0 in both games. NV ESM 0.06. FO3 ESM 4.0. Max AP is a derived stat (`fAVDActionPointsBase` 65 + `fAVDActionPointsMult` × Agility: NV 3.0, FO3 2.0; see E.1.2).
- **Exe.** NV getter 0x66DDA0, user 0x88B660 (0x88B696), caller 0x8C3C40 (0x8C3EA9). FO3 getter 0x5A1980, user 0x6FBF40 (0x6FBF5A).
- **Today.** `falloutscripts.cpp:1031-1048` runs it per frame while `!mVatsOpen`, with the perk entry point `actionPointRegen` when a weapon is held. `vats.cpp:201-210` has the two forms. Matches (NV share, FO3 absolute). The "uses AP" actor test was not checked (player: fine).
- **Round check.** NV: empty the AP and measure the time to refill: `1/0.06` = 16.7 s from empty to full, whatever the maximum (no perk). Tolerance ±8% (frame steps and the 0.5 s sampling). FO3: `maxAP / 4` (maxAP = 65 + 2×Agility; Agility 5 → 75 → 18.75 s). Tolerance ±8%.
- **Confidence.** Exe-read.

### D.S1 Activation and pick reach (replaces `iMaxActivateDist`)

- **Games.** Same exe default in NV and FO3.
- **Rule.** The pick ray that decides what the player can activate is `iActivatePickLength` long, measured from the camera. The default is 150 game units (the exe default; no ESM value; the INI can override). With a melee weapon drawn, the melee reach settings (`fObjectHitH2HReach`, `fObjectHitOneHandReach`, …) take its place for the attack pick (NV 0x8AE73B…0x8AE7A2).
- **Settings.** `iActivatePickLength` 150 (NV `push 150` at init, FO3 table 150). Telekinesis or extra range: none found.
- **Exe.** NV 0x86F6A0 (the setter at 0x86F7D3 `mov ecx, 0x11D10D4; … call 0x7034C0(value)`; 0x7034C0 → the camera's pick-length setter 0x984F60). 0x8C94E0 (0x8C99FE). 0x8AE660 (0x8AE73B). FO3 0x6E4510 (0x6E4604), 0x715D80 (0x715E2C), 0x7255E0 (0x7259DE).
- **Today.** `worldimp.cpp:860-867` `getMaxActivationDistance` = `iMaxActivateDist` (192, a Morrowind setting) unless `--activate-dist`. Differs: 192 against 150.
- **Round check.** Place an activator at distance d from the eye. It activates at d = 140 and not at d = 160. Tolerance ±5 units. Third person: D.unclear 6.
- **Confidence.** Exe-read for the value and the setter. The camera origin is inferred.

### D.S2 Talk distance (replaces `iMaxInfoDist`)

- **Rule.** The exe has no setting that limits how far the player can be from the person being talked to once dialogue is open. The dialogue menu fixes the target. Starting a conversation is an ordinary activation (D.S1). `fDistancetoPlayerforConversations` (3000, both exes) is read nowhere (init only in NV and FO3). `iSpeakSoundLipDistance` (750) limits lip sync.
- **Today.** `worldimp.cpp:875-885` uses `iMaxInfoDist` as the focus distance in GUI mode. In Fallout a Lua dialogue menu is open in that mode, so this only matters for what is focused behind it.
- **Round check.** None.
- **Confidence.** Exe-read (the negative: no user of the setting).

### D.S3 Swimming settings (replaces `fSwimHeightScale`)

- **Rule.** No Fallout setting does the job of `fSwimHeightScale`. The swim and breath settings that exist:
  - `fActorSwimBreathBase` 10, `fActorSwimBreathMult` 0.5, `fActorSwimBreathDamage` 0.2 (NV 0x6482F0, 0x648A50; FO3 0x584D20);
  - `fMoveSwimWalkBase` 0.5, `fMoveSwimRunBase` 0.5, `fMoveSwimWalkAthleticsMult` 0.02, `fMoveSwimRunAthleticsMult` 0.1 (init only; unused);
  - `fJumpSwimmingMult` 2.0 (NV 0x930640; FO3 0x7668B0; ESM both 2.0);
  - `fSwimRadiationDamageMult` (exe 0.5; both ESMs 0.1; NV 0x888B50, FO3 0x711D60. Part C cites NV 0x88A9A3 for the same setting; see [the disagreements](#where-the-spec-parts-disagree)).

  The water-height rule is in C.W1. Nothing here sets a "swim level" fraction.
- **Today.** `physicssystem.cpp:951`: swim level = water level − 2 × half-height × `fSwimHeightScale`. Breath uses 0.875 of the height for "head under" (`derivedstats.hpp:71`).
- **Confidence.** Exe-read for the setting values and users. The water-height rule was not found by part D (D.unclear 5; part C found it).

### D.S4 Audio distances (replaces the six `fAudio*`)

- **Rule.** Fallout has no `fAudio*` settings. Each SOUN record has its own minimum and maximum attenuation distance (bytes 0 and 1 of `SNDD`, 36-byte layout; the 12-byte `SNDX` layout has them at the same places). Sound distances come from the sound records.
- **Data.** In the base ESMs: NV has 3,054 `SNDD` and 136 `SNDX` records, of which 404 have both 0. FO3 has 721 and 862, of which 225 have both 0. The most common values: min 20, max 9 to 18. The unit is 5 game units per step (GECK convention; not verified in the exe; online memory).
- **Related settings.** `fDeathSoundMaxDistance` = 1000 limits death sounds (NV 0x89E89D, FO3 0x71B5B1). `iSpeakSoundLipDistance` = 750 limits lip-sync. `fWeaponInterior*AttenuationMod` and `*VolumeMod` adjust weapon sounds indoors.
- **Today.** `soundbuffer.cpp:36-39`, `soundmanagerimp.cpp:272-281`: the Morrowind defaults (20, 50, 10, 60, 5, 40 for min, max, mult, voice) apply to every sound that has no record range.
- **What to do.** Use the SOUN record's min and max. Keep a fallback for records with 0/0 (D.unclear 7).
- **Round check.** A loop sound with min 20, max 12 (×5 units: 100 and 60 units?): an audibility test only with a mic. Skip.
- **Confidence.** Exe-read for the absence. Record statistics from the ESMs. The ×5 unit is online-only.

### D.S5 Cell respawn (replaces `iMonthsToRespawn`)

- **Rule.** `iHoursToRespawnCell` = 72 hours (exe default; no ESM value; both games). A cell that was left resets its contents after that many game hours. Merchants refill on `iMerchantRespawnDay1` = 0 and `iMerchantRespawnDay2` = 3 (day numbers; NV 0x886360, FO3 0x70D300).
- **Exe.** NV 0x526100 (0x526104). FO3 users 0x4BFAC0, 0x4BFBA0, 0x4DEC50, 0x6D01D0, 0x6DE4E0, 0x6F5600, 0x70D300, 0x78ED50.
- **Today.** `cellstore.cpp:1318-1320`: `24 * 30 * iMonthsToRespawn` hours (a Morrowind setting). `components/falloutscript/respawn.hpp:20` already documents `iHoursToRespawnCell`. Whether the Morrowind `CellStore::respawn` still runs for Fallout cells, and how, was not traced.
- **Round check.** None (needs 72 game hours). A unit test of the hour count suffices.
- **Confidence.** Exe-read for the value. The contents reset rule is another area's.

### D.S6 Default cell name (replaces `sDefaultCellname`)

- **Rule.** `sDefaultCellName` = "Wilderness" (exe default for NV and FO3; no ESM value). When a cell has no name of its own and is exterior, its display name is this string (NV 0x551740: the cell's FULL name if present, else, for an exterior cell, the setting). Today's code asks the worldspace's name first for ESM4 cells (`worldimp.cpp:598-605`). The exe does not do that in this function. The HUD location line ("Mojave Wasteland") is built elsewhere (not read).
- **Today.** `worldimp.cpp:605`, `worldmodel.cpp:322`: `sDefaultCellname` from the Morrowind setting store (a lookup that throws if the setting is missing). Replace it with the exe default "Wilderness".
- **Confidence.** Exe-read.

### D.S7 Defaults that plugins do not carry

- **Rule.** `FalloutScripts::gameSettingValues()` (`falloutscripts.cpp:4181-4203`) builds a map from the ESM4 `GMST` records only, with lower-cased names. `gameSetting(name)` (`falloutprogress.cpp:102`) returns nullopt when absent. `gameSetting(name, fallback)` (`falloutscripts.cpp:2650`) returns the fallback. There are 44 call sites that pass a numeric fallback, copied by hand from the exes (examples: `fEssentialHealthPercentReGain` 0.3, `fMoveOneCrippledLegSpeedMult` 0.85, `fCombatPlayerLimbDamageMult`). `exesettings.cpp` merges only the default texts of string settings (read from the player's own exe, never stored in the repo). So the numeric exe defaults are not merged.
- **Missing from the call sites:** all of the settings in part D that the ESM lacks: `fPlayerDeathReloadTime`, `fDyingTimer`, `fKnockdown*`, `fKnockback*`, `fHealingRateSleeping*`, `iActivatePickLength`, `iHoursToRespawnCell`, `sDefaultCellName`, `fCrippledArm*`.
- **How to read them.** The same scan as `exesettings.cpp` finds the numeric default. NV: static-init code `fld1` / `fldz` / `fld [float]` / `push imm` before `push name; mov ecx, obj; call 0x40C150 / 0x40E0B0` (see `lib.py: gmst_objs`). FO3: a table of {default (4 bytes), name pointer, tag pointer}, 12 bytes each. The setting object in FO3 starts 8 bytes before the name pointer.
- **Owner decisions.** Step 4 removes `esmfallbacks.lua` (moved to `files/data` in step 3 with a comment that step 4 removes it). The `fAudio*` settings have no exe counterpart.
- **Tolerance for rounds.** None (they are data).
- **Confidence.** Code-read.

### D.table Settings table (the value that runs: ESM if present, else the exe default)

| Setting | NV | FO3 | Where it is read (NV / FO3) |
|---|---|---|---|
| `fPlayerDeathReloadTime` | 5.0 (exe) | 5.0 (exe) | 0x93E840 / 0x788350 |
| `fDyingTimer` | 2.0 (exe) | 2.0 (exe) | 0x89D900 / 0x71AC50 |
| `fKillCameraTime` | 3.5 (exe) | absent | 0x89D900 |
| `fEssentialDeathTime` | 10.0 (exe) | 10.0 (exe) | 0x89D900 / 0x71AC50 |
| `fEssentialHealthPercentReGain` | 0.3 (exe) | 0.3 (exe) | 0x89D900 / 0x71AC50 |
| `bEssentialTakeNoDamage` (INI) | 1 | 1 | 0x8A1800 / (FO3 string present) |
| `fKnockdownBaseHealthThreshold` | 75 (exe) | 75 (exe) | 0x646400 / 0x5827F0 |
| `fKnockdownCurrentHealthThreshold` | 25 (exe) in part D's table; 50 in both ESMs per the dumps | 25 (exe) in part D's table; 50 per the dumps | same |
| `fKnockdownChance` | 0.25 (exe) | 0.25 (exe) | same |
| `fKnockdownDamageBase` / `Mult` | 0 / 0.3 (exe) | 0 / 0.3 (exe) | same |
| `fKnockdownAgilBase` / `Mult` | 0 / 1.0 (exe) | 0 / 1.0 (exe) | same |
| `fKnockbackAgilBase` / `Mult` | 1.0 / −0.008 (exe) | same | 0x646580 / 0x582980 |
| `fKnockbackDamageBase` / `Mult` | 50 / 10 (exe) | same | same |
| `fKnockbackForceMax`, `fKnockbackTime` | 512, 1.0 (exe) | same | 0x89A760 / 0x71C930 |
| `fMoveOneCrippledLegSpeedMult` / `Two…` | 0.85 / 0.75 (ESM = exe) | same | 0x647D10 / 0x5845B0 |
| `fCombatPlayerLimbDamageMult` | 0.5 (ESM = exe) | 0.5 | 0x9B6F6D / 0x7C3E7D |
| `fCrippledArm1H` / `2H` / `Arms1H` / `Arms2H` | 0.2 / 0.4 / 0.4 / 0.6 (exe) | absent | 0x646910 |
| `fGunSpreadArmBase/Mult`, `HeadBase/Mult` | 0, −1 style (unused in NV) | 1 / −1 / 1 / −1 (exe) | 0x582C90 (FO3) |
| `fConcussionTimer` | 40 (ESM; exe 15) | 40 (ESM; exe 15) | 0x80BE40 / 0x6A6650 |
| `fAVDHealRateEndurance6..10Bonus` | 5, 5, 5, 10, 10 (ESM; exe 1, 1, 1, 2, 2) | 5, 5, 5, 10, 10 (ESM) | 0x643A70 / 0x581620 |
| `fHealingRateSleepingBase` / `Mult` | 2 / 2 (exe) | 2 / 2 (exe) | 0x648B00 / 0x5850B0 |
| `fActionPointsRestoreRate` | **0.06** (ESM; exe 1.0) | **4.0** (ESM; exe 1.0) | 0x66DDA0 / 0x5A1980 |
| `fAVDActionPointsBase` / `Mult` | 65 / 3 (ESM) | 65 / 2 (ESM) | see E.1.2 |
| `iActivatePickLength` | 150 (exe) | 150 (exe) | 0x86F6A0 / 0x6E4510 |
| `iHoursToRespawnCell` | 72 (exe) | 72 (exe) | 0x526100 / 0x4BFAC0 … |
| `sDefaultCellName` | "Wilderness" (exe) | "Wilderness" (exe) | 0x551740 / 0x4D3BC0 |
| `fSwimRadiationDamageMult` | 0.1 (ESM; exe 0.5) | 0.1 (ESM; exe 0.5) | 0x888B50 / 0x711D60 |

Where a row says "(exe)", the ESM has no record. The ESM columns were confirmed from `gmst_nv.txt` and `gmst_fo3.txt`. The "exe" values come from the exe scans.

### D.checks Checks the next rounds can use

| What to measure | Expected | Tolerance and why |
|---|---|---|
| time from death to reload (default) | 5.0 s | ±0.6 s: the timer starts at the first frame seen in state 1 or 2; frame step; real-time clock |
| time from death to main menu with no save | 5.0 s | ±0.6 s |
| health 0.5 → dead | dead in 1 frame | exact |
| essential NPC down time | 10.0 s | ±1.0 s: timer ticks by frame; teammates may pause |
| hit on a downed essential: health | 30% of max | ±1 point |
| AP refill (NV, no perk) from empty | 16.7 s | ±8% |
| AP refill (FO3) from empty | maxAP / 4 s | ±8% |
| health regen over 2880 simulated s (NV, End 9) | +8.0 HP | ±0.5 HP |
| run speed, one leg crippled / two | ×0.85 / ×0.75 | ±0.02 |
| activation at 140 / 160 units | yes / no | ±5 units |
| knock-down by a 76-damage hit on 100 HP | always | exact |
| knock-down by a 40-damage hit (Agility 5) | 25% of 200 trials = 50 (with the exe value 25; see D.D5) | ±20 |
| NV spread, both arms crippled minus healthy | +0.4 (one-hand) | ±0.01 |

### D.unclear Open items in part D

1. Death flow: box or auto-reload. The spec's read of NV 0x93E840 and FO3 0x788350 is that the default (`fPlayerDeathReloadTime` = 5) auto-reloads without a box, and the box only shows when the setting is ≤ 0. Today's Lua shows the box at 5 s, and rounds 12BQ and 12BR accepted that behaviour (box at +5.02 s). The spec did not run the game, and no game output seen distinguishes the two. Whoever implements D.D2 should re-read the branch (two instructions: `test ah, 0x41 / jne` at NV 0x93EF6, `test ah,5 / jp` at FO3 0x78B1A1). A hidden run on the real exe would settle it. An earlier note (Mac round 4A H2) read the branch the other way round.
2. What health an essential actor has when it gets up. The spec reads NV 0x8A0960 (called at knock-down and at wake-up) as "heal all damage, +1, and restore limbs to full". The reading rests on the inference that vtable slot +0x14 of the actor-value owner is the damage modifier (negative when damaged). Not tested. The 30% rule is certain, and it covers only a second blow on an already-down actor.
3. Unit of the regeneration time. `dt` is the simulated second. OpenFallout's earlier reads agree (an hour of waiting = 3600 / timescale seconds). The clock `[0x11E0E2C]` was not traced to its source.
4. Head crippled and concussion. The ESM spells `HeadCrippleEffect` (0x67572) and `PlayerConcussed` (0x6533A) hold `Perception −4` (effect `ReducePerception` 0x32DE3). The exe has no immediate reference to either form id, so scripts in the ESM probably apply them. The vision effect `FXCrippleHead` (an image-space modifier) is played by the concussion-effect update (NV 0x80BE40, every `fConcussionTimer` = 40 s in both ESMs; exe default 15). What applies `HeadCrippleEffect` when the head is crippled was not found. Not covered in OpenFallout (`GetConcussed` only, `falloutdamage.cpp:179`).
5. `fSwimHeightScale`. The exe has no setting with that job in the spec's name list. Swim state and water depth are part C's job. OpenFallout already uses 0.875 of the height for "head under water" (`derivedstats.hpp:71`).
6. Third-person reach. `iActivatePickLength` is the ray length of the pick from the camera. Whether third person adds the camera distance was not found (NV 0x86F7D3 is guarded by a camera-mode INI value 0x11C3EA4, which was not named).
7. The audio fallback for a SOUN record whose min and max are both 0. 404 of the NV records and 225 of the FO3 records have min = max = 0 (some are the 12-byte old layout). What the engine does for them was not traced.
8. Player knock-down and unconsciousness. The exe's knock-down check (D.D5) takes any actor. What it does to the player (camera, controls) was not traced.
9. Player death camera and input lock. Only the start of death (0x709C40(0) → 0x77F3C0(0): hides the HUD main tile, inferred) was found, plus the fact that the player update keeps running during the 5 s. No death camera or input lock was found in the exe. The kill camera (`fKillCameraTime` 3.5 s, NV only) is for kills the player makes, not the player's death.
10. Per-frame health regeneration conditions. NV 0x8C3C40 calls the health regen only when a process flag (mask 0x20000000) is set. The flag was not named. Whether it is off in combat is not known.

---

## E. Stats and levelling

### E.0 Summary

- **Derived stats are not stored.** In both games the exe has one table of actor values (77 in NV, 73 in FO3). Each entry has flags and, for derived ones, a calculation function. When a value is asked for, the game works it out again from the current SPECIAL, the level, the settings and the modifiers. Changing Endurance changes health at once. Changing Luck changes every skill.
- **Health** is the record's base health (the player record says 100) plus Endurance × 20 plus (level − 1) × a per-level number (5 in NV, 10 in FO3). A fresh level-1 character with Endurance 5 has 200 HP in both games. Health uses the permanent (non-temporary) Endurance.
- **Action points** are 65 + Agility × 3 in NV and 65 + Agility × 2 in FO3 (80 and 75 at Agility 5). **Carry weight** is 150 + Strength × 10 (200 at Strength 5), in both games. **Critical chance** is Luck percent. These use the current attribute, including temporary chem effects.
- **Skills** (player): `2 + floor(2 × governing attribute) + ceil(0.5 × Luck)`, plus 15 if the skill is tagged. All attributes at 5 give 15 untagged and 30 tagged. The Luck term rounds up. The exe recomputes skills from the current SPECIAL. Points spent at level-up are a separate permanent modifier on top.
- **XP for level n** is `(n−1) × 200 + 150 × (n−1)(n−2)/2`. Level 2 needs 200 XP. Level 3 needs 550. The 150 comes from `Fallout3.esm` and `FalloutNV.esm`. The exe default is 100, and the ESM wins.
- **Skill points per level.** NV `floor(10 + 0.5 × min(Int,10))`, plus 1 when the new level is even and Int is odd. FO3 `11 + min(Int − 1, 10)`, which is 10 + Int. At Int 5, level 2: NV gives 13 and FO3 gives 15.
- **Perks.** FO3 gives one on every level. NV gives one when the new level is divisible by `iLevelsPerPerk` (2). TTW sets `iLevelsPerPerk` to 1, so TTW gives a perk every level.
- **Level cap.** FO3 20 (exe default), 30 with Broken Steel. NV 30, plus 5 for each installed NV add-on (Dead Money, Honest Hearts, Old World Blues, Lonesome Road), up to 50. TTW's own plugin sets 60, but the exe then clamps it to 50 (E.unclear 6).
- **Where today's code disagrees with the exe:**
  - the skill formula in chargen rounds the Luck part down, so Luck 5 gives 14 instead of 15;
  - skills are frozen at chargen and do not follow SPECIAL afterwards;
  - poison resistance, radiation resistance, unarmed damage and fatigue are not derived at all;
  - melee damage is not floored;
  - AP, carry weight and crit chance use the permanent attribute where the exe uses the current one;
  - health follows XP immediately where the exe uses the stored level.

### E.0.1 How the original game computes an actor value

**The table.** Both exes register actor values at start-up: NV register function 0x66efe0, table builder 0x66f260 (77 entries). FO3 register function 0x5a2f20 called from 0x5a3026 to 0x5a3962 (73 entries). Each entry is `{index, name, type, flags, calcFn, ...}`. The NV entry struct has the calc function at +0x4c and the flags at +0x44. The lookup is NV 0x66e920.

Derived entries and their calc function:

| AV | Name | NV calc | FO3 calc | Flags (NV) |
|---|---|---|---|---|
| 12 | ActionPoints | 0x6439d0 | 0x5815d0 | 0 |
| 13 | CarryWeight | 0x643a10 | 0x5815f0 | 0x801 |
| 14 | CritChance | 0x643b10 | 0x5816a0 | 0x800 |
| 15 | HealRate | 0x643a70 | 0x581620 | 0x801 |
| 16 | Health | 0x643670 (→ 0x6436c0) | 0x581b10 (→ 0x5814d0) | 0x10c1 |
| 17 | MeleeDamage | 0x643b50 | 0x5816c0 | 0x800 |
| 19, 20 | PoisonResist, RadResist | 0x643e40 | 0x581950 | 0x800 |
| 22 | Fatigue | 0x643820 (→ 0x643870) | none | 0x1841 |
| 32 to 45 | skills | 0x643bd0 (→ 0x643c90) | 0x581b50 (→ 0x5817e0) | 0x10, 0x410, 0x11 |
| 56 | UnarmedDamage | 0x643ed0 | 0x5819b0 | 0x800 |

**Base value of an actor** (NV `Actor::GetBase`, the actor-value-owner sub-object at actor +0xa4, vtable slot +4 = 0x8803a0; FO3 0x6f4db0):
1. `refBase` = the base form's stored value (record DATA or DNAM).
2. If a script `SetActorValue` override exists and the AV is not a skill: base = override.
3. A creature with AV flag 0x1000 (Health): base = `refBase`.
4. If the AV is derived (flag 0x800), or the actor is the player, or flag 0x80: derived = `calcFn(actor, av)`. If it has no calc function, base = `refBase`. If flag 0x40 (Health and Fatigue) or an override exists: base = derived + `refBase`. Otherwise base = derived.
5. Otherwise base = `refBase` (the non-player path for plain AP and skills).

That is NV 0x880471 to 0x880567 and FO3 0x6f4ec9 to 0x6f4ee5. The flag test at 0x6f4ed3 and the add at 0x6f4ee5 are the key instructions in FO3.

**Current value of the player.** NV `PlayerCharacter` vtable 0x108a974, slot +0xc (0x93acb0) = base + three modifier arrays. The arrays are PlayerCharacter fields (floats indexed by AV number, 77 entries each):
- +0x244: damage (taken off).
- +0x378: permanent modifier. This is where spent skill points and perk changes go.
- +0x4b0: temporary modifier (chem and effect buffs). Health's temporary slot is at +0x4ac.

The mapping comes from slots +0x10 to +0x18 of the vtable (0x94c460 reads kind 0, 0x94c490 kind 2, 0x94c4c0 kind 1). It is exe-read. The debug print at NV 0x5bd960 labels the slots damage, temp and perm.

**Level-up value.** NV vtable +0x20 (0x93ad60). For a non-skill AV it is `base + perm modifier` (no temp buffs), clamped. For a skill it is the derived value computed from base + perm SPECIAL, plus override and perm modifier. NV slot +0x1c (0x93ad30) is its floor as an int.

Health, skill points per level and the level-up screen use the level-up value. AP, carry weight, crit chance, melee damage, resists and skills (the on-screen value) use the current value (NV 0x66ef50 calls `vt+0xc` then clamps through 0x66f190).

**Clamp.** NV 0x66f190 applies per-flag limits. A skill (flag 0x10) is clamped to 0..100 (constant 100.0 at 0x1016410).

**Where the player's own data sits:**

| Thing | Original game | OpenFallout today |
|---|---|---|
| SPECIAL base | player record DATA bytes 4..10 (all 5), copied into the AV base | `ActorValues`, set from chargen (`falloutscripts.cpp:7033`) |
| level | stored on the player base form's actor-base data (ACBS level, set through 0x47dfe0 on `base+0x30`; read through `vt+0x28`, NV 0x87f9f0). The pending-level-up struct is at PlayerCharacter +0x878 → {byte pending at 0, int next-level XP at 4, byte lock at 8} (NV 0x464e10 and 0x559450) | `PlayerProgress::level()`, derived from XP (`playerprogress.cpp:92`) |
| XP | actor value 24 (`XP`), base part, changed through `ModActorValue` (`vt+0x3a8` of the actor). Capped at XPfor(`iMaxCharacterLevel`) (NV 0x8d51a5) | `PlayerProgress::mExperience` |
| karma | actor value 23 | `PlayerProgress::mKarma` |
| perm and temp modifiers, damage | the PlayerCharacter arrays above | `ActorValues::Entry` |
| skill points to spend | not stored; computed when the level-up menu opens (NV 0x784c80 → 0x648bc0, FO3 0x657190 → 0x585620) | `PlayerProgress::mSkillPoints` (stored) |
| tagged skills | the player's class record: TESClass +0x44, +0x48, +0x4c, +0x50 (4 slots), tested by NV 0x5a5f40 | `Chargen::mTagSlots`, then +15 baked into the base |

### E.1.1 Max health (player)

- **Games.** Both. The per-level multiplier differs: NV 5, FO3 10. TTW uses NV.
- **Rule.** Record health plus Endurance times 20 plus the levels above 1 times the per-level number.
- **Formula.** `health = recordHealth + (END_levelup + fAVDHealthEnduranceOffset) × fAVDHealthEnduranceMult + (level − 1) × fAVDHealthLevelMult`. The level term applies only when level > 0. `END_levelup` is base + permanent modifier. The temporary Endurance is not used.
- **Settings.**

  | Name | NV ESM | FO3 ESM | Exe default NV / FO3 |
  |---|---|---|---|
  | fAVDHealthEnduranceMult | 20 | 20 | 20 / 5 |
  | fAVDHealthEnduranceOffset | 0 | 0 | 0 |
  | fAVDHealthLevelMult | 5 | 10 | 20 / 20 |

  Player record DATA health = 100 in both games (Player NPC_ 0x7, bytes `64000000 0505050505050505`). In the exe default column, the NV value for `EnduranceMult` and `LevelMult` comes from the static initialisers at 0xf5cd1e and 0xf5cd4e. The FO3 column comes from the table at 0xf5efb8 and 0xf5efd0. The ESMs override them.
- **Exe.**
  - NV: wrapper 0x643670 (reads `vt+0x20(7)`, clamps with 0x66f190, calls 0x6436c0). Player branch at 0x643715 (flag from `vt+0x360`, is-player), uses 0x64371e and 0x643728. Level term at 0x6437d7 to 0x64380f, `fAVDHealthLevelMult` at 0x6437fa. The level is read from `[actor+0xa4]->vt+0x28` (u16).
  - FO3: wrapper 0x581b10 (reads `vt+0x20(7)`), formula 0x5814d0. The player branch tests `vt+0x35c`. Endurance term at 0x58150d to 0x581517. Level term 0x581592 to 0x5815b1.
  - The base is added through AV flag 0x40: NV 0x880567 (`fadd` of `refBase`), FO3 0x6f4ee5.
- **Today.** `DerivedStats::playerHealth` (`derivedstats.cpp:53`) uses the same formula, called from `falloutscripts.cpp:4414` with `permanent(AV::endurance)` and the record health. Matches. Since step 4a `mProgress.level()` is the stored level, as the exe reads it (E.unclear 3). The creature branch (`falloutscripts.cpp:4407`) matches the exe (flag 0x1000 means the raw record value).
- **Round check.** Fresh character, SPECIAL all 5, level 1: `GetActorValue Health` = 200 in NV and FO3, tolerance 0.5 (an exact float sum; leave room for display rounding only). Level 2 (end of the level-up screen): NV 205, FO3 210. Endurance 7, level 1: 240. Do the check with no perks, traits or temporary effects.
- **Confidence.** Exe-read for the formula and its settings. Inferred for the record-health addend. Online: Hit Points (https://fallout.fandom.com/wiki/Hit_Points) gives 95 + END×20 + level×5 for NV, which equals 100 + END×20 + (level−1)×5. The FO3 per-level term comes from the exe and the ESM. The wikis found disagree (web results gave an END-based per-level gain for the older games).

### E.1.2 Action points

- **Games.** Both, with a different multiplier.
- **Formula.** `AP = fAVDActionPointsBase + Agility_current × fAVDActionPointsMult`. For the player only (flags 0 means other actors use the record value).
- **Settings.** Base 65 (both ESMs; exe default 65). Mult NV 3, FO3 2 (exe default 2 in FO3's table at 0xf5f000; the NV exe default was not found in the pickles, the ESM has 3).
- **Exe.** NV 0x6439d0 (Agility id 0xa at 0x6439d6, mult at 0x6439e0, base at 0x6439f2). FO3 0x5815d0 (id 0xa, `fmul` at 0x5815dc, `fadd` at 0x5815e2).
- **Today.** `DerivedStats::actionPoints` (`derivedstats.cpp:68`), called at `falloutscripts.cpp:4373`. The formula matches. It uses the permanent Agility instead of the current one, so a temporary Agility buff would not change AP (a minor mismatch).
- **Round check.** All 5: NV 80, FO3 75. Tolerance exact (0.01).
- **Confidence.** Exe-read. Online: https://strategywiki.org/wiki/Fallout_3/Resulting_Statistics gives 65 + Agility×2 for FO3.

### E.1.3 Carry weight

- **Formula.** `CW = fAVDCarryWeightsBase + Strength_current × fAVDCarryWeightMult`. Base 150, Mult 10, the same in both ESMs and exes (this is part E's statement; part B gives NV exe defaults 150 and 25, see [the disagreements](#where-the-spec-parts-disagree)).
- **Exe.** NV 0x643a10 (id 5 at 0x643a16). FO3 0x5815f0.
- **Today.** `derivedstats.cpp:73`, `falloutscripts.cpp:4375`. Matches (the same permanent-against-current difference). Perks that add carry weight come through `falloutperks.cpp:21`, outside this part.
- **Round check.** Strength 5: 200 in both games. Strength 8: 230.
- **Confidence.** Exe-read.

### E.1.4 Critical chance

- **Formula.** `crit% = fAVDCritLuckBase + Luck_current × fAVDCritLuckMult`. Base 0, Mult 1 (NV: the ESM has the Base only and the exe default of Mult is 1.0; FO3 exe default 1.0). So Luck 5 gives 5.
- **Exe.** NV 0x643b10 (id 0xb). FO3 0x5816a0. No clamp (flag 0x800).
- **Today.** `derivedstats.cpp:83` clamps to 0..100. The exe does not clamp this AV. Harmless below Luck 100. The permanent-against-current difference applies.
- **Round check.** Luck 5: 5, exact.
- **Confidence.** Exe-read.

### E.1.5 Melee damage bonus

- **Formula.** `bonus = floor((Strength_current + fAVDMeleeDamageStrengthOffset) × fAVDMeleeDamageStrengthMult)`. If the result is not above 0, it is 0. The AV only exists for AV 17. Offset 0 and Mult 0.5 in both ESMs. So Strength 5 gives 2 and Strength 6 gives 3.
- **Exe.** NV 0x643b50 (floor call 0x406ce0 at 0x643ba1). FO3 0x5816c0 (floor call 0xc00d40 at 0x5816f2).
- **Today.** `DerivedStats::meleeDamage` (`derivedstats.cpp:78`) does not floor, so Strength 5 gives 2.5. Mismatch. The value feeds `falloutscripts.cpp:2926`.
- **Round check.** Strength 5: 2, exact.
- **Confidence.** Exe-read. The floor call is an inferred identification (see E.unclear 9 for FO3).

### E.1.6 Unarmed damage bonus

- **Formula.** `unarmed = fAVDUnarmedDamageBase + Unarmed_current × fAVDUnarmedDamageMult`. Base 0.5 and Mult 0.05 in both ESMs (FO3's exe default is 2.0 and 0.15; the ESM overrides). Unarmed 15 gives 1.25.
- **Exe.** NV 0x643ed0 (AV 45 read at 0x643eee, `fmul` mult at 0x643efd, base at 0x643f0e). FO3 0x5819b0 (Unarmed id 0x2d at 0x5819c6).
- **Today.** Missing. AV 56 is not in `ActorValues::isDerived` (`actorvalues.cpp:55`) or `derivedValue`.
- **Round check.** All 5, untagged, Unarmed 15: 1.25, tolerance 0.01.
- **Confidence.** Exe-read.

### E.1.7 Poison and radiation resistance (base)

- **Formula.** `poisonResist = (END_current + fAVDPoisonResistEnduranceOffset) × fAVDPoisonResistEnduranceMult`. `radResist = (END_current + fAVDRadResistEnduranceOffset) × fAVDRadResistEnduranceMult`.
- **Settings.** Neither setting is in either ESM, so the exe defaults apply (NV static initialisers at 0xf5d4ce to 0xf5d55e; FO3 table at 0xf5f168 to 0xf5f18c): poison Offset −1, Mult 5. Rad Offset −1, Mult 2. Endurance 5 gives poison 20 and rad 8.
- **Exe.** NV 0x643e40 (AV 0x13 poison at 0x643e5f, 0x14 rad at 0x643e92). FO3 0x581950.
- **Today.** Missing (nothing derives AV 19 or 20).
- **Round check.** Endurance 5: poison 20, rad 8, exact. Armour and perks add on top, so test unarmoured.
- **Confidence.** Exe-read.

### E.1.8 Heal rate

- **Formula.** Endurance (floor) 6, 7, 8, 9, 10 gives 5, 5, 5, 10, 10, otherwise 0. The settings `fAVDHealRateEndurance6Bonus` to `10Bonus` are the same in both ESMs (the exe defaults are 1, 1, 1, 2, 2; see D.R1).
- **Exe.** NV 0x643a70 (reads 0x66ef20, which floors). FO3 0x581620.
- **Today.** `derivedstats.cpp:88`, only for the player (`falloutscripts.cpp:4385`). Matches. Not a round-check target.
- **Confidence.** Exe-read.

### E.1.9 Skills (initial value and live formula)

- **Games.** Both. NV has Survival (AV 44) and no Big Guns calc. FO3 has Big Guns (33) and no AV 44 calc. TTW uses NV.
- **Rule.** A skill is a base of 2, plus twice its governing attribute, plus half the Luck rounded up, plus 15 if it is a tag skill. The player's value is recomputed whenever SPECIAL changes. Points spent at level-up sit on top as a permanent modifier. The display is clamped to 0..100.
- **Formula.** `skill = fAVDSkill<X>Base + floor(attr × fAVDSkillPrimaryBonusMult) + ceil(Luck × fAVDSkillLuckBonusMult) + (tagged ? fAVDTagSkillBonus : 0)`, then `+ permMod − damage`, clamped 0..100.
- **Settings.** Every `fAVDSkill*Base` = 2 in both ESMs. `fAVDSkillPrimaryBonusMult` and `fAVDSkillLuckBonusMult` are not in the ESMs, so the exe defaults apply: 2.0 and 0.5 (NV at 0xf5d46e and 0xf5d19e; FO3 table 0xf5f150 and 0xf5f0a8). `fAVDTagSkillBonus` = 15 in both ESMs (exe default 10, overridden).
- **Governing attribute** (NV table at 0x119b144 + av × 4, FO3 table at 0xf5e0ec + av × 4):

  | Skill (AV) | NV | FO3 |
  |---|---|---|
  | Barter 32 | Charisma | Charisma |
  | Big Guns 33 | Endurance (no calc in NV) | Endurance |
  | Energy Weapons 34 | Perception | Perception |
  | Explosives 35 | Perception | Perception |
  | Lockpick 36 | Perception | Perception |
  | Medicine 37 | Intelligence | Intelligence |
  | Melee Weapons 38 | Strength | Strength |
  | Repair 39 | Intelligence | Intelligence |
  | Science 40 | Intelligence | Intelligence |
  | Guns (Small Guns) 41 | Agility | Agility |
  | Sneak 42 | Agility | Agility |
  | Speech 43 | Charisma | Charisma |
  | Survival 44 | Endurance | (no calc) |
  | Unarmed 45 | Endurance | Endurance |

- **Exe.** NV 0x643bd0 → 0x643c90. Bonus combiner 0x643c20 (floor at 0x643c45, ceil 0x4e4450 at 0x643c6c). Tag test and `fAVDTagSkillBonus` at 0x643e0e to 0x643e2b (tests the class tag slots through 0x884350 and 0x5a5f40). FO3 0x581b50 → 0x5817e0. Combiner 0x581770 (ceil 0xc03f30 at 0x58179e, floor 0xc00d40 at 0x5817b1). Tag add at 0x581926. Attribute reads are current values when the flag argument is 0 (0x643ce3) and level-up values when it is 1 (0x643d18).
- **Today.**
  - Starting values come from `Chargen::skillValue` (`chargen.cpp:432`): `skillBase + 2 × governing + lucky / 2 + tag`. `lucky / 2` is integer division, i.e. floor. The exe rounds up: Luck 5 gives 3, not 2. Mismatch: today all-5 untagged skills are 14, and the exe gives 15.
  - The constants `skillBase = 2` and `tagBonus = 15` are fixed (`chargen.hpp:96`, `:97`). They agree with the ESM values but are not read from the settings.
  - The value is written once as a base (`falloutscripts.cpp:7010` `applyChargenSkills`) and then frozen. In the exe it is live, so perks, chems and bobbleheads that change SPECIAL change skills. Mismatch.
  - The 0..100 display clamp exists (`actorvalues.cpp:28`).
- **Round check.** SPECIAL all 5, no tags, level 1: every skill (NV: 13 skills, no Big Guns; FO3: 13 skills, no AV 44) = 15. A tagged skill = 30. Second example, S7 P6 E5 C4 I7 A6 L5:

  | Skill | Value |
  |---|---|
  | Barter, Speech | 13 |
  | Energy Weapons, Explosives, Lockpick | 17 |
  | Guns (Small Guns), Sneak | 17 |
  | Medicine, Repair, Science | 19 |
  | Melee Weapons | 19 |
  | Survival, Unarmed (and FO3 Big Guns) | 15 |

  Tolerance exact (integers).
- **Confidence.** Exe-read for NV. The FO3 rounding direction is inferred from the instruction pattern and from structural identity with NV (E.unclear 9). Online: the search did not return the skill formula, only that Luck boosts all skills (https://fallout.fandom.com/wiki/Fallout:_New_Vegas_skills).

### E.1.10 Damage resistance and damage threshold (note)

NV AV 18 (0x8a0a90), AV 76 (0x8a0ad0) and FO3 AV 18 (0x6f6ba0) are armour sums, not SPECIAL formulas. They are another part's topic. Fatigue and limb conditions are covered elsewhere. Limb conditions return `fAVDDefaultBodyPartCondition` (NV 0x643a50, FO3 0x581610).

### E.1.11 Companion nerve (NV)

Not found among the registered derived stats. No `fAVD*Nerve` setting exists in either ESM. The 77 NV AVs have no "nerve". The spec did not find it and calls it n/a for step 4.

### E.1.12 XP needed for each level

- **Games.** Same formula in both. TTW uses NV.
- **Formula.** `XP(1) = 0`, `XP(2) = iXPBase`, `XP(L) = (L−1) × iXPBase + iXPBumpBase × (L−1)(L−2)/2`. The loop in the exe: result = base, step = base, repeated L−2 times: step += bump, result += step.
- **Settings.** `iXPBase` is not in either ESM, so the exe default 200 applies (NV 0xf6a6c9, FO3 table 0xf62314). `iXPBumpBase` = 150 in both ESMs (exe default 100, overridden).
- **Values.** XP(2) = 200, XP(3) = 550, XP(4) = 1050, XP(5) = 1700, XP(10) = 7200, XP(20) = 29450, XP(30) = 66700.
- **Exe.** NV 0x648b50 (settings at 0x648b60 and 0x648b75). FO3 0x5850f0 (settings at 0x5850fe and 0x585103).
- **XP storage and cap.** NV `AddExperience` 0x8d5100 refuses any XP once the player's level ≥ the current `iMaxCharacterLevel`. Otherwise the amount goes through perk entry point 9, is rounded up (ceil, 0x4e4430) and added to AV 24. It is clamped so the total never exceeds XPfor(`iMaxCharacterLevel`) (0x8d5198 to 0x8d51c3). The level-up is pending when XP ≥ XPfor(level + 1) and level < max (0x8d55e0).
- **Today.** `Levelling::experienceForLevel` and `uncappedExperience` (`levelling.cpp:45`, `:83`) use the same formula. `PlayerProgress::rewardExperience` caps at XPfor(max level) (`playerprogress.cpp:50`). Matches. Difference: the exe refuses XP at max level, while OpenFallout accepts and clamps (same result). The old `PlayerProgress::levelFor` (`playerprogress.cpp:78`) is equivalent.
- **Round check.** `GetXPForNextLevel` at level 1 (XP 0) = 200. At level 2 = 550. Tolerance exact. After `RewardXP 200` with no XP perks, the level-up screen must open.
- **Confidence.** Exe-read.

### E.1.13 Skill points per level

- **Games.** Differ. TTW uses NV.
- **NV formula.** `m = min(floor(Int_levelup), 10)`. `pts = floor(0.5 × m + 10)`. If `level % 2 == 0` and `m % 2 == 1`: `pts += 1`. Then perk entry point 10 modifies it (Educated; Comprehension is a book, not this). `iLevelUpSkillPointsBase`, `iLevelUpSkillPointsInterval` and `iLevelUpSkillCount` are dead in NV (no code reads them). TTW's `iLevelUpSkillPointsBase` = 8 is dead too.
- **FO3 formula.** `pts = iLevelUpSkillPointsInterval × min(Int_levelup − 1, 10) + iLevelUpSkillPointsBase` = `1 × min(Int − 1, 10) + 11`, i.e. 10 + Int up to Int 11. Then entry point 10.
- **Settings.** NV exe constants 0.5 (0x1011588) and 10.0 (0x1020758). FO3 settings, both ESMs: 11 and 1 (exe defaults 7 and 2, overridden).
- **Which level.** The NV level argument is the player's current level, read at menu creation (0x648bc0 uses `vt+0x28`), i.e. the new level after the increment.
- **Exe.** NV 0x648bc0 → 0x648c10, called from the level-up menu creator 0x784c80 (call at 0x784ee3, then entry point 10 at 0x784f01). FO3 0x585620 (clamp at 0x585630, formula 0x58563a) called from 0x657190 (0x65737b), with 0x585130 used by the NPC auto-calc.
- **Tag skill multiplier.** `iSkillPointsTagSkillMult` = 1 in both games (FO3 0x554f38, NPC auto-calc). It has no effect on the player's screen.
- **Today.** `Levelling::skillPoints` (`levelling.cpp:121`) matches both formulas, including the NV even-level rule (the code comments say "the level argument is a guess"; the exe confirms it is the new level). Entry point 10 handling is outside this part.
- **Round check.** Int 5, first level-up to level 2: NV 13, FO3 15. Level 3: NV 12, FO3 15. Int 7 at level 2: NV 14, FO3 17. Tolerance exact. Do the check without Educated or similar perks.
- **Confidence.** Exe-read (NV and FO3 formulas). The even-level +1 reading is from the NV helper at 0x648c10 (instructions at 0x648c42 to 0x648c6d).

### E.1.14 Perk rate

- **FO3.** A perk on every level. The level-up menu code (0x657190) has no modulus test, and `iLevelsPerPerk` has no FO3 setting. (Earlier notes in memory file m6-round4a agree.)
- **NV.** `level % iLevelsPerPerk == 0` (NV 0x784f34 to 0x784f47), with the new level. `iLevelsPerPerk` is not in `FalloutNV.esm`, so the exe default 2 applies (static initialiser 0xf5c756). Always one pick.
- **TTW.** `TaleOfTwoWastelands.esm` sets `iLevelsPerPerk` = 1, so TTW gives a perk on every level. This is a data difference inside "TTW uses NV rules". (Part F adds that TTW Hardcore sets `iLevelsPerPerk` to 2 through a script; see F.R17.)
- **Today.** `LevellingRules` reads `iLevelsPerPerk` for NV (`levelling.cpp:71`) and fixes FO3 to 1 (`levelling.cpp:62`). Matches, including TTW once the game settings are loaded from the TTW load order.
- **Round check.** NV: first perk screen at level 2, none at level 3, next at 4. FO3: a perk at level 2 and 3. TTW: a perk at 2 and 3 (and with 6 levels gained at once, check each). Exact.
- **Confidence.** Exe-read (NV and FO3). Data-read (TTW setting; the ESM value was read from `ttw_output/TaleOfTwoWastelands.esm`).

### E.1.15 Level cap

| Game | Cap | How |
|---|---|---|
| FO3 | 20 | exe default (static init 0xf62308, default 20). `Fallout3.esm` has no `iMaxCharacterLevel` |
| FO3 + Broken Steel | 30 | `BrokenSteel.esm` setting = 30 |
| NV | 30 | `FalloutNV.esm` setting = 30 |
| NV + add-ons | 30 + 5 per add-on, max 50 | NV 0x7d5941 to 0x7d598e: `min(max(current + 5 × count, 30), 50)`. The count comes from the plugin file-name prefixes matched in `DataHandler::LoadPlugins` 0x4624b0 (DeadMoney, HonestHearts, OldWorldBlues, LonesomeRoad) |
| TTW | 50 in practice | `TaleOfTwoWastelands.esm` sets 60. The block above gives min(max(60 + 20, 30), 50) = 50 (E.unclear 6). `Fallout3.esm` in the TTW folder has no level setting |

- **Today.** `Levelling::maxLevelForFiles` (`levelling.cpp:105`) gives 30 for the base game, +5 per add-on up to 50 for NV, and max(…, 30) for FO3 with Broken Steel. This matches the table. For TTW it gives 50 because it ignores the 60. Matches (by the exact clamp rule rather than by reading the ESM value).
- **Round check.** NV with all four add-ons: the cap is 50 and XP stops at XP(50) = 49 × 200 + 150 × 49 × 48 / 2 = 186,200. FO3 base: cap 20 and XP stops at 29,450. Exact.
- **Confidence.** Exe-read (NV code). Data-read (ESMs). Inferred (the TTW runtime value).

### E.1.16 Tag skills and when the level-up screen shows

- **Tag skills.** Chargen chooses them. They live in the player's class record (4 slots, NV 0x5a5f40). Each gives +`fAVDTagSkillBonus` (15) while the class lists the skill (a live check, not baked in). The Tag! perk adds a slot through the perk system. `iSkillPointsTagSkillMult` = 1. The number of chargen picks is E.unclear 2.
- **Today.** The +15 is baked into the base value at chargen and added or removed by hand later (`falloutscripts.cpp:7036` to `7044`). The result is the same at creation. A later Tag! perk or untag works, but the player's class is not consulted.
- **Level-up screen.** NV: `AddExperience` raises XP. When XP ≥ XPfor(level + 1) and level < cap, the pending flag is set (0x8d55e0 writes 1 at `*(PC+0x878)` byte 0). The main loop (0x70c4a0, which tests byte 0x11d8905, from earlier notes) opens the screen (NV 0x784c80, FO3 0x657190). The screen shows once per level. After Done the level advances by one and the check runs again (0x8d5270, 0x8d53db). The menu shows the points computed above and, when the level qualifies, the perk list.
- **Today.** `PlayerProgress::awardNextLevel` (`playerprogress.cpp:129`) grants one level at a time with a skill-point count and a perk flag. Matches in shape.
- **Round check.** Give exactly XP(2) − 0 = 200 XP. Expect one level-up screen for level 2, with 13 (NV) or 15 (FO3) points at Int 5. Give 550 XP from level 1: two screens in sequence.
- **Confidence.** Exe-read for the check and the menu points. Inferred for "one screen per level, then re-check".

### E.numbers Expected numbers a round can check

SPECIAL 5 each, no tags, no perks or traits, level 1, unarmoured.

| Quantity | FO3 | NV and TTW | Tolerance | Why |
|---|---|---|---|---|
| Max HP | 200 | 200 | ±0.5 | exact float sum; allow display rounding |
| Max HP at level 2 | 210 | 205 | ±0.5 | per-level term 10 / 5 |
| Action points | 75 | 80 | ±0.01 | exact |
| Carry weight | 200 | 200 | ±0.01 | exact |
| Crit chance | 5 | 5 | ±0.01 | exact |
| Melee damage bonus | 2 | 2 | ±0.01 | floor of 2.5 |
| Poison resist / rad resist | 20 / 8 | 20 / 8 | ±0.01 | exact |
| Each skill, untagged | 15 | 15 | ±0 | integer; ceil(2.5) = 3 |
| Each skill, tagged | 30 | 30 | ±0 | +15 |
| XP to reach level 2 / 3 | 200 / 550 | 200 / 550 | ±0 | integer |
| Skill points at level 2 (Int 5) | 15 | 13 | ±0 | integer |
| Skill points at level 3 (Int 5) | 15 | 12 | ±0 | NV even-level rule |
| Perk at level 2 / 3 | yes / yes | yes / no (TTW: yes / yes) | exact | rate rule |
| Level cap | 20 (30 with Broken Steel) | 30 + 5 per add-on, max 50 (TTW 50) | exact | exe clamp |

Do not mix sources of variation in one check. Temporary effects, perks, traits, the Hardcore mode and difficulty settings all change these numbers.

A second SPECIAL for a non-trivial check: S7 P6 E5 C4 I7 A6 L5. Expected: NV AP 83, FO3 AP 77; carry 220; crit 5; melee 3; HP 200; skills as in E.1.9; skill points at level 2: NV 14, FO3 17.

### E.today Today's engine, in one table

| Rule | Status | Where |
|---|---|---|
| Health (player) | matches, except the level source | `derivedstats.cpp:53`, `falloutscripts.cpp:4412-4414` |
| AP, carry, crit | match; use the permanent, not the current, attribute | `derivedstats.cpp:68-83`, `falloutscripts.cpp:4373-4377` |
| Melee damage | mismatch: no floor | `derivedstats.cpp:78` |
| Unarmed damage | missing | `actorvalues.cpp:55` (not in `isDerived`) |
| Poison, rad resist | missing | `actorvalues.cpp:55` |
| Fatigue | missing (NV only; probably not needed) | |
| Skills initial | mismatch: Luck rounds down | `chargen.cpp:432` |
| Skills follow SPECIAL | mismatch: frozen at chargen | `falloutscripts.cpp:7010-7014` |
| Starting values for unknown AVs | SPECIAL 5, skills 15, health 100 | `actorvalues.cpp:39-51` |
| XP formula and cap | matches | `levelling.cpp:45`, `playerprogress.cpp:50` |
| Skill points per level | matches | `levelling.cpp:121` |
| Perk rate | matches | `levelling.cpp:62`, `:71`, `:143` |
| Level cap | matches | `levelling.cpp:61`, `:105` |
| Level source | differs: XP-derived, not stored | `playerprogress.cpp:92`, `:129` |
| Skill points | stored in `PlayerProgress`; the exe computes them when the screen opens | `playerprogress.cpp:129` |
| Tag skills | baked into the base; the exe reads the class | `falloutscripts.cpp:7036-7044` |

### E.design Suggested design for step 4 (from these findings)

- Make the player's derived stats live. Skills (AV 32 to 45), poison and rad resist, unarmed damage and melee damage should be computed on read, from the current SPECIAL, the level and the settings, as the exe does. Keep spent skill points, books and perk changes as a permanent modifier on top.
- Read `fAVDSkillPrimaryBonusMult` (default 2.0), `fAVDSkillLuckBonusMult` (default 0.5), the per-skill bases, `fAVDTagSkillBonus` and the resist settings from the GMST store, with the exe defaults above when the ESM lacks them.
- Keep the two attribute readers separate: the level-up value (base + permanent) for health and skill points, and the current value (including temporary effects) for AP, carry weight, crit chance, melee damage and skills.
- Use the AV clamp for skills (0..100).

### E.unclear Open items in part E

1. Base health term. The spec reads the base health as the record's DATA health (player: 100) added through AV flag 0x40. That path was decoded (NV 0x8803a0, FO3 0x6f4db0). The base-form getter it calls (`vt+8` of the base-form actor-value object) was not decoded, so "refBase = record health" is inferred. It matches the 200 HP seen in play and the online wikis.
2. Tag slot count at character creation. The class record has 4 tag slots (TESClass +0x44 to +0x50, read by NV 0x5a5f40). The number the chargen screen allows (3, plus a 4th from the Tag! perk) was not found in the exe. OpenFallout uses 3 (`chargen.hpp:90`). This is inferred from play knowledge only.
3. When the stored player level changes. The exe reads the level from a stored field (PlayerCharacter level via `vt+0x28` / 0x87f9f0). The "level up pending" test is NV 0x8d55e0. Settled in step 4a: the level goes up when the level-up screen closes (0x7851F2 calls 0x8D5270, which stores level + 1 and runs the pending test again). NV 0x8d5300 sets XP to XPfor(L−1) and the next threshold to XPfor(L+1); which command or flow calls it was not identified. A round should check whether max HP rises before or after the level-up screen closes.
4. Does the current HP rise with max HP at level-up? Not traced.
5. Damage resistance and damage threshold (AV 18 and AV 76, NV 0x8a0a90 and 0x8a0ad0, FO3 0x6f6ba0) are armour sums. They are not derived from SPECIAL and are outside this part. They were not decoded.
6. TTW level cap. `TaleOfTwoWastelands.esm` sets `iMaxCharacterLevel` to 60. NV code at 0x7d5941 recomputes the setting as `min(max(current + 5 × dlcCount, 30), 50)`. With all four NV add-ons that is 50. It was not run. If that block does not run before XP is added, the cap would be 60.
7. NV Big Guns (AV 33) and FO3 slot 44 ("UNUSED01", Throwing). In NV, AV 33 has no calc function and is flagged 0x2000, so it is not derived. In FO3, AV 44 has no calc function either. FO3's table at 0xf5e0ec maps slot 44 to Intelligence, but that is irrelevant because no function is registered.
8. NPC and creature formulas (the non-player branch of health, NPC auto-calc) are only sketched. Step 4b follows. The NPC auto-calc is FO3 0x554dc0 and NV 0x603be0, and uses the same formulas plus the class tags.
9. FO3 rounding functions 0xc00d40 (floor) and 0xc03f30 (ceil) were identified by their instruction pattern, not by a named symbol. The same pattern in NV (0xec6940 floor, 0xec9e10 ceil) matches the known NV initial value of 15. A round should confirm FO3 at Luck 5 or 7.
10. Fatigue (AV 22) is derived in NV only (0x643820): `90 + 20 × End + 10 × (level − 1)` from exe defaults, with no ESM override. FO3 has no calc function. Not needed for step 4 unless Hardcore uses it.

---

## F. Hardcore (New Vegas only)

Hardcore is part of PR 4c. This part is the rule list that PR builds on. All addresses in part F are NV exe addresses. "Port" in the spec means OpenFallout at `9c0cdf6895`.

### F.0 Summary

- **NV only.** Hardcore exists only in New Vegas, and in TTW, which runs on NV. Fallout 3 has no hardcore state, no DEHY, HUNG or SLPD records, no `fHC*` settings and no hardcore branch in its exe. Every rule below is NV-only.
- **The mode** is one integer in the player (`player+0x7BC`; hardcore = exactly 1) plus one byte (`player+0xE38`, "always hardcore", the alias `behemoth` in scripts). The byte stays 1 from the start of a game until the mode is turned off once. The only ESM-driven use of that byte is the Hoover Dam endgame scripts, which give achievement 30.
- **The mode is offered once**, at the end of Doc Mitchell's "Doc Outro" dialogue (INFO 0x1057E7). A result script activates a paper activator that shows a two-button box (`VCG01CasualHardcoreMessage`: button 0 "No (Recommended)", button 1 "Yes"). Its script calls `SetHardcore 1` or `SetHardcore 0`. The Gameplay options menu has a "Hardcore Mode" Off/On switch that can change it at any time, in either direction. The exe has no rule that stops it being switched on again.
- **Three needs** are three actor values of the player: Dehydration (AV 73, HUD "H2O", Pip-Boy "H20"), Hunger (AV 74, "FOD"), SleepDeprevation (AV 75, "SLP"). Each is a level that goes up by whole points as game time passes. It goes down with food, drink, chems, sleep and a few spells. A point is added each `rate × TimeScale / 60` game minutes. The rates are settings (10 / 25 / 50). At the default time scale 30 that is 12 / 4.8 / 2.4 points per game hour, and exactly 1 point per 10 / 25 / 50 real seconds at any time scale. The exe checks nothing but the clock, so waiting, sleeping and fast travel add points simply because game time jumps.
- **A need has five stages** (levels 200, 400, 600, 800, 1000). Entering a stage swaps the player's stage spell (SPEL) and tells the player with two lines of text. Stages 200 to 800 lower SPECIAL attributes in steps of 1 to 3. Stage 1000 is a spell that does 10000 health damage, i.e. death.
- **Sleeping** (the "sleeping" flag, not waiting) takes `min(SLP, 60)` off the sleep level for every hour slept (`fHCSleepRestorationMod`, exe default 60). It also stops sleep deprivation rising while it lasts. In hardcore the end-of-sleep full restore of Health, Fatigue and all limb conditions (0x8A0960) is skipped, so sleep no longer mends limbs.
- **Healing differs by data, not by code.** The Stimpak, Super Stimpak, Doctor's Bag, Rad-Away and about 180 foods and drinks have effects with an `IsHardcore` condition (condition function 586 = `IsHardcore`). Hardcore Stimpak = `RestoreHealth` 5 per second for 6 s (6 with Fast Metabolism), with no limb repair. Rad-Away = 5 per second for 10 s. Food and drink add `RestoreStarvationLevel`, `RestoreDehydrationLevel`, `DamageDehydrationLevel` and `RestoreSleepDeprivationLevel` effects only in hardcore.
- **Ammunition has weight only in hardcore** (0x48EBC0, the AMMO weight at DAT2+8, form `+0xB8`). 10mm is 0.033 per round (3.3 for 100).
- **A companion (player teammate) is essential unless hardcore is on** (`Actor::IsEssential` 0x87F3D0). There is no other companion-specific code in the exe. Two quest scripts just branch on `IsHardcore`.
- **HUD.** A rectangle `HardcoreMode` in `hud_main_menu.xml` holds five labels H2O, FOD, SLP, RAD, LMB (the first three only in hardcore). A label is hidden below 200, half alpha (0x7C) from 200, full alpha from 400, red from 600, red and flashing from 800. The Pip-Boy STATUS screen has three extra pages (H2O, FOD, SLP) in `stats_menu.xml`, shown only in hardcore.
- **TTW.** The base needs, stage, ammo, healing and companion records are unchanged. TTW adds script-side behaviour on top (a second prompt in Vault 101, `ToggleHardcoreTracking` calls around scripted time jumps, VATS and levels-per-perk settings while hardcore is on, and an optional patch `bHardcoreNeedsFix`). The engine has none of the TTW extras.
- **Port verdict.** The port already implements nearly all of this (M16 work). The differences found:
  1. the needs timer skips on the wrong player control (port: Fighting; exe: Pip-Boy);
  2. AMMO records with a 12-byte DAT2 lose their weight in the loader (`.22LR` and 10 more);
  3. `ToggleHardcoreTracking`, `GetHardcoreTracking` and the stage-threshold commands (JIP, used by TTW) are missing;
  4. minor: clock-went-backwards handling, global 0x11A0294, TTW prompt extras.

### F.keys Key values

Values are from `FalloutNV.esm`, else the exe default. TTW's merged `FalloutNV.esm` has the same values.

| Setting | Value in `FalloutNV.esm` | Exe default (init 0xF65680…) | Used |
|---|---|---|---|
| `fHCDehydrationRate` (obj 0x11D0C48) | **absent** | **10.0** (0x1017B78) | 0x969C6E |
| `fHCStarvationRate` (0x11D1014, FormID 0x162BC9) | **25.0** | 0.39 | 0x969C9E |
| `fHCSleepDeprivationRate` (0x11D1440, 0x162BCA) | **50.0** | 0.26 | 0x969C86 |
| `fHCSleepRestorationMod` (0x11D0C24) | absent | **60.0** | 0x94E0ED |
| `fHCHalfAlphaTextThreshold` (0x11D05A4) | absent | 200.0 | 0x773CDD, 0x773F60 |
| `fHCFullAlphaTextThreshold` (0x11D0C84) | absent | 400.0 | 0x773CC1, 0x773F25 |
| `fHCRedTextThreshold` (0x11D132C) | absent | 600.0 | 0x773CA3, 0x773EE7 |
| `fHCFlashTextThreshold` (0x11D07FC) | absent | 800.0 | 0x773C86, 0x773EA2 |
| `sDehydrationIncrease` / `Decrease` / `NotSick` | "Your dehydration level has increased." / "...decreased." / "You no longer have dehydration sickness." | same without the final dot | 0x8C5610 |
| `sHungerIncrease` / `Decrease` / `NotSick` | **absent** | "Your Hunger level has increased" / "...decreased" / "You no longer have Hunger sickness" | 0x8C5890 |
| `sSleepDeprevationIncrease` / `Decrease` / `NotSick` | "Your Sleep Deprivation level has increased" / "...decreased" / "You no longer have Sleep Deprivation sickness" | "Your SleepDeprevation level has ..." | 0x8C5B10 |
| `sRadiationSick` | absent | "You are now sick with" | 0x8C57EB |
| `sHardcore` | absent | "Hardcore Mode" | 0x7CD305 |
| `sStatsH20Abbrev` / `FOD` / `SLP` / `RAD` / `LMB` | absent | "H20" / "FOD" / "SLP" / "RAD" / "LMB" | menus |
| `TimeScale` GLOB 0x3A | 30.0 | | 0x867950 |

The dehydration rate 10 comes from the exe: the master has no entry. The port already uses 10 as a fallback (`apps/openfallout/mwscript/falloutinteraction.cpp:1415`), so it is right. If a plugin ever adds the setting, the port would pick it up as the exe would.

The DLC masters (Dead Money, Honest Hearts, Old World Blues, Lonesome Road, Gun Runners, the four pre-order packs) have no `fHC*`, no DEHY, HUNG or SLPD, no AMMO override and no AVIF for the needs. They only add scripts and spells (F.R18).

### F.R1 Hardcore state, commands, new game

- **Games.** NV only. FO3 has none (the port keeps it off: `FalloutScripts::setHardcoreMode` returns for non-NV, `falloutneeds.cpp:52-55`). TTW uses NV's.
- **Rule.** The player holds an int (`+0x7BC`; `IsHardcore` is true only for exactly 1) and a byte (`+0xE38`, `IsAlwaysHardcore`, alias `behemoth`). A new game (`PlayerCharacter` constructor 0x938B46) calls `SetHardcore(0, loading=0)` and then sets `+0xE38 = 1`. So a new game is not hardcore, and "always hardcore" is true until something turns it off.
- **Setter 0x969E90(value, loading).**
  1. It calls 0x5D43C0 → 0x418520 → 0x963B00 (a refresh of the player's process; the effect was not traced).
  2. If `value == 0 && loading == 0`: 0x5DE9B0 sets `+0xE38 = 0`. Then, if the global byte `[0x11A0294]` (via 0x7D6E60) is 0, each of the three needs AV 73, 75, 74 is lowered by its current value (the AV callbacks run, so stage spells and "decrease" messages fire).
  3. It stores `+0x7BC = value`.
- **Commands.**
  - `SetHardcore` opcode 0x124B, 0x5DE920: needs an arg in [0, 1], else logs "Attempting to set hardcore on an invalid player!" and returns false. It calls the setter with `(arg, 0)`. If `arg != 1` it calls 0x5DE9B0.
  - `IsHardcore` 0x124A → 0x4D1360 (`+0x7BC == 1`).
  - `IsAlwaysHardcore` / `behemoth` 0x126B → 0x5A6050 (`+0xE38`).
  - `DisableHardcoreTracking` / `babby` 0x126A, 0x5DE9D0 → `+0xE38 = 0`.
  - Condition function 586 (0x24A) is the `IsHardcore` handler.
  - Save: 0x9590F0 writes both. Load 0x95A3B0 and 0x95AE3D calls the setter with `loading = 1` and then, if the saved byte is 0, 0x5DE9B0. Load also sets player `+0xE39 = 1` (re-baseline, F.R5).
- **Settings.** None.
- **Exe.** 0x969E90 (store 0x969F86), 0x5DE920, 0x4D1360 (`cmp [eax+0x7BC],1` at 0x4D136A), 0x5DE9B0 (`mov byte [eax+0xE38],0` at 0x5DE9BA), 0x938F4E and 0x938F54 (new game: setter then `+0xE38 = 1`).
- **Today.** `components/falloutscript/hardcore.hpp:128-169` and `hardcore.cpp:195-256` implement all four commands, the two fields and the zeroing rule exactly (`set`, `command`, `reset`, `write/read`). New game reset: `falloutscripts.cpp:852-855`. Save section `hardcore`: `falloutsave.cpp:1164-1174`. Matches, except the global 0x11A0294 exception (F.unclear 7), which is not ported (`m16-needs-notes.md` "Not done").
- **Round check.** After a new game, `IsHardcore == 0` and `IsAlwaysHardcore == 1`. After `SetHardcore 1`, `IsHardcore == 1`. After `SetHardcore 0`, `IsAlwaysHardcore == 0`, the three needs are exactly 0, and a "decreased" line was shown for each need that had a stage. `SetHardcore 2` returns false and changes nothing. Tolerance 0.
- **Confidence.** Exe-read.

### F.R2 The Doc Mitchell prompt (turning it on at the start)

- **Games.** NV (and TTW with its own variant, F.R17).
- **Rule.** In Doc Mitchell's house (cell `GSDocMitchellHouse`, 0x103DF9), the quest `VCG01` ("Ain't That a Kick in the Head", 0x104C1C) has the dialogue topic `VCG01DocMitchellTopic076` ("Doc Outro", 0x1057DD). Its INFO 0x1057E7 has four lines ("You should talk to Sunny Smiles...", "I reckon some of the other folks...", "Anyway, you ever get hurt out there, you come right back...", "But try not to get killed anymore."). Its end result script is:
  `VCG01CasualHardcoreMessageREF.Activate player 1; EnablePlayerControls 1 1 0 1 1 1 0; set VGenericTimer.fTimer to 0.1; set VGenericTimer.nEvent to 3; StartQuest VGenericTimer`
  (its begin script just un-destroys the door: `GSDocMitchellHouseIntDoorREF.SetDestroyed 0`). The REF is 0x11F293 (`VCG01CasualHardcoreMessageREF`, activator 0x11F292 "Choose Game Mode", `paper02.nif`, in that cell at (1555.2, 12792.5, 7505.2)). Its script `VCG01CasualHardcoreMessageSCRIPT` (0x11F291): `OnActivate: ShowMessage VCG01CasualHardcoreMessage 1`. In `GameMode`: `nButton = GetButtonPressed; if nButton >= 0 && bModeSet == 0 { if nButton == 1 SetHardcore 1 else SetHardcore 0; bModeSet = 1; MarkForDelete }`.
- **Message** `VCG01CasualHardcoreMessage` (MESG 0x11E661, title "Hardcore Mode", buttons 0 "No (Recommended)", 1 "Yes", DNAM=1): "Fallout: New Vegas allows you to play in a new Hardcore mode that greatly increases the challenge of the game. In this mode, Stimpaks heal over time and cannot mend broken limbs, Rad-Away removes radiation over time, ammunition has weight, and dehydration is a constant concern. This mode is only recommended for advanced players. / You may turn Hardcore mode on or off at any time in the Gameplay menu, but if you activate Hardcore mode now and maintain it through the end of the main storyline, you will receive a special reward. Would you like to activate Hardcore mode?"
  Choosing "No" calls `SetHardcore 0` too, which clears `IsAlwaysHardcore` (F.R1). So "No" forfeits the achievement.
- **Other message hooks.** The `CGTutorial` and `VCGTutorial` scripts show `CGTutorialCrippleHardcore` ("You are crippled. Use a Doctor's Bag or find a doctor.") instead of the normal cripple tutorial when `player.IsHardcore`. The first-time help `HelpHardcoreNeeds` (MESG 0x190, also `HelpHardcoreNeedsJosh` 0x17370F) is in the exe's help list (0x46D42E). What triggers it was not traced.
- **Exe.** None beyond `SetHardcore`. This is all data.
- **Today.** Data-driven (the script engine runs INFO result scripts; `ShowMessage` and `GetButtonPressed` exist, `vanillacommands.inc:35,91`). No dedicated test was found for this box. Present, unverified by a round. The help message trigger was not found in the port.
- **Round check.** In `GSDocMitchellHouse`, run the Doc Outro INFO to its end. Expect a message box with the buttons {"No (Recommended)", "Yes"}. Press button 1. Expect `IsHardcore == 1`, REF 0x11F293 marked for delete, and the player controls enabled. Tolerance: exact strings.
- **Confidence.** Exe-read (commands) plus ESM-read (scripts, INFO, MESG, REFR).

### F.R3 The Gameplay menu switch

- **Games.** NV.
- **Rule.** Options > Gameplay has an enum row labelled `sHardcore` ("Hardcore Mode") with two values (the exe builds it at 0x7CD2E2..0x7CD317, row type 8, callback 0x7D13A0). The row is unconditional (no quest or game-state gate in the exe). Setting it calls 0x7D1400(value), which, for 0 or 1, calls the setter 0x969E90(value, 0), identical to the command (F.R1), including the zeroing of needs when set to 0 (and clearing `IsAlwaysHardcore`). Reading it returns `IsHardcore`. There is no rule that stops turning it back on. The "special reward" only needs the byte to stay 1, which turning it off clears for good.
- **Exe.** 0x7D13A0 (read: 0x7D13DA → 0x4D1360; write: 0x7D13CD → 0x7D1400). 0x7D1400 (calls 0x969E90 at 0x7D1416 and 0x7D142C).
- **Today.** `files/data/scripts/omw/falloutsettingsitems.lua:107` (`toggle('hardcore','sHardcore',OFF_ON,...)`), `apps/openfallout/mwlua/menuscripts.cpp:290,314`, `FalloutScripts::setHardcoreMode` `falloutneeds.cpp:50-58`. Test `apps/openfallout_tests/mwscript/testgameoptions.cpp:38-50`. Matches.
- **Round check.** Toggle on, off, on: `IsHardcore` 1, 0, 1. After "off" the needs are 0. `IsAlwaysHardcore` is 0 afterwards and stays 0 after "on".
- **Confidence.** Exe-read.

### F.R4 The achievement flag

- **Rule.** `IsAlwaysHardcore` (script alias `behemoth`) is read by exactly four quest scripts at the end of the main story: `vHDBattleController` (0x133075), `VHDLegionBattle` (0x137AB9), `VHDIndependentBattle` (0x154233), `VHDHouseBattle` (0x154234): `If (player.behemoth == 1) AddAchievement 30` (the line after a `;Hardcore Achievement` comment; the same scripts also call `AddAchievement 40`, `38` and so on for the ending itself). `AddAchievement` (0x114A, 0x5D3B20) has no hardcore test of its own.
- **Today.** The byte is kept, saved and returned (`hardcore.hpp:128-169`). `AddAchievement` handling is outside this part. Matches for the flag.
- **Round check.** Not worth a hidden round. A unit test: `IsAlwaysHardcore` is 1 after a new game and `SetHardcore 1`, and 0 after any off.
- **Confidence.** Exe-read plus ESM-read.

### F.R5 Need accrual (the tick)

- **Games.** NV only.
- **Rule.** `Player::UpdateHardcore` 0x969C30 runs once per frame from `PlayerCharacter::Update` (0x93F374, only if `IsHardcore`). It also runs once per game hour from the sleep/wait menu tick 0x7C0580 (0x7C067D, before 0x94DF80, so the needs see the hour that just passed). It is a method of the player.
- **Formula.**
  ```
  if (player+0x680 & 4)  return;                 // DisablePlayerControls 2nd argument (Pip-Boy) is on (0x5A03F0(4))
  T      = TimeScale GLOB value                  // 0x867950 reads (calendar+0x14)->value
  now    = GameDaysPassed * 24 * 60              // game minutes, 0x867EA0 ( x double 24.0 at 0x10356D8, x double 60.0 at 0x1012638 )
  I_h2o  = fHCDehydrationRate        * T / 60    // game minutes per point
  I_slp  = fHCSleepDeprivationRate   * T / 60
  I_fod  = fHCStarvationRate         * T / 60
  if (last_h2o == 0)  or  (player+0xE39 != 0):   // first run, or the "load/new game" flag
        last_h2o = last_fod = last_slp = now;  player+0xE39 = 0;   // (0x9590D0(0))  -> no accrual this call
  d_h2o = now - last_h2o;  d_fod = now - last_fod;  d_slp = now - last_slp   // floats
  if (d_h2o >= I_h2o)  { pts = trunc(d_h2o / I_h2o);  AV73 += pts (0x3B0 vtable);  last_h2o = now(re-read) }
  if (d_slp >= I_slp)  { pts = trunc(d_slp / I_slp);
                         if (!Player.sleeping)  AV75 += pts;                       // 0x5A1E50 = byte [player+0x658]
                         last_slp = now }                                           // updated even while sleeping
  if (d_fod >= I_fod)  { pts = trunc(d_fod / I_fod);  AV74 += pts;  last_fod = now }
  ```
  The remainder (`d - pts*I`) is thrown away each time a need is credited. The three `last_*` values and the "now" slots are static floats (0x11E0D74/78/7C last; 0x11E0D80/84/88 now). They are not saved. A game that is loaded or started sets `+0xE39 = 1` (0x958FD8, 0x95A3E6), so counting restarts from the load time: the days between two saves count for nothing.
- **The skip bit.** The tick returns while the Pip-Boy control is off: bit value 4 of player `+0x680` (the second argument of `DisablePlayerControls`). Exe mask values: 1 movement, 2 looking, 4 Pip-Boy, 8 fighting (0x5CAE30, memory M12-12AM). The M16 code and `m16-needs-notes.md` label this "Fighting control (bit 4)", which is wrong.
- **Settings.** The three rate settings, `TimeScale`, `GameDaysPassed`, player flags `+0x680`, `+0x658`, `+0xE39`.
- **Exe.** 0x969C30. Key instructions: 0x969C86, 0x969C9E, 0x969C6E (rate × scale / 60), 0x969D5B..0x969D63 (compare), 0x969D70 (`ftol`), 0x969D8E (`call [vtbl+0x3B0] with (AV, pts, 0)`), 0x969DC9 (sleep gate), 0x969DFD and 0x969E4E (stamp), 0x969C46 (Pip-Boy skip).
- **Consequences.**
  - (a) The interval in real time is `rate` seconds per point at any steady time scale.
  - (b) A time jump (wait, sleep, fast travel, `SetGameHour`) credits `trunc(jump / I)` points at the next call.
  - (c) Because the remainder is dropped, hourly menu ticks credit `trunc(60 / I)` per hour: 12, 4 and 2 at time scale 30, not 12, 4.8, 2.4.
  - (d) While the Pip-Boy control is disabled, the tick is skipped without moving the stamps, so the time spent is credited as a burst when the control comes back (unless something re-baselines).
- **Today.** `HardcoreNeeds::update` (`hardcore.cpp:108-133`) and the host `FalloutScripts::updateHardcore` (`falloutinteraction.cpp:1402-1434`; called per frame `falloutprogress.cpp:397`, per rest hour `falloutinteraction.cpp:1495`, per trip hour `falloutinteraction.cpp:1325`). The interval formula, truncation, remainder drop, sleep gate, first-call baseline and clear on load all match. Differences:
  1. Wrong control bit: `falloutinteraction.cpp:1420` skips while `PlayerControl::Fighting` is off. The exe tests bit value 4, which is the Pip-Boy control. The port's own mask is by index (PipBoy = bit 1). Fix: test `PlayerControl::PipBoy`.
  2. Time going backwards: the port re-baselines (`hardcore.cpp:118`). The exe does not (negative delta, no accrual until the clock catches up). Cosmetic.
  3. The port skips the tick when god mode is on (clears the baseline, `falloutinteraction.cpp:1423-1429`) and when the player is dead. The exe has no such test at this level (god mode only rejects the fatal damage later). Cosmetic, deliberate.
- **Round check.** See [the expected numbers](#fnumbers-expected-numbers-nv-default-time-scale-30-hardcore-on-needs-start-at-0).
- **Confidence.** Exe-read (also `docs/openfallout/m13-exe-reads.md` item 21 agrees).

### F.R6 Rates and times (derived numbers)

At the default `TimeScale` 30 (GLOB 0x3A):

| Need | AV | Rate | Interval (game min) | Points per game hour, continuous | Points per 60-min menu tick | Real seconds per point | Real time to 1000 | Game time to 1000 |
|---|---|---|---|---|---|---|---|---|
| Dehydration (H2O) | 73 | 10 | 5.0 | 12.0 | 12 | 10 | 2 h 46 m 40 s | 83.3 h |
| Hunger (FOD) | 74 | 25 | 12.5 | 4.8 | 4 | 25 | 6 h 56 m 40 s | 208.3 h |
| Sleep (SLP) | 75 | 50 | 25.0 | 2.4 | 2 | 50 | 13 h 53 m 20 s | 416.7 h |

Stage 1 (200) arrives after 2000, 5000 and 10000 real seconds. The rates are intervals: a larger value is slower. Today's engine: `HardcoreNeeds::mRates{10,25,50}` default (`hardcore.hpp:122-123`), set from the settings (`falloutinteraction.cpp:1415-1416`). Matches. Confidence: exe-read.

### F.R7 Sleep restoration and the end of sleep

- **Rule.** Each hour of a sleep or wait is one call of 0x94DF80 (from the rest menu tick 0x7C0580, and from 0x942421 and 0x9469FB for sleep scripts). It decrements the hours left (`player+0x654`) and advances effects by `3600 / TimeScale` seconds (0x823C40, 0x88B510 heal). If hardcore and the player is sleeping (`player+0x658`, not just waiting), it lowers AV 75 by `min(AV75_current, fHCSleepRestorationMod)` (0x94E0D1..0x94E13A). Waiting never lowers SLP.
  When the last hour is done, if the sleeping flag was set and not hardcore, 0x8A0960 restores Health (AV 16 damage) and Fatigue (AV 22) and sets limb conditions 25-31 to the full value (`[0x1013974] = 1000`). In hardcore this is skipped, so a long sleep restores no limbs and no "rest" health beyond the normal per-hour heal-rate pass. Scripted `ResetHealth` (0x5C6B60) has no hardcore test and still heals everything: Doc Mitchell, Julie Farkas and companion scripts use it.
- **Settings.** `fHCSleepRestorationMod` 60 (exe default).
- **Exe.** 0x94DF80 (sleep gate 0x94E0C5, min 0x94E0FC..0x94E119, apply 0x94E13A; completion 0x94E1A8..0x94E1BA), 0x5A1E50, 0x8A0960.
- **Today.** `HardcoreNeeds::sleepHour` (`hardcore.cpp:135-143`). Call at `falloutinteraction.cpp:1514` (only `sleeping && hardcoreOn()`). Sleep gate in the tick `hardcore.cpp:125-126`. Whether the port skips the full limb restore at the end of a hardcore sleep was not checked (the rest-completion code is outside `falloutneeds.cpp`). Needs and limb restore: unchecked.
- **Round check.** From zero needs, sleep 8 h in a bed at time scale 30: SLP 0 (accrual suppressed; the reduction is capped by the level), H2O 96 (12×8), FOD 32 (4×8). From SLP 300, sleep 3 h: 300 − 3×60 = 120 (plus nothing added). Tolerance: H2O and FOD ±1 per hour of float rounding (the hourly `trunc(60/I)` can drop to 11 and 3 when the float game-minute differs by an epsilon). SLP exact. Confirmed live in an M16 round (`m16-needs-notes.md`: 8 h sleep, dehydration 96, hunger 33, sleep 0).
- **Confidence.** Exe-read.

### F.R8 Stages, the stage records, the spells and the messages

- **Records** (all in `FalloutNV.esm`): `DEHY` Water1..5 (0x161901-0x161905), `HUNG` hunger1..5 (0x161906-0x16190A), `SLPD` sleep1..5 (0x16190B-0x16190F). Each `DATA` = `uint32 threshold, FormID spell`. Thresholds 200, 400, 600, 800, 1000.
- **Selection.** After any change of the AV, a callback (0x8C5610 H2O, 0x8C5890 FOD, 0x8C5B10 SLP) takes the stage = the first record in the record list whose threshold <= value (0x4018B0, 0x402030, 0x402760: `fild threshold; fld value; fcompp; test ah,1` skip if value < threshold), for the old and the new value. If the old and new stage differ: remove the old stage's spell, add the new one (0x824400 remove from `player+0x94`, vtable `+8` add on `+0x88`). If `new == old`, nothing. For the player only (`cmp ecx,[0x11DEA3C]`):
  - message 1 (always when the stage changed): the "Increase" text if the change was positive, else the "Decrease" text. With the in-pain Vault Boy (`glow_message_vaultboy_in_pain.dds`) for up, `glow_message_vaultboy_very_happy.dds` for down. 2 seconds (`[0x10162C0]` = 2.0).
  - message 2: if a stage is now active: `sprintf("%s %s", sRadiationSick, stageSpellName)` = "You are now sick with Minor Dehydration" (in pain). Else the "NotSick" text (very happy).

  The list order matters only if the records were unsorted. They are by ascending form id, i.e. 200..1000. The selector would take the lowest-threshold stage first if the list were ascending: unverified. This suggests the list is walked from the highest. The port sorts ascending and picks the highest stage whose threshold <= value (`hardcore.cpp:82-90`), which is the intended result. Treat "highest threshold <= level" as the rule. The exact list order of the exe was not read.
- **Stage spells** (SPEL, NV 0x15FA1C-0x15FA2A), all effects duration 0:

  | Level | Dehydration (SPIT type 1, 0x15FA1C..1F) | Starvation (0x15FA21..24) | Sleep dep. (0x15FA26..29) |
  |---|---|---|---|
  | 200 | Minor Dehydration: Endurance -1 | Minor Starvation: Strength -1 | Minor Sleep Dep.: Agility -1 |
  | 400 | Adv.: Endurance -2, Perception -1 | Adv.: Strength -2, Charisma -1 | Adv.: Agility -2, Intelligence -1 |
  | 600 | Crit.: Endurance -3, Perception -2, Intelligence -1 | Crit.: Strength -3, Charisma -2, Perception -1 | Crit.: Agility -3, Intelligence -2, Endurance -1 |
  | 800 | Deadly: Endurance -3, Agility -2, Perception -2 | Deadly: Strength -3, Charisma -2, Perception -2 | Deadly: Agility -3, Endurance -2, Intelligence -2 |
  | 1000 | Fatal Dehydration (SPIT type 4): DamageHealth 10000 (0x15FA20) | Fatal Starvation: DamageHealth 10000 (0x15FA25) | Fatal Sleep Dep.: DamageHealth 10000 (0x15FA2A) |

  The reduce effects are the standard `ReduceEndurance`, `ReduceAgility` and so on (value modifiers, AV 7, 10, 6, 9, 5, 8), with the magnitude as given. Stage 800 is not a superset of stage 600 for dehydration (Intelligence drops out, Agility enters) and for sleep (Intelligence stays, Endurance goes 1 → 2).
- **AV direction.** `Damage*Level` MGEFs (flags 0x1000075, hostile and detrimental) add to the level. `Restore*Level` (0x70) subtract. MGEFs: `DamageDehydrationLevel` 0x162BCB, `RestoreDehydrationLevel` 0x161F36, `DamageStarvation` 0x162BCD, `RestoreStarvationLevel` 0x162BCC, `DamageSleepDeprivationLevel` 0x162C8D, `RestoreSleepDeprivationLevel` 0x162C8E. AVIF: 0x5F7 AVDehydration "H20" (ANAM H20), 0x5F8 AVHunger "Starvation" (FOD), 0x5F9 AVSleepDeprevation "Sleep Deprivation" (SLP). The sign mapping is consistent with the callback's `delta > 0 = "increased"`. The AV-table sign flag was not traced (F.unclear 5).
- **Death at 1000.** Reaching stage 1000 adds the Fatal ability whose effect is `DamageHealth` magnitude 10000 duration 0 on AV 16 (a normal health damage: god mode rejects it, `m13-exe-reads.md` item 21). Wait, sleep and travel are not refused in advance (F.unclear 3).
- **Today.** `hardcoreStages` (`falloutinteraction.cpp:1380-1399`) reads DEHY, HUNG and SLPD from the ESM4 stores. `chooseStage` (`hardcore.cpp:65-106`). Text lines `noticeLines` (`hardcore.cpp:50-63`) and `needNotice` (`falloutneeds.cpp:65-83`: hunger, sleep and dehydration text via `gameSettingText`, "sRadiationSick" + name, faces 1 and 3, 2.0 s). The stage listener calls `mItemUse.checkHealth` so a fatal stage kills (`falloutscripts.cpp:580`). Matches. For the `NotSick` and first-seen-level cases: the port does not announce the first level seen after a load (`hardcore.cpp:103-105`). The exe's callback fires only on AV changes and a load restores AVs without callbacks, so it is also silent. Consistent.
- **Round check.** Set the dehydration AV from 199 to 200 (for example by a drink). The player has spell 0x15FA1C, Endurance current = base − 1, and two messages: "Your dehydration level has increased." then "You are now sick with Minor Dehydration". At 1000 the player dies. Tolerance: exact attribute deltas; message text exact.
- **Confidence.** Exe-read (callbacks, formats) plus ESM-read (records, spells).

### F.R9 What reduces (or raises) each need: data

- **Mechanism.** Ordinary `ALCH` and `SPEL` effects with a condition `IsHardcore == 1` (function 586), so they do nothing outside hardcore. 179 ALCH and 11 SPEL records in `FalloutNV.esm` carry that condition (DeadMoney 30 ALCH + 4 SPEL + 2 INFO + 2 MESG, HonestHearts 8 + 3, OldWorldBlues 6 + 7, LonesomeRoad 12 + 13, ClassicPack 1 SPEL). Full dumps: `tmp-hardcore/items_delta.txt` (125 items with a need effect or a hardcore difference), `alch_hc.txt` (all).
- **Magnitudes are instant (duration 0) level changes**, for example:

  | Item | Hardcore need effects | Other |
  |---|---|---|
  | Purified Water / Dirty Water / Irradiated Water | Dehydration -50 (and a second -7 with perk Camel of the Mojave 0x167E26) | +2 health/5 s; Dirty +6 rads, Irradiated +16 rads |
  | Water spells `WaterHeal1Purified`..`WaterHeal1Rads500` | Dehydration -200 (-30 more with Camel) | rads 0 / 4 / 6 / 10 / 20 / 500 for Purified / Good / Average / Bad / Terrible / Lethal |
  | Fresh fruit and vegetables (Apple, Carrot, Pear, Potato, Banana Yucca, Agave, Prickly pear...) | Dehydration -10 or -15, Hunger -5 .. -15 (Maize -40) | |
  | Cram-type foods, steaks, meats | Hunger -15 .. -80 (Bighorner Steak -75, Brahmin Wellington -100, Cook-Cook's Fiend Stew -80, Caravan Lunch -200 and Dehydration -15) | |
  | Gecko Kabob / Bloatfly Slider / Desert Salad | Hunger -75 .. -80 and Dehydration -30 .. -50 | |
  | Pork N' Beans, Potato Crisps | Hunger -40 / -10, Dehydration **+20 / +10** (salty) | |
  | Beer / wine / whiskey / Scotch / vodka / moonshine | Dehydration **+15 .. +25** | alcohol buffs as usual |
  | Nuka-Cola family, Sunset Sarsaparilla | Nuka-Cola Dehydration +10, Sleep **-15**; Ice Cold Nuka-Cola Sleep -60; Quantum / Atomic Cocktail Sleep -100 (Dehydration +10 / +30); Sunset +5 | |
  | Coyote Tobacco Chew | Sleep -30 | |
  | Cateye / Victory / Quartz | Sleep -15, Dehydration +10 | |
  | `*RestUp` spells (`VMQ03RestUp` 0x1768BC, `NVDLC01..04RestUp`) | all three needs -10000 (-1000 in LR) | full health too; cast by quest scripts (`Player.CIOS NVDLC01RestUp` "Hardcore Mode Reset") |
  | `NVDLC01AdrenalineShot` | Sleep -10000 | |
  | Pre-order canteen `PreordVault13CanteenHeal` | Dehydration -25 | +15 health |

  No item removes a stage spell directly (the callbacks do that). There are no stage-spell Hardcore-specific cures other than these.
- **Radiation interplay.** None in the tick. Dirty and irradiated food and drink add rads (`DamageRadiationLevel`, AV 54) in both modes. `sNoSleepInRadiation` and `sNoWaitInRadiation` refuse sleep and wait while an irradiating region or effect is active in both modes. Rad-Away differs by mode (F.R10). The HUD "RAD" label always shows (F.R12). Dead Money's toxic cloud damage is hardcore-only (F.R16).
- **Today.** All of it is data read by the generic effect code (`ItemUse`, `Magic`). Condition 586 uses the "same handler as the command" (`m16-needs-notes.md`). Not run on real items in a round ("food and drink effects beyond the data conditions are not done"; `m16-needs-notes.md` "Not done"). Present, unverified. Water-source drinking and `WaterHeal*` application: missing (F.unclear 4).
- **Round check.** With hardcore on and H2O 300, drink Purified Water: H2O 243 (300 − 50 − 7 with the Camel perk, else 250). Eat a Brahmin Wellington at FOD 400: 300. Tolerance exact (instant integer effects), ±0 after clamping at 0.
- **Confidence.** ESM-read. The mechanism is exe-read.

### F.R10 Healing over time, limbs and Rad-Away

- **Hardcore Stimpak (0x15169).** `RestoreHealth` (MGEF 0x14E, value modifier AV 16) magnitude 5, duration 6 if not Fast Metabolism (PERK 0x94EBF). Magnitude 6, duration 6 with Fast Metabolism. Non-hardcore: `RestoreHealthStimpak` (MGEF 0x9FE63, archetype 34, "Restore Health & Conditions") instant 30 / 36, which also repairs limb conditions. A duration effect applies `magnitude × dt` per update (`ValueModifierEffect::Update` 0x82C660, `fmul [ebp+8]` at 0x82C6F5; the update also logs "Using a food?" for AV 16 on the player). So the hardcore Stimpak = 5 HP/s for 6 s = 30 HP total (36 with the perk): the same total, paid out over time, with no limb repair. `dt` is simulation time, not game time. The same code makes every `RestoreHealth` food item per-second: Fresh Apple `RestoreHealth 1/7 s` = 7 HP.
- **Super Stimpak (0xCCEF2).** Hardcore `RestoreHealth` 20 for 3 s (24 with Fast Metabolism) = 60 (72), plus `SuperStimpakEffect` (30 s debuff). Non-hardcore instant 60 / 72 including limbs. Auto-Inject variants (LR): 18 / 22 for 3 s.
- **Doctor's Bag (0xCB05C).** Non-hardcore effect `RestoreAllLimbs` (script archetype). Hardcore: `NVRestoreLimbs` (MGEF 0xFFCA0, archetype 35) magnitude 36 (45 with Fast Metabolism): limb condition points (F.unclear 6).
- **Rad-Away (0x15167).** Non-hardcore: `RestoreRadiationLevel` 50 instant (+10 with perk `RadAwayChallengePerk` "Free Radical" 0x15EADA). Hardcore: 5 for 10 s (= 50 total, per second) and the +10 instant with Free Radical. (AV 54.)
- **What heals a crippled limb in hardcore** (exe and data): the Doctor's Bag. The scripted `ResetHealth` (0x5C6B60, no hardcore test) used by doctor NPCs (Doc Mitchell `GSDocMitchellScript`, Julie Farkas script) and by companion scripts after combat. Dead Money's and Lonesome Road's doors (`NVDLC04MojaveDoorSCRIPT` "heal hardcore players" before travel) and the Auto-Doc and `RestUp` spells (restore health and needs; `RestUp` itself only `RestoreHealth` + needs, not limbs). Sleep does not heal limbs in hardcore (F.R7). The wiki line "sleeping in certain beds" is not supported by the exe (some bed scripts could call `ResetHealth`; a scan found none that does for the player).
- **Fast Metabolism's** 20% is the data above (30 → 36, 5 → 6), not a code multiplier.
- **Today.** All data. The generic effect code ticks `RestoreHealth` as magnitude per second (`ItemUse` per-second handling, `m16-needs-notes.md` "Stimpaks ... are data"). Present, not run with a Stimpak in a round ("no item was used").
- **Round check (hardcore, health 50/200, no Fast Metabolism).** Use a Stimpak: after 1 s +5 (tolerance ±1), after 3 s +15, after 6 s +30 (±2), after 8 s still +30. No limb condition changed. `GetHasMagicEffect RestoreHealth` is true during the 6 s and false after. Super Stimpak: +20/s for 3 s. Rad-Away at 200 rads: −5/s, after 10 s −50 (±1). Non-hardcore: +30 on use at once and crippled limbs restored.
- **Confidence.** Exe-read (per-second rule, conditions) plus ESM-read.

### F.R11 Ammo weight

- **Games.** NV only (FO3 AMMO has weight but no hardcore gate; the FO3 exe weight function has none).
- **Rule.** `GetWeight(form, hardcore)` 0x48EBC0 switches on the form type. For AMMO (type 0x29) it returns `form+0xB8` (the DAT2 weight) only if `hardcore`, otherwise 0 (falls to the default 0x1012054). Every caller passes `IsHardcore`: item and inventory weight (0x4D1266, 0x707E8D, 0x728E24, 0x75BF1E, 0x75DDD2, 0x778181, 0x891C49, 0x8BFB3A, 0x9BC318), container sums (0x968B9B, 0x968F44, 0x969158), and the actor's carried weight 0x577250 (via 0x8A0CD0). The AMMO loader reads DAT2 into `form+0xB0` with a fixed 20-byte read (0x5030C5): +0 projectiles per shot, +4 projectile, +8 weight, +12 consumed ammo, +16 consumed percentage. DAT2 is also present with 12 bytes (weight still at +8). The exe copies whatever is there.
- **Formula.** `carried_ammo_weight = hardcore ? count * AMMO.DAT2[8:12] : 0`.
- **Values (ESM, per round).** 10mm 0.033, 9mm 0.029, .357 0.035, .44 0.043, 12 ga 0.075, .308 0.055, 5.56mm 0.026, 5mm 0.017, .22LR 0.0076 (DAT2 is 12 bytes), energy cell 0.078, microfusion cell 0.1, 25mm grenade 0.0 (20-byte DAT2 weight absent?), 25mm HE 0.25 (12 bytes). The full table is `tmp-hardcore/ammo_weights.txt`. Five AMMO records have no DAT2 (weight 0): Dart, Missile (ant and centaur spit), Nellis Artillery Shell, Mirelurk King sonic. TTW does not change any vanilla AMMO weight (it overrides 110 AMMO records; the weights of the NV ones are identical; it adds 20 new ones, e.g. 10mm AP 0.033, .32 0.035, .45 auto 0.065).
- **Today.** `carriedAmmoWeight` (`hardcore.hpp:223-226`), used in `falloutinteraction.cpp:221`, `falloutscripts.cpp:2501`, `falloutpipboy.cpp:175`. The logic matches.
  **Defect.** The AMMO loader `components/esm4/loadammo.cpp:89-101` reads DAT2 only when its size is exactly 20 and skips it otherwise. So the 11 records with a 12-byte DAT2 get weight 0 in hardcore: `.22LR Round` 0x7EA27, `.22LR Plinking` 0x121162, `25mm Grenade, High Explosive` 0x13E449, `Flamer Fuel, Homemade` 0x166F62, `Alien Power Cell` 0x29364, `Mesmetron Power Cell` 0x6A80D, `5.56mm Round (robot)` 0x78CC4, `Energy Cell (turret)` 0x166B5A, `Flamer Fuel (robot)` 0x78CC1, `Pure Water` 0x15FD42, `Sonic Energy` 0x14F44A. `.22LR` is a common player round (100 rounds should weigh 0.76; the port says 0). Present but different. Fix: accept `size >= 12` and read the first 12.
- **Round check.** Hardcore on, give 100 × `Ammo10mm` (0x4241): carried weight rises by 3.3 (±0.01). Hardcore off: by 0. Give 100 `.22LR`: +0.76 (±0.01). Tolerance 0.01 because weights are 4-decimal floats.
- **Confidence.** Exe-read plus ESM-read.

### F.R12 HUD meters (H2O / FOD / SLP / RAD / LMB)

- **File.** `menus\main\hud_main_menu.xml` (in `Fallout - Misc.bsa`; extracted copy `tmp-hardcore/menus/hud_main_menu.xml`). Lines 411-480: `<rect name="HardcoreMode">` (width 386, height 20, alpha 255) with five `<text>` children, `font 7`, right-justified, `systemcolor &hudmain;`, `zoom &scale;`: `Dehydration` (string `&-sStatsH20Abbrev;` = "H20"), `Hunger` ("FOD"), `Sleep` ("SLP"), `Rads` ("RAD"), `LMBs` (`&-sStatsLMBAbbrev;` "LMB").
- **Layout (exe 0x76BFE0).** The rect sits at `x = healthBlock.x + width + 10 + 150`, the same `y` as the health bar. Labels are placed left to right with 10 px gaps (`NEEDS_OFFSET 150`, `NEEDS_GAP 10` in the port).
- **Per frame (0x7739F0(isHardcore), called from 0x770BE3).**
  - RAD label: the AV 54 value. LMB label: a fake level from the number of the limb conditions 25-31 (AV 0x19..0x1F; the exe loops over the body-part conditions listed in the 0x773BBC window: head, torso, arms, legs, counting those with value 0): 0 → 0; 1 → `fHCHalfAlphaTextThreshold + 1`; 2 → full alpha + 1; 3 → red + 1; ≥ 4 → flash + 1. These two always run.
  - If hardcore: labels H2O (AV 73), SLP (AV 75), FOD (AV 74) are styled by the same function. If not hardcore, their alpha is forced to 0.
- **Style of a tile (0x773E90(tile, value, flashFlag)).** `value >= 800`: colour index 2 (red) and alpha = the flashing value. `>= 600`: colour 2, alpha 255. `>= 400`: colour 1 (hud), alpha 255. `>= 200`: colour 1, alpha 0x7C (124). Else colour 1, alpha 0 (hidden). (Trait 0xFF4 = colour index, 0xFA9 = alpha.) The flashing alpha is a triangle wave between 128 and 255 at 0.5 per millisecond of real time (0x1016248 = 0.5, 0x101E704 = 128, 0x1023CD8 = 255), advanced once per update in 0x773E90.
- **Today.** `NeedsDisplay` (`hardcore.hpp:171-215`, `hardcore.cpp:258-295`). Host `needsStatus` (`falloutneeds.cpp:95-135`). HUD Lua `falloutmenuhud.lua:80-82,131-165,624-640,811-843` (row placed at the same x and y, tiles per level). Status bridge `falloutstatus.lua:586`. The limb counter in the port uses conditions 25-30 (six parts, `falloutneeds.cpp:39-40`). The exe loop covers at least AV 0x19, 0x1A and so on; the full list of conditions it counts was read only for the first three. The port's six (25-30) follows the earlier M16 reading. Matches (colours, alpha, thresholds, flash, hidden needs when off). The exact limb set is a minor open item.
- **Round check.** Hardcore on, set H2O to 150, 250, 450, 650, 850: label alpha 0 / 124 / 255 (colour 1) / 255 (colour 2) / 128-255 flashing (colour 2). With H2O 850, sample the alpha twice 200 ms apart: they differ by about 100 (±20). Non-hardcore: H2O, FOD and SLP alpha 0. The RAD label shows by AV 54 with the same style table.
- **Confidence.** Exe-read (style, constants) plus XML-read.

### F.R13 Pip-Boy status pages for the needs

- **File.** `menus\main\stats_menu.xml` (copy: `tmp-hardcore/menus/stats_menu.xml`). Status tab sub-pages by `StatsMenu.user0`: 0 CND, 1 RAD, 2 EFF, **3 H2O, 4 FOD, 5 SLP**. Buttons: `stats_H20_button` (id 53, label `&-sStatsH20Abbrev;`, `listindex 4`, `_selected = user0 == 3`), `stats_FOD_button` (id 55, `user0 == 4`, listindex 5), `stats_SLP_button` (id 57, `user0 == 5`, listindex 6), stacked below the EFF button. Containers: `stats_dehydration_container` (line 1044), `stats_hunger_container` (1298), `stats_sleep_container` (1553), each shown when `user0` equals its page, `zoom 130`. Each contains:
  - an effects bracket (`stats_h20_effects_bracket`, id 54 / 56 / 58, hidden until code shows it);
  - an effects label (`user10` text = the stage's effect description);
  - a resist box (hidden);
  - a meter image `Interface\Stats\rad_meter_bracket.dds` with `_zero_pos 186`, `_full_pos 482` (x zoom/100);
  - labels `stats_h20_label` (the title from the `_h20` / `_hunger` / `_sleep` trait), `..._zero` ("0"), `..._mid` (`user2 / 2`), `..._max` (`user2`);
  - an arrow `Interface\Shared\Arrow\pointer_tail.dds` at `zero + (full - zero) * user11 / user2`;
  - the player's value text (`user11`).
- **Code.** 0x7E0060(mode) allows modes 0..5 in hardcore and 0..2 otherwise. 0x7DC4F0 (Pip-Boy menu refresh) calls 0x7DDC20, 0x7DDD10, 0x7DDE00 (fill the three pages) only if hardcore. 0x7E05F0 and 0x7E06F0 (navigation) and 0x7DB380 and 0x7DF2D1 also test it. `user2` = the largest stage threshold (1000), `user11` = the level. The effects text names the stage's penalties (`stats_hunger_effect_` strings in the exe at 0x10774E8, used by 0x7DE4FD).
- **Today.** `files/data/scripts/omw/falloutpipboystats.lua:45-55,150-175,328-348` (modes 4-6 counted from 1, `user2` and `user11` filled, buttons hidden when not hardcore). `needs[...Most]` from `needsStatus` (`falloutneeds.cpp:108-115`). Matches the meter. The "effects list does not name the need stages" (M16 "Not done") is missing: the effect label text `user10` and the effect bracket.
- **Round check.** Hardcore on, open STATUS > H2O with H2O = 250: arrow x = zero + (full − zero) × 250/1000 (= `186 + 296*0.25 = 260` × zoom/100 in menu units), `user11` text "250", max "1000", mid "500". Non-hardcore: the three buttons are not visible.
- **Confidence.** XML-read plus exe-read (mode limit).

### F.R14 Companion death

- **Games.** NV (and TTW). FO3 0x6F4A10 has no such rule.
- **Rule.** `Actor::IsEssential` 0x87F3D0: base = the actor base (or its template base). If a base exists: `if (player exists && actor.isTeammate (byte +0x18D) && !Player.IsHardcore) return true;`, else return `baseFlag (0x87F480, base+0x30) || 0x8C1B30(actor)` (the other essential sources). So a teammate is essential unless hardcore is on. Nothing is switched off or on at mode change: the same query is asked each time. Turning hardcore on immediately makes companions mortal. Turning it off makes them essential again (a dead one stays dead).
- **Scripts.** `VNPCFollowersQuestSCRIPT` resets a destroyed ED-E only if `!IsHardcore`. `HVBunkerExplosionScript` kills companions inside the Hidden Valley bunker only if hardcore. Dead Money's `NVDLC01BombCollarQuestSCRIPT` branches on companion deaths "in non-hardcore mode". Companion scripts (`RoseofSharonCassidyScript` etc.) call `ResetHealth` in `OnCombatEND` regardless of mode.
- **Today.** `companionIsEssential` (`hardcore.hpp:230-233`), `FalloutScripts::essentialByMode` (`falloutneeds.cpp:85-93`; NV only; excludes the player), used in `combatcommands.cpp:138`. Matches. The data scripts are run as data.
- **Round check.** Hardcore off: reduce a teammate's health to 0 with a hit. The actor is "unconscious/essential" (cannot die). Hardcore on: it dies. Tolerance: boolean.
- **Confidence.** Exe-read (0x87F3D0, key 0x87F42D).

### F.R15 Messages shown to the player (complete list)

1. Stage change (F.R8): `s<Need>Increase|Decrease` + (`sRadiationSick` + " " + stage name | `s<Need>NotSick`), 2 s each, with the in-pain or very-happy Vault Boy. Texts: see [the key values](#fkeys-key-values). Stage names: Minor, Adv., Crit., Deadly, Fatal Dehydration, Starvation, Sleep Dep.
2. The Doc prompt box (F.R2).
3. `CGTutorialCrippleHardcore` (F.R2).
4. The first-time help `HelpHardcoreNeeds` (full text in the MESG; mentions H2O, FOD, SLP, the Pip-Boy Status screen, waiting, sleeping and fast travel, and says "If performing any of these activities would result in death, you will be prevented from doing so" (F.unclear 3)).
5. Dead Money Elijah barks about the toxic cloud when hardcore (F.R16).
6. The Gameplay menu: the "Hardcore Mode" row.

- **Port.** 1 is present (`falloutneeds.cpp:65-83`). 2 and 3 are data. 4 was not found.

### F.R16 Other Hardcore differences (each claim checked)

| Claim | Result | Evidence |
|---|---|---|
| Needs tick with game time, so wait, sleep and fast travel cost needs | **True**, with no special cost code | F.R5: delta-of-clock rule; fast travel (0x93CDF0) advances time by hours; the port runs `updateHardcore` per hour of the trip (`falloutinteraction.cpp:1325`) |
| "Sleep only in beds" | **Not hardcore-specific.** Sleeping needs a bed in both modes (the menu `sleep_wait_menu.xml` offers Wait anywhere, Sleep at a bed). Only a sleep restores SLP; waiting does not | F.R7; 0x94E0C5 |
| Sleep restores health and limbs | In hardcore the end-of-sleep full restore is skipped | 0x94E1AB (`IsHardcore` test) before 0x8A0960 |
| Fast travel cost and limits | **No hardcore difference found.** Refusals are the same predicate (0x93D660) | no `IsHardcore` caller in the travel code (all 34 callers classified, below) |
| DT and DR changes | **No** | no `IsHardcore` caller in damage code |
| Dead Money: toxic cloud damage | **Hardcore only**: `NVDLC01GlobalToxicDamageSCRIPT` `PlayerREF.DamageActorValue Health 3` each `GameMode` frame in the listed Villa worldspaces if not interior, Health >= 10 and `IsHardcore` | script 0x100E883; barks `NVDLC01GlobalToxicBarkTriggerSCRIPT` |
| Old World Blues Stealth Suit and Lonesome Road auto-injectors | In hardcore they apply only one Stimpak at a time (`HasMagicEffect RestoreHealth == 0`). Non-hardcore: apply until health > 50% | `NVDLC03AuralStealthSuitQuestSCRIPT`, `NVDLC04AutoInjectQuestSCRIPT` |
| LR `Mojave` and `Wreckage` doors | heal hardcore players when travelling | `NVDLC04MojaveDoorSCRIPT` (0x100_39E6), `NVDLC04WreckageDoorSCRIPT` |
| Dead Money start | `Player.CIOS NVDLC01RestUp` resets all needs and health ("Hardcore Mode Reset") | quest `NVDLC01MQ01` |
| Hardcore achievement | F.R4 | |

All 34 `IsHardcore` (0x4D1360) call sites in the exe are accounted for: 17 weight sums and ammo weight (F.R11), the command 0x5A5FA3, the sleep tick (0x7C066B), the per-frame update (0x93F365), the menu getter (0x7D13DA), the Pip-Boy pages and navigation (0x7DB5E7, 0x7DC263, 0x7DC558, 0x7DC615, 0x7DC648, 0x7DC714, 0x7DC7C7, 0x7DF390, 0x7DF459, 0x7DF477, 0x7E0073, 0x7E05FD, 0x7E06FD), the HUD row (0x770BE3), the save of the flag (0x95A2B3), the sleep gates (0x94E0B3, 0x94E1AB), essential (0x87F42D), and weight for an actor (0x8A0CDC). There is no other behaviour. (Weight call sites: 0x4D1266, 0x707E8D, 0x728E24, 0x75BF1E, 0x75DDD2, 0x778181, 0x891C49, 0x8BFB3A, 0x968B9B, 0x968F44, 0x969158, 0x9BC318.)

- **Today.** None of the DLC scripts need engine code. As data, they depend on the script engine. Dead Money toxic damage and so on: present as data, unverified.

### F.R17 TTW differences

- TTW's `FalloutNV.esm` (the merged copy in `ttw_output`, 465 055 records) and `TaleOfTwoWastelands.esm` hold the same `fHCStarvationRate` 25 and `fHCSleepDeprivationRate` 50, the same DEHY, HUNG and SLPD, and the same 15 stage SPEL. There is no `fHC*` override. TTW has 423 ALCH and 4 SPEL with an `IsHardcore` condition (the NV set plus its own FO3 conversions). Its Stimpak, Super Stimpak, Doctor's Bag, water and food hardcore effects equal NV's, with extra conditions on `GetIsID` (72) and `GetItemCount` (47) (F.unclear 8).
- TTW scripts: 50 scripts mention hardcore. New behaviour (all NVSE and JIP functions, not the exe):
  - `TTWCG01CasualHardcoreMessageSCRIPT` (0xC01C9F4): the same box as F.R2, shown when leaving Vault 101 (`Vault101ExitScript`: `CG01HardcoreTriggerREF.activate player 1`). "Yes": `if !IsHardcore { SetHardcore 1; call TTWHardcoreFunctionScript }`. "No": `if IsHardcore { SetHardcore 0 }`. Then it marks for delete and activates `Vault101CaveDoorInterior`.
  - `TTWHardcoreFunctionScript` (0xC009266) (also called on every game load while hardcore, from `TTWGenericHandlerScript`): `SetNumericGameSetting fVATSPlayerDamageMult 0.75`, `fVATSRangeSpreadUncertainty 0.01`, `fVATSSpreadMult 0.5`, `fVATSRangeSpreadMax 10`, `fVATSDamageToWeaponMult 1.0`, `iLevelsPerPerk TTWHardcoreLevelsPerPerk` (GLOB 0xC079037 = 2.0). So TTW hardcore: a perk every 2 levels, and tougher VATS.
  - `ToggleHardcoreTracking 0/1` (JIP LN 0x2837) around scripted time jumps (`SetGameHour 14` in `MrHouseSCRIPT` and `VHDHouseBattleSCRIPT`, trains, `MQ04Script` Tranquility Lane, DLC travel doors). `VCG01RemoveDocCarebearModeTriggerSCRIPT` calls `ToggleHardcoreTracking 1` when the player leaves Doc's house (F.unclear 1).
  - `bHardcoreNeedsFix` optional patch (F.unclear 2).
- **Today.** None of the extender functions exist. `git grep` finds `ToggleHardcoreTracking` only in docs and CSV: `docs/openfallout/ttw-extension-codes.csv:110`, `coverage/extender-tables/jip_nvse.csv:884,1002,1011-1014` (`GetHardcoreTracking` 0x28AD, `Get/SetHardcoreStageThreshold` 0x28B6/7, `Get/SetHardcoreStageEffect` 0x28B8/9). Missing. Needed for TTW hardcore: `ToggleHardcoreTracking` (semantics: pause the tick and re-baseline), `GetHardcoreTracking`, `SetNumericGameSetting`.
- **Round check (TTW).** `ToggleHardcoreTracking 0; SetGameHour +10; ToggleHardcoreTracking 1` must add 0 to the needs (the jump not credited). It needs adjusting if tracking means something else.

### F.R18 Dead Money, Old World Blues and other DLC scripts (as data)

As in F.R16. No DLC changes the rates, stages or weights. Dead Money additionally (a) overrides 24 NV ALCH (`ScotchGood`, `Wine`, `Vodka`, `Irradiated*` and so on) and `fCombatTeammateFollowRadiusMult`; (b) does toxic cloud damage (3 health per frame-tick while in the Villa worldspaces, hardcore only).

### F.numbers Expected numbers (NV, default time scale 30, hardcore on, needs start at 0)

Use `TimeScale` read from the game (30). Let `h` be game hours of an uninterrupted frame-by-frame clock advance. The tolerances account for the floor and the per-frame remainder drop.

| After | H2O (AV 73) | FOD (AV 74) | SLP (AV 75) | Why |
|---|---|---|---|---|
| 1 real second of play | 0.1 pts | 0.04 | 0.02 | 1 pt per `rate` seconds |
| 10 real seconds | 1 (0-1) | 0 | 0 | |
| 1 game hour waited with the wait menu (one tick) | 12 (±1) | 4 (±1) | 2 (±1) | `trunc(60/I)`: 60/5, 60/12.5, 60/25 |
| 24 game hours waited (24 ticks) | 288 (±12) | 96 (±12) | 48 (±12) | 24 × `trunc(60/I)` = 24 × 12 / 4 / 2; float rounding can lose 1 point per few ticks (the M16 notes saw hunger 33, not 32, after an 8 h sleep) |
| 24 game hours of continuous play (2880 real seconds, no menu) | 288 (±3) | 115 (±3) | 57 (±2) | 4.8 and 2.4 per hour accrue exactly in continuous play (the remainder is dropped only per credit, a frame's worth) |
| 83.3 game hours | 1000 (stage 5: death) | | | wait 84 h with hardcore on: H2O reaches 1000; the player dies |
| 8 h sleep from 0/0/0 | 96 (±4) | 32 (±1; M16 saw 33) | 0 | sleeping suppresses SLP accrual and the hourly reduction is `min(level, 60)` |
| 8 h sleep from SLP 300 | 96 | 32 | 0 (300 − 5 × 60 = 0 after hour 5, stays 0) | `min(level, 60)` per hour, no accrual while sleeping |

- Stage entries (continuous play, real time): H2O stage 200 after 2000 s (33.3 min), 400 after 4000 s, 600 after 6000 s, 800 after 8000 s, 1000 after 10 000 s. FOD: 5000 s / ... / 25 000 s. SLP: 10 000 s / ... / 50 000 s. At each entry the attribute penalties of F.R8 apply. Tolerance ±1 point (±0.1 s per point of timing).
- One Stimpak, no Fast Metabolism: +5 HP per second for 6 s. At t = 1, 3, 6, 8 s the health gain is 5 (±1), 15 (±2), 30 (±3), 30. With Fast Metabolism 6/s, total 36. Super Stimpak 20/s × 3 s = 60 (72). Non-hardcore Stimpak: +30 at once (+36).
- Rad-Away hardcore: −5 rads/s for 10 s (−50 total, ±2).
- Weight of 100 rounds of 10mm: 3.3 (0.033 × 100; ±0.01) in hardcore, 0 otherwise. 100 × 9mm 2.9. 100 × 12 ga 7.5. 100 × .22LR 0.76 (needs the loader fix). 100 × energy cells 7.8.
- `SetHardcore 0` with H2O 500, FOD 300, SLP 200: all three become exactly 0 (and the stage spells come off) when the menu-state global is false.
- Companion: a teammate is essential if and only if `IsHardcore == 0`.
- HUD: label alpha as F.R12.

### F.today Port status table (summary)

| Rule | Port | Status |
|---|---|---|
| F.R1 state, commands, save | `hardcore.cpp:195-256`, `falloutsave.cpp:1164` | present, matches (except global 0x11A0294) |
| F.R2 Doc prompt | data | present, unverified; help message trigger missing |
| F.R3 menu switch | `falloutsettingsitems.lua:107` | matches |
| F.R4 achievement flag | `hardcore.hpp:128` | matches |
| F.R5 tick | `hardcore.cpp:108`, `falloutinteraction.cpp:1402` | matches, except the Pip-Boy against Fighting control bit (different) |
| F.R6 rates | `hardcore.hpp:122` | matches |
| F.R7 sleep reduction | `hardcore.cpp:135`, `falloutinteraction.cpp:1514` | matches; the end-of-sleep limb and health restore skip is unchecked |
| F.R8 stages, messages, death | `hardcore.cpp:65`, `falloutneeds.cpp:65` | matches |
| F.R9 food, drink, water | data | present, unverified; water sources missing |
| F.R10 Stimpak, Doctor's Bag, Rad-Away | data | present, unverified |
| F.R11 ammo weight | `hardcore.hpp:223` | logic matches; the loader drops a 12-byte DAT2 (different) |
| F.R12 HUD | `hardcore.cpp:258`, Lua | matches |
| F.R13 Pip-Boy pages | `falloutpipboystats.lua` | matches the meters; effect text missing |
| F.R14 companions | `falloutneeds.cpp:85` | matches |
| F.R17 TTW | none | missing (`ToggleHardcoreTracking` and others) |

### F.unclear Open items in part F

1. What `ToggleHardcoreTracking` does (JIP LN 0x2837, used 20+ times by TTW and once in TTW's vanilla-quest overrides). Inferred from use: `ToggleHardcoreTracking 0` is called before a scripted `SetGameHour`, cutscene, train trip or Tranquility Lane, and `1` after, so it pauses the needs timer and re-baselines it. No JIP source or GECK page could be read (geckwiki returned HTTP 403). Which player byte it flips (`+0xE39`, a control bit, or a plugin-side flag) is not known.
2. The TTW optional patch `bHardcoreNeedsFix` (set to 1 on every game start by `TTWGenericHandlerScript`). The plugin source is not available. It is probably a fix to the needs timer (the same handler sets `bGameDaysPassedFix` too), but what it changes is unknown. If it changes the tick arithmetic, the numbers in F.numbers would differ under TTW.
3. Does the exe refuse a wait, sleep or fast travel that would kill? The help message `HelpHardcoreNeeds` says so ("If performing any of these activities would result in death, you will be prevented from doing so"). No such check was found: the three refusal predicates (wait, sleep, fast travel) ask the usual facts and no needs fact. 0x7C0580 runs 0x969C30 and 0x94DF80 with no death test (agrees with `docs/openfallout/m13-exe-reads.md` item 21). Treated as help text only. Not run live.
4. How the exe picks the `WaterHeal*` spells (drinking from sinks, toilets and water: spells `WaterHeal1Purified` ... `WaterHeal1Rads500`, each `RestoreDehydrationLevel 200` in hardcore, 30 more with the perk Camel of the Mojave). The spells are not referenced by any record. Presumably the exe applies them by water quality. The port has no water-source drinking at all (not checked beyond a grep).
5. Clamping of the need levels. The tick only adds. `Restore*` effects reduce the level. Whether the AV is clamped at 0 (and at 1000) by the AV registration (0x66EFE0 row: flags 0x221) was not traced. The fatal stage makes the upper bound moot. The port clamps nothing special (it uses `ActorValues`).
6. The Doctor's Bag hardcore effect `NVRestoreLimbs` (MGEF archetype 35, magnitude 36 / 45). Which limb(s) get how many condition points (a menu choice of limb, or all at once, "minimal healing each") was not read from the exe. The port treats it as data.
7. Global 0x11A0294 (read by `SetHardcore` through 0x7D6E60). When true, turning hardcore off does not zero the three needs. It is written 0 at 0x788921 and 0x7D59A0 and read in about 20 menu functions around 0x788440-0x78D4C6 and 0x7CB7D0. The spec believes it means "menu opened with no game loaded" (the main-menu options screen) but did not find its writer of 1.
8. TTW conditions with `GetIsID` (72) and `GetItemCount` (47) that TTW added to Stimpak and food effects (F.R17). The purpose is unknown. They are not hardcore tests.
9. The Pip-Boy quick-use buttons `stats_stimpak_button`, `stats_healing_mode`, `stats_radaway_button`, `stats_radx_button`, `stats_drbag_button` (status page 0). The port lists them as `HIDDEN` "what hardcore adds". No exe test that hides or adds them by `IsHardcore` was found (the `IsHardcore` callers in the Pip-Boy are 0x7DC4F0, 0x7DF2D1, 0x7E0060, 0x7E05F0, 0x7E06F0, 0x7DB380, none of which touches those buttons by name; not traced to the end).
10. The 2000 and 1000 message face constants (Vault Boy "in pain" and "very happy") were read from the icon paths in 0x8C5610. The port maps them to face kinds 3 and 1. No live check.
11. Wiki claims not in the exe: "fast travel costs", "DT changes", "sleep only in beds" (F.R16). Not found.

---

## G. Companions

Part G covers recruiting a companion, knocking one out without Hardcore, and companion death with Hardcore. It is NV only. Form ids are the plain `FalloutNV.esm` numbers (master index 0). The 4c round uses Cass (owner decision Q19).

### G.0 Summary

- **Hardcore removes "essential" with one exe check, no script.** `Actor::IsEssential` (`FalloutNV.exe` 0x87F3D0) says "yes" for any character that is a player teammate unless the player is in Hardcore mode (player + 0x7BC == 1). Otherwise it falls back to the record flag and the per-reference flag. Cass and ED-E have the essential flag off in their records, so they are essential only through that rule. No companion script or quest touches it.
- **Without Hardcore, a companion at 0 HP is put down, not killed.** The kill routine (0x89D900, called by the `KillActor` command and by damage) sees an essential character and sets life state 6 instead of dead. No death, no `OnDeath` script, no corpse. A timer of `fEssentialDeathTime` = 10 s (exe default; no plugin overrides it) is started. When it runs out, the character gets up. For a teammate the timer only runs while the player is not "in danger" (inferred, see G.unclear 2).
- **With Hardcore on, the same blow kills.** The character dies normally: a corpse with its inventory, and `OnDeath` runs. Cass's script sets `bCassDead`, removes her perk and drives `VCassCompanion` to stage 110 (failed). ED-E's script sets `bEDEDead` and drives `vDialogueEDE` to stage 100 (failed). No respawn (neither record has the respawn flag).
- **Today's engine already has all of this**, written from the same exe addresses: `companionIsEssential` (`hardcore.hpp:230`), `fallsInsteadOfDying` (`falloutscripts.cpp:3218`), wake timers (`combatstate.cpp:180, 350`), `IsHardcore` and `SetHardcore`, `SetPlayerTeammate`, `KillActor`, and dialogue with follow-ups. Every command used by the Cass and ED-E recruit and death scripts has a handler. The differences from the exe are small and listed in G.today.
- **Which companion for the round: Cass.** Her recruit is 11 dialogue picks with no message box and no object that has to move. It needs three stand-ins (quest VMS05 at stage 30, 750 caps, Barter 50). ED-E needs 3 items, three message-box answers that the round tools cannot give today, a 2-second script timer, and a `MoveTo` of a persistent creature that sits disabled in a cell nobody loads. ED-E is the better second variant (it tests a robot).

### G.1 Recruit paths

#### G.1a Shared machinery (both companions)

- Quest `VNPCFollowers` (0xB16D0, script `VNPCFollowersQuestSCRIPT` 0x1209D1) holds the bookkeeping. Script variable indexes used in conditions: 21 `bCassHired`, 22 `bCassFired`, 23 `bCassAvailable`, 30 `bEDEHired`, 31 `bEDEFired`, 32 `bEDEAvailable`, 15 `RexHired`, 47 `bHumanoidInParty`, 48 `bCritterInParty`, 18 `bPlayerHasFollower`, 19 `nCurrentFollowers`, 46 `bCassDead`, 65 `bEDEDead`.
- Topic `FollowersHired` ("I think we should travel together", DIAL 0x37084, flags 0x02 = top-level, so it is offered whenever the conversation has no explicit choice list). It also lists the other follower topics, all top-level: `FollowersWait` 0x37071, `FollowersLetsGo` 0x37082, `FollowersFired` 0x37083, `GOODBYE` 0xD4.
- The recruit result script of every companion ends with the same four commands: `AddToFaction FollowerFaction 1`, `SetPlayerTeammate 1`, `SetFactionRank TeammateFaction 1`, `EvaluatePackage`. Cass and ED-E also `AddPerk CompanionSuite` (on themselves), a personal perk on the player (`WhiskeyRose`, `EnhancedSensors`), `SetIgnoreFriendlyHits 1`, `IgnoreCrime 1`, `SetCombatStyle`, and increment `nCurrentFollowers`. The companion wheel reads `GetPlayerTeammate`.

#### G.1b Cass (NPC_ 0x133FDD `RoseofSharonCassidy`, script 0x13408C)

**Where she stands.** Reference ACHR `RoseofSharonCassidyREF` 0x135F19, cell 0x141AA5 `MojaveOutpostBarracks01`, name "Mojave Outpost Barracks" (unique name; interior), position (2103, 1961, 9328), persistent, enabled. Her home marker `CassHomeMarkerREF` 0x171C43 is in the same cell (2200, 1974, 9328). The cell also holds an NCR trooper group and `vMOLaceyREF`.

**Teleport.** `r.teleport{ cell = 'Mojave Outpost Barracks', pos = {2103, 1861, 9328}, yaw = 0, onGround = true }` puts the player 100 units south of her, facing north (+y). The spec did not read the geometry. If the floor check fails, use her own position from `r.lookup({ 'FalloutNV.esm', 0x135F19 })`.

**Requirements a fresh post-intro character does not have:**
1. Quest `VMS05` "You Can Depend on Me" (0xEDB37) at stage 30, so that objective 30 is displayed. Topic003 needs `GetObjectiveDisplayed(VMS05, 30) == 1`. Stage 30's result also checks that Cass is alive, shows objectives 30, 40, 50, adds a note and enables `HenryJamisonRef`. In play this is Alice McLafferty's job (stages 10, 15, 20 first).
2. Barter ≥ 50 (actor value 0x20) and ≥ 750 caps (`Caps001` 0xF) at the moment of Topic009. With Barter below 50 the same pick fails and falls back to Speech 50 / 75 branches (Topic026, Topic022) that lead to the "help Jackson first" branch, not to a recruit.
3. `VNPCFollowers.bHumanoidInParty == 0` (Topic065 and Topic080 need it).

**The order of picks.** Found with a graph search over the choice links `TCLT` and follow-ups `TCFU`. All INFO conditions along it are listed. The DIAL ids in the third column are what `core.conversation.get().options[i].topic` returns (as `FormId:0x...`):

| # | Cass says / player picks (topic) | DIAL id | Condition on the pick | Result |
|---|---|---|---|---|
| 0 | greeting "Looking for trouble?" (INFO 0x133FCE, say-once, no condition besides her id; the later greeting is INFO 0x15CDED) | greeting | first meeting (a second talk uses 0x15CDED, `VNPCFollowers` 21 == 0, 22 == 0, `VDialogueCass` var 4 == 0) | sets `VDialogueCass.bMetCass`; adds four topics |
| 1 | "Crimson Caravan sent me, they want to buy out your caravan." (`...Topic003`) | 0x15C754 | VMS05 objective 30 displayed; `VDialogueCass` var 3 == 0 | |
| 2 | "Wait, what happened to your caravan?" (`Topic004`) | 0x15C753 | none | |
| 3 | "So even if you have nothing, you won't sell?" (`Topic007`) | 0x15C750 | none | |
| 4 | "I have the offer letter here, the terms are fair." (`Topic008`) | 0x15C74F | none | sets `bBuyCaravanIntro` = 1 |
| 5 | "Are you sure?" (`Topic025`) | 0x15C740 | `bBuyCaravanIntro` == 1, `bBoughtCaravan` == 0 | |
| 6 | "It's just a name, this - this is money." (`Topic009`) | 0x15C74E | `bBoughtCaravan` == 0, caps ≥ 750, **Barter ≥ 50** (two INFOs; the other one is the FAILED branch) | RewardXP 50, `bCaravanBarterSuccess` = 1 |
| 7 | "Far from it, to turn down the original terms." (`Topic013`) | 0x15C74A | none | INFO 0x15C766 then follow-up 0x15C767: VMS05 objective 30 completed, 35 shown, `bBoughtCaravan` = 1, **removes 750 caps** |
| 8 | "What will you do now?" (`Topic041`) | 0x15C736 | none | |
| 9 | "There's other places to go." (`Topic000`) | 0x15D047 | none | |
| 10 | "Maybe it's not where you go, it's the company." (`Topic065`) | 0x15D046 | `bHumanoidInParty == 0` | its follow-up is the recruit INFO below |
| 11 | follow-up INFO **0x15D054** in topic `...Topic002` (goodbye flag, no condition) | (0x15C755, not offered) | | **the recruit result** |

An equivalent shorter tail: step 8, then "You could come with me." (`Topic080`, DIAL 0x15CDCF, INFO 0x15D05B, needs `bHumanoidInParty == 0`), then "Why not? I could use someone who knows the roads." (`Topic068`, DIAL 0x15D045), whose follow-up is the same INFO 0x15D054.

**Result script of INFO 0x15D054** (names only): `bJoinedPartyOnce` = 1 (first time `nRecruited`++), `bPlayerHasFollower` = 1, `nCurrentFollowers`++, `SetIgnoreFriendlyHits 1`, `IgnoreCrime 1`, `bCassAvailable` = 1, `bCassHired` = 1, `bCassFired` = 0, `bHumanoidInParty` = 1, her `Waiting` = 0, `IsFollowingDefault` = 1, `CombatStyleRanged` = 1, `AddPerk CompanionSuite`, player `AddPerk WhiskeyRose`, `ShowMessage FollowerMessagePerkCassAdd`, `RemoveFromFaction NCRFactionNV`, `AddToFaction FollowerFaction 1`, **`SetPlayerTeammate 1`**, `SetFactionRank TeammateFaction 1`, `EvaluatePackage`, `ResetAI`, `StartQuest vCassTimer`, and the VMS29a objective fix-up.

**Later hires** (after she was told to leave: `bCassAvailable` == 1) go through the top-level `FollowersHired` (INFOs 0x15C88A and 0x15C88B in `VNPCFollowers`) with the same result. Do not use this as the first hire: `bCassAvailable` is set to 1 only by the recruit INFOs themselves, by a Mortimer node that sets it to 0, and by debug topics. The round cannot set a quest-script variable.

**Other facts.** The storage-node greeting of topic `...Topic002` (INFO 0x15C761, needs `VDialogueCass` var 6 `iTest` == 1) is a test hook, never true in play. A conversation option list built from `TCLT` only shows a choice when the topic has a response that passes (OpenFallout `addOption`, `dialogue.cpp:304-314`).

#### G.1c ED-E (CREA 0x10C769 `NVCompanionEdE`, script `EDEScript` 0x151D03)

**Where he stands.** The visible one is the disabled robot `NVCompanionEDEDisabledREF` (ACRE 0x1572E6, base CREA 0x1572E2 `NVCompanionEdEDdisabled`, script `EDEScriptDisabled` 0x1572E0), in cell 0xD70E2 `PrimmNashResidence`, name "Nash Residence" (unique, interior), position (2093.1, 1576.3, 7751.6). The cell has `EDEHomeMarker` 0xA23D1 at (2048, 1632, 7664), which is probably floor level (not verified). The real companion `EDE1Ref` (ACRE 0x1732D1) sits disabled in cell 0x134FF5 "HooverDam" (Hoover Dam Global Dummy Cell). `EDE2Ref` 0x1732D0 and `EDE3Ref` 0x1732CF are the two upgraded versions, in the same place.

**Requirements.** The data has no condition on any quest of Johnson Nash. Nash is only the owner of the cell's name. To make ED-E available, the player has to get the robot repaired through message boxes (all in `EDEScriptDisabled`). The button numbers are fixed: a button hidden by its condition keeps its number (OpenFallout does the same, `mNumbers`, `falloutscripts.cpp:1956-2018`), so the number the script reads is not the position in the box.

| Box (MESG id) | Buttons (number: label, condition) |
|---|---|
| `EDERepair00` 0x1572EE | 0 Leave; 1 Try to fix (Repair) [needs script var `iRepairState`==0]; 2 Try to fix (Repair) [`iRepairState`==1]; 3 Examine the electronics (Science) [`iRepairState`==0] |
| `EDERepair01` 0x1572EF | 0 Leave; 1 Run the repair [**Repair >= 65**, AV 0x27]; 2 Examine the electronics (Science) [`iRepairState`==0]; 3 Repair with parts |
| `EDERepair02` 0x1572F0 | 0 Leave; 1 Try to fix (Repair); 2 Run the bypass [**Science >= 55**, AV 0x28] |
| `EDERepair03` 0x1572ED | 0 Leave; 1 Try to fix (Repair) [**Repair >= 35**]; 2 Repair with parts |
| `EDERepair04` 0x1717D3 | 0 Leave; 1 Fix it with the parts [needs `SpareParts` ×3 (MISC 0x31944), `SensorModule` ×2 (0x22109), `ScrapElectronics` ×1 (0x13B2B1)]; 2 Try without parts (Repair); 3 Examine (Science) |

**A fresh-character route that needs no skill.** Give the three item types. Activate the robot. The script answers per the table: box 00 number 1, box 01 number 3, box 04 number 1. The script removes the items, fades, sets `iEDEFixed` = 1, `DisablePlayerControls`, `StartQuest vDialogueEDE`, `bEDEAvailable` = 1, `nRecruited`++, `EDE1Ref.MoveTo NVCompanionEDEDisabledREF`. Two seconds later (`GameMode`, `GetSecondsPassed`) the script sets `EDE1Ref.SetRestrained 1`, enables EDE1Ref, `ResetAI`, and disables itself. `EnablePlayerControls` is not called by this script. It happens in ED-E's first greeting INFO 0x1579D7 ("Curious Beep", result `enableplayercontrols`, `RemoveScriptPackage EDEDialoguePackage`). ED-E's own `EDEScript` sets `Waiting` free: after 5 s `OnceOnly` = 2, `SetRestrained 0`, `ResetAI`.

**Then the conversation.** Talk to EDE1Ref (any greeting, 0x1579D7 or 0x1579D8 "beeps"). Pick the top-level `FollowersHired` (DIAL 0x37084). INFO 0x1579C3 answers it. Conditions: voice type `RobotEDE`, `bEDEAvailable` == 1, `bEDEHired` == 0, `RexHired` == 0. Its result: clears `Waiting` on all three EDE refs, `IgnoreCrime 1`, `bPlayerHasFollower` = 1, `bEDEHired` = 1, `bEDEFired` = 0, `bCritterInParty` = 1, `AddPerk CompanionSuite`, player `AddPerk EnhancedSensors`, `SetCombatStyle FollowersCombatStyleRanged`, `AddToFaction FollowerFaction 1`, **`SetPlayerTeammate 1`**, `SetFactionRank TeammateFaction 1`, `EvaluatePackage`, `SetIgnoreFriendlyHits 1`. The vanilla ED-E quest `vDialogueEDE` (0x1572E8, "ED-E My Love") needs nothing else for this.

#### G.1d Which is simpler and more reliable for an automated round

Cass. The reasons:
1. Everything is `core.conversation.choose` plus three stand-ins (`r.quest('VMS05', 30)`, `r.give` 750 caps, `r.command('ForceActorValue', 'player', {32, 60})`).
2. No message box has to be answered with a chosen button. The round player answers every box with its first button after 1 s (`roundplayer.lua:55-72`), which for ED-E is "Leave It Alone".
3. No `MoveTo` of a persistent creature.
4. A stable target: one persistent ACHR in an interior.

ED-E is shorter in dialogue (one pick) but needs a new "press button by position" round step, the three items, the 2 s fade script, and the unproven `MoveTo`. Use ED-E as variant two. The two in one round share `VNPCFollowers`, so run them in separate engine starts (variants), each starting with no companion.

### G.2 Knock-out without Hardcore (NV exe, essential companion at 0 HP)

All from `FalloutNV.exe`. Addresses are file VA.

- **The setting that turns the whole thing on.** INI `bEssentialTakeNoDamage:GamePlay`, default 1 (`Fallout_default.ini:451`, static default 1 at 0xFA8770, byte at 0x11E0888). With it off, essential characters are not protected. OpenFallout never reads it (always on).
- **Essential test** `Actor::IsEssential` 0x87F3D0. (a) The player exists, actor + 0x18D (teammate) is set, and `Player::IsHardcore` (0x4D1360: player + 0x7BC == 1) is false: true. (b) Else the base form ACBS flag 0x2 (via 0x87F480), or the reference flag at actor + 0x140 bit 31 (0x8C1B30, set by `SetActorRefEssential`).
- **Life state change** 0x8A1800 (`SetLifeState`). INI on, essential, and new state 1 (dying) or 2 (dead) becomes 6. State 6 with `CanBeKnockedOut` (0x8845A0) false becomes 0 (alive, nothing happens). A death handler (0x8B01C0, starts the death proper) returns early for an essential character, so `OnDeath` does not run. The dying-state handler (0x8868D6) does the same.
- **The kill routine** 0x89D900 (callers include the `KillActor` command, 0x5BE2F4, and the damage paths). For an essential character:
  - If it is already unconscious (3) or restrained (5): health is set to `fEssentialHealthPercentReGain` (0.3, exe 0x1017870) of its base health (0x89DE9D-0x89DEFB).
  - Otherwise: process + 0xA4 = `fEssentialDeathTime` (exe default 10.0, static init 0xF69130, read at 0x89DF50). HighProcess + 0x33C = 20.0 (unknown). Combat ends. `SetLifeState(6)`. Health and limb conditions are repaired by 0x8A0960 (health below 0 becomes 1.0). Then the message `sEssentialCharacterDown`.
- **Recovery** (0x888B50 at 0x889079, per frame). Life state 6 and process level 3 or 1: the timer is decreased (slot 0xE0 subtracts the frame time), and when it reaches 0 the actor gets `SetLifeState(0)`, 0x8A0960 and the event 0xB. Teammates (actor + 0x18D) only decrease it while the player's byte + 0xDF0 is false. Others always do.
- **Nothing in the ESM overrides this.** `fEssentialDeathTime`, `fEssentialHealthPercentReGain` and `bEssentialTakeNoDamage` appear in no `.esm` or `.esp` of the New Vegas Data folder (FalloutNV, DLCs, packs).
- **Scripts that help.** Both companion scripts have `OnCombatEnd`: `if GetPlayerTeammate == 1: ResetHealth; RestoreAV` for the six limb and brain conditions. ED-E also resets `Aggression` and `BrainCondition`. Cass's own comment in `OnHit VFSJeanBaptisteCuttingREF` ("removing Cass from factions so she actually dies in non-hardcore mode", plus `SetPlayerTeammate 0` before `kill`) confirms the rule from the data side: only a non-teammate Cass can be killed outside Hardcore.
- Community pages agree (casual: companions are knocked out; Hardcore: they can die): https://fallout.fandom.com/wiki/Hardcore_mode and https://fallout.fandom.com/wiki/Fallout:_New_Vegas_companions. They are not used as evidence for any number.

### G.3 With Hardcore

- **How essential is removed.** The exe check above (part (a) is skipped when player + 0x7BC == 1). No script, no faction trick. `SetHardcore 0/1` writes the same flag (0x7D1400 and 0x5DE920 end in 0x969E90, per the existing notes in `falloutneeds.cpp:52`). `IsHardcore` is command 0x124A, `SetHardcore` 0x124B. The records of both companions are non-essential, so the result is "can die".
- **What the death leaves behind.**
  - A corpse in the world with its inventory. Cass: caravan shotgun (WEAP 0x176E55), combat knife (0x176E56), 15 `AmmoCompanion` (0x176E54), 13 caps, an empty whiskey bottle, `OutfitCass` (ARMO 0x1350EF), `CassHat` (0x15EF8B), plus anything given. ED-E: `EDEZapGun` (WEAP 0x166B94) and 1 energy cell. Neither record has the respawn flag (Cass ACBS 0x91, ED-E 0xD0), so no respawn. `VNPCFollowersQuestSCRIPT` re-adds the companion weapons only while the character lives (`GetItemCount` below 2).
  - **Cass `OnDeath`.** If `bCassHired` == 1: `nCurrentFollowers`--, `bPlayerHasFollower` = 0 when it reaches 0, `bCassHired` = 0, `bHumanoidInParty` = 0, `ShowMessage FollowerMessageDeadCass`, player `RemovePerk WhiskeyRose`, `ShowMessage FollowerMessagePerkCassRemove`. Always: `if GetStage VCassCompanion >= 0: SetStage VCassCompanion 110` (0 counts, so it fires even if the quest never started; stage 110 has the fail flag 0x02), VMS29a objective fix-ups, VMS05 objective 30 hidden and 36 shown, and `VNPCFollowers.bCassDead` = 1. The teammate flag is not cleared by the death.
  - **ED-E `OnDeath`.** If `bEDEHired` == 1: `nCurrentFollowers`--, `bCritterInParty` = 0, `bPlayerHasFollower` = 0 at zero, `bEDEHired` = 0, `ShowMessage FollowerMessageDeadEDE`, `RemovePerk EnhancedSensors`, `ShowMessage FollowerMessagePerkEDERemove`. Always: `bEDEDead` = 1, and `if GetQuestCompleted vDialogueEDE == 0: SetStage vDialogueEDE 100` (stage 100 has the fail flag).
- **Without Hardcore none of those writes happen** (no death, no `OnDeath`). `VCassCompanion` stays below 110 and `bCassDead` stays 0.

### G.today Today's engine

Supported, with the code that does it:

| Need | Where |
|---|---|
| top-level topics when there is no choice list, `TCLT` choices, `TCFU` follow-ups, say-once | `components/falloutscript/dialogue.cpp:154` (top-level list), `:317-332` (`options`), `:182` (follow-ups), result scripts `:334-348` |
| Lua dialogue: `core.conversation.available/get/choose/close/finish` | `apps/openfallout/mwlua/conversationbindings.cpp:90-121` |
| `SetPlayerTeammate`, `GetPlayerTeammate` | `components/falloutscript/objectstate.cpp:360-366` (the player cannot be a teammate) |
| `GetPlayerTeammateCount` | `components/falloutscript/actorstatus.cpp:534` |
| `AddToFaction`, `RemoveFromFaction`, `SetFactionRank`, `RemoveFromAllFactions` | `actors.cpp:665-677`, `standing.cpp:368` |
| `AddPerk`, `RemovePerk`, `RewardXP` | `playerprogress.cpp:325, 330, 315` |
| `SetEssential`, `SetActorRefEssential`, `IsEssential`, `IsActorRefEssential` | `combatcommands.cpp:227, 233, 241-242`; order in `isEssential` `:134-146` (mode first, then script overrides, then record) |
| teammate-and-not-Hardcore rule (0x87F3D0 part a) | `hardcore.hpp:230` `companionIsEssential`, `falloutneeds.cpp:88-94` `essentialByMode` (NV only) |
| record essential flag (with templates) | `falloutscripts.cpp:2689-2705` `essentialByRecord` |
| `SetHardcore`, `IsHardcore`, `IsAlwaysHardcore`, `DisableHardcoreTracking`; Lua `core.interact.setHardcore` | `hardcore.cpp:237, 250-252`; `falloutneeds.cpp:50`; `interactbindings.cpp:184-187`; round step `r.hardcore(on)` `roundworld.lua:320` |
| `KillActor` that does not kill an essential character, `GetDead` | `actors.cpp:660` + `ActorState::kill` `:497-505`, `:654` |
| put down instead of dying, 10 s timer from `fEssentialDeathTime`, 30% at down-again | `falloutscripts.cpp:3218-3240`; timers `combatstate.cpp:180-192`, `:350-358`; wake `falloutscripts.cpp:4047-4057` |
| `SetUnconscious`, `GetUnconscious`, `SetRestrained`, `GetRestrained` | `combatcommands.cpp:202-224` |
| `OnDeath` and `OnCombatEnd` blocks | `falloutscripts.cpp:4087, 4096` |
| `ResetHealth`, `RestoreActorValue`, `ResurrectActor` (alias `Resurrect`), `ForceActorValue`, `GetHealthPercentage` | `actorvalues.cpp:357, 335, 365, 300, 348` |
| `GetDestroyed`, `ClearDestruction` | `referencestate.cpp:136`, `destruction.cpp:401` |
| `SetIgnoreFriendlyHits`, `IgnoreCrime`, `SetCombatStyle`, `EvaluatePackage`, `ResetAI`, `StartQuest`, `SetStage`, `SetObjectiveDisplayed/Completed`, `GetObjectiveDisplayed`, `MoveToMarker` (alias `MoveTo`) | `combatcommands.cpp:301`, `objectstate.cpp:338, 378`, `packagecommands.cpp:28, 71`, `quests.cpp:429-466`, `placementcommands.cpp:92` |
| quest-variable conditions (`GetQuestVariable` 79, `GetScriptVariable` 53) | `conditions.cpp:13-14, 158-159` |
| message box numbers keep the script's numbers when a button is hidden | `falloutscripts.cpp:1956-1960, 2012-2018` |
| round tools: `r.teleport{cell=...}`, `r.activate`, `r.quest`, `r.give`, `r.command`, `r.hardcore`, `r.state`, `r.lookup`, `r.describe`, `r.core.combat.command(name, id, ...)` (returns the value), `core.combat.strike`, `core.combat.isDead` | `round/README.md`, `scripts/roundworld.lua`, `mwlua/combatbindings.cpp:65, 105, 138` |

The spec checked 73 distinct command and block names used by the Cass and ED-E scripts and the recruit INFOs. Every command name has a handler (a string match in code, not a proof of full behaviour). The seven block names (`GameMode`, `OnActivate`, `OnCombatEnd`, `OnDeath`, `OnHit`, `OnLoad`, `SayToDone`) are block types in `script.hpp:55-60`. It confirmed that `OnDeath` and `OnCombatEnd` are triggered (`falloutscripts.cpp:4087, 4096`) and did not check the others. No command in these scripts is missing.

**Differences from the exe** (what a round must not over-assert):
1. Teammate wake timer: the exe holds it while the player's danger byte is set. OpenFallout counts 10 s straight (`CombatState::advance`).
2. Health at wake: the exe floors negative health at 1.0 (plus scripts). OpenFallout restores 30% when health is 0. (Part D reads this differently; see [the disagreements](#where-the-spec-parts-disagree).)
3. A downed essential character hit again: the exe restarts the 10 s timer (state 6 is not 3 or 5). OpenFallout takes the 30% branch (`mCombat.unconscious`).
4. `bEssentialTakeNoDamage` is never read (assumed 1). `sEssentialCharacterDown` is not shown. The `CanBeKnockedOut` test (0x8845A0) has no counterpart.
5. `essentialByMode` is New Vegas only. Fallout 3 teammates are essential by record and script only (not examined in the Fallout 3 exe).

**Missing for the round (round tools, not the engine):**
- A conversation helper in the round player script. `roundplayer.lua` has no question for `core.conversation.get/choose/close`. Autoplay's `pickTopic` (`autoplay.lua:105-141`) is the pattern. It needs three handlers (list options, choose a topic id, close).
- Pressing a message-box button by script number. `answerBox` only presses 1. `core.messageBox.press(n)` takes a position among the shown buttons (autoplay `buttonFor`, `autoplay.lua:54-62`), so a scenario for ED-E has to count shown buttons from the `ROUND box buttons=` line. Only needed for ED-E.
- A way to read a quest-script variable (for example `VNPCFollowers.bCassHired`, `bCassDead`). There is none in Lua (`questbindings.cpp` has stage, running, objectives and globals only). Substitutes in G.round use `GetPlayerTeammate`, `GetDead`, `GetUnconscious`, `IsEssential`, quest stages and the engine log.

### G.round Proposed round scenario (New Vegas; one engine start per variant)

Variants: `casual` (`r.hardcore(false)`) and `hardcore` (`r.hardcore(true)`), each with Cass. A third pair with ED-E comes later. All checks are numbers and ids, no game text. `NV = 'FalloutNV.esm'`. Calls through `r.core.combat.command(name, formId[, args])` return the value; log it.

**Preparation (every variant, in order; each step logs `ROUND ...`):**
1. **P1 start.** Load any New Vegas state in the Mojave (a post-intro save, or the autoplay hand-over). Log `r.eye('start')`. `r.hardcore(<variant>, 'mode')` must log `on=true|false`.
2. **P2 stand-ins.** `r.quest('VMS05', 30)` (the log must say `stage=30`). `r.give({NV, 0xF}, 800)`. `r.command('ForceActorValue', 'player', {32, 60})`. Check: `r.core.combat.command('GetActorValue', player, 32)` ≥ 50 and `GetItemCount` of caps ≥ 750. If a check fails, the scenario stops with `ROUND error`.
3. **P3 go.** `r.teleport{ cell = 'Mojave Outpost Barracks', pos = {2103, 1861, 9328}, yaw = 0, onGround = true, label = 'cass' }`. `r.settle(3, 30)`. Log `r.actors('near', 600, 8)`. The line must contain record 0x133FDD and a distance under 300. If not found: `ROUND error cass not there`.
4. **P4 baseline.** With ref id `cass = 0x135F19`: `GetPlayerTeammate` = 0, `IsEssential` = 0, `GetDead` = 0, `GetUnconscious` = 0. Log the quest stage of `VCassCompanion` (`r.quest('VCassCompanion')`) as the "before" value.

**Recruit (the same in both variants):**
5. **R1 talk.** `r.activate({NV, 0x135F19}, 'cass')`. Wait until the conversation is active and not speaking (poll `conversation.get()`; limit 40 s). Log `speaker=` (must equal Cass's id) and the options count.
6. **R2 picks.** For each topic id in the order 0x15C754, 0x15C753, 0x15C750, 0x15C74F, 0x15C740, 0x15C74E, 0x15C74A, 0x15C736, 0x15D047, 0x15D046: wait for `speaking == false`, check the option list contains the id, log `ROUND talk pick=<id> options=<n>`, choose it. A missing option is a `ROUND error` that names the step. The likely causes, in this order: VMS05 not at 30, caps below 750, Barter below 50. After the last pick the follow-up INFO 0x15D054 speaks and the conversation ends by itself (goodbye flag). Wait until `active == false` (limit 40 s).
7. **R3 check hire.** `GetPlayerTeammate`(cass) == 1. `GetPlayerTeammateCount` ≥ 1. Caps are 800 − 750 = 50 (the 750 is taken at Topic013). `IsEssential`(cass) == 1 in `casual` and == 0 in `hardcore` (this is the exe rule observed). VMS05 objective 35 is displayed (`quest:objectiveDisplayed(35)`). Log `r.inventory('cass', cassId)`.

**Knock out / kill:**
8. **K1 hit.** Make the player the killer: `r.core.combat.command('KillActor', cassId, playerId)` (the same entry the game's `Kill` uses). An alternative that also tests damage: `r.core.combat.strike(player, cass, <a pistol record>)` repeated until the answer has `killed` or `downed`. Log the return values. Wait 1 s.
9. **K2 casual checks (right after, t = 1 s).** `GetDead` == 0. `GetUnconscious` == 1. `IsEssential` == 1. `GetPlayerTeammate` == 1. `r.state('cass', cass)` logs `dead=false`. The `OnDeath` side effects did not happen: `VCassCompanion` stage equals the value from P4 (not 110).
10. **K3 casual wake (wait 16 s, no hostile in the cell).** `GetUnconscious` == 0, `GetDead` == 0, `GetPlayerTeammate` == 1. HP > 0 logged (do not compare the value; log it as `hp=` so the 1.0 / 30% / full question in G.unclear 1 is answered by the first real run). A second hit while down (optional, a separate variant): hit again at t = 5 s. Expect still `GetDead` == 0 and the timer restarting (exe), against 30% health (OpenFallout today). Log both `hp` and when `GetUnconscious` clears. This documents difference 3.
11. **K2 hardcore checks (t = 1 s).** `GetDead` == 1. `GetUnconscious` == 0. `IsEssential` == 0. Side effects: `VCassCompanion` stage == 110 and `quest.failed` true. `VMS05` objective 36 is not needed. Corpse: `r.inventory('corpse', cassId)` must list the shotgun 0x176E55 and the knife 0x176E56 (counts, ids, no text).
12. **K3 hardcore stays dead (wait 20 s).** `GetDead` still 1, `GetUnconscious` 0, no `ResurrectActor` log. `r.actors` shows the body (dead) where it fell.
13. **End.** `r.shot('end')` (optional). The round fails on any `ROUND error`, and on expectation lines: `expect_log` for `ROUND talk pick=0x15d046`, for the K2 and K3 lines. `forbid_log`: `ROUND error`, `A script of a response cannot be run` (`dialogue.cpp:343`) and `MoveToMarker: .* has no place`.

**ED-E variant (steps replacing 3-7).**
- Teleport to the cell by name `Nash Residence` at the position of `NVCompanionEDEDisabledREF` (look the object up, offset (0, -120, 0)).
- `r.give` 3 × SpareParts 0x31944, 2 × SensorModule 0x22109, 1 × ScrapElectronics 0x13B2B1. `r.boxes(false)`. Activate 0x1572E6.
- Answer the three boxes by script number 1, 3, 1. The positions depend on which buttons show. With the fresh values (Repair < 65 and no Science stand-in) the positions are 2, 3, 2 for boxes 00, 01, 04. Verify against the `ROUND box buttons=` lines: 3, 3 and 4 buttons.
- Wait 4 s for the 2 s fade script. `EDE1Ref` 0x1732D1 must then be enabled in the cell (`r.lookup` and `describe`).
- Activate it, pick topic 0x37084, and expect INFO 0x1579C3's result: `GetPlayerTeammate` == 1.
- Kill checks as above, with `bEDEDead` and `vDialogueEDE` stage 100 failed in `hardcore`, and no stage change in `casual`.
- Mark G.unclear 7, 8 and 9.

**What each step proves.** P2 and R2 prove the dialogue and quest conditions work on the real data. R3 proves `SetPlayerTeammate` and the 0x87F3D0 rule in both modes. K2 casual proves the down state and that no death script ran. K3 casual proves the timer. K2 and K3 hardcore prove death, the `OnDeath` effects, the left-behind corpse inventory, and that nothing brings the body back.

### G.unclear Open items in part G

Do not assert these in a round.

1. Health when the companion gets up. Exe evidence: at knock-down and at wake-up the routine 0x8A0960 raises health to 1.0 only if it is below 0 (exactly 0 stays 0), and restores the limb conditions. The 30% share (`fEssentialHealthPercentReGain` = 0.3, exe 0x1017870) is read in one place only: when a character that is unconscious (state 3) or restrained (5) takes another lethal blow (0x89DE9D). Cass's and ED-E's `OnCombatEnd` blocks call `ResetHealth` for teammates, which fills health, but the order against the wake-up was not proved. OpenFallout gives 30% at wake-up (`falloutscripts.cpp:4047-4057`; its own comment says "not known"). A round should assert only "alive, not unconscious, HP > 0".
2. The "player is in danger" flag that holds the teammate timer. In the per-frame actor update (0x888B50, around 0x889079), for a teammate in state 6 the timer is only decreased when `Player::0x953C50(0)` is false (byte at player + 0xDF0). The same byte feeds the HUD's Caution/Danger sneak text (0x7732D0). The spec reads it as "some hostile is hunting the player" but did not trace its writer 0x9931C0 to the end. OpenFallout counts the 10 s unconditionally.
3. Process-level condition. That recovery code only runs when the actor's process answers 3 or 1 from its vtable slot 0x40C. The levels were not named. A companion far from the player may recover differently. (Part A lists vt offset 0x40c of the process as `GetKnockedState`; the two parts do not say whether they mean the same call.)
4. The `sEssentialCharacterDown` message ("%s %s" with a Vault Boy icon) is shown at the end of the down routine (0x89E07E) when the state is 6. It was not proved that it is limited to teammates or to the player's view. OpenFallout shows nothing.
5. What 20.0 (0x1017868) in the down routine means. It is written to the HighProcess field at +0x33C right after the 10 s timer (+0xA4). Not identified.
6. Whether Cass's and ED-E's `EvaluatePackage` and follow packages really make them follow in OpenFallout. Not needed for the round (she stays in the cell). Not checked.
7. ED-E, first recruit, engine risk. The repair script does `EDE1Ref.MoveTo NVCompanionEDEDisabledREF`. `EDE1Ref` (0x1732D1) is a persistent, initially-disabled creature in the cell "HooverDam" (Hoover Dam Global Dummy Cell, 0x134FF5). OpenFallout's `MoveToMarker` logs "has no place the game knows" for references it cannot place (`falloutscripts.cpp:6520`). It needs one dry run.
8. Two `OnActivate` blocks in `EDEScriptDisabled` (`OnActivate Player` and plain `OnActivate`). Whether the exe runs both for one activation (two message boxes) or only the specific one was not checked. Use the log's `ROUND box` lines to see how many open.
9. Whether ED-E can be put down at all. The exe test 0x8845A0 ("can be knocked out") rejects a record with the "no knockdowns" flag (0x04000000), some process levels, and a creature flag 0x800000. ED-E's record flags (0x102000D0) have none of those flags, so the spec expects it to work, but did not run it. The follower script's one-time "reset ED-E if destroyed, not Hardcore" patch (`VNPCFollowersQuestSCRIPT`, `bFixedEde`) hints that the shipped game had cases where ED-E ended destroyed without Hardcore.
10. Starting values of a fresh post-intro character (Barter, Repair, Science, caps). The game was not run. Do not rely on them; the scenario sets what it needs.

---

## Not found in the exe

These are things the research looked for and did not find. For each, the table says what the spec suggests, or what follows from it. An engine value chosen here is a choice of this project, not a reading of the exe. The PR that builds it should say what it chose and why.

| What | Where it was looked for | What the spec suggests |
|---|---|---|
| Capsule height of the character controller | A.C2, A.unclear 4. 0xc72410 reads the 3D root bounds at `cinfo+0x80`; its arithmetic was not decoded | The human skeleton's `BSBound` is 46.2 wide, 35.3 deep and 132.4 tall (half extents 23.1 × 17.64 × 66.22). The spec did not prove the capsule uses it. Either keep today's 20 × 20 × 64 half-extent cylinder, or use radius 31 × scale and a height from the 3D bound. A round checks the choice |
| Step height | A.C2. The exe has no `fStep*` setting | Nothing in the exe proves the numbers OpenFallout uses (34 up, 62 down). The spec leaves the choice to the project |
| Gravity of the character body | A.C2, C.unclear 2. It comes from the Havok world. The INI list scanned has no gravity setting. The jump uses a gravity multiplier of 32.0 (C.J1) | Nothing proves today's 8.96 m/s² × 69.99. Say what was chosen and why |
| The physics of the body's velocity (how animation root motion and input become the body's velocity) | A.C3, A.unclear 6 | Follow the animation's movement, as the class names suggest (A.C3). Measure real speed in a round (B.unclear 1) |
| Angular turn rate | B.M11, B.unclear 7. Mouse turning has no speed constant | None found |
| A setting that does the job of `fSwimHeightScale` | D.S3, D.unclear 5 | Use the three depth lines of C.W1: 0.01, 0.7, 0.875 of body height |
| A setting that limits talk distance (`iMaxInfoDist`) | D.S2 | None needed. The dialogue menu fixes the target. Start a conversation as an ordinary activation (150 units) |
| The six `fAudio*` settings | D.S4 | Use the SOUN record's min and max. Keep a fallback for the 404 NV and 225 FO3 records with min = max = 0 |
| A name for life state 6 in part A | A.K1. Parts D and G name it "essential down" | Parts D and G use "essential down" |
| What applies `HeadCrippleEffect` when the head is crippled | D.L4, D.unclear 4. No reference to the spell ids in the exe | Scripts in the ESM probably apply it. Not implemented today |
| A death camera or input lock for the player's death | D.unclear 9 | None found. The player update keeps running during the 5 s |
| A refusal of a wait, sleep or fast travel that would kill (Hardcore) | F.unclear 3. The help text says it exists | Treat the help text as text only. No check was found |
| How the exe picks the `WaterHeal*` spells | F.unclear 4 | Presumably by water quality. Missing in the port |
| Which limbs the Doctor's Bag `NVRestoreLimbs` effect heals, and by how much | F.unclear 6 | Treat it as data |
| Where the global 0x11A0294 is set to 1 | F.unclear 7 | The spec believes it means "menu opened with no game loaded". Not ported |
| The number of chargen tag picks | E.unclear 2 | OpenFallout uses 3 (inferred from play knowledge) |
| A "companion nerve" stat in NV | E.1.11. No `fAVD*Nerve` setting exists in either ESM | None. n/a for step 4 |
| Default values of the ragdoll solver INI settings | A.K3 | Not read |
| FO3's `ProcessLists::Update`, FO3's submerged-fraction function and its constants, and FO3's ragdoll creator | A.T4, C.unclear 9, A.K3 | Where the structure matches, the spec assumes FO3 equals NV and marks the claim as inferred |
| A scale, per-direction or diagonal rule for movement | B.M9, B.unclear 2 | None found. All four directions use one speed number |
| What the form at vtable +0x428 (type 0x12) is, whose floats scale a non-player's speed | B.unclear 6 | Not identified. The player always gets "none" |

## Unverified

These were read or inferred but never confirmed by a run of the real game. A round must not enforce them as fact.

| Claim | Where | How to settle it |
|---|---|---|
| The movement speed number equals ground displacement in units per second | B.unclear 1, B.unclear 3 | Measure real displacement in the movement round and report what was found. Until then use ±5% (then ±2%) |
| A diagonal is not normalised | B.unclear 2 | Measure |
| Sneak plus run can happen (193.12 u/s holstered in NV) | B.unclear 4 | Read the input layer |
| FO3's weight argument is the actor's total carried weight (AV 46), and whether equipped items count | B.unclear 8 | Measure in FO3 with known inventory |
| The swim speed in water | B.M8, B.unclear 9 | Report as an observation within ±20% |
| Whether the player's input blocks jumping when over-encumbered | C.unclear 3 | Read the input layer. Do not test over-encumbered jumps until then |
| The size factor of the fresh player is 1.0 (so the apex is 64 units) | C.unclear 1 | Measure the apex |
| Whether landing in water avoids damage | C.F4, C.unclear 4 | Read more, or run |
| FO3's water lines (0.01, 0.7, 0.875) equal NV's | C.W1, C.unclear 9 | Run in FO3 |
| Weapons cannot be drawn while swimming | C.W3 | Online only. Check in the exe |
| The air-control scalar (180) and what it multiplies | C.J2, C.unclear 7 | Not decoded |
| Player death auto-reloads without a box at the default `fPlayerDeathReloadTime` | D.D2, D.unclear 1 | A hidden run of the real exe, or a re-read of the two branch instructions (NV 0x93EF6, FO3 0x78B1A1). The owner decision Q28 already follows the exe reading |
| What health an essential actor has when it gets up | D.D4, D.unclear 2, G.unclear 1 | The first real run. Log `hp=` and do not assert a value |
| The unit of `dt` in health regeneration (the simulated second) | D.unclear 3 | Trace the clock `[0x11E0E2C]` |
| The process flag (mask 0x20000000) that gates per-frame health regeneration | D.unclear 10 | Name the flag. Check whether it is off in combat |
| Third-person pick reach (`iActivatePickLength`) | D.unclear 6 | Read the camera-mode INI value 0x11C3EA4 |
| The ×5 unit of the sound distances | D.S4 | Online memory only. Check in the exe |
| The 3-frame slicing of the lower process tiers | A.T4, A.unclear 9 | Measure. It is a performance throttle, not a rule to copy |
| That "31" is the capsule radius, and the capsule height | A.C2 | Debug dump of the body in a live run |
| The teammate wake timer holding while the player is "in danger" (byte player + 0xDF0) | G.unclear 2 | Trace the writer 0x9931C0 |
| Whether current HP rises with max HP at level-up (the stored level, E.unclear 3, was settled in step 4a: it rises when the screen closes) | E.unclear 4 | A round checks the current health before and after the level-up screen |
| TTW's level cap is 50, not 60 | E.1.15, E.unclear 6 | Run TTW to the cap |
| FO3 rounds Luck up in the skill formula | E.1.9, E.unclear 9 | A round at Luck 5 or 7 in FO3 |
| The TTW rules in general | A.unclear 10 | The TTW plugins were not read for part A. NV rules apply by definition (owner decision Q11) |
| What `ToggleHardcoreTracking` does, and what `bHardcoreNeedsFix` changes | F.unclear 1, F.unclear 2 | Not readable from the exe. TTW numbers may differ if the patch changes the tick |
| Stage selection order in Hardcore (lowest or highest threshold first) | F.R8 | Treat "highest threshold ≤ level" as the rule |
| Whether the port skips the end-of-sleep limb restore in hardcore | F.R7 | Check the rest-completion code |
| Ammo weight with the 12-byte DAT2 fix | F.R11 | A round gives 100 × `.22LR` and expects +0.76 |
| Items and effects of Hardcore as data (food, Stimpaks, Rad-Away, DLC scripts) | F.R9, F.R10, F.R16 | Never run on real items in a round |

## Where the spec parts disagree

Each line names both sources. The page follows the stated reading unless it says otherwise.

1. **`fKnockdownCurrentHealthThreshold`.** Part A (A.K2) says 50. Part D (D.D5 and its settings table) says 25, as an exe default with "no ESM value". The dumps `tmp-death/gmst_nv.txt` (line 487), `tmp-death/gmst_fo3.txt` (line 368) and `tmp-actors/gm_all.txt` (line 83) all show 50.0 in both ESMs. The ESM value wins, so 50 is the value that runs. Part D's example round (a 40-damage hit on 100 HP rolls a 25% chance) uses 25 and would not roll at 50. Fix the round before it runs.
2. **Health when an essential actor gets up.** Part D (D.D4, D.unclear 2) reads 0x8A0960 as "heal all damage, +1, restore limbs to full" at both lying down and getting up. Part G (G.unclear 1) reads the same routine as raising health to 1.0 only if it is below 0, and restoring the limbs. Part F (F.R7) describes 0x8A0960 as restoring Health damage, Fatigue and limbs at the end of a non-hardcore sleep, which fits part D. Neither reading was run. Today's code gives 30% (`falloutscripts.cpp:4047-4057`). A round should assert only "alive, not unconscious, HP > 0".
3. **Exe default of `fAVDCarryWeightMult`.** Part B (B.M5a) says the NV exe defaults are 150 and 25. Part E (E.1.3) says base 150 and mult 10, the same in both ESMs and exes. Both agree the ESM value is 10 (`tmp-actors/gm_all.txt` shows 10.0 in both ESMs), so the value that runs is the same.
4. **The null parent cell in the tier rule.** Part A's step 3 of A.T2 says a null cell gives Middle Low (2). Part A's formula note says a null cell gives Low (3). Both are the spec's reading of the disassembly, not run.
5. **`fJumpMoveBase` and `fJumpMoveMult`.** Part A (A.C3) lists them as exe defaults in both games. Part C (C.J2) says NV does not read the settings: the 0.3 is a constant at 0x11B0140 and the base is a zero global at 0x1267BBC. FO3 copies the settings into globals and reads them. The numbers (0 and 0.3) agree.
6. **The NV address that reads `fSwimRadiationDamageMult`.** Part C (C.W1) cites 0x88A9A3. Part D (D.S3) cites 0x888B50. Part A lists 0x888B50 as the NV `Actor::Update` (vt+0x2f8). Whether 0x88A9A3 is inside it was not checked.
7. **Which part of the process holds the "3 or 1" in the companion recovery.** Part G (G.unclear 3) calls it the actor's process level read through vtable slot 0x40C. Part A lists process vt offset 0x40c as `GetKnockedState`. They may be the same call. Not resolved.
8. **The name of bit 4 of the player control mask (+0x680).** Part A's table (NVSE) counts bits by index: 0 movement, 1 look, 2 Pip-Boy, 3 fight. Part F and the owner decisions use the mask value: 1 movement, 2 looking, 4 Pip-Boy, 8 fighting. It is the same bit: the Pip-Boy control, which the needs timer must test (F.R5). The M16 code and `m16-needs-notes.md` mislabel it "Fighting".
9. **Death: box or auto-reload.** Rounds 12BQ and 12BR (and an earlier note, Mac round 4A H2) accepted the Reload / Main Menu box at +5.02 s. Part D reads the exe the other way. The owner decision Q28 follows part D. This is a change to today's behaviour. See D.D2.
10. **Cross-references inside part C.** C.J3 points to "movement.md M4" for crippled legs. In part B the crippled-leg rule is B.M7. B.M4 is sneak. This page uses B.M7.
11. **Life state 6.** Part A (A.K1) finds no name. Parts D and G call it "essential down". The sources agree on the behaviour: `IsDead(false)` counts it.
12. **Heal rate bonus.** Part D's summary says "(6-8 → 1, 9-10 → 2 in the exe; 5 and 10 in the NV ESM)". Part D's own D.R1 and part E (E.1.8) say both ESMs override to 5, 5, 5, 10, 10. This page uses the D.R1 and E.1.8 wording.

## What changes in the engine because of this page

The owner's decisions (Q9 and Q10) say there is no review stop, and that work stops only if a rule is surprising. These rules differ from today's engine in ways that change what a player sees:

- The player's walk and run speed (about 150 and 270 today; 84.7 and 338.8 in NV).
- Player death: auto-reload after `fPlayerDeathReloadTime`, instead of the Reload / Main Menu box.
- The fall height, the swim line and the pick reach (150, not 192).
- Skills that follow SPECIAL, and a Luck term that rounds up.
- NPC speed and body size (the NPC part is built in step 4b).

## What step 4a settled

PR "Make the player a Fallout actor with the original games' rules" builds these rules. Where it chose something the page left open, it says so.

- **Player record.** The player is PlayerRef 0x14 of NPC_ 0x7 of the first Fallout plugin. The placeholder records are gone.
- **Process tiers (A).** An actor whose parent cell is null is in the Low tier (NV 0x4511E0).
- **Character physics (A.C2).** Capsule radius 31 × size, half height 66.22 (the human skeleton's bound), slope 47 degrees. The step height stays 34 units: neither executable has a step setting. Gravity stays the Havok default of 9.8 m/s² (about 686 units/s²): the executables set none, and the jump of `fJumpHeightMin` 64 then takes about 0.86 s.
- **Water lines (C.W1).** Wading at 0.01 of the height, swimming at 0.7, head under at 0.875. `fSwimHeightScale` is gone.
- **Settings (D.S7).** The numeric defaults are read from the player's own executable and put under the plugin values. The 44 hand-copied fallbacks are gone. `fKnockdownCurrentHealthThreshold` runs at the data value 50; `fAVDCarryWeightMult` at the data value 10. A setting that neither has reads 0 and is logged once.
- **Morrowind settings (D table).** `iActivatePickLength` (150) replaces `iMaxActivateDist`; `sDefaultCellName` replaces `sDefaultCellname`; the six `fAudio*` are gone (a sound of a record uses the record's range; a voice and a sound by file name keep the distances they had); `fStromWindSpeed` and `fStromWalkMult` are gone with the storm push (Fallout weathers are never storms).
- **Death (D.D1, D.D2).** An actor is out of health below 1.0, whatever lowered it: damage, or `SetActorValue`, `ModActorValue` and `ForceActorValue` on the health (NV 0x898650 checks after each change). This holds for characters and creatures too; before, only damage killed them. The death flow auto-reloads after `fPlayerDeathReloadTime`.
- **Regeneration (D.R1).** The player heals by heal rate / 3600 each simulated second, and by the sleeping formula while it sleeps. The perk term is entry point 0xC worked out from 0, in points a second (NV 0x88B567, the player only; `ItemUse::update` adds it). Characters and creatures in the High and Middle High tiers heal by their own heal rate the same way: NV 0x8C3C40, the update of an actor with a process, calls 0x88B510 for any actor whose cached health is below its most (flag 0x20000000 of the cached values at process+0x2C, flags at +0x44; the name is from xNVSE, not traced). The heal rate is worked out for every actor.
- **Stats (E).** Action points, carry weight, critical chance and melee damage use the current attribute. Melee damage is floored. Unarmed damage and the poison and radiation resistances are derived. Half the Luck rounds up in the skills. The player's skills follow the SPECIAL of now (E.1.9): a skill has no stored base, the points of the level-ups and of the skill books are its permanent modifier, and a save of an engine before this keeps its points that way (the base it had minus the chargen value of the base attributes). The player's level is a stored level (E.unclear 3): the experience says when a level up is due, and the level goes up by one when the screen of that level is done (NV 0x7851F2, the screen's close, calls 0x8D5270: level + 1, then the pending test again). The health, the scripts' GetLevel, the Pip-Boy and the spawns go by the stored level, so the most health rises when the screen closes. A save keeps no level of its own: a loaded game is at the level handed out.
- **NPC speed (B).** A character walks and runs at the speed of the movement rules, the player's own formula. A creature goes at its animation's speed times its SpeedMult and its legs.
- **Sneak camera.** The games have no sneak eye-height setting. The camera of the first person is on the `Camera1st` node of the first-person skeleton (NV: "Missing 'Camera1st' on '%s'."), and the first-person sneak animations lower the whole skeleton: `Bip01 NonAccum` is at z −40 in `_1stperson/sneakmtidle.kf`, 0 in `mtidle.kf`. The port follows that node: the eye (124 units over the feet standing) goes down by as much as the animation of the arms takes `Camera1st` down, about 40 units while sneaking. m15-steps checks it.
- **Jump in water (C.W2).** A swimmer whose head is out of the water jumps twice as high (`jumpHeight` with swimming). The movement solver puts the jump in the inertia, lets it carry the swimmer over the swim line, and gravity brings it back; a swimmer with its head under the water does not jump. A jump from the water has no take-off height for the fall rule.
- **Left for 4c.** The get-up health of an essential actor.

## Sources

- **Exes.** `FalloutNV.exe` (sha256 `89e70204…`) and `Fallout3.exe`, disassembled; names from the exes' own tables and debug strings.
- **Game data.** `FalloutNV.esm`, `Fallout3.esm`, `BrokenSteel.esm`, `DeadMoney.esm`, `LonesomeRoad.esm`, the other add-on masters, `TaleOfTwoWastelands.esm` and TTW's merged `FalloutNV.esm`, and the skeleton NIFs and menu XML in the game's BSA archives.
- **Online (agreement only, never the only source for a number).** xNVSE headers ([GameObjects.h](https://github.com/xNVSE/NVSE/blob/master/nvse/nvse/GameObjects.h), [GameProcess.h](https://github.com/xNVSE/NVSE/blob/master/nvse/nvse/GameProcess.h)), the GECK wiki ([GetKnockedState](https://geck.uesp.net/wiki/GetKnockedState), [Movement Settings](https://geckwiki.com/index.php/Movement_Settings), [Stats List](https://geckwiki.com/index.php/Stats_List)), [Hit Points](https://fallout.fandom.com/wiki/Hit_Points), [New Vegas skills](https://fallout.fandom.com/wiki/Fallout:_New_Vegas_skills), [FO3 Resulting Statistics](https://strategywiki.org/wiki/Fallout_3/Resulting_Statistics), [Hardcore mode](https://fallout.fandom.com/wiki/Hardcore_mode), [New Vegas companions](https://fallout.fandom.com/wiki/Fallout:_New_Vegas_companions). Several of these pages were blocked or known only from search summaries.
- **In-repo notes.** [m16-needs-notes.md](m16-needs-notes.md), [m13-exe-reads.md](m13-exe-reads.md), [m14-notes.md](m14-notes.md).
- **Spec parts.** The seven research parts (A to G) and their helper scripts and dumps were kept outside the repository, in the owner's scratch area. Their working notes are not part of this page.
