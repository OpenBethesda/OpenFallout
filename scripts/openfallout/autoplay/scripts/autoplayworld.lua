-- The world side of autoplay.lua: logs the stage of the quests of the intro of Fallout 3 and New Vegas whenever one changes,
-- does what the intro waits for when a quest stands still at a stage listed in autoplaygates.lua (walks the player into a
-- trigger volume, makes the player activate an object or shoot at one, or takes the player to another cell), and quits when
-- the intro is over or the time is up. Lines start with AUTOPLAY.
local core = require('openfallout.core')
local util = require('openfallout.util')
local world = require('openfallout.world')
local config = require('scripts.autoplaygates')

-- The quests of the two intros, by editor id, and the stage of the main quest at which the intro is over. The table of the gates
-- can name other stages at which the run ends (`finish`: quest and stage) and another time limit (`limit`, real seconds),
-- for a run that goes on past the intro.
local QUESTS = { 'CG00', 'CG01', 'CG02', 'CG03', 'CG04', 'CGTutorial', 'MQ01', 'VCG00', 'VCG01', 'VCG02', 'VCG03', 'VCG04',
    'VMQ01' }
local DONE = config.finish or { MQ01 = 10, VMQ01 = 10, CG04 = 200, VCG01 = 200 }
-- Real seconds after which the run ends whatever happens
local LIMIT = config.limit or 2400
-- The stage at which each intro is over, logged once when a run goes on past it (`intro over` is for the end of the run)
local INTRO_END = { CG04 = 200, VCG01 = 200 }
local introLogged = {}

local last, changed, tries = {}, {}, {}
-- The quests that are followed: those of the intros, and any other that a gate waits for
local watched, known = {}, {}
local function watch(name)
    if not known[name] then
        known[name] = true
        table.insert(watched, name)
    end
end
for _, name in ipairs(QUESTS) do watch(name) end
for _, gate in ipairs(config.gates) do watch(gate.quest) end
-- A quest that only ends the run is followed too
local finishing = {}
for name in pairs(DONE) do table.insert(finishing, name) end
table.sort(finishing)
for _, name in ipairs(finishing) do watch(name) end
-- What stops the world, told when it changes: the tags that pause it (the screens do, with the tag ui)
local pausedNow = ''
local elapsed = 0
local started = core.getRealTime()
local lastGate = 0
-- The gates that wait for a talk to end, logged once each ('AUTOPLAY talk wait')
local waitLogged = {}

-- The one the player talks with (as core.getFormId writes it), or nil when no talk is open
local function talkingWith()
    if not core.conversation.available() then return nil end
    local state = core.conversation.get()
    if not state.active or state.speakerId == '' then return nil end
    return state.speakerId
end

-- Whether the gate activates a character while the player talks with another: the engine starts no second talk, so the
-- activation would do nothing and use up a try. Such a gate waits until the talk is over (autoplay.lua ends it when it
-- has picked its topics), as a player says goodbye before turning to someone else. An object that is no character (a
-- terminal, a seat) is activated as before, also in a talk.
local function waitsForTalk(gate)
    if gate.how ~= 'activate' or gate.refs then return false end
    local speaker = talkingWith()
    if speaker == nil then return false end
    local form = core.getFormId(gate.file, gate.ref)
    if speaker == form then return false end
    local object = world.getObjectByFormId(form)
    for _, actor in ipairs(world.activeActors) do
        if actor == object then return true, speaker end
    end
    return false
end

