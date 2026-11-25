-- The probe of the saves (M7), a world script that is loaded beside autoplayworld.lua by autoplaysaves.omwscripts. Where the
-- table `saves` of autoplaygates.lua says (`save`: a quest has stood still at its stage for `after` real seconds), the game is
-- saved with the description `autoplay-save`; the quests (stage and whether they run) are written into the save and logged
-- ("AUTOPLAY digest saved"). Where `load` says the same, the game is loaded back in the same engine, once. A game that was
-- loaded from the save, in this engine or in a new one (--load-savegame), logs the quests as it has them as soon as it runs
-- ("digest loaded"), says whether they are those of the save ("digest check ok" or "MISMATCH") and saves again a few seconds
-- later with the description `autoplay-again`, so that the two files can be compared (scripts/openfallout/savediff.py). The
-- phase is kept in the save: 'play' until the game is saved, 'saved' in that save, and 'resaved' in the second. The saving and
-- the loading are done by autoplaysaves.lua, a menu script. Lines start with AUTOPLAY.
--
-- Besides the quests the digest holds the player (step 4): where it stands (cell, and position within POSITION_TOLERANCE), its
-- level, experience, health and maximum health, SPECIAL (base and value now of each attribute) and how many items it carries.
-- It is taken in onSave, so that it is the player of the moment the file was written, and compared on the first frame after the
-- load: `digest check ok` means the quests AND the player are those of the save. Where each value is read from (the calls that
-- the Fallout player of step 4 must keep answering, and none of them a Morrowind stats function):
--   cell, position     world.players[1].cell / .position           (the player's reference: the engine's own place of it)
--   level, experience  core.pipboy.stats().level / .experience     (Fallout's PlayerProgress)
--   health, maximum    core.pipboy.stats().health / .maxHealth     (the actor value Health and its maximum, as the HUD shows them)
--   SPECIAL            core.pipboy.attributes()[i].base / .value   (the seven actor values 5 to 11, base and current)
--   item count         core.pipboy.items(): the sum of `count`     (what the Pip-Boy lists the player as carrying, in units)
-- A save from before the player was in the digest (the old-save round) has none: the line says so and only the quests are compared;
-- the old-save round holds the numbers that its file must give.
local core = require('openfallout.core')
local types = require('openfallout.types')
local world = require('openfallout.world')
local config = require('scripts.autoplaygates')

local saves = config.saves or {}
-- The quests that make the digest: those of the two intros, and any other that a gate waits for
local QUESTS = { 'CG00', 'CG01', 'CG02', 'CG03', 'CG04', 'CGTutorial', 'MQ01', 'VCG00', 'VCG01', 'VCG02', 'VCG03', 'VCG04',
    'VMQ01' }
local watched, known = {}, {}
local function watch(name)
    if not known[name] then
        known[name] = true
        table.insert(watched, name)
    end
end
for _, name in ipairs(QUESTS) do watch(name) end
for _, gate in ipairs(config.gates) do watch(gate.quest) end
for _, list in ipairs({ saves.save or {}, saves.load or {}, saves.fixtures or {} }) do
    for _, want in ipairs(list) do watch(want.quest) end
end

local phase, savedDigest, savedPlayer = 'play', nil, nil
-- the saves of the fixtures (`saves.fixtures` of the table: a quest at a stage for `after` seconds): each is written once
local fixtureDone = {}
local started = core.getRealTime()
-- real seconds since the stage of each quest changed
local stage, since = {}, {}
local loadedAt, checked, askedResave = nil, false, false

local function digest()
    local parts = {}
    for _, name in ipairs(watched) do
        local quest = world.quests.find(name)
        if quest then table.insert(parts, string.format('%s=%d/%s', name, quest.stage, quest.running and 'r' or '-')) end
    end
    return table.concat(parts, ' ')
end


-- ---- the player of the digest -------------------------------------------------------------------------------------------------
-- How far (game units, about 1.5 m) the player may be from where it was saved on the first frame of the loaded game: the autoplay
-- tool is moving it between the save and the file being written, and the player of the loaded game has had a frame to move
local POSITION_TOLERANCE = 150
-- How much health may differ: the first frame of a game has had a moment of regeneration at most
local HEALTH_TOLERANCE = 1.0

local function cellText(cell)
    if cell == nil then return 'nocell' end
    if cell.isExterior then return string.format('%s:%d,%d', tostring(cell.worldSpaceId), cell.gridX or 0, cell.gridY or 0) end
    return tostring(cell.id)
end

-- The player now as a table (nil and the error when the Fallout calls fail)
local function playerState()
    local player = world.players[1]
    if player == nil then return nil, 'no player' end
    local ok, result = pcall(function()
        local stats = core.pipboy.stats()
        local special = {}
        for _, row in ipairs(core.pipboy.attributes()) do
            special[#special + 1] = { number = row.number, base = row.base, value = row.value }
        end
        local rows, units = 0, 0
        for _, item in ipairs(core.pipboy.items()) do
            rows, units = rows + 1, units + (item.count or 0)
        end
        local p = player.position
        return { cell = cellText(player.cell), x = p.x, y = p.y, z = p.z, level = stats.level, xp = stats.experience,
            health = stats.health, maxHealth = stats.maxHealth, special = special, rows = rows, units = units }
    end)
    if not ok then return nil, tostring(result) end
    return result
end

local function specialText(special)
    local parts = {}
    for _, row in ipairs(special) do parts[#parts + 1] = string.format('%d=%g/%g', row.number, row.base, row.value) end
    return table.concat(parts, ',')
end

local function playerText(state)
    return string.format('cell=%s pos=%.0f,%.0f,%.0f level=%.0f xp=%.0f hp=%.2f/%.2f special=%s items=%d rows=%d', state.cell, state.x, state.y,
        state.z, state.level, state.xp, state.health, state.maxHealth, specialText(state.special), state.units, state.rows)
end

-- The fields of the player that are not those of the saved one, as words (an empty list: the same player)
local function playerMismatches(saved, now)
    local bad = {}
    if saved.cell ~= now.cell then table.insert(bad, string.format('cell %s -> %s', saved.cell, now.cell)) end
    local distance = math.sqrt((saved.x - now.x) ^ 2 + (saved.y - now.y) ^ 2 + (saved.z - now.z) ^ 2)
    if distance > POSITION_TOLERANCE then table.insert(bad, string.format('position moved %.0f units', distance)) end
    if saved.level ~= now.level then table.insert(bad, string.format('level %s -> %s', tostring(saved.level), tostring(now.level))) end
    if saved.xp ~= now.xp then table.insert(bad, string.format('xp %s -> %s', tostring(saved.xp), tostring(now.xp))) end
    if math.abs(saved.health - now.health) > HEALTH_TOLERANCE then
        table.insert(bad, string.format('health %.2f -> %.2f', saved.health, now.health))
    end
    if math.abs(saved.maxHealth - now.maxHealth) > 0.01 then
        table.insert(bad, string.format('maxHealth %.2f -> %.2f', saved.maxHealth, now.maxHealth))
    end
    if specialText(saved.special) ~= specialText(now.special) then
        table.insert(bad, 'special ' .. specialText(saved.special) .. ' -> ' .. specialText(now.special))
    end
    if saved.units ~= now.units then table.insert(bad, string.format('items %d -> %d', saved.units, now.units)) end
    return bad
end

-- Whether a screen is open (the player script tells every second); a fixture is written only while none is
local screenOpen = true

local function askMenu(event, data)
    types.Player.sendMenuEvent(world.players[1], event, data)
end

-- Whether one of the quests of the list has stood at its stage long enough (`after` of the entry, else of the table)
local function stoodAt(wants, real)
    for _, want in ipairs(wants or {}) do
        local quest = world.quests.find(want.quest)
        local wait = want.after or saves.after or 5
        if quest and quest.stage == want.stage and real - (since[want.quest] or real) >= wait then
            return true
        end
    end
    return false
end

return {
    eventHandlers = {
        AutoplayMode = function(data) screenOpen = data == nil or data.open ~= false end,
    },
    engineHandlers = {
        onSave = function()
            -- the player of the moment the file is written (a save from before this had none: `player` is nil there)
            local state, err = playerState()
            if state then
                print('AUTOPLAY', 'player', 'saved', playerText(state))
            else
                print('AUTOPLAY', 'player', 'error', 'save', tostring(err))
            end
            return { phase = phase, digest = savedDigest, player = state }
        end,
        onLoad = function(data)
            if data == nil then return end
            phase, savedDigest, savedPlayer = data.phase or 'play', data.digest, data.player
            print('AUTOPLAY', 'loaded', 'phase=' .. phase)
            if phase == 'saved' then loadedAt = core.getRealTime() - started end
        end,
        onUpdate = function()
            local real = core.getRealTime() - started
            for _, name in ipairs(watched) do
                local quest = world.quests.find(name)
                if quest and stage[name] ~= quest.stage then
                    stage[name] = quest.stage
                    since[name] = real
                end
            end
            if loadedAt ~= nil then
                -- the game was loaded from the save: the first frame tells what the quests are, a few seconds later it is
                -- saved again
                if not checked then
                    checked = true
                    local now = digest()
                    print('AUTOPLAY', 'digest', 'loaded', now)
                    local player, err = playerState()
                    local same = now == savedDigest
                    if player == nil then
                        print('AUTOPLAY', 'player', 'error', 'load', tostring(err))
                        same = false
                    else
                        print('AUTOPLAY', 'player', 'loaded', playerText(player))
                        if savedPlayer == nil then
                            -- a save of an older engine: there is no player in its digest, the quests are all there is to compare
                            print('AUTOPLAY', 'player', 'no digest in this save')
                        else
                            local bad = playerMismatches(savedPlayer, player)
                            if #bad > 0 then
                                same = false
                                print('AUTOPLAY', 'player', 'MISMATCH', table.concat(bad, '; '))
                            end
                        end
                    end
                    print('AUTOPLAY', 'digest', same and 'check ok' or 'check MISMATCH')
                end
                if not askedResave and real - loadedAt >= (saves.resaveAfter or 3) then
                    askedResave = true
                    phase = 'resaved'
                    askMenu('AutoplaySave', { description = 'autoplay-again' })
                end
            elseif phase == 'play' and stoodAt(saves.save, real) then
                phase, savedDigest = 'saved', digest()
                print('AUTOPLAY', 'digest', 'saved', savedDigest)
                askMenu('AutoplaySave', { description = 'autoplay-save' })
            elseif phase == 'saved' and stoodAt(saves.load, real) then
                phase = 'loading'
                askMenu('AutoplayLoad', { description = 'autoplay-save' })
            end
            -- The fixtures: a save for the rounds of step 4 to start from, written where the game is outside its intro and the player has
            -- its controls (a quest at a stage for `after` seconds), once each, whatever the phase of the probe is
            for index, want in ipairs(saves.fixtures or {}) do
                if not fixtureDone[index] and not screenOpen and stoodAt({ want }, real) then
                    fixtureDone[index] = true
                    print('AUTOPLAY', 'fixture', want.description, want.quest, 'stage=' .. tostring(want.stage))
                    askMenu('AutoplaySave', { description = want.description })
                end
            end
        end,
    },
}
