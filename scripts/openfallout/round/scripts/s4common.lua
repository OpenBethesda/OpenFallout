-- The common part of the rounds of step 4 (the player as a Fallout actor): the numbers the rules give (movement.md, jump-fall-swim.md,
-- death-condition-settings.md and stats-levelling.md of the rules spec, from the exes of the two games) and how a round says that a
-- measured number is right or wrong.
--
-- A check logs one line that a person or a search finds with `grep 's4 measure'`:
--     s4 measure walk_holstered_api=84.700 expect=84.700 tol=0.05 ok
-- `tol` is the allowed difference: `5%` of the expected number, or an absolute number, or `3%|1` when either is enough ("3 % or 1 HP,
-- whichever is larger"). A number outside it is a `ROUND error` line, so that round.py's verdict is FAIL and the report lists it
-- under "scenario errors": there are no retries, no widening, no skips here.
local M = {}

-- ---- how a number is judged ------------------------------------------------------------------------------------------------------

local function within(got, want, tol)
    local difference = math.abs(got - want)
    if tol.abs ~= nil and difference <= tol.abs then return true end
    if tol.rel ~= nil and difference <= tol.rel * math.abs(want) then return true end
    return false
end

local function toleranceText(tol)
    local parts = {}
    if tol.rel ~= nil then table.insert(parts, string.format('%g%%', tol.rel * 100)) end
    if tol.abs ~= nil then table.insert(parts, string.format('%g', tol.abs)) end
    return table.concat(parts, '|')
end

-- Says whether `got` is `want` within `tol` ({ abs = n }, { rel = fraction } or both: either is enough). `why` says in one sentence where the
-- tolerance comes from or what a failure means; it is on the line of a failure.
function M.check(r, name, got, want, tol, why)
    if got == nil then
        r.error(string.format('s4 measure %s=none expect=%.3f tol=%s FAIL not measured', name, want, toleranceText(tol)))
        return false
    end
    local ok = within(got, want, tol)
    local line = string.format('s4 measure %s=%.3f expect=%.3f tol=%s %s', name, got, want, toleranceText(tol), ok and 'ok' or 'FAIL')
    if ok then r.log(line) else r.error(line .. (why and (' ' .. why) or '')) end
    return ok
end

-- A yes or no (the tolerance is none)
function M.flag(r, name, got, want, why)
    local ok = (got == want)
    local line = string.format('s4 measure %s=%s expect=%s tol=exact %s', name, tostring(got), tostring(want), ok and 'ok' or 'FAIL')
    if ok then r.log(line) else r.error(line .. (why and (' ' .. why) or '')) end
    return ok
end

-- A number that is looked at and not judged (the spec marks it unverified, or it only explains another)
function M.observe(r, name, value, note)
    r.log(string.format('s4 observe %s=%s%s', name, type(value) == 'number' and string.format('%.3f', value) or tostring(value),
        note and (' ' .. note) or ''))
end

-- ---- the games ---------------------------------------------------------------------------------------------------------------------

M.games = {
    nv = {
        file = 'FalloutNV.esm',
        pistol = 0xe3778,                         -- WeapNV9mmPistol (one-handed: no weapon penalty in the speed)
        heavy = 0x7cbcc,                          -- WoodChipper (a miscellaneous item of 50 lb)
        -- open flat ground of the Mojave east of the doctor's house of Goodsprings (m15_steps.lua: 8340 to 8370 high)
        arena = { x = -69660, y = 1900, z = 8352 },
        mojave = { 0x0daeb9, 0x0daeb8, 0x0daebb },
    },
    fo3 = {
        file = 'Fallout3.esm',
        pistol = 0x434f,                          -- Weap10mmPistol
        heavy = 0x7cbcc,                          -- WoodChipper (50 lb)
        -- the flattest ground of the Capital Wasteland near its middle that the land records give (cell 14,-9: 40 units between the
        -- highest and lowest point of the cell, 10,776 high on average), a quarter in from the south west corner
        cell = 0x000cbb,
        arena = { x = 14 * 4096 + 1024, y = -9 * 4096 + 256, z = 10800 },
    },
}

-- Takes the player to the start of the arena, on the ground, facing north
function M.goArena(r, game, label, yaw)
    local g = M.games[game]
    local o = { pos = { g.arena.x, g.arena.y, g.arena.z }, yaw = yaw or 0, onGround = true, label = label }
    if g.mojave then
        local mojave
        for _, id in ipairs(g.mojave) do
            local ok, cell = pcall(function() return r.world.getCellById(r.core.getFormId(g.file, id)) end)
            if ok and cell then mojave = cell break end
        end
        if mojave == nil then error('no cell of the Mojave was found') end
        o.worldspace = mojave
    else
        o.cellId = r.core.getFormId(g.file, g.cell)
    end
    return r.teleport(o)
end

-- Form ids as text
function M.form(r, game, id) return r.core.getFormId(M.games[game].file, id) end

-- Puts items of a record in the inventory of the player (the script command AddItem, as the games' own scripts do)
function M.addItem(r, game, id, count)
    r.command('AddItem', 'player', { M.form(r, game, id), count or 1 })
end

-- ---- the rules: movement -------------------------------------------------------------------------------------------------------------

-- The speed the games give the player (units a second): `fMoveBaseSpeed` 77, the holstered bonus `fMoveNoWeaponMult` 1.1, the sneak
-- factor `fMoveSneakMult` 0.57 and the run factor `fMoveRunMult` 4; in Fallout 3 also the weight term `1 - k * min(weight, 150) / 150`
-- with k 0.3 holstered and 0.4 drawn (`fMoveEncumEffectNoWeapon`, `fMoveEncumEffect`, `fMoveWeightMax`: from the exe, no plugin has
-- them). New Vegas (and Tale of Two Wastelands) has no weight term. `o`: drawn, run, sneak, weight. movement.md M1 to M5.
function M.speed(game, o)
    local speed = 77.0
    if not o.drawn then speed = speed * 1.1 end
    if game == 'fo3' then
        local weight = math.min(o.weight or 0, 150)
        speed = speed * (1 - (o.drawn and 0.4 or 0.3) * weight / 150)
    end
    if o.sneak then speed = speed * 0.57 end
    if o.run then speed = speed * 4.0 end
    return speed
end

-- ---- the rules: falling ----------------------------------------------------------------------------------------------------------------

-- 0.025 * (lost - 600) ^ 1.65 above 600 units (fJumpFallHeightMin 600, fJumpFallHeightExponent 1.65, fJumpFallHeightMult 0.025); none
-- below. At normal difficulty. jump-fall-swim.md F2.
function M.fallDamage(lost)
    if lost <= 600 then return 0 end
    return 0.025 * (lost - 600) ^ 1.65
end

-- ---- the rules: levelling ------------------------------------------------------------------------------------------------------------

-- The experience at which a level begins: (L-1) * iXPBase 200 + iXPBumpBase 150 * (L-1)(L-2)/2. stats-levelling.md 1.12.
function M.xpForLevel(level)
    if level <= 1 then return 0 end
    return (level - 1) * 200 + 150 * (level - 1) * (level - 2) / 2
end

-- The points to spend at the screen of a level: Fallout 3 11 + min(Int - 1, 10); New Vegas floor(10 + 0.5 * min(Int, 10)), one more when
-- the new level is even and Int is odd. `level` is the new level. stats-levelling.md 1.13.
function M.skillPoints(game, intelligence, level)
    if game == 'fo3' then return 11 + math.min(intelligence - 1, 10) end
    local m = math.min(math.floor(intelligence), 10)
    local points = math.floor(10 + 0.5 * m)
    if level % 2 == 0 and m % 2 == 1 then points = points + 1 end
    return points
end

-- Whether the screen of the new level offers a perk: every level in Fallout 3; in New Vegas when the level is a multiple of
-- iLevelsPerPerk (2; Tale of Two Wastelands sets 1). stats-levelling.md 1.14.
function M.perkOffered(game, level)
    if game == 'fo3' then return true end
    return level % 2 == 0
end

-- Maximum health: the record's 100 + Endurance * 20 + (level - 1) * 5 (New Vegas) or 10 (Fallout 3). stats-levelling.md 1.1.
function M.maxHealth(game, endurance, level)
    return 100 + endurance * 20 + (level - 1) * (game == 'fo3' and 10 or 5)
end

-- The number of the actor values of SPECIAL (the exes' own indexes) and the attribute that governs each skill (by the number of the
-- skill). stats-levelling.md 1.9.
M.STRENGTH, M.PERCEPTION, M.ENDURANCE, M.CHARISMA, M.INTELLIGENCE, M.AGILITY, M.LUCK = 5, 6, 7, 8, 9, 10, 11
M.skillAttribute = {
    [32] = M.CHARISMA, [33] = M.ENDURANCE, [34] = M.PERCEPTION, [35] = M.PERCEPTION, [36] = M.PERCEPTION, [37] = M.INTELLIGENCE,
    [38] = M.STRENGTH, [39] = M.INTELLIGENCE, [40] = M.INTELLIGENCE, [41] = M.AGILITY, [42] = M.AGILITY, [43] = M.CHARISMA,
    [44] = M.ENDURANCE, [45] = M.ENDURANCE,
}

-- A skill: 2 + 2 * its attribute (now) + ceil(Luck / 2) + 15 when tagged. `attributes` is the list of the stats table (number, value).
function M.skillValue(skill, attributes)
    local byNumber = {}
    for _, row in ipairs(attributes) do byNumber[row.number] = row.value end
    local governing = M.skillAttribute[skill.number]
    if governing == nil or byNumber[governing] == nil or byNumber[M.LUCK] == nil then return nil end
    return 2 + 2 * byNumber[governing] + math.ceil(0.5 * byNumber[M.LUCK]) + (skill.tagged and 15 or 0)
end

-- The value of an attribute (`value` now, or `base`) by its number
function M.attribute(stats, number, field)
    for _, row in ipairs(stats.attributes or {}) do
        if row.number == number then return row[field or 'value'] end
    end
    return nil
end

-- The skills that are tagged. The Pip-Boy's skill rows carry no tag flag, so the tags are read from the first look at the character: a
-- tagged skill stands 15 above the formula and an untagged one at most one under it (the rounding of Luck), so 7 above is a tag.
function M.taggedSkills(stats)
    local tags = {}
    for _, skill in ipairs(stats.skills or {}) do
        local untagged = M.skillValue({ number = skill.number, tagged = false }, stats.attributes or {})
        if skill.tagged == true or (untagged ~= nil and skill.value - (skill.modifier or 0) - untagged >= 7) then tags[skill.number] = true end
    end
    return tags
end

-- How many skills are not what the formula says, with the first few of them as words (skill number: got, wanted). `tags`: the skills
-- that are tagged (taggedSkills of the first look; the flag of the row itself counts too)
function M.skillMismatches(stats, tags)
    local count, words = 0, {}
    for _, skill in ipairs(stats.skills or {}) do
        local want = M.skillValue({ number = skill.number, tagged = skill.tagged == true or (tags ~= nil and tags[skill.number] == true) },
            stats.attributes or {})
        -- what the level-up points and the books added for good is on top of the formula (an engine without the field: none)
        local got = skill.value - (skill.modifier or 0)
        if want ~= nil and math.abs(got - want) > 0.001 then
            count = count + 1
            if #words < 6 then table.insert(words, string.format('%d:%g/%g', skill.number, got, want)) end
        end
    end
    return count, table.concat(words, ',')
end

return M
