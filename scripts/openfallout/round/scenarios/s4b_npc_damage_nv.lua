-- Step 4b, NPC damage (New Vegas): a hostile character's blows lower the player's health by the amount of the rules. God mode is off
-- and the player wears nothing, so no armor stands between the blow and the health (DT 0, DR 0) and the difficulty is the save's.
--   M  a person who fights with what it carries (GS Settler AAM, ACHR 0x104F03): any blow that lands lowers the health
--   F  Powder Gangers with guns (the placed ones of round 14A, cell -6,-10): a shot that lands lowers the health
--   G  a feral ghoul (CREA 0x167ECC VCrTier1FeralGhoul1A, made by PlaceAtMe): its blow is 15.5 by the rule (see GHOUL_BLOW), and the
--      player at Normal difficulty takes it whole (fDiffMultHPToPCN 1.0)
-- The player's health is read with GetActorValue 16 ten times a second; every drop is one line (`drop`) and the checks read them.
local NV = 'FalloutNV.esm'
local GHOUL = 0x167ECC
-- A blow of the ghoul (fallout-rules.md D and E): the record's damage 20 (CREA DATA) times the skill factor fDamageSkillBase 0.5
-- + fDamageSkillMult 0.5 x its combat skill 25 / 100, plus its melee damage, strength 7 x 0.5 floored: 12.5 + 3 = 15.5
local GHOUL_BLOW = 20 * (0.5 + 0.5 * 25 / 100) + math.floor(7 * 0.5)
local SETTLER = { NV, 0x104F03 }
local ARENA = { -69660, 1900, 8352 } -- open flat ground east of the doctor's house (round 14A)
local GANGERS = { x = -21860, y = -38900, z = 8100, ids = { 0x1629A4, 0x1629A3 } } -- cell -6,-10, guns (round 14A)
local HEALTH, DR, DT = 16, 18, 76

