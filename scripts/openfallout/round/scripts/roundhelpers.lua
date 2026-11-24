-- Round helpers for scenarios that hold a conversation, press a message-box button, read the state of a quest or of an actor, and wait
-- for real seconds and game hours (step 4, the companion round of Hardcore). A scenario asks for them once:
--
--     local h = require('scripts.roundhelpers')(r)
--
-- `r` is the scenario player (roundworld.lua); the helpers are made of its steps and of the engine's own calls (core.conversation,
-- core.messageBox, core.combat.command), so they run in the scenario's coroutine and the ones that wait must be called from the
-- scenario function itself, not from a pcall (the same rule as r.wait).
--
-- Every helper logs ONE line that starts with `ROUND<tab>h.<name><tab>ok` and holds numbers and form ids, never a text of the
-- game. A helper that cannot do its job raises error() with the reason: the scenario ends and the round fails with
-- `ROUND error scenario ...`. A CHECK is different: `h.check` logs `ROUND<tab>check<tab><label><tab>ok` when the value is the one
-- the rules say, and `ROUND<tab>error<tab>check<tab><label><tab>FAIL ...` when it is not, and the scenario goes on, so that one run
-- shows every check that failed (the round fails on the first line).
--
-- Forms are given as `{ 'FalloutNV.esm', 0x135F19 }`, as the text core.getFormId writes ("FormId:0x..."), or as a bare number,
-- which is a record of `h.file` (FalloutNV.esm).
local core = require('openfallout.core')

