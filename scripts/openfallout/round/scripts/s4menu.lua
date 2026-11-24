-- The menu side of the rounds of step 4 (a menu script is the only kind that may save the game): a scenario asks for a save with
-- the event S4Save (roundworld.lua: r.saveGame) and the save is written under that description. The line says whether it was
-- written; the engine says which file in its own log. It does nothing until it is asked.
local menu = require('openfallout.menu')

return {
    eventHandlers = {
        S4Save = function(data)
            local ok, err = pcall(menu.saveGame, data.description)
            print('ROUND', 's4save', tostring(data.description), ok and 'ok' or ('error ' .. tostring(err)))
        end,
    },
}
