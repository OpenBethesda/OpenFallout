---
-- Allows for manipulation of the data loaded from content files while the game is first started.
-- Records can be created and deleted using this package as if a content file had done so.
-- @context load
-- @module content
-- @usage local content = require('openfallout.content')

--- @{#ActivatorContent}: Activator manipulation.
-- @field [parent=#content] #ActivatorContent activators

---
-- A mutable list of all @{openfallout.types#ActivatorRecord}s.
-- @field [parent=#ActivatorContent] #list<openfallout.types#ActivatorRecord> records
-- @usage
-- content.activators.records.MyActivator = { mwscript = 'float', model = 'meshes/w/w_chitin_arrow.nif', name = 'Quest marker' }

--- @{#ApparatusContent}: Apparatus manipulation.
-- @field [parent=#content] #ApparatusContent apparatuses

--- @{openfallout.types#ApparatusTYPE}: Apparatus types
-- @field [parent=#ApparatusContent] openfallout.types#ApparatusTYPE TYPE

---
-- A mutable list of all @{openfallout.types#ApparatusRecord}s. Record IDs are immutable.
-- @field [parent=#ApparatusContent] #list<openfallout.types#ApparatusRecord> records
-- @usage
-- content.apparatuses.records.MyAlembic = { name = 'Industrial Alembic', model = 'meshes/a/alembic.nif', icon = 'icons/a/alembic.dds', type = content.apparatuses.TYPE.Alembic, quality = 2.5, weight = 4, value = 300 }
-- @usage
-- content.apparatuses.records.BetterAlembic = { template = content.apparatuses.records.MyAlembic, quality = 10 }

--- @{#BookContent}: Book manipulation.
-- @field [parent=#content] #BookContent books

---
-- A mutable list of all @{openfallout.types#BookRecord}s.
-- @field [parent=#BookContent] #list<openfallout.types#BookRecord> records
-- @usage
-- content.books.records.MyBook = { template = content.books.records['bk_lustyargonianmaid'], text = content.books.records['bk_BoethiahPillowBook'].text }

--- @{#ClassContent}: Class manipulation.
-- @field [parent=#content] #ClassContent classes

---
-- A mutable list of all @{openfallout.types#ClassRecord}s.
-- @field [parent=#ClassContent] #list<openfallout.types#ClassRecord> records
-- @usage
-- content.classes.records.MyClass = { name = 'Nightbonk', template = content.classes.records['Nightblade'], majorSkills = { 'BluntWeapon', 'Mysticism', 'Illusion', 'Alteration', 'Sneak' } }

--- @{#DoorContent}: Door manipulation.
-- @field [parent=#content] #DoorContent doors

---
-- A mutable list of all @{openfallout.types#DoorRecord}s.
-- @field [parent=#DoorContent] #list<openfallout.types#DoorRecord> records
-- @usage
-- content.doors.records.MyDoor = { template = content.doors.records['door_dwrv_double00'], mwscript = 'blockedDoor', name = 'Overly Heavy Dwemer Door' }

--- @{#FactionContent}: Faction manipulation.
-- @field [parent=#content] #FactionContent factions

---
-- A mutable list of all @{openfallout.core#FactionRecord}s.
-- @field [parent=#FactionContent] #list<openfallout.core#FactionRecord> records
-- @usage
-- content.factions.records.MyFaction = { name = 'Peons', hidden = false, reactions = { Hlaalu = -1, Telvanni = 1 }, attributes = { 'Luck', 'Willpower' }, skills = { 'Destruction', 'HeavyArmor' }, ranks = { { name = 'Peon' }, { name = 'Minion', attributeValues = { 50, 50 }, primarySkillValue = 100 } } }

--- @{#GMSTContent}: GMST manipulation.
-- @field [parent=#content] #GMSTContent gameSettings

---
-- Returns a table containing all fallback values defined in `openmw.cfg`.
-- @function [parent=#GMSTContent] getFallbacks
-- @return #table

---
-- A mutable list of all game settings.
-- @field [parent=#GMSTContent] #map<#string, #any> records
-- @usage
-- content.gameSettings.records.fJumpAcrobaticsBase = 1024

--- @{#GlobalContent}: Global variable manipulation.
-- @field [parent=#content] #GlobalContent globals

---
-- A mutable list of all global mwscript variables.
-- @field [parent=#GlobalContent] #map<#string, #number> records
-- @usage
-- content.globals.records.MyVariable = 42

--- @{#IngredientContent}: Ingredient manipulation.
-- @field [parent=#content] #IngredientContent ingredients

---
-- A mutable list of all @{openfallout.types#IngredientRecord}s.
-- Note that ingredient effects only have the `id`, `affectedAttribute`, and `affectedSkill` properties.
-- @field [parent=#IngredientContent] #list<openfallout.types#IngredientRecord> records
-- @usage
-- content.ingredients.records.MyIngredient = { template = content.ingredients.records['ingred_ectoplasm_01'], name = 'Soylent', effects = { { id = 'vampirism' } } }

--- @{#LevCreatureContent}: Levelled creature manipulation.
-- @field [parent=#content] #LevCreatureContent levelledCreatures

---
-- A mutable list of all @{openfallout.types#CreatureLevelledListRecord}s.
-- @field [parent=#LevCreatureContent] #list<openfallout.types#CreatureLevelledListRecord> records
-- @usage
-- content.levelledCreatures.records.MyLevCreature = { calculateFromAllLevels = true, chanceNone = 0.5, creatures = { { id = 'scrib', level = 1 }, { id = 'fargoth', level = 2 } } }

--- @{#LevItemContent}: Levelled item manipulation.
-- @field [parent=#content] #LevItemContent levelledItems

---
-- A mutable list of all @{openfallout.types#ItemLevelledListRecord}s.
-- @field [parent=#LevItemContent] #list<openfallout.types#ItemLevelledListRecord> records
-- @usage
-- content.levelledItems.records.MyLevItem = { calculateForEach = true, calculateFromAllLevels = true, items = { { id = 'keening', level = 1 }, { id = 'sunder', level = 1 } } }

--- @{#LightContent}: Light manipulation.
-- @field [parent=#content] #LightContent lights

---
-- A mutable list of all @{openfallout.types#LightRecord}s.
-- @field [parent=#LightContent] #list<openfallout.types#LightRecord> records
-- @usage
-- content.lights.records.MyLight = { template = content.lights.records['torch'], duration = -1, name = 'Infinite Torch' }

--- @{#LockpickContent}: Lockpick manipulation.
-- @field [parent=#content] #LockpickContent lockpicks

---
-- A mutable list of all @{openfallout.types#LockpickRecord}s.
-- @field [parent=#LockpickContent] #list<openfallout.types#LockpickRecord> records
-- @usage
-- content.lockpicks.records.MyLockpick = { template = content.lockpicks.records['skeleton_key'], name = 'Digipick' }

--- @{#MiscContent}: Misc manipulation.
-- @field [parent=#content] #MiscContent miscs

---
-- A mutable list of all @{openfallout.types#MiscellaneousRecord}s.
-- @field [parent=#MiscContent] #list<openfallout.types#MiscellaneousRecord> records
-- @usage
-- content.miscs.records.MyMisc = { template = content.miscs.records['gold_001'], mwscript = 'BILL_MarksSpiritSummon', weight = 5 }

--- @{#ProbeContent}: Probe manipulation.
-- @field [parent=#content] #ProbeContent probes

---
-- A mutable list of all @{openfallout.types#ProbeRecord}s.
-- @field [parent=#ProbeContent] #list<openfallout.types#ProbeRecord> records
-- @usage
-- content.probes.records.MyProbe = { template = content.probes.records['probe_bent'], quality = 5, name = 'Alien Probe' }

--- @{#RaceContent}: Race manipulation.
-- @field [parent=#content] #RaceContent races

---
-- A mutable list of all @{openfallout.types#RaceRecord}s.
-- @field [parent=#RaceContent] #list<openfallout.types#RaceRecord> records
-- @usage
-- content.races.records.MyRace = { template = content.races.records['wood elf'], name = 'Sheep Elf', height = { male = 2, female = 2 }, weight = { male = 2, female = 2 } }

--- @{#RepairContent}: Repair item manipulation.
-- @field [parent=#content] #RepairContent repairs

---
-- A mutable list of all @{openfallout.types#RepairRecord}s.
-- @field [parent=#RepairContent] #list<openfallout.types#RepairRecord> records
-- @usage
-- content.repairs.records.MyRepair = { template = content.repairs.records['hammer_repair'], name = 'Hammer Time' }

--- @{#StaticContent}: Static manipulation.
-- @field [parent=#content] #StaticContent statics

---
-- A mutable list of all @{openfallout.types#StaticRecord}s.
-- @field [parent=#StaticContent] #list<openfallout.types#StaticRecord> records
-- @usage
-- content.statics.records.MyStatic = { model = 'meshes/b/B_N_Wood Elf_M_Head_02.nif' }

--- @{#SoundContent}: Sound manipulation.
-- @field [parent=#content] #SoundContent sounds

---
-- A mutable list of all @{openfallout.core#SoundRecord}s.
-- @field [parent=#SoundContent] #list<openfallout.core#SoundRecord> records
-- @usage
-- content.sounds.records.MySound = { template = content.sounds.records['MournDayAmb'], fileName = 'sound/fx/funny.wav' }

return nil