return { limit = 600, delay = 4, run = function(r)
    local h = require('scripts.roundhelpers')(r)
    local core = r.core
    local function phase(name) r.log('PHASE ' .. name) end
    local PLAYER = core.spawn.player()
    local player = r.player()

    local function hp() return h.command('GetActorValue', h.player(), HEALTH) end
    local function value(index)
        local ok, v = pcall(h.command, 'GetActorValue', h.player(), index)
        return ok and v or nil
    end
    -- Health back to full between the fights (the base: what ResetHealth gives)
    local function heal(label)
        if not pcall(h.command, 'ResetHealth', h.player()) then h.command('RestoreActorValue', h.player(), HEALTH, 1000) end
        r.wait(0.5)
        r.log(string.format('%s: health %.1f', label, hp()))
    end
    -- Watches the player's health for at most `seconds` (real), or until `want` drops were seen; gives the list of drops
    local function watch(label, seconds, want)
        local drops = {}
        local last = hp()
        local deadline = core.getRealTime() + seconds
        local started = core.getSimulationTime()
        while core.getRealTime() < deadline and #drops < want do
            r.wait(0.1)
            local now = hp()
            if now < last - 0.01 then
                drops[#drops + 1] = last - now
                r.log(string.format('drop %s %d: %.2f (%.1f -> %.1f) at %.1f s of the game', label, #drops, last - now, last, now,
                    core.getSimulationTime() - started))
            end
            last = now
        end
        r.log(string.format('watch %s: %d drops in %.1f s of the game', label, #drops, core.getSimulationTime() - started))
        return drops
    end

    ---------------------------------------------------------------------------------------------------------------- S: setup
    phase('S setup')
    r.god(false)
    local mojave
    for _, id in ipairs({ 0x0DAEB9, 0x0DAEB8, 0x0DAEBB }) do
        local ok, cell = pcall(function() return r.world.getCellById(r.core.getFormId(NV, id)) end)
        if ok and cell then mojave = cell break end
    end
    if mojave == nil then error('no cell of the Mojave was found') end
    r.teleport{ pos = ARENA, worldspace = mojave, yaw = 0, onGround = true, label = 'arena' }
    r.wait(10)
    r.settle(3, 30)
    r.closeDialogue('arena')
    -- nothing worn: the blows reach the health whole
    h.command('RemoveAllItems', h.player())
    r.wait(1)
    r.log(string.format('S player DR=%s DT=%s', tostring(value(DR)), tostring(value(DT))))
    h.check('s-dr-0', value(DR) or -1, 0)
    h.check('s-dt-0', value(DT) or -1, 0)
    heal('S')

    ---------------------------------------------------------------------------------------------------------------- M: melee NPC
    phase('M melee')
    local settler = h.object(SETTLER, 'settler')
    r.place(settler, { dist = 250, bearing = math.pi / 2, label = 'settler' })
    r.wait(1)
    r.inventory('settler', settler)
    r.fight(settler, player, 'settler -> player')
    r.track({ { label = 'settler', actor = settler } }, 1)
    local blows = watch('settler', 40, 2)
    r.untrack()
    r.state('settler after', settler)
    h.checkAtLeast('m-settler-hits-at-least-1', #blows, 1)
    h.checkAtLeast('m-settler-blow-above-0', blows[1] or 0, 0.5)
    r.peace(settler, 'settler')
    r.wait(1)
    heal('M')

    ---------------------------------------------------------------------------------------------------------------- F: gunfire
    phase('F gunfire')
    r.teleport{ pos = { GANGERS.x, GANGERS.y, GANGERS.z }, worldspace = mojave, yaw = 0, onGround = true, label = 'gangers' }
    r.wait(10)
    r.settle(3, 30)
    r.closeDialogue('gangers')
    heal('F')
    local shooters = {}
    for _, id in ipairs(GANGERS.ids) do
        local ok, ganger = pcall(r.lookup, { NV, id })
        if ok and ganger ~= nil then
            shooters[#shooters + 1] = ganger
            r.place(ganger, { dist = 600, bearing = #shooters * 0.4, label = string.format('ganger 0x%x', id) })
        end
    end
    h.checkAtLeast('f-gangers-found', #shooters, 1)
    r.wait(1)
    for _, ganger in ipairs(shooters) do r.fight(ganger, player, 'ganger -> player') end
    local shots = watch('gangers', 40, 2)
    for _, ganger in ipairs(shooters) do
        r.state('ganger after', ganger)
        r.peace(ganger, 'ganger')
    end
    h.checkAtLeast('f-shots-hit-at-least-1', #shots, 1)
    h.checkAtLeast('f-shot-above-0', shots[1] or 0, 0.5)
    -- (the ghoul comes last: nothing can stop it)
    ---------------------------------------------------------------------------------------------------------------- G: ghoul
    phase('G ghoul')
    -- (a reference that PlaceAtMe makes is not an object of the Lua world: it is driven by its form id; 250 units in front)
    local made = core.spawn.run('PlaceAtMe', PLAYER, { core.getFormId(NV, GHOUL), 1, 250, 0 })
    h.check('g-ghoul-made', made ~= nil and made ~= 0, true)
    local ghoulId = 'FormId:' .. string.format('0x%x', made or 0)
    r.wait(1)
    -- (it attacks the player on sight: aggression 2, very aggressive; the script commands cannot name a reference that
    -- PlaceAtMe made, so the round does not start or stop its fight)
    r.log('G ghoul ' .. ghoulId)
    local drops = watch('ghoul', 40, 3)
    h.checkAtLeast('g-ghoul-hits-at-least-1', #drops, 1)
    -- each blow is the rule's (no armor, Normal difficulty); a critical blow adds its critical damage on top
    h.checkRange('g-first-blow-by-the-rule', drops[1] or -1, GHOUL_BLOW - 0.25, GHOUL_BLOW + 0.25)
    for i = 2, #drops do r.log(string.format('G blow %d: %.2f', i, drops[i])) end

    heal('F end')
    r.god(true)
    r.log('npc damage round done')
end }
