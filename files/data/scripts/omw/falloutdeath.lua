-- The death of the player in Fallout 3 and New Vegas, as the games' code does it (FalloutNV.exe 0x93E840, Fallout3.exe
-- 0x788350; rule D.D2 of docs/openfallout/fallout-rules.md). While the player is dying or dead a timer runs down from
-- fPlayerDeathReloadTime (5 in both executables, in neither ESM) in real time; when it is over the most recent save is
-- loaded, or the main menu opens when there is no save. Only when the setting is 0 or below does the game ask instead, in
-- a message box that says sMiscPlayerDeadMessage with two buttons, Reload (sMiscPlayerDeadLoadOption) and Main Menu
-- (sMiscPlayerDeadMenuOption), and waits for the answer. What dead is, is the engine's (core.pipboy.playerDead); the box
-- is the message box of the game's own file (falloutmessage.lua).
local core = require('openfallout.core')
local I = require('openfallout.interfaces')

local pipboy = core.pipboy

if not pipboy.available() then
    return {}
end

-- Real seconds since the player was seen dead, and whether the box has been asked for since
local deadFor = 0
local shown = false

-- What the game's settings say (the stand-in words of the port when the install has none)
local function word(name, fallback)
    local ok, text = pcall(pipboy.text, name)
    if ok and type(text) == 'string' and text ~= '' then return text end
    return fallback
end

-- The exe's two ends of the timer: the most recent save, or the main menu when there is none
local function reload()
    shown = true
    print(string.format('the death timer is over after %.2f s: %s', deadFor,
        pipboy.hasRecentSave() and 'the newest save loads' or 'the main menu opens'))
    if pipboy.hasRecentSave() then
        pipboy.reloadRecent()
    else
        pipboy.openMainMenu()
    end
end

-- The branch of a setting of 0 or below: the box with its two buttons (no timer, no key of its own)
local function ask()
    shown = true
    if not pipboy.hasRecentSave() then
        pipboy.openMainMenu()
        return
    end
    local asked = I.FalloutMessage ~= nil and I.FalloutMessage.ask(
        word('sMiscPlayerDeadMessage', 'Reloading the most recent save game'),
        { word('sMiscPlayerDeadLoadOption', 'Reload'), word('sMiscPlayerDeadMenuOption', 'Main Menu') },
        function(index)
            if index == 1 then
                pipboy.reloadRecent()
            elseif index == 2 then
                pipboy.openMainMenu()
            elseif index == nil then
                -- (the box could not be shown after all, when the game's message file is not usable)
                pipboy.askReload()
            end
        end)
    -- (an install without the game's message file: the engine's own question about loading the last save)
    if not asked then pipboy.askReload() end
end

return {
    engineHandlers = {
        onFrame = function()
            if not pipboy.playerDead() then
                shown = false
                deadFor = 0
                return
            end
            if shown then return end
            local reloadTime = pipboy.setting('fPlayerDeathReloadTime') or 5
            if deadFor == 0 then print(string.format('the player is dead (fPlayerDeathReloadTime %g)', reloadTime)) end
            if reloadTime <= 0 then
                ask()
                return
            end
            -- The world goes on while the player lies there (the time is the real one: the game may be paused)
            deadFor = deadFor + core.getRealFrameDuration()
            if deadFor >= reloadTime then
                reload()
            end
        end,
    },
}
