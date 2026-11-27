-- A local script that a round attaches to a character (object:addScript, the CUSTOM tag of round.omwscripts). The event
-- RoundActorDrive switches the character's AI off (self:enableAI(false)) and sets its controls for some seconds of the game:
-- forward and side from -1 to 1, run, a turn (radians) at the start. RoundActorAI switches the AI back on (or off). Each change
-- is logged as a line that starts with ROUND.
local core = require('openfallout.core')
local self = require('openfallout.self')

local function log(...) print('ROUND', ...) end

local drive = nil

local function stop()
    self.controls.movement = 0
    self.controls.sideMovement = 0
    self.controls.run = false
end

return {
    engineHandlers = {
        onUpdate = function()
            if drive == nil then return end
            if core.getSimulationTime() >= drive.untilTime then
                stop()
                log('actor', 'drive', 'over', string.format('game=%.2f', core.getSimulationTime() - drive.from))
                drive = nil
                return
            end
            self.controls.movement = drive.forward
            self.controls.sideMovement = drive.side
            self.controls.run = drive.run
        end,
    },
    eventHandlers = {
        RoundActorDrive = function(data)
            self:enableAI(false)
            self.controls.yawChange = data.turn or 0
            drive = { forward = data.forward or 1, side = data.side or 0, run = data.run == true,
                from = core.getSimulationTime(), untilTime = core.getSimulationTime() + (data.seconds or 1) }
            log('actor', 'drive', string.format('forward=%.2f side=%.2f run=%s seconds=%.2f', drive.forward, drive.side,
                tostring(drive.run), data.seconds or 1))
        end,
        RoundActorAI = function(data)
            drive = nil
            stop()
            self:enableAI(data.on ~= false)
            log('actor', 'ai', tostring(data.on ~= false))
        end,
    },
}