-- Does what the gate says: the player goes to the middle of the reference (a trigger volume), or activates it (or each of
-- the `refs`), or is told to shoot at it (the player script fires: the weapon is not a thing of the world script), or is
-- taken to another cell (a place the data has no door to), or the quest is put at a stage (for what the engine cannot bring
-- about yet)
local function useGate(index, gate, real)
    tries[index] = (tries[index] or 0) + 1
    lastGate = real
    local ok, err = pcall(function()
        if gate.how == 'setstage' then
            if not world.quests.find(gate.quest):setStage(gate.to) then error('not set') end
            return
        end
        local player = world.players[1]
        if gate.how == 'teleport' then
            local cell = world.getCellById(core.getFormId(gate.file, gate.cell))
            player:teleport(cell, util.vector3(gate.at[1], gate.at[2], gate.at[3]), { onGround = true })
            return
        end
        if gate.refs then
            for _, ref in ipairs(gate.refs) do
                world.getObjectByFormId(core.getFormId(gate.file, ref)):activateBy(player)
            end
            return
        end
        local object = world.getObjectByFormId(core.getFormId(gate.file, gate.ref))
        if gate.how == 'activate' then
            object:activateBy(player)
        elseif gate.how == 'fire' then
            player:sendEvent('AutoplayFire', { target = object, form = core.getFormId(gate.file, gate.ref),
                shots = gate.shots or 3, untilDead = gate.untilDead == true })
        else
            -- A walk keeps the way the player faces, which a conversation has turned toward whoever spoke (the speaker can be
            -- far off); a gate with `face` turns the player to that reference (of the same plugin) first, as a player who
            -- walks to a place looks at what the place is for
            local options
            if gate.face then
                local target = world.getObjectByFormId(core.getFormId(gate.file, gate.face))
                local away = target.position - object.position
                options = { rotation = util.transform.rotateZ((math.atan2 or math.atan)(away.x, away.y)) }
            end
            player:teleport(object.cell, object.position, options)
        end
    end)
    local target
    if gate.how == 'setstage' then
        target = string.format('to=%d', gate.to)
    elseif gate.how == 'teleport' then
        target = string.format('cell=%06X', gate.cell)
    else
        target = string.format('ref=%06X', gate.ref or gate.refs[1])
    end
    print('AUTOPLAY', 'gate', gate.how, gate.quest, string.format('stage=%d %s try=%d', gate.stage, target, tries[index]),
        ok and 'ok' or tostring(err))
end

return {
    engineHandlers = {
        onUpdate = function(dt)
            elapsed = elapsed + dt
            local real = core.getRealTime() - started
            local tags = {}
            for tag in pairs(world.getPausedTags()) do table.insert(tags, tag) end
            table.sort(tags)
            local paused = string.format('paused=%s tags=%s', tostring(core.isWorldPaused()), table.concat(tags, ','))
            if paused ~= pausedNow then
                pausedNow = paused
                print('AUTOPLAY', 'world', paused, string.format('real=%.1f', real))
            end
            for _, name in ipairs(watched) do
                local quest = world.quests.find(name)
                if quest then
                    local state = string.format('stage=%d running=%s', quest.stage, tostring(quest.running))
                    if last[name] ~= state then
                        last[name] = state
                        changed[name] = real
                        print('AUTOPLAY', 'quest', name, state, string.format('game=%.1f real=%.1f', elapsed, real))
                    end
                    if INTRO_END[name] and quest.stage >= INTRO_END[name] and not introLogged[name] then
                        introLogged[name] = true
                        print('AUTOPLAY', 'intro done', name, string.format('real=%.1f', real))
                    end
                    if DONE[name] and quest.stage >= DONE[name] then
                        print('AUTOPLAY', 'intro over', name, string.format('real=%.1f', real))
                        core.quit()
                    end
                end
            end
            -- a quest that stands still at the stage of a gate: the intro waits for the player
            for index, gate in ipairs(config.gates) do
                local quest = world.quests.find(gate.quest)
                if quest and quest.stage == gate.stage and (tries[index] or 0) < (gate.tries or config.tries)
                    and real - (changed[gate.quest] or real) >= (gate.settle or config.settle)
                    and real - lastGate >= (gate.gap or config.settle) then
                    -- an object that is not there is the business of useGate, which logs it
                    local ok, waits, speaker = pcall(waitsForTalk, gate)
                    if not ok and not waitLogged[index] then
                        waitLogged[index] = true
                        print('AUTOPLAY', 'talk wait error', gate.quest, string.format('stage=%d', gate.stage),
                            tostring(waits))
                    end
                    if ok and waits then
                        if not waitLogged[index] then
                            waitLogged[index] = true
                            print('AUTOPLAY', 'talk wait', gate.quest, string.format('stage=%d ref=%06X', gate.stage,
                                gate.ref), 'talk=' .. speaker)
                        end
                    else
                        useGate(index, gate, real)
                    end
                end
            end
            if real >= LIMIT then
                print('AUTOPLAY', 'time is up')
                core.quit()
            end
        end,
    },
}