return function(r)
    local h = {}
    -- the plugin that a bare number names
    h.file = 'FalloutNV.esm'

    local function log(...) print('ROUND', ...) end

    -- A helper that cannot do its job: the scenario ends (roundworld.lua logs `ROUND error scenario <message>`)
    local function fail(name, ...)
        local parts = {}
        for i = 1, select('#', ...) do parts[i] = tostring((select(i, ...))) end
        error('h.' .. name .. ': ' .. table.concat(parts, ' '), 0)
    end

    ---------------------------------------------------------------------------------------------------------------- forms

    -- The form id as text, as core.combat and core.conversation write it
    function h.form(spec)
        if type(spec) == 'userdata' then
            return r.form(spec) -- an object that the scenario holds
        elseif type(spec) == 'table' then
            return core.getFormId(spec[1], spec[2])
        elseif type(spec) == 'number' then
            return core.getFormId(h.file, spec)
        end
        return tostring(spec)
    end

    -- The number inside a form id text ("FormId:0x1135f19" -> 0x1135f19), nil when there is none
    local function formNumber(text)
        local hex = tostring(text):lower():match('0x(%x+)')
        return hex and tonumber(hex, 16) or nil
    end
    h.formNumber = formNumber

    local function hex(number) return number and string.format('0x%x', number) or 'nil' end

    -- The object of a reference (an ACHR, an ACRE, a REFR), or fail
    function h.object(spec, name)
        local ok, object = pcall(r.lookup, h.form(spec))
        if not ok or object == nil then fail(name or 'object', 'there is no reference', tostring(h.form(spec)), tostring(object)) end
        return object
    end

    -- The player's own reference, as core.combat writes it
    function h.player() return core.combat.player() end

    ----------------------------------------------------------------------------------------------- script commands, states

    -- A command of the script language by name, on a reference (an actor spec, or nil/'' for none) with numbers or forms as
    -- arguments. The value comes back at once (core.combat.command); a command the table does not know is NaN
    function h.command(name, actor, ...)
        local reference = actor ~= nil and actor ~= '' and h.form(actor) or ''
        local arguments = { ... }
        for i, argument in ipairs(arguments) do
            if type(argument) == 'table' then arguments[i] = h.form(argument) end
        end
        local value = core.combat.command(name, reference, table.unpack(arguments))
        if value ~= value then fail('command', name, 'is not a command of this game') end
        return value
    end

    local function flag(name, actor) return h.command(name, actor) ~= 0 end

    function h.isTeammate(actor) return flag('GetPlayerTeammate', actor) end
    function h.isEssential(actor) return flag('IsEssential', actor) end
    function h.isDead(actor) return flag('GetDead', actor) end
    function h.isKnockedOut(actor) return flag('GetUnconscious', actor) end

    -- The health of a character: the actor value Health (16) as GetActorValue gives it, and GetHealthPercentage; nil for what a
    -- command does not answer
    function h.health(actor)
        local okValue, current = pcall(h.command, 'GetActorValue', actor, 16)
        local okPercent, percent = pcall(h.command, 'GetHealthPercentage', actor)
        return okValue and current or nil, okPercent and percent or nil
    end

    local function bit(value) return value and 1 or 0 end

    -- One line with the four states of an actor (and its health); gives them as a table (booleans, hp, hpPercent)
    function h.state(actor, label)
        local state = {
            teammate = h.isTeammate(actor),
            essential = h.isEssential(actor),
            dead = h.isDead(actor),
            knockedOut = h.isKnockedOut(actor),
        }
        local current, percent = h.health(actor)
        state.hp, state.hpPercent = current, percent
        log('h.state', 'ok', tostring(label or '-'), 'teammate=' .. bit(state.teammate), 'essential=' .. bit(state.essential),
            'dead=' .. bit(state.dead), 'knockedOut=' .. bit(state.knockedOut),
            'hp=' .. (current and string.format('%.1f', current) or 'n/a'),
            'hpPercent=' .. (percent and string.format('%.3f', percent) or 'n/a'))
        return state
    end

    function h.teammateCount() return h.command('GetPlayerTeammateCount') end

    -- Puts items in the inventory of an actor (the player by default) as the AddItem of a script does (the inventory the script
    -- language and the Pip-Boy read; r.give puts them in the Lua one)
    function h.addItem(item, count, actor)
        h.command('AddItem', actor or h.player(), item, count or 1)
        log('h.addItem', 'ok', hex(formNumber(h.form(item))), tostring(count or 1))
    end

    -- The number of items of a record that an actor carries (the player by default)
    function h.itemCount(item, actor)
        return h.command('GetItemCount', actor or h.player(), item)
    end

    -- A check: the value is the one the rules say. A failure is a `ROUND error` line (the round fails) and the scenario goes on.
    local function show(value)
        if type(value) == 'boolean' then return value and '1' or '0' end
        if type(value) == 'number' then return string.format('%g', value) end
        return tostring(value)
    end
    function h.check(label, actual, wanted)
        if type(actual) == 'boolean' then actual = actual and 1 or 0 end
        if type(wanted) == 'boolean' then wanted = wanted and 1 or 0 end
        if actual == wanted then
            log('check', label, 'ok', 'actual=' .. show(actual), 'want=' .. show(wanted))
            return true
        end
        r.error('check', label, 'FAIL', 'actual=' .. show(actual), 'want=' .. show(wanted))
        return false
    end
    -- A number of at least `minimum`
    function h.checkAtLeast(label, actual, minimum)
        if type(actual) == 'number' and actual >= minimum then
            log('check', label, 'ok', 'actual=' .. show(actual), 'want>=' .. show(minimum))
            return true
        end
        r.error('check', label, 'FAIL', 'actual=' .. show(actual), 'want>=' .. show(minimum))
        return false
    end
    -- A number within [low, high]
    function h.checkRange(label, actual, low, high)
        if type(actual) == 'number' and actual >= low and actual <= high then
            log('check', label, 'ok', 'actual=' .. show(actual), string.format('want=%g..%g', low, high))
            return true
        end
        r.error('check', label, 'FAIL', 'actual=' .. show(actual), string.format('want=%g..%g', low, high))
        return false
    end

    ---------------------------------------------------------------------------------------------------------------- waiting

    -- Real seconds (a line of its own, so that the report shows where the time went)
    function h.waitSeconds(seconds, label)
        local before = core.getRealTime()
        r.wait(seconds)
        log('h.waitSeconds', 'ok', tostring(label or '-'), string.format('asked=%.1f real=%.1f', seconds, core.getRealTime() - before))
    end

    -- Game hours. With `rest = true` the player waits in the game's own Wait screen (the clock jumps; `r.rest`); otherwise the
    -- scenario lets the game run and watches its clock, at most `timeout` real seconds (the clock runs `TimeScale` times
    -- faster than the real one: 30 by default, so an hour takes 120 s). Gives the game hours that went by.
    function h.waitGameHours(hours, o)
        o = o or {}
        local before = r.clock()
        if before == nil then fail('waitGameHours', 'the game clock cannot be read') end
        local startedAt = core.getRealTime()
        if o.rest then
            local reply = r.rest{ hours = hours, label = o.label or 'waitGameHours', timeout = o.timeout }
            if reply == nil or not reply.started then fail('waitGameHours', 'the Wait screen did not start') end
        else
            local scale = core.getGameTimeScale and core.getGameTimeScale() or 30
            local timeout = o.timeout or (hours * 3600 / math.max(scale, 0.01)) * 1.5 + 30
            local reached = r.waitFor(function()
                local now = core.getGameTime and (core.getGameTime() / 3600) or nil
                return now ~= nil and now - before >= hours
            end, timeout, 'game hours')
            if not reached then fail('waitGameHours', 'the clock did not move', tostring(hours), 'hours within', timeout, 's') end
        end
        local passed = (r.clock() or before) - before
        log('h.waitGameHours', 'ok', tostring(o.label or '-'), string.format('asked=%.2f passed=%.2f real=%.1f', hours, passed,
            core.getRealTime() - startedAt), 'mode=' .. (o.rest and 'rest' or 'clock'))
        return passed
    end

    ---------------------------------------------------------------------------------------------------------------- quests

    local function quest(name, fn)
        local found = r.world.quests.find(tostring(name))
        if found == nil then fail(fn, 'there is no quest', tostring(name)) end
        return found
    end

    -- The stage a quest is at (the one that was set last, 0 if none), whether it is running, completed or failed
    function h.questStage(name)
        local q = quest(name, 'questStage')
        log('h.questStage', 'ok', tostring(name), 'stage=' .. tostring(q.stage), 'running=' .. tostring(q.running),
            'completed=' .. tostring(q.completed), 'failed=' .. tostring(q.failed))
        return q.stage, q
    end

    -- Puts a quest at a stage as a script's SetStage does (the result scripts of the stage run). Fails when the quest has no such
    -- stage or the stage cannot be repeated and the quest is not at it
    function h.setQuestStage(name, stage)
        local q = quest(name, 'setQuestStage')
        local before = q.stage
        local done = q:setStage(stage)
        if done == false and q.stage ~= stage then
            fail('setQuestStage', tostring(name), 'could not be put at stage', stage, 'it is at', q.stage)
        end
        log('h.setQuestStage', 'ok', tostring(name), 'before=' .. tostring(before), 'now=' .. tostring(q.stage))
        return q.stage
    end

    function h.objectiveDisplayed(name, objective)
        local q = quest(name, 'objectiveDisplayed')
        local shown = q:objectiveDisplayed(objective) == true
        log('h.objectiveDisplayed', 'ok', tostring(name), 'objective=' .. tostring(objective), 'displayed=' .. bit(shown))
        return shown
    end

    -- The variables of a quest's script (VNPCFollowers.bCassHired, VCassCompanion's own) are NOT readable or settable from Lua on
    -- master: the interpreter reads them for conditions (GetQuestVariable, answered by the condition evaluator) and scripts, and no
    -- binding, command or world command gives them to a script of Lua. Rather than answer 0, which would pass a check that
    -- should fail, the helper fails. The companion round reads what the variables cause instead (the teammate flag, the quest
    -- stage that OnDeath sets, an objective that a result script shows).
    function h.questVariable(name, variable)
        fail('questVariable', 'cannot read', tostring(name) .. '.' .. tostring(variable), '- master has no Lua or script command that reads a variable of a quest script;',
            'use the effect of the variable (h.isTeammate, h.questStage, h.objectiveDisplayed)')
    end
    function h.setQuestVariable(name, variable)
        fail('setQuestVariable', 'cannot set', tostring(name) .. '.' .. tostring(variable), '- master has no Lua or script command that writes a variable of a quest script')
    end

    ---------------------------------------------------------------------------------------------------------- conversation

    local function view() return core.conversation.get() end

    local function topicList(state, limit)
        local list = {}
        for i, option in ipairs(state.options or {}) do
            if i > (limit or 12) then list[#list + 1] = '...' break end
            list[#list + 1] = hex(formNumber(option.topic))
        end
        return table.concat(list, ',')
    end

    -- What the conversation is now, as numbers and ids: active, speaking, ended, the speaker's reference, the number of options
    -- and their topics (the same list `core.conversation.get().options` has)
    function h.conversation(label)
        local state = view()
        log('h.conversation', 'ok', tostring(label or '-'), 'active=' .. bit(state.active), 'speaking=' .. bit(state.speaking),
            'ended=' .. bit(state.ended), 'speaker=' .. hex(formNumber(state.speakerId)), 'options=' .. #(state.options or {}),
            'topics=' .. topicList(state))
        return state
    end

    -- Waits until the response that is being said is over (the window of the conversation reads it to its end) and the options
    -- are there, or the conversation is over. `timeout` real seconds (60)
    function h.waitSpoken(timeout, label)
        local ok = r.waitFor(function()
            local state = view()
            return not state.active or not state.speaking
        end, timeout or 60, 'spoken ' .. tostring(label or '-'))
        if not ok then fail('waitSpoken', 'the response was still being said after', timeout or 60, 's') end
        return view()
    end

    -- The player talks with an actor: activates it and waits until the conversation is open, the speaker is the actor and the greeting
    -- has been said (so that the topics can be picked). Gives the state of the conversation.
    function h.talk(actor, o)
        o = o or {}
        local object = h.object(actor, 'talk')
        if view().active then fail('talk', 'a conversation is open already (speaker', hex(formNumber(view().speakerId)) .. ')') end
        local startedAt = core.getRealTime()
        object:activateBy(r.player())
        local opened = r.waitFor(function() return view().active end, o.timeout or 20, 'conversation opens')
        if not opened then fail('talk', 'no conversation opened after the player activated', hex(formNumber(h.form(actor)))) end
        local state = view()
        local wanted = formNumber(h.form(actor))
        local speaker = formNumber(state.speakerId)
        if speaker ~= wanted then fail('talk', 'the speaker is', hex(speaker), 'not', hex(wanted)) end
        state = h.waitSpoken(o.timeout or 60, 'greeting')
        if not state.active then fail('talk', 'the conversation ended with the greeting') end
        log('h.talk', 'ok', 'speaker=' .. hex(speaker), 'options=' .. #(state.options or {}), 'topics=' .. topicList(state),
            string.format('real=%.1f', core.getRealTime() - startedAt))
        return state
    end

    -- Picks a topic of the conversation: `topic` (a form, the number the data names it by), or `text` (a part of the words of the
    -- topic, any case; the words are matched and never logged), or `index` (the place in the list, from 1). Waits for the previous
    -- response to be over first. Fails when there is no conversation or no such topic. Gives the option picked.
    function h.pick(o)
        local state = h.waitSpoken(o.timeout or 60, 'before pick')
        if not state.active then fail('pick', 'the conversation is over') end
        local options = state.options or {}
        local found, how
        if o.index ~= nil then
            found, how = options[o.index], 'index=' .. o.index
        elseif o.topic ~= nil then
            local wanted = formNumber(h.form(o.topic))
            how = 'topic=' .. hex(wanted)
            for i, option in ipairs(options) do
                if formNumber(option.topic) == wanted then found = i break end
            end
            found = found and options[found]
        elseif o.text ~= nil then
            local needle = tostring(o.text):lower()
            how = 'text'
            for _, option in ipairs(options) do
                if tostring(option.text):lower():find(needle, 1, true) then found = option break end
            end
        else
            fail('pick', 'needs topic, text or index')
        end
        if not found then fail('pick', 'no option for', how, '- the conversation offers', #options, 'topics:', topicList(state, 20)) end
        local index
        for i, option in ipairs(options) do if option == found then index = i break end end
        local after = core.conversation.choose(found.topic)
        -- `finish = true`: the response is over at once (the window would end it when the lines are read; its end script runs here
        -- as it does there). For a pick whose words are not what a round looks at.
        if o.finish then
            -- (a response with a follow-up says the next one when it is over: finish those too, six at most)
            for _ = 1, 6 do
                if not (after.active and after.speaking) then break end
                after = core.conversation.finish()
            end
        end
        log('h.pick', 'ok', 'topic=' .. hex(formNumber(found.topic)), 'index=' .. tostring(index), 'options=' .. #options,
            'by=' .. how, 'finish=' .. bit(o.finish), 'speaking=' .. bit(after.speaking), 'ended=' .. bit(after.ended))
        return found
    end

    -- Picks the topics one after the other (each a form, or a table for h.pick)
    function h.pickAll(topics)
        for _, topic in ipairs(topics) do
            local isOptions = type(topic) == 'table' and (topic.topic ~= nil or topic.text ~= nil or topic.index ~= nil)
            h.pick(isOptions and topic or { topic = topic })
        end
    end

    -- Waits until the conversation is over (a response that ends it, or a close). Fails after `timeout` real seconds (60)
    function h.waitConversationEnd(timeout)
        local startedAt = core.getRealTime()
        local ok = r.waitFor(function() return not view().active end, timeout or 60, 'conversation ends')
        if not ok then fail('waitConversationEnd', 'the conversation was still open after', timeout or 60, 's; options:', topicList(view(), 20)) end
        log('h.waitConversationEnd', 'ok', string.format('real=%.1f', core.getRealTime() - startedAt))
    end

    -- Ends the conversation as the Goodbye of the window does
    function h.closeConversation()
        local before = view().active
        core.conversation.close()
        log('h.closeConversation', 'ok', 'wasActive=' .. bit(before))
    end

    ---------------------------------------------------------------------------------------------------------- message boxes

    local function boxState()
        local ok, box = pcall(core.messageBox.current)
        if not ok or box == nil then return nil end
        return box
    end

    -- The box that is open: number, message form, number of buttons (never the words); nil when none is open
    function h.box(label)
        local box = boxState()
        if box == nil then
            log('h.box', 'ok', tostring(label or '-'), 'open=0')
            return nil
        end
        log('h.box', 'ok', tostring(label or '-'), 'open=1', 'serial=' .. tostring(box.serial), 'form=' .. tostring(box.form),
            'buttons=' .. #box.buttons)
        return box
    end

    -- Waits until a box is open (the scenario's own answers are off while it waits, so that the box stays) and gives it
    function h.waitBox(timeout)
        r.boxes(false)
        local ok = r.waitFor(function() return boxState() ~= nil end, timeout or 30, 'box opens')
        if not ok then
            r.boxes(true)
            fail('waitBox', 'no message box opened within', timeout or 30, 's')
        end
        return boxState()
    end

    -- Presses a button of the box that is open (waiting for one to open, `timeout` seconds, 30): `index` is the place among the buttons
    -- that are shown, from 1 (what core.messageBox.press takes), `text` a part of the button's words (any case), `last` the last
    -- button. `form` says which message the box must show (a box of another message is a failure). The boxes are answered by the
    -- scenario player again afterwards.
    function h.pressBox(o)
        local box = h.waitBox(o.timeout)
        if o.form ~= nil and formNumber(box.form) ~= formNumber(h.form(o.form)) then
            r.boxes(true)
            fail('pressBox', 'the box shows', tostring(box.form), 'not', tostring(h.form(o.form)))
        end
        local index, how
        if o.index ~= nil then
            index, how = o.index, 'index=' .. o.index
        elseif o.last then
            index, how = #box.buttons, 'last'
        elseif o.text ~= nil then
            how = 'text'
            local needle = tostring(o.text):lower()
            for i, button in ipairs(box.buttons) do
                if tostring(button):lower():find(needle, 1, true) then index = i break end
            end
        else
            r.boxes(true)
            fail('pressBox', 'needs index, last or text')
        end
        if index == nil or index < 1 or index > #box.buttons then
            r.boxes(true)
            fail('pressBox', 'the box has no button for', how, '- it has', #box.buttons, 'buttons')
        end
        local pressed = core.messageBox.press(index)
        r.boxes(true)
        if not pressed then fail('pressBox', 'the game refused the button', index) end
        log('h.pressBox', 'ok', 'serial=' .. tostring(box.serial), 'form=' .. tostring(box.form), 'buttons=' .. #box.buttons,
            'index=' .. index, 'by=' .. how)
        return index
    end

    ------------------------------------------------------------------------------------------------------------ kill and hardcore

    -- Kills an actor as the game's own Kill (KillActor) does, the player being the killer unless `killer` says another. An essential
    -- character is not killed by it: the rules put it down. Gives the value the command gave.
    function h.kill(actor, killer)
        local id = h.form(actor)
        local by = killer ~= nil and h.form(killer) or h.player()
        local value
        for _, name in ipairs({ 'KillActor', 'Kill' }) do
            value = core.combat.command(name, id, by)
            if value == value then
                log('h.kill', 'ok', 'actor=' .. hex(formNumber(id)), 'command=' .. name, 'value=' .. show(value))
                return value
            end
        end
        fail('kill', 'neither KillActor nor Kill is a command of this game')
    end

    -- Blows of a weapon (the form of a pistol, say) at an actor until it is dead or falls, `limit` blows (20), 0.15 s apart. Gives the
    -- last row core.combat.strike gave ({ hit, damage, killed, downed, ... }) and how many blows it took. A character that neither dies
    -- nor falls is a failed helper (error()); with `soft = true` it is a `ROUND error` line instead, and the scenario goes on.
    function h.strike(actor, weapon, o)
        o = o or {}
        local id = h.form(actor)
        local attacker = o.attacker ~= nil and h.form(o.attacker) or h.player()
        local row, n
        for blow = 1, o.limit or 20 do
            local ok, result = pcall(core.combat.strike, attacker, id, weapon and h.form(weapon) or '', o.limb or -1)
            if not ok then
                if o.soft then
                    r.error('h.strike', 'blow ' .. blow .. ' failed: ' .. tostring(result))
                    return nil, blow
                end
                fail('strike', 'blow', blow, 'failed:', tostring(result))
            end
            row, n = result, blow
            if result.killed or result.downed then break end
            r.wait(0.15)
        end
        if not (row.killed or row.downed) then
            if o.soft then
                r.error('h.strike', 'the actor neither died nor fell in ' .. n .. ' blows')
                return row, n
            end
            fail('strike', 'the actor neither died nor fell in', n, 'blows')
        end
        log('h.strike', 'ok', 'actor=' .. hex(formNumber(id)), 'blows=' .. n, 'killed=' .. bit(row.killed), 'downed=' .. bit(row.downed),
            string.format('damage=%.1f', row.damage or 0))
        return row, n
    end

    -- The Hardcore mode on or off, the way the game's own switch does it (the Gameplay screen's Hardcore Mode and the box that
    -- Doc Mitchell's last line shows both end in SetHardcore, and so does core.interact.setHardcore, which r.hardcore calls).
    -- Fails when the mode is not what was asked afterwards.
    function h.setHardcore(on)
        local reply = r.hardcore(on == true, 'h.setHardcore')
        if reply == nil or reply.on ~= (on == true) then
            fail('setHardcore', 'the mode is', tostring(reply and reply.on), 'after asking for', tostring(on == true))
        end
        log('h.setHardcore', 'ok', 'on=' .. bit(reply.on))
        return reply.on
    end

    return h
end
