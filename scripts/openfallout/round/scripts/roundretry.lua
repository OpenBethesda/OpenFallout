-- A menu script of the rounds: a refused load is answered by asking for the same save again (rounds/m1-refused-retry.json).
-- Once the main menu has been up a moment with no game, it asks for the newest save with `missingContent = 'refuse'`. The save
-- wants a content file the round does not load, so the engine refuses it and calls `onLoadRefused`; the handler asks for the
-- same save with `missingContent = 'accept'`, once. Then the game runs, and the round's scenario with it.
--
-- It does nothing unless it is asked for: the engine only runs it when `--content roundretry.omwscripts` names it, and its folder
-- is a data folder of a round only (round.py gives the engine `--data` with the folder of the runner). It asks for one load at
-- most, only at the main menu with no game, and answers one refusal at most, of the save it asked for.
--
-- Its lines start with ROUND (round.py shows them in the report; a `ROUND error` line fails the round) and hold counts and
-- booleans, never a name of a save, a character or a file.
local core = require('openfallout.core')
local menu = require('openfallout.menu')

-- Real seconds the main menu stays up with no game before the load is asked for (the menu's first frames are left alone)
local DELAY = 2

local function log(...) print('ROUND', 'retry', ...) end
local function fail(...) print('ROUND', 'error', 'retry', ...) end

local menuSince = nil
local asked = nil       -- { directory, slotName } of the load it asked for
local retried = false

local function newestSave()
    local count, best = 0, nil
    for directory, saves in pairs(menu.getAllSaves()) do
        for slotName, info in pairs(saves) do
            count = count + 1
            local made = info.creationTime or 0
            if best == nil or made > best.made then
                best = { directory = directory, slotName = slotName, made = made }
            end
        end
    end
    return count, best
end

-- An error of the engine names the save: the line says only that the call failed
local function load(directory, slotName, answer)
    local ok = pcall(menu.loadGame, directory, slotName, { missingContent = answer })
    if not ok then fail('load not asked', 'missingContent=' .. answer) end
    return ok
end

local function onFrame()
    if asked ~= nil then return end
    if menu.getState() ~= menu.STATE.NoGame or not menu.mainMenuOpen() then
        menuSince = nil
        return
    end
    local now = core.getRealTime()
    if menuSince == nil then
        menuSince = now
        return
    end
    if now - menuSince < DELAY then return end
    local count, save = newestSave()
    if save == nil then
        asked = {}
        fail('no save', 'saves=' .. count)
        return
    end
    asked = save
    log('asked', 'saves=' .. count, 'missingContent=refuse')
    load(save.directory, save.slotName, 'refuse')
end

local function onLoadRefused(info)
    if asked == nil or asked.directory == nil then return end
    local same = info.directory == asked.directory and info.slotName == asked.slotName
    local missing = info.missingContent and #info.missingContent or 0
    if retried then
        -- the save was refused again after the answer `accept`: asking once more would be a loop
        fail('refused again', 'missing=' .. missing, 'same=' .. tostring(same))
        return
    end
    retried = true
    log('refused', 'missing=' .. missing, 'same=' .. tostring(same), 'asking=accept')
    load(info.directory, info.slotName, 'accept')
end

log('ready')

return {
    engineHandlers = {
        onFrame = onFrame,
        onLoadRefused = onLoadRefused,
    },
}
